/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Wrapper on the XRFDC bare metal function class for rogue access
 * ----------------------------------------------------------------------------
 * Complementary mapping to Rfdc, RfdcTile, and RfdcBlock python classes
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcBlock.py
 * ----------------------------------------------------------------------------
 * This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the 'axi-soc-ultra-plus-core', including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
 **/

#include "rogue/Directives.h"

#include "PyRFdc.h"
#include "xrfdc_hw.h"

#include <inttypes.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifndef __BAREMETAL__
#include <openssl/sha.h>
#endif

#include <metal/log.h>

#include "rogue/GeneralError.h"
#include "rogue/GilRelease.h"
#include "rogue/interfaces/memory/Constants.h"
#include "rogue/interfaces/memory/Transaction.h"
#include "rogue/interfaces/memory/TransactionLock.h"

namespace rim = rogue::interfaces::memory;

#ifndef NO_PYTHON
    #include <boost/python.hpp>
namespace bp = boost::python;
#endif

#ifdef __BAREMETAL__
#define RFDC_DEVICE_ID  XPAR_XRFDC_0_DEVICE_ID
#else
#define RFDC_DEVICE_ID  0
#endif

#ifdef __BAREMETAL__
#define printf xil_printf
#endif

// The PYRFDC_CONFIG ROM payload is the raw XRFdc_Config image written by
// scripts/pyrfdc_mem.py; this tripwire fails the build loudly if the sysroot's
// librfdc layout ever changes size instead of silently decoding garbage.
static_assert(sizeof(XRFdc_Config) == 1880,
    "librfdc XRFdc_Config layout changed: rebuild the bitstreams with a pyrfdc_mem.py that matches this librfdc");

// ROM header constants (scripts/pyrfdc_mem.py is the generator)
static const uint32_t PYRFDC_ROM_MAGIC          = 0x52464443U; // "RFDC"
static const uint32_t PYRFDC_ROM_FORMAT_VERSION = 1U;
static const uint32_t PYRFDC_ROM_HEADER_BYTES   = 32U;

// Config status codes, exposed at 0x14000 and mirrored by _Rfdc.py ConfigStatus
enum {
    CfgNotLoaded          = 0,
    CfgOk                 = 1,
    CfgMissing            = 2,
    CfgBadMagic           = 3,
    CfgBadVersion         = 4,
    CfgBadSize            = 5,
    CfgIpVersionMismatch  = 6,
    CfgTileEnableMismatch = 7,
    // 8 is reserved and no longer produced: a driver bring-up failure on a valid
    // ROM makes the constructor throw. Kept so host decoding of the codes stays stable.
    CfgDriverBringUpFailed = 8,
    CfgBadHash            = 9,
};

// ----------------------------------------------------------------------------
// libmetal log ring. File-static because the libmetal log handler is a plain C
// function pointer with no `this`. Keeps the most recent 16 lines,
// sequence-numbered so a failing restart command can select only the lines
// logged since it started.
// The handler never calls back into PyRFdc or libmetal state.
// ----------------------------------------------------------------------------
namespace {

struct MetalLogLine {
    uint64_t seq;
    int level;
    std::string text;
};

std::mutex g_metalRingMtx;
std::deque<MetalLogLine> g_metalRing;
uint64_t g_metalSeq = 0;
std::shared_ptr<rogue::Logging> g_metalLog;

uint64_t metalRingSeqNow() {
    std::lock_guard<std::mutex> l(g_metalRingMtx);
    return g_metalSeq;
}

std::vector<std::string> metalRingSince(uint64_t seq) {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> l(g_metalRingMtx);
    for (const auto& line : g_metalRing) {
        if (line.seq > seq) {
            out.push_back(line.text);
        }
    }
    return out;
}

void metalLogHandler(enum metal_log_level level, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::string t(buf);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) {
        t.pop_back();
    }
    while (!t.empty() && (t.front() == '\n' || t.front() == '\r' || t.front() == ' ')) {
        t.erase(0, 1);
    }

    {
        std::lock_guard<std::mutex> l(g_metalRingMtx);
        g_metalRing.push_back({++g_metalSeq, int(level), t});
        if (g_metalRing.size() > 16) {
            g_metalRing.pop_front();
        }
    }

    if (g_metalLog) {
        uint32_t rogueLevel;
        switch (level) {
            case METAL_LOG_EMERGENCY:
            case METAL_LOG_ALERT:
            case METAL_LOG_CRITICAL:
                rogueLevel = rogue::Logging::Critical;
                break;
            case METAL_LOG_ERROR:
                rogueLevel = rogue::Logging::Error;
                break;
            case METAL_LOG_WARNING:
                rogueLevel = rogue::Logging::Warning;
                break;
            case METAL_LOG_NOTICE:
            case METAL_LOG_INFO:
                rogueLevel = rogue::Logging::Info;
                break;
            default:
                rogueLevel = rogue::Logging::Debug;
                break;
        }
        g_metalLog->log(rogueLevel, "%s", t.c_str());
    }

    // Chain to the default handler so journal output is unchanged
    metal_default_log_handler(level, "%s\n", t.c_str());
}

}  // namespace

//! Create a block, class creator
PyRFdcPtr PyRFdc::create(const std::string& cfg) {
    PyRFdcPtr b = std::make_shared<PyRFdc>(cfg);
    return (b);
}

//! Restart and CurrentState registers of one tile
static void TileStateRegs(XRFdc* inst, uint32_t type, uint32_t tile, uint32_t& state, uint32_t& restart) {
    uint32_t base = XRFDC_CTRL_STS_BASE(type, tile);
    restart = XRFdc_ReadReg(inst, base, XRFDC_RESTART_OFFSET) & XRFDC_RESTART_MASK;
    state = XRFdc_ReadReg(inst, base, XRFDC_CURRENT_STATE_OFFSET) & 0xFFU;
}

//! True when the driver's software PLL cache holds the six values that
//! XRFdc_CfgInitialize copies from the tile config (ADC and DAC config share
//! the field names, hence the template)
template <typename CfgT>
static bool PllCacheMirrors(const CfgT& c, const XRFdc_PLL_Settings& p) {
    return (p.SampleRate == c.SamplingRate) && (p.RefClkFreq == c.RefClkFreq) &&
           (p.Enabled == c.PLLEnable) && (p.FeedbackDivider == c.FeedbackDiv) &&
           (p.OutputDivider == c.OutputDiv) && (p.RefClkDivider == c.RefClkDiv);
}

//! Create an block
PyRFdc::PyRFdc(const std::string& cfg) : rim::Slave(4,0x1000) { // Set min=4B and max=4kB
    // Every data member carries an in-class initializer (PyRFdc.h), so a
    // not-initialized object serves the allow-listed registers deterministically
    log_ = rogue::Logging::create("PyRFdc");

    // RFDC config ROM status: not loaded until the ladder below decides otherwise
    cfgStatus_ = CfgNotLoaded;
    cfgMessage_ = "NotLoaded: PyRFdc has not decoded a config";
    cfgRomBytes_ = uint32_t(cfg.size());
    std::memset(cfgHeader_, 0, sizeof(cfgHeader_));
    std::memcpy(cfgHeader_, cfg.data(), std::min(cfg.size(), size_t(PYRFDC_ROM_HEADER_BYTES)));

    // ------------------------------------------------------------------
    // Validate the PYRFDC_CONFIG ROM bytes handed in by the launcher before
    // any libmetal call, so an invalid or absent ROM is reported
    // not-initialized whatever the UIO device state is, with nothing to clean.
    // First failing check decides the status; never read past cfg.size().
    // ------------------------------------------------------------------
    char msgBuf[256];

    if (cfg.size() == 0) {
        SetConfigStatus(CfgMissing,
            "Missing: no RFDC config ROM could be read at 0x4_0000_1000 (read failed or empty); "
            "rebuild the firmware with the PYRFDC_CONFIG ROM (AddPyRfdcMem after the RFDC .xci)");

    } else if (cfg.size() < 4) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "Missing: ROM read returned only %zu byte(s), need at least 4 to read the magic word",
            cfg.size());
        SetConfigStatus(CfgMissing, msgBuf);

    } else if (cfgHeader_[0] != PYRFDC_ROM_MAGIC) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadMagic: ROM word 0 is 0x%08X, not 0x%08X; this bitstream has no PYRFDC_CONFIG ROM; "
            "rebuild the firmware with a core that has it", cfgHeader_[0], PYRFDC_ROM_MAGIC);
        SetConfigStatus(CfgBadMagic, msgBuf);

    } else if (cfg.size() < PYRFDC_ROM_HEADER_BYTES) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadSize: ROM read returned %zu bytes, the header needs %u", cfg.size(), PYRFDC_ROM_HEADER_BYTES);
        SetConfigStatus(CfgBadSize, msgBuf);

    } else if (cfgHeader_[1] != PYRFDC_ROM_FORMAT_VERSION) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadVersion: ROM format version %u, expected %u; rebuild the firmware with a matching "
            "axi-soc-ultra-plus-core", cfgHeader_[1], PYRFDC_ROM_FORMAT_VERSION);
        SetConfigStatus(CfgBadVersion, msgBuf);

    } else if (cfgHeader_[2] != uint32_t(sizeof(XRFdc_Config))) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadSize: ROM payload size %u, sizeof(XRFdc_Config) is %zu for this librfdc; rebuild the "
            "bitstream with a matching pyrfdc_mem.py", cfgHeader_[2], sizeof(XRFdc_Config));
        SetConfigStatus(CfgBadSize, msgBuf);

    // Both the comparison and the message widen to size_t before adding:
    // cfgHeader_[2] is attacker influenced in the ROM tamper model, and a
    // 32-bit PYRFDC_ROM_HEADER_BYTES + cfgHeader_[2] wraps for a header claiming
    // a payload within 32 bytes of UINT32_MAX. The exact-size check above makes
    // that unreachable today, but that is an ordering property of the ladder, not
    // a guarantee this check can rely on.
    } else if (cfg.size() < size_t(PYRFDC_ROM_HEADER_BYTES) + size_t(cfgHeader_[2])) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadSize: ROM read returned %zu bytes, header plus payload need %zu",
            cfg.size(), size_t(PYRFDC_ROM_HEADER_BYTES) + size_t(cfgHeader_[2]));
        SetConfigStatus(CfgBadSize, msgBuf);

    } else {
        // Header is self-consistent; check the payload hash (tamper
        // mitigation) before trusting the payload for anything else.
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(cfg.data() + PYRFDC_ROM_HEADER_BYTES),
               sizeof(XRFdc_Config), digest);

        if (std::memcmp(digest, &cfgHeader_[4], 16) != 0) {
            SetConfigStatus(CfgBadHash,
                "BadHash: ROM payload sha256 does not match header words 4 to 7; the ROM may be "
                "corrupt or tampered, rebuild the firmware with a matching pyrfdc_mem.py");
        }
    }

    if (cfgStatus_ != CfgNotLoaded) {
        log_->error("%s", cfgMessage_.c_str());
        return;
    }

#ifdef __BAREMETAL__
    // Ensure baremetal driver is ready
    if (XRFdc_LookupConfig(RFDC_DEVICE_ID) == NULL) {
        log_->error("PyRFdc: Baremetal RFdc Configuration Lookup Failed!");
        return;
    }
#endif

    // The ROM is valid from here on, so a libmetal or driver bring-up failure
    // throws: a half-built object must never be served. The destructor does
    // not run for a constructor that throws, so each throw below first calls
    // CloseMetal for whatever has been opened so far.

    // Initialize libmetal (should be after ensuring baremetal is ready).
    // Nothing is open after a failed metal_init, so there is nothing to close.
    struct metal_init_params init_param = METAL_INIT_DEFAULTS;
    int rc = metal_init(&init_param);
    if (rc != 0) {
        throw rogue::GeneralError::create("PyRFdc::PyRFdc",
            "metal_init failed (returned %d); the RFDC driver cannot start", rc);
    }
    metalInited_ = true;

    // Install only AFTER a successful metal_init, which overwrites the handler.
    // g_metalLog is created once; a re-constructed PyRFdc (never happens in this
    // process, but kept defensive) reuses it.
    if (!g_metalLog) {
        g_metalLog = rogue::Logging::create("PyRFdc.metal");
    }
    metal_set_log_handler(metalLogHandler);

#ifndef __BAREMETAL__
    // deviceptr_ starts at nullptr and CloseMetal closes it only when set,
    // because XRFdc_RegisterMetal does not always write it on failure: when
    // XRFdc_GetDeviceNameByDeviceId cannot find the device node it returns
    // through RETURN_PATH having left *DevicePtr untouched (xrfdc_sinit.c).
    // This is the only XRFdc_RegisterMetal call in the file; the destructor
    // closes the same handle through CloseMetal instead of registering again.
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr_) != XRFDC_SUCCESS) {
        CloseMetal();
        throw rogue::GeneralError::create("PyRFdc::PyRFdc",
            "XRFdc_RegisterMetal failed for RFDC device %d; check that the usp_rf_data_converter UIO device is bound",
            int(RFDC_DEVICE_ID));
    }
#endif

    // Live sanity checks: need only the io region set up by XRFdc_RegisterMetal.
    // A mismatch is a bitstream problem, not a bring-up failure: report it
    // not-initialized and leave the device open for the destructor to close.
    uint32_t ipVerReg = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_IP_BASE, 0x0);
    if (ipVerReg != cfgHeader_[3]) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "IpVersionMismatch: RFDC IP version register 0x%08X, ROM expects 0x%08X; the "
            "bitstream and its ROM disagree, rebuild with make clean", ipVerReg, cfgHeader_[3]);
        SetConfigStatus(CfgIpVersionMismatch, msgBuf);

    } else {
        XRFdc_Config romCfg;
        std::memcpy(&romCfg, cfg.data() + PYRFDC_ROM_HEADER_BYTES, sizeof(XRFdc_Config));

        uint32_t tilesEnabledReg = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_IP_BASE, XRFDC_TILES_ENABLED_OFFSET) & 0xFFU;
        uint32_t tilesEnabledRom = 0;
        for (int t = 0; t < 4; t++) {
            if (romCfg.ADCTile_Config[t].Enable != 0) {
                tilesEnabledRom |= (1U << t);
            }
            if (romCfg.DACTile_Config[t].Enable != 0) {
                tilesEnabledRom |= (1U << (4 + t));
            }
        }

        if (tilesEnabledReg != tilesEnabledRom) {
            std::snprintf(msgBuf, sizeof(msgBuf),
                "TileEnableMismatch: tiles-enabled register 0x%02X, ROM config 0x%02X; "
                "rebuild with make clean", tilesEnabledReg, tilesEnabledRom);
            SetConfigStatus(CfgTileEnableMismatch, msgBuf);

        } else {
            if (XRFdc_CfgInitialize(RFdcInstPtr_, &romCfg) != XRFDC_SUCCESS) {
                CloseMetal();
                throw rogue::GeneralError::create("PyRFdc::PyRFdc",
                    "XRFdc_CfgInitialize failed on a valid PYRFDC_CONFIG ROM config");
            }
            std::snprintf(msgBuf, sizeof(msgBuf),
                "Ok: XRFdc_Config loaded from the PYRFDC_CONFIG ROM (format 1, %zu bytes, IP 0x%08X)",
                sizeof(XRFdc_Config), cfgHeader_[3]);
            SetConfigStatus(CfgOk, msgBuf);
        }
    }

    if (cfgStatus_ != CfgOk) {
        log_->error("%s", cfgMessage_.c_str());
        return;
    }
    log_->info("%s", cfgMessage_.c_str());

    log_->debug("PyRFdc::PyRFdc() Initialization Complete");

    // Construction only reads the hardware. XRFdc_CfgInitialize has already
    // primed the driver caches from the ROM config, so no tile is written to,
    // restarted or reconfigured here.
    for (uint32_t i = 0; i < 2; i++) {

        // Init the MTS configurations (software only); the operator's Tiles mask starts empty
        uint32_t mtsInitStatus = XRFdc_MultiConverter_Init(&mtsConfig_[i], 0, 0, XRFDC_TILE_ID0);
        if (mtsInitStatus != XRFDC_MTS_OK) {
            log_->error("XRFdc_MultiConverter_Init failed for converter type %u, code %u", i, mtsInitStatus);
        }
        mtsConfig_[i].Tiles = 0;

        for (uint32_t j = 0; j < 4; j++) {

            // Check if tile is enabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_SUCCESS) {
                continue;
            }

            // Software check that the driver's PLL cache still matches the ROM
            // config the way ResyncDriverCache assumes (reads nothing, writes nothing)
            bool mirrored;
            if (i == XRFDC_ADC_TILE) {
                mirrored = PllCacheMirrors(RFdcInst_.RFdc_Config.ADCTile_Config[j], RFdcInst_.ADC_Tile[j].PLL_Settings);
            } else {
                mirrored = PllCacheMirrors(RFdcInst_.RFdc_Config.DACTile_Config[j], RFdcInst_.DAC_Tile[j].PLL_Settings);
            }
            if (!mirrored) {
                log_->warning("PLL cache mirror mismatch: %s tile %u driver PLL cache differs from the ROM config",
                              (i == XRFDC_ADC_TILE) ? "ADC" : "DAC", j);
            }

            // Fill the staging buffers from the live hardware, read-only
            if (!RefreshStaging(i, j)) {
                uint32_t state = 0;
                uint32_t restart = 0;
                TileStateRegs(RFdcInstPtr_, i, j, state, restart);
                log_->info("staging not refreshed at construction: %s tile %u CurrentState=%u Restart=%u",
                           (i == XRFDC_ADC_TILE) ? "ADC" : "DAC", j, state, restart);
            }
        }
    }

    log_->debug("PyRFdc::PyRFdc()");
}

//! Destroy a block
PyRFdc::~PyRFdc() {
    // Log the destruction of the class
    log_->debug("PyRFdc::~PyRFdc() called");

    CloseMetal();

    log_->debug("PyRFdc::~PyRFdc() completed");
}

//! Close the metal device when it is set, then finish libmetal when it was
//! initialized; each step runs at most once because it clears its own flag
void PyRFdc::CloseMetal() {
#ifndef __BAREMETAL__
    if (deviceptr_ != nullptr) {
        metal_device_close(deviceptr_);
        deviceptr_ = nullptr;
    }
    if (metalInited_) {
        metal_finish();
        metalInited_ = false;
    }
#endif
}

//! Record the config status and the human-readable message (truncated, ASCII)
void PyRFdc::SetConfigStatus(uint32_t status, const std::string& msg) {
    cfgStatus_ = status;
    cfgMessage_ = msg.substr(0, 255);
}

//! True only for the registers a not-initialized PyRFdc must still serve
bool PyRFdc::DriverFree(uint32_t addr) const {
    if (addr == 0x12000 || addr == 0x12004 || addr == 0x12008) {
        return true;
    }
    if (addr >= 0x13000 && addr <= 0x13004) {
        return true;
    }
    if (addr >= 0x14000 && addr <= 0x141FC) {
        return true;
    }
    return false;
}

//! Serve the config status block at 0x14000 to 0x141FC. Read-only: a write is refused
//! (txnRefused_), so IgnoreMetalError cannot complete it, and it is logged at debug.
//! The RomMixerConfig, ResetRecord and MtsValid writes are not refused this way.
void PyRFdc::ConfigStatusReg(uint32_t addr) {
    if (!rdTxn_) {
        errMsg_ = "ConfigStatus(): read-only\n";
        txnRefused_ = true;
        return;
    }

    if (addr == 0x14000) {
        data_ = cfgStatus_;

    } else if (addr >= 0x14004 && addr <= 0x1401C) {
        data_ = cfgHeader_[1 + ((addr - 0x14004) >> 2)];

    } else if (addr == 0x14020) {
        data_ = cfgHeader_[0];

    } else if (addr == 0x14024) {
        data_ = cfgRomBytes_;

    } else if (addr >= 0x14028 && addr <= 0x140FC) {
        data_ = 0;

    } else if (addr >= 0x14100 && addr <= 0x141FC) {
        uint32_t wordIdx = (addr - 0x14100) >> 2;
        uint32_t word = 0;
        for (uint32_t b = 0; b < 4; b++) {
            size_t charIdx = size_t(wordIdx) * 4 + b;
            uint8_t ch = (charIdx < cfgMessage_.size()) ? uint8_t(cfgMessage_[charIdx]) : 0;
            word |= uint32_t(ch) << (8 * b);
        }
        data_ = word;

    } else {
        data_ = 0;
    }
}

//! Read the clock distribution once per transaction. The result stays cached until
//! doTransaction clears clkDistFetched_ at the start of the next transaction, so every
//! word of a block read comes from one snapshot. Below Gen3 the driver refuses the
//! call, so the status reads 0 (unsupported) with every field 0 and no transaction
//! error is raised. A failed Get reads status 2 with every field 0, and the warning is
//! logged once when the status changes into 2, not on every transaction (a ReadAll reads
//! hundreds of window words). clkDistStatus_ is assigned only here and doTransaction
//! clears only clkDistFetched_, so before the assignment it holds the status the previous
//! transaction left. The Get only reads registers (xrfdc_clock.c 896 to 1049 of the
//! 2026.1 driver) and has no tile-state precondition, so the window, the staging refresh
//! and the commit read it whatever state the tiles are in, and a parked affected tile
//! proceeds with a warning as documented. On Gen3 its only failure is the IP type check,
//! so status 2 is not reachable with this driver except through a driver change.
void PyRFdc::ClkDistFetch() {
    if (clkDistFetched_) {
        return;
    }

    uint32_t prevStatus = clkDistStatus_;
    clkDist_ = XRFdc_Distribution_System_Settings();
    if (RFdcInstPtr_->RFdc_Config.IPType < XRFDC_GEN3) {
        clkDistStatus_ = 0;
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClkDistribution-Gen-3/DFE
        if (XRFdc_GetClkDistribution(RFdcInstPtr_, &clkDist_) == XRFDC_SUCCESS) {
            clkDistStatus_ = 1;
        } else {
            clkDist_ = XRFdc_Distribution_System_Settings();
            clkDistStatus_ = 2;
            if (prevStatus != 2) {
                log_->warning("XRFdc_GetClkDistribution failed; the clock distribution window reads GetFailed");
            }
        }
    }
    clkDistFetched_ = true;
}

//! Serve the clock distribution window at 0x15000 to 0x157FC. Read-only: a write is
//! refused (txnRefused_) and reaches neither a register nor the driver, so
//! IgnoreMetalError cannot complete it, and it is logged at debug.
void PyRFdc::ClkDistReg(uint32_t addr) {
    if (!rdTxn_) {
        errMsg_ = "ClkDist(): read-only\n";
        txnRefused_ = true;
        return;
    }

    ClkDistFetch();
    data_ = ClkDistWord(clkDist_, XRFdc_GetTileLayout(RFdcInstPtr_), clkDistStatus_, addr - 0x15000);
}

static std::string TileName(uint32_t type, uint32_t tile) {
    return std::string((type == XRFDC_ADC_TILE) ? "ADC" : "DAC") + " tile " + std::to_string(tile);
}

//! The text goes through a fixed format so a percent sign in it can never act as one
static void LogWarning(const std::shared_ptr<rogue::Logging>& log, const std::string& text) {
    log->warning("%s", text.c_str());
}

//! Serve the SetClkDistribution staging words at 0x15800 to 0x1586C. They mirror
//! XRFdc_Distribution_Settings (layout in PyRFdc.h), are plain software words and
//! never reach the driver or a register, so a write is always accepted. ShutdownMode
//! is staged like the rest and refused by the commit when it is not 0.
void PyRFdc::ClkDistStagingReg(uint32_t addr) {
    uint32_t off = addr - 0x15800;
    XRFdc_Distribution_Settings& s = clkDistStage_;

    if ((off >= 0x30) && (off <= 0x6C)) {
        // SampleRates[type][tile] in MSPS: ADC at 0x30, DAC at 0x50, 8 bytes per tile
        uint32_t type = (off >= 0x50) ? XRFDC_DAC_TILE : XRFDC_ADC_TILE;
        uint32_t tile = (off - ((type == XRFDC_DAC_TILE) ? 0x50 : 0x30)) >> 3;
        bool upper = ((off >> 2) & 1U) != 0;
        if (rdTxn_) {
            data_ = DoubleToUint32(s.SampleRates[type][tile], upper);
        } else {
            s.SampleRates[type][tile] = RemapDoubleWithUint32(s.SampleRates[type][tile], data_, upper);
        }
        return;
    }

    if ((off == 0x20) || (off == 0x24)) {
        bool upper = (off == 0x24);
        if (rdTxn_) {
            data_ = DoubleToUint32(s.DistRefClkFreq, upper);
        } else {
            s.DistRefClkFreq = RemapDoubleWithUint32(s.DistRefClkFreq, data_, upper);
        }
        return;
    }

    uint32_t* field = nullptr;
    switch (off) {
        case 0x00: field = &s.SourceType; break;
        case 0x04: field = &s.SourceTileId; break;
        case 0x08: field = &s.EdgeTypes[0]; break;
        case 0x0C: field = &s.EdgeTileIds[0]; break;
        case 0x10: field = &s.EdgeTypes[1]; break;
        case 0x14: field = &s.EdgeTileIds[1]; break;
        case 0x18: field = &s.DistributedClock; break;
        case 0x1C: field = &s.ShutdownMode; break;
        default: break;
    }

    if (field == nullptr) {
        // 0x28 and 0x2C are reserved: read 0, a write is ignored
        if (rdTxn_) {
            data_ = 0;
        }
    } else if (rdTxn_) {
        data_ = *field;
    } else {
        *field = data_;
    }
}

//! Command word 0x15900: write the index of a Get entry to load the staging from it
//! (ShutdownMode 0). A refusal never changes the staging.
void PyRFdc::ClkDistRefreshCmd() {
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    const std::string head = "SetClkDistribution RefreshStaging refused: ";

    if (RFdcInstPtr_->RFdc_Config.IPType < XRFDC_GEN3) {
        errMsg_ = head + "the IP is below Gen3 and has no clock distribution";
        txnRefused_ = true;
        return;
    }

    ClkDistFetch();
    if (clkDistStatus_ != 1) {
        errMsg_ = head + "the current clock distribution could not be read";
        txnRefused_ = true;
        return;
    }

    uint32_t count = ClkDistCount(clkDist_);
    if (data_ >= count) {
        errMsg_ = head + "index " + std::to_string(data_) + " is not a distribution, the IP reports " +
                  std::to_string(count);
        txnRefused_ = true;
        return;
    }

    const XRFdc_Distribution_Settings& e = clkDist_.Distributions[data_];
    XRFdc_Distribution_Settings s = XRFdc_Distribution_Settings();
    s.SourceType = e.SourceType;
    s.SourceTileId = e.SourceTileId;
    s.EdgeTypes[0] = e.EdgeTypes[0];
    s.EdgeTileIds[0] = e.EdgeTileIds[0];
    s.EdgeTypes[1] = e.EdgeTypes[1];
    s.EdgeTileIds[1] = e.EdgeTileIds[1];
    s.DistRefClkFreq = e.DistRefClkFreq;
    s.DistributedClock = e.DistributedClock;
    std::memcpy(s.SampleRates, e.SampleRates, sizeof(s.SampleRates));
    s.ShutdownMode = 0;
    clkDistStage_ = s;
}

//! Check the staged distribution against everything the driver would reject late and
//! against the tiles the Set would cut off, without writing anything. Order: Gen3, the
//! span geometry, ShutdownMode, the span tiles enabled, the span rates and the
//! reference frequency, the current distribution, then the affected set and the
//! orphans. Returns the first failing class and, when lines is given, one line per
//! refusal ("SetClkDistribution refused: ...") and, for each tile of a per tile
//! refusal, a diagnostic line (call TileCheck, RateCheck or OrphanCheck) with a detail
//! line, tiles in ADC 0 to 3 then DAC 0 to 3 order. The masks are filled for Ok and
//! Orphan only; the affected tiles are the enabled ones. A disabled tile is named
//! without a register read.
ClkDistCheck PyRFdc::ClkDistValidate(uint32_t& adcAffected, uint32_t& dacAffected, uint32_t& adcSpan,
                                     uint32_t& dacSpan, std::vector<std::string>* lines) {
    adcAffected = 0;
    dacAffected = 0;
    adcSpan = 0;
    dacSpan = 0;

    const char* op = "SetClkDistribution";
    const std::string head = "SetClkDistribution refused: ";
    const XRFdc_Distribution_Settings& s = clkDistStage_;

    if (RFdcInstPtr_->RFdc_Config.IPType < XRFDC_GEN3) {
        if (lines != nullptr) {
            lines->push_back(head + "the IP is below Gen3 and has no clock distribution");
        }
        return ClkDistUnsupported;
    }

    uint32_t layout = XRFdc_GetTileLayout(RFdcInstPtr_);
    uint32_t lower = 0;
    uint32_t upper = 0;
    uint32_t source = 0;
    ClkDistCheck geometry = ClkDistGeometry(layout, s, lower, upper, source);
    if (geometry != ClkDistOk) {
        if (lines != nullptr) {
            std::string staged = "SourceType " + std::to_string(s.SourceType) + " SourceTileId " + std::to_string(s.SourceTileId) +
                                 " EdgeTypes " + std::to_string(s.EdgeTypes[0]) + " " + std::to_string(s.EdgeTypes[1]) +
                                 " EdgeTileIds " + std::to_string(s.EdgeTileIds[0]) + " " + std::to_string(s.EdgeTileIds[1]) +
                                 " DistributedClock " + std::to_string(s.DistributedClock);
            if (geometry == ClkDistBadField) {
                lines->push_back(head + "a staged type is not 0 or 1, a staged tile id is not a tile of this IP or DistributedClock is above 2: " + staged);
            } else {
                lines->push_back(head + "the staged span is not a legal distribution: the source must lie between the two edge tiles, a single tile span needs DistributedClock 0, a multi tile span needs 1 or 2, an ADC source may not reach a DAC tile and an edge tile source may not distribute OutDiv or a full rate clock without its PLL: " + staged);
            }
        }
        return geometry;
    }

    if (s.ShutdownMode != 0) {
        if (lines != nullptr) {
            lines->push_back(head + "ShutdownMode " + std::to_string(s.ShutdownMode) +
                             " is not allowed, the commit always runs with ShutdownMode 0 so that every enabled tile ends at state 15");
        }
        return ClkDistShutdownMode;
    }

    uint32_t spanAdc = 0;
    uint32_t spanDac = 0;
    DistMaskToTiles(layout, SpanMask(lower, upper), spanAdc, spanDac);
    std::vector<std::pair<uint32_t, uint32_t> > spanTiles = ClkDistOrderedTiles(spanAdc, spanDac);

    bool disabled = false;
    for (size_t i = 0; i < spanTiles.size(); i++) {
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, spanTiles[i].first, spanTiles[i].second) != XRFDC_SUCCESS) {
            disabled = true;
            if (lines != nullptr) {
                lines->push_back(std::string(op) + " " + TileName(spanTiles[i].first, spanTiles[i].second) +
                                 ": TileCheck failed; the tile is not enabled in this design but lies inside the staged span");
            }
        }
    }
    if (disabled) {
        if (lines != nullptr) {
            lines->insert(lines->begin(), head + "a tile of the staged span is not enabled");
        }
        return ClkDistTileDisabled;
    }

    bool badRate = false;
    for (size_t i = 0; i < spanTiles.size(); i++) {
        uint32_t type = spanTiles[i].first;
        uint32_t tile = spanTiles[i].second;
        double rate = s.SampleRates[type][tile];
        double minRate = 0.0;
        double maxRate = 0.0;
        bool limits = (XRFdc_GetMinSampleRate(RFdcInstPtr_, type, tile, &minRate) == XRFDC_SUCCESS) &&
                      (XRFdc_GetMaxSampleRate(RFdcInstPtr_, type, tile, &maxRate) == XRFDC_SUCCESS);
        if (limits && ClkDistRateOk(rate, minRate, maxRate)) {
            continue;
        }
        badRate = true;
        if (lines != nullptr) {
            lines->push_back(DiagLine(op, type, tile, "RateCheck"));
            if (limits) {
                lines->push_back(TileName(type, tile) + ": SampleRate " + std::to_string(rate) +
                                 " MSPS is not a finite number inside " + std::to_string(minRate) + " to " +
                                 std::to_string(maxRate) + " MSPS");
            } else {
                lines->push_back(TileName(type, tile) + ": SampleRate " + std::to_string(rate) +
                                 " MSPS could not be checked, the driver did not report the tile limits");
            }
        }
    }

    double sourceRate = s.SampleRates[s.SourceType][s.SourceTileId];
    if (!ClkDistRefOk(s.DistRefClkFreq, sourceRate)) {
        badRate = true;
        if (lines != nullptr) {
            lines->push_back(DiagLine(op, s.SourceType, s.SourceTileId, "RateCheck"));
            lines->push_back(TileName(s.SourceType, s.SourceTileId) + ": DistRefClkFreq " + std::to_string(s.DistRefClkFreq) +
                             " MHz must be finite and positive, and inside " + std::to_string(XRFDC_REFFREQ_MIN) + " to " +
                             std::to_string(XRFDC_REFFREQ_MAX) + " MHz when the source runs its PLL");
        }
    }
    if (badRate) {
        if (lines != nullptr) {
            lines->insert(lines->begin(), head + "a staged sample rate or the reference frequency is out of range");
        }
        return ClkDistBadRate;
    }

    ClkDistFetch();
    if (clkDistStatus_ != 1) {
        if (lines != nullptr) {
            lines->push_back(head + "the current clock distribution could not be read");
        }
        return ClkDistUnsupported;
    }

    uint32_t orphanMask = 0;
    uint32_t affected = ClkDistAffected(clkDist_, SpanMask(lower, upper), source, orphanMask);

    uint32_t enabledAdc = 0;
    uint32_t enabledDac = 0;
    for (uint32_t t = 0; t < 4; t++) {
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, XRFDC_ADC_TILE, t) == XRFDC_SUCCESS) {
            enabledAdc |= 1U << t;
        }
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, XRFDC_DAC_TILE, t) == XRFDC_SUCCESS) {
            enabledDac |= 1U << t;
        }
    }

    uint32_t affAdc = 0;
    uint32_t affDac = 0;
    uint32_t orphAdc = 0;
    uint32_t orphDac = 0;
    DistMaskToTiles(layout, affected, affAdc, affDac);
    DistMaskToTiles(layout, orphanMask, orphAdc, orphDac);
    affAdc &= enabledAdc;
    affDac &= enabledDac;
    orphAdc &= enabledAdc;
    orphDac &= enabledDac;

    adcAffected = affAdc;
    dacAffected = affDac;
    adcSpan = spanAdc;
    dacSpan = spanDac;

    if ((orphAdc | orphDac) != 0) {
        if (lines != nullptr) {
            lines->push_back(head + "an enabled tile that is clocked today would end up outside every distribution");
            std::vector<std::pair<uint32_t, uint32_t> > orphans = ClkDistOrderedTiles(orphAdc, orphDac);
            for (size_t i = 0; i < orphans.size(); i++) {
                uint32_t type = orphans[i].first;
                uint32_t tile = orphans[i].second;
                uint32_t index = DistIndex(layout, type, tile);
                std::string by = "a distribution that this Set replaces";
                uint32_t count = ClkDistCount(clkDist_);
                for (uint32_t d = 0; d < count; d++) {
                    const XRFdc_Distribution_Info& info = clkDist_.Distributions[d].Info;
                    if ((index >= info.LowerBound) && (index <= info.UpperBound)) {
                        by = TileName(clkDist_.Distributions[d].SourceType, clkDist_.Distributions[d].SourceTileId);
                        break;
                    }
                }
                lines->push_back(DiagLine(op, type, tile, "OrphanCheck"));
                lines->push_back(TileName(type, tile) + ": clocked today by " + by +
                                 " and outside the staged span, so it would have no clock after the Set");
            }
        }
        return ClkDistOrphan;
    }
    return ClkDistOk;
}

//! Serve the preview and last-commit words at 0x15908 to 0x15928, read-only. The preview
//! words are ClkDistValidate of the staging (status, then the four masks, which are 0
//! unless the staging passes up to the orphan check); the last-commit words are the
//! masks of the last commit, 0 after a refused one. Read-only: a write is refused
//! (txnRefused_), so IgnoreMetalError cannot complete it, and it is logged at debug.
void PyRFdc::ClkDistPreviewReg(uint32_t addr) {
    if (!rdTxn_) {
        errMsg_ = "ClkDistPreview(): read-only\n";
        txnRefused_ = true;
        return;
    }

    if (addr >= 0x1591C) {
        data_ = clkDistLast_[(addr - 0x1591C) >> 2];
        return;
    }

    uint32_t adcAffected = 0;
    uint32_t dacAffected = 0;
    uint32_t adcSpan = 0;
    uint32_t dacSpan = 0;
    ClkDistCheck check = ClkDistValidate(adcAffected, dacAffected, adcSpan, dacSpan, nullptr);

    switch (addr) {
        case 0x15908: data_ = uint32_t(check); break;
        case 0x1590C: data_ = adcAffected; break;
        case 0x15910: data_ = dacAffected; break;
        case 0x15914: data_ = adcSpan; break;
        default: data_ = dacSpan; break;
    }
}

//! Command word 0x15904: commit the staged distribution with XRFdc_SetClkDistribution.
//! The whole check runs before the driver is called, in this order: ClkDistValidate,
//! then the restart precheck of every affected enabled tile (ADC 0 to 3, then DAC 0
//! to 3). A refusal sets txnRefused_ so IgnoreMetalError cannot swallow it, writes
//! nothing and records nothing. A busy tile refuses the whole commit and every busy
//! tile is named; a parked tile proceeds with a warning and its record carries the
//! parked flag. The driver always runs with ShutdownMode disabled, from a copy of the
//! staging, so every span tile is started up to state 15 inside the call. The MtsValid
//! of every converter type with an affected tile is cleared just before the call.
//! Afterwards each span tile gets one record (ok only when the driver returned success
//! and the tile reads state 15 with Restart clear) and a staging refresh when ready.
void PyRFdc::ClkDistCommit() {
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    uint64_t seq0 = metalRingSeqNow();
    for (uint32_t i = 0; i < 4; i++) {
        clkDistLast_[i] = 0;
    }

    uint32_t adcAffected = 0;
    uint32_t dacAffected = 0;
    uint32_t adcSpan = 0;
    uint32_t dacSpan = 0;
    std::vector<std::string> lines;
    ClkDistCheck check = ClkDistValidate(adcAffected, dacAffected, adcSpan, dacSpan, &lines);
    if (check != ClkDistOk) {
        errMsg_ = JoinWholeLines(lines, ErrorTextMaxChars);
        txnRefused_ = true;
        return;
    }

    std::vector<std::pair<uint32_t, uint32_t> > affected = ClkDistOrderedTiles(adcAffected, dacAffected);
    std::vector<TileFailure> adcBusy;
    std::vector<TileFailure> dacBusy;
    uint32_t parked[2] = {0, 0};
    for (size_t i = 0; i < affected.size(); i++) {
        uint32_t type = affected[i].first;
        uint32_t tile = affected[i].second;
        uint32_t firstState = 0;
        uint32_t lastState = 0;
        RestartWait wait = RestartPrecheck(type, tile, firstState, lastState);
        if (wait == RestartBusy) {
            std::string text = BusyText(firstState, lastState);
            (type == XRFDC_ADC_TILE ? adcBusy : dacBusy).push_back(TileFailure{tile, "RestartPrecheck", text});
            LogWarning(log_, std::string("SetClkDistribution ") + TileName(type, tile) + ": " + text);
        } else if (wait == RestartParked) {
            parked[type] |= 1U << tile;
            LogWarning(log_, std::string("SetClkDistribution ") + TileName(type, tile) +
                             ": parked, Restart stayed set for " + std::to_string(uint32_t(XRFDC_RESTART_CLR_DLY_CNT)) +
                             " ms with CurrentState frozen at " + std::to_string(firstState) +
                             "; restarting from the staged distribution");
        }
    }
    if (!adcBusy.empty() || !dacBusy.empty()) {
        errMsg_ = RestartFailureError("SetClkDistribution", adcBusy, dacBusy, seq0);
        txnRefused_ = true;
        return;
    }

    clkDistLast_[0] = adcAffected;
    clkDistLast_[1] = dacAffected;
    clkDistLast_[2] = adcSpan;
    clkDistLast_[3] = dacSpan;

    // The restart makes the MTS outputs of every converter type with an affected tile stale
    ClearMtsValidTypes(((adcAffected != 0) ? 0x1U : 0U) | ((dacAffected != 0) ? 0x2U : 0U));

    // The commit never shuts the tiles down: the copy runs with ShutdownMode disabled
    XRFdc_Distribution_Settings local = clkDistStage_;
    local.ShutdownMode = XRFDC_DISABLED;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetClkDistribution-Gen-3/DFE
    uint32_t driverStatus = XRFdc_SetClkDistribution(RFdcInstPtr_, &local);
    bool driverOk = (driverStatus == XRFDC_SUCCESS);

    std::vector<TileFailure> adcFail;
    std::vector<TileFailure> dacFail;
    if (!driverOk) {
        TileFailure f = TileFailure{clkDistStage_.SourceTileId, "XRFdc_SetClkDistribution",
                                    "driver returned " + std::to_string(driverStatus)};
        (clkDistStage_.SourceType == XRFDC_ADC_TILE ? adcFail : dacFail).push_back(f);
    }

    std::vector<std::pair<uint32_t, uint32_t> > span = ClkDistOrderedTiles(adcSpan, dacSpan);
    for (size_t i = 0; i < span.size(); i++) {
        uint32_t type = span[i].first;
        uint32_t tile = span[i].second;
        bool ready = TileReady(type, tile);
        uint32_t flags = (((parked[type] >> tile) & 1U) != 0) ? ResetFlagParked : 0;
        if (ready && !RefreshStaging(type, tile)) {
            flags |= ResetFlagStagingNotRefreshed;
            LogWarning(log_, std::string("staging not refreshed: SetClkDistribution ") + TileName(type, tile));
        }
        RecordRestart(type, tile, RestartOpClkDist, (driverOk && ready) ? ResetResultOk : ResetResultFailed, flags);
    }

    for (size_t i = 0; i < affected.size(); i++) {
        uint32_t type = affected[i].first;
        uint32_t tile = affected[i].second;
        if (!TileReady(type, tile)) {
            (type == XRFDC_ADC_TILE ? adcFail : dacFail).push_back(TileFailure{tile, "StateCheck", ""});
        }
    }

    if (!adcFail.empty() || !dacFail.empty()) {
        // Tile order inside each type; the driver failure keeps its place before a state check of the same tile
        auto byTile = [](const TileFailure& a, const TileFailure& b) { return a.tile < b.tile; };
        std::stable_sort(adcFail.begin(), adcFail.end(), byTile);
        std::stable_sort(dacFail.begin(), dacFail.end(), byTile);
        errMsg_ = RestartFailureError("SetClkDistribution", adcFail, dacFail, seq0);
    }

    // The next transaction reads the distribution again
    clkDistFetched_ = false;
}

//! Serve the Vivado mixer fields of the RFDC config ROM at tile-only 0x200 to
//! 0x23C, 0x10 bytes per block: MixerType, MixerInputDataType and the two words
//! of NCOFreq. They are the values XRFdc_CfgInitialize was given, served so the
//! host can restore a never-written mixer. NCOFreq is read from the config
//! because XRFdc_DACInitialize seeds the DAC driver cache from MixerType instead.
//! XRFdc_Config holds NCOFreq in GHz (the .xci value), while the host writes it
//! back as XRFdc_Mixer_Settings.Freq, which is in MHz, so it is served in MHz.
void PyRFdc::RomMixerConfigReg(uint32_t tileAddr) {
    if (!rdTxn_) {
        errMsg_ = "RomMixerConfig(): read-only\n";
        return;
    }

    uint32_t block = 0;
    uint32_t field = 0;
    RomMixerIndex(tileAddr, block, field);

    uint32_t mixerType = 0;
    uint32_t inputType = 0;
    double ncoFreq = 0.0;

    if (tileType_ == XRFDC_ADC_TILE) {
        const XRFdc_ADCBlock_DigitalDataPath_Config& c = RFdcInst_.RFdc_Config.ADCTile_Config[tileId_].ADCBlock_Digital_Config[block];
        mixerType = c.MixerType;
        inputType = c.MixerInputDataType;
        ncoFreq   = c.NCOFreq;
    } else {
        const XRFdc_DACBlock_DigitalDataPath_Config& c = RFdcInst_.RFdc_Config.DACTile_Config[tileId_].DACBlock_Digital_Config[block];
        mixerType = c.MixerType;
        inputType = c.MixerInputDataType;
        ncoFreq   = c.NCOFreq;
    }
    ncoFreq = 1000.0*ncoFreq; // Convert from GHz to MHz

    switch (field) {
        case 0:
            data_ = mixerType;
            break;
        case 1:
            data_ = inputType;
            break;
        case 2:
            data_ = DoubleToUint32(ncoFreq, false);
            break;
        default:
            data_ = DoubleToUint32(ncoFreq, true);
            break;
    }
}

//! Tile-only 0x828: re-read the tile's clock source, PLL, QMC and mixer
//! settings from the driver into the staging words. Writes no hardware register
//! and, like every other refresh, does nothing unless the tile is at state 15
//! with Restart clear.
void PyRFdc::RefreshStagingCmd() {
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    if (!RefreshStaging(tileType_, tileId_)) {
        uint32_t state = 0;
        uint32_t restart = 0;
        TileStateRegs(RFdcInstPtr_, tileType_, tileId_, state, restart);
        errMsg_ = std::string("RefreshStaging ") + ((tileType_ == XRFDC_ADC_TILE) ? "ADC" : "DAC") +
                  " tile " + std::to_string(tileId_) + ": not refreshed; CurrentState=" + std::to_string(state) +
                  " Restart=" + std::to_string(restart) +
                  "; needs the tile at state 15 with Restart clear and every driver getter to succeed\n";
        // A refused refresh must reach the host even while IgnoreMetalError is set
        txnRefused_ = true;
    }
}

//! Serve the tile-only restart record registers at 0x814 to 0x824
void PyRFdc::ResetRecordReg(uint32_t addr) {
    if (!rdTxn_) {
        errMsg_ = "ResetRecord(): read-only\n";
        return;
    }

    uint32_t t = tileType_;
    uint32_t n = tileId_;

    if (addr == 0x814) {
        // PG269 v2.6 p.43: Reset Count, automatic restarts only, 8-bit saturating
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(t, n), 0x0038) & 0xFFU;

    } else if (addr == 0x818) {
        data_ = resetRecord_[t][n];

    } else if (addr == 0x81C) {
        data_ = stateAtFailure_[t][n];

    } else if (addr == 0x820) {
        data_ = commonAtFailure_[t][n];

    } else if (addr == 0x824) {
        data_ = clkDetAtFailure_[t][n];

    } else {
        data_ = 0;
    }
}

//! Record a restart command's outcome for one tile: sequence, op, flags and result
//! (PackResetRecord). On a result other than ok, snapshot CurrentState, Common
//! Status, and the clock detector BEFORE IgnoreMetalError can swallow the error.
void PyRFdc::RecordRestart(uint32_t type, uint32_t tile, uint32_t op, uint32_t result, uint32_t flags) {
    uint16_t seq = uint16_t(resetSeq_[type][tile] + 1);
    if (seq == 0) {
        // Sequence 0 means the tile was never restarted, so the wrap skips it
        seq = 1;
    }
    resetSeq_[type][tile] = seq;
    resetRecord_[type][tile] = PackResetRecord(seq, op, result, flags);

    if (result != ResetResultOk) {
        uint32_t base = XRFDC_CTRL_STS_BASE(type, tile);
        stateAtFailure_[type][tile] = XRFdc_ReadReg(RFdcInstPtr_, base, 0x000C) & 0xFFU;
        commonAtFailure_[type][tile] = XRFdc_ReadReg(RFdcInstPtr_, base, 0x0228) & 0xFU;
        if (RFdcInstPtr_->RFdc_Config.IPType >= XRFDC_GEN3) {
            clkDetAtFailure_[type][tile] = XRFdc_ReadReg(RFdcInstPtr_, base, 0x0084) & 0x1U;
        } else {
            clkDetAtFailure_[type][tile] = 0xFFU;
        }
    }
}

//! One key=value diagnostic line for a failing tile. Plain XRFdc_ReadReg reads plus XRFdc_GetClockSource; never blocks or asserts.
std::string PyRFdc::DiagLine(const char* op, uint32_t type, uint32_t tile, const char* call) {
    uint32_t base = XRFDC_CTRL_STS_BASE(type, tile);
    uint32_t currentState = XRFdc_ReadReg(RFdcInstPtr_, base, 0x000C) & 0xFFU;
    uint32_t common = XRFdc_ReadReg(RFdcInstPtr_, base, 0x0228);
    uint32_t clockPresent = common & 0x1U;
    uint32_t supplyUp     = (common >> 1) & 0x1U;
    uint32_t powerUp      = (common >> 2) & 0x1U;
    uint32_t pllLocked    = (common >> 3) & 0x1U;

    std::string clkDet;
    if (RFdcInstPtr_->RFdc_Config.IPType >= XRFDC_GEN3) {
        clkDet = std::to_string(XRFdc_ReadReg(RFdcInstPtr_, base, 0x0084) & 0x1U);
    } else {
        clkDet = "NA";
    }

    uint32_t clkSrc = 0;
    std::string clkSrcStr;
    if (XRFdc_GetClockSource(RFdcInstPtr_, type, tile, &clkSrc) != XRFDC_SUCCESS) {
        clkSrcStr = "Unknown";
    } else if (clkSrc == XRFDC_EXTERNAL_CLK) {
        clkSrcStr = "External";
    } else if (clkSrc == XRFDC_INTERNAL_PLL_CLK) {
        clkSrcStr = "InternalPLL";
    } else {
        clkSrcStr = "Unknown";
    }

    char line[256];
    std::snprintf(line, sizeof(line),
        "%s %s tile %u: %s failed; CurrentState=%u ClockPresent=%u SupplyUp=%u PowerUp=%u PllLocked=%u ClkDet=%s ClkSrc=%s",
        op, (type == XRFDC_ADC_TILE) ? "ADC" : "DAC", tile, call,
        currentState, clockPresent, supplyUp, powerUp, pllLocked,
        clkDet.c_str(), clkSrcStr.c_str());
    return std::string(line);
}

//! One DiagLine per failed tile (tile order), then each non-empty detail line
//! tagged with its tile, then the metal lines captured since seq0; newline
//! separated, no trailing empty line. Whole lines only, within the TcpClient
//! error limit (ErrorTextMaxChars): the DiagLines come first so they are the
//! last to be dropped, and a line that does not fit is never cut. The failures
//! of both converter types are reported with the ADC lines before the DAC lines
//! (ClkDistComposeLines).
std::string PyRFdc::RestartFailureError(const char* op, const std::vector<TileFailure>& adcFailures,
                                        const std::vector<TileFailure>& dacFailures, uint64_t seq0) {
    std::vector<std::string> diag[2];
    std::vector<std::string> detail[2];
    const std::vector<TileFailure>* failures[2] = {&adcFailures, &dacFailures};

    for (uint32_t type = 0; type < 2; type++) {
        for (size_t i = 0; i < failures[type]->size(); i++) {
            const TileFailure& f = (*failures[type])[i];
            diag[type].push_back(DiagLine(op, type, f.tile, f.call));
            if (!f.detail.empty()) {
                std::string line = (type == XRFDC_ADC_TILE) ? "ADC" : "DAC";
                line += " tile " + std::to_string(f.tile) + ": ";
                line += f.detail;
                detail[type].push_back(line);
            }
        }
    }

    std::vector<std::string> lines = ClkDistComposeLines(diag[XRFDC_ADC_TILE], diag[XRFDC_DAC_TILE],
                                                         detail[XRFDC_ADC_TILE], detail[XRFDC_DAC_TILE],
                                                         metalRingSince(seq0));
    return JoinWholeLines(lines, ErrorTextMaxChars);
}

//! The restart failure text of one converter type
std::string PyRFdc::RestartFailureError(const char* op, uint32_t type,
                                        const std::vector<TileFailure>& failures, uint64_t seq0) {
    const std::vector<TileFailure> none;
    if (type == XRFDC_ADC_TILE) {
        return RestartFailureError(op, failures, none, seq0);
    }
    return RestartFailureError(op, none, failures, seq0);
}

//! True only when the tile's Restart register is clear and CurrentState reads 15
bool PyRFdc::TileReady(uint32_t type, uint32_t tile) {
    uint32_t state = 0;
    uint32_t restart = 0;
    TileStateRegs(RFdcInstPtr_, type, tile, state, restart);
    return (restart == 0) && (state == XRFDC_SM_STATE15);
}

//! Clear the MTS valid latch of each converter type in typeMask (bit 0 ADC, bit 1 DAC).
//! The only place besides MtsSync that clears a flag. The sync masks and reference
//! tiles are the operator's and are never touched.
void PyRFdc::ClearMtsValidTypes(uint32_t typeMask) {
    if ((typeMask & 0x1U) != 0) {
        mtsValid_[XRFDC_ADC_TILE] = false;
    }
    if ((typeMask & 0x2U) != 0) {
        mtsValid_[XRFDC_DAC_TILE] = false;
    }
}

//! The MTS outputs go stale from the moment a restarted tile stops feeding its clock,
//! so the flags are cleared before the first restart write of a command. A restart of a
//! distribution's source tile also drops the clock of every member, so the types come
//! from the topology (MtsTypesToClear). The driver Get only reads registers and has no
//! tile-state precondition (xrfdc_clock.c 896 to 1049 of the 2026.1 driver), but a
//! topology read taken while tiles are moving, for example while a SetClkDistribution is
//! still reprogramming the distribution map, can be inconsistent. So the topology is
//! read fresh only on Gen3 with every enabled tile at state 15 and Restart clear;
//! otherwise, or when the Get fails, both types are cleared, so the flag can only err
//! toward 0.
void PyRFdc::ClearMtsValidForRestart(uint32_t adcMask, uint32_t dacMask) {
    XRFdc_Distribution_System_Settings topo = XRFdc_Distribution_System_Settings();
    bool topoValid = false;

    if (RFdcInstPtr_->RFdc_Config.IPType >= XRFDC_GEN3) {
        bool allReady = true;
        for (uint32_t type = 0; type < 2; type++) {
            for (uint32_t t = 0; t < 4; t++) {
                if (XRFdc_CheckTileEnabled(RFdcInstPtr_, type, t) != XRFDC_SUCCESS) {
                    continue;
                }
                if (!TileReady(type, t)) {
                    allReady = false;
                }
            }
        }
        if (allReady) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClkDistribution-Gen-3/DFE
            topoValid = (XRFdc_GetClkDistribution(RFdcInstPtr_, &topo) == XRFDC_SUCCESS);
        }
    }

    ClearMtsValidTypes(MtsTypesToClear(XRFdc_GetTileLayout(RFdcInstPtr_), topo, topoValid, adcMask, dacMask));
}

//! Fill the staging buffers of one tile from the hardware, read-only. Nothing
//! is written back to the tile by any restart. Does nothing unless the tile is
//! ready, so no driver getter ever runs against a state machine that is moving.
bool PyRFdc::RefreshStaging(uint32_t type, uint32_t tile) {
    if (!TileReady(type, tile)) {
        return false;
    }

    bool ok = true;

    uint32_t clkSrc = 0;
    if (XRFdc_GetClockSource(RFdcInstPtr_, type, tile, &clkSrc) == XRFDC_SUCCESS) {
        clkSrcConfig_[type][tile] = clkSrc;
    } else {
        ok = false;
    }

    XRFdc_PLL_Settings pll = {};
    if (XRFdc_GetPLLConfig(RFdcInstPtr_, type, tile, &pll) == XRFDC_SUCCESS) {
        pll.SampleRate = 1000.0*pll.SampleRate; // Convert from GSPS to MSPS
        pllConfig_[type][tile] = pll;
    } else {
        ok = false;
    }

    for (uint32_t block = 0; block < 4; block++) {

        // Check if block enabled
        if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, type, tile, block) != XRFDC_SUCCESS) {
            continue;
        }

        XRFdc_QMC_Settings qmc = {};
        if (XRFdc_GetQMCSettings(RFdcInstPtr_, type, tile, block, &qmc) == XRFDC_SUCCESS) {
            qmcConfig_[type][tile][block] = qmc;
        } else {
            ok = false;
        }

        if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, type, tile, block) == XRFDC_SUCCESS) {
            // Check for ADC tile or DAC DUC not bypassed
            if ((type == XRFDC_ADC_TILE) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(type, tile, block), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                XRFdc_Mixer_Settings mixer = {};
                if (XRFdc_GetMixerSettings(RFdcInstPtr_, type, tile, block, &mixer) == XRFDC_SUCCESS) {
                    mixerConfig_[type][tile][block] = mixer;
                } else {
                    ok = false;
                }
            }
        }
    }

    return ok;
}

//! After a restart from state 0 the hardware is back at the baseline, so make
//! the driver's software caches match a freshly constructed instance: the PLL
//! settings that XRFdc_CfgInitialize copies from the tile config, and the mixer
//! type and frequency it seeds per block. Instance members only, no register
//! access. XRFdc_UpdatePLLStruct is static in the driver, so it is mirrored here.
void PyRFdc::ResyncDriverCache(uint32_t type, uint32_t tile) {
    if (tile >= 4) {
        return;
    }

    XRFdc_Config& cfg = RFdcInst_.RFdc_Config;

    if (type == XRFDC_ADC_TILE) {
        XRFdc_PLL_Settings& pll = RFdcInst_.ADC_Tile[tile].PLL_Settings;
        pll.SampleRate      = cfg.ADCTile_Config[tile].SamplingRate;
        pll.RefClkFreq      = cfg.ADCTile_Config[tile].RefClkFreq;
        pll.Enabled         = cfg.ADCTile_Config[tile].PLLEnable;
        pll.FeedbackDivider = cfg.ADCTile_Config[tile].FeedbackDiv;
        pll.OutputDivider   = cfg.ADCTile_Config[tile].OutputDiv;
        pll.RefClkDivider   = cfg.ADCTile_Config[tile].RefClkDiv;

        for (uint32_t block = 0; block < 4; block++) {
            // XRFdc_ADCInitialize seeds Freq with NCOFreq in GHz although the field is in
            // MHz; mirrored on purpose. XRFdc_GetMixerSettings reads it only to unfold an
            // NCO beyond +/-Fs/2, so until the first SetMixerSettings such an NCO reads
            // back folded into the first Nyquist zone
            XRFdc_Mixer_Settings& mixer = RFdcInst_.ADC_Tile[tile].ADCBlock_Digital_Datapath[block].Mixer_Settings;
            mixer.MixerType = uint8_t(cfg.ADCTile_Config[tile].ADCBlock_Digital_Config[block].MixerType);
            mixer.Freq      = cfg.ADCTile_Config[tile].ADCBlock_Digital_Config[block].NCOFreq;
            mixer.MixerMode = 0;
        }

    } else {
        XRFdc_PLL_Settings& pll = RFdcInst_.DAC_Tile[tile].PLL_Settings;
        pll.SampleRate      = cfg.DACTile_Config[tile].SamplingRate;
        pll.RefClkFreq      = cfg.DACTile_Config[tile].RefClkFreq;
        pll.Enabled         = cfg.DACTile_Config[tile].PLLEnable;
        pll.FeedbackDivider = cfg.DACTile_Config[tile].FeedbackDiv;
        pll.OutputDivider   = cfg.DACTile_Config[tile].OutputDiv;
        pll.RefClkDivider   = cfg.DACTile_Config[tile].RefClkDiv;

        for (uint32_t block = 0; block < 4; block++) {
            // XRFdc_DACInitialize seeds Freq from the block's MixerType, not its
            // NCOFreq; mirrored on purpose so a restarted tile matches a fresh one
            XRFdc_Mixer_Settings& mixer = RFdcInst_.DAC_Tile[tile].DACBlock_Digital_Datapath[block].Mixer_Settings;
            mixer.MixerType = uint8_t(cfg.DACTile_Config[tile].DACBlock_Digital_Config[block].MixerType);
            mixer.Freq      = cfg.DACTile_Config[tile].DACBlock_Digital_Config[block].MixerType;
            mixer.MixerMode = 0;
        }
    }
}

//! Wait for a tile whose Restart register is set before a new restart is issued.
//! Polls at the driver's own restart-clear budget (1000 x 1 ms). Writes nothing.
//! firstState and lastState report CurrentState at the start and at the last sample.
RestartWait PyRFdc::RestartPrecheck(uint32_t type, uint32_t tile, uint32_t& firstState, uint32_t& lastState) {
    uint32_t base = XRFDC_CTRL_STS_BASE(type, tile);

    uint32_t restart = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_RESTART_OFFSET) & XRFDC_RESTART_MASK;
    firstState = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_CURRENT_STATE_OFFSET) & 0xFFU;
    lastState = firstState;
    if (restart == 0) {
        return RestartClear;
    }

    bool moved = false;
    for (uint32_t i = 0; i < XRFDC_RESTART_CLR_DLY_CNT; i++) {
        metal_sleep_usec(XRFDC_RESTART_CLR_WAIT);
        if ((XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_RESTART_OFFSET) & XRFDC_RESTART_MASK) == 0) {
            lastState = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_CURRENT_STATE_OFFSET) & 0xFFU;
            return RestartClear;
        }
        lastState = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_CURRENT_STATE_OFFSET) & 0xFFU;
        if (lastState != firstState) {
            moved = true;
        }
    }
    return ClassifyRestartWait(false, moved);
}

//! Decide whether a restart command that is not run through RestartTiles
//! (RestartSM, a DynamicPLLConfig commit) may go ahead after RestartPrecheck.
//! A busy tile is refused: the error text is set, txnRefused_ is raised so
//! IgnoreMetalError cannot swallow it, and nothing is written. A parked tile is
//! logged and allowed to proceed. Returns true when the caller may proceed.
bool PyRFdc::RestartProceed(const char* op, uint32_t type, uint32_t tile, RestartWait wait,
                            uint32_t firstState, uint32_t lastState, uint64_t seq0) {
    const char* typeName = (type == XRFDC_ADC_TILE) ? "ADC" : "DAC";

    if (wait == RestartBusy) {
        std::string text = BusyText(firstState, lastState);
        std::vector<TileFailure> failures;
        failures.push_back(TileFailure{tile, "RestartPrecheck", text});
        errMsg_ = RestartFailureError(op, type, failures, seq0);
        txnRefused_ = true;
        log_->warning("%s %s tile %u: %s", op, typeName, tile, text.c_str());
        return false;
    }

    if (wait == RestartParked) {
        log_->warning("%s %s tile %u: parked, Restart stayed set for %u ms with CurrentState frozen at %u; restarting from the requested start state",
                      op, typeName, tile, XRFDC_RESTART_CLR_DLY_CNT, firstState);
    }
    return true;
}

//! Restart one tile, or every enabled tile of the type when tile is negative
//! (in RestartOrder: the clock distribution sources first, the reverse for a
//! Shutdown). Each tile gets a precheck and then exactly one driver call;
//! a failure is recorded and the loop moves on to the next tile. Tile_Id -1 is
//! never forwarded to the driver. Nothing is replayed into the tile afterwards:
//! the staging buffers are only refreshed from the hardware once the tile is
//! back at state 15. Reset, StartUp and CustomStartUp are recorded in the
//! per-tile restart record; Shutdown is not, so it never moves the sequence.
void PyRFdc::RestartTiles(uint32_t op, uint32_t type, int tile, uint32_t startState, uint32_t endState) {
    uint64_t seq0 = metalRingSeqNow();
    std::vector<TileFailure> failures;
    const char* typeName = (type == XRFDC_ADC_TILE) ? "ADC" : "DAC";
    const char* opName = "Restart";
    const char* call = "RestartTiles";
    bool recorded = (op != RestartOpShutdown);
    char buf[220];

    switch (op) {
        case RestartOpReset:
            opName = "Reset";
            call = "XRFdc_Reset";
            break;
        case RestartOpStartUp:
            opName = "StartUp";
            call = "XRFdc_StartUp";
            break;
        case RestartOpCustomStartUp:
            opName = "CustomStartUp";
            call = "XRFdc_CustomStartUp";
            break;
        case RestartOpShutdown:
            opName = "Shutdown";
            call = "XRFdc_Shutdown";
            break;
        default:
            break;
    }

    // The MTS outputs are stale from the moment any tile that feeds them restarts,
    // whatever happens next. The operator's Tiles and RefTile are left alone.
    uint32_t restarted = 0;
    if (tile >= 0) {
        restarted = (tile < 4) ? (1U << tile) : 0U;
    } else {
        for (uint32_t t = 0; t < 4; t++) {
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, type, t) == XRFDC_SUCCESS) {
                restarted |= 1U << t;
            }
        }
    }
    ClearMtsValidForRestart((type == XRFDC_ADC_TILE) ? restarted : 0U, (type == XRFDC_DAC_TILE) ? restarted : 0U);

    // Every tile of the type: the clock distribution sources go first (RestartOrder).
    // The Get only reads registers, so it is valid whatever state the tiles are in.
    uint32_t order[4] = {0, 1, 2, 3};
    if (tile < 0) {
        ClkDistFetch();
        RestartOrder(XRFdc_GetTileLayout(RFdcInstPtr_), clkDist_, clkDistStatus_ == 1, type,
                     op == RestartOpShutdown, order);
    }

    for (uint32_t k = 0; k < 4; k++) {
        uint32_t t = order[k];

        // Select the tiles
        if (tile >= 0) {
            if (uint32_t(tile) != t) {
                continue;
            }
        } else if (XRFdc_CheckTileEnabled(RFdcInstPtr_, type, t) != XRFDC_SUCCESS) {
            continue;
        }

        uint32_t flags = 0;
        uint32_t firstState = 0;
        uint32_t lastState = 0;
        RestartWait wait = RestartPrecheck(type, t, firstState, lastState);

        // The state machine is still moving: a restart now would corrupt it
        if (wait == RestartBusy) {
            std::string text = BusyText(firstState, lastState);
            if (recorded) {
                RecordRestart(type, t, op, ResetResultBusy, 0);
            }
            failures.push_back(TileFailure{t, "RestartPrecheck", text});
            txnRefused_ = true;
            log_->warning("%s %s tile %u: %s", opName, typeName, t, text.c_str());
            continue;
        }

        // The tile is parked: restart it from the requested start state
        if (wait == RestartParked) {
            flags |= ResetFlagParked;
            log_->warning("%s %s tile %u: parked, Restart stayed set for %u ms with CurrentState frozen at %u; restarting from the requested start state",
                          opName, typeName, t, XRFDC_RESTART_CLR_DLY_CNT, firstState);
        }

        // Exactly one explicit-tile driver call
        int status = XRFDC_FAILURE;
        switch (op) {
            case RestartOpReset:
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
                status = XRFdc_Reset(RFdcInstPtr_, type, int(t));
                break;
            case RestartOpStartUp:
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
                status = XRFdc_StartUp(RFdcInstPtr_, type, int(t));
                break;
            case RestartOpCustomStartUp:
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
                status = XRFdc_CustomStartUp(RFdcInstPtr_, type, int(t), startState, endState);
                break;
            case RestartOpShutdown:
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
                status = XRFdc_Shutdown(RFdcInstPtr_, type, int(t));
                break;
            default:
                break;
        }

        if (status == XRFDC_SUCCESS) {

            // A restart from state 0 returns the hardware to the baseline
            if ((op == RestartOpReset) || ((op == RestartOpCustomStartUp) && (startState == XRFDC_STATE_OFF))) {
                ResyncDriverCache(type, t);
            }

            // Only a restart that ends at state 15 can refresh the staging
            if ((endState == XRFDC_SM_STATE15) && !RefreshStaging(type, t)) {
                uint32_t state = 0;
                uint32_t restart = 0;
                TileStateRegs(RFdcInstPtr_, type, t, state, restart);
                flags |= ResetFlagStagingNotRefreshed;
                log_->warning("staging not refreshed: %s %s tile %u CurrentState=%u Restart=%u",
                              opName, typeName, t, state, restart);
            }

        } else {
            if (endState == XRFDC_SM_STATE15) {
                flags |= ResetFlagStagingNotRefreshed;
            }
            std::string detail;
            if (wait == RestartParked) {
                std::snprintf(buf, sizeof(buf),
                    "parked: Restart stayed set for %u ms with CurrentState frozen at %u; restarted from state %u",
                    XRFDC_RESTART_CLR_DLY_CNT, firstState, startState);
                detail = buf;
            }
            failures.push_back(TileFailure{t, call, detail});
        }

        if (recorded) {
            RecordRestart(type, t, op, (status == XRFDC_SUCCESS) ? ResetResultOk : ResetResultFailed, flags);
        }
    }

    if (!failures.empty()) {
        // Reported in tile order, whatever order the tiles were restarted in
        std::stable_sort(failures.begin(), failures.end(),
                         [](const TileFailure& a, const TileFailure& b) { return a.tile < b.tile; });
        errMsg_ = RestartFailureError(opName, type, failures, seq0);
    }
}

//! Read-only check for the write gate. Collects the tiles the write depends on
//! (per ScopeTile, ScopeTypeAll, ScopeMtsGroup and ScopeMtsSysref of
//! WriteGateInfo) and refuses on the first one whose Restart register is set or
//! whose CurrentState is below 15. Reads two registers per tile, writes nothing,
//! never waits.
bool PyRFdc::GateWrite(const WriteGateInfo& info, uint32_t addr, std::string& why) {
    uint32_t mask[2] = {0, 0};
    uint32_t winType = (((addr >> 15) & 0x1) == 0x0) ? XRFDC_ADC_TILE : XRFDC_DAC_TILE;

    switch (info.scope) {
        case ScopeTile:
            mask[winType] = 1U << ((addr >> 13) & 0x3);
            break;

        case ScopeTypeAll:
            for (uint32_t t = 0; t < 4; t++) {
                if (XRFdc_CheckTileEnabled(RFdcInstPtr_, uint32_t(info.type), t) == XRFDC_SUCCESS) {
                    mask[info.type] |= 1U << t;
                }
            }
            break;

        case ScopeMtsGroup:
            for (uint32_t t = 0; t < 4; t++) {
                uint32_t group = MtsGroupMask(mtsConfig_[info.type].Tiles, mtsConfig_[info.type].RefTile);
                if (((group >> t) & 0x1) && (XRFdc_CheckTileEnabled(RFdcInstPtr_, uint32_t(info.type), t) == XRFDC_SUCCESS)) {
                    mask[info.type] |= 1U << t;
                }
            }
            break;

        case ScopeMtsSysref:
            // XRFdc_MTS_Sysref_Config always writes DAC tile 0 and every tile of both masks
            for (uint32_t type = 0; type < 2; type++) {
                for (uint32_t t = 0; t < 4; t++) {
                    bool inScope = ((mtsConfig_[type].Tiles >> t) & 0x1) || ((type == XRFDC_DAC_TILE) && (t == 0));
                    if (inScope && (XRFdc_CheckTileEnabled(RFdcInstPtr_, type, t) == XRFDC_SUCCESS)) {
                        mask[type] |= 1U << t;
                    }
                }
            }
            break;

        default:
            break;
    }

    for (uint32_t type = 0; type < 2; type++) {
        for (uint32_t t = 0; t < 4; t++) {
            if (((mask[type] >> t) & 0x1) == 0) {
                continue;
            }
            uint32_t base = XRFDC_CTRL_STS_BASE(type, t);
            uint32_t restart = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_RESTART_OFFSET) & XRFDC_RESTART_MASK;
            uint32_t state = XRFdc_ReadReg(RFdcInstPtr_, base, XRFDC_CURRENT_STATE_OFFSET) & 0xFFU;
            if ((restart != 0) || (state < XRFDC_SM_STATE15)) {
                char buf[220];
                std::snprintf(buf, sizeof(buf),
                    "WriteGate %s refused: %s tile %u CurrentState=%u Restart=%u; writes need the tile at state 15 with Restart clear",
                    info.name, (type == XRFDC_ADC_TILE) ? "ADC" : "DAC", t, state, restart);
                why = buf;
                return false;
            }
        }
    }
    return true;
}

//! True unless the address is a gated write and a tile in its scope is not ready
bool PyRFdc::WriteAllowed(uint32_t addr, std::string& why) {
    WriteGateInfo gi = ClassifyWrite(addr);
    if (gi.kind != WriteGated) {
        return true;
    }
    return GateWrite(gi, addr, why);
}

void PyRFdc::StartUp(int Tile_Id) {

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    // One prechecked XRFdc_StartUp per tile
    RestartTiles(RestartOpStartUp, tileType_, Tile_Id, XRFDC_SM_STATE1, XRFDC_SM_STATE15);
}

void PyRFdc::Shutdown(int Tile_Id) {

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    // One prechecked XRFdc_Shutdown per tile. Never gated on state 15, so a
    // parked tile can still be shut down.
    RestartTiles(RestartOpShutdown, tileType_, Tile_Id, XRFDC_SM_STATE1, XRFDC_SM_STATE1);
}

void PyRFdc::Reset(int Tile_Id) {

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    // One prechecked XRFdc_Reset per tile, nothing replayed afterwards
    RestartTiles(RestartOpReset, tileType_, Tile_Id, XRFDC_SM_STATE0, XRFDC_SM_STATE15);
}

void PyRFdc::CustomStartUp(int Tile_Id) {

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    // StartState in bits 3:0 and EndState in bits 7:4 of one word, the layout
    // of the CustomStartUp commands in the Rfdc and RfdcTile python devices.
    // The driver validates the range before it touches any register.
    uint32_t startState = data_ & 0xF;
    uint32_t endState = (data_ >> 4) & 0xF;
    RestartTiles(RestartOpCustomStartUp, tileType_, Tile_Id, startState, endState);
}

void PyRFdc::GetIPStatus() {
    int status = XRFDC_SUCCESS;
    XRFdc_IPStatus IPStatusPtr = {};
    XRFdc_TileStatus TileStatus = {};

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIPStatus
    status = XRFdc_GetIPStatus(RFdcInstPtr_, &IPStatusPtr);

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Select the tile
        TileStatus = isADC_ ? IPStatusPtr.ADCTileStatus[tileId_] : IPStatusPtr.DACTileStatus[tileId_];

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_TileStatus
        data_  = uint32_t(TileStatus.IsEnabled&0x1)      <<0; // BIT0
        data_ |= uint32_t(TileStatus.TileState&0xF)      <<1; // BIT4:BIT1
        data_ |= uint32_t(TileStatus.BlockStatusMask&0x3)<<5; // BIT6:BIT5
        data_ |= uint32_t(TileStatus.PowerUpState&0x1)   <<7; // BIT7
        data_ |= uint32_t(TileStatus.PLLState&0x1)       <<8; // BIT8

    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "GetIPStatus(): failed\n";
    }
}

void PyRFdc::GetBlockStatus(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_BlockStatus BlockStatus = {};

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetBlockStatus
        status = XRFdc_GetBlockStatus(RFdcInstPtr_, tileType_, tileId_, blockId_, &BlockStatus);

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_BlockStatus
        if (index==0) {
            data_  = DoubleToUint32(BlockStatus.SamplingFreq, false);

        } else if (index==1) {
            data_  = DoubleToUint32(BlockStatus.SamplingFreq, true);

        } else if (index==2) {
            data_  = uint32_t(BlockStatus.AnalogDataPathStatus&0xFF)   <<0; // BIT7:BIT0
            data_ |= uint32_t(BlockStatus.DigitalDataPathStatus&0xFFFF)<<8; // BIT23:BIT8
            data_ |= uint32_t(BlockStatus.DataPathClocksStatus&0x1)   <<24; // BIT24
            data_ |= uint32_t(BlockStatus.IsFIFOFlagsEnabled&0x1)     <<25; // BIT25
            data_ |= uint32_t(BlockStatus.IsFIFOFlagsAsserted&0x1)    <<26; // BIT26

        } else {
            status = XRFDC_FAILURE;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "GetBlockStatus(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::MixerSettings(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
        switch (index) {
            case 0:
                mixerConfig_[tileType_][tileId_][blockId_].Freq  = RemapDoubleWithUint32(mixerConfig_[tileType_][tileId_][blockId_].Freq, data_, false);
                break;
            case 1:
                mixerConfig_[tileType_][tileId_][blockId_].Freq  = RemapDoubleWithUint32(mixerConfig_[tileType_][tileId_][blockId_].Freq, data_, true);
                break;
            case 2:
                mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset  = RemapDoubleWithUint32(mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset, data_, false);
                break;
            case 3:
                mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset  = RemapDoubleWithUint32(mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset, data_, true);
                break;
            case 4:
                mixerConfig_[tileType_][tileId_][blockId_].EventSource = data_;
                break;
            case 5:
                mixerConfig_[tileType_][tileId_][blockId_].CoarseMixFreq = data_;
                break;
            case 6:
                mixerConfig_[tileType_][tileId_][blockId_].MixerMode      = uint32_t((data_>>0)  & 0xFF);
                mixerConfig_[tileType_][tileId_][blockId_].FineMixerScale = uint8_t( (data_>>8)  & 0xFF);
                mixerConfig_[tileType_][tileId_][blockId_].MixerType      = uint8_t( (data_>>16) & 0xFF);
                break;
            case 7:
                status = XRFDC_FAILURE;
                if ((tileType_==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(tileType_, tileId_, blockId_), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetMixerSettings
                    status = XRFdc_SetMixerSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &mixerConfig_[tileType_][tileId_][blockId_]);
                    // Only a Tile or Slice event source takes the update event; the staged event source is never rewritten
                    if ((status == XRFDC_SUCCESS) && CommitNeedsUpdateEvent(mixerConfig_[tileType_][tileId_][blockId_].EventSource)) {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_UpdateEvent
                        status = XRFdc_UpdateEvent(RFdcInstPtr_, tileType_, tileId_, blockId_, XRFDC_EVENT_MIXER);
                    }
                }
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
        switch (index) {
            case 0:
                data_  = DoubleToUint32(mixerConfig_[tileType_][tileId_][blockId_].Freq, false);
                break;
            case 1:
                data_  = DoubleToUint32(mixerConfig_[tileType_][tileId_][blockId_].Freq, true);
                break;
            case 2:
                data_  = DoubleToUint32(mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset, false);
                break;
            case 3:
                data_  = DoubleToUint32(mixerConfig_[tileType_][tileId_][blockId_].PhaseOffset, true);
                break;
            case 4:
                data_ = mixerConfig_[tileType_][tileId_][blockId_].EventSource;
                break;
            case 5:
                data_ = mixerConfig_[tileType_][tileId_][blockId_].CoarseMixFreq;
                break;
            case 6:
                data_  = uint32_t(mixerConfig_[tileType_][tileId_][blockId_].MixerMode&0xFF)      <<0;  // BIT7:BIT0
                data_ |= uint32_t(mixerConfig_[tileType_][tileId_][blockId_].FineMixerScale&0xFF) <<8;  // BIT15:BIT8
                data_ |= uint32_t(mixerConfig_[tileType_][tileId_][blockId_].MixerType&0xFF)      <<16; // BIT23:BIT16

                break;
            case 7:
                data_ = 1; // Always return 1 so this is a set() and not posted() cmd
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MixerSettings(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::QMCSettings(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
        switch (index) {
            case 0:
                qmcConfig_[tileType_][tileId_][blockId_].EnablePhase = (data_>>0)&0x1;
                qmcConfig_[tileType_][tileId_][blockId_].EnableGain  = (data_>>1)&0x1;
                break;
            case 1:
                qmcConfig_[tileType_][tileId_][blockId_].EventSource = data_;
                break;
            case 2:
                qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor  = RemapDoubleWithUint32(qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor, data_, false);
                break;
            case 3:
                qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor  = RemapDoubleWithUint32(qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor, data_, true);
                break;
            case 4:
                qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor  = RemapDoubleWithUint32(qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor, data_, false);
                break;
            case 5:
                qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor  = RemapDoubleWithUint32(qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor, data_, true);
                break;
            case 6:
                qmcConfig_[tileType_][tileId_][blockId_].OffsetCorrectionFactor = data_;
                break;
            case 7:
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetQMCSettings
                status = XRFdc_SetQMCSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &qmcConfig_[tileType_][tileId_][blockId_]);
                // Only a Tile or Slice event source takes the update event; the staged event source is never rewritten
                if ((status == XRFDC_SUCCESS) && CommitNeedsUpdateEvent(qmcConfig_[tileType_][tileId_][blockId_].EventSource)) {
                    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_UpdateEvent
                    status = XRFdc_UpdateEvent(RFdcInstPtr_, tileType_, tileId_, blockId_, XRFDC_EVENT_QMC);
                }
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
        switch (index) {
            case 0:
                data_  = uint32_t(qmcConfig_[tileType_][tileId_][blockId_].EnablePhase&0x1) <<0; // BIT0
                data_ |= uint32_t(qmcConfig_[tileType_][tileId_][blockId_].EnableGain &0x1) <<1; // BIT1
                break;
            case 1:
                data_ = qmcConfig_[tileType_][tileId_][blockId_].EventSource;
                break;
            case 2:
                data_  = DoubleToUint32(qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor, false);
                break;
            case 3:
                data_  = DoubleToUint32(qmcConfig_[tileType_][tileId_][blockId_].GainCorrectionFactor, true);
                break;
            case 4:
                data_  = DoubleToUint32(qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor, false);
                break;
            case 5:
                data_  = DoubleToUint32(qmcConfig_[tileType_][tileId_][blockId_].PhaseCorrectionFactor, true);
                break;
            case 6:
                data_ = qmcConfig_[tileType_][tileId_][blockId_].OffsetCorrectionFactor;
                break;
            case 7:
                data_ = 1; // Always return 1 so this is a set() and not posted() cmd
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "QMCSettings(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::CoarseDelaySettings() {
    int status = XRFDC_SUCCESS;
    XRFdc_CoarseDelay_Settings settings = {};

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoarseDelaySettings
    status = XRFdc_GetCoarseDelaySettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Check if write
    if (!rdTxn_) {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_CoarseDelay_Settings
        settings.CoarseDelay = ( (data_>>0) &0xFF);
        settings.EventSource = ( (data_>>8) &0xFF);

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCoarseDelaySettings
        if (status == XRFDC_SUCCESS) {
            status = XRFdc_SetCoarseDelaySettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_CoarseDelay_Settings
        data_  = uint32_t(settings.CoarseDelay&0xFF) <<0; // BIT7:BIT0
        data_ |= uint32_t(settings.EventSource&0xFF) <<8; // BIT15:BIT8
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CoarseDelaySettings(): failed\n";
    }
}

void PyRFdc::UpdateEvent(uint32_t XRFDC_EVENT) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_UpdateEvent
        status = XRFdc_UpdateEvent(RFdcInstPtr_, tileType_, tileId_, blockId_, XRFDC_EVENT);

    // Else read
    } else {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "UpdateEvent(" + std::to_string(XRFDC_EVENT) + "): failed\n";
    }
}

void PyRFdc::InterpolationFactor() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetInterpolationFactor
            status = XRFdc_SetInterpolationFactor(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
            status = XRFdc_GetInterpolationFactor(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "InterpolationFactor(): failed\n";
    }
}

void PyRFdc::DecimationFactor() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactor
            status = XRFdc_SetDecimationFactor(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
            status = XRFdc_GetDecimationFactor(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DecimationFactor(): failed\n";
    }
}

void PyRFdc::DecimationFactorObs() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactorObs-Gen-3/DFE
            status = XRFdc_SetDecimationFactorObs(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactorObs-Gen-3/DFE
            status = XRFdc_GetDecimationFactorObs(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DecimationFactorObs(): failed\n";
    }
}

void PyRFdc::FabClkOutDiv() {
    int status = XRFDC_SUCCESS;
    uint16_t settings = uint16_t(data_&0x7);

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabClkOutDiv
        status = XRFdc_SetFabClkOutDiv(RFdcInstPtr_, tileType_, tileId_, settings);

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabClkOutDiv
        status = XRFdc_GetFabClkOutDiv(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_ = uint32_t(settings);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabClkOutDiv(): failed\n";
    }
}

void PyRFdc::FabWrVldWords() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {

        // Check for ADC tile
        if (isADC_) {
            status = XRFDC_FAILURE;

        // Else DAC tile
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabWrVldWords
            status = XRFdc_SetFabWrVldWords(RFdcInstPtr_, tileId_, blockId_, settings);
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWords
        status = XRFdc_GetFabWrVldWords(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }


    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabWrVldWords(): failed\n";
    }
}

void PyRFdc::FabWrVldWordsObs() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWordsObs-Gen-3/DFE
            status = XRFdc_GetFabWrVldWordsObs(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabWrVldWordsObs(): failed\n";
    }
}

void PyRFdc::FabRdVldWords() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {

        // Check for DAC tile
        if (!isADC_) {
            status = XRFDC_FAILURE;

        // Else ADC tile
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabRdVldWords
            status = XRFdc_SetFabRdVldWords(RFdcInstPtr_, tileId_, blockId_, settings);
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabRdVldWords
        status = XRFdc_GetFabRdVldWords(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabRdVldWords(): failed\n";
    }
}

void PyRFdc::FabRdVldWordsObs() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {

        // Check for DAC tile
        if (!isADC_) {
            status = XRFDC_FAILURE;

        // Else ADC tile
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabRdVldWordsObs-Gen-3/DFE
            status = XRFdc_SetFabRdVldWordsObs(RFdcInstPtr_, tileId_, blockId_, settings);
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabRdVldWordsObs-Gen-3/DFE
        status = XRFdc_GetFabRdVldWordsObs(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabRdVldWordsObs(): failed\n";
    }
}

void PyRFdc::ThresholdStickyClear() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {

        // Check for DAC tile
        if (!isADC_) {
            status = XRFDC_FAILURE;

        // Else ADC tile
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ThresholdStickyClear
            status = XRFdc_ThresholdStickyClear(RFdcInstPtr_, tileId_, blockId_, settings);
        }
    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ThresholdStickyClear(): failed\n";
    }
}

void PyRFdc::ThresholdClrMode() {
    int status = XRFDC_SUCCESS;
    uint32_t ThresholdToUpdate = (data_>>0)&0xFF;
    uint32_t ClrMode           = (data_>>8)&0xFF;

    // Check if write
    if (!rdTxn_) {

        // Check for DAC tile
        if (!isADC_) {
            status = XRFDC_FAILURE;

        // Else ADC tile
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdClrMode
            status = XRFdc_SetThresholdClrMode(RFdcInstPtr_, tileId_, blockId_, ThresholdToUpdate, ClrMode);
        }
    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ThresholdClrMode(): failed\n";
    }
}

void PyRFdc::ThresholdSettings(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_Threshold_Settings settings = {};

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetThresholdSettings
        status = XRFdc_GetThresholdSettings(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Threshold_Settings
            switch (index) {
                case 0:
                    settings.ThresholdMode[0] = data_&0x3; // Range: 0 to 3 (0-OFF, 1-sticky-over, 2-sticky-under and 3-hysteresis)
                    break;
                case 1:
                    settings.ThresholdMode[1] = data_&0x3; // Range: 0 to 3 (0-OFF, 1-sticky-over, 2-sticky-under and 3-hysteresis)
                    break;
                case 2:
                    settings.ThresholdAvgVal[0] = data_;
                    break;
                case 3:
                    settings.ThresholdAvgVal[1] = data_;
                    break;
                case 4:
                    settings.ThresholdUnderVal[0] = data_;
                    break;
                case 5:
                    settings.ThresholdUnderVal[1] = data_;
                    break;
                case 6:
                    settings.ThresholdOverVal[0] = data_;
                    break;
                case 7:
                    settings.ThresholdOverVal[1] = data_;
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }

            // Update the thresholds, only after a good read and a valid index
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdSettings
            if (status == XRFDC_SUCCESS) {
                settings.UpdateThreshold = XRFDC_UPDATE_THRESHOLD_BOTH;
                status = XRFdc_SetThresholdSettings(RFdcInstPtr_, tileId_, blockId_, &settings);
            }

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Threshold_Settings
            switch (index) {
                case 0:
                    data_ = settings.ThresholdMode[0];
                    break;
                case 1:
                    data_ = settings.ThresholdMode[1];
                    break;
                case 2:
                    data_ = settings.ThresholdAvgVal[0];
                    break;
                case 3:
                    data_ = settings.ThresholdAvgVal[1];
                    break;
                case 4:
                    data_ = settings.ThresholdUnderVal[0];
                    break;
                case 5:
                    data_ = settings.ThresholdUnderVal[1];
                    break;
                case 6:
                    data_ = settings.ThresholdOverVal[0];
                    break;
                case 7:
                    data_ = settings.ThresholdOverVal[1];
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ThresholdSettings(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::DecoderMode() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecoderMode
            status = XRFdc_SetDecoderMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecoderMode
            status = XRFdc_GetDecoderMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DecoderMode(): failed\n";
    }
}

void PyRFdc::ResetNCOPhase() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetNCOPhase
        status = XRFdc_ResetNCOPhase(RFdcInstPtr_, tileType_, tileId_, blockId_);

    // Else read
    } else {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ResetNCOPhase(): failed\n";
    }
}

void PyRFdc::SetupFIFO(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    uint8_t settings = uint8_t((data_>>1)&0x1);

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFO
        status = XRFdc_SetupFIFO(RFdcInstPtr_, tileType_, Tile_Id, settings);

    // Else read
    } else {
         status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "SetupFIFO(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::SetupFIFOObs(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    uint8_t settings = uint8_t((data_>>1)&0x1);

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    } else {
        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOObs-Gen-3/DFE
            status = XRFdc_SetupFIFOObs(RFdcInstPtr_, tileType_, Tile_Id, settings);

        // Else read
        } else {
             status = XRFDC_FAILURE;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "SetupFIFOObs(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::SetupFIFOBoth(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    uint8_t settings = uint8_t((data_>>1)&0x1);

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    } else {
        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOBoth-Gen-3/DFE
            status = XRFdc_SetupFIFOBoth(RFdcInstPtr_, tileType_, Tile_Id, settings);

        // Else read
        } else {
             status = XRFDC_FAILURE;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "SetupFIFOBoth(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::OutputCurr() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetOutputCurr
            status = XRFdc_GetOutputCurr(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "OutputCurr(): failed\n";
    }
}

void PyRFdc::FIFOStatus() {
    int status = XRFDC_SUCCESS;
    uint8_t settings = 0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFIFOStatus
        status = XRFdc_GetFIFOStatus(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_  = uint32_t(settings);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FIFOStatus(): failed\n";
    }
}

void PyRFdc::FIFOStatusObs() {
    int status = XRFDC_SUCCESS;
    uint8_t settings = 0;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFIFOStatusObs-Gen-3/DFE
            status = XRFdc_GetFIFOStatusObs(RFdcInstPtr_, tileType_, tileId_, &settings);
            data_  = uint32_t(settings);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FIFOStatusObs(): failed\n";
    }
}

void PyRFdc::NyquistZone() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_&0x3;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetNyquistZone
        status = XRFdc_SetNyquistZone(RFdcInstPtr_, tileType_, tileId_, blockId_, settings);

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNyquistZone
        status = XRFdc_GetNyquistZone(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "NyquistZone(): failed\n";
    }
}

void PyRFdc::InvSincFIR() {
    int status = XRFDC_SUCCESS;
    uint16_t settings = uint16_t(data_);

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {
        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetInvSincFIR
            status = XRFdc_SetInvSincFIR(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInvSincFIR
            status = XRFdc_GetInvSincFIR(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = uint32_t(settings);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "InvSincFIR(): failed\n";
    }
}

void PyRFdc::CalibrationMode() {
    int status = XRFDC_SUCCESS;
    uint8_t settings = uint8_t(data_&0x3);

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalibrationMode
            status = XRFdc_SetCalibrationMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalibrationMode
            status = XRFdc_GetCalibrationMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = uint32_t(settings);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CalibrationMode(): failed\n";
    }
}

void PyRFdc::DisableCoefficientsOverride() {
    int status = XRFDC_SUCCESS;
    uint8_t settings = uint8_t(data_&0x3);

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_DisableCoefficientsOverride
            status = XRFdc_DisableCoefficientsOverride(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            status = XRFDC_FAILURE;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DisableCoefficientsOverride(): failed\n";
    }
}

void PyRFdc::CalCoefficients(uint32_t calType, uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_Calibration_Coefficients settings = {};

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalCoefficients
        status = XRFdc_GetCalCoefficients(RFdcInstPtr_, tileId_, blockId_, calType, &settings);

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Calibration_Coefficients
            switch (index) {
                case 0:
                    settings.Coeff0 = data_;
                    break;
                case 1:
                    settings.Coeff1 = data_;
                    break;
                case 2:
                    settings.Coeff2 = data_;
                    break;
                case 3:
                    settings.Coeff3 = data_;
                    break;
                case 4:
                    settings.Coeff4 = data_;
                    break;
                case 5:
                    settings.Coeff5 = data_;
                    break;
                case 6:
                    settings.Coeff6 = data_;
                    break;
                case 7:
                    settings.Coeff7 = data_;
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalCoefficients
            if (status == XRFDC_SUCCESS) {
                status = XRFdc_SetCalCoefficients(RFdcInstPtr_, tileId_, blockId_, calType, &settings);
            }

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Calibration_Coefficients
            switch (index) {
                case 0:
                    data_ = settings.Coeff0;
                    break;
                case 1:
                    data_ = settings.Coeff1;
                    break;
                case 2:
                    data_ = settings.Coeff2;
                    break;
                case 3:
                    data_ = settings.Coeff3;
                    break;
                case 4:
                    data_ = settings.Coeff4;
                    break;
                case 5:
                    data_ = settings.Coeff5;
                    break;
                case 6:
                    data_ = settings.Coeff6;
                    break;
                case 7:
                    data_ = settings.Coeff7;
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CalCoefficients(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::CalFreeze(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_Cal_Freeze_Settings settings = {};

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalFreeze
        status = XRFdc_GetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Calibration_Coefficients
            switch (index) {
                case 0:
                    settings.CalFrozen = data_;
                    break;
                case 1:
                    settings.DisableFreezePin = data_;
                    break;
                case 2:
                    settings.FreezeCalibration = data_;
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalFreeze
            if (status == XRFDC_SUCCESS) {
                status = XRFdc_SetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);
            }

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Calibration_Coefficients
            switch (index) {
                case 0:
                    data_ = settings.CalFrozen;
                    break;
                case 1:
                    data_ = settings.DisableFreezePin;
                    break;
                case 2:
                    data_ = settings.FreezeCalibration;
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CalFreeze(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::Dither() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDither
            status = XRFdc_SetDither(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDither
            status = XRFdc_GetDither(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "Dither(): failed\n";
    }
}

void PyRFdc::DataScaler() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACDataScaler
            status = XRFdc_SetDACDataScaler(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDACDataScaler
            status = XRFdc_GetDACDataScaler(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DataScaler(): failed\n";
    }
}

void PyRFdc::ClockSource() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClockSource
        status = XRFdc_GetClockSource(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ClockSource(): failed\n";
    }
}

void PyRFdc::PLLConfig(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_PLL_Settings settings = {};

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPLLConfig
    status = XRFdc_GetPLLConfig(RFdcInstPtr_, tileType_, tileId_, &settings);

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_PLL_Settings
        switch (index) {
            case 0:
                data_ = settings.Enabled;
                break;
            case 1:
                data_  = DoubleToUint32(settings.RefClkFreq, false);
                break;
            case 2:
                data_  = DoubleToUint32(settings.RefClkFreq, true);
                break;
            case 3:
                data_  = DoubleToUint32(settings.SampleRate, false);
                break;
            case 4:
                data_  = DoubleToUint32(settings.SampleRate, true);
                break;
            case 5:
                data_ = settings.RefClkDivider;
                break;
            case 6:
                data_ = settings.FeedbackDivider;
                break;
            case 7:
                data_ = settings.OutputDivider;
                break;
            case 8:
                data_ = settings.FractionalMode;
                break;
            case 9:
                data_ = uint32_t(settings.FractionalData>>0 & 0xFFFFFFFFULL);
                break;
            case 10:
                data_ = uint32_t(settings.FractionalData>>32 & 0xFFFFFFFFULL);
                break;
            case 11:
                data_ = settings.FractWidth;
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "PLLConfig(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::PLLLockStatus() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPLLLockStatus
        status = XRFdc_GetPLLLockStatus(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_ = settings  + 1; // Adding a plus one help with software known when RemoteVariable not read yet
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "PLLLockStatus(): failed\n";
    }
}

void PyRFdc::LinkCoupling() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {

        ////////////////////////////////////////////////////////////////////////////////
        // XRFdc_GetLinkCoupling API Scheduled for deprication in 2024.1, please use the XRFdc_GetCoupling() API
        ////////////////////////////////////////////////////////////////////////////////
        // // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetLinkCoupling
        // status = XRFdc_GetLinkCoupling(RFdcInstPtr_, tileId_, blockId_, &settings);
        ////////////////////////////////////////////////////////////////////////////////

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoupling
        status = XRFdc_GetCoupling(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "LinkCoupling(): failed\n";
    }
}

void PyRFdc::DSA(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_DSA_Settings settings = {};

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDSA-Gen-3/DFE
        status = XRFdc_GetDSA(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_DSA_Settings-Gen-3/DFE
            switch (index) {
                case 0:
                    settings.DisableRTS = data_;
                    break;
                case 1:
                    // Copy from data_ to Attenuation
                    memcpy(&settings.Attenuation, &data_, sizeof(float));
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDSA-Gen-3/DFE
            if (status == XRFDC_SUCCESS) {
                status = XRFdc_SetDSA(RFdcInstPtr_, tileId_, blockId_, &settings);
            }

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_DSA_Settings-Gen-3/DFE
            switch (index) {
                case 0:
                    data_ = settings.DisableRTS;
                    break;
                case 1:
                    // Copy from Attenuation to data_
                    memcpy(&data_, &settings.Attenuation, sizeof(uint32_t));
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DSA(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::DACVOP() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACVOP-Gen-3/DFE
            status = XRFdc_SetDACVOP(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            status = XRFDC_FAILURE;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DACVOP(): failed\n";
    }
}

void PyRFdc::DACCompMode() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACCompMode-Gen-3/DFE
            status = XRFdc_SetDACCompMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDACCompMode-Gen-3/DFE
            status = XRFdc_GetDACCompMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DACCompMode(): failed\n";
    }
}

void PyRFdc::DataPathMode() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDataPathMode-Gen-3/DFE
            status = XRFdc_SetDataPathMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataPathMode-Gen-3/DFE
            status = XRFdc_GetDataPathMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DataPathMode(): failed\n";
    }
}

void PyRFdc::IMRPassMode() {
    int status = XRFDC_SUCCESS;
    uint32_t settings = data_;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetIMRPassMode-Gen-3/DFE
            status = XRFdc_SetIMRPassMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIMRPassMode-Gen-3/DFE
            status = XRFdc_GetIMRPassMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IMRPassMode(): failed\n";
    }
}

void PyRFdc::SignalDetector(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_Signal_Detector_Settings settings = {};

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetSignalDetector-Gen-3/DFE
        status = XRFdc_GetSignalDetector(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Signal_Detector_Settings-Gen-3/DFE
            switch (index) {
                case 0:
                    settings.Mode = uint8_t(data_&0xFF);
                    break;
                case 1:
                    settings.TimeConstant = uint8_t(data_&0xFF);
                    break;
                case 2:
                    settings.Flush = uint8_t(data_&0xFF);
                    break;
                case 3:
                    settings.EnableIntegrator = uint8_t(data_&0xFF);
                    break;
                case 4:
                    settings.Threshold = uint16_t(data_&0xFFFF);
                    break;
                case 5:
                    settings.ThreshOnTriggerCnt = uint16_t(data_&0xFFFF);
                    break;
                case 6:
                    settings.ThreshOffTriggerCnt = uint16_t(data_&0xFFFF);
                    break;
                case 7:
                    settings.HysteresisEnable = uint8_t(data_&0xFF);
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetSignalDetector-Gen-3/DFE
            if (status == XRFDC_SUCCESS) {
                status = XRFdc_SetSignalDetector(RFdcInstPtr_, tileId_, blockId_, &settings);
            }

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Signal_Detector_Settings-Gen-3/DFE
            switch (index) {
                case 0:
                    data_ = uint32_t(settings.Mode);
                    break;
                case 1:
                    data_ = uint32_t(settings.TimeConstant);
                    break;
                case 2:
                    data_ = uint32_t(settings.Flush);
                    break;
                case 3:
                    data_ = uint32_t(settings.EnableIntegrator);
                    break;
                case 4:
                    data_ = uint32_t(settings.Threshold);
                    break;
                case 5:
                    data_ = uint32_t(settings.ThreshOnTriggerCnt);
                    break;
                case 6:
                    data_ = uint32_t(settings.ThreshOffTriggerCnt);
                    break;
                case 7:
                    data_ = uint32_t(settings.HysteresisEnable);
                    break;
                default:
                    status = XRFDC_FAILURE;
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "SignalDetector(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::ResetInternalFIFOWidth() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetInternalFIFOWidth-Gen-3/DFE
        status = XRFdc_ResetInternalFIFOWidth(RFdcInstPtr_, tileType_, tileId_, blockId_);

    // Else read
    } else {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ResetInternalFIFOWidth(): failed\n";
    }
}

void PyRFdc::ResetInternalFIFOWidthObs() {
    int status = XRFDC_SUCCESS;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetInternalFIFOWidthObs-Gen-3/DFE
            status = XRFdc_ResetInternalFIFOWidthObs(RFdcInstPtr_, tileId_, blockId_);

        // Else read
        } else {
            data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ResetInternalFIFOWidthObs(): failed\n";
    }
}

void PyRFdc::PwrModeSettings(uint8_t index) {
    int status = XRFDC_SUCCESS;
    XRFdc_Pwr_Mode_Settings settings = {};

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPwrMode-Gen-3/DFE
    status = XRFdc_GetPwrMode(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Pwr_Mode_Settings-Gen-3/DFE
        switch (index) {
            case 0:
                settings.DisableIPControl = data_;
                break;
            case 1:
                settings.PwrMode = data_;
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetPwrMode-Gen-3/DFE
        if (status == XRFDC_SUCCESS) {
            status = XRFdc_SetPwrMode(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        }

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Pwr_Mode_Settings-Gen-3/DFE
        switch (index) {
            case 0:
                data_ = settings.DisableIPControl;
                break;
            case 1:
                data_ = settings.PwrMode;
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "PwrModeSettings(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::TileBaseAddr() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_TileBaseAddr
        data_ = XRFdc_Get_TileBaseAddr(RFdcInstPtr_, tileType_, tileId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "TileBaseAddr(): failed\n";
    }
}

void PyRFdc::BlockBaseAddr() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_BlockBaseAddr
        data_ = XRFdc_Get_BlockBaseAddr(RFdcInstPtr_, tileType_, tileId_, blockId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "BlockBaseAddr(): failed\n";
    }
}

void PyRFdc::NoOfADCBlocks() {
    int status = XRFDC_SUCCESS;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNoOfADCBlocks
            data_ = XRFdc_GetNoOfADCBlocks(RFdcInstPtr_, tileId_);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "NoOfADCBlocks(): failed\n";
    }
}

void PyRFdc::NoOfDACBlock() {
    int status = XRFDC_SUCCESS;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNoOfDACBlock
            data_ = XRFdc_GetNoOfDACBlock(RFdcInstPtr_, tileId_);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "NoOfDACBlock(): failed\n";
    }
}

void PyRFdc::IsADCBlockEnabled(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsADCBlockEnabled
            data_ = XRFdc_IsADCBlockEnabled(RFdcInstPtr_, tileId_, uint32_t(index));
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsADCBlockEnabled(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::IsDACBlockEnabled(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsDACBlockEnabled
            data_ = XRFdc_IsDACBlockEnabled(RFdcInstPtr_, tileId_, uint32_t(index));
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsDACBlockEnabled(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::IsHighSpeedADC() {
    int status = XRFDC_SUCCESS;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsHighSpeedADC
            data_ = XRFdc_IsHighSpeedADC(RFdcInstPtr_, tileId_);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsHighSpeedADC(): failed\n";
    }
}

void PyRFdc::DataType() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataType
        data_ = XRFdc_GetDataType(RFdcInstPtr_, tileType_, tileId_, blockId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DataType(): failed\n";
    }
}

void PyRFdc::DataWidth() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataWidth
        data_ = XRFdc_GetDataWidth(RFdcInstPtr_, tileType_, tileId_, blockId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DataWidth(): failed\n";
    }
}

void PyRFdc::InverseSincFilter() {
    int status = XRFDC_SUCCESS;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInverseSincFilter
            data_ = XRFdc_GetInverseSincFilter(RFdcInstPtr_, tileId_, blockId_);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "InverseSincFilter(): failed\n";
    }
}

void PyRFdc::MixedMode() {
    int status = XRFDC_SUCCESS;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixedMode
            data_ = XRFdc_GetMixedMode(RFdcInstPtr_, tileId_, blockId_);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MixedMode(): failed\n";
    }
}

void PyRFdc::MasterTile(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMasterTile
        data_ = XRFdc_GetMasterTile(RFdcInstPtr_, uint32_t(index));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MasterTile(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::SysRefSource(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetSysRefSource
        data_ = XRFdc_GetSysRefSource(RFdcInstPtr_, uint32_t(index));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "SysRefSource(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::IPBaseAddr() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_IPBaseAddr
        data_ = XRFdc_Get_IPBaseAddr(RFdcInstPtr_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IPBaseAddr(): failed\n";
    }
}

void PyRFdc::FabClkFreq(bool upper) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabClkFreq
        data_ = DoubleToUint32(XRFdc_GetFabClkFreq(RFdcInstPtr_, tileType_, tileId_), upper);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FabClkFreq(" + std::to_string(upper) + "): failed\n";
    }
}

void PyRFdc::IsFifoEnabled() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsFifoEnabled
        data_ = XRFdc_IsFifoEnabled(RFdcInstPtr_, tileType_, tileId_, blockId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsFifoEnabled(): failed\n";
    }
}

void PyRFdc::DriverVersion(bool upper) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDriverVersion
        data_ = DoubleToUint32(XRFdc_GetDriverVersion(), upper);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DriverVersion(" + std::to_string(upper) + "): failed\n";
    }
}

void PyRFdc::ConnectedIData() {
    int status = XRFDC_SUCCESS;
    int convNum;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetConnectedIData
        convNum = XRFdc_GetConnectedIData(RFdcInstPtr_, tileType_, tileId_, blockId_);
        // Copy from convNum to data_
        memcpy(&data_, &convNum, sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ConnectedIData(): failed\n";
    }
}

void PyRFdc::ConnectedQData() {
    int status = XRFDC_SUCCESS;
    int convNum;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetConnectedQData
        convNum = XRFdc_GetConnectedQData(RFdcInstPtr_, tileType_, tileId_, blockId_);
        // Copy from convNum to data_
        memcpy(&data_, &convNum, sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ConnectedQData(): failed\n";
    }
}

void PyRFdc::IsADCDigitalPathEnabled() {
    int status = XRFDC_SUCCESS;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Else ADC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsADCDigitalPathEnabled
            data_ = XRFdc_IsADCDigitalPathEnabled(RFdcInstPtr_, tileId_, blockId_) + 1; // Adding a plus one help with software known when RemoteVariable not read yet
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsADCDigitalPathEnabled(): failed\n";
    }
}

void PyRFdc::IsDACDigitalPathEnabled() {
    int status = XRFDC_SUCCESS;

    // Check for ADC tile
    if (isADC_) {
        status = XRFDC_FAILURE;

    // Else DAC tile
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFDC_FAILURE;

        // Else read
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsDACDigitalPathEnabled
            data_ = XRFdc_IsDACDigitalPathEnabled(RFdcInstPtr_, tileId_, blockId_) + 1; // Adding a plus one help with software known when RemoteVariable not read yet
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IsDACDigitalPathEnabled(): failed\n";
    }
}

void PyRFdc::CheckDigitalPathEnabled() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckDigitalPathEnabled
        data_ = XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, tileType_, tileId_, blockId_) + 1; // Adding a plus one help with software known when RemoteVariable not read yet
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CheckDigitalPathEnabled(): failed\n";
    }
}

void PyRFdc::CheckBlockEnabled(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckBlockEnabled
        data_ = XRFdc_CheckBlockEnabled(RFdcInstPtr_, tileType_, tileId_, uint32_t(index)) + 1; // Adding a plus one help with software known when RemoteVariable not read yet
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CheckBlockEnabled(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::CheckTileEnabled(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckTileEnabled
        data_ = XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, uint32_t(index)) + 1; // Adding a plus one help with software known when RemoteVariable not read yet
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CheckTileEnabled(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::TileLayout() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetTileLayout
        data_ = uint32_t(XRFdc_GetTileLayout(RFdcInstPtr_));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "TileLayout(): failed\n";
    }
}

void PyRFdc::MultibandConfig() {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMultibandConfig
        data_ = XRFdc_GetMultibandConfig(RFdcInstPtr_, tileType_, tileId_);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MultibandConfig(): failed\n";
    }
}

void PyRFdc::MaxSampleRate(bool upper) {
    int status = XRFDC_SUCCESS;
    double settings = 0.0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMaxSampleRate
        status = XRFdc_GetMaxSampleRate(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_ = DoubleToUint32(settings, upper);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MaxSampleRate(" + std::to_string(upper) + "): failed\n";
    }
}

void PyRFdc::MinSampleRate(bool upper) {
    int status = XRFDC_SUCCESS;
    double settings = 0.0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMinSampleRate
        status = XRFdc_GetMinSampleRate(RFdcInstPtr_, tileType_, tileId_, &settings);
        data_ = DoubleToUint32(settings, upper);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MinSampleRate(" + std::to_string(upper) + "): failed\n";
    }
}

void PyRFdc::DynamicPLLConfig(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        switch (index) {
            case 0:
                pllConfig_[tileType_][tileId_].RefClkFreq = RemapDoubleWithUint32(pllConfig_[tileType_][tileId_].RefClkFreq, data_, false);
                break;
            case 1:
                pllConfig_[tileType_][tileId_].RefClkFreq = RemapDoubleWithUint32(pllConfig_[tileType_][tileId_].RefClkFreq, data_, true);
                break;
            case 2:
                pllConfig_[tileType_][tileId_].SampleRate = RemapDoubleWithUint32(pllConfig_[tileType_][tileId_].SampleRate, data_, false);
                break;
            case 3:
                pllConfig_[tileType_][tileId_].SampleRate = RemapDoubleWithUint32(pllConfig_[tileType_][tileId_].SampleRate, data_, true);
                break;
            case 4:
                clkSrcConfig_[tileType_][tileId_] = data_;
                break;
            case 5:
                {
                    uint64_t seq0 = metalRingSeqNow();

                    // The commit restarts the tile, so the MTS outputs are stale
                    ClearMtsValidForRestart((tileType_ == XRFDC_ADC_TILE) ? (1U << tileId_) : 0U,
                                            (tileType_ == XRFDC_DAC_TILE) ? (1U << tileId_) : 0U);

                    // A refused commit leaves status at success on purpose: the
                    // refusal text is already in errMsg_ and must not be replaced
                    uint32_t firstState = 0;
                    uint32_t lastState = 0;
                    RestartWait wait = RestartPrecheck(tileType_, tileId_, firstState, lastState);
                    if (!RestartProceed("DynamicPLLConfig", tileType_, tileId_, wait, firstState, lastState, seq0)) {
                        break;
                    }

                    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_DynamicPLLConfig
                    status = XRFdc_DynamicPLLConfig(RFdcInstPtr_, tileType_, tileId_, uint8_t(clkSrcConfig_[tileType_][tileId_]), pllConfig_[tileType_][tileId_].RefClkFreq, pllConfig_[tileType_][tileId_].SampleRate);

                    // The driver keeps its own PLL cache in step; only the staging is refreshed
                    if ((status == XRFDC_SUCCESS) && !RefreshStaging(tileType_, tileId_)) {
                        log_->warning("staging not refreshed: DynamicPLLConfig %s tile %u",
                                      (tileType_ == XRFDC_ADC_TILE) ? "ADC" : "DAC", uint32_t(tileId_));
                    }
                }
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }

    // Else read
    } else {
        switch (index) {
            case 0:
                data_ = DoubleToUint32(pllConfig_[tileType_][tileId_].RefClkFreq, false);
                break;
            case 1:
                data_ = DoubleToUint32(pllConfig_[tileType_][tileId_].RefClkFreq, true);
                break;
            case 2:
                data_ = DoubleToUint32(pllConfig_[tileType_][tileId_].SampleRate, false);
                break;
            case 3:
                data_ = DoubleToUint32(pllConfig_[tileType_][tileId_].SampleRate, true);
                break;
            case 4:
                data_ = clkSrcConfig_[tileType_][tileId_];
                break;
            case 5:
                data_ = 1; // Always return 1 so this is a set() and not posted() cmd
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "DynamicPLLConfig(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::IntrEnable() {
    int status = XRFDC_SUCCESS;
    uint32_t enableMask = 0;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

    // Else write
    } else {
        enableMask = data_;
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrEnable
        status = XRFdc_IntrEnable(RFdcInstPtr_, tileType_, tileId_, blockId_, enableMask);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        if (rdTxn_) {
            errMsg_ = "IntrEnable(): failed\n";
        } else {
            errMsg_ = "IntrEnable(): XRFdc_IntrEnable failed, code " + std::to_string(status) + "\n";
        }
    }
}

void PyRFdc::IntrDisable() {
    int status = XRFDC_SUCCESS;
    uint32_t disableMask = 0;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

    // Else write
    } else {
        disableMask = data_;
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrDisable
        status = XRFdc_IntrDisable(RFdcInstPtr_, tileType_, tileId_, blockId_, disableMask);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        if (rdTxn_) {
            errMsg_ = "IntrDisable(): failed\n";
        } else {
            errMsg_ = "IntrDisable(): XRFdc_IntrDisable failed, code " + std::to_string(status) + "\n";
        }
    }
}

// Registering an interrupt handler probably makes no sense in this context, so
// the following function is excluded.
// void XRFdc_SetStatusHandler(XRFdc *InstancePtr, void *CallBackRefPtr,  XRFdc_StatusHandler FunctionPtr);
// where the callback would be
// u32 XRFdc_IntrHandler(u32 Vector, void *XRFdcPtr);

void PyRFdc::IntrClr() {
    int status = XRFDC_SUCCESS;
    uint32_t clearMask = 0;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

    // Else write
    } else {
        clearMask = data_;
        metal_log(METAL_LOG_DEBUG, "Clear interrupts with mask: 0x%08X\n", clearMask);
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrClr
        status = XRFdc_IntrClr(RFdcInstPtr_, tileType_, tileId_, blockId_, clearMask);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IntrClr(): failed\n";
    }
}

void PyRFdc::GetIntrStatus() {
    int status = XRFDC_SUCCESS;
    uint32_t intrStatusMask = 0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIntrStatus
        status = XRFdc_GetIntrStatus(RFdcInstPtr_, tileType_, tileId_, blockId_, &intrStatusMask);
        data_ = intrStatusMask;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "GetIntrStatus(): failed\n";
    }
}

void PyRFdc::GetEnabledInterrupts() {
    int status = XRFDC_SUCCESS;
    uint32_t intrEnabledMask = 0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetEnabledInterrupts
        status = XRFdc_GetEnabledInterrupts(RFdcInstPtr_, tileType_, tileId_, blockId_, &intrEnabledMask);
        data_ = intrEnabledMask;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "GetEnabledInterrupts(): failed\n";
    }
}

void PyRFdc::MtsEnabled() {
    int status = XRFDC_SUCCESS;
    int i;
    uint32_t EnablePtr = 0;
    uint32_t settings = 0;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Loop through the tiles
        for (i = 0; i < 4; i++) {

            // Set the value
            EnablePtr = 0;

            // Check if TILE is enabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, i) == XRFDC_SUCCESS) {
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMTSEnable
                uint32_t rc = XRFdc_GetMTSEnable(RFdcInstPtr_, tileType_, i, &EnablePtr);
                if (rc != XRFDC_SUCCESS) {
                    errMsg_ = "MtsEnabled(" + std::to_string(tileType_) + "): XRFdc_GetMTSEnable failed on tile " +
                              std::to_string(i) + ", code " + std::to_string(rc) + "\n";
                    break;
                }
                settings |= ((EnablePtr&0x1)<<i);
            }
        }
        // Return the MTS enabled bit mask unless a query failed
        if (errMsg_.empty()) {
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MtsEnabled(" + std::to_string(tileType_) + "): failed\n";
    }
}

void PyRFdc::MtsRefTile() {
    // Check for a write
    if (!rdTxn_) {

        if (data_ > XRFDC_TILE_ID_MAX) {
            errMsg_ = "MtsRefTile(" + std::to_string(tileType_) + "): failed\n";
        } else {
            mtsConfig_[tileType_].RefTile = data_;
        }

    // Else Read
    } else {
        data_ = mtsConfig_[tileType_].RefTile;

    }
}

void PyRFdc::MtsSysrefConfig() {
    // Check if read
    if (rdTxn_) {
        data_ = (XRFdc_ReadReg(RFdcInstPtr_, (XRFDC_DRP_BASE(XRFDC_DAC_TILE, 0) + XRFDC_HSCOM_ADDR), XRFDC_MTS_SRCAP_T1)>>10)&0x1;
    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MTS_Sysref_Config
        uint32_t rc = XRFdc_MTS_Sysref_Config(RFdcInstPtr_,  &mtsConfig_[XRFDC_DAC_TILE],  &mtsConfig_[XRFDC_ADC_TILE], (data_&0x1));
        if (rc != XRFDC_MTS_OK) {
            errMsg_ = "MtsSysrefConfig(): XRFdc_MTS_Sysref_Config failed, code " + std::to_string(rc) + "\n";
        }
    }
}

void PyRFdc::MtsSysRefEnable() {
    // Check for a write
    if (!rdTxn_) {
        mtsConfig_[tileType_].SysRef_Enable = data_;

    // Else Read
    } else {
        data_ = mtsConfig_[tileType_].SysRef_Enable;

    }
}

void PyRFdc::MtsTargetLatency() {
    // Check for a write
    if (!rdTxn_) {
        // Copy from data_ to mtsConfig_[tileType_].Target_Latency
        memcpy(&mtsConfig_[tileType_].Target_Latency, &data_, sizeof(int32_t));

    // Else Read
    } else {
        // Copy from mtsConfig_[tileType_].Target_Latency to data_
        memcpy(&data_, &mtsConfig_[tileType_].Target_Latency, sizeof(uint32_t));
    }
}

void PyRFdc::MtsTiles() {
    // Check for a write
    if (!rdTxn_) {
        mtsConfig_[tileType_].Tiles = data_;

    // Else Read
    } else {
        data_ = mtsConfig_[tileType_].Tiles;
    }
}

void PyRFdc::MtsSync() {
    int status, i;

    // Check for a write
    if (!rdTxn_) {

        // Not valid until this sync completes, whatever the outcome
        mtsValid_[tileType_] = false;

        // Reset status values
        for(i=0; i<4; i++) {
            mtsfactor_[tileType_][i] = 0;
            mtsConfig_[tileType_].Latency[i] = 0;
            mtsConfig_[tileType_].Offset[i] = 0;
        }

        // With no tile in the group the driver would still scan the reference tile
        if (mtsConfig_[tileType_].Tiles == 0) {
            errMsg_ = "MtsSync(" + std::to_string(tileType_) + "): refused, the Tiles mask is zero; set AdcTiles or DacTiles first\n";
            txnRefused_ = true;
            return;
        }

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Sync
        status = XRFdc_MultiConverter_Sync(RFdcInstPtr_, tileType_, &mtsConfig_[tileType_]);
        if (status == XRFDC_MTS_OK) {
            mtsValid_[tileType_] = true;
            for(i=0; i<4; i++) {
                if((1<<i)&mtsConfig_[tileType_].Tiles) {
                    uint32_t rc;
                    uint32_t blk;
                    std::string failed;

                    // The factor getters need the digital path of the block they read, so the
                    // factor comes from the first block with an enabled digital path. The driver
                    // needs block 0 for a synced tile, so block 0 is chosen on every current board.
                    for (blk = 0; blk < 4; blk++) {
                        if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, tileType_, i, blk) == XRFDC_SUCCESS) {
                            break;
                        }
                    }
                    if (blk == 4) {
                        mtsValid_[tileType_] = false;
                        mtsfactor_[tileType_][i] = 0;
                        errMsg_ = "MtsSync(" + std::to_string(tileType_) + "): no enabled block on tile " +
                                  std::to_string(i) + " to read the factor from\n";
                        break;
                    }
                    if (tileType_ == XRFDC_ADC_TILE) {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
                        rc = XRFdc_GetDecimationFactor(RFdcInstPtr_, i, blk, &mtsfactor_[tileType_][i]);
                        failed = "XRFdc_GetDecimationFactor failed";
                    } else {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
                        rc = XRFdc_GetInterpolationFactor(RFdcInstPtr_, i, blk, &mtsfactor_[tileType_][i]);
                        failed = "XRFdc_GetInterpolationFactor failed";
                    }
                    if (rc != XRFDC_SUCCESS) {
                        // The sync itself completed but its factor is unknown: not valid
                        mtsValid_[tileType_] = false;
                        mtsfactor_[tileType_][i] = 0;
                        errMsg_ = "MtsSync(" + std::to_string(tileType_) + "): " + failed + " on tile " +
                                  std::to_string(i) + " block " + std::to_string(blk) + ", code " + std::to_string(rc) + "\n";
                        break;
                    }
                }
            }

        } else {
            errMsg_ = "MtsSync(" + std::to_string(tileType_) + "): Multi-Tile-Sync did not complete successfully. Error code (" + std::to_string(status) + ")\n";
        }

    // Else Read
    } else {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
    }
}

void PyRFdc::MtsValid() {
    // Check if write
    if (!rdTxn_) {
        errMsg_ = "MtsValid(" + std::to_string(tileType_) + "): read-only\n";

    // Else read
    } else {
        // The latch says a sync took; the group must also be up now. A tile of the group that
        // is below state 15 or still restarting reads 0, which covers a restart PyRFdc did
        // not command. The read never changes the latch.
        bool valid = mtsValid_[tileType_];
        if (valid) {
            uint32_t group = MtsGroupMask(mtsConfig_[tileType_].Tiles, mtsConfig_[tileType_].RefTile);
            for (uint32_t t = 0; t < 4; t++) {
                if ((((group >> t) & 0x1) != 0) && (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, t) == XRFDC_SUCCESS) &&
                    !TileReady(tileType_, t)) {
                    valid = false;
                    break;
                }
            }
        }
        data_ = valid ? 1 : 0;
    }
}

void PyRFdc::MtsLatency(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Copy from mtsConfig_[tileType_].Latency[index] to data_
        memcpy(&data_, &mtsConfig_[tileType_].Latency[index], sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MtsLatency(" + std::to_string(tileType_) + "): failed\n";
    }
}

void PyRFdc::MtsOffset(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Copy from mtsConfig_[tileType_].Offset[index] to data_
        memcpy(&data_, &mtsConfig_[tileType_].Offset[index], sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MtsOffset(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::MtsFactor(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        data_ = mtsfactor_[tileType_][index];
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MtsFactor(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::IpVersion() {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/IP-Version-Information-0x0000
        data_ = XRFdc_ReadReg(RFdcInstPtr_, 0x0, 0x0);

    // Else write
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IpVersion(): failed\n";
    }
}

void PyRFdc::RestartSM() {

    // Check if read: the live Restart register, 1 while the power-on state
    // machine runs toward its end state and 0 when it is idle
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-Power-On-State-Machine-Register-0x0004
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), XRFDC_RESTART_OFFSET) & XRFDC_RESTART_MASK;

    // Else write
    } else {
        uint64_t seq0 = metalRingSeqNow();

        // The MTS outputs are stale from the moment the tile restarts
        ClearMtsValidForRestart((tileType_ == XRFDC_ADC_TILE) ? (1U << tileId_) : 0U,
                                (tileType_ == XRFDC_DAC_TILE) ? (1U << tileId_) : 0U);

        // A restart written into a state machine that is still moving corrupts it
        uint32_t firstState = 0;
        uint32_t lastState = 0;
        RestartWait wait = RestartPrecheck(tileType_, tileId_, firstState, lastState);
        if (!RestartProceed("RestartSM", tileType_, tileId_, wait, firstState, lastState, seq0)) {
            return;
        }

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-Power-On-State-Machine-Register-0x0004
        XRFdc_WriteReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), XRFDC_RESTART_OFFSET, XRFDC_RESTART_MASK);

        // The sequence runs on its own, so the staging is left as it was
        log_->info("staging not refreshed: RestartSM is asynchronous");
    }
}

void PyRFdc::RestartState() {
    // Check if read
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-State-Register-0x0008
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), XRFDC_RESTART_STATE_OFFSET);
    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-State-Register-0x0008
        XRFdc_ClrSetReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_),  XRFDC_RESTART_STATE_OFFSET, XRFDC_PWR_STATE_MASK, data_);
    }
}

void PyRFdc::ClockDetector() {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Clock-Detector-Register-0x0084-Gen-3/DFE
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), 0x0084);

    // Else write
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "ClockDetector(): failed\n";
    }
}

void PyRFdc::TileCommonStatus() {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/RF-DAC/RF-ADC-Tile-n-Common-Status-Register-0x0228
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), 0x0228);

    // Else write
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "TileCommonStatus(): failed\n";
    }
}

void PyRFdc::TileCurrentState() {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Current-State-Register-0x000C
        data_ = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), 0x000C);

    // Else write
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "TileCurrentState(): failed\n";
    }
}

void PyRFdc::MetalLogLevel() {
    // Check for a write
    if (!rdTxn_) {
        metalLogLevel_ = bool(data_&0x1);

        // Set log level based on debugPrint flag
        if (metalLogLevel_) {
            metal_set_log_level(METAL_LOG_DEBUG);
        } else {
            metal_set_log_level(METAL_LOG_ERROR);
        }

    } else {
        data_ = uint32_t(metalLogLevel_);
    }
}

void PyRFdc::IgnoreMetalError() {
    // Check for a write
    if (!rdTxn_) {
        ignoreMetalError_ = bool(data_&0x1);
    } else {
        data_ = uint32_t(ignoreMetalError_);
    }
}

void PyRFdc::ScratchPad() {
    // Check for a write
    if (!rdTxn_) {
        scratchPad_ = data_;
    } else {
        data_ = scratchPad_;
    }
}

void PyRFdc::DoubleTestReg(bool upper) {
    // Check for a write
    if (!rdTxn_) {
        doubleTestReg_ = RemapDoubleWithUint32(doubleTestReg_,data_,upper);
    } else {
        data_ = DoubleToUint32(doubleTestReg_,upper);
    }
}

//! Function to convert a double into a uint32 with little endianness
uint32_t PyRFdc::DoubleToUint32(double value, bool upper) {

    uint64_t temp;
    std::memcpy(&temp, &value, sizeof(double));  // Copy double into a uint64_t

    if (upper) {
        return static_cast<uint32_t>(temp >> 32);  // Upper 32 bits
    } else {
        return static_cast<uint32_t>(temp);        // Lower 32 bits
    }
}

//! Function to convert a uint32 into double with little endianness
double PyRFdc::RemapDoubleWithUint32(double original, uint32_t newPart, bool upper) {
    uint64_t temp;
    std::memcpy(&temp, &original, sizeof(double));  // Copy double into a uint64_t

    if (upper) {
        temp = (static_cast<uint64_t>(newPart) << 32) | (temp & 0xFFFFFFFFULL);  // Replace upper 32 bits
    } else {
        temp = (temp & 0xFFFFFFFF00000000ULL) | static_cast<uint64_t>(newPart);  // Replace lower 32 bits
    }

    double newValue;
    std::memcpy(&newValue, &temp, sizeof(double));  // Copy modified uint64_t back into double
    return newValue;
}

//! Post a transaction. Master will call this method with the access attributes.
void PyRFdc::doTransaction(rim::TransactionPtr tran) {
    int32_t  size = int32_t(tran->size());
    uint32_t addr = uint32_t(tran->address() & 0xFFFFFFFFULL);
    uint8_t* ptr  = tran->begin();
    uint32_t tileAddr = 0;
    uint32_t blockAddr = 0;
    uint32_t wrdIdx = 0;
    bool tileOnly = false;
    bool refused = false;
    std::string err;  // consumed only after mtx_ is released
    std::string why;  // write gate refusal text

    rim::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

        // clear and read errMsg_ only while mtx_ is held
        errMsg_.clear();
        txnRefused_ = false;
        clkDistFetched_ = false;

        while (size > 0)
        {
            // Copy from (ptr + wrdIdx) to data
            memcpy(&data_, ptr+wrdIdx, sizeof(uint32_t));

            // Check the type of transaction
            if (tran->type() == rim::Write || tran->type() == rim::Post) {
                // Set the flag
                rdTxn_ = false;
            } else {
                // Set the flag
                rdTxn_ = true;
            }

            // Check if ADC/DAC type - BIT15
            isADC_ = (((addr >> 15) & 0x1) == 0x0);
            tileType_ = isADC_ ? XRFDC_ADC_TILE : XRFDC_DAC_TILE;

            // Get TILE ID - BIT14:BIT13
            tileId_ = (addr>>13)&0x3;
            tileAddr = addr&0xFFF;

            // Check if tile only - BIT12
            tileOnly = (((addr >> 12) & 0x1) == 0x0);

            // Get BLOCK ID and block address - BIT11:BIT10
            blockId_  = (addr>>10)&0x3;
            blockAddr = addr&0x3FF;

            ////////////////////////////////////////////////////////////////
            // Not-initialized guard: refuse everything outside the allow-list
            // while the RFDC config ROM has not loaded a valid XRFdc_Config
            ////////////////////////////////////////////////////////////////
            if ((cfgStatus_ != CfgOk) && !DriverFree(addr)) {
                errMsg_ = "PyRFdc not initialized: " + cfgMessage_;
                txnRefused_ = true;
                if (rdTxn_) {
                    data_ = 0;
                }

            ////////////////////////////////////////////////////////////////
            // Write gate: a write that reaches a driver setter or a tile or
            // block register is refused, with no wait and no hardware write,
            // while any tile in its scope has Restart set or is below state
            // 15. Staging words and restart commands pass. A refusal sets
            // txnRefused_ so IgnoreMetalError cannot swallow it, and ends the
            // transaction so no later word reaches a handler.
            ////////////////////////////////////////////////////////////////
            } else if (!rdTxn_ && !WriteAllowed(addr, why)) {
                errMsg_ = why;
                txnRefused_ = true;
                log_->warning("%s", why.c_str());
                break;

            ////////////////////////////////////////////////////////////////
            // 1st check for the global registers access and commands
            ////////////////////////////////////////////////////////////////
            } else if (addr==0x10000) {
                tileType_ = XRFDC_ADC_TILE;
                StartUp(-1);

            } else if (addr==0x10004) {
                tileType_ = XRFDC_DAC_TILE;
                StartUp(-1);

            } else if (addr==0x10008) {
                tileType_ = XRFDC_ADC_TILE;
                Shutdown(-1);

            } else if (addr==0x1000C) {
                tileType_ = XRFDC_DAC_TILE;
                Shutdown(-1);

            } else if (addr==0x10010) {
                tileType_ = XRFDC_ADC_TILE;
                Reset(-1);

            } else if (addr==0x10014) {
                tileType_ = XRFDC_DAC_TILE;
                Reset(-1);

            } else if (addr==0x10018) {
                tileType_ = XRFDC_ADC_TILE;
                CustomStartUp(-1);

            } else if (addr==0x1001C) {
                tileType_ = XRFDC_DAC_TILE;
                CustomStartUp(-1);

            } else if (addr==0x10020) {
                tileType_ = XRFDC_ADC_TILE;
                SetupFIFO(-1);

            } else if (addr==0x10024) {
                tileType_ = XRFDC_DAC_TILE;
                SetupFIFO(-1);

            } else if (addr==0x10028) {
                tileType_ = XRFDC_ADC_TILE;
                SetupFIFOObs(-1);

            } else if (addr==0x1002C) {
                tileType_ = XRFDC_ADC_TILE;
                SetupFIFOBoth(-1);

            } else if ( (addr >= 0x10030) && (addr <= 0x10034) ) {
                MasterTile((addr>>2)&0x1);

            } else if ( (addr >= 0x10038) && (addr <= 0x1003C) ) {
                SysRefSource((addr>>2)&0x1);

            } else if (addr==0x10040) {
                IPBaseAddr();

            } else if (addr==0x10044) {
                IpVersion();

            } else if ( (addr >= 0x10048) && (addr <= 0x1004C) ) {
                DriverVersion(bool((addr>>2)&0x1));

            } else if ( (addr >= 0x10050) && (addr <= 0x1005C) ) {
                tileType_ = XRFDC_ADC_TILE;
                CheckTileEnabled((addr>>2)&0x3);

            } else if ( (addr >= 0x10060) && (addr <= 0x1006C) ) {
                tileType_ = XRFDC_DAC_TILE;
                CheckTileEnabled((addr>>2)&0x3);

            } else if (addr==0x10070) {
                TileLayout();

            } else if (addr==0x11000) {
                tileType_ = XRFDC_ADC_TILE;
                MtsEnabled();

            } else if (addr==0x11004) {
                tileType_ = XRFDC_DAC_TILE;
                MtsEnabled();

            } else if (addr==0x11008) {
                tileType_ = XRFDC_ADC_TILE;
                MtsSync();

            } else if (addr==0x1100C) {
                tileType_ = XRFDC_DAC_TILE;
                MtsSync();

            } else if (addr==0x11010) {
                tileType_ = XRFDC_ADC_TILE;
                MtsRefTile();

            } else if (addr==0x11014) {
                tileType_ = XRFDC_DAC_TILE;
                MtsRefTile();

            } else if (addr==0x11018) {
                tileType_ = XRFDC_ADC_TILE;
                MtsSysRefEnable();

            } else if (addr==0x1101C) {
                tileType_ = XRFDC_DAC_TILE;
                MtsSysRefEnable();

            } else if (addr==0x11020) {
                tileType_ = XRFDC_ADC_TILE;
                MtsTargetLatency();

            } else if (addr==0x11024) {
                tileType_ = XRFDC_DAC_TILE;
                MtsTargetLatency();

            } else if (addr==0x11028) {
                tileType_ = XRFDC_ADC_TILE;
                MtsTiles();

            } else if (addr==0x1102C) {
                tileType_ = XRFDC_DAC_TILE;
                MtsTiles();

            } else if (addr==0x11030) {
                tileType_ = XRFDC_ADC_TILE;
                MtsValid();

            } else if (addr==0x11034) {
                tileType_ = XRFDC_DAC_TILE;
                MtsValid();

            } else if (addr==0x11100) {
                MtsSysrefConfig();

            } else if ( (addr >= 0x11200) && (addr <= 0x1120C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MtsLatency((addr>>2)&0x3);

            } else if ( (addr >= 0x11210) && (addr <= 0x1121C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MtsLatency((addr>>2)&0x3);

            } else if ( (addr >= 0x11220) && (addr <= 0x1122C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MtsOffset((addr>>2)&0x3);

            } else if ( (addr >= 0x11230) && (addr <= 0x1123C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MtsOffset((addr>>2)&0x3);

            } else if ( (addr >= 0x11240) && (addr <= 0x1124C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MtsFactor((addr>>2)&0x3);

            } else if ( (addr >= 0x11250) && (addr <= 0x1125C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MtsFactor((addr>>2)&0x3);

            } else if (addr==0x12000) {
                MetalLogLevel();

            } else if (addr==0x12004) {
                IgnoreMetalError();

            } else if (addr==0x12008) {
                ScratchPad();

            } else if ( (addr >= 0x13000) && (addr <= 0x13004) ) {
                DoubleTestReg(bool((addr>>2)&0x1));

            } else if ( (addr >= 0x14000) && (addr <= 0x141FC) ) {
                ConfigStatusReg(addr);

            } else if ( (addr >= 0x15000) && (addr <= 0x157FC) ) {
                ClkDistReg(addr);

            } else if ( (addr >= 0x15800) && (addr <= 0x1586C) ) {
                ClkDistStagingReg(addr);

            } else if (addr==0x15900) {
                ClkDistRefreshCmd();

            } else if (addr==0x15904) {
                ClkDistCommit();

            } else if ( (addr >= 0x15908) && (addr <= 0x15928) ) {
                ClkDistPreviewReg(addr);

            } else if (addr<0x10000) {

                ////////////////////////////////////////////////////////////////
                // Check for the tile only registers access and commands
                ////////////////////////////////////////////////////////////////
                if (tileOnly) {

                    if (tileAddr==0x000) {
                        StartUp(tileId_);

                    } else if (tileAddr==0x004) {
                        Shutdown(tileId_);

                    } else if (tileAddr==0x008) {
                        Reset(tileId_);

                    } else if (tileAddr==0x00C) {
                        CustomStartUp(tileId_);

                    } else if (tileAddr==0x010) {
                        GetIPStatus();

                    } else if (tileAddr==0x014) {
                        FabClkOutDiv();

                    } else if (tileAddr==0x018) {
                        SetupFIFO(tileId_);

                    } else if (tileAddr==0x01C) {
                        SetupFIFOObs(tileId_);

                    } else if (tileAddr==0x020) {
                        SetupFIFOBoth(tileId_);

                    } else if (tileAddr==0x024) {
                        FIFOStatus();

                    } else if (tileAddr==0x028) {
                        FIFOStatusObs();

                    } else if (tileAddr==0x02C) {
                        ClockSource();

                    } else if ( (tileAddr >= 0x030) && (tileAddr <= 0x05C) ) {
                        PLLConfig( (tileAddr-0x030)>>2 );

                    } else if (tileAddr==0x060) {
                        PLLLockStatus();

                    } else if (tileAddr==0x064) {
                        TileBaseAddr();

                    } else if (tileAddr==0x068) {
                        NoOfADCBlocks();

                    } else if (tileAddr==0x06C) {
                        NoOfDACBlock();

                    } else if ( (tileAddr >= 0x070) && (tileAddr <= 0x07C) ) {
                        IsADCBlockEnabled((tileAddr>>2)&0x3);

                    } else if ( (tileAddr >= 0x080) && (tileAddr <= 0x08C) ) {
                        IsDACBlockEnabled((tileAddr>>2)&0x3);

                    } else if (tileAddr==0x090) {
                        IsHighSpeedADC();

                    } else if (tileAddr==0x094) {
                        MultibandConfig();

                    } else if ( (tileAddr >= 0x098) && (tileAddr <= 0x09C) ) {
                        FabClkFreq(bool((tileAddr>>2)&0x1));

                    } else if ( (tileAddr >= 0x0A0) && (tileAddr <= 0x0AC) ) {
                        tileType_ = XRFDC_ADC_TILE;
                        CheckBlockEnabled((tileAddr>>2)&0x3);

                    } else if ( (tileAddr >= 0x0B0) && (tileAddr <= 0x0BC) ) {
                        tileType_ = XRFDC_DAC_TILE;
                        CheckBlockEnabled((tileAddr>>2)&0x3);

                    } else if ( (tileAddr >= 0x0C0) && (tileAddr <= 0x0C4) ) {
                        MaxSampleRate(bool((tileAddr>>2)&0x1));

                    } else if ( (tileAddr >= 0x0C8) && (tileAddr <= 0x0CC) ) {
                        MinSampleRate(bool((tileAddr>>2)&0x1));

                    } else if ( (tileAddr >= 0x100) && (tileAddr <= 0x11C) ) {
                        DynamicPLLConfig((tileAddr>>2)&0x7);

                    } else if (tileAddr==0x800) {
                        RestartSM();

                    } else if (tileAddr==0x804) {
                        RestartState();

                    } else if (tileAddr==0x808) {
                        ClockDetector();

                    } else if (tileAddr==0x80C) {
                        TileCommonStatus();

                    } else if (tileAddr==0x810) {
                        TileCurrentState();

                    } else if (tileAddr==0x814) {
                        ResetRecordReg(tileAddr);

                    } else if (tileAddr==0x818) {
                        ResetRecordReg(tileAddr);

                    } else if (tileAddr==0x81C) {
                        ResetRecordReg(tileAddr);

                    } else if (tileAddr==0x820) {
                        ResetRecordReg(tileAddr);

                    } else if (tileAddr==0x824) {
                        ResetRecordReg(tileAddr);

                    } else if ( (tileAddr >= 0x200) && (tileAddr <= 0x23C) ) {
                        RomMixerConfigReg(tileAddr);

                    } else if (tileAddr==0x828) {
                        RefreshStagingCmd();

                    } else {
                        errMsg_ = "Undefined memory";
                    }

                ////////////////////////////////////////////////////////////////
                // Else check for the tile + block registers access and commands
                ////////////////////////////////////////////////////////////////
                } else {

                    if ( (blockAddr >= 0x000) && (blockAddr <= 0x008) ) {
                        GetBlockStatus((blockAddr>>2)&0x3);

                    } else if ( (blockAddr >= 0x020) && (blockAddr <= 0x03C) ) {
                        MixerSettings((blockAddr>>2)&0x7);

                    } else if ( (blockAddr >= 0x040) && (blockAddr <= 0x05C) ) {
                        QMCSettings((blockAddr>>2)&0x7);

                    } else if (blockAddr==0x060) {
                        CoarseDelaySettings();

                    } else if (blockAddr==0x064) {
                        UpdateEvent(XRFDC_EVENT_CRSE_DLY);

                    } else if (blockAddr==0x068) {
                        InterpolationFactor();

                    } else if (blockAddr==0x070) {
                        DecimationFactor();

                    } else if (blockAddr==0x074) {
                        DecimationFactorObs();

                    } else if (blockAddr==0x078) {
                        FabWrVldWords();

                    } else if (blockAddr==0x07C) {
                        FabWrVldWordsObs();

                    } else if (blockAddr==0x080) {
                        FabRdVldWords();

                    } else if (blockAddr==0x084) {
                        FabRdVldWordsObs();

                    } else if (blockAddr==0x088) {
                        ThresholdStickyClear();

                    } else if (blockAddr==0x08C) {
                        ThresholdClrMode();

                    } else if ( (blockAddr >= 0x090) && (blockAddr <= 0x0AC) ) {
                        ThresholdSettings(ThresholdIndex(blockAddr));

                    } else if (blockAddr==0x0B0) {
                        DecoderMode();

                    } else if (blockAddr==0x0B4) {
                        ResetNCOPhase();

                    } else if (blockAddr==0x0B8) {
                        OutputCurr();

                    } else if (blockAddr==0x0BC) {
                        NyquistZone();

                    } else if (blockAddr==0x0C0) {
                        InvSincFIR();

                    } else if (blockAddr==0x0C4) {
                        CalibrationMode();

                    } else if (blockAddr==0x0C8) {
                        DisableCoefficientsOverride();

                    } else if ( (blockAddr >= 0x0D0) && (blockAddr <= 0x0EC) ) {
                        CalCoefficients(0, CalCoefficientsIndex(blockAddr, 0x0D0));

                    } else if ( (blockAddr >= 0x0F0) && (blockAddr <= 0x10C) ) {
                        CalCoefficients(1, CalCoefficientsIndex(blockAddr, 0x0F0));

                    } else if ( (blockAddr >= 0x110) && (blockAddr <= 0x12C) ) {
                        CalCoefficients(2, CalCoefficientsIndex(blockAddr, 0x110));

                    } else if ( (blockAddr >= 0x130) && (blockAddr <= 0x14C) ) {
                        CalCoefficients(3, CalCoefficientsIndex(blockAddr, 0x130));

                    } else if ( (blockAddr >= 0x150) && (blockAddr <= 0x158) ) {
                        CalFreeze((blockAddr>>2)&0x3);

                    } else if (blockAddr==0x15C) {
                        Dither();

                    } else if (blockAddr==0x160) {
                        DataScaler();

                    } else if (blockAddr==0x164) {
                        LinkCoupling();

                    } else if ( (blockAddr >= 0x168) && (blockAddr <= 0x16C) ) {
                        DSA((blockAddr>>2)&0x1);

                    } else if (blockAddr==0x170) {
                        DACVOP();

                    } else if (blockAddr==0x174) {
                        DACCompMode();

                    } else if (blockAddr==0x178) {
                        DataPathMode();

                    } else if (blockAddr==0x17C) {
                        IMRPassMode();

                    } else if ( (blockAddr >= 0x180) && (blockAddr <= 0x19C) ) {
                        SignalDetector(SignalDetectorIndex(blockAddr));

                    } else if (blockAddr==0x1A0) {
                        ResetInternalFIFOWidth();

                    } else if (blockAddr==0x1A4) {
                        ResetInternalFIFOWidthObs();

                    } else if ( (blockAddr >= 0x1A8) && (blockAddr <= 0x1AC) ) {
                        PwrModeSettings((blockAddr>>2)&0x1);

                    } else if (blockAddr==0x1B0) {
                        BlockBaseAddr();

                    } else if (blockAddr==0x1B4) {
                        DataType();

                    } else if (blockAddr==0x1B8) {
                        DataWidth();

                    } else if (blockAddr==0x1BC) {
                        InverseSincFilter();

                    } else if (blockAddr==0x1C0) {
                        MixedMode();

                    } else if (blockAddr==0x1C4) {
                        IsFifoEnabled();

                    } else if (blockAddr==0x1C8) {
                        ConnectedIData();

                    } else if (blockAddr==0x1CC) {
                        ConnectedQData();

                    } else if (blockAddr==0x1D0) {
                        IsADCDigitalPathEnabled();

                    } else if (blockAddr==0x1D4) {
                        IsDACDigitalPathEnabled();

                    } else if (blockAddr==0x1D8) {
                        CheckDigitalPathEnabled();

                    } else if (blockAddr==0x1DC) {
                        IntrEnable();

                    } else if (blockAddr==0x1E0) {
                        IntrDisable();

                    } else if (blockAddr==0x1E4) {
                        IntrClr();

                    } else if (blockAddr==0x1E8) {
                        GetIntrStatus();

                    } else if (blockAddr==0x1EC) {
                        GetEnabledInterrupts();

                    } else {
                        errMsg_ = "Undefined memory";
                    }
                }
            } else {
                errMsg_ = "Undefined memory";
            }

            if (rdTxn_) {
                // Copy from data to (ptr + wrdIdx)
                memcpy(ptr+wrdIdx, &data_, sizeof(uint32_t));
            }

            // Increment/decrement the counters
            size   -= sizeof(uint32_t);
            addr   += sizeof(uint32_t);
            wrdIdx += sizeof(uint32_t);

        } // while (size > 0)

        // copy while still holding mtx_, then swallow once per transaction on
        // the local copy (never on a refusal)
        err = errMsg_;
        refused = txnRefused_;
        if (ignoreMetalError_ && !txnRefused_) {
            err.clear();
        }
    } // rim::TransactionLockPtr tlock = tran->lock();

    // Complete transaction without error
    if (err.empty()) {
        tran->done();

    // Complete transaction with error message
    } else {
        // Refusals are logged at debug level so a polling GUI cannot flood the journal
        if (refused) {
            log_->debug("%s", err.c_str());
        } else {
            log_->error("%s", err.c_str());
        }
        tran->errorStr(err);
    }

}

void PyRFdc::setup_python() {
#ifndef NO_PYTHON
    bp::class_<PyRFdc, PyRFdcPtr, bp::bases<rim::Slave>, boost::noncopyable>(
        "PyRFdc",
        bp::init<std::string>());
    bp::implicitly_convertible<PyRFdcPtr, rim::SlavePtr>();
#endif
}

#ifndef NO_PYTHON
BOOST_PYTHON_MODULE(PyRFdc) {
    PyRFdc::setup_python();
}
#endif
