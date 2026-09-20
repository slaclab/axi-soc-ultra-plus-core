/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Board-free diagnostics harness for
 * shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp.
 *
 * There is one RF Data Converter carrier available to this work and no
 * power-cycle route to it, so every claim about what PyRFdc.cpp reports has
 * to be provable at a desk. This harness is that proof mechanism. It
 * compiles the production source with no edits to it, against the
 * hand-written shim headers under files/tests/shim/, and drives it through
 * its real public entry point, PyRFdc::doTransaction.
 *
 * What it proves:
 *
 *   (a) the shim set is sufficient to compile and link PyRFdc.cpp exactly as
 *       the Yocto recipe ships it, at the same -std=c++11 the real build
 *       uses, so the harness cannot accept code the target build rejects
 *
 *   (b) a write followed by a read of the scratchpad register at 0x12008
 *       round-trips its value through the real address decode, the real
 *       per-word loop and the real completion epilogue, rather than through
 *       a reimplementation of any of them
 *
 *   (c) the message a failing global ADC reset reports today is exactly the
 *       bare prefix, with no failing-tile detail in it. This pins current
 *       behavior so that a later change to that message is a visible diff in
 *       a running test rather than an assertion in prose
 *
 *   (d) the two tile-type constants that the [2][4] shadow arrays in
 *       PyRFdc.h are indexed by hold the values that layout assumes. The
 *       same pair is pinned at compile time by the static_assert in
 *       PyRFdc.cpp, which compiles in the Yocto build as well, so the real
 *       xrfdc.h is held to the same two values by the same line
 *
 * Run it with:
 *
 *   make -C shared/Yocto/recipes-apps/pyrfdc/files/tests test
 *
 * Output is two levels, matching the convention the Python bench tools in
 * software/scripts/ already use: one "  <label>: PASS|FAIL" line per claim,
 * then RESULT PASS or RESULT FAIL as the last line, with exit status 0 or 1.
 * Every claim runs even after an earlier one fails, so one run reports every
 * broken claim rather than only the first.
 *
 * What a PASS here is not: the shim is a second definition of the driver
 * surface and can drift from the real xrfdc.h, so a green run is a claim
 * about the shim's behavior and not automatically about the Yocto build's.
 * The shim's provenance and the limits of that claim are written down in the
 * Shim fidelity section of files/tests/shim/xrfdc.h.
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

#include <cstdio>
#include <cstring>
#include <string>

/*
 * Compile the production source into this translation unit rather than
 * linking it as a separate object. This is deliberate, and it is the same
 * move emulator/driver/tests/prbs_cross_validate.c makes for the kernel PRBS
 * source: including it gives the harness reach into PyRFdc's private members
 * and private methods, so a later check can exercise Reset(-1) and the
 * internal state it leaves behind without adding a test-only public API to
 * PyRFdc.h. Widening the class for the benefit of a test would change the
 * shipped header; this does not change the shipped sources at all.
 */
#include "../PyRFdc.cpp"  // NOLINT(build/include) -- deliberate host compile of the production driver source

#include "xrfdcScript.h"

namespace {

//! Count of claims that reported FAIL. main returns non-zero when non-zero.
int gFailures = 0;

/*
 * Print one claim verdict and keep going. An earlier failing claim must not
 * stop a later one from running, because a run that stops at the first
 * failure reports one broken thing per invocation and this harness is meant
 * to be read once. Mirrors runCheck in software/scripts/captureRfdcState.py.
 */
void runCheck(const char *label, bool outcome) {
    printf("  %s: %s\n", label, outcome ? "PASS" : "FAIL");
    if (!outcome) gFailures++;
}

//! Drive one 32-bit write through the real doTransaction and hand back the
//! transaction, so the caller can read what the epilogue recorded on it.
rim::TransactionPtr driveWrite(PyRFdcPtr device, uint64_t addr, uint32_t value) {
    rim::TransactionPtr tran = rim::Transaction::create(addr, sizeof(uint32_t), rim::Write);
    tran->setWord(0, value);
    device->doTransaction(tran);
    return tran;
}

//! Drive one 32-bit read through the real doTransaction.
rim::TransactionPtr driveRead(PyRFdcPtr device, uint64_t addr) {
    rim::TransactionPtr tran = rim::Transaction::create(addr, sizeof(uint32_t), rim::Read);
    device->doTransaction(tran);
    return tran;
}

/*
 * (a) The shim set compiles and links the production source unchanged.
 *
 * The load-bearing evidence for this claim is a build-time fact: this
 * translation unit included ../PyRFdc.cpp and the link resolved every driver
 * symbol it names, so the binary would not exist otherwise. What is left to
 * check at run time is that the resulting class is actually constructible
 * and reports the access bounds the production constructor asks its base
 * class for, which is the cheapest witness that the object really is the
 * production class and not an empty stand-in.
 */
void checkShimCompilesPyRFdc() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    bool ok = (device != nullptr);
    if (ok) ok = (device->doMinAccess() == 4) && (device->doMaxAccess() == 0x1000);

    runCheck("shim compiles PyRFdc.cpp unchanged", ok);
}

/*
 * (b) A value written to the scratchpad register comes back out of a
 * subsequent read, through the real dispatch chain in both directions.
 *
 * 0x12008 is the scratchpad offset in the global debug block. The pattern is
 * chosen with every byte distinct and both halves non-zero, so a readback
 * that dropped, duplicated or byte-swapped a half cannot look like a pass.
 */
void checkScratchpadRoundTrip() {
    const uint32_t pattern = 0xA5C33C5Au;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr write = driveWrite(device, 0x12008, pattern);
    rim::TransactionPtr read = driveRead(device, 0x12008);

    bool ok = write->doneCalled() && !write->errorStrCalled();
    if (ok) ok = read->doneCalled() && !read->errorStrCalled();
    if (ok) ok = (read->getWord(0) == pattern);

    if (!ok) {
        fprintf(stderr,
                "scratchpad: wrote 0x%08X, read 0x%08X, write done=%u err=%u, "
                "read done=%u err=%u, read error text '%s'\n",
                pattern, read->getWord(0), write->doneCalls(), write->errorStrCalls(),
                read->doneCalls(), read->errorStrCalls(), read->errorStrValue().c_str());
    }

    runCheck("scratchpad round-trips through doTransaction", ok);
}

/*
 * (c) A failing global ADC reset reports the bare prefix and nothing else.
 *
 * The expected text is what PyRFdc.cpp line 372 produces for Tile_Id of -1:
 * the literal "Reset(", the tile id, "): failed" and a newline. It is
 * written out here rather than recomputed, so that a change to that line has
 * to change this literal too and cannot slip through as a test that quietly
 * tracks whatever the code now says.
 *
 * This check pins today's behavior on purpose. The gap it makes visible is
 * that the driver names neither the failing tile nor the reason, even though
 * the reset sweep visited four tiles and only one of them failed.
 *
 * XRFdc_Reset is scripted to fail for ADC tile 3 alone, so a message naming
 * no tile is the interesting outcome rather than the only possible one. The
 * recorded call list is checked as well, because an error string produced
 * without the sweep ever reaching tile 3 would be the right text for the
 * wrong reason.
 */
void checkPreChangeGlobalResetMessage() {
    const std::string expected = "Reset(-1): failed\n";

    PyRFdcPtr device = PyRFdc::create();

    // Cleared after construction, so the recorded list holds only what this
    // one transaction caused.
    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, 0x10010, 1);

    bool sweptEveryTile = true;
    for (uint32_t tile = 0; tile < 4; tile++) {
        if (!gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, tile, XRFDC_SCRIPT_ANY)) {
            sweptEveryTile = false;
        }
    }

    bool ok = sweptEveryTile;
    if (ok) ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = (tran->errorStrValue() == expected);
    if (ok) ok = (gScript.logErrors.size() == 1) && (gScript.logErrors[0] == expected);

    if (!ok) {
        fprintf(stderr,
                "global reset: swept all four tiles=%d, err=%u done=%u, text '%s', "
                "logged %zu error line(s)\n",
                static_cast<int>(sweptEveryTile), tran->errorStrCalls(), tran->doneCalls(),
                tran->errorStrValue().c_str(), gScript.logErrors.size());
    }

    runCheck("pre-change global reset message is the bare prefix", ok);
}

/*
 * (d) The tile-type constants match what the shadow arrays assume.
 *
 * PyRFdc.cpp assigns tileType_ from these two constants at line 3281, and
 * PyRFdc.h lines 72 to 85 index its [2][4] and [2][4][4] shadow arrays with
 * that value, so the ADC constant must be 0 and the DAC constant must be 1
 * or the two groups are silently swapped. A swap does not crash and does not
 * error: it returns one converter's settings under the other's name, which
 * is why this is worth a check of its own.
 *
 * The values are read here rather than restated, so this check follows the
 * shim if the shim changes. The compile-time half of the pin lives in
 * PyRFdc.cpp and is the half that also holds the real xrfdc.h; this half
 * puts the result in the harness output, where a reader sees it, instead of
 * only in a build that did not fail.
 */
void checkTileTypeIndices() {
    const u32 adc = XRFDC_ADC_TILE;
    const u32 dac = XRFDC_DAC_TILE;

    bool ok = (adc == 0u) && (dac == 1u);

    if (!ok) {
        fprintf(stderr, "tile type indices: ADC=%u DAC=%u, expected 0 and 1\n", adc, dac);
    }

    runCheck("tile type indices match the shadow-array layout", ok);
}

}  // namespace

int main(int /*argc*/, char ** /*argv*/) {
    printf("PyRFdc board-free diagnostics\n");

    checkShimCompilesPyRFdc();
    checkScratchpadRoundTrip();
    checkPreChangeGlobalResetMessage();
    checkTileTypeIndices();

    printf("RESULT %s\n", (gFailures == 0) ? "PASS" : "FAIL");
    return (gFailures == 0) ? 0 : 1;
}
