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

//! Address of the global ADC reset, the one _Rfdc.py exposes as ResetAllAdc
//! and the one Init() reaches first.
const uint64_t kResetAllAdc = 0x10010;

//! The register offsets the diagnostic read is expected to use, taken from
//! the four existing accessor bodies in PyRFdc.cpp rather than restated from
//! any other source.
const uint32_t kOffsetRestartState = XRFDC_RESTART_STATE_OFFSET;
const uint32_t kOffsetCurrentState = 0x000C;

//! How many times needle occurs in haystack. Overlaps are not counted; no
//! needle used below can overlap itself.
size_t countOf(const std::string &haystack, const std::string &needle) {
    size_t n = 0;
    size_t at = haystack.find(needle);
    while (at != std::string::npos) {
        n++;
        at = haystack.find(needle, at + needle.size());
    }
    return n;
}

//! The eight tile labels in the order the message is required to emit them.
const char *const kTileLabels[8] = {"ADC0", "ADC1", "ADC2", "ADC3",
                                    "DAC0", "DAC1", "DAC2", "DAC3"};

/*
 * True when every tile label present in the message appears in the canonical
 * ADC0..3 then DAC0..3 order, and no label appears twice. Written over
 * whichever labels are present rather than over a fixed expected set, so the
 * same check keeps its meaning as the report widens from the swept type's
 * four tiles to all eight.
 */
bool labelsAreInCanonicalOrder(const std::string &msg, size_t *countOut) {
    size_t previous = 0;
    bool first = true;
    size_t present = 0;

    for (size_t i = 0; i < 8; i++) {
        const std::string label = std::string(" ") + kTileLabels[i] + " ";
        const size_t at = msg.find(label);
        if (at == std::string::npos) continue;
        if (countOf(msg, label) != 1) return false;
        present++;
        if (!first && at < previous) return false;
        previous = at;
        first = false;
    }

    if (countOut != nullptr) *countOut = present;
    return true;
}

/*
 * Second, independent copy of the IPSM state names, transcribed by hand from
 * python/axi_soc_ultra_plus_core/rfsoc_utility/__init__.py, where enumState
 * maps the same sixteen integers to the same sixteen strings and is the enum
 * every host-side CurrentState RemoteVariable is decoded against.
 *
 * The point of the claim that uses it is to catch drift between the two
 * copies: a name edited on one side and not the other turns a host-side
 * comparison against that table into a mismatch that nothing else reports.
 * Transcribed rather than generated from the driver's own table, because
 * generating it from the copy under test would make the two copies one copy
 * and the claim would assert nothing.
 */
const char *const kPythonEnumState[16] = {
    "Device_Power-up_and_Configuration[0]",
    "Device_Power-up_and_Configuration[1]",
    "Device_Power-up_and_Configuration[2]",
    "Power_Supply_Adjustment[0]",
    "Power_Supply_Adjustment[1]",
    "Power_Supply_Adjustment[2]",
    "Clock_Configuration[0]",
    "Clock_Configuration[1]",
    "Clock_Configuration[2]",
    "Clock_Configuration[3]",
    "Clock_Configuration[4]",
    "Converter_Calibration[0]",
    "Converter_Calibration[1]",
    "Converter_Calibration[2]",
    "Wait_for_deassertion_of_AXI4-Stream_reset",
    "Done",
};

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

/* ------------------------------------------------------------------------ */
/* Per-tile accumulation on the global reset path.                           */
/*                                                                           */
/* Every claim below is asserted over what a caller can observe: the string  */
/* handed to Transaction::errorStr, the line handed to Logging::error, and   */
/* the recorded driver call list. None of them reaches into a member of      */
/* PyRFdc, even though including ../PyRFdc.cpp would allow it, so a later    */
/* rework of how the report is stored cannot break a claim that is really    */
/* about what the report says.                                               */
/* ------------------------------------------------------------------------ */

/*
 * Every tile the sweep failed on is named, not only the last one iterated.
 *
 * Today PyRFdc.cpp assigns the sweep's XRFdc_Reset return to one
 * sweep-scoped variable inside the tile loop, so tile 3's result overwrites
 * tile 1's and a failure on tiles 0, 1 or 2 alone leaves no trace at all.
 * Two tiles are scripted to fail here for exactly that reason: a report that
 * names only one of them is the defect.
 */
void checkAccumulatesEveryFailingTile() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 1, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = (countOf(msg, " ADC1 ") == 1) && (countOf(msg, " ADC3 ") == 1);
    if (ok) ok = (msg.find("2 failing tile(s)") != std::string::npos);
    if (ok) ok = (countOf(msg, " ADC0 ") == 0) && (countOf(msg, " ADC2 ") == 0);

    if (!ok) {
        fprintf(stderr, "accumulate: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("accumulates every failing tile", ok);
}

/*
 * The records come out in a specified order, not in the order the failures
 * happened to be discovered or scripted.
 *
 * The failures are scripted tile 3 first and tile 1 second so the selector's
 * own order disagrees with the required output order. A formatter that
 * appended a record per failure as it found it would be stable only by
 * accident of the loop; walking the accumulator in index order is stable by
 * construction, and that is what this pins.
 */
void checkTileOrderIsAdcThenDac() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 1, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    const size_t adc1 = msg.find(" ADC1 ");
    const size_t adc3 = msg.find(" ADC3 ");

    size_t present = 0;
    bool ok = labelsAreInCanonicalOrder(msg, &present);
    if (ok) ok = (adc1 != std::string::npos) && (adc3 != std::string::npos) && (adc1 < adc3);

    if (!ok) {
        fprintf(stderr, "tile order: %zu label(s) present, ADC1 at %zu, ADC3 at %zu, text '%s'\n",
                present, adc1, adc3, msg.c_str());
    }

    runCheck("tile order is ADC0..3 then DAC0..3", ok);
}

/*
 * Two tiles that failed the same way stay two records.
 *
 * Identical captured state on both, so any keying of the accumulator on the
 * captured values rather than on the tile index would collapse them into
 * one, and a reader would conclude one tile failed when two did.
 */
void checkTwoIdenticalFailuresEmitTwoRecords() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    for (uint32_t tile = 1; tile <= 2; tile++) {
        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, tile, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCurrentState, 6);
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetRestartState, 3);
    }

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = (countOf(msg, " ADC1 ") == 1) && (countOf(msg, " ADC2 ") == 1);
    if (ok) ok = (countOf(msg, "state=0x00000006") == 2);
    if (ok) ok = (countOf(msg, "restart=0x00000003") == 2);
    if (ok) ok = (msg.find("2 failing tile(s)") != std::string::npos);

    if (!ok) {
        fprintf(stderr, "two identical failures: text '%s'\n", msg.c_str());
    }

    runCheck("two identical failures emit two records", ok);
}

/*
 * A sweep that failed on nothing is still a clean transaction.
 *
 * The accumulator must not turn a healthy reset into an error, and it must
 * not leak state from a previous reset into this one, which is why the
 * accumulator is cleared at the top of the sweep and not only when the
 * transaction begins.
 */
void checkZeroFailuresLeavesTransactionClean() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);

    bool ok = tran->doneCalled() && !tran->errorStrCalled();
    if (ok) ok = tran->errorStrValue().empty();
    if (ok) ok = gScript.logErrors.empty();

    if (!ok) {
        fprintf(stderr, "zero failures: done=%u err=%u, text '%s'\n",
                tran->doneCalls(), tran->errorStrCalls(), tran->errorStrValue().c_str());
    }

    runCheck("zero failures leaves the transaction clean", ok);
}

/*
 * A tile whose diagnostic read did not run says so, and prints no value.
 *
 * XRFdc_ReadReg hands back a word with no way to report that it could not
 * service the read, so the diagnostic helper gates on the one status-bearing
 * call in the sequence and reports the whole tile as unavailable when the
 * driver refuses it. An earlier register capture taken on this carrier right
 * after a real converter failure read state 0 on all eight tiles, while the
 * PS UART console had named one of them at a different state thirty seconds
 * before, and nothing in the capture told a register that read zero apart
 * from one that was never read at all. A zero printed here must never be
 * able to mean that again.
 */
void checkUnreadDiagnosticsReportUnavailable() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptFailure("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = (countOf(msg, " ADC3 ") == 1);
    if (ok) ok = (msg.find("unavailable") != std::string::npos);
    if (ok) ok = (countOf(msg, "state=") == 0);
    if (ok) ok = (countOf(msg, "clkdet=") == 0);
    // The refused tile performs no control and status reads at all, so an
    // unavailable record cannot be one that read and then discarded.
    if (ok) {
        ok = !gScript.sawCall("XRFdc_ReadReg", XRFDC_ADC_TILE, 3, kOffsetCurrentState);
    }

    if (!ok) {
        fprintf(stderr, "unavailable: text '%s'\n", msg.c_str());
    }

    runCheck("unread diagnostics report unavailable", ok);
}

/*
 * All sixteen decoded names are byte-identical to the Python enumState
 * values, checked one scripted read at a time through the real message path
 * rather than by reaching into the driver's table.
 */
void checkDecodedStateNamesMatchPythonTable() {
    bool ok = true;
    uint32_t firstBad = 0;
    std::string badText;

    for (uint32_t value = 0; value < 16; value++) {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
        gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetCurrentState, value);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string msg = tran->errorStrValue();
        const std::string want = std::string("(") + kPythonEnumState[value] + ")";

        if (countOf(msg, want) != 1) {
            ok = false;
            firstBad = value;
            badText = msg;
            break;
        }
    }

    if (!ok) {
        fprintf(stderr, "decode table: state %u expected '%s', text '%s'\n",
                firstBad, kPythonEnumState[firstBad], badText.c_str());
    }

    runCheck("decoded state names match the python table", ok);
}

/*
 * A raw current-state read with bits above bit 3 set is masked to four bits
 * before it indexes the decode table, and the unmasked value is still
 * printed.
 *
 * The host side declares CurrentState as four bits (_RfdcTile.py), but the
 * read in PyRFdc.cpp is of the whole word, so an out-of-range value would
 * index past a sixteen-entry table. Printing the raw value alongside the
 * name keeps the number readable when the two tables ever disagree.
 */
void checkRawStateAboveFifteenIsMasked() {
    const uint32_t raw = 0x26;

    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetCurrentState, raw);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = (msg.find("state=0x00000026") != std::string::npos);
    if (ok) ok = (countOf(msg, std::string("(") + kPythonEnumState[6] + ")") == 1);

    if (!ok) {
        fprintf(stderr, "masked decode: text '%s'\n", msg.c_str());
    }

    runCheck("raw state above fifteen is masked before decode", ok);
}

}  // namespace

int main(int /*argc*/, char ** /*argv*/) {
    printf("PyRFdc board-free diagnostics\n");

    checkShimCompilesPyRFdc();
    checkScratchpadRoundTrip();
    checkPreChangeGlobalResetMessage();
    checkTileTypeIndices();

    checkAccumulatesEveryFailingTile();
    checkTileOrderIsAdcThenDac();
    checkTwoIdenticalFailuresEmitTwoRecords();
    checkZeroFailuresLeavesTransactionClean();
    checkUnreadDiagnosticsReportUnavailable();
    checkDecodedStateNamesMatchPythonTable();
    checkRawStateAboveFifteenIsMasked();

    printf("RESULT %s\n", (gFailures == 0) ? "PASS" : "FAIL");
    return (gFailures == 0) ? 0 : 1;
}
