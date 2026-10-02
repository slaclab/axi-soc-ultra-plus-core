/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <rogue/GilRelease.h>. Shadows the real
 * rogue header, which is not installed on a plain development host.
 *
 * Deliberately empty apart from the include guard. PyRFdc.cpp includes this
 * header but never names rogue::GilRelease, so an empty shim is the honest
 * stand-in: adding a class body would be surface the production code does not
 * touch. The real header releases the Python global interpreter lock for the
 * scope of an object, which has no meaning in a build made with NO_PYTHON.
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

#ifndef __ROGUE_GIL_RELEASE_H__
#define __ROGUE_GIL_RELEASE_H__

#endif  /* __ROGUE_GIL_RELEASE_H__ */
