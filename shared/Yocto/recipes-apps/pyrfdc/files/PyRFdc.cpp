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
 *
 * The getter half is no longer outstanding. The constructor queries the
 * clock distribution topology once and caches it, and the call site carries
 * the documentation link for it.
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
#include <cstring>
#include <string>

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

// doTransaction assigns tileType_ from these two constants, and the [2][4]
// and [2][4][4] shadow arrays in PyRFdc.h are indexed by that value, so any
// other pair silently swaps the ADC and DAC groups rather than failing.
static_assert(XRFDC_ADC_TILE == 0 && XRFDC_DAC_TILE == 1,
              "PyRFdc.h indexes its [2][4] shadow arrays by tile type: "
              "XRFDC_ADC_TILE must be 0 and XRFDC_DAC_TILE must be 1");

// The constructor gates its clock distribution query on
// RFdc_Config.IPType >= XRFDC_GEN3, so a header defining this differently
// silently flips which boards take the query path rather than failing: a
// lower value asks boards the driver will refuse and print an error for, a
// higher one stops asking boards that can answer.
//
// Like the tile-type assertion above, this one compiles in the Yocto build
// as well as in the host harness, so it pins the real xrfdc.h and not only
// the shim.
static_assert(XRFDC_GEN3 == 2,
              "PyRFdc.cpp gates the clock distribution query on "
              "RFdc_Config.IPType >= XRFDC_GEN3: XRFDC_GEN3 must be 2");

// Names of the sixteen states of the tile IPSM, indexed by the low four bits
// of the current state register at offset 0x000C.
// https://docs.amd.com/r/en-US/pg269-rf-data-converter/Current-State-Register-0x000C
//
// These strings are a second copy. The first lives in enumState in
// python/axi_soc_ultra_plus_core/rfsoc_utility/__init__.py, which is what
// every host-side CurrentState RemoteVariable is decoded against, so an edit
// on one side and not the other makes a host comparison against that table
// miss. The raw value is printed alongside the name below for exactly that
// reason: the number survives any drift between the two copies.
static const char* const IPSM_STATE_NAMES[16] = {
    "Device_Power-up_and_Configuration[0]",
    "Device_Power-up_and_Configuration[1]",
    "Device_Power-up_and_Configuration[2]",
    "Power_Supply_Adjustment[0]",
    "Power_Supply_Adjustment[1]",
    "Power_Supply_Adjustment[2]",
    "Clock_Configuration[0]",
    "Clock_Configuration[1]",
    "Clock_Configuration[2]",
    "Clock_Configuration[3]",
    "Clock_Configuration[4]",
    "Converter_Calibration[0]",
    "Converter_Calibration[1]",
    "Converter_Calibration[2]",
    "Wait_for_deassertion_of_AXI4-Stream_reset",
    "Done",
};

// A 0x prefix and as many hex digits as the value needs, built without
// <iomanip> or snprintf so the result is the same under the Yocto build and
// a host build and contains nothing but ASCII. Leading zeros are dropped
// because eight of these appear per tile and the whole line has a hard
// length budget, see DIAG_MSG_BUDGET below.
static std::string HexValue(uint32_t value) {
    static const char* const digits = "0123456789ABCDEF";
    char buf[8];
    int n = 0;

    do {
        buf[n++] = digits[value & 0xF];
        value >>= 4;
    } while (value != 0);

    std::string out = "0x";
    while (n > 0) {
        out += buf[--n];
    }
    return out;
}

// Longest report this code will build, in characters, excluding the trailing
// newline.
//
// The completion epilogue hands the same string to two places: the caller,
// through Transaction::errorStr, where a std::string of any length survives;
// and the console, through Logging::error, where it does not. Measured in
// rogue v6.15.0, rogue::Logging::intLog formats into a stack buffer with
// vsnprintf and a size argument of 1000, so anything past 999 characters
// never reaches the PS UART at all. Eight tile records at full width came to
// 1105, which would have dropped the last two tiles off the console silently
// while the exception text kept them.
//
// Held below that with room for the omission marker, so the code can say it
// ran out of line rather than being cut off mid word by something it cannot
// see.
static const size_t DIAG_MSG_BUDGET = 960;

// The one place a constructor outcome is turned into words.
//
// Kept as a single mapping rather than a string at each call site, so the
// wording of a step lives in one place and the rejection cannot describe the
// same outcome two ways. The two lookup steps are spelled apart on purpose:
// both call XRFdc_LookupConfig, and a reader who is told only the function
// name cannot tell the baremetal readiness check from the configuration
// fetch that follows libmetal.
static const char* InitFailStepName(uint32_t reason) {
    switch (reason) {
        case PYRFDC_INIT_OK:
            return "construction completed";
        case PYRFDC_INIT_FAIL_NOT_COMPLETED:
            return "construction did not complete";
        case PYRFDC_INIT_FAIL_BAREMETAL_LOOKUP:
            return "construction failed at XRFdc_LookupConfig in the baremetal readiness check";
        case PYRFDC_INIT_FAIL_METAL_INIT:
            return "construction failed at metal_init";
        case PYRFDC_INIT_FAIL_CONFIG_LOOKUP:
            return "construction failed at XRFdc_LookupConfig for the driver configuration";
        case PYRFDC_INIT_FAIL_REGISTER_METAL:
            return "construction failed at XRFdc_RegisterMetal";
        default:
            return "construction failed at an unknown step";
    }
}

//! Create a block, class creator
PyRFdcPtr PyRFdc::create() {
    PyRFdcPtr b = std::make_shared<PyRFdc>();
    return (b);
}

//! Create an block
PyRFdc::PyRFdc() : rim::Slave(4,0x1000) { // Set min=4B and max=4kB
    int i, j, k;
    log_ = rogue::Logging::create("PyRFdc");

    // Every early return below leaves the object constructed, so the
    // per-tile records are put in a defined state before the first of them.
    clearTileDiag();

#ifdef __BAREMETAL__
    // Ensure baremetal driver is ready
    if (XRFdc_LookupConfig(RFDC_DEVICE_ID) == NULL) {
        initFailReason_ = PYRFDC_INIT_FAIL_BAREMETAL_LOOKUP;
        log_->error("PyRFdc: Baremetal RFdc Configuration Lookup Failed!");
        return;
    }
#endif

    // Initialize libmetal (should be after ensuring baremetal is ready)
    struct metal_init_params init_param = METAL_INIT_DEFAULTS;
    if (metal_init(&init_param)) {
        initFailReason_ = PYRFDC_INIT_FAIL_METAL_INIT;
        log_->error("PyRFdc: Failed to initialize libmetal");
        // The clear is redundant on this path, because the bring-up did not
        // succeed and the flag was therefore never set. It is written anyway
        // so that every release in this function is paired with a clear as a
        // rule, rather than the pairing having to be judged case by case at
        // each site.
        metalReady_ = false;
        metal_finish();
        return;
    }

    // The library is up from here, so the release below is owned by this
    // function until one of the bail-outs performs it, and by the destructor
    // if none of them does.
    metalReady_ = true;

    // Initialize RFdc Configuration
    XRFdc_Config *ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
    if (ConfigPtr == NULL) {
        initFailReason_ = PYRFDC_INIT_FAIL_CONFIG_LOOKUP;
        log_->error("PyRFdc: RFdc Config Failure");
        metalReady_ = false;
        metal_finish();
        return;
    }

#ifndef __BAREMETAL__
    struct metal_device *deviceptr = nullptr;
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) != XRFDC_SUCCESS) {
        initFailReason_ = PYRFDC_INIT_FAIL_REGISTER_METAL;
        log_->error("PyRFdc: XRFdc_RegisterMetal() Failure");
        // A registration that did not succeed opened nothing to close. It is
        // the only writer of deviceptr and it writes it through the out
        // parameter on success alone, so only what the call actually handed
        // back is closed here. The declaration above carries the initializer
        // for the same reason the destructor's does.
        if (deviceptr != nullptr) {
            metal_device_close(deviceptr);
        }
        metalReady_ = false;
        metal_finish();
        return;
    }
#endif

    // Bind the status rather than discard it. This is the call that copies
    // the configuration into the driver instance and marks it ready, so a
    // non-success return here is exactly the case where every later
    // transaction would read through an instance that was never set up.
    // The argument list and the position in the sequence are unchanged. What
    // the status now also decides is whether the constructor continues.
    uint32_t cfgStatus = XRFdc_CfgInitialize(RFdcInstPtr_, ConfigPtr);

    if (cfgStatus != XRFDC_SUCCESS) {
        // No reason value is assigned here, deliberately. A configuration
        // initialize that did not succeed is a construction that neither
        // bailed out at a named step nor completed, and the declaration
        // default in PyRFdc.h already reports exactly that. Naming a fifth
        // step for this path would put that default back out of reach.
        //
        // Sited before the sample rate workaround loop below rather than
        // after it, because that loop writes two fields of the driver
        // instance per tile, and everything below it issues driver calls,
        // a PLL reconfigure and a raw register read among them, through an
        // instance the driver has just declined to configure.
        log_->error("PyRFdc: XRFdc_CfgInitialize() Failure");
        return;
    }

    log_->debug("PyRFdc::PyRFdc() Initialization Complete");

    // Work around for MaxSampleRate until I figure out how to properly
    //get the ConfigPtr (and/or devicetree) to set this configuration properly
    for(j=0; j<4; j++) {
        RFdcInstPtr_->RFdc_Config.ADCTile_Config[j].MaxSampleRate = 5.9;
        RFdcInstPtr_->RFdc_Config.DACTile_Config[j].MaxSampleRate = 10.0;
    }

    // The driver instance is usable from here and not before. This is the
    // only place the flag is raised, it is raised once, and it is raised
    // unconditionally: every failing step returns above rather than falling
    // through, so reaching this line means every step reported success and a
    // condition here could no longer be false.
    //
    // Every path that leaves this constructor without reaching this line,
    // the four early returns above, the configuration initialize bail-out
    // and any added later, leaves the object carrying the declaration
    // defaults in PyRFdc.h and is therefore dead.
    driverValid_ = true;
    initFailReason_ = PYRFDC_INIT_OK;

    // Init local variables
    //
    // The next four are the reset a completed construction performs, not the
    // initialization anything relies on: the declarations in PyRFdc.h carry
    // the same values and are what every construction path, including the
    // ones that return above this block, actually depends on.
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

    // The board's clock distribution topology, captured here for the same
    // reasons clkSrcDefault_ and pllDefault_ are captured below: it costs
    // nothing per reset, the topology is fixed by the IP so it cannot change
    // at runtime, and it keeps a query that can fail off the path that has
    // to keep working when the board is already degraded.
    //
    // The IP generation is captured unconditionally, and it decides which of
    // two sources answers. The order below is the rule, and the rule matters
    // more than either source does on its own.
    //
    // A driver reporting a generation from the third up to the highest this
    // file knows how to ask, PYRFDC_IPTYPE_MAX_KNOWN, is asked the documented
    // question and is asked nothing else. If that call returns non-success
    // the cache stays at the no-distribution values PyRFdc.h declares, and
    // this code deliberately does not fall through to the raw decode. A board
    // that answered the documented call and answered it with an error is
    // telling the driver something, and a raw register read cannot correct
    // that. Falling through would also mean one board could report two
    // different topologies depending on whether a transient failure happened
    // to land on that one call at construction.
    //
    // A driver reporting anything below the third generation refuses the
    // documented call at its first branch, without reading hardware, and
    // prints a console error every time it is asked. That refusal is a
    // property of the driver's own range check and not of anything here.
    // This file is consumed by every SLAC RFSoC project, so an unguarded call
    // would add a permanent error line at every bridge start on boards this
    // work is not trying to change. Such a driver is therefore never asked,
    // and gets the raw clock detect decode instead, which no IP generation
    // gate can block. That low arm is left exactly as wide as it was: it is
    // the branch the project's only successful topology reading was taken on,
    // and narrowing it to protect against an unmeasured input would trade a
    // hypothetical for a measured regression.
    //
    // The upper bound exists because the driver's own range check protects
    // only values the driver could have produced. IPType is copied out of a
    // device tree property the driver does not validate, and this project has
    // read 255 out of it on a real board, from a node whose parameter list
    // was zero bytes. An unbounded test admits that value and routes exactly
    // the degraded case to the one branch with no guard of its own in front
    // of it. Anything above the bound therefore consults no source at all and
    // records PYRFDC_CLKDIST_SRC_UNSUPPORTED_GEN, which is what keeps a
    // generation nobody can vouch for distinguishable at 0x12010 from a
    // driver that answered, from one that refused and from one that took the
    // raw decode.
    //
    // What that third arm deliberately does not do is fail construction or
    // raise. The fallback for an unavailable topology is the ungrouped per
    // type sweep, which is the guarantee every board without a distribution
    // already depends on, and a generation this file cannot interpret is one
    // more way for the topology to be unavailable rather than a new class of
    // error.
    //
    // What it does do is say so, and it is no longer alone in that. Of the
    // three ways out of this capture branch, two end with no topology: the
    // documented getter asked and refusing, and a reported generation this
    // file will not put the question to. Both lose the master before edge
    // ordering for every tile, which is not a routine outcome, and on both
    // the source field alone is visible only to a host that already knows
    // which offset to read, on a boot whose register path may itself have
    // degraded. The third way out is the one where a source answered, and it
    // prints nothing. So both of the first two emit one line through the log
    // channel, at the same level a default bridge admits, each naming its own
    // cause and not the other's, and never through the
    // diagnostic error path, for the reason the recovery and withdrawal
    // reports below give: a topology outcome is not a transaction failure and
    // construction must not start failing over one. Keeping the bound has a
    // cost, stated here rather than left implicit: a genuinely high
    // generation part that the previous unbounded test served correctly now
    // obtains no topology at all, and that cost is accepted rather than
    // discharged.
    //
    // The no-distribution fallback needs no branch of its own on either path.
    // The documented path writes the cache only inside an XRFDC_SUCCESS test,
    // which is the guard shape the two captures below use, and the raw decode
    // leaves a tile ungrouped when its register names no source. So a board
    // with no distribution keeps every tile at the ungrouped values PyRFdc.h
    // declares and its reset path is what it is today.
    //
    // The order is source first and normalization second, and the
    // normalization runs on every path out of the branch below, including
    // the one that obtained nothing, where it is a no-op. Neither arm calls
    // it: the ordering invariant it establishes is a property of the cache
    // that every consumer reads, not of whichever source happened to fill
    // that cache, and the same defect was reachable from both arms.
    ipType_ = RFdcInstPtr_->RFdc_Config.IPType;
    if ((ipType_ >= XRFDC_GEN3) && (ipType_ <= PYRFDC_IPTYPE_MAX_KNOWN)) {
        // The structure is roughly three kilobytes, so it lives in a block of
        // its own rather than being carried through the tile loops below.
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClkDistribution-Gen-3/DFE
        XRFdc_Distribution_System_Settings clkDist;

        // Every slot marked unused before the driver sees the structure.
        // cacheClkDistribution decides a slot is unused by testing one field
        // against a sentinel, and that test means nothing unless something
        // wrote the sentinel. The only party here that can guarantee it is
        // this caller: a driver version that fills only the slots it found
        // would otherwise leave the decode reading this process's stack as a
        // topology, and the range checks downstream admit any residue that
        // happens to look like a valid tile pair.
        //
        // The zero fill alone is not enough, which is why the loop follows
        // it. A zeroed slot reads as sourced by tile 0, and tile 0 is a real
        // tile. <cstring> is already included at the top of this file.
        std::memset(&clkDist, 0, sizeof(clkDist));
        for (uint32_t slot = 0; slot < 8; slot++) {
            clkDist.Distributions[slot].SourceTileId = XRFDC_CLK_DST_INVALID;
        }

        if (XRFdc_GetClkDistribution(RFdcInstPtr_, &clkDist) == XRFDC_SUCCESS) {
            cacheClkDistribution(&clkDist);
        } else {
            // The same argument the arm below is reported on, which was
            // always an argument about this branch rather than about one arm
            // of it. A boot that obtained no topology loses the master
            // before edge ordering for every tile, that is not a routine
            // outcome, and the source field alone is visible only to a host
            // that already knows which offset to read, on a boot whose
            // register path may itself have degraded. If anything the case
            // for saying so is stronger here: this arm is a vendor getter
            // refusing on a register path that has been measured degrading,
            // where the arm below is a reported generation nothing has ever
            // read on this hardware.
            //
            // What is specific to this arm: the cache keeps the no topology
            // source value, and the enumeration in PyRFdc.h says in its own
            // words that this value means asked and refused rather than
            // never asked. The distinction is therefore already named and
            // already published. What it is not is readable on a console,
            // and the console half is the one that has to survive the boot
            // where the register itself cannot be read.
            //
            // One sentence, so it sits far inside the console budget the
            // failure report is held to and needs no omission counter, and
            // assembled with std::string the way the rest of the message
            // assembly in this file is. The reported generation is
            // deliberately not named: the generation is not what went wrong
            // on this arm, and naming it would make the two no topology
            // lines harder rather than easier to tell apart. Through the log
            // channel only, never through setDiagError or errMsg_, so no
            // transaction verdict moves and construction does not fail.
            log_->error("%s", std::string("clock distribution topology not obtained"
                                    " because the documented getter returned"
                                    " non-success; reset falls back to per"
                                    " type ordering\n").c_str());
        }
    } else if (ipType_ < XRFDC_GEN3) {
        decodeClkDistributionRaw();
    } else {
        // No source consulted and no driver call made. The cache keeps the
        // ungrouped values PyRFdc.h declares and the status register says
        // which of the four cases this was.
        clkDistSource_ = PYRFDC_CLKDIST_SRC_UNSUPPORTED_GEN;

        // One sentence naming the reported value, so it is well inside the
        // console budget the failure report is held to and needs no omission
        // counter. Assembled with std::string and HexValue, matching the rest
        // of the message assembly in this file.
        log_->error("%s", (std::string("clock distribution topology not obtained"
                                 " because the reported IP generation ")
                     + HexValue(ipType_)
                     + " is outside the range this driver asks; reset falls"
                       " back to per type ordering\n").c_str());
    }
    normalizeClkDistCache();

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
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) == XRFDC_SUCCESS) {

                // Get the default Clock source
                if (XRFdc_GetClockSource(RFdcInstPtr_, i, j, &clkSrcDefault_[i][j]) == XRFDC_SUCCESS) {
                    clkSrcConfig_[i][j] = clkSrcDefault_[i][j];
                }

                // Get the default PLL configuration
                if (XRFdc_GetPLLConfig(RFdcInstPtr_, i, j, &pllDefault_[i][j]) == XRFDC_SUCCESS) {
                    pllDefault_[i][j].SampleRate = 1000.0*pllDefault_[i][j].SampleRate; // Convert from GSPS to MSPS
                    pllConfig_[i][j] = pllDefault_[i][j];
                    // Set the default PLL configuration (required for intializing the mixer's Sampling rate when doing XRFdc_GetMixerSettings)
                    XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
                }

                // Loop through block indexes
                for(k=0; k<4; k++) {

                    // Check if block enabled
                    if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) == XRFDC_SUCCESS) {

                        if (XRFdc_GetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]) == XRFDC_SUCCESS) {
                            qmcConfig_[i][j][k] = qmcDefault_[i][j][k];
                        }

                        // Get the default Mixer configuration
                        if ((XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) == XRFDC_SUCCESS) && (RFdcInstPtr_->UpdateMixerScale<=0x1U)) {
                            // Check for ADC tile or DAC DUC not bypassed
                            if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                                // Get the mixer setting
                                if ( XRFdc_GetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]) == XRFDC_SUCCESS) {
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
    // Two nested tests, each answering a different question.
    //
    // The outer one is so that libmetal is released by whichever end brought
    // it up and never by both. Every constructor path that released the
    // library cleared the flag immediately before doing so, so reaching this
    // point with it still set means the release is this destructor's to
    // perform and has not already happened.
    //
    // The inner one is so that XRFdc_RegisterMetal is never handed a driver
    // instance the driver declined to configure or never saw. That call
    // writes into the instance, which is the same reach through an
    // unconfigured instance that rejectIfDriverDead refuses on every
    // transaction a host can issue.
    //
    // What this deliberately does not do, stated here rather than left to be
    // rediscovered: on the path where XRFdc_CfgInitialize reported
    // non-success a metal device was registered during construction and is
    // not closed individually here, and the release below is what ends its
    // lifetime.
    if (metalReady_) {
        if (driverValid_) {
            struct metal_device *deviceptr = nullptr;
            if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) == XRFDC_SUCCESS && deviceptr) {
                metal_device_close(deviceptr);  // Close metal device if applicable
            }
        }
        metal_finish(); // Cleanup metal library
        metalReady_ = false;
    }
#endif

    log_->debug("PyRFdc::~PyRFdc() completed");
}

void PyRFdc::StartUp(int Tile_Id) {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        status = XRFdc_StartUp(RFdcInstPtr_, tileType_, Tile_Id);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        // One tile per call on this path, so there is nothing to
        // accumulate: start from empty records, record this tile, and hand
        // the same formatter the global reset path uses a set holding one
        // failure. The tile type is the one the dispatch set for this
        // transaction and the tile id is this call's own argument, both
        // passed explicitly so the helper's contract stays uniform and no
        // caller depends on member state.
        if ((Tile_Id >= 0) && (Tile_Id <= 3)) {
            clearTileDiag();
            recordTileFailure(tileType_, uint8_t(Tile_Id), "XRFdc_StartUp");
            setDiagError(buildDiagMessage("StartUp", Tile_Id));

        // The group form, one driver call covering every tile of the type.
        // It names no tile, so there is nothing to attribute and the
        // message it has always reported is kept.
        } else {
            errMsg_ = "StartUp(" + std::to_string(Tile_Id) + "): failed\n";
        }
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
        // Same shape as StartUp above, and for the same reason.
        if ((Tile_Id >= 0) && (Tile_Id <= 3)) {
            clearTileDiag();
            recordTileFailure(tileType_, uint8_t(Tile_Id), "XRFdc_Shutdown");
            setDiagError(buildDiagMessage("Shutdown", Tile_Id));

        } else {
            errMsg_ = "Shutdown(" + std::to_string(Tile_Id) + "): failed\n";
        }
    }
}

void PyRFdc::Reset(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    int i, j, k;
    int diagType, diagTile;
    int entryType = tileType_;
    uint32_t walk[8];
    uint32_t walkLen = 0;
    uint32_t w;
    // The group masters a recovery has already been attempted for during
    // this call. Local, and on this frame, for the reason the arming pass
    // below states: the bound is per global reset, so it has to expire with
    // the call rather than need a boundary of its own to be cleared on.
    uint32_t attempted[8];
    uint32_t attemptedLen = 0;
    // The half of the arming pass bound a resize could break silently.
    static_assert(sizeof(attempted) == sizeof(walk),
                  "the attempted list holds one entry for every tile the walk can hold");
    bool sweepFailed = false;

    // Check if read
    if (rdTxn_) {
        data_ = 1; // Always return 1 so this is a set() and not posted() cmd

    // Else write
    } else {

        // Check for global TYPE reset
        if (Tile_Id<0) {
            // Start this sweep with an empty set of per-tile records. Scoped
            // to the sweep and not to doTransaction, because one sweep walks
            // four tiles and every one of their results has to survive to
            // the end of it.
            clearTileDiag();

            // The tiles this call owns, in the order it has to visit them.
            //
            // Not the four tiles of tileType_ any more. This call takes the
            // distribution groups whose master is of its own tile type, in
            // full and master first, including edge tiles of the other
            // type, and then its own tiles that belong to no group. It
            // takes no group whose master is of the other type.
            //
            // On this carrier the ADC entry point therefore covers ADC 0, 1
            // and 2, which have their own clock pins and are ungrouped, and
            // declines to touch ADC 3. The DAC entry point covers DAC 0 as
            // the master of the distribution and then ADC 3, DAC 1, DAC 2
            // and DAC 3 as its edges. ADC 3 has no clock pin of its own, so
            // it cannot be restarted in isolation from DAC 0, and an ADC
            // reset declining to touch it is the property this division is
            // for rather than a tile it forgot.
            //
            // The list is built on this frame and the helper makes no
            // driver call, so nothing allocates and a cache that read back
            // wrong cannot reach a converter: every tile below still passes
            // the enable probe before any driver call is made against it.
            entryType = tileType_;
            walkLen = buildOwnedTileWalk(uint32_t(entryType), walk);

            // Clear the cycle counts for exactly the tiles this sweep is
            // about to cover, which is the walk above and nothing else.
            //
            // Deliberately not all eight. The two global resets are separate
            // transactions, so clearing the other type's counts here would
            // erase what the earlier call recorded and a host reading the
            // count after both resets would see only the second. On this
            // carrier that means the ADC call clears ADC 0, 1 and 2 and
            // leaves ADC 3 alone, and the DAC call clears DAC 0 through 3
            // and ADC 3, so the pair reads one per tile.
            //
            // The exactness flag is cleared in this same loop and for the
            // same tiles, because it qualifies the count beside it. Two
            // parallel arrays cleared in two places is a defect waiting to
            // happen, so they are cleared together.
            for(w=0; w<walkLen; w++) {
                resetCycles_[walk[w] >> 2][walk[w] & 0x3] = 0;
                resetCyclesInexact_[walk[w] >> 2][walk[w] & 0x3] = false;
            }

            // Init the MTS configurations
            XRFdc_MultiConverter_Init(&mtsConfig_[entryType], 0, 0, XRFDC_TILE_ID0);
            mtsConfig_[entryType].Tiles = 0;

            // Walk the owned tiles. i and j are the walked tile's own type
            // and id, which on the group path is not always this call's own
            // tile type, and everything inside the loop is per tile and
            // unchanged.
            for(w=0; w<walkLen; w++) {
                i = int(walk[w] >> 2);
                j = int(walk[w] & 0x3);

                // Init the MTS factor status. Cleared for the tiles this
                // call resets and no others, for the reason the cycle count
                // clear above states: a cached factor for a tile this call
                // did not restart is still the factor that tile is running.
                mtsfactor_[i][j] = 0;

                // Check if tile is enabled
                if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) == XRFDC_SUCCESS) {

                    // Read the tile's power-up status, before the PLL
                    // reconfigure below and not after it, because the
                    // reconfigure is what changes that status.
                    //
                    // A read is being added to the healthy path on purpose.
                    // It is the only way to know whether the reconfigure
                    // performed an IPSM cycle of its own, which it does
                    // only for a tile that was already powered up, and that
                    // is the fact the cycle decision further down turns on.
                    // The cost is one masked register read per tile per
                    // global reset.
                    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/RF-DAC/RF-ADC-Tile-n-Common-Status-Register-0x0228
                    uint32_t pwrUpStatus = XRFdc_RDReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(i, j), XRFDC_STATUS_OFFSET, XRFDC_PWR_UP_STAT_MASK);

                    // Restore default configuration
                    uint32_t pllStatus = XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
                    if (pllStatus != XRFDC_SUCCESS) {
                        recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_DynamicPLLConfig");
                    }

                    // Decide this tile's IPSM cycles, and decide separately
                    // whether the figure can be stood behind.
                    //
                    // The reconfigure above performs a cycle of its own,
                    // through the same restart primitive an explicit reset
                    // reaches, but only when the tile was already powered up
                    // and only when the call returned success. When both
                    // held, that internal cycle is the tile's one cycle and
                    // nothing further is issued here.
                    //
                    // A call that returns non-success is two different
                    // events wearing one return value. It may have refused
                    // before it touched the tile, which is what the
                    // reference frequency check does, or it may have driven
                    // the tile down and back up and then failed at the end.
                    // The first performed no cycle. The second performed one
                    // and then the compensating reset performs another.
                    //
                    // Re-reading the power-up status separates only one of
                    // those: a tile reading not powered up after the call
                    // was measurably driven down by it, so that tile is
                    // counted as two. A tile still reading powered up is a
                    // refusal and a completed late failure alike, which from
                    // here are indistinguishable, so the count is recorded
                    // as not exact rather than guessed at. The re-read costs
                    // one masked register access and is reachable only on a
                    // tile whose reconfigure already failed, so the healthy
                    // path costs exactly what it did before.
                    //
                    // The compensating reset is issued in every non-success
                    // case regardless of which of the two it was, and no new
                    // condition guards it. The failure this carrier actually
                    // produces is the early reference frequency refusal on
                    // its five external clock tiles, and withholding the
                    // reset from them would leave those tiles with no cycle
                    // at all.
                    //
                    // The count below counts IPSM cycles and not XRFdc_Reset
                    // calls. A counter placed only at the call site would
                    // read zero for a healthy powered-up tile, and a claim
                    // written against it would pass while proving nothing.
                    //
                    // A non-success from the compensating reset is recorded
                    // under the literal step name XRFdc_Reset, the same
                    // literal the two removed sites used, so every message
                    // and every log grep that names that step keeps working.
                    if ((pwrUpStatus != 0) && (pllStatus == XRFDC_SUCCESS)) {
                        resetCycles_[i][j]++;

                    } else {
                        bool leftDown = false;
                        bool cannotTell = false;

                        if (pwrUpStatus != 0) {
                            // Same base address, same offset and same mask
                            // as the read before the reconfigure, so the two
                            // answers are comparable.
                            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/RF-DAC/RF-ADC-Tile-n-Common-Status-Register-0x0228
                            uint32_t pwrUpAfter = XRFdc_RDReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(i, j), XRFDC_STATUS_OFFSET, XRFDC_PWR_UP_STAT_MASK);
                            leftDown = (pwrUpAfter == 0);
                            cannotTell = !leftDown;
                        }

                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
                        uint32_t resetStatus = XRFdc_Reset(RFdcInstPtr_, i, j);
                        if (resetStatus != XRFDC_SUCCESS) {
                            recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_Reset");
                        }

                        // Counted whether or not the reset returned success,
                        // because the tile was driven at its state machine
                        // either way and the count is a record of what was
                        // issued rather than of what worked. Two for a tile
                        // the reconfigure left down, because that tile was
                        // driven twice.
                        resetCycles_[i][j]++;
                        if (leftDown) {
                            resetCycles_[i][j]++;
                        }
                        if (cannotTell) {
                            resetCyclesInexact_[i][j] = true;
                        }
                    }

                    clkSrcConfig_[i][j] = clkSrcDefault_[i][j];
                    pllConfig_[i][j] = pllDefault_[i][j];

                    // Loop through block indexes
                    for(k=0; k<4; k++) {

                        // Check if block enabled
                        if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) == XRFDC_SUCCESS) {

                            // The settings status is recorded against the
                            // tile before the guard below, so the earlier
                            // of the two calls is the one the tile's step
                            // names. The guard itself now admits only a
                            // success. It previously admitted every status
                            // except the one failure value, which meant a
                            // status that was neither ran the event update
                            // on settings that may never have been applied.
                            uint32_t qmcStatus = XRFdc_SetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]);
                            if (qmcStatus != XRFDC_SUCCESS) {
                                recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_SetQMCSettings");
                            }
                            if (qmcStatus == XRFDC_SUCCESS) {
                                uint32_t qmcEventStatus = XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_QMC);
                                if (qmcEventStatus != XRFDC_SUCCESS) {
                                    recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_UpdateEvent");
                                }
                            }
                            qmcConfig_[i][j][k] = qmcDefault_[i][j][k];

                            // Get the default Mixer configuration
                            if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) == XRFDC_SUCCESS) {
                                // Check for ADC tile or DAC DUC not bypassed
                                if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                                    // Same shape as the quadrature pair
                                    // above, and for the same reason. The
                                    // event update stays nested in the
                                    // settings status, and that status now
                                    // has to be a success for it to run,
                                    // so a settings call answering outside
                                    // the documented pair no longer raises
                                    // an event for settings it may not
                                    // have applied.
                                    uint32_t mixerStatus = XRFdc_SetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]);
                                    if (mixerStatus != XRFDC_SUCCESS) {
                                        recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_SetMixerSettings");
                                    }
                                    if (mixerStatus == XRFDC_SUCCESS) {
                                        uint32_t mixerEventStatus = XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_MIXER);
                                        if (mixerEventStatus != XRFDC_SUCCESS) {
                                            recordTileFailure(uint32_t(i), uint8_t(j), "XRFdc_UpdateEvent");
                                        }
                                    }
                                }
                            }
                            mixerConfig_[i][j][k] = mixerDefault_[i][j][k];

                        }
                    }
                }
            }

            // Arm one bounded recovery per clock group whose edge tile could
            // not be restarted.
            //
            // Placed after the walk and before the derivation below on
            // purpose: a recovery that works clears the records of the tiles
            // it recovered, so it has to run before anything reads them.
            //
            // What arms one. A tile whose record names the step XRFdc_Reset
            // and whose cached role is edge. The restart timeout itself is
            // never visible here: the wait lives inside the driver and is a
            // static function there, so it surfaces to this code only as a
            // non-success return from that call, and that return is what the
            // record carries.
            //
            // A tile that failed at any other step does not arm one. A
            // failing PLL reconfigure or a failing settings write is a
            // different fault and re-cycling the group would not address it.
            // A tile that is ungrouped does not arm one either, because
            // there is no group to re-establish, and neither does a group
            // master: the recovery re-establishes a group around its master,
            // and a master that will not reset is not a group that can be
            // re-established by resetting it again.
            //
            // On a board the topology reports as having no distribution no
            // tile is ever an edge, so this pass finds nothing, the whole
            // recovery is unreachable, and that board's reset path is
            // bit-identical to the one before this was added.
            //
            // The bound is one attempt per group per global reset, whatever
            // number of that group's edge tiles failed. It is tracked in the
            // local set of attempted masters below rather than in a member
            // because a member would need a boundary to clear on, and this
            // work deliberately introduced no state that outlives a call. A
            // single attempt is the only bound this project can state
            // honestly: it adds roughly one extra cycle to a reset that was
            // going to fail anyway and nothing at all to the healthy path,
            // and each failed internal restart wait costs one second on the
            // transaction thread.
            for(w=0; w<walkLen; w++) {
                const uint32_t armIdx = walk[w];
                const TileDiag &armDiag = tileDiag_[armIdx >> 2][armIdx & 0x3];
                const TileClkDist &armTile = clkDist_[armIdx >> 2][armIdx & 0x3];
                uint32_t masterIdx;
                uint32_t a;
                bool alreadyAttempted = false;

                if (!armDiag.failed || (armDiag.step == nullptr)) {
                    continue;
                }
                if (std::strcmp(armDiag.step, "XRFdc_Reset") != 0) {
                    continue;
                }
                if (armTile.role != PYRFDC_CLKDIST_EDGE) {
                    continue;
                }

                masterIdx = (uint32_t(armTile.masterType) * 4) + uint32_t(armTile.masterTile);

                // No range guard here, because recoverClkGroup
                // owns the range refusal: it refuses an index above seven at
                // its own entry, ahead of every write, and announces the
                // refusal on the error channel, naming the index and the
                // arming tile. The one comparison with seven below decides
                // only whether the value takes part in the one attempt per
                // group bound, and it skips nothing: every value reaches the
                // call except an in-range master already in the list.
                //
                // An ungrouped tile carries 0xFF in both master fields, so an
                // ungrouped tile that reached this point would form 1275
                // above, while a master would form its own index, which is in
                // range. No range test can catch a master that a regressed
                // role test lets through. The only thing that catches it is
                // the group master sub-check of the board-free claim
                // a failing tile that is not an edge arms no recovery.
                // The role test above is what keeps both out today.
                //
                // Why the dedupe runs on in-range values only. Every ungrouped
                // tile forms the same value, so a dedupe on it would pass the
                // first such arm in a pass and drop every later one with no
                // line, which is exactly the silent skip a regression in the
                // role test must not be hidden behind. Scoped to in-range
                // values, the dedupe lets every out of range arm reach the
                // refusal, which announces it once per arming tile. An out of
                // range value names no group, so there is no group for the
                // one attempt bound to protect.
                //
                // The bound. attempted holds only in-range masters, gains at
                // most one entry per iteration of this loop, and this loop
                // runs walkLen times. walkLen is at most eight because
                // buildOwnedTileWalk emits each tile index at most once, and
                // attempted is declared the size of walk, which the
                // static_assert beside the two declarations holds. So
                // attemptedLen never exceeds walkLen and never exceeds eight.
                if (masterIdx <= 7) {
                    for (a = 0; a < attemptedLen; a++) {
                        if (attempted[a] == masterIdx) {
                            alreadyAttempted = true;
                        }
                    }
                    if (alreadyAttempted) {
                        continue;
                    }

                    attempted[attemptedLen++] = masterIdx;
                }
                recoverClkGroup(masterIdx, armIdx);
            }

            // A sweep failed when any tile recorded a failure. Derived from
            // the records rather than tracked in parallel with them, so a
            // call site that starts recording a failure cannot forget to
            // tell the sweep about it and leave the report unsent.
            for(diagType=0; diagType<2; diagType++) {
                for(diagTile=0; diagTile<4; diagTile++) {
                    if (tileDiag_[diagType][diagTile].failed) {
                        sweepFailed = true;
                    }
                }
            }

            // Widen the report to every tile of both types once the sweep
            // has failed. ResetAllAdc is called before ResetAllDac, so an
            // ADC reset that reports an error means the DAC reset never
            // runs and the DAC tiles are never looked at, even though the
            // ADC tile that fails on this carrier takes its clock from DAC
            // tile 0. Reading them afterwards from the host is not an
            // option: the register path degrades once a converter fails.
            //
            // This is a read and nothing else. It issues no reset, no PLL
            // reconfigure and no event update against the other type, and
            // it does not touch the sweep's own driver calls above. The
            // tiles it fills keep failed false, so the message tells a tile
            // that failed apart from a tile that was merely observed.
            //
            // Only on the failing path, so a healthy reset performs no
            // extra reads at all.
            if (sweepFailed) {
                for(diagType=0; diagType<2; diagType++) {
                    for(diagTile=0; diagTile<4; diagTile++) {

                        // Never read a tile twice. A tile that failed was
                        // already read at the instant it failed, and that
                        // reading is the one worth keeping; reading it
                        // again here would overwrite it with the state the
                        // tile settled into afterwards.
                        if (tileDiag_[diagType][diagTile].diagRead) {
                            continue;
                        }

                        readTileDiagnostics(uint32_t(diagType), uint8_t(diagTile),
                                            &tileDiag_[diagType][diagTile]);
                    }
                }
            }

            // Say which tiles of this call's own type it left alone.
            //
            // A global reset that legitimately passes over one of its own
            // tiles has to say so. On this carrier the ADC entry point
            // covers three of four ADC tiles, and a board owner reading a
            // log that shows an ADC reset quietly skipping a tile sees
            // something indistinguishable from the fault this driver is
            // being changed to prevent.
            //
            // log_->warning and not setDiagError. A deferral is a correct
            // outcome and the transaction still completes, so routing it
            // through the diagnostic error path would report a healthy
            // reset as a failure on every boot of every board that has a
            // distribution at all.
            //
            // The warning level and not the error level the recovery
            // report uses. That difference is deliberate and it is the
            // routineness that decides it: a deferral happens on every
            // reset of every board that has a distribution, while a
            // recovery means a reset failed and had to be retried. The
            // cost is that a reader who needs this line has to raise the
            // host side log level before the boot in question, which is
            // written down on the return code reference page.
            //
            // The line is emitted only when something was deferred, so a
            // board with no distribution gains no per reset log output.
            const std::string deferral = buildDeferralMessage(uint32_t(entryType));
            if (!deferral.empty()) {
                log_->warning("%s", deferral.c_str());
            }

        // Else not a global reset
        } else {
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
            status = XRFdc_Reset(RFdcInstPtr_, tileType_, Tile_Id);
        }
    }

    // Check if not successful
    if (sweepFailed) {
        setDiagError(buildDiagMessage("Reset", Tile_Id));

    } else if (status != XRFDC_SUCCESS) {
        // The single-tile branch, reached at a tile-scoped address rather
        // than by sweeping. _Rfdc.py's Init() calls this per tile after
        // both global resets, so a failure here is reachable on the same
        // boot as one in the sweep and was just as unattributed.
        if ((Tile_Id >= 0) && (Tile_Id <= 3)) {
            clearTileDiag();
            recordTileFailure(tileType_, uint8_t(Tile_Id), "XRFdc_Reset");
            setDiagError(buildDiagMessage("Reset", Tile_Id));

        } else {
            errMsg_ = "Reset(" + std::to_string(Tile_Id) + "): failed\n";
        }
    }
}

void PyRFdc::readTileDiagnostics(uint32_t type, uint8_t tile, TileDiag *out) {
    uint32_t lockStatus = 0;

    if (out == nullptr) {
        return;
    }

    // Gate the whole read on the one call in this sequence that can report a
    // refusal. XRFdc_ReadReg hands back a word and has no way to say it could
    // not service the read, so a tile that is not answering would otherwise
    // fill this record with zeros that look exactly like a tile sitting in
    // state 0. Done first, so a refused tile performs no reads at all and
    // diagRead stays false for the message to report.
    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPLLLockStatus
    if (XRFdc_GetPLLLockStatus(RFdcInstPtr_, type, tile, &lockStatus) != XRFDC_SUCCESS) {
        return;
    }

    // Raw, unlike the PLLLockStatus transaction body, which adds one so the
    // host can tell an unread RemoteVariable from a read one. Nothing here is
    // a RemoteVariable, and a diagnostic that silently offsets a register
    // value is worse than no diagnostic.
    out->pllLock = lockStatus;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-State-Register-0x0008
    out->restartState = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(type, tile), XRFDC_RESTART_STATE_OFFSET);

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Current-State-Register-0x000C
    out->currentState = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(type, tile), 0x000C);

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/Clock-Detector-Register-0x0084-Gen-3/DFE
    out->clockDetector = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(type, tile), 0x0084);

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/RF-DAC/RF-ADC-Tile-n-Common-Status-Register-0x0228
    out->commonStatus = XRFdc_ReadReg(RFdcInstPtr_, XRFDC_CTRL_STS_BASE(type, tile), 0x0228);

    out->diagRead = true;
}

void PyRFdc::recordTileFailure(uint32_t type, uint8_t tile, const char *step) {
    TileDiag &diag = tileDiag_[type][tile];

    // Keep the first step that went wrong, not the last. A tile whose PLL
    // reconfigure fails and whose reset then fails as well went wrong at
    // the reconfigure; naming the reset would name the consequence and
    // hide the cause. The opposite choice is equally defensible, which is
    // why it is written down here rather than left to be inferred from the
    // guard.
    if (!diag.failed) {
        diag.failed = true;
        diag.step = step;
    }

    // Read the tile now, while it is still in the state that failed, and
    // once per sweep at most. The host register path degrades once a
    // converter fails, so a value read back after a later step is not the
    // value that was there when the tile first went wrong.
    if (!diag.diagRead) {
        readTileDiagnostics(type, tile, &diag);
    }
}

void PyRFdc::clearTileDiag() {
    int t, i;

    for (t=0; t<2; t++) {
        for (i=0; i<4; i++) {
            tileDiag_[t][i].failed        = false;
            tileDiag_[t][i].step          = "";
            tileDiag_[t][i].diagRead      = false;
            tileDiag_[t][i].restartState  = 0;
            tileDiag_[t][i].currentState  = 0;
            tileDiag_[t][i].clockDetector = 0;
            tileDiag_[t][i].commonStatus  = 0;
            tileDiag_[t][i].pllLock       = 0;
        }
    }
}

std::string PyRFdc::buildDiagMessage(const char *entryPoint, int tileId) {
    // Indexed by tile type, the same way tileDiag_ is.
    static const char* const typeName[2] = {"ADC", "DAC"};
    int failing = 0;
    int omitted = 0;
    int t, i;
    std::string msg;

    for (t=0; t<2; t++) {
        for (i=0; i<4; i++) {
            if (tileDiag_[t][i].failed) {
                failing++;
            }
        }
    }

    // Opens with the same entry-point form the single-tile path uses, so a
    // reader and any log grep still find Reset(-1): failed at the front.
    msg = std::string(entryPoint) + "(" + std::to_string(tileId) + "): failed, "
        + std::to_string(failing) + " failing tile(s):";

    // Walk the records in index order, ADC 0 to 3 then DAC 0 to 3, rather
    // than in the order the failures were found. Discovery order is stable
    // only by accident of how the sweep loops; this order is specified, so
    // two runs that failed on the same tiles read the same way.
    for (t=0; t<2; t++) {
        for (i=0; i<4; i++) {
            const TileDiag &diag = tileDiag_[t][i];
            std::string record;

            // A tile that neither failed nor was read has nothing to say.
            if (!diag.failed && !diag.diagRead) {
                continue;
            }

            record = " " + std::string(typeName[t]) + std::to_string(i) + " ";
            record += diag.failed ? diag.step : "ok";

            // No state fields at all when the read did not run. A zero here
            // would be indistinguishable from a tile genuinely reading zero,
            // which is the reading that made an earlier register snapshot
            // impossible to interpret.
            if (!diag.diagRead) {
                record += " diagnostics unavailable;";

            } else {
                // Mask to four bits before indexing. The host side declares
                // this field as four bits wide, but the read above is of the
                // whole word, so an out of range value would index past the
                // table.
                record += " state=" + HexValue(diag.currentState)
                        + "(" + IPSM_STATE_NAMES[diag.currentState & 0x0F] + ")"
                        + " restart=" + HexValue(diag.restartState)
                        + " clkdet=" + HexValue(diag.clockDetector)
                        + " common=" + HexValue(diag.commonStatus)
                        + " plllock=" + HexValue(diag.pllLock) + ";";
            }

            // Stop at the budget rather than let the console cut the line
            // off wherever it happens to run out. Counting what was left out
            // keeps the report honest: a reader can tell a short line that
            // said everything from one that did not.
            if ((msg.size() + record.size()) > DIAG_MSG_BUDGET) {
                omitted++;
                continue;
            }

            msg += record;
        }
    }

    if (omitted > 0) {
        msg += " +" + std::to_string(omitted) + " tile(s) omitted, line budget reached;";
    }

    msg += "\n";
    return msg;
}

void PyRFdc::CustomStartUp(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    uint32_t StartState = (data_>>0)&0xF;
    uint32_t EndState   = (data_>>8)&0xF;

    // Check if read
    if (rdTxn_) {
        // Pre-existing behavior, kept deliberately. Unlike the other
        // command offsets, a read of this one reports a failure rather
        // than handing back one and executing nothing. It is the only
        // guaranteed non-success return reachable from the host with no
        // real converter fault, which makes it the way this reporting path
        // is exercised on a healthy board, so it is relied on elsewhere and
        // is not converted to the harmless-read form of its neighbours.
        status = XRFDC_FAILURE;

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        status = XRFdc_CustomStartUp(RFdcInstPtr_, tileType_, Tile_Id, StartState, EndState);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        // Same shape as StartUp above, and for the same reason.
        if ((Tile_Id >= 0) && (Tile_Id <= 3)) {
            clearTileDiag();
            recordTileFailure(tileType_, uint8_t(Tile_Id), "XRFdc_CustomStartUp");
            setDiagError(buildDiagMessage("CustomStartUp", Tile_Id));

        } else {
            errMsg_ = "CustomStartUp(" + std::to_string(Tile_Id) + "): failed\n";
        }
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
                    if (status == XRFDC_SUCCESS) {
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
                if (status == XRFDC_SUCCESS) {
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
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, tileType_, i) == XRFDC_SUCCESS) {
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

// Why the driver instance is not usable, as one word at offset 0x1200C.
//
// Same shape as the two pure-state bodies above: a read hands back a member
// and a write is refused. It names RFdcInstPtr_ nowhere, which is the whole
// reason it exists, because the instance it is describing is the one that
// may never have been initialized.
//
// Deliberately not polled from the host side. The variable that reads it in
// python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py carries no poll
// interval, because a polled variable adds a background transaction every
// interval to a driver that may be dead, on a register path that has been
// measured degrading once a converter fails. It is read when someone asks.
void PyRFdc::InitFailReason() {
    // Check for a write
    if (!rdTxn_) {
        errMsg_ = "InitFailReason(): read only\n";
    } else {
        data_ = initFailReason_;
    }
}

// The package tile index of one tile, on a four-ADC four-DAC part.
//
// The distribution chain is numbered by package tile rather than by tile
// type and tile id: the DAC tiles occupy the low half of the chain in
// descending tile order and the ADC tiles the high half, so DAC n sits at
// 3 - n and ADC n sits at 7 - n. That is what puts DAC 0 and ADC 3 next to
// each other, which is the adjacency a distribution between them needs.
// This rule comes from driver source that is not present on this host, so
// it is written down here rather than left to be re-derived.
static uint32_t ClkDistPackageIndex(uint32_t type, uint32_t tile) {
    return (type == XRFDC_DAC_TILE) ? (3 - tile) : (7 - tile);
}

// The inverse of the rule above, so a walk over package indices can name
// the tiles it visited.
static void ClkDistTypeTile(uint32_t pkg, uint32_t *type, uint32_t *tile) {
    if (pkg <= 3) {
        *type = XRFDC_DAC_TILE;
        *tile = 3 - pkg;
    } else {
        *type = XRFDC_ADC_TILE;
        *tile = 7 - pkg;
    }
}

void PyRFdc::cacheClkDistribution(const XRFdc_Distribution_System_Settings *dist) {
    if (dist == nullptr) {
        return;
    }

    for (uint32_t slot = 0; slot < 8; slot++) {
        const XRFdc_Distribution_Settings *entry = &dist->Distributions[slot];

        // An unused slot is marked by its source tile id and not by a zero
        // fill, because tile 0 is a real tile.
        if (entry->SourceTileId == XRFDC_CLK_DST_INVALID) {
            continue;
        }

        // Every index below reaches a fixed [2][4] member array, so a slot
        // naming a tile outside that range is skipped rather than trusted.
        // The values come from a driver reading hardware registers, and a
        // register that read back as something unexpected is exactly the
        // case this driver is being taught to survive.
        if ((entry->SourceType > XRFDC_DAC_TILE) ||
            (entry->SourceTileId > XRFDC_TILE_ID_MAX) ||
            (entry->EdgeTypes[0] > XRFDC_DAC_TILE) ||
            (entry->EdgeTypes[1] > XRFDC_DAC_TILE) ||
            (entry->EdgeTileIds[0] > XRFDC_TILE_ID_MAX) ||
            (entry->EdgeTileIds[1] > XRFDC_TILE_ID_MAX)) {
            continue;
        }

        const uint32_t edge0 = ClkDistPackageIndex(entry->EdgeTypes[0], entry->EdgeTileIds[0]);
        const uint32_t edge1 = ClkDistPackageIndex(entry->EdgeTypes[1], entry->EdgeTileIds[1]);
        const uint32_t lower = (edge0 < edge1) ? edge0 : edge1;
        const uint32_t upper = (edge0 < edge1) ? edge1 : edge0;

        // Both edges on one package index is a distribution of a single
        // tile, which is a tile on its own clock however the slot is filled
        // in. It contributes no group and leaves that tile ungrouped, which
        // is how the driver itself treats a tile whose source is itself.
        if (lower == upper) {
            continue;
        }

        // The group is every tile between the two edges inclusive.
        for (uint32_t pkg = lower; pkg <= upper; pkg++) {
            uint32_t type = 0;
            uint32_t tile = 0;
            ClkDistTypeTile(pkg, &type, &tile);

            // First slot wins. A tile an earlier slot already placed keeps
            // that placement, and a later slot whose package range also
            // covers it does not take it over.
            //
            // Stated as a rule rather than left to chance because two slots
            // with overlapping ranges would otherwise give one tile two
            // masters depending on the order the slots are read in. A tile
            // holding two masters is walked by two groups and cycled twice
            // inside one pair of global resets, which is the defect this
            // driver is removing wearing a different hat. The same rule
            // keeps a tile that is an edge of an earlier group from being
            // turned into the master of a later one, so no tile ever holds
            // two roles either.
            if (clkDist_[type][tile].role != PYRFDC_CLKDIST_UNGROUPED) {
                continue;
            }

            clkDist_[type][tile].masterType = uint8_t(entry->SourceType);
            clkDist_[type][tile].masterTile = uint8_t(entry->SourceTileId);

            if ((type == entry->SourceType) && (tile == entry->SourceTileId)) {
                clkDist_[type][tile].role = PYRFDC_CLKDIST_MASTER;
            } else {
                clkDist_[type][tile].role = PYRFDC_CLKDIST_EDGE;
            }
        }

        clkDistGroups_++;
    }

    // Recorded whatever the decode found, including nothing. This says which
    // layer answered the question, not whether the answer had a group in it,
    // and a board with no distribution is a different fact from a board that
    // was never asked.
    clkDistSource_ = PYRFDC_CLKDIST_SRC_API;
}

// Decode the distribution topology out of the per-tile clock detect
// register, for a driver that refuses the documented query.
//
// The decode is not invented here. It is the one the driver itself performs
// inside XRFdc_GetClkDistribution, reproduced step for step: the search for
// the source package tile is the loop at xrfdc_clock.c:944-947, and turning
// a package tile index back into a tile type and tile id is
// XRFdc_DistTile2TypeTile at xrfdc_clock.c:2040-2053, both read at upstream
// tag xilinx_v2026.1. Reproducing rather than paraphrasing is deliberate:
// the two topology sources have to agree about the same board, and the only
// way to be sure they do is to run the same arithmetic.
//
// Every index that reaches the fixed [2][4] member array is bounded by
// construction and not by a range check, which is the same protection
// cacheClkDistribution gets from its explicit checks and is worth stating
// because the unsigned package arithmetic underflows on an out of range
// tile id. The two tile loops below run over 0..1 and 0..3, so a package
// index formed from them is in 0..7. A decoded source index is
// XRFDC_CLK_DST_TILE_224 minus a loop variable bounded by
// XRFDC_CLK_DST_TILE_224, so it is in 0..7 as well, and ClkDistTypeTile
// maps 0..7 onto a tile id in 0..3 on both halves.
void PyRFdc::decodeClkDistributionRaw() {
    uint32_t srcPkg[2][4];
    bool hasSrc[2][4];
    uint32_t type, tile;

    // First pass: what each tile says its own clock comes from.
    for (type = 0; type < 2; type++) {
        for (tile = 0; tile < 4; tile++) {
            uint32_t i;

            srcPkg[type][tile] = 0;
            hasSrc[type][tile] = false;

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckTileEnabled
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, type, tile) != XRFDC_SUCCESS) {
                continue;
            }

            // The per tile clock detect register at offset 0x0080, which
            // carries the distribution source. This is not the clock
            // detector status at 0x0084 that readTileDiagnostics reads.
            // https://docs.amd.com/r/en-US/pg269-rf-data-converter
            const uint32_t detect = XRFdc_RDReg(RFdcInstPtr_,
                                                XRFDC_CTRL_STS_BASE(type, tile),
                                                XRFDC_CLOCK_DETECT_OFFSET,
                                                XRFDC_CLOCK_DETECT_SRC_MASK);

            // Walk the two bit fields from the lowest upwards. The first
            // one whose shifted value equals XRFDC_ENABLED names the
            // source, and the package index counts down from
            // XRFDC_CLK_DST_TILE_224 as the field index counts up, because
            // the package tile constants are numbered highest tile first.
            for (i = 0; i <= XRFDC_CLK_DST_TILE_224; i++) {
                if ((detect >> (i << 1)) == XRFDC_ENABLED) {
                    srcPkg[type][tile] = XRFDC_CLK_DST_TILE_224 - i;
                    hasSrc[type][tile] = true;
                    break;
                }
            }

            // A register that reads zero names no source at all, and this
            // tile contributes nothing. The driver's own equivalent path
            // calls that a distribution system misconfiguration and warns.
            // Here it is simply no distribution: the tile stays ungrouped
            // and the reset path for it is bit identical to today, which is
            // the fallback this driver has to keep for every board with no
            // distribution at all.
        }
    }

    // Second pass: which of the tiles that name themselves are masters.
    //
    // A tile whose decoded source is itself is not automatically ungrouped,
    // and its own register cannot settle which it is. It is a master when at
    // least one other tile decoded to it and ungrouped when no other tile
    // did, so the answer depends on what every other tile said. That is why
    // this is a second pass rather than more work inside the first.
    for (type = 0; type < 2; type++) {
        for (tile = 0; tile < 4; tile++) {
            uint32_t masterType = 0;
            uint32_t masterTile = 0;
            uint32_t ownPkg;

            if (!hasSrc[type][tile]) {
                continue;
            }

            ownPkg = ClkDistPackageIndex(type, tile);
            ClkDistTypeTile(srcPkg[type][tile], &masterType, &masterTile);

            if (srcPkg[type][tile] == ownPkg) {
                uint32_t followers = 0;
                uint32_t t, n;

                for (t = 0; t < 2; t++) {
                    for (n = 0; n < 4; n++) {
                        if ((t == type) && (n == tile)) {
                            continue;
                        }
                        if (hasSrc[t][n] && (srcPkg[t][n] == ownPkg)) {
                            followers++;
                        }
                    }
                }

                // Nobody follows it, so it is a tile on its own clock,
                // which is ungrouped and contributes no group.
                if (followers == 0) {
                    continue;
                }

                clkDist_[type][tile].role = PYRFDC_CLKDIST_MASTER;
                clkDistGroups_++;
            } else {
                clkDist_[type][tile].role = PYRFDC_CLKDIST_EDGE;
            }

            clkDist_[type][tile].masterType = uint8_t(masterType);
            clkDist_[type][tile].masterTile = uint8_t(masterTile);
        }
    }

    // Recorded whatever the decode found, including nothing, for the same
    // reason the documented decode records its own source. This says that a
    // decode ran, not that it found a group, so a decode that ran and found
    // nothing stays distinguishable from one that was never attempted, and
    // clkDistGroups_ is what says whether anything was found.
    clkDistSource_ = PYRFDC_CLKDIST_SRC_RAW_DECODE;
}

// Make the cache satisfy the precondition the walk's ordering rests on.
//
// buildOwnedTileWalk's first pass keys on the master role. A cache that
// names a master without marking one therefore yields no group at all, both
// the master and its edges fall through to the pass that sorts by tile
// index, and on this carrier that emits ADC 3 at index 3 ahead of DAC 0 at
// index 4. That is the ordering this driver exists to remove, produced by a
// cache shape both decoders can reach without any anomalous register value.
//
// It makes no driver call, which is worth stating because every other call
// site in this file compares a driver return against XRFDC_SUCCESS and there
// is nothing here to compare. That is also what lets it run on the path
// where the documented topology query already returned an error.
void PyRFdc::normalizeClkDistCache() {
    uint32_t idx;

    // Pass one, resolve. An edge naming another edge is one link of a
    // chain, and what the walk needs from it is the tile at the far end.
    //
    // Bounded at eight hops, with an in-range test and a revisit test at
    // every hop. Eight tiles means a chain longer than eight hops has
    // visited one twice and is a cycle, this runs at construction time on a
    // device whose registers can read back anything, and a loop that did not
    // terminate here is a bridge that never starts. A tile the walk cannot
    // resolve keeps the master fields the decode gave it: it is still owned
    // by exactly one entry point through tileIsOwnedBy and still walked by
    // that one, so membership stays total either way.
    for (idx = 0; idx < 8; idx++) {
        bool seen[8] = {false, false, false, false, false, false, false, false};
        bool resolved = false;
        uint32_t named;
        uint32_t hop;

        if (clkDist_[idx >> 2][idx & 0x3].role != PYRFDC_CLKDIST_EDGE) {
            continue;
        }

        named = (uint32_t(clkDist_[idx >> 2][idx & 0x3].masterType) * 4) +
                uint32_t(clkDist_[idx >> 2][idx & 0x3].masterTile);
        if (named > 7) {
            continue;
        }

        seen[idx] = true;

        for (hop = 0; hop < 8; hop++) {
            if (seen[named]) {
                break;
            }
            seen[named] = true;

            if (clkDist_[named >> 2][named & 0x3].role != PYRFDC_CLKDIST_EDGE) {
                resolved = true;
                break;
            }

            named = (uint32_t(clkDist_[named >> 2][named & 0x3].masterType) * 4) +
                    uint32_t(clkDist_[named >> 2][named & 0x3].masterTile);
            if (named > 7) {
                break;
            }
        }

        if (resolved) {
            clkDist_[idx >> 2][idx & 0x3].masterType = uint8_t(named >> 2);
            clkDist_[idx >> 2][idx & 0x3].masterTile = uint8_t(named & 0x3);
        }
    }

    // Pass two, promote. A tile at least one edge names, that no decode
    // marked, is the master of a group whatever its role field says.
    //
    // The group count is incremented here because a group that exists only
    // because this pass found it is still a group, and a status register
    // reporting no groups beside a map register naming a master would have
    // the two words contradict each other. On the path where the decode
    // counted a slot whose master it never marked, the count therefore rises
    // once more than the slot count, which is the honest reading: the slot
    // and the group this pass recovered from it are two separate findings.
    //
    // Nothing is demoted here and a tile already marked as a master is left
    // exactly as it was by this pass, though pass three below withdraws a
    // master whose own master fields name a different tile. The byte
    // identical outcome promised here therefore belongs to a cache that
    // already satisfies the postcondition rather than to pass two on its
    // own, and every claim written against such a cache stays green.
    for (idx = 0; idx < 8; idx++) {
        uint32_t named;

        if (clkDist_[idx >> 2][idx & 0x3].role != PYRFDC_CLKDIST_EDGE) {
            continue;
        }

        named = (uint32_t(clkDist_[idx >> 2][idx & 0x3].masterType) * 4) +
                uint32_t(clkDist_[idx >> 2][idx & 0x3].masterTile);
        if (named > 7) {
            continue;
        }

        if (clkDist_[named >> 2][named & 0x3].role != PYRFDC_CLKDIST_UNGROUPED) {
            continue;
        }

        // Named as its own master, the way both decoders already record a
        // master, so tileIsOwnedBy and ClkDistMap need no rule of their own
        // for a tile this pass marked.
        clkDist_[named >> 2][named & 0x3].role = PYRFDC_CLKDIST_MASTER;
        clkDist_[named >> 2][named & 0x3].masterType = uint8_t(named >> 2);
        clkDist_[named >> 2][named & 0x3].masterTile = uint8_t(named & 0x3);
        clkDistGroups_++;
    }

    // Pass three, withdraw. A tile the cache cannot place in an orderable
    // group is put in no group at all, in both of the ways the cache can
    // hold one: an edge still naming a tile that is not a marked master, and
    // a tile carrying the master role whose own master fields name a
    // different tile.
    //
    // What that buys is that the postcondition holds for every input rather
    // than for every input the two decoders happen to produce. The
    // alternative, naming a master picked by tile index or by the order the
    // resolve pass walked the chain, would publish an ordering the registers
    // do not support, on a word a host reads as evidence of what this driver
    // did.
    //
    // Two forward sub-passes, each one sweep of the eight tiles, and the
    // ordering between them is load bearing rather than a tidy-up. Sub-pass
    // one runs to completion before the first edge is inspected, so the set
    // of tiles still carrying the master role is fixed for the whole of
    // sub-pass two. Sub-pass two then changes the role of edges only, and an
    // edge that another edge names fails its own keep test at its own
    // inspection, because its target does not carry the master role either.
    // Each sub-pass is therefore monotone within itself: a tile that fails
    // its own test when its index comes up would fail it just as surely
    // later, and a tile that passes cannot be made to fail by anything the
    // same sub-pass does afterwards. Every tile that has to be withdrawn is
    // withdrawn at its own inspection, and one sweep of eight in each is
    // sufficient. Interleaving the two would break that argument, because an
    // edge inspected before the master it names was withdrawn would be kept
    // and nothing later would come back for it.
    //
    // The out of range arm and the unresolvable chain arm are the same case
    // and are treated identically on purpose, and so is a master that names
    // another tile. None of the three is reachable from either decoder
    // today: the documented path range checks every field of a slot before
    // it uses one and takes the master role only where the slot's source is
    // the tile being written, and the raw decode's indices are bounded by
    // construction and take that role only where the decoded source is the
    // tile's own package index. But the postcondition is published with no
    // proviso, so this makes it total rather than conditional.
    //
    // The group count is deliberately left exactly as the decode set it, so a
    // cache whose slots were counted and whose grouping was then withdrawn
    // publishes a non-zero count beside an all-ungrouped map. That
    // disagreement belongs to the separate question of what that field
    // counts, which the two decoders and the promote pass above already
    // answer three different ways. Redefining it here would be a fourth
    // answer and would move the meaning of a published register as a side
    // effect of a different fix.
    {
        static const char* const typeName[2] = {"ADC", "DAC"};
        std::string tiles;
        uint32_t withdrawn = 0;

        // Sub-pass one, the marked masters. A tile carrying the master role
        // is a master only if its own master fields name itself, which is
        // what every consumer of this cache reads it as: tileIsOwnedBy
        // routes a tile to the reset of its master's type, and ClkDistMap
        // publishes a master's own nibble as its own index. A tile marked
        // master that names some other tile satisfies neither, so the honest
        // answer for it is the same one an unresolvable edge gets.
        for (idx = 0; idx < 8; idx++) {
            uint32_t named;

            if (clkDist_[idx >> 2][idx & 0x3].role != PYRFDC_CLKDIST_MASTER) {
                continue;
            }

            named = (uint32_t(clkDist_[idx >> 2][idx & 0x3].masterType) * 4) +
                    uint32_t(clkDist_[idx >> 2][idx & 0x3].masterTile);

            // No in-range test ahead of this comparison, unlike the two
            // passes above, because idx is in 0 to 7 and a composed value
            // outside that range cannot equal it. An out of range master
            // index is therefore withdrawn by this same inequality rather
            // than by a test of its own, and the composed value is only ever
            // compared here and never used as a subscript.
            if (named == idx) {
                continue;
            }

            // The same sentinel the edge withdrawal below writes, for the
            // reason given there.
            clkDist_[idx >> 2][idx & 0x3].role = PYRFDC_CLKDIST_UNGROUPED;
            clkDist_[idx >> 2][idx & 0x3].masterType = 0xFF;
            clkDist_[idx >> 2][idx & 0x3].masterTile = 0xFF;

            withdrawn++;
            tiles += " " + std::string(typeName[idx >> 2]) + std::to_string(idx & 0x3);
        }

        // Sub-pass two, the edges. Unchanged, and it picks up every edge
        // orphaned by sub-pass one for free, because such an edge now names
        // a tile whose role is ungrouped and so fails the keep test below.
        for (idx = 0; idx < 8; idx++) {
            uint32_t named;

            if (clkDist_[idx >> 2][idx & 0x3].role != PYRFDC_CLKDIST_EDGE) {
                continue;
            }

            named = (uint32_t(clkDist_[idx >> 2][idx & 0x3].masterType) * 4) +
                    uint32_t(clkDist_[idx >> 2][idx & 0x3].masterTile);

            if ((named <= 7) &&
                (clkDist_[named >> 2][named & 0x3].role == PYRFDC_CLKDIST_MASTER)) {
                continue;
            }

            // Both master fields go back to the sentinel the header declares
            // for an ungrouped tile, not to zero, so the tile becomes
            // indistinguishable from one no decode ever touched rather than
            // reading as mastered by ADC tile 0, which is a real tile and a
            // false statement.
            clkDist_[idx >> 2][idx & 0x3].role = PYRFDC_CLKDIST_UNGROUPED;
            clkDist_[idx >> 2][idx & 0x3].masterType = 0xFF;
            clkDist_[idx >> 2][idx & 0x3].masterTile = 0xFF;

            withdrawn++;
            tiles += " " + std::string(typeName[idx >> 2]) + std::to_string(idx & 0x3);
        }

        // Emitted only when something was withdrawn, so a board whose cache
        // already held the invariant gains no console output at all.
        //
        // Through the log channel and never through setDiagError or errMsg_.
        // A cache that cannot be ordered is a topology outcome and not a
        // transaction failure, so the verdict a caller gets back must not
        // move. At the error level and not the warning level, for the reason
        // the recovery report above is at the error level: the host side
        // bridge filters by a global level whose default admits errors and
        // discards warnings, and a boot that lost the ordering guarantee has
        // to be visible without anyone knowing to read a register for it.
        //
        // At most eight tile names, which sits far inside the console budget
        // the failure report is held to, so there is no omission counter here.
        if (withdrawn > 0) {
            log_->error("%s", (std::string("clock distribution grouping withdrawn from ")
                         + std::to_string(withdrawn)
                         + " tile(s) because the cached topology named no orderable"
                           " master:" + tiles
                         + "; reset falls back to per type ordering\n").c_str());
        }
    }
}

bool PyRFdc::tileIsOwnedBy(uint32_t type, uint32_t idx) const {
    const TileClkDist &tile = clkDist_[idx >> 2][idx & 0x3];

    // A tile in no group belongs to the reset of its own type, which is
    // what every tile on a board with no distribution at all is.
    if (tile.role == PYRFDC_CLKDIST_UNGROUPED) {
        return ((idx >> 2) == type);
    }

    // A tile in a group belongs to the reset of its master's type, whatever
    // its own type is. Both decodes record a master's own master as itself,
    // so this covers a master and an edge with one rule rather than two.
    return (uint32_t(tile.masterType) == type);
}

// The tiles one global reset owns, in the order it has to visit them.
//
// Both the group order and the within group order are specified here rather
// than left to the shape of a loop, so two runs against the same board
// produce the same recorded sequence and an ordering claim means something.
//
// Groups first, in ascending order of the master's tile index, and inside a
// group the master before every one of its edge tiles, those in ascending
// tile index order. Then the remaining tiles this call owns, in ascending
// tile index order, which on a consistent topology is exactly this type's
// ungrouped tiles in ascending tile id.
//
// That last pass is written as "the remaining tiles this call owns" and not
// as "this type's ungrouped tiles" on purpose. A cache describing an edge
// whose master was never marked as one, which is what a distribution slot
// naming a source outside its own edge range looks like, would otherwise
// leave that tile in no walk at all and silently unreset by either entry
// point. Membership is total by construction this way: every tile is owned
// by exactly one of the two calls, and every tile a call owns is walked.
uint32_t PyRFdc::buildOwnedTileWalk(uint32_t type, uint32_t *walk) const {
    bool taken[8] = {false, false, false, false, false, false, false, false};
    uint32_t count = 0;
    uint32_t master, idx;

    if (walk == nullptr) {
        return 0;
    }

    for (master = 0; master < 8; master++) {
        // Only the groups this call masters. A group mastered by the other
        // type is that call's work, in full.
        if ((master >> 2) != type) {
            continue;
        }
        if (clkDist_[master >> 2][master & 0x3].role != PYRFDC_CLKDIST_MASTER) {
            continue;
        }

        walk[count++] = master;
        taken[master] = true;

        for (idx = 0; idx < 8; idx++) {
            const TileClkDist &tile = clkDist_[idx >> 2][idx & 0x3];

            if (taken[idx] || (tile.role != PYRFDC_CLKDIST_EDGE)) {
                continue;
            }
            if (((uint32_t(tile.masterType) * 4) + uint32_t(tile.masterTile)) != master) {
                continue;
            }

            walk[count++] = idx;
            taken[idx] = true;
        }
    }

    for (idx = 0; idx < 8; idx++) {
        if (taken[idx] || !tileIsOwnedBy(type, idx)) {
            continue;
        }

        walk[count++] = idx;
        taken[idx] = true;
    }

    return count;
}

// The tiles this call passed over, named with the tile that took them.
//
// Length. The opening clause is under a hundred characters and each tile
// adds about twenty, and at most four tiles of one type can be deferred, so
// the worst case is under two hundred against the 960 character budget the
// console imposes. The budget is not at risk here and no omission counter
// is needed, which is worth saying because the failure report further up
// does need one: that line can carry eight tiles of register values.
//
// Built with std::string concatenation, matching the constraint the rest of
// the message assembly in this file works under. No <iomanip> and no
// snprintf, so the text is the same under the Yocto build and a host build
// and contains nothing but ASCII. No hex field appears because nothing on
// this line is a register value.
std::string PyRFdc::buildDeferralMessage(uint32_t type) const {
    // The same two names and the same tile numbering the failure report
    // uses, so a reader comparing the two lines is reading one vocabulary.
    static const char* const typeName[2] = {"ADC", "DAC"};
    std::string tiles;
    uint32_t deferred = 0;
    uint32_t idx;

    if (type > XRFDC_DAC_TILE) {
        return std::string();
    }

    for (idx = 0; idx < 8; idx++) {
        const TileClkDist &tile = clkDist_[idx >> 2][idx & 0x3];

        if (((idx >> 2) != type) || tileIsOwnedBy(type, idx)) {
            continue;
        }

        deferred++;

        // The master pair is masked before it indexes the name table. A
        // deferred tile is in a group, so both fields carry a real tile
        // and neither decode can write one out of range, but the table has
        // two entries and the mask is what keeps that true of the index as
        // well as of the value.
        tiles += " " + std::string(typeName[idx >> 2]) + std::to_string(idx & 0x3)
               + " to master " + std::string(typeName[tile.masterType & 0x1])
               + std::to_string(tile.masterTile & 0x3) + ";";
    }

    if (deferred == 0) {
        return std::string();
    }

    return std::string(typeName[type]) + " global reset deferred "
         + std::to_string(deferred)
         + " tile(s) to a group mastered by the other tile type:" + tiles + "\n";
}

// Re-run one distribution group once with the explicit reset.
//
// Order. The master first and then its edge tiles in ascending tile index
// order, which is the order the normal walk uses. An edge tile has no clock
// until its master has one, so re-cycling an edge ahead of its master would
// repeat the mistake this whole change exists to stop making.
//
// Primitive. XRFdc_Reset and never XRFdc_DynamicPLLConfig. The reconfigure
// performs its own restart only for a tile that is already powered up, and
// a tile that failed to come back is not, so a recovery built on it would
// perform no cycle at all on precisely the tile it exists for.
//
// Counting. One additional cycle per tile the attempt resets, so a tile
// whose recovery fired reads two in the cycle count register rather than
// one, and a reader can tell a tile that took one pass from a tile that
// took two whatever the reset finally returned.
//
// Visibility, which is the point of the whole helper. A recovery that
// succeeded makes the reset return success, which is exactly the shape of a
// reset that never had a problem. Without the counters and this line a
// silent retry would hide the failure rate that later measurements exist to
// establish, and twenty clean reboots could be twenty recoveries. The
// counters say that an attempt happened and what it returned. Neither they
// nor this line are evidence that a recovery repairs anything.
//
// log_->error and not setDiagError. These are two different mechanisms and
// keeping them apart is the whole of this paragraph. setDiagError is what
// makes a transaction report a failure to its caller, and a recovery that
// succeeded has no failure to report, so it is not used here and the
// transaction still completes clean. log_->error only selects the level
// the line is printed at, and printing at that level does not turn a
// recovered reset into a reported failure.
//
// The level is the point. The host side log is filtered by a global level
// whose default admits errors and discards warnings, so a report below it
// does not reach a console at all, and this is the report that has to
// survive the boot where the counter register cannot be read. A recovery
// that fired means a reset failed and had to be retried, which is never a
// routine outcome, so the level is proportionate. The deferral report in
// the global reset branch stays on the warning channel for the opposite
// reason, and the two are deliberately not on one channel.
//
// Built with std::string concatenation and HexValue, matching the rest of
// the message assembly in this file. No <iomanip> and no snprintf, so the
// text is the same under the Yocto build and a host build and contains
// nothing but ASCII. The line names two tiles and one word, so it is well
// under the 960 character console budget and needs no omission counter.
void PyRFdc::recoverClkGroup(uint32_t masterIdx, uint32_t armingIdx) {
    // The two fixed arrays below hold eight, and the length that indexes
    // them is bounded by eight only if the master is one of the eight
    // indices the skip in the group scan can match. A master outside that
    // range makes the skip never fire, lets the scan append eight entries
    // on top of the master and writes one entry past the end of both
    // arrays. Tested here so the bound is a property of this function
    // rather than of its caller's arming predicate.
    //
    // Placed ahead of every write, and that placement is kept: a call that
    // issued nothing must move no published counter, because the armed
    // count is evidence a host reads and an inflated one is evidence of an
    // attempt that did not happen. The refusal itself is not silent. It is
    // announced on the error channel the report at the end of this function
    // uses, because a path that discards a recovery whole must not have to
    // be inferred from the absence of a line, which is the same reason the
    // documented getter's refusal in the constructor has a report of its
    // own. No caller in this file can pass such an index today, so the line
    // is expected never to be emitted, and that is why it costs one line
    // rather than a counter. The line has three format arguments, the refused
    // index and the arming tile among them. The host test build's logging
    // shim declares error with a printf format attribute, so in that build a
    // specifier that does not match its argument is a compiler warning, as it
    // is for the dropped tile count further down. The three are checked in
    // that build only: the rogue header the image is built against carries
    // no such attribute, so the image build checks none of them. Nothing runs
    // it either: no board-free claim executes this line.
    if (masterIdx > 7) {
        log_->error("clock group recovery refused because master index %u named by %s%u"
                    " is out of range; no tile was reset and no recovery counter moved\n",
                    unsigned(masterIdx), ((armingIdx >> 2) & 0x1) ? "DAC" : "ADC",
                    unsigned(armingIdx & 0x3));
        return;
    }

    // The same two names and the same tile numbering the failure report and
    // the deferral line use, so a reader comparing the three is reading one
    // vocabulary.
    static const char* const typeName[2] = {"ADC", "DAC"};
    uint32_t group[8];
    uint32_t groupLen = 0;
    uint32_t dropped = 0;
    uint32_t idx, g;

    // Whether every reset this call actually issued returned success, over
    // the tiles the loop below did not skip. On its own that says nothing
    // about a group whose every member the enable probe refuses: such a
    // call issues no reset at all and this flag is still vacuously true.
    // Nothing below reads it alone for that reason. The success accounting
    // consults the driven count beside it, and so does the outcome word in
    // the report, which gives a pass that drove nothing a value of its own
    // rather than borrowing the word for a pass whose every reset worked.
    //
    // The argument this comment used to make instead was that the case
    // cannot arise, because the arming tile had to be enabled for the sweep
    // to have reached a reset against it and recorded the failure that
    // armed this call. That argument is sound about the vendor driver in
    // front of this code today, and it is not a property of this code: it
    // rests on the enable probe reading static configuration and therefore
    // never changing answer inside one transaction, and a probe that did
    // change answer would make the case reachable with no edit here. The
    // driven count beside this flag is what makes the outcome a property of
    // this code instead, and the success accounting consults both.
    //
    // A disabled tile's refusal still must not be counted against the
    // attempt. Counting it would make the whole recovery unreachable on any
    // board with a partially populated group, which is most of them, which
    // is why the guard below skips such a tile rather than failing it.
    bool attemptOk = true;

    // How many of this group's tiles this call actually drove, and which
    // group slots they were. Both are written only after the enable guard
    // below, so a tile the guard skipped is recorded as skipped by omission
    // rather than by a second test that could come to disagree with the
    // first.
    //
    // Indexed by the group loop's own subscript, the same subscript group[]
    // is indexed by, and deliberately not by tile index. The clear loop
    // further down already walks the group by that subscript, so guarding
    // it from this array forms no new index. Eight entries because the
    // entry test at the top of this function refuses a master index above
    // seven, which is what bounds the length that subscripts both arrays.
    uint32_t drivenCount = 0;
    bool driven[8] = {false, false, false, false, false, false, false, false};

    // The master, then every edge tile that names it, in ascending tile
    // index order. Derived from the cached topology rather than from the
    // walk the sweep used, because the walk holds the tiles this call owns
    // and this holds the tiles of one group.
    group[groupLen++] = masterIdx;

    for (idx = 0; idx < 8; idx++) {
        const TileClkDist &tile = clkDist_[idx >> 2][idx & 0x3];

        if (idx == masterIdx) {
            continue;
        }
        if (tile.role != PYRFDC_CLKDIST_EDGE) {
            continue;
        }
        if (((uint32_t(tile.masterType) * 4) + uint32_t(tile.masterTile)) != masterIdx) {
            continue;
        }

        // Bounded on its own as well as by the entry test above, and the
        // redundancy is deliberate: it keeps both arrays safe from an
        // overrun even if a later edit removes that test on the grounds
        // that the caller's role test already keeps the master in range.
        //
        // What the bound trades the overrun for is not nothing. A tile it
        // turns away is neither reset nor has its diagnostic record
        // cleared, the group size field of the report below would
        // under-report the real group, and the outcome word would still
        // read plausibly. That is why a turned away tile is counted here
        // and announced below rather than silently absorbed.
        if (groupLen < 8) {
            group[groupLen++] = idx;
        } else {
            dropped++;
        }
    }

    // Announced here, after the scan and before the drive loop, because the
    // final count is known as soon as the scan closes, and announcing it
    // before any reset is issued means the truncation is reported even on a
    // pass that then fails partway through the drive loop. One line for the
    // whole truncation rather than one per dropped tile, so it sits far
    // inside the console budget the failure report is held to. The count is
    // a format argument, which the host test build checks through its
    // logging shim's printf format attribute and the image build does not.
    if (dropped > 0) {
        log_->error("clock group recovery truncated at the bound of eight tiles;"
                    " %u tile(s) dropped from the group are neither reset nor cleared\n",
                    unsigned(dropped));
    }

    for (g = 0; g < groupLen; g++) {
        const uint32_t type = group[g] >> 2;
        const uint32_t tile = group[g] & 0x3;

        // The same guard the sweep uses before it touches a tile, and for
        // two reasons rather than one.
        //
        // A disabled tile is not part of this group in any sense the driver
        // recognises, and it answers every call with a non-success. Counting
        // that refusal against the attempt below would make the whole
        // recovery unreachable on any board with a partially populated
        // group, which is most of them.
        //
        // The cycle increment sits behind the same guard as the reset it
        // counts, because that count is published through a register this
        // project reads as evidence of what the driver did, and a cycle
        // recorded for a tile that was never driven is evidence of something
        // that did not happen.
        if (XRFdc_CheckTileEnabled(RFdcInstPtr_, int(type), int(tile)) != XRFDC_SUCCESS) {
            continue;
        }

        driven[g] = true;
        drivenCount++;

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        uint32_t resetStatus = XRFdc_Reset(RFdcInstPtr_, int(type), int(tile));
        if (resetStatus != XRFDC_SUCCESS) {
            attemptOk = false;
        }

        // Counted whether or not the reset returned success, for the reason
        // the sweep's own compensating reset states: the tile was driven at
        // its state machine either way.
        resetCycles_[type][tile]++;
    }

    // Saturating, so a board that somehow armed more than this field can
    // hold reads as out of range rather than wrapping back to a small and
    // plausible number.
    if (recoveriesArmed_ < 0xFFFF) {
        recoveriesArmed_++;
    }

    // Both halves, so an attempt that issued nothing increments the armed
    // counter above and not the succeeded one. The succeeded half of the
    // published word is the only evidence a host has that a recovery ever
    // worked, and a pass that drove no tile has nothing to report as having
    // worked.
    if (attemptOk && (drivenCount > 0)) {
        if (recoveriesSucceeded_ < 0xFFFF) {
            recoveriesSucceeded_++;
        }

        // Clear the failure but keep the evidence. Dropping failed and step
        // is what lets the transaction complete; keeping diagRead and the
        // values captured at the instant of the failure is what lets a
        // message built for some other tile still report this one as
        // observed rather than as failed, which is a distinction the
        // message builder already draws.
        //
        // Scoped to the records this attempt actually addressed. The attempt
        // re-ran one primitive, XRFdc_Reset, so the only record it can
        // honestly clear is a record naming that primitive. A tile whose
        // quadrature settings, mixer settings, PLL reconfigure or event
        // update returned non-success failed at something this attempt did
        // not address, and clearing it would report a reset that left
        // settings unapplied as a clean one.
        //
        // The clear is a different question from the arm. The arming test
        // upstream already decides which tile may arm an attempt, but one
        // group can hold tiles that failed at different steps, so the tile
        // that armed says nothing about what the rest of its group went
        // wrong at.
        for (g = 0; g < groupLen; g++) {
            TileDiag &d = tileDiag_[group[g] >> 2][group[g] & 0x3];

            // Two tests, in this order, asking two different questions.
            //
            // This one asks whether the attempt addressed this tile at all.
            // A tile the enable guard skipped had nothing re-run against
            // it, so there is no re-run for a clear to be honest about.
            //
            // The step-name test below asks whether the attempt addressed
            // the kind of failure the record names. The two come apart on
            // exactly one input, and it is the input that matters: a tile
            // that recorded a reset failure in the sweep and was skipped
            // here failed at the step this attempt would have addressed and
            // was nonetheless not addressed. It passes the test below and
            // fails this one, and clearing it would report a reset that
            // never ran as a clean one.
            if (!driven[g]) {
                continue;
            }

            if (!d.failed || (d.step == nullptr) ||
                (std::strcmp(d.step, "XRFdc_Reset") != 0)) {
                continue;
            }
            d.failed = false;
            d.step   = "";
        }
    }

    log_->error("%s", (std::string("clock group recovery armed by ")
                 + typeName[(armingIdx >> 2) & 0x1] + std::to_string(armingIdx & 0x3)
                 + ", group master " + typeName[(masterIdx >> 2) & 0x1]
                 + std::to_string(masterIdx & 0x3)
                 + ", group size " + std::to_string(groupLen)
                 + ", tiles reset " + std::to_string(drivenCount)
                 + ", outcome " + ((drivenCount == 0) ? "nothing driven"
                                                      : (attemptOk ? "succeeded" : "failed"))
                 + ", armed/succeeded so far " + HexValue(recoveriesArmed_)
                 + "/" + HexValue(recoveriesSucceeded_) + "\n").c_str());
}

// Where the cached topology came from, which IP generation the driver
// reported and how many distribution groups were found, as one word at
// offset 0x12010.
//
// Same shape as the reason register above: a read hands back members and a
// write is refused, and it names RFdcInstPtr_ nowhere, which is what lets
// the dead-driver guard admit it. The IP generation is the load-bearing
// field for a host trying to work out why this driver is behaving oddly,
// because nothing else on the board publishes it.
//
// Deliberately not polled, for the reason the reason register states.
void PyRFdc::ClkDistStatus() {
    // Check for a write
    if (!rdTxn_) {
        errMsg_ = "ClkDistStatus(): read only\n";
    } else {
        // Both fields are one byte wide in this encoding and both are
        // saturated rather than truncated, so an unexpectedly large value
        // reads as out of range instead of wrapping into a plausible one.
        const uint32_t ipType = (ipType_ > 0xFF) ? 0xFF : ipType_;
        const uint32_t groups = (clkDistGroups_ > 0xFF) ? 0xFF : clkDistGroups_;

        data_ = (clkDistSource_ & 0xFF) | (ipType << 8) | (groups << 16);
    }
}

// The per-tile distribution map, as one word at offset 0x12014.
//
// Four bits per tile at tile index type * 4 + tile, so ADC 0 occupies bits
// 3 down to 0 and DAC 3 occupies bits 31 down to 28. A nibble holds 0xF when
// the tile is ungrouped, and otherwise the tile index of that tile's
// distribution master, so a master's own nibble holds its own index.
//
// Read only and not polled, for the same reasons as the register above.
void PyRFdc::ClkDistMap() {
    // Check for a write
    if (!rdTxn_) {
        errMsg_ = "ClkDistMap(): read only\n";
    } else {
        uint32_t map = 0;

        for (uint32_t type = 0; type < 2; type++) {
            for (uint32_t tile = 0; tile < 4; tile++) {
                const uint32_t index = (type * 4) + tile;
                uint32_t nibble = 0xF;

                if (clkDist_[type][tile].role != PYRFDC_CLKDIST_UNGROUPED) {
                    nibble = (uint32_t(clkDist_[type][tile].masterType) * 4) +
                             uint32_t(clkDist_[type][tile].masterTile);
                }

                map |= (nibble & 0xF) << (4 * index);
            }
        }

        data_ = map;
    }
}

// How many IPSM cycles the last global reset issued per tile, as one word at
// offset 0x12018.
//
// Four bits per tile at tile index type * 4 + tile, so ADC 0 occupies bits 3
// down to 0 and DAC 3 occupies bits 31 down to 28. The encoding is part of
// this driver's address space contract and is fixed here. A real count
// saturates one below the reserved value rather than wrapping, so counting
// can never produce the reserved value, and an unexpectedly large count
// reads as out of range rather than as a plausible small one. The top nibble
// value is reserved and is not a count at all: the note at the cap below
// states what it means.
//
// A cycle is counted whether the tile was cycled by an explicit reset or by
// the PLL reconfigure's own internal restart. A count of explicit calls would
// read zero for every tile of a healthy global reset, which is exactly the
// case a host most wants to confirm, so it would publish a number that means
// nothing on the path it exists to describe.
//
// The two global resets are separate transactions and each clears only the
// tiles it covers, so a host that has driven both reads a word describing all
// eight tiles and a host that has driven one reads that type's nibbles.
//
// Same shape as the two registers above: a read hands back members and a
// write is refused, and it names RFdcInstPtr_ nowhere, which is what lets the
// dead-driver guard admit it on read. Deliberately not polled, for the reason
// the reason register states.
void PyRFdc::ResetCycleCount() {
    // Check for a write
    if (!rdTxn_) {
        errMsg_ = "ResetCycleCount(): read only\n";
    } else {
        uint32_t counts = 0;

        for (uint32_t type = 0; type < 2; type++) {
            for (uint32_t tile = 0; tile < 4; tile++) {
                const uint32_t index = (type * 4) + tile;

                // The reserved value for a tile the driver cannot stand a
                // number for, and otherwise a real count capped one below
                // it. The cap moved down by one so counting can never
                // produce the reserved value, and a tile that somehow
                // issued more cycles than the cap reads as the cap rather
                // than wrapping back to a small and plausible number.
                const uint32_t cycles =
                    resetCyclesInexact_[type][tile]
                        ? PYRFDC_RESET_CYCLES_NOT_EXACT
                        : ((resetCycles_[type][tile] > PYRFDC_RESET_CYCLES_MAX_REPORTED)
                               ? PYRFDC_RESET_CYCLES_MAX_REPORTED
                               : resetCycles_[type][tile]);

                counts |= (cycles & 0xF) << (4 * index);
            }
        }

        data_ = counts;
    }
}

// How many clock group recoveries were armed and how many of them succeeded,
// as one word at offset 0x1201C.
//
// Bits 15 down to 0 hold the armed count and bits 31 down to 16 hold the
// succeeded count. The encoding is part of this driver's address space
// contract and is fixed here. Both halves saturate at 0xFFFF rather than
// wrapping, for the reason the status register above states.
//
// Two counts and not one. Never armed, armed and failed, and armed and
// succeeded are three different findings, and a single number collapses two
// of them. A word reading zero says no recovery was ever armed, which is a
// different statement from a recovery that was not needed, and a reader has
// to be able to tell them apart from the cycle count beside this one.
//
// Counted since construction rather than per reset, so a host reading this
// after a boot learns whether anything fired at all.
//
// It says nothing about whether a recovery repairs a converter. It says one
// was attempted and what the driver returned, and that is the whole of it.
//
// Same shape as the three registers above: a read hands back members and a
// write is refused, and it names RFdcInstPtr_ nowhere, which is what lets
// the dead-driver guard admit it on read. It answers zero on an instance
// whose construction never produced a driver, which is the truth: a driver
// that never came up ran no sweep and armed no recovery. Deliberately not
// polled, for the reason the reason register states.
void PyRFdc::RecoveryCount() {
    // Check for a write
    if (!rdTxn_) {
        errMsg_ = "RecoveryCount(): read only\n";
    } else {
        const uint32_t armed =
            (recoveriesArmed_ > 0xFFFF) ? 0xFFFF : recoveriesArmed_;
        const uint32_t succeeded =
            (recoveriesSucceeded_ > 0xFFFF) ? 0xFFFF : recoveriesSucceeded_;

        data_ = (armed & 0xFFFF) | ((succeeded & 0xFFFF) << 16);
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

void PyRFdc::setDiagError(const std::string &msg) {
    errMsg_ = msg;
    errMsgProtected_ = true;
}

bool PyRFdc::rejectIfDriverDead(uint32_t addr) {
    // A live driver leaves here having done one boolean test and nothing
    // else, so every dispatch below behaves exactly as it did.
    if (driverValid_) {
        return false;
    }

    // Admitted by rule rather than by a hand-kept list: exactly the offsets
    // whose bodies never dereference the driver instance. Those are the
    // metal error bypass, the scratchpad and the double test pair, which
    // read and write a member and nothing else, plus the reads of the metal
    // log level and of the initialization failure reason.
    //
    // The metal log level is the one offset admitted in one direction only.
    // Its read hands back a stored boolean, but its write calls into
    // libmetal, which on a dead driver may never have been initialized, so
    // the write is refused.
    if ((addr == 0x12004) || (addr == 0x12008) ||
        ((addr >= 0x13000) && (addr <= 0x13004))) {
        return false;
    }
    //
    // The two clock distribution registers, the per-tile reset cycle count
    // and the recovery counter are admitted on read by that same rule rather
    // than as an exception to it: all four bodies read members and none
    // dereferences the driver instance, and the IP generation a host reads
    // at 0x12010 is exactly what someone trying to find out why this driver
    // is dead needs to see. The cycle count and the recovery counter answer
    // zero on such an instance, which is the truth: a driver that never came
    // up ran no sweep, cycled no tile and armed no recovery.
    if (rdTxn_ && ((addr == 0x12000) || (addr == 0x1200C) ||
                   ((addr >= 0x12010) && (addr <= 0x1201C)))) {
        return false;
    }

    // Rejecting here, before the dispatch chain, is what keeps
    // readTileDiagnostics from ever running on a driver instance that was
    // never initialized: a write to a reset offset is refused before Reset()
    // is entered, so the sweep and the per-tile register reads it performs
    // on a failure are both unreachable on a dead driver.
    //
    // Assign and return, the same shape the two terminal undefined-memory
    // assignments use. A refused register access is an error to report back
    // to the caller, never a reason to raise, to end the process or to take
    // any other route out of this function.
    setDiagError("PyRFdc: driver unusable, " + std::string(InitFailStepName(initFailReason_))
                 + " (" + HexValue(addr) + " rejected)\n");
    return true;
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

     // Initialize as an empty string
     errMsg_.clear();

     // Cleared here and nowhere else, alongside the message it describes, so
     // the flag belongs to one transaction exactly as the message does.
     errMsgProtected_ = false;

    rim::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

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
            // 1st check for the global registers access and commands
            ////////////////////////////////////////////////////////////////
            if (rejectIfDriverDead(addr)) {
                // Refused: this word performs no dispatch at all. The guard
                // has already assigned the error string naming the
                // constructor step that failed.

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

            } else if (addr==0x1200C) {
                InitFailReason();

            } else if (addr==0x12010) {
                ClkDistStatus();

            } else if (addr==0x12014) {
                ClkDistMap();

            } else if (addr==0x12018) {
                ResetCycleCount();

            } else if (addr==0x1201C) {
                RecoveryCount();

            } else if ( (addr >= 0x13000) && (addr <= 0x13004) ) {
                DoubleTestReg(bool((addr>>2)&0x1));

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

            // The bypass exists for the metal-layer noise its name
            // describes. Left clearing unconditionally it would also discard
            // the tile reports and the driver-unusable refusals, which would
            // make setting one published register a way to turn a board that
            // is failing into a board that reports nothing.
            //
            // Asymmetry worth stating rather than leaving to be discovered:
            // this clear runs once per word while both the message and the
            // protection flag are reset once per transaction, so inside one
            // multi-word transaction a protected message set on an early
            // word stays protected for every word after it. That is the safe
            // direction, because over-protecting can only ever preserve a
            // real diagnostic, and the reset paths this reporting covers are
            // single-word transactions in any case.
            //
            // Blast radius: the register defaults to off and nothing in this
            // repository sets it, so this narrows an escape hatch that is
            // inert today rather than changing any observed behavior.
            if (ignoreMetalError_ && !errMsgProtected_) {
                errMsg_.clear();
            }

            // Increment/decrement the counters
            size   -= sizeof(uint32_t);
            addr   += sizeof(uint32_t);
            wrdIdx += sizeof(uint32_t);

        } // while (size > 0)
    } // rim::TransactionLockPtr tlock = tran->lock();

    // Complete transaction without error
    if (errMsg_.empty()) {
        tran->done();

    // Complete transaction with error message
    } else {
        log_->error("%s", errMsg_.c_str());
        tran->errorStr(errMsg_);
    }

}

void PyRFdc::setup_python() {
#ifndef NO_PYTHON
    bp::class_<PyRFdc, PyRFdcPtr, bp::bases<rim::Slave>, boost::noncopyable>(
        "PyRFdc",
        bp::init<>());
    bp::implicitly_convertible<PyRFdcPtr, rim::SlavePtr>();
#endif
}

#ifndef NO_PYTHON
BOOST_PYTHON_MODULE(PyRFdc) {
    PyRFdc::setup_python();
}
#endif
