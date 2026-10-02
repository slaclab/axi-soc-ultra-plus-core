/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <xparameters.h>. Shadows the generated
 * AMD board-support header, which exists only inside a bare-metal BSP and is
 * not installed on a plain development host.
 *
 * PyRFdc.h includes this header only inside #ifdef __BAREMETAL__, and the
 * default harness build does not define __BAREMETAL__, so the non-baremetal
 * constructor path is the one that runs and nothing here is reached. The
 * file exists so that a harness built with -D__BAREMETAL__ still resolves
 * its includes rather than failing on a missing header, and it supplies the
 * one symbol that path names, XPAR_XRFDC_0_DEVICE_ID.
 *
 * The device id value is harness-chosen at 0, matching the value the
 * non-baremetal build already uses at PyRFdc.cpp line 49. It is not sourced
 * from any real BSP, because the id a real BSP emits depends on the
 * generated hardware handoff, not on the driver.
 *
 * Test-build only. See shim/rogue/Directives.h for why files/tests/ cannot
 * reach the Yocto image build.
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

#ifndef __XPARAMETERS_H__
#define __XPARAMETERS_H__

//! Harness-chosen, matching the non-baremetal device id at PyRFdc.cpp:49.
#define XPAR_XRFDC_0_DEVICE_ID 0

#endif  /* __XPARAMETERS_H__ */
