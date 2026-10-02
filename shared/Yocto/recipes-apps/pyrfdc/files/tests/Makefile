# ----------------------------------------------------------------------------
# Company    : SLAC National Accelerator Laboratory
# ----------------------------------------------------------------------------
# Description:
#     Board-free host build of shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp.
#
#     Compiles the production driver source with no edits to it, against the
#     hand-written shim headers under shim/, and links it to the diagnostics
#     harness in pyRFdcDiagnostics.cpp and the driver stubs in xrfdcStub.cpp.
#     Nothing here is built by the Yocto recipe: pyrfdc.bb names PyRFdc.cpp,
#     PyRFdc.h and CMakeLists.txt one by one with S = ${WORKDIR}, so this
#     subdirectory is never fetched into the work directory.
#
#     `make test` builds the harness and runs it. It exits 0 only when every
#     claim printed PASS and the harness's last line is RESULT PASS. See
#     pyRFdcDiagnostics.cpp for the list of claims and for the limits of what
#     a green run establishes.
#
#     `make test` also reads two files from outside files/: the host model
#     python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py and the reference
#     page docs/reference/pyrfdc_return_codes.rst, for the claim that pins the
#     published register text. The harness finds them from where its own
#     binary sits, six directories below the submodule root, resolved through
#     /proc/self/exe each time it runs, so a copied or moved tree
#     reads its own documents and no path fixed at build time can name another
#     tree. A copy of files/ alone therefore reads that claim red, by design: a
#     claim that could not read its input must not pass.
#
#     Note for a caller that reads the verdict off the last line: invoking
#     this from elsewhere as `make -C <dir> test` makes GNU make print its
#     own entering and leaving directory lines around the harness output, so
#     the leaving line, not RESULT, is last. Use `make -s -C <dir> test`, or
#     read the exit status, or grep for the RESULT line by name. Setting
#     --no-print-directory inside this Makefile does not help: the outer make
#     has already decided to print those lines before it reads this file.
# ----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
# ----------------------------------------------------------------------------

SRC_DIR  := $(abspath ..)
SHIM_DIR := $(abspath shim)

CXX      ?= g++
# -std=c++11 matches ../CMakeLists.txt line 10, so this harness cannot accept
# code the Yocto build would reject. -D__STDC_FORMAT_MACROS matches line 11.
# -DNO_PYTHON keeps the boost.python blocks in PyRFdc.h and PyRFdc.cpp out,
# so no boost or Python stub is needed.
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++11
DEFINES  := -DNO_PYTHON -D__STDC_FORMAT_MACROS
LDFLAGS  ?=

# The shim path MUST come first on the include path. <xrfdc.h>, <xrfdc_hw.h>
# and the rogue/ headers are not installed on a development host, so without
# shim/ ahead of everything else they resolve to nothing and the build fails
# to find them.
INCLUDES := -I$(SHIM_DIR) -I$(SRC_DIR)

BIN  := pyRFdcDiagnostics
OBJS := pyRFdcDiagnostics.o xrfdcStub.o

.PHONY: all test clean

all: $(BIN)

pyRFdcDiagnostics.o: pyRFdcDiagnostics.cpp $(SRC_DIR)/PyRFdc.cpp $(SRC_DIR)/PyRFdc.h
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

xrfdcStub.o: xrfdcStub.cpp
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)

test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(OBJS)
