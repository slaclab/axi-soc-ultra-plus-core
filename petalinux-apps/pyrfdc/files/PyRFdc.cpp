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

#include <cstring>
#include <memory>
#include <string>
#include <utility>

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

    XRFdc_Config *ConfigPtr;

#ifndef __BAREMETAL__
    struct metal_device *deviceptr;
#endif
    struct metal_init_params init_param = METAL_INIT_DEFAULTS;

    if (metal_init(&init_param)) {
        metal_log(METAL_LOG_ERROR, "PyRFdc: Failed to run metal initialization\n");
    }

    ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
    if (ConfigPtr == NULL) {
        metal_log(METAL_LOG_ERROR, "PyRFdc: RFdc Config Failure\n\r");
    }

#ifndef __BAREMETAL__
    if (XRFdc_RegisterMetal(RFdcInstPtr_, RFDC_DEVICE_ID, &deviceptr) != XRFDC_SUCCESS) {
        metal_log(METAL_LOG_ERROR, "PyRFdc: XRFdc_RegisterMetal() Failure\n\r");
    }
#endif

    XRFdc_CfgInitialize(RFdcInstPtr_, ConfigPtr);

    SetDebugPrint(false);
    totAlloc_ = 0;
    totSize_  = 0;
    log_      = rogue::Logging::create("memory.PyRFdc");
}

//! Destroy a block
PyRFdc::~PyRFdc() {
    PYRFDC_MAP_TYPE::iterator it = memMap_.begin();
    while (it != memMap_.end()) {
        free(it->second);
        ++it;
    }
}

void PyRFdc::SetDebugPrint(bool flag) {
    DebugPrint_ = flag;
   /* Set log level based on debugPrint flag */
   if (DebugPrint_) {
      metal_set_log_level(METAL_LOG_DEBUG);
   } else {
      metal_set_log_level(METAL_LOG_ERROR);
   }
}

bool PyRFdc::GetDebugPrint() {
    return DebugPrint_;
}

//! Post a transaction. Master will call this method with the access attributes.
void PyRFdc::doTransaction(rim::TransactionPtr tran) {
    uint64_t addr4k;
    uint64_t off4k;
    uint64_t size4k;
    uint32_t size = tran->size();
    uint32_t type = tran->type();
    uint64_t addr = tran->address();
    uint8_t* ptr  = tran->begin();

    // printf("Got transaction address=0x%" PRIx64 ", size=%" PRIu32 ", type = %" PRIu32 "\n", addr, size, type);

    rogue::interfaces::memory::TransactionLockPtr tlock = tran->lock();
    {
        std::lock_guard<std::mutex> lock(mtx_);

        while (size > 0) {
            addr4k = (addr / 0x1000) * 0x1000;
            off4k  = addr % 0x1000;
            size4k = (addr4k + 0x1000) - (addr4k + off4k);

            if (size4k > size) size4k = size;
            size -= size4k;
            addr += size4k;

            if (memMap_.find(addr4k) == memMap_.end()) {
                memMap_.insert(std::make_pair(addr4k, reinterpret_cast<uint8_t*>(malloc(0x1000))));
                totSize_ += 0x1000;
                totAlloc_++;
                log_->debug("Allocating block at 0x%x. Total Blocks %i, Total Size = %i", addr4k, totAlloc_, totSize_);
            }

            // Write or post
            if (tran->type() == rogue::interfaces::memory::Write || tran->type() == rogue::interfaces::memory::Post) {
                // printf("Write data to 4k=0x%" PRIx64 ", offset=0x%" PRIx64 ", size=%" PRIu64 "\n", addr4k, off4k,
                // size4k);
                memcpy(memMap_[addr4k] + off4k, ptr, size4k);

                // Read or verify
            } else {
                // printf("Read data from 4k=0x%" PRIx64 ", offset=0x%" PRIx64 ", size=%" PRIu64 "\n", addr4k, off4k,
                // size4k);
                memcpy(ptr, memMap_[addr4k] + off4k, size4k);
            }

            ptr += size4k;
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
