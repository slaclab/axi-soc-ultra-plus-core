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
#ifndef __PYTHON_XRFDC_MODULE_H__
#define __PYTHON_XRFDC_MODULE_H__
#include "rogue/Directives.h"

#include <stdint.h>

#include <memory>
#include <thread>
#include <vector>

#include "rogue/interfaces/memory/Slave.h"

#ifndef NO_PYTHON
    #include <boost/python.hpp>
#endif

#ifdef __BAREMETAL__
#include "xparameters.h"
#endif
#include "xrfdc.h"

#define PYRFDC_MAP_TYPE std::map<uint64_t, uint8_t*>

//! Memory interface Emlator device
/** This memory will respond to transactions, emilator hardware by responding to read
 * and write transactions.
 */
class PyRFdc : public rogue::interfaces::memory::Slave {
    // RFdc driver instance
    XRFdc RFdcInst_;
    XRFdc *RFdcInstPtr_ = &RFdcInst_;

    // Map to store 4K address space chunks
    PYRFDC_MAP_TYPE memMap_;

    // Lock
    std::mutex mtx_;

    // Get/Set DebugPrint_
    bool DebugPrint_;
    void SetDebugPrint(bool flag);
    bool GetDebugPrint();

    // Total allocated memory
    uint32_t totAlloc_;
    uint32_t totSize_;

    //! Log
    std::shared_ptr<rogue::Logging> log_;

  public:
    static std::shared_ptr<PyRFdc> create();

    // Setup class for use in python
    static void setup_python();

    // Create a PyRFdc device
    PyRFdc();

    // Destroy the PyRFdc
    ~PyRFdc();

    //! Handle the incoming memory transaction
    void doTransaction(std::shared_ptr<rogue::interfaces::memory::Transaction> transaction);
};

//! Alias for using shared pointer as PyRFdcPtr
typedef std::shared_ptr<PyRFdc> PyRFdcPtr;

#endif
