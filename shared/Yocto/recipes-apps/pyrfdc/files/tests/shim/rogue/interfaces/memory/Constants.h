/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for
 * <rogue/interfaces/memory/Constants.h>. Shadows the real rogue header,
 * which is not installed on a plain development host.
 *
 * Supplies the three transaction-type constants PyRFdc::doTransaction
 * compares against. The numeric values are copied from the rogue v6.15.0
 * header of the same name, read from the pinned conda environment at
 * envs/rogue_v6.15.0/include/rogue/interfaces/memory/Constants.h, so the
 * harness and the target agree on what a write is. The real header also
 * defines Verify, error codes and the TCP bridge probe value, none of which
 * PyRFdc.cpp names.
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

#ifndef __ROGUE_INTERFACES_MEMORY_CONSTANTS_H__
#define __ROGUE_INTERFACES_MEMORY_CONSTANTS_H__

#include <stdint.h>

namespace rogue {
namespace interfaces {
namespace memory {

//! Read transaction. Source: rogue v6.15.0 Constants.h.
static const uint32_t Read = 0x1;

//! Write transaction. Source: rogue v6.15.0 Constants.h.
static const uint32_t Write = 0x2;

//! Posted write transaction. Source: rogue v6.15.0 Constants.h.
static const uint32_t Post = 0x3;

}  // namespace memory
}  // namespace interfaces
}  // namespace rogue

#endif  /* __ROGUE_INTERFACES_MEMORY_CONSTANTS_H__ */
