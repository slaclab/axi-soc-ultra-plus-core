/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <xrfdc_hw.h>, the register-level header
 * of the AMD RF Data Converter driver. Shadows the real header, which ships
 * with the bare-metal driver and is not installed on a plain development
 * host.
 *
 * Supplies the base-address and offset macros PyRFdc.cpp names. Every value
 * here is consumed by the production code in exactly one way: it is handed
 * to XRFdc_ReadReg, XRFdc_WriteReg, XRFdc_ClrSetReg or XRFdc_RDReg, all of
 * which are harness stubs. So for most of these the numeric value is opaque
 * to the harness and is harness-chosen rather than sourced, which each
 * comment states plainly. Two are not opaque and are called out below.
 *
 * XRFDC_CTRL_STS_BASE and XRFDC_BLOCK_BASE are deliberately functions of
 * every argument they take. A base macro that ignored its tile argument
 * would make a production helper that reads the wrong tile indistinguishable
 * from one that reads the right tile, because both would produce the same
 * recorded call. Keeping the argument dependence is what lets a wrong-tile
 * read show up in the recorded call list.
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

#ifndef __XRFDC_HW_H__
#define __XRFDC_HW_H__

#include "xrfdc.h"

/*
 * Tile control and status base address, as a function of both tile type and
 * tile id. Harness-chosen layout: the real driver's arithmetic is not
 * reproduced, because the production code only passes the result to a stub.
 * What matters, and what is preserved, is that distinct (type, tile) pairs
 * produce distinct bases, so the stub can attribute a read to a tile.
 */
#define XRFDC_CTRL_STS_BASE(type, tile) (0x4000u + ((type) * 0x10000u) + ((tile) * 0x4000u))

/*
 * Per-block base address, a function of tile type, tile id and block id, for
 * the same reason as above. Harness-chosen layout.
 */
#define XRFDC_BLOCK_BASE(type, tile, block) \
    (XRFDC_CTRL_STS_BASE(type, tile) + 0x1000u + ((block) * 0x400u))

/*
 * DRP base and the high-speed common block offset, used together at
 * PyRFdc.cpp:2930 to read the multi-tile sysref capture bit.
 * Harness-chosen layout, for the same reason as above.
 */
#define XRFDC_DRP_BASE(type, tile) (0x6000u + ((type) * 0x10000u) + ((tile) * 0x4000u))
#define XRFDC_HSCOM_ADDR 0x1C00u

/*
 * Offsets and masks the diagnostic and restart bodies pass through to the
 * register stubs. Harness-chosen values; nothing in the production code
 * compares any of them against anything.
 */
#define XRFDC_RESTART_OFFSET 0x0004u
#define XRFDC_RESTART_MASK 0x0001u
#define XRFDC_RESTART_STATE_OFFSET 0x0008u
#define XRFDC_PWR_STATE_MASK 0x000Fu
#define XRFDC_MTS_SRCAP_T1 0x0040u
#define XRFDC_DAC_DATAPATH_OFFSET 0x0034u
#define XRFDC_DATAPATH_MODE_MASK 0x0003u

/*
 * NOT opaque. PyRFdc.cpp compares the XRFdc_RDReg return against this value
 * at lines 214, 338 and 500 to decide whether a DAC interpolation datapath
 * is in full-bandwidth bypass, and takes the mixer branch when it differs.
 * Sourced from the AMD PG269 RF Data Converter driver API reference, where
 * XRFDC_DAC_INT_MODE_FULL_BW_BYPASS is 0x2. No sysroot copy of the real
 * xrfdc_hw.h is reachable on this host to cross-check it against.
 *
 * Load-bearing for the harness in one direction: the XRFdc_RDReg stub
 * returns 0 for an unscripted address, so any non-zero value here makes the
 * mixer branch run. A value of 0 would silently skip it and leave a branch
 * of the reset sweep unexercised.
 */
#define XRFDC_DAC_INT_MODE_FULL_BW_BYPASS 0x2u

/*
 * NOT opaque, and NOT the register the diagnostic reads already take.
 *
 * The clock distribution source lives at offset 0x0080. The register
 * readTileDiagnostics already reads is 0x0084, the clock detector status.
 * Both are per tile, both answer, and only one of them names the tile that
 * sources this tile's clock. Confusing the two is the single most likely
 * way the distribution decode goes silently wrong, which is why the
 * difference is written here rather than left to the reader.
 *
 * The mask keeps the low bit of each two bit field. The production decode
 * shifts the masked value right by two bits per step and compares the
 * result against XRFDC_ENABLED, which is the driver's own decode, so the
 * value of the mask decides which bits that comparison can ever see.
 *
 * Source: xrfdc_hw.h at upstream tag xilinx_v2026.1, the release the
 * installed image was built from. XRFDC_CLOCK_DETECT_OFFSET is defined at
 * line 327 of that header and XRFDC_CLOCK_DETECT_SRC_MASK at line 1907.
 * Unlike the five PG269-only constants above, these two were read out of
 * the upstream header itself.
 */
#define XRFDC_CLOCK_DETECT_OFFSET 0x80U
#define XRFDC_CLOCK_DETECT_SRC_MASK 0x00005555U

/*
 * NOT opaque. The tile common status register carries the tile's power-up
 * status, and the global reset sweep reads it through XRFdc_RDReg with the
 * mask below to decide whether the PLL reconfigure that follows can have
 * performed an IPSM cycle of its own. A wrong offset or a wrong mask makes
 * that decision on the wrong bits, which either fires a compensating reset
 * on a tile that was already cycled or leaves a tile with no cycle at all.
 *
 * readTileDiagnostics already reads this same register at PyRFdc.cpp:807 and
 * again at :3713, both times as the bare literal 0x0228. Those two literals
 * are deliberately left alone: this change adds a named constant for the new
 * call site rather than editing two lines it is not otherwise touching.
 *
 * The shift is declared for completeness of the field definition and is not
 * consumed by the production code, which tests the masked read against zero
 * rather than normalizing it to the field's own value.
 *
 * Source: xrfdc_hw.h at upstream tag xilinx_v2026.1, the release the
 * installed image was built from, in the same reading that produced the two
 * clock detect constants above.
 */
#define XRFDC_STATUS_OFFSET 0x228U
#define XRFDC_PWR_UP_STAT_MASK 0x00000004U
#define XRFDC_PWR_UP_STAT_SHIFT 2U

#endif  /* __XRFDC_HW_H__ */
