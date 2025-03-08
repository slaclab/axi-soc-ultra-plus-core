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

//! Create a block, class creator
PyRFdcPtr PyRFdc::create() {
    PyRFdcPtr b = std::make_shared<PyRFdc>();
    return (b);
}

//! Create an block
PyRFdc::PyRFdc() : rim::Slave(4,0x1000) { // Set min=4B and max=4kB
    int i, j, k;
    log_ = rogue::Logging::create("PyRFdc");
    metal_set_log_level(METAL_LOG_ERROR);

#ifndef __BAREMETAL__
    struct metal_device *deviceptr;
#endif
    struct metal_init_params init_param = METAL_INIT_DEFAULTS;

    if (metal_init(&init_param)) {
        log_->error("PyRFdc: Failed to run metal initialization\n");
    }

    XRFdc_Config *ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
    if (ConfigPtr == NULL) {
        log_->error("PyRFdc: RFdc Config Failure\n");
    }

#ifndef __BAREMETAL__
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) != XRFDC_SUCCESS) {
        log_->error("PyRFdc: XRFdc_RegisterMetal() Failure\n");
    }
#endif

    XRFdc_CfgInitialize(RFdcInstPtr_, ConfigPtr);

    // Work around for MaxSampleRate until I figure out how to properly
    //get the ConfigPtr (and/or devicetree) to set this configuration properly
    for(j=0; j<4; j++) {
        RFdcInstPtr_->RFdc_Config.ADCTile_Config[j].MaxSampleRate = 5.9;
        RFdcInstPtr_->RFdc_Config.DACTile_Config[j].MaxSampleRate = 10.0;
    }

    // Init local variables
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
        // Init the MTS configurations
        XRFdc_MultiConverter_Init(&mstConfig_[i], 0, 0, XRFDC_TILE_ID0);
        mstConfig_[i].Tiles = 0;

        // Loop through tile indexes
        for(j=0; j<4; j++) {
            // Init the MTS factor status
            mtsfactor_[i][j] = 0;

            // Get the default Clock source
            XRFdc_GetClockSource(RFdcInstPtr_, i, j, &clkSrcDefault_[i][j]);
            clkSrcConfig_[i][j] = clkSrcDefault_[i][j];

            // Get the default PLL configuration
            XRFdc_GetPLLConfig(RFdcInstPtr_, i, j, &pllDefault_[i][j]);
            pllDefault_[i][j].SampleRate = 1000.0*pllDefault_[i][j].SampleRate; // Convert from GSPS to MSPS
            pllConfig_[i][j] = pllDefault_[i][j];

            // Set the default PLL configuration
            if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_FAILURE) {
                XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
            }

            // Loop through block indexes
            for(k=0; k<4; k++) {

                // Get the default QMC configuration
                if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                    XRFdc_GetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]);
                }
                qmcConfig_[i][j][k] = qmcDefault_[i][j][k];

                // Get the default QMC configuration
                if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                    if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                        XRFdc_GetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]);
                    }
                }
                mixerConfig_[i][j][k] = mixerDefault_[i][j][k];

            }
        }
    }

    log_->debug("PyRFdc::PyRFdc()");
}

//! Destroy a block
PyRFdc::~PyRFdc() {
}

void PyRFdc::StartUp(int Tile_Id) {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        status = XRFdc_StartUp(RFdcInstPtr_, tileType_, Tile_Id);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "StartUp(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::Shutdown(int Tile_Id) {
    int status = XRFDC_SUCCESS;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

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
        status = XRFDC_FAILURE;

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        status = XRFdc_Reset(RFdcInstPtr_, tileType_, Tile_Id);

        // Check for global TYPE reset
        if (Tile_Id<0) {
            // Init the i variable
            i = tileType_;

            // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Init
            XRFdc_MultiConverter_Init(&mstConfig_[i], 0, 0, XRFDC_TILE_ID0);
            mstConfig_[i].Tiles = 0;

            // Loop through tile indexes
            for(j=0; j<4; j++) {
                // Init the MTS factor status
                mtsfactor_[i][j] = 0;

                // Set the default PLL configuration
                if (XRFdc_CheckTileEnabled(RFdcInstPtr_, i, j) != XRFDC_FAILURE) {
                    XRFdc_DynamicPLLConfig(RFdcInstPtr_, i, j, uint8_t(clkSrcDefault_[i][j]), pllDefault_[i][j].RefClkFreq, pllDefault_[i][j].SampleRate);
                }
                clkSrcConfig_[i][j] = clkSrcDefault_[i][j];
                pllConfig_[i][j] = pllDefault_[i][j];

                // Loop through block indexes
                for(k=0; k<4; k++) {

                    // Set the default QMC configuration
                    if (XRFdc_CheckBlockEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                        if (XRFdc_SetQMCSettings(RFdcInstPtr_, i, j, k, &qmcDefault_[i][j][k]) != XRFDC_FAILURE) {
                            XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_QMC);
                        }
                    }
                    qmcConfig_[i][j][k] = qmcDefault_[i][j][k];

                    // Get the default Mixer configuration
                    if (XRFdc_CheckDigitalPathEnabled(RFdcInstPtr_, i, j, k) != XRFDC_FAILURE) {
                        if ((i==0) || (XRFdc_RDReg(RFdcInstPtr_, XRFDC_BLOCK_BASE(i, j, k), XRFDC_DAC_DATAPATH_OFFSET, XRFDC_DATAPATH_MODE_MASK) != XRFDC_DAC_INT_MODE_FULL_BW_BYPASS)) {
                            if (XRFdc_SetMixerSettings(RFdcInstPtr_, i, j, k, &mixerDefault_[i][j][k]) != XRFDC_FAILURE) {
                                XRFdc_UpdateEvent(RFdcInstPtr_, i, j, k, XRFDC_EVENT_MIXER);
                            }
                        }
                    }
                    mixerConfig_[i][j][k] = mixerDefault_[i][j][k];

                }
            }

            // Execute reset again after restoring the settings
            status = XRFdc_Reset(RFdcInstPtr_, tileType_, Tile_Id);
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "Reset(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::CustomStartUp(int Tile_Id) {
    int status = XRFDC_SUCCESS;
    uint32_t StartState = (data_>>0)&0xF;
    uint32_t EndState   = (data_>>8)&0xF;

    // Check if read
    if (rdTxn_) {
        status = XRFDC_FAILURE;

    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        status = XRFdc_CustomStartUp(RFdcInstPtr_, tileType_, Tile_Id, StartState, EndState);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CustomStartUp(" + std::to_string(Tile_Id) + "): failed\n";
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
                data_ = XRFDC_SUCCESS;
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
                data_ = XRFDC_SUCCESS;
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
        data_ = XRFDC_SUCCESS;
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
        data_ = XRFDC_SUCCESS;
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
        data_ = XRFDC_SUCCESS;
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
            data_ = XRFDC_SUCCESS;
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
                data_ = XRFDC_SUCCESS;
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

void PyRFdc::MstEnabled() {
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
        // Return the MST enabled bit mask
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MstEnabled(" + std::to_string(tileType_) + "): failed\n";
    }
}

void PyRFdc::MstRefTile() {
    // Check for a write
    if (!rdTxn_) {

        if (data_ > XRFDC_TILE_ID_MAX) {
            errMsg_ = "MstRefTile(" + std::to_string(tileType_) + "): failed\n";
        } else {
            mstConfig_[tileType_].RefTile = data_;
        }

    // Else Read
    } else {
        data_ = mstConfig_[tileType_].RefTile;

    }
}

void PyRFdc::MstSysrefConfig() {
    // Check if read
    if (rdTxn_) {
        data_ = (XRFdc_ReadReg(RFdcInstPtr_, (XRFDC_DRP_BASE(XRFDC_DAC_TILE, 0) + XRFDC_HSCOM_ADDR), XRFDC_MTS_SRCAP_T1)>>10)&0x1;
    // Else write
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MTS_Sysref_Config
        XRFdc_MTS_Sysref_Config(RFdcInstPtr_,  &mstConfig_[XRFDC_DAC_TILE],  &mstConfig_[XRFDC_ADC_TILE], (data_&0x1));
    }
}

void PyRFdc::MstSysRefEnable() {
    // Check for a write
    if (!rdTxn_) {
        mstConfig_[tileType_].SysRef_Enable = data_;

    // Else Read
    } else {
        data_ = mstConfig_[tileType_].SysRef_Enable;

    }
}

void PyRFdc::MstTargetLatency() {
    // Check for a write
    if (!rdTxn_) {
        // Copy from data_ to mstConfig_[tileType_].Target_Latency
        memcpy(&mstConfig_[tileType_].Target_Latency, &data_, sizeof(int32_t));

    // Else Read
    } else {
        // Copy from mstConfig_[tileType_].Target_Latency to data_
        memcpy(&data_, &mstConfig_[tileType_].Target_Latency, sizeof(uint32_t));
    }
}

void PyRFdc::MstTiles() {
    // Check for a write
    if (!rdTxn_) {
        mstConfig_[tileType_].Tiles = data_;

    // Else Read
    } else {
        data_ = mstConfig_[tileType_].Tiles;
    }
}

void PyRFdc::MstSync() {
    int status, i;

    // Check for a write
    if (!rdTxn_) {

        // Reset status values
        for(i=0; i<4; i++) {
            mtsfactor_[tileType_][i] = 0;
            mstConfig_[tileType_].Latency[i] = 0;
            mstConfig_[tileType_].Offset[i] = 0;
        }

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Sync
        status = XRFdc_MultiConverter_Sync(RFdcInstPtr_, tileType_, &mstConfig_[tileType_]);
        if (status == XRFDC_MTS_OK) {
            for(i=0; i<4; i++) {
                if((1<<i)&mstConfig_[tileType_].Tiles) {
                    if (tileType_ == XRFDC_ADC_TILE) {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
                        XRFdc_GetInterpolationFactor(RFdcInstPtr_, i, 0, &mtsfactor_[tileType_][i]);
                    } else {
                        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
                        XRFdc_GetDecimationFactor(RFdcInstPtr_, i, 0, &mtsfactor_[tileType_][i]);
                    }
                }
            }

        } else {
            errMsg_ = "MstSync(" + std::to_string(tileType_) + "): DAC Multi-Tile-Sync did not complete successfully. Error code (" + std::to_string(status) + ")\n";
        }

    // Else Read
    } else {
        data_ = 1;
    }
}

void PyRFdc::MstLatency(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Copy from mstConfig_[tileType_].Latency[index] to data_
        memcpy(&data_, &mstConfig_[tileType_].Latency[index], sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MstLatency(" + std::to_string(tileType_) + "): failed\n";
    }
}

void PyRFdc::MstOffset(uint8_t index) {
    int status = XRFDC_SUCCESS;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
        // Copy from mstConfig_[tileType_].Offset[index] to data_
        memcpy(&data_, &mstConfig_[tileType_].Offset[index], sizeof(uint32_t));
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "MstOffset(" + std::to_string(index) + "): failed\n";
    }
}

void PyRFdc::MstFactor(uint8_t index) {
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
        errMsg_ = "MstFactor(" + std::to_string(index) + "): failed\n";
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
        status = XRFDC_FAILURE;

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

     // Initialize as an empty string
     errMsg_.clear();

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
            if (addr==0x10000) {
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
                MstEnabled();

            } else if (addr==0x11004) {
                tileType_ = XRFDC_DAC_TILE;
                MstEnabled();

            } else if (addr==0x11008) {
                tileType_ = XRFDC_ADC_TILE;
                MstSync();

            } else if (addr==0x1100C) {
                tileType_ = XRFDC_DAC_TILE;
                MstSync();

            } else if (addr==0x11010) {
                tileType_ = XRFDC_ADC_TILE;
                MstRefTile();

            } else if (addr==0x11014) {
                tileType_ = XRFDC_DAC_TILE;
                MstRefTile();

            } else if (addr==0x11018) {
                tileType_ = XRFDC_ADC_TILE;
                MstSysRefEnable();

            } else if (addr==0x1101C) {
                tileType_ = XRFDC_DAC_TILE;
                MstSysRefEnable();

            } else if (addr==0x11020) {
                tileType_ = XRFDC_ADC_TILE;
                MstTargetLatency();

            } else if (addr==0x11024) {
                tileType_ = XRFDC_DAC_TILE;
                MstTargetLatency();

            } else if (addr==0x11028) {
                tileType_ = XRFDC_ADC_TILE;
                MstTiles();

            } else if (addr==0x1102C) {
                tileType_ = XRFDC_DAC_TILE;
                MstTiles();

            } else if (addr==0x11100) {
                MstSysrefConfig();

            } else if ( (addr >= 0x11200) && (addr <= 0x1120C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MstLatency((addr>>2)&0x3);

            } else if ( (addr >= 0x11210) && (addr <= 0x1121C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MstLatency((addr>>2)&0x3);

            } else if ( (addr >= 0x11220) && (addr <= 0x1122C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MstOffset((addr>>2)&0x3);

            } else if ( (addr >= 0x11230) && (addr <= 0x1123C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MstOffset((addr>>2)&0x3);

            } else if ( (addr >= 0x11240) && (addr <= 0x1124C) ) {
                tileType_ = XRFDC_ADC_TILE;
                MstFactor((addr>>2)&0x3);

            } else if ( (addr >= 0x11250) && (addr <= 0x1125C) ) {
                tileType_ = XRFDC_DAC_TILE;
                MstFactor((addr>>2)&0x3);

            } else if (addr==0x12000) {
                MetalLogLevel();

            } else if (addr==0x12004) {
                IgnoreMetalError();

            } else if (addr==0x12008) {
                ScratchPad();

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

            if (ignoreMetalError_) {
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
        log_->error(errMsg_.c_str());
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
