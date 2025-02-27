/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description:
 * A memory space emulator. Allows user to test a Rogue tree without real hardware.
 * This block will auto allocate memory as needed.
 * ----------------------------------------------------------------------------
 * This file is part of the rogue software platform. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
 *    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of the rogue software platform, including this file, may be
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
    DebugPrint_ = false;
    rdTxn_ = false;
    isADC_ = false;
    tileId_ = 0;
    blockId_ = 0;
    data_ = 0;

    // Set log level
    metal_set_log_level(METAL_LOG_ERROR);
}

//! Destroy a block
PyRFdc::~PyRFdc() {
}

void PyRFdc::DebugPrint() {
    // Check for a write
    if (!rdTxn_) {
        DebugPrint_ = bool(data_&0x1);

        /* Set log level based on debugPrint flag */
        if (DebugPrint_) {
            metal_set_log_level(METAL_LOG_DEBUG);
        } else {
            metal_set_log_level(METAL_LOG_ERROR);
        }

    } else {
        data_ = uint32_t(DebugPrint_);
    }
}

void PyRFdc::NyquistZone() {
    int status = 0;
    uint32_t settings = data_&0x3;

    // Check if write
    if (!rdTxn_) {
        status = XRFdc_SetNyquistZone(RFdcInstPtr_, tileType_, tileId_, blockId_, settings);

    // Else read
    } else {
        status = XRFdc_GetNyquistZone(RFdcInstPtr_, tileType_, tileId_, blockId_, &settings);
        data_ = settings;
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::NyquistZone(): failed\n");
    }
}

void PyRFdc::CalibrationMode() {
    int status = 0;
    uint8_t settings = uint8_t(data_&0x3);

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Only ADC tiles support CalibrationMode
    } else {

        // Check if write
        if (!rdTxn_) {
            status = XRFdc_SetCalibrationMode(RFdcInstPtr_, tileId_, blockId_, settings);

        // Else read
        } else {
            status = XRFdc_GetCalibrationMode(RFdcInstPtr_, tileId_, blockId_, &settings);
            data_ = settings;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::NyquistZone(): failed\n");
    }
}

void PyRFdc::CalFrozen() {
    int status = 0;
    XRFdc_Cal_Freeze_Settings settings;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Only ADC tiles support CalFrozen
    } else {

        // Get the current Cal_Freeze_Settings
        status = XRFdc_GetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            settings.CalFrozen = data_;
            status = XRFdc_SetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Else read
        } else {
            data_ = settings.CalFrozen;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::CalFrozen(): failed\n");
    }
}

void PyRFdc::DisableFreezePin() {
    int status = 0;
    XRFdc_Cal_Freeze_Settings settings;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Only ADC tiles support DisableFreezePin
    } else {

        // Get the current Cal_Freeze_Settings
        status = XRFdc_GetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            settings.DisableFreezePin = data_;
            status = XRFdc_SetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Else read
        } else {
            data_ = settings.DisableFreezePin;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::DisableFreezePin(): failed\n");
    }
}

void PyRFdc::FreezeCalibration() {
    int status = 0;
    XRFdc_Cal_Freeze_Settings settings;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Only ADC tiles support FreezeCalibration
    } else {

        // Get the current Cal_Freeze_Settings
        status = XRFdc_GetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
            settings.FreezeCalibration = data_;
            status = XRFdc_SetCalFreeze(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Else read
        } else {
            data_ = settings.FreezeCalibration;
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::FreezeCalibration(): failed\n");
    }
}

void PyRFdc::ThresholdSettings(uint8_t index) {
    int status = 0;
    XRFdc_Threshold_Settings settings;

    // Check for DAC tile
    if (!isADC_) {
        status = XRFDC_FAILURE;

    // Only ADC tiles support ThresholdSettings
    } else {

        // Get the current Threshold_Settings
        status = XRFdc_GetThresholdSettings(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Check if write
        if (!rdTxn_) {
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
                    break;
            }
            status = XRFdc_SetThresholdSettings(RFdcInstPtr_, tileId_, blockId_, &settings);

        // Else read
        } else {
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
                    break;
            }
        }
    }

    // Check if not successful
    if (status != XRFDC_SUCCESS) {
        data_  = 0xFFFFFFFF;
        log_->error("PyRFdc::ThresholdSettings(): failed\n");
    }
}

//! Post a transaction. Master will call this method with the access attributes.
void PyRFdc::doTransaction(rim::TransactionPtr tran) {
    uint32_t addr = uint32_t(tran->address() & 0xFFFF);
    uint8_t* ptr  = tran->begin();
    uint32_t blockAddr = 0;

    rim::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

        // Check the type of transaction
        if (tran->type() == rim::Write || tran->type() == rim::Post) {
            // Set the flag
            rdTxn_ = false;
            // Copy from ptr to data
            memcpy(&data_, ptr, sizeof(uint32_t));
        } else {
            // Set the flag
            rdTxn_ = true;
        }

        // Check if ADC tiles
        isADC_ = (addr<0x4000) ? true : false;
        tileType_ = isADC_ ? XRFDC_ADC_TILE : XRFDC_DAC_TILE;

        // Get TILE ID
        tileId_ = (addr>>12)&0x3;

        // Get BLOCK ID and block address
        blockId_  = (addr>>10)&0x3;
        blockAddr = addr&0x3FF;

        // Check for the tile region
        if (addr<0x8000) {

            if (blockAddr==0x000) {
                NyquistZone();

            } else if (blockAddr==0x004) {
                CalibrationMode();

            } else if (blockAddr==0x008) {
                CalFrozen();

            } else if (blockAddr==0x00C) {
                DisableFreezePin();

            } else if (blockAddr==0x010) {
                FreezeCalibration();

            } else if ( (blockAddr >= 0x020) && (blockAddr < 0x03F) ) {
                ThresholdSettings((blockAddr>>2)&0x7);
            }

        } else if (addr==0xF000) {
            MstAdcTiles();

        } else if (addr==0xF004) {
            MstDacTiles();

        } else if (addr==0xFFFC) {
            DebugPrint();
        }

        if (rdTxn_) {
            // Copy from data to ptr
            memcpy(ptr, &data_, sizeof(uint32_t));
        }
    }
    tran->done();
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
