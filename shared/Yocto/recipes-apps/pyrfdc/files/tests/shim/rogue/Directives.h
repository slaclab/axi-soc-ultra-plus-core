/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <rogue/Directives.h>. Shadows the real
 * rogue header, which is not installed on a plain development host, so that
 * PyRFdc.cpp and PyRFdc.h can be compiled unchanged by the harness under
 * files/tests/. Deliberately carries only the one directive the production
 * sources actually depend on: __STDC_FORMAT_MACROS, which enables the PRI*
 * printf macros from <inttypes.h> in C++. The real header additionally sets
 * NumPy, boost.python and CRC++ directives; the harness builds with NO_PYTHON
 * and links none of those libraries, so reproducing them here would add
 * surface with no behavior behind it.
 *
 * Test-build only. Nothing in the Yocto image build reaches this file: the
 * recipe pyrfdc.bb names PyRFdc.cpp, PyRFdc.h and CMakeLists.txt one by one
 * with S = ${WORKDIR}, so files/tests/ is never fetched into the work
 * directory.
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

#ifndef __ROGUE_DIRECTIVES_H__
#define __ROGUE_DIRECTIVES_H__

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#endif  /* __ROGUE_DIRECTIVES_H__ */
