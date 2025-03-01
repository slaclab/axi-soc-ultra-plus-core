/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Wrapper on the XRFDC bare metal function class for rogue access
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
PyRFdc::PyRFdc() : rim::Slave(4, 4) { // Set min=max=4 bytes for only 32-bit transactions

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

    // Init local variables
    errMsg_.clear();
    scratchPad_ = 0;
    metalLogLevel_ = false;

    rdTxn_ = false;
    isADC_ = false;
    tileId_ = 0;
    blockId_ = 0;
    data_ = 0;

    mstAdcTiles_ = 0;
    mstDacTiles_ = 0;

    log_->debug("PyRFdc::PyRFdc()");
}

//! Destroy a block
PyRFdc::~PyRFdc() {
}

void PyRFdc::StartUp(int Tile_Id) {
    int status;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        status = XRFdc_StartUp(RFdcInstPtr_, tileType_, Tile_Id);

    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "StartUp(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::Shutdown(int Tile_Id) {
    int status;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        status = XRFdc_Shutdown(RFdcInstPtr_, tileType_, Tile_Id);

    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "Shutdown(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::Reset(int Tile_Id) {
    int status;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        status = XRFdc_Reset(RFdcInstPtr_, tileType_, Tile_Id);

    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "Reset(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::CustomStartUp(int Tile_Id) {
    int status;
    uint32_t StartState = (data_>>0)&0xF;
    uint32_t EndState   = (data_>>8)&0xF;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        status = XRFdc_CustomStartUp(RFdcInstPtr_, tileType_, Tile_Id, StartState, EndState);

    // Else read
    } else {
        status = XRFDC_FAILURE;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "CustomStartUp(" + std::to_string(Tile_Id) + "): failed\n";
    }
}

void PyRFdc::GetIPStatus() {
    int status;
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
    int status;
    XRFdc_BlockStatus BlockStatus;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetBlockStatus
    status = XRFdc_GetBlockStatus(RFdcInstPtr_, tileType_, tileId_, blockId_, &BlockStatus);

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {
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
    int status;
    XRFdc_Mixer_Settings settings;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixerSettings
    status = XRFdc_GetMixerSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
        switch (index) {
            case 0:
                settings.Freq  = RemapDoubleWithUint32(settings.Freq, data_, false);
                break;
            case 1:
                settings.Freq  = RemapDoubleWithUint32(settings.Freq, data_, true);
                break;
            case 2:
                settings.PhaseOffset  = RemapDoubleWithUint32(settings.PhaseOffset, data_, false);
                break;
            case 3:
                settings.PhaseOffset  = RemapDoubleWithUint32(settings.PhaseOffset, data_, true);
                break;
            case 4:
                settings.EventSource = data_;
                break;
            case 5:
                settings.CoarseMixFreq = data_;
                break;
            case 6:
                settings.MixerMode      = uint32_t((data_>>0)  & 0xFF);
                settings.FineMixerScale = uint8_t( (data_>>8)  & 0xFF);
                settings.MixerType      = uint8_t( (data_>>16) & 0xFF);
                break;
            case 7:
                status = XRFDC_FAILURE;
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetMixerSettings
        status = XRFdc_SetMixerSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
        switch (index) {
            case 0:
                data_  = DoubleToUint32(settings.Freq, false);
                break;
            case 1:
                data_  = DoubleToUint32(settings.Freq, true);
                break;
            case 2:
                data_  = DoubleToUint32(settings.PhaseOffset, false);
                break;
            case 3:
                data_  = DoubleToUint32(settings.PhaseOffset, true);
                break;
            case 4:
                data_ = settings.EventSource;
                break;
            case 5:
                data_ = settings.CoarseMixFreq;
                break;
            case 6:
                data_  = uint32_t(settings.MixerMode&0xFF)      <<0;  // BIT7:BIT0
                data_ |= uint32_t(settings.FineMixerScale&0xFF) <<8;  // BIT15:BIT8
                data_ |= uint32_t(settings.MixerType&0xFF)      <<16; // BIT23:BIT16

                break;
            case 7:
                status = XRFDC_FAILURE;
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
    int status;
    XRFdc_QMC_Settings settings;

    // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetQMCSettings
    status = XRFdc_GetQMCSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
        switch (index) {
            case 0:
                settings.EnablePhase = (data_>>0)&0x1;
                settings.EnableGain  = (data_>>1)&0x1;
                break;
            case 1:
                settings.EventSource = data_;
                break;
            case 2:
                settings.GainCorrectionFactor  = RemapDoubleWithUint32(settings.GainCorrectionFactor, data_, false);
                break;
            case 3:
                settings.GainCorrectionFactor  = RemapDoubleWithUint32(settings.GainCorrectionFactor, data_, true);
                break;
            case 4:
                settings.PhaseCorrectionFactor  = RemapDoubleWithUint32(settings.PhaseCorrectionFactor, data_, false);
                break;
            case 5:
                settings.PhaseCorrectionFactor  = RemapDoubleWithUint32(settings.PhaseCorrectionFactor, data_, true);
                break;
            case 6:
                settings.OffsetCorrectionFactor = data_;
                break;
            case 7:
                status = XRFDC_FAILURE;
                break;
            default:
                status = XRFDC_FAILURE;
                break;
        }
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetQMCSettings
        status = XRFdc_SetQMCSettings(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);

    // Else read
    } else {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
        switch (index) {
            case 0:
                data_  = uint32_t(settings.EnablePhase&0x1) <<0; // BIT0
                data_ |= uint32_t(settings.EnableGain &0x1) <<1; // BIT1
                break;
            case 1:
                data_ = settings.EventSource;
                break;
            case 2:
                data_  = DoubleToUint32(settings.GainCorrectionFactor, false);
                break;
            case 3:
                data_  = DoubleToUint32(settings.GainCorrectionFactor, true);
                break;
            case 4:
                data_  = DoubleToUint32(settings.PhaseCorrectionFactor, false);
                break;
            case 5:
                data_  = DoubleToUint32(settings.PhaseCorrectionFactor, true);
                break;
            case 6:
                data_ = settings.OffsetCorrectionFactor;
                break;
            case 7:
                status = XRFDC_FAILURE;
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
    int status;
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
    int status;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_UpdateEvent
        status = XRFdc_UpdateEvent(RFdcInstPtr_, tileType_, tileId_, blockId_, XRFDC_EVENT);

    // Else read
    } else {
        status = XRFDC_FAILURE;
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
    int status;
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
    int status;
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
        errMsg_ = "ThresholdSettings(): failed\n";
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
    int status;

    // Check if write
    if (!rdTxn_) {
        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetNCOPhase
        status = XRFdc_ResetNCOPhase(RFdcInstPtr_, tileType_, tileId_, blockId_);

    // Else read
    } else {
        status = XRFDC_FAILURE;
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
        errMsg_ = "SetupFIFO(): failed\n";
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
        errMsg_ = "SetupFIFOObs(): failed\n";
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
        errMsg_ = "SetupFIFOBoth(): failed\n";
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
    int status;
    uint8_t settings;

    // Check if write
    if (!rdTxn_) {
        status = XRFDC_FAILURE;

    // Else read
    } else {

        // https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFIFOStatus
        status = XRFdc_GetFIFOStatus(RFdcInstPtr_, tileType_, Tile_Id, &settings);
        data_  = uint32_t(settings);
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        errMsg_ = "FIFOStatus(): failed\n";
    }
}

void PyRFdc::FIFOStatusObs() {
    int status;
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
            status = XRFdc_GetFIFOStatusObs(RFdcInstPtr_, tileType_, Tile_Id, &settings);
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
    int status;
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
        errMsg_ = "CalCoefficients(): failed\n";
    }
}

void PyRFdc::CalFreeze(uint32_t calType, uint8_t index) {
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
        errMsg_ = "CalFreeze(): failed\n";
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

















void PyRFdc::MstAdcTiles() {
    int status, i;
    uint32_t factor;

    // Check for a write
    if (!rdTxn_) {
        // Set the MST value
        mstAdcTiles_ = (data_&0xF);

        // ADC MTS Settings
        XRFdc_MultiConverter_Sync_Config ADC_Sync_Config;

        // Run MTS for the ADC
        log_->debug("\n=== Run ADC Sync ===\n");

        // Initialize ADC MTS Settings
        XRFdc_MultiConverter_Init (&ADC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
        ADC_Sync_Config.Tiles = mstAdcTiles_;

        status = XRFdc_MultiConverter_Sync(RFdcInstPtr_, XRFDC_ADC_TILE, &ADC_Sync_Config);
        if(status == XRFDC_MTS_OK){
            log_->debug("ADC Multi-Tile-Sync completed successfully\n");
        } else {
            errMsg_ = "ADC Multi-Tile-Sync did not complete successfully. Error code is (" + std::to_string(status) + ")\n";
            mstAdcTiles_  = 0;
        }

        // Report Overall Latency in T1 (Sample Clocks) and Offsets (in terms of PL words) added to each FIFO
        log_->debug("\n\n=== Multi-Tile Sync Report ===\n");
        for(i=0; i<4; i++) {
            if( (1<<i)&ADC_Sync_Config.Tiles ) {
                XRFdc_GetDecimationFactor(RFdcInstPtr_, i, 0, &factor);
                log_->debug("ADC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, ADC_Sync_Config.Latency[i], factor, ADC_Sync_Config.Offset[i]);
            }
        }

    } else {
        data_ = uint32_t(mstAdcTiles_);
    }
}

void PyRFdc::MstDacTiles() {
   int status, i;
   uint32_t factor;

    // Check for a write
    if (!rdTxn_) {
        // Set the MST value
        mstDacTiles_ = (data_&0xF);

        // DAC MTS Settings
        XRFdc_MultiConverter_Sync_Config DAC_Sync_Config;

        // Run MTS for the DAC
        log_->debug("\n=== Run DAC Sync ===\n");

        // Initialize DAC MTS Settings
        XRFdc_MultiConverter_Init (&DAC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
        DAC_Sync_Config.Tiles = mstDacTiles_;

        status = XRFdc_MultiConverter_Sync(RFdcInstPtr_, XRFDC_DAC_TILE, &DAC_Sync_Config);
        if(status == XRFDC_MTS_OK){
            log_->debug("DAC Multi-Tile-Sync completed successfully\n");
        }else{
            errMsg_ = "DAC Multi-Tile-Sync did not complete successfully. Error code is (" + std::to_string(status) + ")\n";
            mstDacTiles_  = 0;
        }

        // Report Overall Latency in T1 (Sample Clocks) and Offsets (in terms of PL words) added to each FIFO
        log_->debug("\n\n=== Multi-Tile Sync Report ===\n");
        for(i=0; i<4; i++) {
            if((1<<i)&DAC_Sync_Config.Tiles) {
                XRFdc_GetInterpolationFactor(RFdcInstPtr_, i, 0, &factor);
                log_->debug("DAC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, DAC_Sync_Config.Latency[i], factor, DAC_Sync_Config.Offset[i]);
            }
        }

    } else {
        data_ = uint32_t(mstDacTiles_);
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

void PyRFdc::ScratchPad() {
    // Check for a write
    if (!rdTxn_) {
        scratchPad_ = data_;
    } else {
        data_ = scratchPad_;
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
    uint32_t addr = uint32_t(tran->address() & 0xFFFFFFFFULL);
    uint8_t* ptr  = tran->begin();
    uint32_t tileAddr;
    uint32_t blockAddr;
    bool tileOnly;

     // Initialize as an empty string
     errMsg_.clear();

    rim::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

        // Check the type of transaction
        if (tran->type() == rim::Write || tran->type() == rim::Post) {
            // Set the flag
            rdTxn_ = false;
            // Copy from ptr to data
            memcpy(&data_, ptr, tran->size());
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














        } else if (addr==0x20000) {
            MstAdcTiles();

        } else if (addr==0x30000) {
            MstDacTiles();

        } else if (addr==0x40000) {
            MetalLogLevel();

        } else if (addr==0x50000) {
            ScratchPad();

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











                } else {
                    errMsg_ = "Undefined memory";
                }


            ////////////////////////////////////////////////////////////////
            // Else check for the tile + block registers access and commands
            ////////////////////////////////////////////////////////////////
            } else {

                if ( (blockAddr >= 0x000) && (blockAddr <= 0x008) ) {
                    GetBlockStatus((blockAddr>>2)&0x3);

                } else if ( (blockAddr >= 0x020) && (blockAddr <= 0x038) ) {
                    MixerSettings((blockAddr>>2)&0x7);

                } else if (blockAddr==0x03C) {
                    UpdateEvent(XRFDC_EVENT_MIXER);

                } else if ( (blockAddr >= 0x040) && (blockAddr <= 0x058) ) {
                    QMCSettings((blockAddr>>2)&0x7);

                } else if (blockAddr==0x05C) {
                    UpdateEvent(XRFDC_EVENT_QMC);

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











                } else {
                    errMsg_ = "Undefined memory";
                }
            }
        } else {
            errMsg_ = "Undefined memory";
        }

        if (rdTxn_) {
            // Copy from data to ptr
            memcpy(ptr, &data_, sizeof(uint32_t));
        }
    }
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
