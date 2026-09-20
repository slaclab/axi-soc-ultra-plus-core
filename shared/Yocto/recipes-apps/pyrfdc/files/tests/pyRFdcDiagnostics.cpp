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
 *   (c) the message a failing global ADC reset reports is exactly one line,
 *       pinned byte for byte, so any later change to that message is a
 *       visible diff in a running test rather than an assertion in prose
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

//! Count of claims that reported anything at all, pass or fail. A suite that
//! silently stopped running claims would otherwise print RESULT PASS.
int gChecks = 0;

/*
 * Print one claim verdict and keep going. An earlier failing claim must not
 * stop a later one from running, because a run that stops at the first
 * failure reports one broken thing per invocation and this harness is meant
 * to be read once. Mirrors runCheck in software/scripts/captureRfdcState.py.
 */
void runCheck(const char *label, bool outcome) {
    printf("  %s: %s\n", label, outcome ? "PASS" : "FAIL");
    gChecks++;
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
const uint32_t kOffsetClockDetector = 0x0084;

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

//! How many recorded calls carry this name against this tile type, whatever
//! the tile id and the fourth field. Asserting on the recorded list rather
//! than on the message matters for the claim that no reset is issued against
//! the second tile type: a message that merely omits the word reset would
//! not prove that no reset was issued.
size_t countCallsForType(const std::string &name, uint32_t type) {
    const std::string prefix = name + "/" + std::to_string(type) + "/";
    size_t n = 0;

    for (size_t i = 0; i < gScript.calls.size(); i++) {
        if (gScript.calls[i].compare(0, prefix.size(), prefix) == 0) n++;
    }
    return n;
}

/*
 * One tile's record, from its label up to the semicolon that closes it, or
 * the empty string when the message carries no record for that tile.
 *
 * Claims about one tile are asserted over its own record rather than over
 * the whole message. A claim that counted a field across the whole line
 * would mean one thing while the message carried the swept type's four
 * tiles and something else once it carried all eight, and the two mutation
 * runs below need claims whose meaning does not move with the width of the
 * report.
 */
std::string recordFor(const std::string &msg, const char *label) {
    const std::string opener = std::string(" ") + label + " ";
    const size_t at = msg.find(opener);
    if (at == std::string::npos) return "";

    const size_t end = msg.find(';', at);
    if (end == std::string::npos) return msg.substr(at);
    return msg.substr(at, end - at);
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
 * (c) A failing global ADC reset reports one exact line, byte for byte.
 *
 * The expected text is written out in full rather than recomputed, so a
 * change to the message has to change this literal too and cannot slip
 * through as a test that quietly tracks whatever the code now says. This is
 * the same pin this file opened with, carried forward onto the message the
 * driver reports now. Its earlier form asserted the bare prefix
 * "Reset(-1): failed\n", which named neither the failing tile nor the
 * reason. Both halves of the report are asserted separately, the string the
 * caller sees and the single line the console sees, because they reach
 * different readers and only one of them survives into a python traceback.
 *
 * XRFdc_Reset is scripted to fail for ADC tile 3 alone, so a message naming
 * the wrong tile is the interesting outcome rather than the only possible
 * one. The recorded call list is checked as well, because an error string
 * produced without the sweep ever reaching tile 3 would be the right text
 * for the wrong reason.
 */
void checkGlobalResetMessageIsPinnedByteForByte() {
    const std::string zeros =
        " ok state=0x0(Device_Power-up_and_Configuration[0])"
        " restart=0x0 clkdet=0x0 common=0x0 plllock=0x0;";
    const std::string expected =
        "Reset(-1): failed, 1 failing tile(s):"
        " ADC0" + zeros +
        " ADC1" + zeros +
        " ADC2" + zeros +
        " ADC3 XRFdc_Reset state=0x6(Clock_Configuration[0])"
        " restart=0x3 clkdet=0x0 common=0x0 plllock=0x0;"
        " DAC0 ok state=0xF(Done)"
        " restart=0x0 clkdet=0x4 common=0x0 plllock=0x0;"
        " DAC1" + zeros +
        " DAC2" + zeros +
        " DAC3" + zeros +
        "\n";

    PyRFdcPtr device = PyRFdc::create();

    // Cleared after construction, so the recorded list holds only what this
    // one transaction caused.
    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    // Scripted readings so the pinned line is not a wall of zeros that an
    // all-zero record of any other tile would satisfy just as well. The
    // failing tile sits at the clock-configuration state this project keeps
    // seeing on the console, mid-restart, and the tile it takes its clock
    // from is somewhere else entirely, which is the whole reason the report
    // carries both groups.
    gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetCurrentState, 6);
    gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetRestartState, 3);
    gScript.scriptRegister(XRFDC_DAC_TILE, 0, kOffsetCurrentState, 15);
    gScript.scriptRegister(XRFDC_DAC_TILE, 0, kOffsetClockDetector, 4);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);

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

    runCheck("global reset message is pinned byte for byte", ok);
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
    if (ok) ok = (msg.find("2 failing tile(s)") != std::string::npos);
    // Both failing tiles carry a record, and each names the driver call
    // that returned non-success rather than being listed as merely seen.
    if (ok) ok = (recordFor(msg, "ADC1").find("XRFdc_Reset") != std::string::npos);
    if (ok) ok = (recordFor(msg, "ADC3").find("XRFdc_Reset") != std::string::npos);
    // The tiles that did not fail are not reported as having failed.
    // Whether they appear at all is a separate claim; this one is only
    // about which tiles the sweep blamed.
    if (ok) ok = (countOf(msg, " ADC0 XRFdc_Reset") == 0);
    if (ok) ok = (countOf(msg, " ADC2 XRFdc_Reset") == 0);

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
    if (ok) ok = (countOf(msg, "state=0x6(") == 2);
    if (ok) ok = (countOf(msg, "restart=0x3 ") == 2);
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

    const std::string record = recordFor(msg, "ADC3");

    bool ok = (countOf(msg, " ADC3 ") == 1);
    if (ok) ok = (record.find("unavailable") != std::string::npos);
    if (ok) ok = (record.find("state=") == std::string::npos);
    if (ok) ok = (record.find("clkdet=") == std::string::npos);
    if (ok) ok = (record.find("plllock=") == std::string::npos);
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
        const std::string record = recordFor(msg, "ADC3");
        const std::string want = std::string("(") + kPythonEnumState[value] + ")";

        // Asserted over the scripted tile's own record, because the other
        // tiles read zero and would otherwise satisfy the state-0 pass on
        // their own.
        if (countOf(record, want) != 1) {
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

    const std::string record = recordFor(msg, "ADC3");

    bool ok = (record.find("state=0x26(") != std::string::npos);
    if (ok) ok = (countOf(record, std::string("(") + kPythonEnumState[6] + ")") == 1);

    if (!ok) {
        fprintf(stderr, "masked decode: text '%s'\n", msg.c_str());
    }

    runCheck("raw state above fifteen is masked before decode", ok);
}

/* ------------------------------------------------------------------------ */
/* Widening the report from the swept type's four tiles to all eight.        */
/*                                                                           */
/* _Rfdc.py's Init() calls the ADC reset and then the DAC reset. When the    */
/* ADC call raises, the DAC reset is never reached, so DAC tile 0 is never   */
/* examined at all. On this carrier that is the tile that matters: the IP    */
/* configuration sets ADC3_Clock_Source to 4, which is DAC tile 0, and       */
/* DAC0_Clock_Dist to 1, which makes DAC tile 0 the sole clock distribution  */
/* master, and RfDataConverter.vhd wires adc0, adc1, adc2 and dac0 clock     */
/* inputs into the IP core and leaves the fourth ADC clock input             */
/* unconnected. The tile that hangs is the tile with no clock of its own,    */
/* and the state of the tile it takes its clock from is not readable after   */
/* the fact, because the host register path has already begun degrading.     */
/* ------------------------------------------------------------------------ */

/*
 * Any global reset failure carries all eight tiles, not only the four of
 * the type that was swept.
 */
void checkReportsAllEightTiles() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    size_t present = 0;
    bool ok = labelsAreInCanonicalOrder(msg, &present);
    if (ok) ok = (present == 8);
    if (ok) ok = (countOf(msg, "1 failing tile(s)") == 1);
    // Eight records, each carrying a state field, and only one of them
    // naming a step. A widening that filled the other four with the
    // unavailable marker would satisfy a bare label count but says nothing.
    if (ok) ok = (countOf(msg, "state=") == 8);
    if (ok) ok = (countOf(msg, "XRFdc_Reset") == 1);
    if (ok) ok = (countOf(msg, "unavailable") == 0);

    if (!ok) {
        fprintf(stderr, "eight tiles: %zu label(s) present, text '%s'\n", present, msg.c_str());
    }

    runCheck("reports all eight tiles on a global reset failure", ok);
}

/*
 * The second tile type is read and nothing more.
 *
 * This is the load-bearing claim of the widening. It is asserted on the
 * recorded driver call list rather than on the message, because a message
 * that merely omits the word reset would not prove that no reset, no PLL
 * reconfigure and no event update was issued against the group that was not
 * swept. Resetting the clock distribution master to find out what it was
 * doing would destroy the thing being measured.
 */
void checkOtherTypeIsDiagnosedButNotReset() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    driveWrite(device, kResetAllAdc, 1);

    bool ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 0);
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_DAC_TILE) == 0);
    if (ok) ok = (countCallsForType("XRFdc_UpdateEvent", XRFDC_DAC_TILE) == 0);
    if (ok) ok = (countCallsForType("XRFdc_StartUp", XRFDC_DAC_TILE) == 0);
    if (ok) ok = (countCallsForType("XRFdc_Shutdown", XRFDC_DAC_TILE) == 0);
    // Nothing is written to the second type either, so the read cannot be a
    // read-modify-write wearing a diagnostic's name.
    if (ok) ok = (countCallsForType("XRFdc_WriteReg", XRFDC_DAC_TILE) == 0);
    if (ok) ok = (countCallsForType("XRFdc_ClrSetReg", XRFDC_DAC_TILE) == 0);
    // And it really was read: four control and status reads per DAC tile.
    if (ok) ok = (countCallsForType("XRFdc_ReadReg", XRFDC_DAC_TILE) == 16);
    if (ok) {
        ok = gScript.sawCall("XRFdc_ReadReg", XRFDC_DAC_TILE, 0, kOffsetCurrentState);
    }
    // The sweep's own four resets against the ADC group are unchanged.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 4);

    if (!ok) {
        fprintf(stderr,
                "other type: DAC reset=%zu pll=%zu event=%zu write=%zu clrset=%zu read=%zu, "
                "ADC reset=%zu\n",
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_UpdateEvent", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_WriteReg", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_ClrSetReg", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_ReadReg", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE));
    }

    runCheck("the other type is diagnosed but not reset", ok);
}

/*
 * Widening the report did not turn a healthy reset into an error, and a
 * healthy reset pays nothing for the widening.
 */
void checkCleanSweepStillReportsNothing() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);

    bool ok = tran->doneCalled() && !tran->errorStrCalled();
    if (ok) ok = tran->errorStrValue().empty();
    // No diagnostic read at all on either type, so the extra reads are paid
    // for only on a path that is already inside a driver error.
    if (ok) ok = (countCallsForType("XRFdc_ReadReg", XRFDC_DAC_TILE) == 0);
    if (ok) {
        ok = !gScript.sawCall("XRFdc_ReadReg", XRFDC_ADC_TILE, 3, kOffsetCurrentState);
    }

    if (!ok) {
        fprintf(stderr, "clean sweep: done=%u err=%u, DAC reads=%zu, text '%s'\n",
                tran->doneCalls(), tran->errorStrCalls(),
                countCallsForType("XRFdc_ReadReg", XRFDC_DAC_TILE),
                tran->errorStrValue().c_str());
    }

    runCheck("a clean sweep on one type still reports nothing", ok);
}

/*
 * The reported line never exceeds what the console can print, and says so
 * when it runs out of room.
 *
 * The completion epilogue hands the same string to Transaction::errorStr,
 * where a std::string of any length survives, and to Logging::error, where
 * it does not: rogue v6.15.0's Logging::intLog formats into a stack buffer
 * with vsnprintf and a size argument of 1000, so anything past 999
 * characters never reaches the PS UART. That is the console this project
 * reads the bare-metal converter diagnostics off, so a report that silently
 * loses its tail there loses exactly the thing this work exists to make
 * visible.
 *
 * Driven at the widest the message can get: every tile scripted into the
 * longest-named state with every readable register at all ones, and all
 * four tiles of the swept type failing, so the step name is the long one
 * too. The omission marker is reached on this input, which is the point.
 * A run that never reached it would leave that branch unexercised.
 */
void checkReportedLineFitsTheConsoleBuffer() {
    // The one the real console truncates at, minus the terminator.
    const size_t consoleLimit = 999;

    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    for (uint32_t type = 0; type < 2; type++) {
        for (uint32_t tile = 0; tile < 4; tile++) {
            // Low four bits 0xE select the longest name in the table, and
            // the high bits keep the printed raw value at its full width.
            gScript.scriptRegister(type, tile, kOffsetCurrentState, 0xFFFFFFFEu);
            gScript.scriptRegister(type, tile, kOffsetRestartState, 0xFFFFFFFFu);
            gScript.scriptRegister(type, tile, kOffsetClockDetector, 0xFFFFFFFFu);
            gScript.scriptRegister(type, tile, 0x0228, 0xFFFFFFFFu);
        }
    }
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    size_t present = 0;
    bool ok = labelsAreInCanonicalOrder(msg, &present);
    if (ok) ok = (msg.size() <= consoleLimit);
    // Nothing is dropped without saying so: what was printed plus what the
    // marker admits to omitting has to account for all eight tiles.
    if (ok) {
        const size_t omitted = 8 - present;
        const std::string marker =
            " +" + std::to_string(omitted) + " tile(s) omitted, line budget reached;";
        ok = (omitted == 0) ? (countOf(msg, "omitted") == 0)
                            : (countOf(msg, marker) == 1);
    }
    // The console's own copy is the one at risk, and it is the same string.
    if (ok) ok = (gScript.logErrors.size() == 1) && (gScript.logErrors[0] == msg);
    // Whatever else was dropped, the four tiles that actually failed are
    // first in index order and survive.
    if (ok) ok = (countOf(msg, "4 failing tile(s)") == 1);
    if (ok) ok = (countOf(msg, "XRFdc_Reset") == 4);

    if (!ok) {
        fprintf(stderr, "console budget: %zu chars, %zu label(s) present, text '%s'\n",
                msg.size(), present, msg.c_str());
    }

    runCheck("the reported line fits the console buffer", ok);
}

/* ------------------------------------------------------------------------ */
/* Attributing a sweep failure to the call that produced it.                 */
/*                                                                           */
/* Naming the tile says which converter went wrong. It does not say where    */
/* in the sequence it went wrong, and the sweep performs five distinct       */
/* driver calls per tile before the reset whose return the report has        */
/* carried so far. One of them, the per-tile PLL reconfigure, is the call    */
/* the root-cause hypothesis blames, and its return value was discarded      */
/* outright, so a failure there reported nothing at all on any board.        */
/* ------------------------------------------------------------------------ */

//! Position of the first recorded call with this exact name and index
//! tuple, or the length of the list when it was never recorded. The length
//! rather than a sentinel, so a call that never happened sorts after every
//! call that did and an ordering comparison stays honest without a second
//! presence test beside it.
size_t firstCallAt(const char *name, uint32_t type, uint32_t tile, uint32_t block) {
    const std::string want = XRFdcScript::describe(name, type, tile, block);

    for (size_t i = 0; i < gScript.calls.size(); i++) {
        if (gScript.calls[i] == want) return i;
    }
    return gScript.calls.size();
}

//! The tile the step-attribution sub-checks script their failure on.
//! Deliberately not tile 3, the tile this project keeps seeing fail on the
//! carrier, so none of them can pass because of something done for that
//! tile in particular.
const uint32_t kStepTile = 2;

//! The label of the tile record kStepTile produces on the ADC group.
const char *const kStepLabel = "ADC2";

/*
 * One printed line per call site, all sharing a prefix so the six can be
 * counted as a group. One aggregate verdict over all six would go red for a
 * single unattributed site and say nothing about which, and the whole point
 * of this claim is that each site is attributed on its own.
 */
void runStepCheck(const char *site, bool ok) {
    const std::string label =
        std::string("every sweep step is attributed by name [") + site + "]";

    runCheck(label.c_str(), ok);
}

/*
 * Each of the six driver calls the sweep makes before its final reset is
 * attributed to its own name when it is the call that failed.
 *
 * Two pairs of these sites are the same driver function reached twice, and
 * each pair needs a discriminator or a claim about one member would be
 * satisfied by the other:
 *
 *   the two XRFdc_Reset sites take identical arguments, so no selector can
 *   separate them and the recorded order is the only discriminator there
 *   is: the first runs before the tile's PLL reconfigure and the second
 *   runs after every block of every tile has been visited
 *
 *   the two XRFdc_UpdateEvent sites differ only in the event they raise,
 *   which the fixture's detail selector can key on, so each is scripted to
 *   fail on its own event and the recorded order is asserted as well
 */
void checkSweepStepsAreAttributedByName() {
    /* Site 1: the first XRFdc_Reset, guarded on the tile's PLL being
     * enabled, which is why this one sub-check has to script that field
     * before the device is constructed. */
    {
        gScript.reset();
        gScript.pllEnabled = 1;
        PyRFdcPtr device = PyRFdc::create();

        // Only the recorded and scripted state is cleared here, not
        // pllEnabled, which the constructor above has already consumed.
        gScript.calls.clear();
        gScript.logErrors.clear();
        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, kStepTile, XRFDC_SCRIPT_ANY,
                              XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        bool ok = (record.find("XRFdc_Reset") != std::string::npos);
        // Attributed at this site and not only at the later one: the tile's
        // diagnostics were taken before its PLL reconfigure ran.
        if (ok) {
            ok = firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY) <
                 firstCallAt("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY);
        }

        if (!ok) {
            fprintf(stderr, "step site 1: record '%s', diag at %zu, reconfigure at %zu\n",
                    record.c_str(),
                    firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                                XRFDC_SCRIPT_ANY),
                    firstCallAt("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, kStepTile,
                                XRFDC_SCRIPT_ANY));
        }

        runStepCheck("XRFdc_Reset before the PLL reconfigure", ok);
    }

    /* Site 2: the per-tile PLL reconfigure. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, kStepTile,
                              XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        const bool ok = (record.find("XRFdc_DynamicPLLConfig") != std::string::npos);

        if (!ok) fprintf(stderr, "step site 2: record '%s'\n", record.c_str());

        runStepCheck("XRFdc_DynamicPLLConfig", ok);
    }

    /* Site 3: the quadrature settings write. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_SetQMCSettings", XRFDC_ADC_TILE, kStepTile,
                              XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        const bool ok = (record.find("XRFdc_SetQMCSettings") != std::string::npos);

        if (!ok) fprintf(stderr, "step site 3: record '%s'\n", record.c_str());

        runStepCheck("XRFdc_SetQMCSettings", ok);
    }

    /* Site 4: the event update that follows the quadrature write. Scripted
     * on the quadrature event alone, so the mixer event site below cannot
     * be what produced the attribution. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailureDetail("XRFdc_UpdateEvent", XRFDC_ADC_TILE, kStepTile,
                                    XRFDC_SCRIPT_ANY, XRFDC_EVENT_QMC, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        bool ok = (record.find("XRFdc_UpdateEvent") != std::string::npos);
        // Taken before the tile's first mixer settings write, which is
        // where the other event site could only have been reached from.
        if (ok) {
            ok = firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY) <
                 firstCallAt("XRFdc_SetMixerSettings", XRFDC_ADC_TILE, kStepTile, 0);
        }

        if (!ok) fprintf(stderr, "step site 4: record '%s'\n", record.c_str());

        runStepCheck("XRFdc_UpdateEvent for the quadrature event", ok);
    }

    /* Site 5: the mixer settings write. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_SetMixerSettings", XRFDC_ADC_TILE, kStepTile,
                              XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        const bool ok = (record.find("XRFdc_SetMixerSettings") != std::string::npos);

        if (!ok) fprintf(stderr, "step site 5: record '%s'\n", record.c_str());

        runStepCheck("XRFdc_SetMixerSettings", ok);
    }

    /* Site 6: the event update that follows the mixer write. Scripted on
     * the mixer event alone, and required to have been attributed after the
     * mixer settings write, so the quadrature event site cannot be what
     * produced it. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailureDetail("XRFdc_UpdateEvent", XRFDC_ADC_TILE, kStepTile,
                                    XRFDC_SCRIPT_ANY, XRFDC_EVENT_MIXER, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        bool ok = (record.find("XRFdc_UpdateEvent") != std::string::npos);
        if (ok) {
            ok = firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY) >
                 firstCallAt("XRFdc_SetMixerSettings", XRFDC_ADC_TILE, kStepTile, 0);
        }

        if (!ok) fprintf(stderr, "step site 6: record '%s'\n", record.c_str());

        runStepCheck("XRFdc_UpdateEvent for the mixer event", ok);
    }
}

/*
 * A failure of the per-tile PLL reconfigure reaches the report.
 *
 * Asserted apart from the loop above because this is the site the next
 * phase's hypothesis turns on: XRFdc_DynamicPLLConfig is called once per
 * tile with the tile's own default clock source, reference frequency and
 * sample rate, and its return value was thrown away, so a failure there was
 * invisible on every board running this driver rather than only on a board
 * with clock distribution.
 */
void checkDiscardedPllReconfigureFailureIsReported() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = !msg.empty();
    if (ok) ok = (countOf(msg, "1 failing tile(s)") == 1);
    if (ok) ok = (recordFor(msg, "ADC3").find("XRFdc_DynamicPLLConfig") != std::string::npos);
    // And the call really was made against that tile, so the text cannot be
    // the right text for the wrong reason.
    if (ok) ok = gScript.sawCall("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY);

    if (!ok) {
        fprintf(stderr, "pll reconfigure: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("a discarded pll reconfigure failure is now reported", ok);
}

/*
 * The step recorded for a failing tile is the driver function that returned
 * non-success, not the entry point that called it.
 *
 * The entry point's name is already in the message, at the front, where the
 * host has always seen it. Repeating it per tile would cost a field and add
 * nothing: every record would carry the same word and a reader would still
 * not know where in the sequence the tile died.
 */
void checkStepNameIsDriverFunctionNotEntryPoint() {
    const std::string expected = std::string(" ADC3 XRFdc_DynamicPLLConfig ");

    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();
    const std::string record = recordFor(msg, "ADC3");

    // The step is the token immediately after the tile label, so a record
    // that carried the entry point there instead cannot satisfy this.
    bool ok = (record.compare(0, expected.size(), expected) == 0);
    // The entry point is Reset, and the message opens with it. It must not
    // appear inside any tile record.
    if (ok) ok = (record.find("Reset") == std::string::npos);
    if (ok) ok = (msg.compare(0, 8, "Reset(-1") == 0);

    if (!ok) {
        fprintf(stderr, "step vs entry point: record '%s', text '%s'\n",
                record.c_str(), msg.c_str());
    }

    runCheck("the step name is the driver function not the entry point", ok);
}

/*
 * A step that failed on one tile does not stop the sweep visiting the rest.
 *
 * This one is green before the change as well as after, and it is here for
 * that reason: it is the guard on a change this work must not make. Whether
 * the other three ADC tiles fail alongside the one that is always named is
 * the question the prior phase could not answer, and a sweep that aborted
 * on the first failure would make it permanently unanswerable. It also
 * keeps the sequencing identical to what every prior measurement on this
 * carrier was taken against.
 */
void checkFailingStepStillVisitsRemainingTiles() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 0, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);

    bool ok = tran->errorStrCalled();
    for (uint32_t tile = 1; tile < 4; tile++) {
        if (!ok) break;
        ok = gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, tile, XRFDC_SCRIPT_ANY) &&
             gScript.sawCall("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, tile,
                             XRFDC_SCRIPT_ANY) &&
             gScript.sawCall("XRFdc_SetQMCSettings", XRFDC_ADC_TILE, tile, 0);
    }

    if (!ok) {
        fprintf(stderr, "remaining tiles: %zu recorded call(s), err=%u\n",
                gScript.calls.size(), tran->errorStrCalls());
    }

    runCheck("a failing sweep step still visits the remaining tiles", ok);
}

/* ------------------------------------------------------------------------ */
/* The per-tile entry points.                                                */
/*                                                                           */
/* StartUp, Shutdown, CustomStartUp and the single-tile branch of Reset all  */
/* carry the bare message the global reset path carried before this work.    */
/* They have no accumulation problem, one tile per call, so what they are    */
/* missing is the diagnostic detail and nothing else. _Rfdc.py's Init()      */
/* calls AdcTile[i].Reset() per tile after the two global resets, so a       */
/* failure on that path is reachable on the same boot as the one this        */
/* project keeps seeing.                                                     */
/* ------------------------------------------------------------------------ */

//! Transaction address of ADC tile 3's tile-only register block. Bit 15
//! clear selects the ADC group, bits 14:13 carry the tile id and bit 12
//! clear selects the tile-only decode, which is the layout doTransaction
//! applies to every address below 0x10000.
const uint64_t kTileAdc3Base = 0x6000;

//! One per-tile command offset within that block, with the driver call it
//! reaches and the identifier its message has always opened with.
struct TileCommandSite {
    const char *site;
    uint64_t addr;
    const char *driver;
    const char *entryPoint;
};

const TileCommandSite kTileCommandSites[4] = {
    {"StartUp", kTileAdc3Base + 0x000, "XRFdc_StartUp", "StartUp"},
    {"Shutdown", kTileAdc3Base + 0x004, "XRFdc_Shutdown", "Shutdown"},
    {"Reset", kTileAdc3Base + 0x008, "XRFdc_Reset", "Reset"},
    {"CustomStartUp", kTileAdc3Base + 0x00C, "XRFdc_CustomStartUp", "CustomStartUp"},
};

//! One printed line per entry point, sharing a prefix so the four can be
//! counted as a group, for the same reason the step sub-checks above do.
void runStartupCheck(const char *site, bool ok) {
    const std::string label =
        std::string("startup paths carry tile diagnostics [") + site + "]";

    runCheck(label.c_str(), ok);
}

/*
 * A failing startup, shutdown, custom startup or single-tile reset names
 * the tile and what its registers said, through the same formatter the
 * global path uses.
 *
 * The scripted state is the one the console keeps naming on this carrier,
 * mid clock configuration, with the clock detector asserted, so a record
 * of zeros cannot satisfy the claim.
 */
void checkStartupPathsCarryTileDiagnostics() {
    for (size_t s = 0; s < 4; s++) {
        const TileCommandSite &site = kTileCommandSites[s];

        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure(site.driver, XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
        gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetCurrentState, 6);
        gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetClockDetector, 1);

        rim::TransactionPtr tran = driveWrite(device, site.addr, 1);
        const std::string msg = tran->errorStrValue();
        const std::string record = recordFor(msg, "ADC3");
        const std::string opener = std::string(site.entryPoint) + "(3): failed";

        bool ok = tran->errorStrCalled() && !tran->doneCalled();
        // The identifier and the argument the host already sees are still
        // at the front, and the line still ends where it always did.
        if (ok) ok = (msg.compare(0, opener.size(), opener) == 0);
        if (ok) ok = (!msg.empty() && msg[msg.size() - 1] == '\n');
        // One tile record, this tile's, naming the driver call that failed.
        if (ok) ok = (countOf(msg, " ADC3 ") == 1);
        if (ok) ok = (record.find(site.driver) != std::string::npos);
        // Raw and decoded state, and the clock detector reading.
        if (ok) ok = (record.find("state=0x6(Clock_Configuration[0])") != std::string::npos);
        if (ok) ok = (record.find("clkdet=0x1") != std::string::npos);
        // A per-tile entry point diagnoses its own tile and no other.
        if (ok) ok = (countOf(msg, "1 failing tile(s)") == 1);
        // And it really read that tile rather than printing a remembered
        // record from an earlier transaction.
        if (ok) ok = gScript.sawCall("XRFdc_ReadReg", XRFDC_ADC_TILE, 3, kOffsetCurrentState);
        // The console copy is the same string, as on the global path.
        if (ok) ok = (gScript.logErrors.size() == 1) && (gScript.logErrors[0] == msg);

        if (!ok) {
            fprintf(stderr, "%s path: err=%u done=%u, text '%s'\n",
                    site.site, tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
        }

        runStartupCheck(site.site, ok);
    }
}

/*
 * Reading a command offset still hands back one and still executes nothing.
 *
 * This is what keeps a read-only register snapshot safe to take against a
 * board that is already misbehaving: a tool that walks the tile register
 * block must not start converters or reset them on the way past. Asserted
 * on the recorded driver call list as well as on the returned word, because
 * a body that executed the command and then returned one anyway would
 * satisfy the value check on its own.
 *
 * CustomStartUp is deliberately not in this set. It is the one command
 * offset whose read reports a failure, which is pre-existing behavior and
 * has its own claim below.
 */
void checkCommandReadReturnsOneAndExecutesNothing() {
    bool ok = true;
    std::string detail;

    for (size_t s = 0; s < 3; s++) {
        const TileCommandSite &site = kTileCommandSites[s];

        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        // Scripted to fail, so a body that did execute the command would
        // also produce an error string and could not pass quietly.
        gScript.scriptFailure(site.driver, XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveRead(device, site.addr);

        bool one = tran->doneCalled() && !tran->errorStrCalled();
        if (one) one = tran->errorStrValue().empty();
        if (one) one = (tran->getWord(0) == 1);
        // No call to the command's own driver function, and no diagnostic
        // read of the tile either.
        if (one) one = (gScript.countCalls(site.driver) == 0);
        if (one) one = !gScript.sawCall("XRFdc_ReadReg", XRFDC_ADC_TILE, 3, kOffsetCurrentState);

        if (!one) {
            ok = false;
            detail = std::string(site.site) + ": word=" + std::to_string(tran->getWord(0)) +
                     " done=" + std::to_string(tran->doneCalls()) +
                     " err=" + std::to_string(tran->errorStrCalls()) +
                     " driver calls=" + std::to_string(gScript.countCalls(site.driver));
            break;
        }
    }

    if (!ok) fprintf(stderr, "command read: %s\n", detail.c_str());

    runCheck("a command read still returns one and executes nothing", ok);
}

/*
 * Reading the custom startup offset still reports a failure.
 *
 * That body sets a failure status on a read rather than handing back one
 * like the other three. It is the only guaranteed non-success return
 * reachable from the host with no real converter fault, so it is the
 * mechanism a later plan uses to induce this reporting path on hardware
 * deliberately. Pinned here so a tidy-up that made it look like its three
 * neighbours would be a visible test diff rather than a silent loss.
 */
void checkCustomStartUpReadStillYieldsFailure() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();

    rim::TransactionPtr tran = driveRead(device, kTileAdc3Base + 0x00C);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = !msg.empty();
    if (ok) ok = (msg.compare(0, 20, "CustomStartUp(3): fa") == 0);
    // Nothing was started: the failure comes from the read branch itself,
    // not from a converter command the read issued.
    if (ok) ok = (gScript.countCalls("XRFdc_CustomStartUp") == 0);

    if (!ok) {
        fprintf(stderr, "custom startup read: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("a custom startup read still yields a failure status", ok);
}

/* ------------------------------------------------------------------------ */
/* Meta-assertions.                                                          */
/*                                                                           */
/* Everything above asserts something about PyRFdc.cpp. These three assert   */
/* something about this file, so that RESULT PASS cannot be produced by a    */
/* suite that quietly stopped running claims, by claims driven against a     */
/* fixture that recorded nothing, or by a fixture that carried one claim's   */
/* scripted state into the next.                                             */
/* ------------------------------------------------------------------------ */

/*
 * The fixture really is emptied between claims.
 *
 * Every claim above opens with gScript.reset() and then scripts exactly the
 * failures it wants. If reset left the previous claim's failure selector in
 * place, a later claim would be driving a sweep it did not script, and the
 * claims that assert a clean transaction would be the first to lie about
 * it. Asserted on all four kinds of state the fixture carries: the recorded
 * call list, the failure selector, the scripted registers and the log
 * lines.
 */
void checkFixtureResetEmptiesRecordedState() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptRegister(XRFDC_ADC_TILE, 3, kOffsetCurrentState, 6);
    driveWrite(device, kResetAllAdc, 1);

    // Everything above is state a later claim must not inherit.
    const bool dirty = !gScript.calls.empty() && !gScript.logErrors.empty() &&
                       (gScript.statusFor("XRFdc_Reset", XRFDC_ADC_TILE, 3,
                                          XRFDC_SCRIPT_ANY) != XRFDC_SUCCESS) &&
                       (gScript.registerValue(XRFDC_ADC_TILE, 3, kOffsetCurrentState) == 6);

    gScript.reset();

    bool ok = dirty;
    if (ok) ok = gScript.calls.empty();
    if (ok) ok = gScript.logErrors.empty();
    if (ok) {
        ok = (gScript.statusFor("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY) ==
              XRFDC_SUCCESS);
    }
    if (ok) ok = (gScript.registerValue(XRFDC_ADC_TILE, 3, kOffsetCurrentState) == 0);

    if (!ok) {
        fprintf(stderr,
                "fixture reset: dirty before=%d, after reset calls=%zu logs=%zu "
                "status=%d reg=%u\n",
                static_cast<int>(dirty), gScript.calls.size(), gScript.logErrors.size(),
                gScript.statusFor("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY),
                gScript.registerValue(XRFDC_ADC_TILE, 3, kOffsetCurrentState));
    }

    runCheck("fixture reset empties the recorded state", ok);
}

/*
 * The fixture records what the sweep did.
 *
 * Several claims above are asserted entirely on the recorded call list. If
 * the stubs ever stopped recording, those claims would read an empty list
 * and every count they compare against zero would pass for the wrong
 * reason.
 */
void checkRecordedCallListIsNotEmpty() {
    PyRFdcPtr device = PyRFdc::create();

    gScript.reset();
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    driveWrite(device, kResetAllAdc, 1);

    bool ok = !gScript.calls.empty();
    // And it records the sweep in particular, not merely something.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) > 0);
    if (ok) ok = (countCallsForType("XRFdc_CheckTileEnabled", XRFDC_ADC_TILE) > 0);

    if (!ok) {
        fprintf(stderr, "recorded call list: %zu entries\n", gScript.calls.size());
    }

    runCheck("the recorded driver call list is not empty after a sweep", ok);
}

//! Claims that run before the count check itself. Update deliberately when a
//! claim is added or removed, so a claim that silently stops being invoked
//! turns this one red instead of shrinking the suite unnoticed.
const int kClaimsBeforeCountCheck = 32;

/*
 * Every claim this file defines actually ran.
 *
 * RESULT PASS is printed from a failure counter, so a suite that stopped
 * invoking half its claims would still print it. The count is written out
 * rather than derived, because deriving it from the same loop that runs the
 * claims would make it agree with whatever the suite happened to do. An
 * earlier bench tool in this project had a whole section that existed in
 * the source and was never called from the entry point, so it never ran and
 * nothing said so.
 */
void checkClaimCountMatchesExpectedTotal() {
    const bool ok = (gChecks == kClaimsBeforeCountCheck);

    if (!ok) {
        fprintf(stderr, "claim count: %d claims ran before this one, expected %d\n",
                gChecks, kClaimsBeforeCountCheck);
    }

    runCheck("claim count matches the expected total", ok);
}

}  // namespace

int main(int /*argc*/, char ** /*argv*/) {
    printf("PyRFdc board-free diagnostics\n");

    checkShimCompilesPyRFdc();
    checkScratchpadRoundTrip();
    checkGlobalResetMessageIsPinnedByteForByte();
    checkTileTypeIndices();

    checkAccumulatesEveryFailingTile();
    checkTileOrderIsAdcThenDac();
    checkTwoIdenticalFailuresEmitTwoRecords();
    checkZeroFailuresLeavesTransactionClean();
    checkUnreadDiagnosticsReportUnavailable();
    checkDecodedStateNamesMatchPythonTable();
    checkRawStateAboveFifteenIsMasked();

    checkReportsAllEightTiles();
    checkOtherTypeIsDiagnosedButNotReset();
    checkCleanSweepStillReportsNothing();
    checkReportedLineFitsTheConsoleBuffer();

    checkSweepStepsAreAttributedByName();
    checkDiscardedPllReconfigureFailureIsReported();
    checkStepNameIsDriverFunctionNotEntryPoint();
    checkFailingStepStillVisitsRemainingTiles();

    checkStartupPathsCarryTileDiagnostics();
    checkCommandReadReturnsOneAndExecutesNothing();
    checkCustomStartUpReadStillYieldsFailure();

    checkFixtureResetEmptiesRecordedState();
    checkRecordedCallListIsNotEmpty();
    checkClaimCountMatchesExpectedTotal();

    printf("RESULT %s\n", (gFailures == 0) ? "PASS" : "FAIL");
    return (gFailures == 0) ? 0 : 1;
}
