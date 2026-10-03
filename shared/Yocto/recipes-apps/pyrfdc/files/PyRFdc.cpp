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
 * TODO: Add support for the following in the future....
 * https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetClkDistribution-Gen-3/DFE
 * https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClkDistribution-Gen-3/DFE
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
    CfgDriverBringUpFailed = 8,
    CfgBadHash            = 9,
};

// ----------------------------------------------------------------------------
// D-11, D-71, D-72, F5, Pattern 2: libmetal log ring. File-static because the
// libmetal log handler is a plain C function pointer with no `this`. Keeps the
// most recent 16 lines, sequence-numbered so a failing restart command can
// select only the lines logged since it started (DIAG-02 concurrency).
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

    // Chain to the default handler so journal output is unchanged (D-71, D-72)
    metal_default_log_handler(level, "%s\n", t.c_str());
}

}  // namespace

//! Create a block, class creator
PyRFdcPtr PyRFdc::create(const std::string& cfg) {
    PyRFdcPtr b = std::make_shared<PyRFdc>(cfg);
    return (b);
}

//! Create an block
PyRFdc::PyRFdc(const std::string& cfg) : rim::Slave(4,0x1000) { // Set min=4B and max=4kB
    // Init local variables (moved to the top so a not-initialized object still
    // serves the allow-listed registers deterministically)
    errMsg_.clear();
    scratchPad_ = 0;
    doubleTestReg_ = 0.0;
    metalLogLevel_ = false;
    ignoreMetalError_ = false;

    rdTxn_ = false;
    isADC_ = false;
    tileId_ = 0;
    tileType_ = 0;
    blockId_ = 0;
    data_ = 0;

    // Per-tile restart records (DIAG-03, DIAG-04): no record yet, no failure yet
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            resetRecord_[i][j] = 0;
            resetSeq_[i][j] = 0;
            stateAtFailure_[i][j] = 0xFF;
            commonAtFailure_[i][j] = 0xFF;
            clkDetAtFailure_[i][j] = 0xFF;
        }
    }

    log_ = rogue::Logging::create("PyRFdc");

    // RFDC config ROM status: not loaded until the ladder below decides otherwise
    cfgStatus_ = CfgNotLoaded;
    cfgMessage_ = "NotLoaded: PyRFdc has not decoded a config";
    cfgRomBytes_ = uint32_t(cfg.size());
    std::memset(cfgHeader_, 0, sizeof(cfgHeader_));
    std::memcpy(cfgHeader_, cfg.data(), std::min(cfg.size(), size_t(PYRFDC_ROM_HEADER_BYTES)));

#ifdef __BAREMETAL__
    // Ensure baremetal driver is ready
    if (XRFdc_LookupConfig(RFDC_DEVICE_ID) == NULL) {
        log_->error("PyRFdc: Baremetal RFdc Configuration Lookup Failed!");
        return;
    }
#endif

    // Initialize libmetal (should be after ensuring baremetal is ready)
    struct metal_init_params init_param = METAL_INIT_DEFAULTS;
    if (metal_init(&init_param)) {
        metal_finish();
        SetConfigStatus(CfgDriverBringUpFailed, "DriverBringUpFailed: metal_init failed; the RFDC driver is not initialized");
        log_->error("%s", cfgMessage_.c_str());
        return;
    }

    // D-11, D-71, Pitfall 8: install only AFTER a successful metal_init, which
    // overwrites the handler. g_metalLog is created once; a re-constructed
    // PyRFdc (never happens in this process, but kept defensive) reuses it.
    if (!g_metalLog) {
        g_metalLog = rogue::Logging::create("PyRFdc.metal");
    }
    metal_set_log_handler(metalLogHandler);

#ifndef __BAREMETAL__
    struct metal_device *deviceptr;
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) != XRFDC_SUCCESS) {
        metal_device_close(deviceptr);
        metal_finish();
        SetConfigStatus(CfgDriverBringUpFailed, "DriverBringUpFailed: XRFdc_RegisterMetal failed; the RFDC driver is not initialized");
        log_->error("%s", cfgMessage_.c_str());
        return;
    }
#endif

    // ------------------------------------------------------------------
    // Validate the PYRFDC_CONFIG ROM bytes handed in by the launcher.
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

    } else if (cfg.size() < size_t(PYRFDC_ROM_HEADER_BYTES) + cfgHeader_[2]) {
        std::snprintf(msgBuf, sizeof(msgBuf),
            "BadSize: ROM read returned %zu bytes, header plus payload need %u",
            cfg.size(), PYRFDC_ROM_HEADER_BYTES + cfgHeader_[2]);
        SetConfigStatus(CfgBadSize, msgBuf);

    } else {
        // Header is self-consistent; check the payload hash (T-01-09 tamper
        // mitigation) before trusting the payload for the live sanity checks.
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(cfg.data() + PYRFDC_ROM_HEADER_BYTES),
               sizeof(XRFdc_Config), digest);

        if (std::memcmp(digest, &cfgHeader_[4], 16) != 0) {
            SetConfigStatus(CfgBadHash,
                "BadHash: ROM payload sha256 does not match header words 4 to 7; the ROM may be "
                "corrupt or tampered, rebuild the firmware with a matching pyrfdc_mem.py");

        } else {
            // Live sanity checks: need only the io region set up by XRFdc_RegisterMetal
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

                } else if (XRFdc_CfgInitialize(RFdcInstPtr_, &romCfg) != XRFDC_SUCCESS) {
                    SetConfigStatus(CfgDriverBringUpFailed,
                        "DriverBringUpFailed: XRFdc_CfgInitialize failed on a valid ROM config");

                } else {
                    std::snprintf(msgBuf, sizeof(msgBuf),
                        "Ok: XRFdc_Config loaded from the PYRFDC_CONFIG ROM (format 1, %zu bytes, IP 0x%08X)",
                        sizeof(XRFdc_Config), cfgHeader_[3]);
                    SetConfigStatus(CfgOk, msgBuf);
                }
            }
        }
    }

    if (cfgStatus_ != CfgOk) {
        log_->error("%s", cfgMessage_.c_str());
        return;
    }
    log_->info("%s", cfgMessage_.c_str());

    log_->debug("PyRFdc::PyRFdc() Initialization Complete");

    // DT-02 (D-34): the MaxSampleRate override is gone now that the ROM
    // delivers a valid XRFdc_Config; RFdcInst_ is value-initialized (PyRFdc.h)
    // so UpdateMixerScale is 0 on a fresh instance, not indeterminate (F8).
    int i, j, k;

    // Loop through type indexes
    for(i=0; i<2; i++) {
        // Loop through tile indexes
        for(j=0; j<4; j++) {

            // Initialize clock source
            clkSrcDefault_[i][j] = XRFDC_EXTERNAL_CLK;
            clkSrcConfig_[i][j] = clkSrcDefault_[i][j];

            // Initialize PLL_Settings data structure
            pllDefault_[i][j].Enabled = 0;
            pllDefault_[i][j].RefClkFreq = 0.0;
            pllDefault_[i][j].SampleRate = 0.0;
            pllDefault_[i][j].RefClkDivider = 1;
            pllDefault_[i][j].FeedbackDivider = 1;
            pllDefault_[i][j].OutputDivider = 1;
            pllDefault_[i][j].FractionalMode = 1;
            pllDefault_[i][j].FractionalData = 1;
            pllDefault_[i][j].FractWidth = 1;
            pllConfig_[i][j] = pllDefault_[i][j];

            // Loop through block indexes
            for(k=0; k<4; k++) {

                // Initialize QMC_Settings data structure
                qmcDefault_[i][j][k].EnablePhase = 0;
                qmcDefault_[i][j][k].EnableGain = 0;
                qmcDefault_[i][j][k].GainCorrectionFactor = 0.0;
                qmcDefault_[i][j][k].PhaseCorrectionFactor = 0.0;
                qmcDefault_[i][j][k].OffsetCorrectionFactor = 0;
                qmcDefault_[i][j][k].EventSource = XRFDC_EVNT_SRC_TILE;
                qmcConfig_[i][j][k] = qmcDefault_[i][j][k];

                // Initialize Mixer_Settings data structure
                mixerDefault_[i][j][k].Freq = 0.0;
                mixerDefault_[i][j][k].PhaseOffset = 0.0;
                mixerDefault_[i][j][k].EventSource = XRFDC_EVNT_SRC_TILE;
                mixerDefault_[i][j][k].CoarseMixFreq = XRFDC_COARSE_MIX_OFF;
                mixerDefault_[i][j][k].MixerMode = XRFDC_MIXER_MODE_OFF;
                mixerDefault_[i][j][k].FineMixerScale = XRFDC_MIXER_SCALE_0P7;
                mixerDefault_[i][j][k].MixerType = XRFDC_MIXER_TYPE_OFF;
                mixerConfig_[i][j][k] = mixerDefault_[i][j][k];
            }
        }
    }

    // Loop through type indexes
    for(i=0; i<2; i++) {

        // Init the MTS configurations
        XRFdc_MultiConverter_Init(&mtsConfig_[i], 0, 0, XRFDC_TILE_ID0);
        mtsConfig_[i].Tiles = 0;

        // Loop through tile indexes
        for(j=0; j<4; j++) {

            // Init the MTS factor status
            mtsfactor_[i][j] = 0;

            // Check if tile is enabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_FAILURE) {

                // Get the default Clock source
                if (XRFdc_GetClockSource(RFdcInstPtr_, i, j, &clkSrcDefault_[i][j]) != XRFDC_FAILURE) {
                    clkSrcConfig_[i][j] = clkSrcDefault_[i][j];
                }

                // Get the default PLL configuration
                if (XRFdc_GetPLLConfig(RFdcInstPtr_, i, j, &pllDefault_[i][j]) != XRFDC_FAILURE) {
                    pllDefault_[i][j].SampleRate = 1000.0*pllDefault_[i][j].SampleRate; // Convert from GSPS to MSPS
                    pllConfig_[i][j] = pllDefault_[i][j];
                    // Set the default PLL configuration (required for intializing the mixer's Sampling rate when doing XRFdc_GetMixerSettings)
                    XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
                }

                // Loop through block indexes
                for(k=0; k<4; k++) {

                    // Check if block enabled
                    if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {

                        if (XRFdc_GetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]) != XRFDC_FAILURE) {
                            qmcConfig_[i][j][k] = qmcDefault_[i][j][k];
                        }

                        // Get the default Mixer configuration (DT-02, D-34: the old
                        // UpdateMixerScale guard is gone now that RFdcInst_ is
                        // value-initialized and never indeterminate, F8)
                        if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                            // Check for ADC tile or DAC DUC not bypassed
                            if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                                // Get the mixer setting
                                if ( XRFdc_GetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]) != XRFDC_FAILURE) {
                                    mixerConfig_[i][j][k] = mixerDefault_[i][j][k];
                                }
                            }
                        }

                    }
                }
            }
        }
    }

    log_->debug("PyRFdc::PyRFdc()");
}

//! Destroy a block
PyRFdc::~PyRFdc() {
    // Log the destruction of the class
    log_->debug("PyRFdc::~PyRFdc() called");

#ifndef __BAREMETAL__
    struct metal_device *deviceptr = nullptr;
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) == XRFDC_SUCCESS && deviceptr) {
        metal_device_close(deviceptr);  // Close metal device if applicable
    }
    metal_finish(); // Cleanup metal library
#endif

    log_->debug("PyRFdc::~PyRFdc() completed");
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

//! Serve the config status block at 0x14000 to 0x141FC
void PyRFdc::ConfigStatusReg(uint32_t addr) {
    if (!rdTxn_) {
        errMsg_ = "ConfigStatus(): read-only\n";
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

//! Serve the tile-only restart record registers at 0x814 to 0x824 (DIAG-03, DIAG-04)
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

//! DIAG-04, D-12: record a restart command's outcome for the current tileType_/tileId_.
//! On failure, snapshot CurrentState, Common Status, and the clock detector BEFORE
//! IgnoreMetalError can swallow the error (F14, D-10, D-22).
void PyRFdc::RecordRestart(uint32_t type, uint32_t tile, uint32_t op, bool ok) {
    uint16_t seq = uint16_t(resetSeq_[type][tile] + 1);
    resetSeq_[type][tile] = seq;
    resetRecord_[type][tile] = (uint32_t(seq) << 16) | ((op & 0xFU) << 8) | (ok ? 1U : 2U);

    if (!ok) {
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

//! DIAG-01 (D-13, D-21, F6, Pattern 3): one key=value diagnostic line for a failing
//! tile. Plain XRFdc_ReadReg reads plus XRFdc_GetClockSource; never blocks or asserts.
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

//! DIAG-01: one DiagLine per failing tile (tile order) plus the metal lines
//! captured since seq0, newline separated, no trailing empty line, capped at
//! 440 characters with the key=value lines first (F6).
std::string PyRFdc::RestartError(const char* op, const char* call, uint32_t type,
                                  const std::vector<uint32_t>& tiles, uint64_t seq0) {
    std::string out;
    for (size_t i = 0; i < tiles.size(); i++) {
        if (i > 0) {
            out += "\n";
        }
        out += DiagLine(op, type, tiles[i], call);
    }

    std::vector<std::string> metal = metalRingSince(seq0);
    for (const auto& line : metal) {
        out += "\n";
        out += line;
    }

    if (out.size() > 440) {
        out.resize(440);
    }
    return out;
}

//! Tile_Id -1 helper (StartUp, CustomStartUp): the driver loops internally over
//! every enabled tile and returns one status for the whole call, so the
//! failing tiles are inferred from live registers: an enabled tile whose
//! Restart bit (0x04) is still set, or whose CurrentState differs from the
//! requested end state, is failing. If none qualify (unexpected), every
//! enabled tile is reported failed rather than silently naming none.
static std::vector<uint32_t> FailingTilesAllAdcDac(XRFdc* inst, uint32_t type, uint32_t endState) {
    std::vector<uint32_t> enabled;
    std::vector<uint32_t> failed;
    for (uint32_t t = 0; t < 4; t++) {
        if (XRFdc_CheckTileEnabled(inst, type, t) != XRFDC_FAILURE) {
            enabled.push_back(t);
            uint32_t base = XRFDC_CTRL_STS_BASE(type, t);
            uint32_t restart = XRFdc_ReadReg(inst, base, 0x0004) & 0x1U;
            uint32_t cur = XRFdc_ReadReg(inst, base, 0x000C) & 0xFFU;
            if (restart != 0 || cur != endState) {
                failed.push_back(t);
            }
        }
    }
    if (failed.empty()) {
        return enabled;
    }
    return failed;
}

void PyRFdc::StartUp(int Tile_Id) {
    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    // Else write
    uint64_t seq0 = metalRingSeqNow();
    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
    int status = XRFdc_StartUp(RFdcInstPtr_, tileType_, Tile_Id);

    if (Tile_Id >= 0) {
        RecordRestart(tileType_, uint32_t(Tile_Id), 2, status == XRFDC_SUCCESS);
        if (status != XRFDC_SUCCESS) {
            std::vector<uint32_t> tiles = {uint32_t(Tile_Id)};
            errMsg_ = RestartError("StartUp", "XRFdc_StartUp", tileType_, tiles, seq0);
        }
        return;
    }

    // Tile_Id -1: the driver loops internally; record every enabled tile
    for (uint32_t t = 0; t < 4; t++) {
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, t) != XRFDC_FAILURE) {
            RecordRestart(tileType_, t, 2, status == XRFDC_SUCCESS);
        }
    }
    if (status != XRFDC_SUCCESS) {
        std::vector<uint32_t> tiles = FailingTilesAllAdcDac(RFdcInstPtr_, tileType_, 15);
        errMsg_ = RestartError("StartUp", "XRFdc_StartUp", tileType_, tiles, seq0);
    }
}

void PyRFdc::Shutdown(int Tile_Id) {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        status = XRFdc_Shutdown(RFdcInstPtr_, tileType_, Tile_Id);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "Shutdown(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::Reset(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    int i, j, k;

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd
        return;
    }

    uint64_t seq0 = metalRingSeqNow();

    // Check for global TYPE reset
    if (Tile_Id<0) {
        // Init the i variable
        i = tileType_;

        // Init the MTS configurations
        XRFdc_MultiConverter_Init(&mtsConfig_[i], 0, 0, XRFDC_TILE_ID0);
        mtsConfig_[i].Tiles = 0;

        // Loop through tile indexes
        for(j=0; j<4; j++) {

            // Init the MTS factor status
            mtsfactor_[i][j] = 0;

            // Check if tile is enabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_FAILURE) {

                // Reset all the Tiles that have their PLL's enabled
                if (pllDefault_[i][j].Enabled > 0) {
                    int firstPassStatus = XRFdc_Reset(RFdcInstPtr_, i, j);
                    if (firstPassStatus != XRFDC_SUCCESS) {
                        // D-13, Pattern 3: first-pass failures are a warning,
                        // not the per-tile record (the second pass below owns
                        // the record for this tile, keeping today's control flow)
                        log_->warning("%s", DiagLine("Reset(first pass)", i, uint32_t(j), "XRFdc_Reset").c_str());
                    }
                }

                // Restore default configuration
                XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
                clkSrcConfig_[i][j] = clkSrcDefault_[i][j];
                pllConfig_[i][j] = pllDefault_[i][j];

                // Loop through block indexes
                for(k=0; k<4; k++) {

                    // Check if block enabled
                    if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {

                        if (XRFdc_SetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]) != XRFDC_FAILURE) {
                            XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_QMC);
                        }
                        qmcConfig_[i][j][k] = qmcDefault_[i][j][k];

                        // Get the default Mixer configuration
                        if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                            // Check for ADC tile or DAC DUC not bypassed
                            if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                                if (XRFdc_SetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]) != XRFDC_FAILURE) {
                                    XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_MIXER);
                                }
                            }
                        }
                        mixerConfig_[i][j][k] = mixerDefault_[i][j][k];

                    }
                }
            }
        }

        // Loop through tile indexes
        std::vector<uint32_t> failedTiles;
        for(j=0; j<4; j++) {

            // Check if tile is enabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_FAILURE) {

                // Execute reset again after restoring the settings
                int tileStatus = XRFdc_Reset(RFdcInstPtr_, i, j);
                RecordRestart(uint32_t(i), uint32_t(j), 1, tileStatus == XRFDC_SUCCESS);
                if (tileStatus != XRFDC_SUCCESS) {
                    failedTiles.push_back(uint32_t(j));
                }
            }

        }

        // Report failure when any enabled tile's final XRFdc_Reset failed,
        // naming every failing tile (previously only the last tile's status counted)
        if (!failedTiles.empty()) {
            errMsg_ = RestartError("Reset", "XRFdc_Reset", uint32_t(i), failedTiles, seq0);
        }
        return;

    // Else not a global reset
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        status = XRFdc_Reset(RFdcInstPtr_, tileType_, Tile_Id);
        RecordRestart(tileType_, uint32_t(Tile_Id), 1, status == XRFDC_SUCCESS);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        std::vector<uint32_t> tiles = {uint32_t(Tile_Id)};
        errMsg_ = RestartError("Reset", "XRFdc_Reset", tileType_, tiles, seq0);
    }
}

void PyRFdc::CustomStartUp(int Tile_Id) {
    uint32_t StartState = (data_>>0)&0xF;
    uint32_t EndState   = (data_>>8)&0xF;

    // Check if read
    if (rdTxn_) {
        errMsg_ = "CustomStartUp(" + std::to_string(Tile_Id) + "): failed\n";
        return;
    }

    // Else write
    uint64_t seq0 = metalRingSeqNow();
    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
    int status = XRFdc_CustomStartUp(RFdcInstPtr_, tileType_, Tile_Id, StartState, EndState);

    if (Tile_Id >= 0) {
        RecordRestart(tileType_, uint32_t(Tile_Id), 3, status == XRFDC_SUCCESS);
        if (status != XRFDC_SUCCESS) {
            std::vector<uint32_t> tiles = {uint32_t(Tile_Id)};
            errMsg_ = RestartError("CustomStartUp", "XRFdc_CustomStartUp", tileType_, tiles, seq0);
        }
        return;
    }

    // Tile_Id -1: the driver loops internally; record every enabled tile
    for (uint32_t t = 0; t < 4; t++) {
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, t) != XRFDC_FAILURE) {
            RecordRestart(tileType_, t, 3, status == XRFDC_SUCCESS);
        }
    }
    if (status != XRFDC_SUCCESS) {
        std::vector<uint32_t> tiles = FailingTilesAllAdcDac(RFdcInstPtr_, tileType_, EndState);
        errMsg_ = RestartError("CustomStartUp", "XRFdc_CustomStartUp", tileType_, tiles, seq0);
    }
}

void PyRFdc::GetIPStatus() {
    int status = XRFDC_SUCCESS;
    XRFdc_IPStatus IPStatusPtr;
    XRFdc_TileStatus TileStatus;

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
    XRFdc_BlockStatus BlockStatus;

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
                    if (status != XRFDC_FAILURE) {
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
                if (status != XRFDC_FAILURE) {
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
    XRFdc_CoarseDelay_Settings settings;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoarseDelaySettings
    status = XRFdc_GetCoarseDelaySettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Check if write
    if (!rdTxn_) {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_CoarseDelay_Settings
        settings.CoarseDelay = ( (data_>>0) &0xFF);
        settings.EventSource = ( (data_>>8) &0xFF);

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCoarseDelaySettings
        status = XRFdc_SetCoarseDelaySettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

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
    XRFdc_Threshold_Settings settings;

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

            // Update the thresholds
            settings.UpdateThreshold = XRFDC_UPDATE_THRESHOLD_BOTH;

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdSettings
            status = XRFdc_SetThresholdSettings(RFdcInstPtr_, tileId_, blockId_, &settings);

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
    uint8_t settings;

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
    uint8_t settings;

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
    XRFdc_Calibration_Coefficients settings;

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
            status = XRFdc_SetCalCoefficients(RFdcInstPtr_, tileId_, blockId_, calType, &settings);

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
    XRFdc_Cal_Freeze_Settings settings;

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
            status = XRFdc_SetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

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
    XRFdc_PLL_Settings settings;

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
    XRFdc_DSA_Settings settings;

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
            status = XRFdc_SetDSA(RFdcInstPtr_, tileId_, blockId_, &settings);

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
    XRFdc_Signal_Detector_Settings settings;

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
            status = XRFdc_SetSignalDetector(RFdcInstPtr_, tileId_, blockId_, &settings);

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
    XRFdc_Pwr_Mode_Settings settings;

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
        status = XRFdc_SetPwrMode(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

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
    double settings;

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
    double settings;

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
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_DynamicPLLConfig
                status = XRFdc_DynamicPLLConfig(RFdcInstPtr_, tileType_, tileId_, uint8_t(clkSrcConfig_[tileType_][tileId_]), pllConfig_[tileType_][tileId_].RefClkFreq, pllConfig_[tileType_][tileId_].SampleRate);
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
        XRFdc_IntrEnable(RFdcInstPtr_, tileType_, tileId_, blockId_, enableMask);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IntrEnable(): failed\n";
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
        XRFdc_IntrDisable(RFdcInstPtr_, tileType_, tileId_, blockId_, disableMask);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "IntrDisable(): failed\n";
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
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, i) != XRFDC_FAILURE) {
                // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMTSEnable
                XRFdc_GetMTSEnable(RFdcInstPtr_, tileType_, i, &EnablePtr);
                settings |= ((EnablePtr&0x1)<<i);
            }
        }
        // Return the MTS enabled bit mask
        data_ = settings;
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
        XRFdc_MTS_Sysref_Config(RFdcInstPtr_,  &mtsConfig_[XRFDC_DAC_TILE],  &mtsConfig_[XRFDC_ADC_TILE], (data_&0x1));
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

        // Reset status values
        for(i=0; i<4; i++) {
            mtsfactor_[tileType_][i] = 0;
            mtsConfig_[tileType_].Latency[i] = 0;
            mtsConfig_[tileType_].Offset[i] = 0;
        }

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Sync
        status = XRFdc_MultiConverter_Sync(RFdcInstPtr_, tileType_, &mtsConfig_[tileType_]);
        if (status == XRFDC_MTS_OK) {
            for(i=0; i<4; i++) {
                if((1<<i)&mtsConfig_[tileType_].Tiles) {
                    if (tileType_ == XRFDC_ADC_TILE) {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
                        XRFdc_GetDecimationFactor(RFdcInstPtr_, i, 0, &mtsfactor_[tileType_][i]);
                    } else {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
                        XRFdc_GetInterpolationFactor(RFdcInstPtr_, i, 0, &mtsfactor_[tileType_][i]);
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
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-Power-On-State-Machine-Register-0x0004
        XRFdc_WriteReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(tileType_, tileId_), XRFDC_RESTART_OFFSET, XRFDC_RESTART_MASK);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "RestartSM(): failed\n";
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
    std::string err;  // DEF-05 (D-10): consumed only after mtx_ is released

    rim::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

        // DEF-05 (D-10): clear and read errMsg_ only while mtx_ is held
        errMsg_.clear();

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
                refused = true;
                if (rdTxn_) {
                    data_ = 0;
                }

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
                        ThresholdSettings((blockAddr>>2)&0x7);

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
                        CalCoefficients(0, (blockAddr>>2)&0x7);

                    } else if ( (blockAddr >= 0x0F0) && (blockAddr <= 0x10C) ) {
                        CalCoefficients(1, (blockAddr>>2)&0x7);

                    } else if ( (blockAddr >= 0x110) && (blockAddr <= 0x12C) ) {
                        CalCoefficients(2, (blockAddr>>2)&0x7);

                    } else if ( (blockAddr >= 0x130) && (blockAddr <= 0x14C) ) {
                        CalCoefficients(3, (blockAddr>>2)&0x7);

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
                        SignalDetector((blockAddr>>2)&0x3);

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

        // DEF-05 (D-10): copy while still holding mtx_, then swallow once per
        // transaction on the local copy (never on a refusal, DIAG-04)
        err = errMsg_;
        if (ignoreMetalError_ && !refused) {
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
