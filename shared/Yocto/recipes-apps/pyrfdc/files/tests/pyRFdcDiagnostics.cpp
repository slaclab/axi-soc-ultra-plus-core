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
#include <new>
#include <string>

/*
 * Compile the production source into this translation unit rather than
 * linking it as a separate object. This is deliberate, and it is the same
 * move emulator/driver/tests/prbs_cross_validate.c makes for the kernel PRBS
 * source. What it buys is one compile and one link unit, built with exactly
 * the shim include path and the flags below and with no second build rule
 * to keep in step, and it puts the driver source's internal-linkage
 * file-scope definitions, IPSM_STATE_NAMES and DIAG_MSG_BUDGET among them,
 * in reach of a check that needs them. Those are unreachable from a
 * separately linked object at any access level, so a check over them would
 * otherwise need a test-only public API in PyRFdc.h, and widening the
 * shipped header for the benefit of a test is what this avoids. It does not
 * change the shipped sources at all.
 *
 * What it does NOT buy, stated because assuming otherwise decided the shape
 * of earlier work here: it gives no access to PyRFdc's private members or
 * private methods. The checks below are free functions in an anonymous
 * namespace and C++ access control applies to them exactly as it would from
 * another translation unit. Every claim in this file is therefore asserted
 * on what a caller can observe through doTransaction, which is the right
 * discipline anyway.
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

//! Drive one multi-word write through the real doTransaction, so a claim can
//! observe what the single completion at the end of the word loop does with
//! an error raised on one word out of several. Every payload word carries the
//! same value; no claim below reads them back.
rim::TransactionPtr driveWriteWords(PyRFdcPtr device, uint64_t addr, uint32_t words,
                                    uint32_t value) {
    rim::TransactionPtr tran =
        rim::Transaction::create(addr, words * uint32_t(sizeof(uint32_t)), rim::Write);

    for (uint32_t i = 0; i < words; i++) {
        tran->setWord(i, value);
    }
    device->doTransaction(tran);
    return tran;
}

//! Address of the global ADC reset, the one _Rfdc.py exposes as ResetAllAdc
//! and the one Init() reaches first.
const uint64_t kResetAllAdc = 0x10010;

//! Address of the global DAC reset, the one _Rfdc.py exposes as ResetAllDac
//! and the one Init() reaches second. The two are separate transactions, so
//! a claim about what one global reset leaves behind has to drive exactly
//! the one it means.
const uint64_t kResetAllDac = 0x10014;

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
    // Four resets against the ADC group, and the number has stayed at four
    // for a completely different reason than it used to hold. It used to be
    // one unconditional post-reset per enabled tile in a second sweep loop
    // that no longer exists. It is now one compensating reset per enabled
    // tile, fired because this fixture's power-up status read returns zero
    // for every tile, so the PLL reconfigure performed no internal cycle for
    // any of them and each tile needs its one cycle issued explicitly.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 4);
    // And the per-tile shape is pinned rather than only the total, so this
    // claim cannot go on passing on a number that happened to stay the same
    // while the sweep underneath it changed again.
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE) == 4);

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
            // Every bit but one, and the one is deliberate. This register
            // is read twice for two different purposes now: the diagnostic
            // reads the whole word, and the sweep reads bit 2 of it through
            // a mask to decide whether the tile was already powered up. All
            // ones would report every tile as powered up, the compensating
            // reset would never fire, the scripted reset failure below
            // would have nothing to fail, and this claim would measure the
            // length of an empty message. Clearing bit 2 keeps the printed
            // field at its full eight hex digits, which is what this claim
            // is about, while leaving the tiles in the state that makes the
            // sweep issue its resets.
            gScript.scriptRegister(type, tile, 0x0228, 0xFFFFFFFBu);
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

//! Position of the nth recorded call with this exact name and index tuple,
//! counting from one, or the length of the list when there are fewer than n
//! of them. The length rather than a sentinel, for the reason firstCallAt
//! gives above: a sentinel that is not an index would have to be special
//! cased at every call site, while the length compares correctly against any
//! real index and keeps an ordering comparison honest on its own.
//!
//! Second occurrences are the only way to ask about a pass that re-runs
//! tiles an earlier pass already visited. Such a pass issues calls carrying
//! the same name, type, tile and block as the first, so no selector can tell
//! the two apart and recorded position is the whole of the discriminator.
size_t nthCallAt(const char *name, uint32_t type, uint32_t tile, uint32_t block, size_t n) {
    const std::string want = XRFdcScript::describe(name, type, tile, block);
    size_t seen = 0;

    for (size_t i = 0; i < gScript.calls.size(); i++) {
        if (gScript.calls[i] != want) continue;

        seen++;
        if (seen == n) return i;
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
 * Each of the six driver calls the sweep makes is attributed to its own name
 * when it is the call that failed.
 *
 * One pair of these sites is the same driver function reached twice, and it
 * needs a discriminator or a claim about one member would be satisfied by
 * the other:
 *
 *   the two XRFdc_UpdateEvent sites differ only in the event they raise,
 *   which the fixture's detail selector can key on, so each is scripted to
 *   fail on its own event and the recorded order is asserted as well
 *
 * XRFdc_Reset was such a pair and is no longer one. The sweep held two reset
 * sites taking identical arguments, one before the tile's PLL reconfigure
 * and one in a second loop after every block of every tile had been visited,
 * and recorded order was the only thing that could tell them apart. The
 * global branch now holds exactly one reset site, the compensating reset,
 * and the ordering assertion on site 1 pins it to its position after the
 * reconfigure rather than separating it from a sibling that no longer
 * exists.
 */
void checkSweepStepsAreAttributedByName() {
    /* Site 1: the compensating XRFdc_Reset, issued after the tile's PLL
     * reconfigure for a tile that reconfigure did not cycle. On this fixture
     * the power-up status read returns zero, so it fires for every tile. */
    {
        PyRFdcPtr device = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, kStepTile, XRFDC_SCRIPT_ANY,
                              XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string record = recordFor(tran->errorStrValue(), kStepLabel);

        bool ok = (record.find("XRFdc_Reset") != std::string::npos);
        // The ordering is inverted from what this sub-check used to assert,
        // and the inversion is the point: the tile's diagnostics are now
        // taken after its reconfigure, because the only reset left in the
        // global branch runs after it rather than before it.
        if (ok) {
            ok = firstCallAt("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY) <
                 firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                             XRFDC_SCRIPT_ANY);
        }

        if (!ok) {
            fprintf(stderr, "step site 1: record '%s', reconfigure at %zu, diag at %zu\n",
                    record.c_str(),
                    firstCallAt("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, kStepTile,
                                XRFDC_SCRIPT_ANY),
                    firstCallAt("XRFdc_GetPLLLockStatus", XRFDC_ADC_TILE, kStepTile,
                                XRFDC_SCRIPT_ANY));
        }

        runStepCheck("XRFdc_Reset after the PLL reconfigure", ok);
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
/* A driver whose initialization failed.                                     */
/*                                                                           */
/* The constructor has four early returns, each of which logs a line and     */
/* leaves the object constructed with its driver instance never initialized. */
/* Every later transaction reads through that instance. On the bench that    */
/* presents as a register read that never answers, and the prior measurement */
/* on this carrier found that whether a read times out is currently a better */
/* failure indicator than anything the registers return. The claims below    */
/* are about replacing that silence with a sentence naming the step that     */
/* failed, and about keeping the registers that need no driver answerable so */
/* the host can ask over the same transport why the driver is dead.          */
/*                                                                           */
/* Three of the four early returns are reachable in this build. The fourth,  */
/* the readiness check that runs before libmetal is brought up, sits inside  */
/* a baremetal-only block and is compiled out here, so no claim can drive    */
/* it. Its reason value exists in both build configurations all the same,    */
/* which is the point of declaring all six unconditionally: a host decoding  */
/* the value does not have to know which build produced the binary.          */
/* ------------------------------------------------------------------------ */

/*
 * Construct an instance whose initialization failed at the named libmetal or
 * driver entry point, then clear the recorded lists so what a claim observes
 * afterwards belongs to its own transaction and not to the construction.
 *
 * The failure is scripted before the construction rather than after it,
 * because the constructor is where these entry points are called. Only the
 * recorded lists are cleared and not the whole fixture, because a full reset
 * would drop the scripted failure and the scripted names match no call any
 * transaction below makes.
 */
PyRFdcPtr createDeadDevice(const char *failingCall) {
    gScript.reset();
    gScript.scriptFailure(failingCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();
    gScript.metalLogs.clear();
    return device;
}

/*
 * The same construction, with the recorded lists left alone.
 *
 * The clear in createDeadDevice is correct for a claim about a transaction:
 * it makes what the claim reads afterwards belong to the transaction it
 * drove and not to the construction that preceded it. It is also exactly
 * what made a claim about the construction itself impossible, because the
 * calls the constructor made after the scripted failure were recorded and
 * then thrown away before any claim looked at them.
 *
 * The two helpers stand side by side rather than one replacing the other, so
 * no claim written against createDeadDevice changes behavior. A claim that
 * asserts about what the constructor did calls this one; a claim that
 * asserts about what a transaction did calls the other.
 */
PyRFdcPtr createDeadDeviceKeepingCalls(const char *failingCall) {
    gScript.reset();
    gScript.scriptFailure(failingCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    return PyRFdc::create();
}

//! The byte the storage below is prefilled with. Non-zero and recognizable:
//! the same 0xA5 the round-trip claims use for a distinctive word, so a value
//! read back out of a member nothing assigned is obviously the fill and not a
//! plausible register value.
const unsigned char kDirtyFill = 0xA5;

//! Storage for one instance, aligned for the class and sized by it, so the
//! fill covers every member a claim can read.
alignas(PyRFdc) unsigned char gDirtyStorage[sizeof(PyRFdc)];

/*
 * The same construction as createDeadDevice, into storage this file filled
 * with a known non-zero pattern first.
 *
 * PyRFdc has a user-provided default constructor, so the allocator behind
 * std::make_shared hands that constructor raw storage and zero-initializes
 * none of it. The reason this helper exists is the other half of that fact: a
 * freshly mapped allocator page reads as zero in practice, so a claim that
 * reads a member no construction path assigned would read zero back through
 * PyRFdc::create() and print PASS with the member uninitialized. That is the
 * shape of false assurance this harness has already produced once. Filling
 * the storage first is what lets such a claim fail.
 *
 * Only one instance from this buffer may be alive at a time, because the next
 * call refills the buffer underneath whatever is still there. Every claim
 * that uses this creates its instance inside its own body and drops it there.
 *
 * The returned pointer destroys in place and frees nothing, since the storage
 * is static and outlives every claim. The recorded lists are cleared exactly
 * as createDeadDevice clears them, so a claim observes only its own
 * transactions.
 */
PyRFdcPtr createDeadDeviceInDirtyStorage(const char *failingCall) {
    gScript.reset();
    gScript.scriptFailure(failingCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    std::memset(gDirtyStorage, kDirtyFill, sizeof(gDirtyStorage));

    PyRFdc *instance = new (static_cast<void *>(gDirtyStorage)) PyRFdc();

    gScript.calls.clear();
    gScript.logErrors.clear();
    gScript.metalLogs.clear();

    return PyRFdcPtr(instance, [](PyRFdc *p) { p->~PyRFdc(); });
}

//! Bytes of stack the helper below writes the fill pattern into. Comfortably
//! larger than the frame the constructor and the calls it makes before its
//! registration step occupy, so the region that frame is about to sit in is
//! covered.
const size_t kStackPoisonBytes = 8192;

/*
 * Write the fill pattern into a region of stack below this function's own
 * frame, then release it.
 *
 * The array is volatile so the write loop survives the -O2 the tests
 * Makefile builds with, and the function carries the no-inline attribute so
 * its frame is really pushed and really released rather than folded into the
 * caller's.
 */
__attribute__((noinline)) void poisonStackRegion() {
    volatile unsigned char scratch[kStackPoisonBytes];
    for (size_t i = 0; i < kStackPoisonBytes; i++) {
        scratch[i] = kDirtyFill;
    }
    // Nothing reads it back, and nothing here can: the only reader that
    // matters is whatever the next frame puts in the same place. The cast
    // keeps the build warning-free without weakening the volatile writes
    // above, which are what makes those writes survive -O2.
    (void)scratch;
}

/*
 * The same construction as createDeadDeviceInDirtyStorage, with the stack the
 * constructor's frame is about to occupy filled with the same pattern first,
 * and with the recorded lists left alone.
 *
 * Placement new into the existing static buffer is used rather than the
 * allocator, because an allocation between the fill and the construction
 * would run allocator code through the very region the constructor's frame is
 * about to occupy and overwrite it.
 *
 * The fill is a best effort at making an uninitialized local in that frame
 * read back as the pattern, and the language does not guarantee it: frame
 * layout, a red zone or a register allocation can put the local somewhere the
 * fill never reached. The evidence that a claim built on this helper is
 * load-bearing therefore rests on the stub mutation as well, which is
 * deterministic because the stub then writes a real device address into the
 * out parameter whatever the scripted status says.
 *
 * The recorded lists are not cleared, because the claim that uses this
 * asserts about what the construction itself did.
 */
PyRFdcPtr createDeadDeviceOnPoisonedStack(const char *failingCall) {
    gScript.reset();
    gScript.scriptFailure(failingCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    std::memset(gDirtyStorage, kDirtyFill, sizeof(gDirtyStorage));
    poisonStackRegion();

    PyRFdc *instance = new (static_cast<void *>(gDirtyStorage)) PyRFdc();

    return PyRFdcPtr(instance, [](PyRFdc *p) { p->~PyRFdc(); });
}

//! A recognizable non-null value a claim puts into its own local before it
//! calls the registration entry point, so a stub that left the out parameter
//! alone is distinguishable from one that wrote a real address into it.
struct metal_device *const kRegistrationSentinel =
    reinterpret_cast<struct metal_device *>(static_cast<uintptr_t>(0xA5A5A5A5u));

/*
 * The step wording, transcribed rather than taken from the driver's own
 * mapping. Generating these from the table under test would make the two
 * copies one copy and the claims would assert nothing about the wording.
 */
const char *const kStepMetalInit = "construction failed at metal_init";
const char *const kStepConfigLookup =
    "construction failed at XRFdc_LookupConfig for the driver configuration";
const char *const kStepRegisterMetal = "construction failed at XRFdc_RegisterMetal";
const char *const kStepNotCompleted = "construction did not complete";

//! The offsets whose bodies never reach the driver instance.
const uint64_t kMetalLogLevel = 0x12000;
const uint64_t kIgnoreMetalError = 0x12004;
const uint64_t kScratchPad = 0x12008;
const uint64_t kInitFailReason = 0x1200C;
const uint64_t kDoubleTestLower = 0x13000;

/*
 * A transaction that would reach the driver instance is refused, and the
 * refusal names the constructor step that failed.
 *
 * Asserted on the recorded driver call list as well as on the message,
 * because an error string produced after the sweep had already run would be
 * the right words for the wrong reason. The global reset path is the one
 * this project keeps failing on, and it is also the path that reads four
 * control and status registers per tile once it has failed, so a rejection
 * that arrived after the dispatch began would still have driven the
 * diagnostic helper through a driver instance that was never initialized.
 */
void checkDeadDriverRejectsDriverTransaction() {
    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = !msg.empty();
    if (ok) ok = (msg.find(kStepMetalInit) != std::string::npos);
    // No reset, and no diagnostic read either, which is what rejecting
    // ahead of the dispatch chain buys.
    if (ok) ok = (gScript.countCalls("XRFdc_Reset") == 0);
    if (ok) ok = (gScript.countCalls("XRFdc_GetPLLLockStatus") == 0);
    if (ok) ok = (gScript.countCalls("XRFdc_ReadReg") == 0);

    if (!ok) {
        fprintf(stderr, "dead driver reject: err=%u done=%u, %zu driver call(s), text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), gScript.calls.size(), msg.c_str());
    }

    runCheck("dead driver rejects a driver transaction", ok);
}

/*
 * The registers that need no driver still answer on a dead driver.
 *
 * This is what makes the rejection useful rather than merely safe: a host
 * that can still read and write the pure-state block can ask the board why
 * the driver is dead over the same transport, instead of inferring it from a
 * read that never returns.
 */
void checkDeadDriverKeepsPureStateReadable() {
    const uint32_t pattern = 0xA5C33C5Au;

    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr write = driveWrite(device, kScratchPad, pattern);
    rim::TransactionPtr read = driveRead(device, kScratchPad);
    rim::TransactionPtr logLevel = driveRead(device, kMetalLogLevel);

    bool ok = write->doneCalled() && !write->errorStrCalled();
    if (ok) ok = read->doneCalled() && !read->errorStrCalled();
    if (ok) ok = (read->getWord(0) == pattern);
    if (ok) ok = logLevel->doneCalled() && !logLevel->errorStrCalled();

    if (!ok) {
        fprintf(stderr,
                "dead driver pure state: wrote 0x%08X read 0x%08X, write err=%u read err=%u "
                "log level err=%u text '%s'\n",
                pattern, read->getWord(0), write->errorStrCalls(), read->errorStrCalls(),
                logLevel->errorStrCalls(), read->errorStrValue().c_str());
    }

    runCheck("dead driver keeps the pure-state block readable", ok);
}

/*
 * The one offset in the pure-state block whose write is refused as well.
 *
 * Reading the metal log level hands back a stored boolean and touches
 * nothing. Writing it calls into libmetal, which on a dead driver may never
 * have been initialized at all, so the read is admitted and the write is
 * not. Asserted on the recorded libmetal call list rather than on the
 * message, because a body that made the call and then reported an error
 * would satisfy a message check on its own.
 */
void checkDeadDriverRejectsMetalLogLevelWrite() {
    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr tran = driveWrite(device, kMetalLogLevel, 1);

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = !tran->errorStrValue().empty();
    if (ok) ok = (gScript.countCalls("metal_set_log_level") == 0);

    if (!ok) {
        fprintf(stderr, "metal log level write: err=%u done=%u, %zu set-log-level call(s)\n",
                tran->errorStrCalls(), tran->doneCalls(),
                gScript.countCalls("metal_set_log_level"));
    }

    runCheck("dead driver rejects a metal log level write", ok);
}

//! One constructor early return, with the entry point that reaches it and
//! the step the rejection is required to name.
struct BailOutSite {
    const char *call;
    const char *step;
};

/*
 * The three early returns reachable in this build. Each is scripted on its
 * own and the message is required to name that step and neither of the
 * others, so a mapping that collapsed two of them into one wording, or that
 * reported a fixed string, cannot pass.
 */
const BailOutSite kBailOutSites[3] = {
    {"metal_init", kStepMetalInit},
    {"XRFdc_LookupConfig", kStepConfigLookup},
    {"XRFdc_RegisterMetal", kStepRegisterMetal},
};

void checkEachBailOutNamesItsOwnStep() {
    bool ok = true;
    std::string detail;

    for (size_t s = 0; s < 3; s++) {
        PyRFdcPtr device = createDeadDevice(kBailOutSites[s].call);

        rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
        const std::string msg = tran->errorStrValue();

        bool one = tran->errorStrCalled() && !tran->doneCalled();
        if (one) one = (msg.find(kBailOutSites[s].step) != std::string::npos);
        // And none of the other two steps, so a message that listed every
        // reason it knows about cannot pass either.
        for (size_t other = 0; one && (other < 3); other++) {
            if (other == s) continue;
            one = (msg.find(kBailOutSites[other].step) == std::string::npos);
        }

        if (!one) {
            ok = false;
            detail = std::string(kBailOutSites[s].call) + ": text '" + msg + "'";
            break;
        }
    }

    if (!ok) fprintf(stderr, "bail-out step: %s\n", detail.c_str());

    runCheck("each constructor bail-out names its own step", ok);
}

/*
 * The registration stub reports its out parameter the way the driver does.
 *
 * This claim owns the stub's contract and nothing else. It constructs no
 * instance of the production class, because its subject is what the stub
 * writes and a construction would make it depend on the production code as
 * well. The entry point is driven directly with a local this claim set to a
 * recognizable non-null value first, so "left alone" and "written with an
 * address" are distinguishable outcomes rather than one indistinguishable
 * non-null.
 *
 * It exists so that a later edit cannot re-blind the harness silently. A stub
 * that went back to writing the out parameter before consulting the scripted
 * status would make every claim about the registration bail-out a claim about
 * a stub kinder than the driver, and nothing else in this file would say so.
 */
void checkRegistrationStubLeavesDevicePointerUnwrittenOnFailure() {
    gScript.reset();
    gScript.scriptFailure("XRFdc_RegisterMetal", XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    struct metal_device *failed = kRegistrationSentinel;
    const u32 failStatus = XRFdc_RegisterMetal(nullptr, RFDC_DEVICE_ID, &failed);

    gScript.reset();

    struct metal_device *passed = kRegistrationSentinel;
    const u32 okStatus = XRFdc_RegisterMetal(nullptr, RFDC_DEVICE_ID, &passed);

    bool ok = (failStatus != XRFDC_SUCCESS);
    if (ok) ok = (failed == kRegistrationSentinel);
    if (ok) ok = (okStatus == XRFDC_SUCCESS);
    if (ok) ok = (passed != nullptr);
    if (ok) ok = (passed != kRegistrationSentinel);

    if (!ok) {
        fprintf(stderr,
                "registration stub: failure status=%u pointer=%p, success status=%u pointer=%p, "
                "sentinel=%p\n",
                failStatus, static_cast<void *>(failed), okStatus, static_cast<void *>(passed),
                static_cast<void *>(kRegistrationSentinel));
    }

    runCheck("the registration stub leaves its device pointer unwritten on a scripted failure", ok);
}

/*
 * A registration that did not succeed handed nothing back, so the bail-out
 * closes nothing.
 *
 * The recorded call list is read while the instance is still alive, for the
 * reason the comment on checkNoDriverCallFollowsAFailedCfgInitialize gives:
 * the destructor closes a device and finishes libmetal of its own, so counts
 * taken after the instance went out of scope would be about teardown rather
 * than about construction. Nothing is written through the instance before
 * those counts are taken either.
 *
 * The libmetal finish is counted alongside the close because the two sit on
 * the same three lines: a guard written so that it skipped the release as
 * well would satisfy a close-only claim and would leak the library.
 *
 * The refusal is driven afterwards so the step a host reads back is pinned
 * here too, since the whole reason the process has to survive this path is
 * that the reason register is the answer to why the driver is dead.
 */
void checkRegistrationBailOutClosesNoDevice() {
    PyRFdcPtr device = createDeadDeviceOnPoisonedStack("XRFdc_RegisterMetal");

    const size_t closes = gScript.countCalls("metal_device_close");
    const size_t finishes = gScript.countCalls("metal_finish");
    const void *handed = gScript.closedDevice;

    bool ok = (closes == 0);
    if (ok) ok = (finishes == 1);

    gScript.calls.clear();
    gScript.logErrors.clear();
    gScript.metalLogs.clear();

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    if (ok) ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = (msg.find(kStepRegisterMetal) != std::string::npos);

    if (!ok) {
        fprintf(stderr,
                "registration bail-out: %zu close call(s), %zu libmetal finish call(s), "
                "closed pointer handed to the close stub %p, text '%s'\n",
                closes, finishes, const_cast<void *>(handed), msg.c_str());
    }

    runCheck("the registration bail-out closes no device the registration never handed back", ok);
}

/*
 * A construction that neither bailed out nor completed is dead, and says so.
 *
 * The configuration initialize call is the one step whose return value the
 * constructor discarded outright, so a driver instance that was never
 * configured was indistinguishable from one that was. The flag is set only
 * when that call reported success, and the reason value it leaves behind is
 * the one the two members carry from their declaration. That is the whole
 * direction of the design: a construction that went wrong in a way nobody
 * anticipated, or on a path added later, is dead by default rather than
 * alive by default, and the absence of an explicit failure is never read as
 * success.
 */
void checkNotCompletedConstructorIsDeadByDefault() {
    PyRFdcPtr device = createDeadDevice("XRFdc_CfgInitialize");

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = (msg.find(kStepNotCompleted) != std::string::npos);
    // Not reported as one of the named bail-outs, none of which happened.
    if (ok) ok = (msg.find(kStepMetalInit) == std::string::npos);
    if (ok) ok = (msg.find(kStepConfigLookup) == std::string::npos);
    if (ok) ok = (msg.find(kStepRegisterMetal) == std::string::npos);
    if (ok) ok = (gScript.countCalls("XRFdc_Reset") == 0);

    if (!ok) {
        fprintf(stderr, "not completed: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("not-completed constructor is dead by default", ok);
}

/*
 * A configuration initialize that reported non-success stops the constructor.
 *
 * Asserted on the recorded call list and not on any message. The refusal
 * message was already correct before this behavior existed, so a claim over
 * the message would print PASS whether the constructor stopped or carried on,
 * and would be evidence about nothing. The three names are the ones the
 * driver instance is reached through after the configuration initialize: the
 * tile enable probe, the PLL reconfigure that is a converter write, and the
 * raw register read.
 */
void checkFailedCfgInitializeStopsTheConstructor() {
    PyRFdcPtr device = createDeadDeviceKeepingCalls("XRFdc_CfgInitialize");

    const size_t tileEnabled = gScript.countCalls("XRFdc_CheckTileEnabled");
    const size_t pllConfig = gScript.countCalls("XRFdc_DynamicPLLConfig");
    const size_t rdReg = gScript.countCalls("XRFdc_RDReg");

    bool ok = (tileEnabled == 0);
    if (ok) ok = (pllConfig == 0);
    if (ok) ok = (rdReg == 0);

    if (!ok) {
        fprintf(stderr,
                "cfg initialize stop: %zu recorded call(s), CheckTileEnabled=%zu "
                "DynamicPLLConfig=%zu RDReg=%zu\n",
                gScript.calls.size(), tileEnabled, pllConfig, rdReg);
    }

    runCheck("a failed configuration initialize stops the constructor", ok);
}

/*
 * The rule form of the claim above: nothing at all runs after the declined
 * configuration initialize.
 *
 * The claim above names three driver functions, so a tenth call added to the
 * constructor later would slip past it. This one asserts position in the
 * ordered call list instead, so any call added after the bail-out fails it
 * whatever the call is named.
 *
 * The list is read while the instance is still alive. The destructor calls
 * into the driver and into libmetal and appends entries of its own, so a
 * claim that read the list after the instance went out of scope would be
 * asserting about teardown rather than about construction.
 *
 * Matched on the name and the field separator rather than against a whole
 * formatted entry, so the claim survives a change to the index fields the
 * stub passes.
 */
void checkNoDriverCallFollowsAFailedCfgInitialize() {
    PyRFdcPtr device = createDeadDeviceKeepingCalls("XRFdc_CfgInitialize");

    const std::string wantPrefix = "XRFdc_CfgInitialize/";

    bool ok = !gScript.calls.empty();
    if (ok) {
        const std::string &last = gScript.calls.back();
        ok = (last.compare(0, wantPrefix.size(), wantPrefix) == 0);
    }

    if (!ok) {
        fprintf(stderr, "no call after cfg initialize: %zu recorded call(s), last '%s'\n",
                gScript.calls.size(),
                gScript.calls.empty() ? "" : gScript.calls.back().c_str());
    }

    runCheck("the constructor makes no driver call after a failed configuration initialize", ok);
}

/*
 * The one write the recorded call list is structurally blind to.
 *
 * The sample rate workaround loop assigns two struct fields of the driver
 * instance per tile and makes no call at all, so nothing in the ordered list
 * moves whether it ran or not. The production member holding the instance is
 * private and these are free functions, so the only handle on it is the
 * pointer the configuration initialize stub was handed.
 *
 * The two expected values were read from the workaround loop in PyRFdc.cpp
 * and compared by exact equality, because the property is that the
 * assignment did not happen at all. A tolerance comparison would pass for a
 * value that merely came close to it.
 */
void checkFailedCfgInitializeLeavesSampleRateUnwritten() {
    //! Read from the workaround loop in PyRFdc.cpp, which assigns 5.9 to
    //! every ADC tile and 10.0 to every DAC tile.
    const double kWorkaroundAdcRate = 5.9;
    const double kWorkaroundDacRate = 10.0;

    PyRFdcPtr device = createDeadDeviceKeepingCalls("XRFdc_CfgInitialize");

    // A null here would let a claim that read nothing report success.
    bool ok = (gScript.cfgInstance != nullptr);

    double adcRate = 0.0;
    double dacRate = 0.0;
    if (ok) {
        adcRate = gScript.cfgInstance->RFdc_Config.ADCTile_Config[0].MaxSampleRate;
        dacRate = gScript.cfgInstance->RFdc_Config.DACTile_Config[0].MaxSampleRate;
        ok = (adcRate != kWorkaroundAdcRate);
    }
    if (ok) ok = (dacRate != kWorkaroundDacRate);

    if (!ok) {
        fprintf(stderr, "sample rate workaround: instance=%p ADC0=%f DAC0=%f\n",
                static_cast<const void *>(gScript.cfgInstance), adcRate, dacRate);
    }

    runCheck("a failed configuration initialize leaves the sample rate workaround unwritten", ok);
}

/*
 * The same rejected transaction twice reports the same bytes.
 *
 * The guard reads the validity flag and the reason code and nothing else,
 * and the rejection path writes to neither, so a second attempt on a dead
 * instance cannot report something different from the first. A guard that
 * accumulated, counted or latched would show up here as two texts that
 * differ, and a host retrying a register write would see the driver's state
 * appear to change while nothing about it had.
 */
void checkRepeatedRejectionIsByteIdentical() {
    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr first = driveWrite(device, kResetAllAdc, 1);
    const std::string firstText = first->errorStrValue();

    rim::TransactionPtr second = driveWrite(device, kResetAllAdc, 1);
    const std::string secondText = second->errorStrValue();

    bool ok = !firstText.empty();
    if (ok) ok = (firstText.find(kStepMetalInit) != std::string::npos);
    if (ok) ok = (firstText == secondText);
    // Both were rejected, rather than the second quietly succeeding.
    if (ok) ok = second->errorStrCalled() && !second->doneCalled();

    if (!ok) {
        fprintf(stderr, "repeated rejection: first '%s', second '%s'\n",
                firstText.c_str(), secondText.c_str());
    }

    runCheck("repeated rejection is byte identical", ok);
}

/*
 * A rejection raised on one word of a multi-word transaction survives to the
 * single completion at the end of the word loop.
 *
 * The error string is cleared once per transaction, before the loop, and the
 * completion runs once after it, so a rejection on a later word has to
 * survive every word after it to be reported at all. Driven across the
 * double test pair, which the guard admits, and the word immediately above
 * it, which it does not, so the transaction crosses an admitted offset and a
 * rejected one in that order.
 */
void checkMultiWordRejectionReachesErrorStr() {
    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr tran = driveWriteWords(device, kDoubleTestLower, 3, 0);
    const std::string msg = tran->errorStrValue();

    bool ok = tran->errorStrCalled() && !tran->doneCalled();
    // The rejection and not the undefined-memory text the same offset would
    // have produced on a live driver.
    if (ok) ok = (msg.find(kStepMetalInit) != std::string::npos);
    if (ok) ok = (msg.find("0x13008") != std::string::npos);

    if (!ok) {
        fprintf(stderr, "multi-word rejection: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("multi-word rejection reaches errorStr", ok);
}

/*
 * A live driver behaves exactly as it did.
 *
 * The guard's first statement is a test of one boolean, and on a live driver
 * it returns having done nothing else, so the dispatch chain, the driver
 * calls it makes and the message a failure produces are all unchanged. The
 * recorded call counts for a clean global ADC reset are written out rather
 * than compared against another run, so a change to the sweep shows up here
 * as a number that moved.
 */
void checkLiveDriverIsUnaffectedByTheGuard() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    rim::TransactionPtr clean = driveWrite(device, kResetAllAdc, 1);

    bool ok = clean->doneCalled() && !clean->errorStrCalled();
    // Still four, and deliberately left at four rather than adjusted. The
    // four used to be the unconditional post-reset of the second sweep loop,
    // once per enabled tile. They are now the compensating reset, once per
    // enabled tile, fired because this fixture's power-up status read
    // returns zero so no tile was cycled by its PLL reconfigure. Same
    // number, different reason, and the reason is written down because a
    // claim that keeps passing for an unexamined reason is how a regression
    // hides.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 4);
    // Moved from 8 to 4: the second sweep loop is gone and it took its own
    // per-tile enable probe with it, so a global reset now walks the tiles
    // once rather than twice.
    if (ok) ok = (countCallsForType("XRFdc_CheckTileEnabled", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_SetQMCSettings", XRFDC_ADC_TILE) == 16);
    if (ok) ok = (countCallsForType("XRFdc_SetMixerSettings", XRFDC_ADC_TILE) == 16);
    if (ok) ok = (countCallsForType("XRFdc_UpdateEvent", XRFDC_ADC_TILE) == 32);
    // Still zero, and it is worth a line because the sweep did gain a raw
    // register read. The power-up status read the compensating reset is
    // gated on is XRFdc_RDReg, which the fixture records under that name.
    // XRFdc_ReadReg is a different recorded name and the two are one
    // character apart, so this zero is a statement about the diagnostic
    // reads and not an accident of spelling.
    if (ok) ok = (gScript.countCalls("XRFdc_ReadReg") == 0);
    // The raw read really did happen, once per enabled tile, so the line
    // above cannot be passing because the gate read was never issued.
    if (ok) ok = (countCallsForType("XRFdc_RDReg", XRFDC_ADC_TILE) == 4);

    // A failing reset still reports through the diagnostic path and not
    // through the rejection path.
    if (ok) {
        PyRFdcPtr failing = PyRFdc::create();

        gScript.reset();
        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

        rim::TransactionPtr tran = driveWrite(failing, kResetAllAdc, 1);
        const std::string msg = tran->errorStrValue();

        ok = tran->errorStrCalled() && !tran->doneCalled();
        if (ok) ok = (msg.compare(0, 8, "Reset(-1") == 0);
        if (ok) ok = (recordFor(msg, "ADC3").find("XRFdc_Reset") != std::string::npos);
        if (ok) ok = (msg.find("driver unusable") == std::string::npos);
    }

    // And a metal log level write still reaches libmetal on a live driver.
    if (ok) {
        PyRFdcPtr live = PyRFdc::create();

        gScript.reset();

        rim::TransactionPtr tran = driveWrite(live, kMetalLogLevel, 1);

        ok = tran->doneCalled() && !tran->errorStrCalled();
        if (ok) ok = (gScript.countCalls("metal_set_log_level") == 1);
    }

    if (!ok) {
        fprintf(stderr, "live driver: clean done=%u err=%u, ADC reset=%zu check=%zu pll=%zu\n",
                clean->doneCalls(), clean->errorStrCalls(),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_CheckTileEnabled", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE));
    }

    runCheck("a live driver is unaffected by the guard", ok);
}

/* ------------------------------------------------------------------------ */
/* The read-only register that reports the constructor failure reason.       */
/*                                                                           */
/* A refusal message reaches whoever issued the transaction that was         */
/* refused. A register reaches anything that can read the address space,     */
/* including a snapshot tool walking the map and a host that has not tried   */
/* a converter command yet. The value is one word at 0x1200C, the next free  */
/* offset after the three existing debug registers, and reading it performs  */
/* no driver access at all, which is what lets a dead driver answer it.      */
/* ------------------------------------------------------------------------ */

//! The pre-existing offsets the new branch must not disturb, and the first
//! address of the tile decode that the chain must still fall through to.
const uint64_t kDoubleTestUpper = 0x13004;
const uint64_t kFirstTileDecode = 0x0000;

/*
 * A dead driver reports which constructor step failed, as a value.
 */
void checkReasonRegisterReportsTheFailedStep() {
    PyRFdcPtr device = createDeadDevice("metal_init");

    rim::TransactionPtr tran = driveRead(device, kInitFailReason);

    bool ok = tran->doneCalled() && !tran->errorStrCalled();
    if (ok) ok = (tran->getWord(0) == uint32_t(PYRFDC_INIT_FAIL_METAL_INIT));

    if (!ok) {
        fprintf(stderr, "reason register dead: word=%u done=%u err=%u text '%s'\n",
                tran->getWord(0), tran->doneCalls(), tran->errorStrCalls(),
                tran->errorStrValue().c_str());
    }

    runCheck("reason register reports the failed step", ok);
}

/*
 * A live driver reports the ok value, so a host reading this register on a
 * healthy board sees a positive answer rather than an absence of one.
 */
void checkReasonRegisterReadsOkOnLiveDriver() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr tran = driveRead(device, kInitFailReason);

    bool ok = tran->doneCalled() && !tran->errorStrCalled();
    if (ok) ok = (tran->getWord(0) == uint32_t(PYRFDC_INIT_OK));

    if (!ok) {
        fprintf(stderr, "reason register live: word=%u done=%u err=%u text '%s'\n",
                tran->getWord(0), tran->doneCalls(), tran->errorStrCalls(),
                tran->errorStrValue().c_str());
    }

    runCheck("reason register reads ok on a live driver", ok);
}

/*
 * The register is read only, and a refused write changes nothing.
 *
 * Driven on a live driver, where the guard returns immediately, so what
 * refuses the write is the register body itself and not the guard. A write
 * that was quietly accepted would let a host overwrite the one field that
 * says whether the driver came up.
 */
void checkReasonRegisterIsReadOnly() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr write = driveWrite(device, kInitFailReason, 0xDEADBEEFu);
    rim::TransactionPtr read = driveRead(device, kInitFailReason);

    bool ok = write->errorStrCalled() && !write->doneCalled();
    if (ok) ok = !write->errorStrValue().empty();
    if (ok) ok = read->doneCalled() && !read->errorStrCalled();
    if (ok) ok = (read->getWord(0) == uint32_t(PYRFDC_INIT_OK));

    if (!ok) {
        fprintf(stderr, "reason register read only: write err=%u done=%u, read word=%u\n",
                write->errorStrCalls(), write->doneCalls(), read->getWord(0));
    }

    runCheck("reason register is read only", ok);
}

/*
 * Reading the register reaches nothing in the driver.
 *
 * This is the property the whole register turns on: a body that touched the
 * driver instance could not answer on the instance this register exists to
 * describe. Asserted on the recorded driver call list on both a live and a
 * dead instance, and paired with a clean completion, because an offset that
 * was never decoded at all would satisfy an empty call list on its own.
 */
void checkReasonRegisterPerformsNoDriverAccess() {
    gScript.reset();
    PyRFdcPtr live = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr liveRead = driveRead(live, kInitFailReason);

    bool ok = liveRead->doneCalled() && !liveRead->errorStrCalled();
    if (ok) ok = gScript.calls.empty();

    if (ok) {
        PyRFdcPtr dead = createDeadDevice("metal_init");

        rim::TransactionPtr deadRead = driveRead(dead, kInitFailReason);

        ok = deadRead->doneCalled() && !deadRead->errorStrCalled();
        if (ok) ok = gScript.calls.empty();
    }

    if (!ok) {
        fprintf(stderr, "reason register driver access: %zu recorded call(s)\n",
                gScript.calls.size());
    }

    runCheck("reason register performs no driver access", ok);
}

/*
 * The new branch disturbs nothing already in the chain.
 *
 * Green before this task as well as after, and that is what it is for: the
 * branch is inserted into a flat else chain whose last arm performs the
 * tile decode for every address below 0x10000, so a branch placed after
 * that arm would be unreachable and a range written too wide would swallow
 * a neighbour. The five pre-existing offsets either side of the new one are
 * re-read here, and so is the first address the tile decode owns.
 */
void checkReasonOffsetDoesNotCollide() {
    const uint32_t pattern = 0x5A3CC3A5u;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    // The scratchpad still stores and returns a word of its own.
    driveWrite(device, kScratchPad, pattern);
    rim::TransactionPtr scratch = driveRead(device, kScratchPad);

    bool ok = scratch->doneCalled() && !scratch->errorStrCalled();
    if (ok) ok = (scratch->getWord(0) == pattern);

    // The metal log level and the metal error bypass still round-trip a
    // boolean of their own, and neither reads as the reason register.
    if (ok) {
        driveWrite(device, kMetalLogLevel, 1);
        rim::TransactionPtr logLevel = driveRead(device, kMetalLogLevel);

        ok = logLevel->doneCalled() && !logLevel->errorStrCalled();
        if (ok) ok = (logLevel->getWord(0) == 1);
    }
    if (ok) {
        driveWrite(device, kIgnoreMetalError, 1);
        rim::TransactionPtr bypass = driveRead(device, kIgnoreMetalError);

        ok = bypass->doneCalled() && !bypass->errorStrCalled();
        if (ok) ok = (bypass->getWord(0) == 1);

        // Put it back, so nothing after this claim inherits the bypass.
        driveWrite(device, kIgnoreMetalError, 0);
    }

    // The double test pair still answers on both of its words.
    if (ok) {
        rim::TransactionPtr lower = driveRead(device, kDoubleTestLower);
        rim::TransactionPtr upper = driveRead(device, kDoubleTestUpper);

        ok = lower->doneCalled() && !lower->errorStrCalled();
        if (ok) ok = upper->doneCalled() && !upper->errorStrCalled();
    }

    // And the chain still falls through to the tile decode: the first
    // address below 0x10000 is ADC tile 0's startup command, whose read
    // hands back one and executes nothing.
    if (ok) {
        gScript.calls.clear();

        rim::TransactionPtr tile = driveRead(device, kFirstTileDecode);

        ok = tile->doneCalled() && !tile->errorStrCalled();
        if (ok) ok = (tile->getWord(0) == 1);
        if (ok) ok = (gScript.countCalls("XRFdc_StartUp") == 0);
    }

    if (!ok) {
        fprintf(stderr, "reason offset collision: scratchpad word=%u\n", scratch->getWord(0));
    }

    runCheck("reason offset does not collide", ok);
}

/* ------------------------------------------------------------------------ */
/* The admitted block, read before anything is written to it.                */
/*                                                                           */
/* The guard keeps five offsets answerable on a dead driver so a host can     */
/* ask over the same transport why the driver is dead. Four of the members    */
/* behind them are assigned in the constructor's local variable block, which  */
/* every early return skips, so on exactly the paths the guard exists for     */
/* those reads hand back whatever the storage held unless the declarations    */
/* say otherwise. Reading with nothing written first is the only order in     */
/* which that is observable: the round-trip claim above writes before it      */
/* reads, which is correct for a round trip and is what hid this.             */
/* ------------------------------------------------------------------------ */

//! The five offsets the guard admits on a dead driver, in the order the
//! claims below read them.
const uint64_t kAdmittedBlock[5] = {kMetalLogLevel, kIgnoreMetalError, kScratchPad,
                                    kDoubleTestLower, kDoubleTestUpper};

//! Print what the admitted block handed back, so a failing run says which of
//! the five reads moved rather than only that the claim is red.
void reportAdmittedBlock(const char *label, const uint32_t *words, const uint32_t *errs) {
    fprintf(stderr,
            "%s: read back 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X, "
            "err counts %u %u %u %u %u\n",
            label, words[0], words[1], words[2], words[3], words[4], errs[0], errs[1], errs[2],
            errs[3], errs[4]);
}

/*
 * A dead driver answers the admitted block with the declared values.
 *
 * Driven in storage this harness prefilled itself, because an instance from
 * the allocator reads back as zero in practice and this claim would then pass
 * with the members uninitialized. Nothing is written to any of the five
 * offsets before they are read.
 */
void checkDeadDriverAnswersAdmittedBlockBeforeWrite() {
    PyRFdcPtr device = createDeadDeviceInDirtyStorage("metal_init");

    uint32_t words[5] = {0, 0, 0, 0, 0};
    uint32_t errs[5] = {0, 0, 0, 0, 0};
    bool ok = true;

    for (size_t i = 0; i < 5; i++) {
        rim::TransactionPtr tran = driveRead(device, kAdmittedBlock[i]);

        words[i] = tran->getWord(0);
        errs[i] = tran->errorStrCalls();

        if (!tran->doneCalled() || tran->errorStrCalled()) ok = false;
        // Zero is what the four declared values produce: false for both
        // booleans, zero for the scratchpad, and both words of a positive
        // zero double.
        if (words[i] != 0) ok = false;
    }

    if (!ok) reportAdmittedBlock("admitted block before write", words, errs);

    runCheck("a dead driver answers the admitted block before anything is written", ok);
}

/*
 * The declined configuration initialize path answers the block too.
 *
 * That path is a different control flow through the constructor than the
 * bail-outs above: it returns from further down, past three steps that
 * reported success. It skips the same local variable block all the same, so
 * a fix that covered the bail-outs and not this one is possible and this
 * claim is what rules it out.
 *
 * The write at the end pins the other half: this path is readable where the
 * guard says it should be and refused where it should be, and the refusal
 * still names the not-completed outcome.
 */
void checkDeclinedCfgInitializeKeepsAdmittedBlockReadable() {
    PyRFdcPtr device = createDeadDeviceInDirtyStorage("XRFdc_CfgInitialize");

    uint32_t words[5] = {0, 0, 0, 0, 0};
    uint32_t errs[5] = {0, 0, 0, 0, 0};
    bool ok = true;

    for (size_t i = 0; i < 5; i++) {
        rim::TransactionPtr tran = driveRead(device, kAdmittedBlock[i]);

        words[i] = tran->getWord(0);
        errs[i] = tran->errorStrCalls();

        if (!tran->doneCalled() || tran->errorStrCalled()) ok = false;
        if (words[i] != 0) ok = false;
    }

    if (!ok) reportAdmittedBlock("admitted block on declined cfg initialize", words, errs);

    if (ok) {
        rim::TransactionPtr refused = driveWrite(device, kResetAllAdc, 1);
        const std::string msg = refused->errorStrValue();

        ok = refused->errorStrCalled() && !refused->doneCalled();
        if (ok) ok = (msg.find(kStepNotCompleted) != std::string::npos);

        if (!ok) {
            fprintf(stderr, "declined cfg initialize refusal: err=%u done=%u, text '%s'\n",
                    refused->errorStrCalls(), refused->doneCalls(), msg.c_str());
        }
    }

    runCheck("a declined configuration initialize leaves the admitted block readable", ok);
}

/*
 * An admitted read changes nothing, so a retry gets the same answer.
 *
 * A host polling one of these offsets to find out why the driver is dead
 * needs the same word each time, and a body that mutated its own member on a
 * read would hand back a different one. Only the equality is asserted here:
 * the claim above owns the declared value, and a claim asserting both would
 * move for two reasons and its one log line could not say which.
 */
void checkAdmittedReadOnDeadDriverMutatesNothing() {
    PyRFdcPtr device = createDeadDeviceInDirtyStorage("metal_init");

    uint32_t first[5] = {0, 0, 0, 0, 0};
    uint32_t second[5] = {0, 0, 0, 0, 0};
    uint32_t errs[5] = {0, 0, 0, 0, 0};
    bool ok = true;

    for (size_t i = 0; i < 5; i++) {
        rim::TransactionPtr one = driveRead(device, kAdmittedBlock[i]);
        rim::TransactionPtr two = driveRead(device, kAdmittedBlock[i]);

        first[i] = one->getWord(0);
        second[i] = two->getWord(0);
        errs[i] = one->errorStrCalls() + two->errorStrCalls();

        if (one->errorStrCalled() || two->errorStrCalled()) ok = false;
        if (first[i] != second[i]) ok = false;
    }

    if (!ok) {
        reportAdmittedBlock("admitted read repeated, first pass", first, errs);
        reportAdmittedBlock("admitted read repeated, second pass", second, errs);
    }

    runCheck("an admitted read on a dead driver mutates nothing", ok);
}

/* ------------------------------------------------------------------------ */
/* What an instance does on its way out.                                     */
/*                                                                           */
/* The transaction guard closed the surface a host can reach: nothing gets   */
/* through an instance the driver declined to configure. Teardown is the     */
/* surface it did not close. The destructor runs on every construction       */
/* outcome, so on the ones where the constructor already released libmetal   */
/* on its way out it releases a second time, and on all of them it hands the */
/* driver instance to the registration entry point, which is the same reach  */
/* through an unconfigured instance that the guard refuses everywhere else.  */
/*                                                                           */
/* Every other claim in this file reads the recorded call list while the     */
/* instance is still alive, and the comment on                               */
/* checkNoDriverCallFollowsAFailedCfgInitialize states the reason: the       */
/* destructor appends entries of its own, so a count taken after the         */
/* instance went out of scope would be about teardown rather than about      */
/* construction. The three claims below invert that convention on purpose,   */
/* because teardown is exactly their subject. Each drops its instance inside */
/* its own body and reads afterwards. That is not an oversight for a later   */
/* reader to tidy back into line with the claims above.                      */
/* ------------------------------------------------------------------------ */

//! The prefix every recorded driver call entry begins with. The recorded
//! form is name/type/tile/block, so an anchored compare against this tests
//! the name field itself and cannot match a libmetal entry or a substring
//! further along the line the way a free text search would.
const std::string kDriverCallPrefix = "XRFdc_";

//! One reachable constructor outcome, named by the call scripted to fail.
struct TeardownOutcome {
    const char *call;   //!< The entry point scripted to report non-success.
    const char *label;  //!< What that outcome is, for a failure diagnostic.
};

/*
 * The four constructor outcomes this build can reach, in the order the
 * constructor reaches them.
 *
 * Written as one table so a reader can see that the set is every outcome
 * this build has rather than an arbitrary sample of them. The baremetal
 * readiness lookup is the fifth and is deliberately absent: it sits inside a
 * block this build compiles out, so no claim here can drive it.
 */
const TeardownOutcome kTeardownOutcomes[4] = {
    {"metal_init", "the libmetal bring-up"},
    {"XRFdc_LookupConfig", "the driver configuration lookup"},
    {"XRFdc_RegisterMetal", "the libmetal device registration"},
    {"XRFdc_CfgInitialize", "the configuration initialize"},
};

/*
 * libmetal is brought up by one party and released once, counted over the
 * whole life of the object.
 *
 * The count spans the construction and the destruction together rather than
 * the destructor alone, and that is what lets one expected value cover all
 * four outcomes: on three of them the constructor performs the release and
 * on the fourth the destructor does. A claim counting only what the
 * destructor did would need a different expected number per path, which
 * restates the control flow instead of constraining it, and it would stay
 * green if a later edit moved the release from one end to the other without
 * changing the total.
 *
 * The recorded lists are kept, because the construction's own calls are half
 * of what is being counted. The instance is dropped explicitly before the
 * count is taken, so the destructor's calls are in the list as well.
 */
void checkLibmetalIsFinishedOnceOverAFailedConstruction() {
    size_t finishes[4] = {0, 0, 0, 0};

    bool ok = true;
    for (size_t s = 0; s < 4; s++) {
        PyRFdcPtr device = createDeadDeviceKeepingCalls(kTeardownOutcomes[s].call);
        device.reset();

        finishes[s] = gScript.countCalls("metal_finish");
        if (finishes[s] != 1) ok = false;
    }

    if (!ok) {
        fprintf(stderr, "libmetal finish count over construction and destruction:");
        for (size_t s = 0; s < 4; s++) {
            fprintf(stderr, " %s=%zu%s", kTeardownOutcomes[s].label, finishes[s],
                    (s == 3) ? "\n" : ",");
        }
    }

    runCheck("libmetal is finished exactly once over a failed construction and its destruction",
             ok);
}

/*
 * An instance the driver never configured makes no driver call while it is
 * destroyed.
 *
 * The same invariant the transaction guard enforces on the surface a host
 * can reach, asserted at the other end of the object's life. The recorded
 * lists are cleared after the construction, so everything still in them when
 * the count is taken belongs to teardown alone.
 *
 * Two assertions rather than one. The anchored prefix compare catches any
 * driver entry point, including one a later edit adds, and the close count
 * is named separately because the close is the call that reaches into
 * libmetal carrying a pointer the registration handed back.
 */
void checkDeadDriverMakesNoDriverCallWhileDestroyed() {
    bool ok = true;
    std::string detail;

    for (size_t s = 0; s < 4; s++) {
        PyRFdcPtr device = createDeadDeviceKeepingCalls(kTeardownOutcomes[s].call);

        gScript.calls.clear();
        gScript.logErrors.clear();
        gScript.metalLogs.clear();

        device.reset();

        bool one = true;
        for (size_t i = 0; i < gScript.calls.size(); i++) {
            if (gScript.calls[i].compare(0, kDriverCallPrefix.size(), kDriverCallPrefix) == 0) {
                one = false;
            }
        }
        if (one) one = (gScript.countCalls("metal_device_close") == 0);

        if (!one) {
            ok = false;
            // The whole surviving list and not merely its size: on a red run
            // the useful datum is which calls teardown actually made.
            detail = std::string(kTeardownOutcomes[s].label) + ": teardown recorded";
            for (size_t i = 0; i < gScript.calls.size(); i++) {
                detail += " " + gScript.calls[i];
            }
            break;
        }
    }

    if (!ok) fprintf(stderr, "dead driver teardown: %s\n", detail.c_str());

    runCheck("a dead driver instance makes no driver call while it is destroyed", ok);
}

/*
 * A construction that completed still tears down exactly as it did.
 *
 * A guard on a change this work must not make, in the same sense as the
 * guard claims earlier in this file, and green from the moment it is
 * written. It exists to turn red if the teardown gate is written so that it
 * never opens: a flag that is declared and cleared but never raised would
 * leave every board that ever constructed the driver successfully holding
 * its metal device and its libmetal bring-up for the life of the process,
 * and nothing else in this suite would say so.
 */
void checkLiveDriverStillClosesItsDeviceOnTeardown() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();
    gScript.metalLogs.clear();

    device.reset();

    const size_t registrations = gScript.countCalls("XRFdc_RegisterMetal");
    const size_t closes = gScript.countCalls("metal_device_close");
    const size_t finishes = gScript.countCalls("metal_finish");

    bool ok = (registrations == 1);
    if (ok) ok = (closes == 1);
    if (ok) ok = (finishes == 1);

    if (!ok) {
        fprintf(stderr,
                "live driver teardown: %zu registration call(s), %zu close call(s), "
                "%zu libmetal finish call(s)\n",
                registrations, closes, finishes);
    }

    runCheck("a live driver still closes its device and finishes libmetal once", ok);
}

/* ------------------------------------------------------------------------ */
/* The metal error bypass, narrowed.                                         */
/*                                                                           */
/* The bypass at 0x12004 cleared the error string unconditionally, inside    */
/* the per-word loop and therefore before the emptiness test that decides    */
/* between a clean completion and an error. Left as it stood, setting one    */
/* published register would have discarded every tile report and every       */
/* driver-unusable refusal, which is a way to make a board that is failing   */
/* report that it is fine. The register is kept and its offset is            */
/* unchanged, because it is published on the host side with an offset in     */
/* the register map, and removing it would be an interface break.            */
/*                                                                           */
/* Two claims, and the second earns its keep as much as the first: a         */
/* narrowing that cleared nothing at all would have removed the register's   */
/* function rather than narrowed it.                                         */
/* ------------------------------------------------------------------------ */

//! An address inside the global block that no branch decodes, so the chain
//! reaches its terminal undefined-memory assignment. That message is not a
//! diagnostic this work protects, which is what makes it the right input
//! for the claim that the bypass still clears what it legitimately should.
//!
//! It was 0x12010 until the two clock distribution registers took 0x12010
//! and 0x12014, then 0x12018 until the per-tile cycle count took that, then
//! 0x1201C until the recovery counter took that, so it moves up again to the
//! first address past the published set that no branch decodes. The claims
//! below need an address that decodes to nothing at all, and a published
//! register that merely refuses a write is not that.
const uint64_t kUndecodedGlobal = 0x12020;

/*
 * With the bypass set, a failing global reset still reports its tiles.
 */
void checkIgnoreMetalErrorCannotClearAResetDiagnostic() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    rim::TransactionPtr bypass = driveWrite(device, kIgnoreMetalError, 1);
    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr tran = driveWrite(device, kResetAllAdc, 1);
    const std::string msg = tran->errorStrValue();

    bool ok = bypass->doneCalled() && !bypass->errorStrCalled();
    if (ok) ok = tran->errorStrCalled() && !tran->doneCalled();
    if (ok) ok = (msg.compare(0, 8, "Reset(-1") == 0);
    if (ok) ok = (recordFor(msg, "ADC3").find("XRFdc_Reset") != std::string::npos);
    // And the console copy survived too, which is the half a python
    // traceback does not carry.
    if (ok) ok = (gScript.logErrors.size() == 1) && (gScript.logErrors[0] == msg);

    // The refusal a dead driver reports is protected by the same route.
    if (ok) {
        PyRFdcPtr dead = createDeadDevice("metal_init");

        driveWrite(dead, kIgnoreMetalError, 1);

        rim::TransactionPtr refused = driveWrite(dead, kResetAllAdc, 1);

        ok = refused->errorStrCalled() && !refused->doneCalled();
        if (ok) {
            ok = (refused->errorStrValue().find(kStepMetalInit) != std::string::npos);
        }
    }

    if (!ok) {
        fprintf(stderr, "bypass vs reset diagnostic: err=%u done=%u, text '%s'\n",
                tran->errorStrCalls(), tran->doneCalls(), msg.c_str());
    }

    runCheck("ignore-metal-error cannot clear a reset diagnostic", ok);
}

/*
 * With the bypass set, an error that is not one of this driver's diagnostics
 * is still cleared, so the register still does the job it exists for.
 */
void checkIgnoreMetalErrorStillClearsAnUnprotectedError() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    // First establish that the same access does report an error with the
    // bypass off, so the pass below cannot come from an address that never
    // produced one.
    rim::TransactionPtr before = driveWrite(device, kUndecodedGlobal, 0);

    bool ok = before->errorStrCalled() && !before->doneCalled();

    if (ok) {
        driveWrite(device, kIgnoreMetalError, 1);

        // The line the access above logged belongs to the bypass-off case.
        gScript.logErrors.clear();

        rim::TransactionPtr after = driveWrite(device, kUndecodedGlobal, 0);

        ok = after->doneCalled() && !after->errorStrCalled();
        if (ok) ok = after->errorStrValue().empty();
        if (ok) ok = gScript.logErrors.empty();
    }

    if (!ok) {
        fprintf(stderr, "bypass vs unprotected error: before err=%u, %zu log line(s)\n",
                before->errorStrCalls(), gScript.logErrors.size());
    }

    runCheck("ignore-metal-error still clears an unprotected error", ok);
}

/* ------------------------------------------------------------------------ */
/* A driver return that is neither success nor the one failure value.        */
/*                                                                           */
/* Every guard in PyRFdc.cpp that tested a driver return used to ask whether */
/* the return was not the single failure value. XRFDC_SUCCESS is 0 and       */
/* XRFDC_FAILURE is 1, and the driver API is not documented to return only   */
/* those two, so any other value was read as a success and the guarded code  */
/* ran on it. The three claims below drive one representative site in the    */
/* constructor and one in the global reset sweep with each of the three      */
/* kinds of return, and assert on the recorded driver call list rather than  */
/* on a message, because what is at stake is whether the guarded code ran at */
/* all and a message would not distinguish that from a message that merely   */
/* differs.                                                                  */
/*                                                                           */
/* Only the first of the three is new behavior. The other two describe the   */
/* two cases the conversion did not change, and they are kept because a      */
/* conversion that also moved either of them would be a different change     */
/* from the one this work intends.                                           */
/*                                                                           */
/* Coverage limit, stated here rather than left to be discovered by someone  */
/* reading a green run as more than it is: these three claims exercise two   */
/* of the sixteen converted sites, one in the constructor and one in the     */
/* reset sweep. The other fourteen are covered by a grep over the production */
/* source, which proves no inequality comparison against the failure value   */
/* survives anywhere in the file, and by no claim here. A claim computed     */
/* over the production translation unit is not something this harness can    */
/* express, so that grep is the whole of the coverage for those fourteen.    */
/* ------------------------------------------------------------------------ */

/*
 * A return value that is neither XRFDC_SUCCESS nor XRFDC_FAILURE.
 *
 * Not taken from the driver headers, which name no third value: it stands
 * for the whole class of returns outside the documented pair, which is
 * exactly the class the old guard admitted and the new guard refuses.
 */
const int kThirdReturnValue = 2;

/*
 * The representative constructor site is XRFdc_CheckTileEnabled, whose
 * branch wraps the three default-configuration getters. The representative
 * reset sweep site is XRFdc_CheckBlockEnabled, whose branch wraps the
 * quadrature and mixer restore for that block. Both are chosen because the
 * code inside their branches makes further recorded driver calls, so
 * "the guarded code ran" is a fact the recorded call list carries.
 */
const char *const kConstructorGuardCall = "XRFdc_CheckTileEnabled";
const char *const kConstructorGuardedCall = "XRFdc_GetClockSource";
const char *const kSweepGuardCall = "XRFdc_CheckBlockEnabled";
const char *const kSweepGuardedCall = "XRFdc_SetQMCSettings";

//! How many times the constructor reached the code inside the guard at the
//! representative constructor site, with that site scripted to return
//! status.
size_t constructorGuardedCallsFor(int status) {
    gScript.reset();
    gScript.scriptFailure(kConstructorGuardCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, status);

    PyRFdcPtr device = PyRFdc::create();
    (void)device;

    return gScript.countCalls(kConstructorGuardedCall);
}

//! How many times a global ADC reset reached the code inside the guard at
//! the representative sweep site, with that site scripted to return status.
//! The device is constructed clean and the failure scripted afterwards, so
//! the count belongs to the sweep and not to the construction.
size_t sweepGuardedCallsFor(int status) {
    gScript.reset();

    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.scriptFailure(kSweepGuardCall, XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, status);

    driveWrite(device, kResetAllAdc, 1);

    return gScript.countCalls(kSweepGuardedCall);
}

/*
 * A third return value stops the call instead of being read as success.
 *
 * This is the behavior change. Before it, a driver call answering anything
 * other than XRFDC_FAILURE was treated as though it had answered
 * XRFDC_SUCCESS, so a tile that reported an enabled check with some other
 * status had its clock source, PLL, quadrature and mixer settings read or
 * written anyway.
 */
void checkThirdReturnValueStopsTheCall() {
    const size_t ctorCalls = constructorGuardedCallsFor(kThirdReturnValue);
    const size_t sweepCalls = sweepGuardedCallsFor(kThirdReturnValue);

    bool ok = (ctorCalls == 0);
    if (ok) ok = (sweepCalls == 0);

    if (!ok) {
        fprintf(stderr,
                "third return value: %zu %s call(s) after a constructor guard returned %d, "
                "%zu %s call(s) after a sweep guard returned %d\n",
                ctorCalls, kConstructorGuardedCall, kThirdReturnValue, sweepCalls,
                kSweepGuardedCall, kThirdReturnValue);
    }

    runCheck("a third return value now stops the call", ok);
}

/*
 * The case that already worked still works.
 *
 * Green before and after by design. A conversion that made the guard
 * stricter than intended, for instance by testing the wrong constant, would
 * turn this red rather than leaving the mistake to be found on a board.
 */
void checkSuccessStillProceeds() {
    const size_t ctorCalls = constructorGuardedCallsFor(XRFDC_SUCCESS);
    const size_t sweepCalls = sweepGuardedCallsFor(XRFDC_SUCCESS);

    bool ok = (ctorCalls > 0);
    if (ok) ok = (sweepCalls > 0);

    if (!ok) {
        fprintf(stderr,
                "success proceeds: %zu %s call(s) in the constructor, %zu %s call(s) in "
                "the sweep\n",
                ctorCalls, kConstructorGuardedCall, sweepCalls, kSweepGuardedCall);
    }

    runCheck("success still proceeds", ok);
}

/*
 * The failure value still stops the call.
 *
 * Also green before and after. It is the other half of the pin: the
 * conversion is required to leave the behavior of the documented failure
 * value exactly where it was, so only the undocumented returns move.
 */
void checkFailureConstantStillStopsTheCall() {
    const size_t ctorCalls = constructorGuardedCallsFor(XRFDC_FAILURE);
    const size_t sweepCalls = sweepGuardedCallsFor(XRFDC_FAILURE);

    bool ok = (ctorCalls == 0);
    if (ok) ok = (sweepCalls == 0);

    if (!ok) {
        fprintf(stderr,
                "failure constant stops: %zu %s call(s) in the constructor, %zu %s call(s) "
                "in the sweep\n",
                ctorCalls, kConstructorGuardedCall, sweepCalls, kSweepGuardedCall);
    }

    runCheck("the failure constant still stops the call", ok);
}

/* ------------------------------------------------------------------------ */
/* The board's clock distribution topology.                                  */
/*                                                                           */
/* A tile with no clock pin of its own takes its clock from another tile,    */
/* and nothing in this driver has ever known which tile that is. The         */
/* constructor now asks the driver once, decodes the answer into a per-tile  */
/* record, and publishes it through two read-only registers, so a host can   */
/* read the grouping the reset path is about to be built on rather than      */
/* infer it from the wizard configuration.                                   */
/* ------------------------------------------------------------------------ */

//! The two read-only registers the cached topology is published through.
const uint64_t kClkDistStatus = 0x12010;
const uint64_t kClkDistMap = 0x12014;

/*
 * Push this carrier's own topology onto the fixture, in the structure's own
 * terms.
 *
 * DAC 0 sources the distribution and the two edges are DAC 3 and ADC 3. In
 * package tile indices, where DAC n is 3 - n and ADC n is 7 - n, DAC 3 is 0
 * and ADC 3 is 4, so the inclusive range between the two edges is DAC 3,
 * DAC 2, DAC 1, DAC 0 and ADC 3, with DAC 0 the source. That is five tiles
 * on one clock and three ADC tiles on their own pins, which is what the IP
 * configuration on this carrier describes.
 */
void scriptThisCarriersDistribution() {
    XRFdcScriptDistribution dist;

    dist.sourceType = XRFDC_DAC_TILE;
    dist.sourceTileId = 0;
    dist.edgeTypes[0] = XRFDC_DAC_TILE;
    dist.edgeTypes[1] = XRFDC_ADC_TILE;
    dist.edgeTileIds[0] = 3;
    dist.edgeTileIds[1] = 3;

    gScript.distributions.push_back(dist);
}

/*
 * The same carrier, in the clock detect register's own terms.
 *
 * One register per tile at offset 0x80. No new fixture field is needed: the
 * XRFdc_RDReg stub returns the scripted value masked by the caller's mask,
 * and the production decode's mask keeps every bit used here.
 *
 * The arithmetic is written out so a reader can check these eight values
 * rather than trust them. A tile's source package index is 7 - i, where i
 * is the index of the bit pair carrying the single set bit. So:
 *
 *   ADC 0 sits at package 7 and takes its own clock, so i is 0:  0x0001
 *   ADC 1 sits at package 6 and takes its own clock, so i is 1:  0x0004
 *   ADC 2 sits at package 5 and takes its own clock, so i is 2:  0x0010
 *   every tile on the distribution names DAC 0 at package 3,
 *   so i is 4:                                                   0x0100
 *
 * and the tiles on the distribution are ADC 3, DAC 0, DAC 1, DAC 2 and
 * DAC 3, with DAC 0 naming itself because a master sources its own clock.
 */
void scriptThisCarriersClockDetect() {
    gScript.scriptRegister(XRFDC_ADC_TILE, 0, 0x80, 0x0001);
    gScript.scriptRegister(XRFDC_ADC_TILE, 1, 0x80, 0x0004);
    gScript.scriptRegister(XRFDC_ADC_TILE, 2, 0x80, 0x0010);
    gScript.scriptRegister(XRFDC_ADC_TILE, 3, 0x80, 0x0100);
    gScript.scriptRegister(XRFDC_DAC_TILE, 0, 0x80, 0x0100);
    gScript.scriptRegister(XRFDC_DAC_TILE, 1, 0x80, 0x0100);
    gScript.scriptRegister(XRFDC_DAC_TILE, 2, 0x80, 0x0100);
    gScript.scriptRegister(XRFDC_DAC_TILE, 3, 0x80, 0x0100);
}

/*
 * A scripted Gen3 topology travels from the driver shim into two registers.
 *
 * The two words are asserted literally rather than rebuilt from the same
 * arithmetic the driver uses, because a check that recomputed the encoding
 * would agree with whatever the encoding happened to be. 0x00010201 is the
 * documented API as the topology source, an IPType of 2 and one group.
 * 0x44444FFF is ADC 0, 1 and 2 ungrouped and ADC 3 plus all four DAC tiles
 * mastered by tile index 4, which is DAC 0.
 */
void checkClockDistributionTopologyIsCapturedAtConstruction() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    if (ok) ok = (status->getWord(0) == 0x00010201u);
    if (ok) ok = (map->getWord(0) == 0x44444FFFu);
    if (ok) ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 1);

    // Asked once at construction and not again per reset. The topology is
    // fixed by the IP, so a query on the reset path would cost a call that
    // can fail on a path that has to keep working when the board is already
    // degraded.
    if (ok) {
        driveWrite(device, kResetAllAdc, 1);
        ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 1);
    }

    if (!ok) {
        fprintf(stderr,
                "clock distribution capture: status=0x%08X map=0x%08X, %zu query call(s)\n",
                status->getWord(0), map->getWord(0),
                gScript.countCalls("XRFdc_GetClkDistribution"));
    }

    runCheck("the clock distribution topology is captured at construction", ok);
}

/*
 * A driver that reports a pre-Gen3 part is never asked for the distribution.
 *
 * The fixture is given a topology it is perfectly willing to serve, so the
 * only reason the query is not made is the runtime gate. That is what makes
 * this a claim about the gate rather than a restatement of an empty fixture.
 *
 * It earns its keep on a file every SLAC RFSoC project consumes. An
 * unguarded call prints "Requested functionality not available for this IP"
 * on the console of every first and second generation board at every bridge
 * start, and this claim is what stops that being reintroduced by someone who
 * reads the gate as redundant with the failure fallback.
 *
 * The expected status word moved from 0x00000100 to 0x00000102 when the raw
 * clock detect decode landed, and the move is deliberate rather than a
 * number that drifted. A pre-Gen3 driver no longer stops at the gate: it
 * reaches the raw decode, which records itself as the topology source
 * whatever it finds, so the source field now reads 2 instead of 0. Nothing
 * was found here, because this claim scripts no clock detect register, so
 * the group count is still zero and the map is still every tile ungrouped.
 * The load-bearing half of this claim is unchanged and is the zero call
 * count on the documented query.
 */
void checkPreGen3DriverIsNeverAskedForTheDistribution() {
    gScript.reset();
    gScript.ipType = 1;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 0);
    if (ok) ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // The raw decode as the source, an IPType of 1 and no groups.
    if (ok) ok = (status->getWord(0) == 0x00000102u);
    // Every tile ungrouped.
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    if (!ok) {
        fprintf(stderr, "pre-Gen3 gate: status=0x%08X map=0x%08X, %zu query call(s)\n",
                status->getWord(0), map->getWord(0),
                gScript.countCalls("XRFdc_GetClkDistribution"));
    }

    runCheck("a pre-Gen3 driver is never asked for the distribution", ok);
}

/*
 * A refused query leaves every tile ungrouped.
 *
 * The call count is asserted at exactly one, so this claim is about the
 * refusal and cannot be satisfied by the runtime gate above. The cache keeps
 * the values it was declared with because the constructor writes it only
 * inside a success test, which is the whole of the no-distribution fallback:
 * there is no separate branch for it to be written wrong in.
 */
void checkRefusedDistributionQueryLeavesEveryTileUngrouped() {
    gScript.reset();
    gScript.ipType = 2;
    gScript.scriptFailure("XRFdc_GetClkDistribution", XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 1);
    if (ok) ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // No source, an IPType of 2 and no groups.
    if (ok) ok = (status->getWord(0) == 0x00000200u);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    if (!ok) {
        fprintf(stderr, "refused query: status=0x%08X map=0x%08X, %zu query call(s)\n",
                status->getWord(0), map->getWord(0),
                gScript.countCalls("XRFdc_GetClkDistribution"));
    }

    runCheck("a refused distribution query leaves every tile ungrouped", ok);
}

/*
 * The two topology sources produce the same map for the same board.
 *
 * This is the cross-check, and it is why this claim is worth its lines.
 * 0x44444FFF is the same literal the documented-API claim above asserts, so
 * the two sources are pinned against one shared expected word rather than
 * against two expectations chosen independently. A raw decode that got the
 * bit-pair direction backwards, took a different mask, or mapped package
 * indices the other way round would produce a different map and would be
 * caught here rather than on a board, where the only instrument is a
 * register read that costs an exclusive hold of the carrier.
 *
 * The IPType is zero, so the documented call is never issued and the map can
 * only have come from the raw decode. That is asserted directly as well.
 */
void checkRawDecodeNamesTheSameMasterAsTheDocumentedApi() {
    gScript.reset();
    gScript.ipType = 0;
    scriptThisCarriersClockDetect();

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // The same word the documented API produced for the same carrier.
    if (ok) ok = (map->getWord(0) == 0x44444FFFu);
    // The raw decode as the source, an IPType of 0 and one group.
    if (ok) ok = (status->getWord(0) == 0x00010002u);
    if (ok) ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 0);

    if (!ok) {
        fprintf(stderr,
                "raw decode cross-check: status=0x%08X map=0x%08X, %zu query call(s)\n",
                status->getWord(0), map->getWord(0),
                gScript.countCalls("XRFdc_GetClkDistribution"));
    }

    runCheck("the raw decode names the same master as the documented api", ok);
}

/*
 * A clock detect register nothing ever programmed yields no distribution.
 *
 * This is the fallback guarantee, and it is the outcome this phase gets if
 * the IP turns out never to initialize that register on a board whose
 * software never called the distribution setter. Every tile stays ungrouped,
 * so the reset path behaves exactly as it does today, and the source field
 * still records that a decode was attempted. A board with no distribution
 * and a board that was never asked stay different facts.
 */
void checkUnprogrammedClockDetectRegisterYieldsNoDistribution() {
    gScript.reset();
    gScript.ipType = 0;

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // The raw decode as the source, an IPType of 0 and no groups.
    if (ok) ok = (status->getWord(0) == 0x00000002u);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    if (!ok) {
        fprintf(stderr, "unprogrammed clock detect: status=0x%08X map=0x%08X\n",
                status->getWord(0), map->getWord(0));
    }

    runCheck("an unprogrammed clock detect register yields no distribution", ok);
}

/*
 * A driver that answered the documented call is never given a second opinion.
 *
 * The clock detect registers are scripted with the real values from the
 * cross-check above, so they are perfectly willing to answer and the only
 * reason they are not read is the layering. The documented call is scripted
 * to fail, which is the case that would tempt a fall-through: a board that
 * answered the documented question and answered it with an error is telling
 * the driver something, and a raw register read cannot correct that.
 *
 * Asserted over the recorded call list rather than over a return value,
 * because the recorded form of a register primitive carries the offset in
 * its fourth field, so absence at one offset is a fact the list can state
 * and a return value cannot.
 */
void checkGen3DriverIsNeverGivenTheRawDecode() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersClockDetect();
    gScript.scriptFailure("XRFdc_GetClkDistribution", XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    PyRFdcPtr device = PyRFdc::create();

    // Taken before any transaction is driven, so the absence is a statement
    // about construction and not about what a read happened to do after it.
    const size_t recorded = gScript.calls.size();
    bool ok = true;

    for (uint32_t type = 0; ok && (type < 2); type++) {
        for (uint32_t tile = 0; ok && (tile < 4); tile++) {
            ok = (firstCallAt("XRFdc_RDReg", type, tile, 0x80) == recorded);
        }
    }

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    if (ok) ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // No source at all, an IPType of 2 and no groups. Not the raw decode's
    // source value, which is the whole claim.
    if (ok) ok = (status->getWord(0) == 0x00000200u);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    if (!ok) {
        fprintf(stderr, "gen3 layering: status=0x%08X map=0x%08X, %zu recorded call(s)\n",
                status->getWord(0), map->getWord(0), recorded);
    }

    runCheck("a gen3 driver is never given the raw decode", ok);
}

/*
 * A driver that filled the caller's array and then refused is still no
 * distribution.
 *
 * The two refusal claims above both leave the fixture's distribution list
 * empty, so under them the array the getter hands back is a zeroed one whose
 * every slot already carries the unused sentinel. A stub that filled that
 * array before reporting its failure would therefore still produce no group,
 * and both claims would keep passing against a production body that had
 * stopped guarding the cache write. This claim removes that hole: the
 * fixture is given this carrier's real topology and is told to write it out
 * before it reports the scripted failure, so the only thing left standing
 * between a filled array and a cached topology is the production
 * XRFDC_SUCCESS test around the cache write.
 *
 * That is what makes this claim red only when that success test is removed,
 * or when the cache write moves outside it, and it is the protection the
 * earlier form of the refusal claim could not reach.
 */
void checkFilledDistributionArrayThatWasRefusedIsStillNoDistribution() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();
    gScript.distributionFillsOnRefusal = true;
    gScript.scriptFailure("XRFdc_GetClkDistribution", XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY,
                          XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    bool ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 1);
    if (ok) ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // No source, an IPType of 2 and no groups, exactly as for a driver that
    // wrote nothing at all.
    if (ok) ok = (status->getWord(0) == 0x00000200u);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    fprintf(stderr, "filled then refused: status=0x%08X map=0x%08X, %zu query call(s)\n",
            status->getWord(0), map->getWord(0),
            gScript.countCalls("XRFdc_GetClkDistribution"));

    runCheck("a filled distribution array that was refused is still no distribution", ok);
}

/*
 * A generation outside the range this driver knows how to ask is asked
 * nothing at all.
 *
 * 255 is not a hypothetical. It is what the installed image read out of
 * RFdc_Config.IPType on this project's own carrier, from a device tree node
 * whose parameter property was zero bytes, so the driver was left reading a
 * parameter block nothing had filled. An unbounded lower test admits it, and
 * it then selects the documented getter, which is the one branch with no
 * range check of its own in front of it.
 *
 * The fixture here is deliberately willing to answer either question: this
 * carrier's topology is pushed for the documented getter and this carrier's
 * clock detect registers are scripted for the raw decode. So the only reason
 * neither was asked is the gate, which is what makes this a claim about the
 * gate rather than a restatement of an empty fixture.
 *
 * The absence of the clock detect reads is asserted over the recorded call
 * list rather than over a return value, for the reason the layering claim
 * above gives: the recorded form of a register primitive carries the offset
 * in its fourth field, so absence at one offset is a fact the list can state.
 */
void checkOutOfRangeGenerationIsAskedNothingAtAll() {
    gScript.reset();
    gScript.ipType = 255;
    scriptThisCarriersDistribution();
    scriptThisCarriersClockDetect();

    PyRFdcPtr device = PyRFdc::create();

    // Taken before any transaction is driven, so the absence is a statement
    // about construction and not about what a read happened to do after it.
    const size_t recorded = gScript.calls.size();
    bool ok = (gScript.countCalls("XRFdc_GetClkDistribution") == 0);

    for (uint32_t type = 0; ok && (type < 2); type++) {
        for (uint32_t tile = 0; ok && (tile < 4); tile++) {
            ok = (firstCallAt("XRFdc_RDReg", type, tile, 0x80) == recorded);
        }
    }

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    if (ok) ok = status->doneCalled() && !status->errorStrCalled();
    if (ok) ok = map->doneCalled() && !map->errorStrCalled();
    // The unsupported-generation source, a generation byte of 0xFF and no
    // groups. Neither the documented getter's source value nor the raw
    // decode's, which is the whole claim.
    if (ok) ok = (status->getWord(0) == 0x0000FF03u);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);

    fprintf(stderr,
            "out of range generation: status=0x%08X map=0x%08X, %zu query call(s), "
            "%zu recorded call(s) at construction\n",
            status->getWord(0), map->getWord(0),
            gScript.countCalls("XRFdc_GetClkDistribution"), recorded);

    runCheck("an out of range generation is asked nothing at all", ok);
}

/*
 * The two new branches disturb nothing already in the chain, and both
 * answer on an instance whose construction never produced a driver.
 *
 * Same shape as the reason-offset claim above and for the same reason: the
 * branches are inserted into a flat else chain, so one written too wide
 * would swallow a neighbour and one placed after the tile decode would be
 * unreachable. The read of the first address past the pair is what proves
 * the next register in this region is still absent rather than already
 * answered by something.
 */
void checkDistributionRegistersDoNotCollideAndAnswerADeadDriver() {
    const uint32_t pattern = 0x3C5AA5C3u;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    // The reason register and the double test pair still answer as before.
    rim::TransactionPtr reason = driveRead(device, kInitFailReason);
    rim::TransactionPtr lower = driveRead(device, kDoubleTestLower);
    rim::TransactionPtr upper = driveRead(device, kDoubleTestUpper);

    bool ok = reason->doneCalled() && !reason->errorStrCalled();
    if (ok) ok = (reason->getWord(0) == 0);
    if (ok) ok = lower->doneCalled() && !lower->errorStrCalled();
    if (ok) ok = upper->doneCalled() && !upper->errorStrCalled();

    // The first address past the pair still decodes to nothing at all.
    if (ok) {
        rim::TransactionPtr undecoded = driveRead(device, kUndecodedGlobal);

        ok = undecoded->errorStrCalled() && !undecoded->doneCalled();
        if (ok) ok = (undecoded->errorStrValue().find("Undefined memory") != std::string::npos);
    }

    // A write to either register is refused by the register itself, which
    // names itself in the refusal, and mutates nothing: the scratchpad is
    // read back afterwards to show that a refused word left the rest of the
    // block alone.
    if (ok) {
        driveWrite(device, kScratchPad, pattern);

        rim::TransactionPtr wrStatus = driveWrite(device, kClkDistStatus, 0xDEADBEEFu);
        rim::TransactionPtr wrMap = driveWrite(device, kClkDistMap, 0xDEADBEEFu);

        ok = wrStatus->errorStrCalled() && !wrStatus->doneCalled();
        if (ok) ok = (wrStatus->errorStrValue().find("ClkDistStatus") != std::string::npos);
        if (ok) ok = wrMap->errorStrCalled() && !wrMap->doneCalled();
        if (ok) ok = (wrMap->errorStrValue().find("ClkDistMap") != std::string::npos);

        if (ok) {
            rim::TransactionPtr scratch = driveRead(device, kScratchPad);

            ok = scratch->doneCalled() && !scratch->errorStrCalled();
            if (ok) ok = (scratch->getWord(0) == pattern);
        }
    }

    // And both are answered rather than refused on an instance whose
    // construction died at the libmetal device registration, which is the
    // whole point of admitting them: the IPType a host reads there is what
    // someone working out why this driver is dead actually needs.
    if (ok) {
        PyRFdcPtr dead = createDeadDevice("XRFdc_RegisterMetal");

        rim::TransactionPtr deadStatus = driveRead(dead, kClkDistStatus);
        rim::TransactionPtr deadMap = driveRead(dead, kClkDistMap);

        ok = deadStatus->doneCalled() && !deadStatus->errorStrCalled();
        if (ok) ok = deadMap->doneCalled() && !deadMap->errorStrCalled();
        // Never captured, no source, no groups, and every tile ungrouped.
        if (ok) ok = (deadStatus->getWord(0) == 0x0000FF00u);
        if (ok) ok = (deadMap->getWord(0) == 0xFFFFFFFFu);

        if (!ok) {
            fprintf(stderr, "distribution registers on a dead driver: status=0x%08X map=0x%08X\n",
                    deadStatus->getWord(0), deadMap->getWord(0));
        }
    }

    if (!ok) {
        fprintf(stderr, "distribution register collision: reason=0x%08X\n", reason->getWord(0));
    }

    // Cleared on the way out, because this claim is the only one here that
    // scripts a construction failure and the two meta-assertions at the end
    // of this file construct their instance before they reset the fixture.
    // Leaving the selector set would hand them a dead driver and make them
    // red for a reason that has nothing to do with what they assert.
    gScript.reset();

    runCheck("the distribution registers do not collide and answer a dead driver", ok);
}

/* ------------------------------------------------------------------------ */
/* One IPSM cycle per enabled tile, and the count published as a register.   */
/*                                                                           */
/* A global reset used to drive an enabled tile through its state machine    */
/* more than once: a conditional reset before the tile's PLL reconfigure, a  */
/* cycle the reconfigure performs internally through the same restart        */
/* primitive, and an unconditional reset in a second sweep loop afterwards.  */
/* Both explicit resets are gone. One compensating reset takes their place,  */
/* issued only when the reconfigure cannot have cycled the tile, which is    */
/* when the tile was not powered up or when the call did not return success. */
/*                                                                           */
/* The three claims below drive both sides of that predicate. A tile must    */
/* never come out of a global reset with zero cycles, and the only way to    */
/* state that as a fact rather than as an untested hope is to assert the     */
/* fire case and the no-fire case separately.                                */
/* ------------------------------------------------------------------------ */

//! The read-only per-tile cycle count. Four bits per tile at tile index
//! type * 4 + tile, so the four ADC nibbles are the low half of the word and
//! the four DAC nibbles the high half.
const uint64_t kResetCycleCount = 0x12018;

//! The tile common status register, whose power-up status bit is what the
//! compensating reset is gated on. Written as the bare offset here for the
//! same reason the three diagnostic offsets above are: the harness states
//! the register it means rather than importing a name from the code it is
//! asserting about.
const uint32_t kOffsetCommonStatus = 0x0228;

//! The power-up status value the fixture scripts into the register above to
//! make the masked read come back non-zero. The production read masks with
//! 0x00000004, so this is that bit and nothing else.
const uint32_t kPoweredUpStatus = 0x4;

/*
 * Every enabled tile gets exactly one cycle on the path a cold boot takes.
 *
 * The default fixture answers every power-up status read with zero, which is
 * how a board whose tiles have not been brought up reads, so the compensating
 * reset fires for all eight tiles across the two global resets. The four
 * resets per type here are that compensating reset and not the deleted
 * post-reset, which is worth saying because the count is the same as the
 * count the deleted code produced.
 */
void checkOneIpsmCyclePerEnabledTileOnTheHealthyPath() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // One per tile, all eight, and exactly one: a nibble of 2 would mean a
    // tile was cycled twice and a nibble of 0 would mean a tile a global
    // reset covered came out of it never having been cycled at all.
    if (ok) ok = (counts->getWord(0) == 0x11111111u);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 4);
    // The second global reset did not clear what the first one recorded.
    // The two are separate transactions and each clears only the tiles it
    // covers, which is the invariant the low half of the word above rests
    // on.
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_DAC_TILE) == 4);

    if (!ok) {
        fprintf(stderr, "healthy path cycles: counts=0x%08X, ADC reset=%zu DAC reset=%zu\n",
                counts->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE));
    }

    runCheck("one ipsm cycle per enabled tile on the healthy path", ok);
}

/*
 * A generation outside the known range leaves the board resetting exactly as
 * it did before grouping existed.
 *
 * Stated as a reset sequence rather than as a register value, because a
 * register value would say what the cache holds and this claim is about what
 * the two global resets then do with it. The same fixture as the claim about
 * the gate: a topology the documented getter would have served and clock
 * detect registers the raw decode would have served, with the reported
 * generation out of range, so no topology reaches the cache from either.
 *
 * Every tile is therefore ungrouped, each entry point owns its own four
 * tiles, and neither reaches across into the other type. That is the
 * pre-grouping behaviour every board without a distribution already depends
 * on, and it is what stops the new arm from being a way to break a board
 * that works today. Lodged here rather than beside the other gate claims
 * because it reads the per tile cycle count declared just above.
 */
void checkOutOfRangeGenerationStillResetsEveryTileAsBefore() {
    gScript.reset();
    gScript.ipType = 255;
    scriptThisCarriersDistribution();
    scriptThisCarriersClockDetect();

    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    const size_t adcSweepAdcResets = countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE);
    const size_t adcSweepDacResets = countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE);

    gScript.calls.clear();
    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    const size_t dacSweepDacResets = countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE);
    const size_t dacSweepAdcResets = countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE);

    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // One cycle for each of the eight tiles, and exactly one.
    if (ok) ok = (counts->getWord(0) == 0x11111111u);
    // Each entry point reset its own four tiles and none of the other type.
    if (ok) ok = (adcSweepAdcResets == 4);
    if (ok) ok = (adcSweepDacResets == 0);
    if (ok) ok = (dacSweepDacResets == 4);
    if (ok) ok = (dacSweepAdcResets == 0);

    fprintf(stderr,
            "out of range reset sequence: counts=0x%08X, adc sweep adc=%zu dac=%zu, "
            "dac sweep dac=%zu adc=%zu\n",
            counts->getWord(0), adcSweepAdcResets, adcSweepDacResets, dacSweepDacResets,
            dacSweepAdcResets);

    runCheck("an out of range generation still resets every tile as before", ok);
}

/*
 * A tile the PLL reconfigure already cycled receives no explicit reset, and
 * is still counted as having been cycled.
 *
 * This is the claim that makes the counter mean cycles rather than calls. A
 * counter incremented only where XRFdc_Reset is called would read zero here,
 * and a claim that asserted only the absence of the reset would pass against
 * such a counter while proving nothing about whether the tile was cycled at
 * all. The zero call count and the non-zero cycle count are therefore
 * asserted together in one claim rather than in two.
 */
void checkPoweredUpTileWithSucceedingReconfigureGetsNoExplicitReset() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    // Powered up, on all four ADC tiles, so the masked power-up read comes
    // back non-zero. The PLL reconfigure is left succeeding, which is the
    // other half of the condition under which the driver performs its own
    // internal cycle.
    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus, kPoweredUpStatus);
    }

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // Not one explicit reset anywhere in the sweep.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 0);
    // And yet every ADC tile is counted as cycled, because the reconfigure
    // cycled it. Only the ADC nibbles are set, because only the ADC group
    // was swept.
    if (ok) ok = (counts->getWord(0) == 0x00001111u);
    // The gate read really was taken, once per tile, so the zero above
    // cannot be a sweep that skipped the tiles entirely.
    if (ok) ok = (countCallsForType("XRFdc_RDReg", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE) == 4);

    if (!ok) {
        fprintf(stderr, "powered up tiles: counts=0x%08X, reset=%zu rdreg=%zu pll=%zu\n",
                counts->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_RDReg", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE));
    }

    runCheck("a powered up tile whose pll reconfigure succeeded gets no explicit reset", ok);
}

/*
 * The tile that was not powered up still gets its one cycle.
 *
 * Three tiles read as powered up and one does not, so the compensating reset
 * has to fire for exactly one of the four and for the right one. The tile
 * that is not powered up is the tile a literal removal of both explicit
 * resets would have left with zero cycles, and it is the wedged tile this
 * work exists to recover: the driver performs no internal cycle for a tile
 * whose initial power-up state reads zero.
 */
void checkWedgedTileStillGetsItsOneCycle() {
    const uint32_t wedged = 3;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus,
                               (tile == wedged) ? 0x0 : kPoweredUpStatus);
    }

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 1);
    // And it is the tile that was not powered up, not merely some tile.
    if (ok) ok = gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, wedged, XRFDC_SCRIPT_ANY);
    // All four nibbles read one: three tiles cycled by their reconfigure and
    // one by the compensating reset.
    if (ok) ok = (counts->getWord(0) == 0x00001111u);

    if (!ok) {
        fprintf(stderr, "wedged tile: counts=0x%08X, reset=%zu, saw tile %u=%d\n",
                counts->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE), wedged,
                gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, wedged, XRFDC_SCRIPT_ANY) ? 1 : 0);
    }

    runCheck("a wedged tile still gets its one cycle", ok);
}

/*
 * A powered up tile whose PLL reconfigure failed still gets its one cycle.
 *
 * The fire predicate is a conjunction of two facts, the power-up status and
 * the reconfigure's return, and the two claims above move only the first of
 * them. A predicate simplified to the power-up bit alone would satisfy both
 * of them and would leave this tile with no cycle at all, which is the row
 * of the per-tile table a board whose clock source reads external sits in:
 * the reconfigure fails its reference frequency check, performs no internal
 * cycle, and the tile needs the compensating reset exactly as much as a tile
 * that was never powered up does.
 *
 * Asserted on the reset call count and the tile it names rather than on the
 * cycle count, because a predicate that dropped the reconfigure status would
 * still increment the count for this tile while issuing nothing, so the
 * count alone cannot tell the two apart and the call list can.
 *
 * The failing tile's own nibble is the reserved not-exact value and not a
 * one. This tile reads powered up both before the reconfigure and after it
 * failed, so from the driver a refusal that never touched the tile and a
 * late failure that had already cycled it are the same observation, and the
 * register declines to pick between one and two. That is a statement about
 * what the count may claim and not about the reset, which still fires here
 * and is what this claim exists to hold in place.
 */
void checkPoweredUpTileWithFailingReconfigureStillGetsItsOneCycle() {
    const uint32_t failing = 1;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus, kPoweredUpStatus);
    }
    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);
    const std::string msg = adc->errorStrValue();

    // One reset, and only for the tile whose reconfigure failed. The other
    // three were powered up and reconfigured cleanly, so their one cycle
    // came from the reconfigure and they need nothing issued.
    bool ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 1);
    if (ok) ok = gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY);
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (counts->getWord(0) == 0x000011F1u);
    // The tile is still reported, and under the call that actually went
    // wrong rather than under the reset that followed it.
    if (ok) ok = adc->errorStrCalled() && !adc->doneCalled();
    if (ok) ok = (recordFor(msg, "ADC1").find("XRFdc_DynamicPLLConfig") != std::string::npos);

    if (!ok) {
        fprintf(stderr, "failing reconfigure: counts=0x%08X, reset=%zu, record '%s'\n",
                counts->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                recordFor(msg, "ADC1").c_str());
    }

    runCheck("a powered up tile whose pll reconfigure failed still gets its one cycle", ok);
}

/* ------------------------------------------------------------------------ */
/* The group ordered walk.                                                   */
/*                                                                           */
/* A global reset no longer walks the four tiles of its own type. It walks   */
/* the distribution groups whose master is of its own type, master first     */
/* and then that master's edge tiles whatever type they are, and afterwards  */
/* its own tiles that belong to no group at all.                             */
/*                                                                           */
/* On this carrier that divides the eight tiles unevenly. The ADC entry      */
/* point covers ADC 0, 1 and 2, which have their own clock pins. The DAC     */
/* entry point covers DAC 0 as the master of the distribution and then       */
/* ADC 3, DAC 1, DAC 2 and DAC 3 as its edges. ADC 3 is the tile with no     */
/* clock pin of its own, and the whole point of the division is that it is   */
/* never restarted by a call that cannot restart DAC 0 first.                */
/*                                                                           */
/* Four of the seven claims in this section run on that carrier topology     */
/* and all four push it through scriptThisCarriersDistribution, the one      */
/* definition of it in this file, so no two of them can drift into           */
/* describing different boards.                                              */
/* ------------------------------------------------------------------------ */

//! How many recorded calls are exactly this name and index tuple.
//!
//! Distinct from countCallsForType, which counts a whole tile type. A claim
//! that one tile was visited exactly once across two entry points needs the
//! tile id inside the comparison, because the type total says nothing about
//! how it was spread over the tiles.
size_t countExactCalls(const char *name, uint32_t type, uint32_t tile, uint32_t block) {
    const std::string want = XRFdcScript::describe(name, type, tile, block);
    size_t n = 0;

    for (size_t i = 0; i < gScript.calls.size(); i++) {
        if (gScript.calls[i] == want) n++;
    }
    return n;
}

//! One tile's nibble of a word encoded four bits per tile at tile index
//! type * 4 + tile, which is how both the cycle count and the distribution
//! map are laid out.
uint32_t tileNibble(uint32_t word, uint32_t type, uint32_t tile) {
    return (word >> (4 * ((type * 4) + tile))) & 0xFu;
}

//! Whether every nibble of a distribution map word that names a master names
//! a tile whose own nibble is that tile's own index.
//!
//! The ungrouped sentinel is skipped, because a tile in no group names
//! nobody. Every other nibble is a tile index, and the tile it points at has
//! to be a marked master, which in this encoding is a tile whose nibble holds
//! its own index. So the whole property is one read of the published word and
//! needs no reach into the driver's private state.
//!
//! Written over the word rather than over a pair of tiles, because the
//! property is about the map as a whole. A helper that took two tiles would
//! have to be driven from a list of pairs written down per fixture, and a
//! fixture whose pair list was incomplete would pass while describing a map
//! that broke the rule somewhere the list did not look.
bool everyNamedMasterNamesItself(uint32_t word) {
    for (uint32_t index = 0; index < 8; index++) {
        const uint32_t named = (word >> (4 * index)) & 0xFu;

        if (named == 0xFu) continue;
        if (((word >> (4 * named)) & 0xFu) != named) return false;
    }
    return true;
}

//! Position of the first enable probe against one tile. The probe is the
//! first driver call the sweep makes against every tile in its walk, so its
//! position is the tile's position in the walk. A reset position would be
//! the position of one conditional step inside the per-tile body instead,
//! which is a different question.
size_t probeAt(uint32_t type, uint32_t tile) {
    return firstCallAt("XRFdc_CheckTileEnabled", type, tile, XRFDC_SCRIPT_ANY);
}

/*
 * An ADC global reset declines to touch the tile whose clock master is a DAC.
 *
 * Read in the negative, this is the property the whole division of work
 * exists for. ADC 3 has no clock pin of its own and takes its clock from
 * DAC 0, so a call that cannot restart DAC 0 first must not restart ADC 3
 * at all. The ADC entry point therefore covers three tiles and not four,
 * and that is the invariant rather than an omission.
 *
 * The cycle count word is asserted alongside the call counts, because a
 * walk that included ADC 3 and happened to make no driver call against it
 * would satisfy every count here and would still have cleared its nibble.
 */
void checkAdcEntryPointDefersATileMasteredByADac() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 3);
    if (ok) ok = !gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY);
    if (ok) ok = (countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE) == 3);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 0);
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // ADC 0, 1 and 2 cycled once each and ADC 3 untouched, so its nibble is
    // still the zero the counter was declared with.
    if (ok) ok = (counts->getWord(0) == 0x00000111u);

    if (!ok) {
        fprintf(stderr,
                "adc defers: counts=0x%08X, adc reset=%zu pll=%zu dac reset=%zu, adc3 reset=%d\n",
                counts->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                static_cast<int>(gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 3,
                                                 XRFDC_SCRIPT_ANY)));
    }

    runCheck("the adc entry point defers a tile mastered by a dac", ok);
}

/*
 * The master of a distribution group is visited before every one of its
 * edge tiles.
 *
 * Asserted on the recorded call order and not on a return value, because
 * the order is the whole content of the property: a call that reset the
 * same five tiles in the wrong order would return exactly the same status
 * and would restart an edge tile while its clock source was still down.
 *
 * The enable probe is the position being compared rather than the reset,
 * because the probe is the first driver call made against every tile in the
 * walk. A reset is conditional on the power up state of the tile, so its
 * position would be the position of one step inside the per-tile body and
 * would go missing entirely for a tile the reconfigure already cycled.
 */
void checkGroupMasterIsResetBeforeItsEdgeTiles() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    const size_t master = probeAt(XRFDC_DAC_TILE, 0);

    bool ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = (master < probeAt(XRFDC_DAC_TILE, 1));
    if (ok) ok = (master < probeAt(XRFDC_DAC_TILE, 2));
    if (ok) ok = (master < probeAt(XRFDC_DAC_TILE, 3));
    if (ok) ok = (master < probeAt(XRFDC_ADC_TILE, 3));
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 1);
    if (ok) ok = gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY);
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // All four DAC tiles and ADC 3 at one cycle each, and the three ADC
    // tiles this entry point does not own still at zero.
    if (ok) ok = (counts->getWord(0) == 0x11111000u);

    if (!ok) {
        fprintf(stderr,
                "master first: counts=0x%08X, probes dac0=%zu dac1=%zu dac2=%zu dac3=%zu "
                "adc3=%zu, dac reset=%zu adc reset=%zu\n",
                counts->getWord(0), master, probeAt(XRFDC_DAC_TILE, 1),
                probeAt(XRFDC_DAC_TILE, 2), probeAt(XRFDC_DAC_TILE, 3),
                probeAt(XRFDC_ADC_TILE, 3),
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE));
    }

    runCheck("the group master is reset before its edge tiles", ok);
}

/*
 * Across the two entry points every tile still receives exactly one cycle.
 *
 * The division of work moves a tile from one call to the other, and the
 * thing that must survive the move is the total. This claim depends on an
 * invariant that is easy to break by accident: each call clears the cycle
 * counters of exactly the tiles it covers, so ADC 3's nibble survives the
 * ADC call that did not cover it and is written by the DAC call that did.
 * A clear of all eight tiles at the top of each sweep would leave this word
 * reading 0x11110000 while every other assertion in this file still passed.
 */
void checkBothEntryPointsTogetherGiveEveryTileOneCycle() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (counts->getWord(0) == 0x11111111u);

    if (!ok) {
        fprintf(stderr, "both entry points: counts=0x%08X\n", counts->getWord(0));
    }

    runCheck("both entry points together give every tile one cycle", ok);
}

/*
 * A tile a global reset declined to touch is named in the log.
 *
 * Exactly one line, and it has to carry both names. A line for every tile
 * on every reset would be noise a reader learns to skip, and no line at all
 * is the thing this claim exists to prevent: a board owner reading a log
 * that shows an ADC reset quietly passing over one of its four tiles sees
 * something indistinguishable from the fault this whole change is removing.
 *
 * The warning channel and not the error channel. A deferral is a correct
 * outcome, the transaction completes, and routing it through the diagnostic
 * error path would report a healthy reset as a failure on every boot. The
 * empty error list and the completed transaction are asserted here so that
 * cannot happen without a claim going red.
 */
void checkDeferredTileIsNamedInTheLog() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    // Construction itself emits no warning, so the count below belongs to
    // the reset and to nothing else.
    bool ok = gScript.logWarnings.empty();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);

    if (ok) ok = (gScript.logWarnings.size() == 1);
    if (ok) ok = (gScript.logWarnings[0].find("ADC3") != std::string::npos);
    if (ok) ok = (gScript.logWarnings[0].find("DAC0") != std::string::npos);
    if (ok) ok = gScript.logErrors.empty();
    if (ok) ok = adc->doneCalled() && !adc->errorStrCalled();

    if (!ok) {
        fprintf(stderr, "deferral log: %zu warning(s), %zu error(s), first '%s'\n",
                gScript.logWarnings.size(), gScript.logErrors.size(),
                gScript.logWarnings.empty() ? "" : gScript.logWarnings[0].c_str());
    }

    runCheck("a deferred tile is named in the log rather than skipped silently", ok);
}

/*
 * A tile two scripted distributions both reach belongs to one of them.
 *
 * The first slot to claim a tile keeps it. Without that rule the same tile
 * would take whichever master the iteration order happened to reach last,
 * and a tile that appeared in two groups would be walked by both entry
 * points and cycled twice inside one pair of global resets, which is the
 * defect this change removes wearing a different hat.
 *
 * The second distribution here names ADC 2 as its own source and ADC 3 as
 * its far edge, so its package range covers a tile the carrier group has
 * already taken. ADC 2 is free and becomes the master of a group of its
 * own; ADC 3 keeps DAC 0.
 */
void checkTileClaimedByTwoGroupsIsResetExactlyOnce() {
    XRFdcScriptDistribution overlap;

    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    overlap.sourceType = XRFDC_ADC_TILE;
    overlap.sourceTileId = 2;
    overlap.edgeTypes[0] = XRFDC_ADC_TILE;
    overlap.edgeTypes[1] = XRFDC_ADC_TILE;
    overlap.edgeTileIds[0] = 2;
    overlap.edgeTileIds[1] = 3;
    gScript.distributions.push_back(overlap);

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr map = driveRead(device, kClkDistMap);
    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    // ADC 3 still names DAC 0, tile index 4, rather than the ADC 2 the
    // later slot would have given it. ADC 2 names itself, tile index 2.
    if (ok) ok = (map->getWord(0) == 0x444442FFu);
    if (ok) ok = (tileNibble(map->getWord(0), XRFDC_ADC_TILE, 3) == 4);
    // One entry in the recorded list for a reset of ADC 3, across both
    // entry points, and one cycle in its nibble.
    if (ok) ok = (countExactCalls("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY) == 1);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 3) == 1);

    if (!ok) {
        fprintf(stderr, "two groups: map=0x%08X counts=0x%08X, adc3 reset entries=%zu\n",
                map->getWord(0), counts->getWord(0),
                countExactCalls("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY));
    }

    runCheck("a tile claimed by two groups is reset exactly once", ok);
}

/*
 * A distribution slot whose two edges are one tile is not a group of one.
 *
 * Both edges landing on the same package index is how the driver describes
 * a tile that sources its own clock and feeds nothing. It contributes no
 * group, the tile it names stays ungrouped, and both entry points therefore
 * walk their own four tiles exactly as they do on a board with no
 * distribution at all.
 */
void checkGroupWhoseTwoEdgesAreTheSameTileContributesNoGroup() {
    XRFdcScriptDistribution single;

    gScript.reset();
    gScript.ipType = 2;

    single.sourceType = XRFDC_DAC_TILE;
    single.sourceTileId = 2;
    single.edgeTypes[0] = XRFDC_DAC_TILE;
    single.edgeTypes[1] = XRFDC_DAC_TILE;
    single.edgeTileIds[0] = 2;
    single.edgeTileIds[1] = 2;
    gScript.distributions.push_back(single);

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr status = driveRead(device, kClkDistStatus);
    rim::TransactionPtr map = driveRead(device, kClkDistMap);
    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    // The group count field of the status word, which is one byte wide at
    // bit 16.
    bool ok = (((status->getWord(0) >> 16) & 0xFFu) == 0);
    if (ok) ok = (map->getWord(0) == 0xFFFFFFFFu);
    if (ok) ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 4);
    if (ok) ok = (counts->getWord(0) == 0x11111111u);

    if (!ok) {
        fprintf(stderr, "single tile slot: status=0x%08X map=0x%08X counts=0x%08X, "
                "adc reset=%zu dac reset=%zu\n",
                status->getWord(0), map->getWord(0), counts->getWord(0),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE));
    }

    runCheck("a group whose two edges are the same tile contributes no group", ok);
}

/*
 * A board with no clock distribution walks the order it always walked.
 *
 * This is the guarantee for every board this change is not trying to
 * affect, and it is checked as an order and a call count rather than as an
 * absence of crashes. Each entry point visits its own four tiles in
 * ascending tile id, which is the order the bare index loop produced, and
 * no tile is deferred so no board gains a per reset log line it did not
 * have before.
 */
void checkNoDistributionLeavesTheTileOrderUnchanged() {
    gScript.reset();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);

    const size_t adc0 = probeAt(XRFDC_ADC_TILE, 0);
    const size_t adc1 = probeAt(XRFDC_ADC_TILE, 1);
    const size_t adc2 = probeAt(XRFDC_ADC_TILE, 2);
    const size_t adc3 = probeAt(XRFDC_ADC_TILE, 3);

    gScript.calls.clear();

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);

    const size_t dac0 = probeAt(XRFDC_DAC_TILE, 0);
    const size_t dac1 = probeAt(XRFDC_DAC_TILE, 1);
    const size_t dac2 = probeAt(XRFDC_DAC_TILE, 2);
    const size_t dac3 = probeAt(XRFDC_DAC_TILE, 3);

    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = (adc0 < adc1) && (adc1 < adc2) && (adc2 < adc3);
    if (ok) ok = (dac0 < dac1) && (dac1 < dac2) && (dac2 < dac3);
    if (ok) ok = (counts->getWord(0) == 0x11111111u);
    if (ok) ok = gScript.logWarnings.empty();

    if (!ok) {
        fprintf(stderr,
                "no distribution: adc probes %zu %zu %zu %zu, dac probes %zu %zu %zu %zu, "
                "counts=0x%08X, %zu warning(s)\n",
                adc0, adc1, adc2, adc3, dac0, dac1, dac2, dac3, counts->getWord(0),
                gScript.logWarnings.size());
    }

    runCheck("no distribution leaves the tile order unchanged", ok);
}

/* ------------------------------------------------------------------------ */
/* A group whose master no decode marked.                                    */
/*                                                                           */
/* The ordering above rests on a cache in which every tile marked as an edge */
/* names a tile marked as a master. A decode can produce a cache that breaks */
/* that, and both decodes can, by different routes. When it happens the      */
/* group is invisible to the walk's first pass, master and edges alike fall  */
/* through to the pass that sorts by tile index, and on this carrier that    */
/* emits ADC 3 at index 3 ahead of DAC 0 at index 4. No anomalous register   */
/* value is needed for it and nothing in the walk notices.                   */
/*                                                                           */
/* The first two claims below are the two reproductions, one per decode. The */
/* third is the property both of them are instances of, asserted over the    */
/* published map word.                                                       */
/* ------------------------------------------------------------------------ */

/*
 * One half of the zero clock detect claim, labelled so a single red half is
 * identifiable.
 *
 * The same shape as runStepCheck above and for the same reason. The two
 * halves ask different questions of the same fixture, one about the order
 * the DAC entry point visits the group in and one about the ADC entry point
 * declining the group altogether, and an aggregate verdict over both would
 * go red for either and say nothing about which.
 */
void runZeroClockDetectCheck(const char *site, bool ok) {
    const std::string label =
        std::string("a master whose own clock detect reads zero is still reset first [") +
        site + "]";

    runCheck(label.c_str(), ok);
}

/*
 * A group master whose own clock detect register reads zero is reset first.
 *
 * This is the raw decode route to a master-less group, scripted exactly as
 * it was reproduced against the unfixed source. The fixture is this
 * carrier's own clock detect with one register changed: DAC 0, the master,
 * reads 0x0000. The decode treats that as naming no source, which is a
 * supported reading rather than a corrupt one, so DAC 0 stays ungrouped
 * while ADC 3 and the three other DAC tiles all name it.
 *
 * The two words are asserted as literals rather than rebuilt from the same
 * arithmetic the driver uses. 0x44444FFF is ADC 0, 1 and 2 ungrouped with
 * ADC 3 and all four DAC tiles naming tile index 4, which is DAC 0 naming
 * itself, and it is the same word this carrier produces when nothing is
 * wrong. 0x00010102 is the raw decode as the source, an IPType of 1 and one
 * group, so the group count rises with the master that was marked.
 *
 * Both the walk order and the reset call order are asserted. The probe
 * positions are the walk, and the reset positions are what the converter
 * actually receives. A walk that were reordered without the resets following
 * it, or the reverse, would satisfy one of the two and is not the property.
 *
 * The second half drives the ADC entry point over the same fixture and
 * asserts that no reset reaches ADC 3 at all. ADC 3 has no clock pin of its
 * own, so an ADC call that cannot restart DAC 0 first must not restart it,
 * and on the unfixed source that call owned ADC 3 only by accident of the
 * index sort.
 */
void checkMasterWithZeroClockDetectIsStillResetFirst() {
    {
        gScript.reset();
        gScript.ipType = 1;
        scriptThisCarriersClockDetect();
        gScript.scriptRegister(XRFDC_DAC_TILE, 0, 0x80, 0x0000);

        PyRFdcPtr device = PyRFdc::create();

        rim::TransactionPtr status = driveRead(device, kClkDistStatus);
        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        gScript.calls.clear();

        rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);

        const size_t masterProbe = probeAt(XRFDC_DAC_TILE, 0);
        const size_t edgeProbe = probeAt(XRFDC_ADC_TILE, 3);
        const size_t masterReset =
            firstCallAt("XRFdc_Reset", XRFDC_DAC_TILE, 0, XRFDC_SCRIPT_ANY);
        const size_t edgeReset =
            firstCallAt("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY);

        bool ok = status->doneCalled() && !status->errorStrCalled();
        if (ok) ok = map->doneCalled() && !map->errorStrCalled();
        if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
        if (ok) ok = (map->getWord(0) == 0x44444FFFu);
        if (ok) ok = (status->getWord(0) == 0x00010102u);
        if (ok) ok = (masterProbe < edgeProbe);
        if (ok) ok = (masterReset < edgeReset);

        if (!ok) {
            fprintf(stderr,
                    "zero clock detect, dac sweep: status=0x%08X map=0x%08X, "
                    "probes dac0=%zu adc3=%zu, resets dac0=%zu adc3=%zu\n",
                    status->getWord(0), map->getWord(0), masterProbe, edgeProbe,
                    masterReset, edgeReset);
        }

        runZeroClockDetectCheck("dac entry point", ok);
    }

    {
        gScript.reset();
        gScript.ipType = 1;
        scriptThisCarriersClockDetect();
        gScript.scriptRegister(XRFDC_DAC_TILE, 0, 0x80, 0x0000);

        PyRFdcPtr device = PyRFdc::create();

        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        gScript.calls.clear();

        rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);

        bool ok = adc->doneCalled() && !adc->errorStrCalled();
        if (ok) ok = (map->getWord(0) == 0x44444FFFu);
        if (ok) ok = !gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY);
        if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 3);

        if (!ok) {
            fprintf(stderr,
                    "zero clock detect, adc sweep: map=0x%08X, adc reset=%zu, adc3 reset=%d\n",
                    map->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                    static_cast<int>(gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 3,
                                                     XRFDC_SCRIPT_ANY)));
        }

        runZeroClockDetectCheck("adc entry point", ok);
    }
}

/*
 * A distribution slot whose source tile is outside its own edge range still
 * resets that source tile first.
 *
 * This is the documented getter route to the same master-less cache the
 * claim above reaches through the raw decode, and it is a separate claim
 * rather than a second half of that one on purpose. The two fixtures arrive
 * at the same cache shape through different decoders, so a fix written into
 * one decoder leaves exactly one of the two red, and a reader looking at a
 * single red verdict can say which decoder it came from.
 *
 * The slot names DAC 0 as its source and ADC 3 and ADC 1 as its two edges.
 * In package indices DAC 0 is 3 while the two edges are 4 and 6, so the
 * source sits below the inclusive range it bounds, that range covers ADC 3,
 * ADC 2 and ADC 1, and the decode marks no tile in it as the master. The
 * slot is well formed and every field is in range, so nothing in the decode
 * rejects it.
 *
 * 0xFFF4444F is asserted as a literal: DAC 0 at tile index 4 naming itself,
 * ADC 1, ADC 2 and ADC 3 naming it, and the three remaining DAC tiles
 * ungrouped because no slot covered them.
 */
void checkSourceOutsideItsEdgeRangeIsStillResetFirst() {
    XRFdcScriptDistribution dist;

    gScript.reset();
    gScript.ipType = 2;

    dist.sourceType = XRFDC_DAC_TILE;
    dist.sourceTileId = 0;
    dist.edgeTypes[0] = XRFDC_ADC_TILE;
    dist.edgeTypes[1] = XRFDC_ADC_TILE;
    dist.edgeTileIds[0] = 3;
    dist.edgeTileIds[1] = 1;
    gScript.distributions.push_back(dist);

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    gScript.calls.clear();

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);

    const size_t master = probeAt(XRFDC_DAC_TILE, 0);
    const size_t adc1 = probeAt(XRFDC_ADC_TILE, 1);
    const size_t adc2 = probeAt(XRFDC_ADC_TILE, 2);
    const size_t adc3 = probeAt(XRFDC_ADC_TILE, 3);

    bool ok = map->doneCalled() && !map->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = (map->getWord(0) == 0xFFF4444Fu);
    if (ok) ok = (master < adc1);
    if (ok) ok = (master < adc2);
    if (ok) ok = (master < adc3);

    if (!ok) {
        fprintf(stderr,
                "source outside its range: map=0x%08X, probes dac0=%zu adc1=%zu adc2=%zu "
                "adc3=%zu\n",
                map->getWord(0), master, adc1, adc2, adc3);
    }

    runCheck("a source tile outside its own edge range is still reset first", ok);
}

/*
 * A slot whose source an earlier slot already took is still ordered behind
 * a marked master.
 *
 * This is the other documented getter route to a master-less group, and it
 * is the one that produces an edge naming another edge rather than an edge
 * naming an ungrouped tile. The first slot is this carrier, so DAC 0 is
 * marked and ADC 3 is one of its edges. The second slot names ADC 3 as its
 * own source and ADC 1 as its far edge, so its range covers ADC 3, ADC 2
 * and ADC 1. First slot wins, ADC 3 keeps the edge role it already had, and
 * ADC 2 and ADC 1 come out naming a tile that is an edge rather than a
 * master.
 *
 * Without the chain being followed the two of them are owned by the ADC
 * entry point, because the tile they name is an ADC. They would then be
 * restarted by a call that cannot restart DAC 0 first, which is the same
 * hazard the division of work exists to remove, arriving at two tiles this
 * time instead of one. 0x4444444F is asserted as a literal: every tile but
 * ADC 0 naming tile index 4, which is DAC 0.
 *
 * Both entry points are driven. The DAC sweep has to put DAC 0 ahead of
 * both re-pointed tiles, and the ADC sweep has to leave both of them alone,
 * and neither half implies the other.
 */
void checkSlotWhoseSourceWasAlreadyTakenIsOrderedBehindAMaster() {
    XRFdcScriptDistribution overlap;

    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    overlap.sourceType = XRFDC_ADC_TILE;
    overlap.sourceTileId = 3;
    overlap.edgeTypes[0] = XRFDC_ADC_TILE;
    overlap.edgeTypes[1] = XRFDC_ADC_TILE;
    overlap.edgeTileIds[0] = 3;
    overlap.edgeTileIds[1] = 1;
    gScript.distributions.push_back(overlap);

    PyRFdcPtr device = PyRFdc::create();

    rim::TransactionPtr map = driveRead(device, kClkDistMap);

    gScript.calls.clear();

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);

    const bool adcTouchedOne =
        gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 1, XRFDC_SCRIPT_ANY);
    const bool adcTouchedTwo =
        gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, 2, XRFDC_SCRIPT_ANY);

    gScript.calls.clear();

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);

    const size_t master = probeAt(XRFDC_DAC_TILE, 0);
    const size_t adc1 = probeAt(XRFDC_ADC_TILE, 1);
    const size_t adc2 = probeAt(XRFDC_ADC_TILE, 2);

    bool ok = map->doneCalled() && !map->errorStrCalled();
    if (ok) ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = (map->getWord(0) == 0x4444444Fu);
    if (ok) ok = !adcTouchedOne;
    if (ok) ok = !adcTouchedTwo;
    if (ok) ok = (master < adc1);
    if (ok) ok = (master < adc2);

    if (!ok) {
        fprintf(stderr,
                "source already taken: map=0x%08X, adc sweep touched adc1=%d adc2=%d, "
                "dac sweep probes dac0=%zu adc1=%zu adc2=%zu\n",
                map->getWord(0), static_cast<int>(adcTouchedOne),
                static_cast<int>(adcTouchedTwo), master, adc1, adc2);
    }

    runCheck("a slot whose source was already taken is ordered behind a master", ok);
}

/*
 * One fixture of the named master contract claim, labelled so a single red
 * fixture is identifiable.
 */
void runNamedMasterCheck(const char *site, bool ok) {
    const std::string label =
        std::string("every named clock master names itself for every topology source [") +
        site + "]";

    runCheck(label.c_str(), ok);
}

/*
 * Every nibble of the published map that names a master names a marked one.
 *
 * This is a contract claim and not a fourth restatement of the three above.
 * Those three pin three particular boards, each with its own expected word
 * written down as a literal. This one pins the property those three are
 * instances of, so a change that reintroduces an ordering rule belonging to
 * one topology source turns it red on a fixture nobody thought to write
 * down, which is the failure mode the two reproductions above both came
 * from.
 *
 * Four fixtures, two per topology source: this carrier through the
 * documented getter, this carrier through the raw decode, and the two
 * master-less reproductions. Each builds its own device after its own
 * fixture reset, so no fixture inherits the one before it, and each reads
 * the property off the published word alone.
 */
void checkEveryNamedClockMasterNamesItself() {
    {
        gScript.reset();
        gScript.ipType = 2;
        scriptThisCarriersDistribution();

        PyRFdcPtr device = PyRFdc::create();
        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        bool ok = map->doneCalled() && !map->errorStrCalled();
        if (ok) ok = everyNamedMasterNamesItself(map->getWord(0));

        if (!ok) fprintf(stderr, "named master, documented getter: map=0x%08X\n",
                         map->getWord(0));

        runNamedMasterCheck("documented getter", ok);
    }

    {
        gScript.reset();
        gScript.ipType = 0;
        scriptThisCarriersClockDetect();

        PyRFdcPtr device = PyRFdc::create();
        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        bool ok = map->doneCalled() && !map->errorStrCalled();
        if (ok) ok = everyNamedMasterNamesItself(map->getWord(0));

        if (!ok) fprintf(stderr, "named master, raw decode: map=0x%08X\n", map->getWord(0));

        runNamedMasterCheck("raw decode", ok);
    }

    {
        gScript.reset();
        gScript.ipType = 1;
        scriptThisCarriersClockDetect();
        gScript.scriptRegister(XRFDC_DAC_TILE, 0, 0x80, 0x0000);

        PyRFdcPtr device = PyRFdc::create();
        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        bool ok = map->doneCalled() && !map->errorStrCalled();
        if (ok) ok = everyNamedMasterNamesItself(map->getWord(0));

        if (!ok) fprintf(stderr, "named master, zero clock detect: map=0x%08X\n",
                         map->getWord(0));

        runNamedMasterCheck("zero clock detect", ok);
    }

    {
        XRFdcScriptDistribution dist;

        gScript.reset();
        gScript.ipType = 2;

        dist.sourceType = XRFDC_DAC_TILE;
        dist.sourceTileId = 0;
        dist.edgeTypes[0] = XRFDC_ADC_TILE;
        dist.edgeTypes[1] = XRFDC_ADC_TILE;
        dist.edgeTileIds[0] = 3;
        dist.edgeTileIds[1] = 1;
        gScript.distributions.push_back(dist);

        PyRFdcPtr device = PyRFdc::create();
        rim::TransactionPtr map = driveRead(device, kClkDistMap);

        bool ok = map->doneCalled() && !map->errorStrCalled();
        if (ok) ok = everyNamedMasterNamesItself(map->getWord(0));

        if (!ok) fprintf(stderr, "named master, source outside its range: map=0x%08X\n",
                         map->getWord(0));

        runNamedMasterCheck("source outside its range", ok);
    }
}

/* ------------------------------------------------------------------------ */
/* One bounded recovery attempt per group.                                   */
/*                                                                           */
/* A restart that timed out surfaces to this driver only as a non-success    */
/* return from XRFdc_Reset, because the wait itself lives inside the AMD     */
/* driver. When that happens on a tile the cached topology calls an edge of  */
/* a distributed clock, the whole group is re-run once with the explicit     */
/* reset, master first.                                                      */
/*                                                                           */
/* What these five claims establish, stated before they are read: the        */
/* mechanism behaves as specified against a scripted fixture. They establish */
/* nothing whatever about whether it recovers a converter on this carrier.   */
/* No software recovery has ever worked here, and the existence of a retry   */
/* path is not evidence that it recovers anything.                           */
/*                                                                           */
/* The counter is the point of the exercise. A recovery that worked makes    */
/* the reset return success, which is the shape of a reset that never needed */
/* one, so without a number that separates never armed from armed and failed */
/* from armed and succeeded, twenty clean reboots could be twenty            */
/* recoveries and nothing would say so.                                      */
/* ------------------------------------------------------------------------ */

//! The read-only word the armed and succeeded counts are published through.
const uint64_t kRecoveryCount = 0x1201C;

/*
 * One sub-check of the not-an-edge claim, labelled so a single red one is
 * identifiable.
 *
 * The same shape as runStepCheck above and for the same reason. One
 * aggregate verdict over the ungrouped tile and the group master would go
 * red for either and say nothing about which, and the two are excluded from
 * arming for two different reasons.
 */
void runNotAnEdgeCheck(const char *site, bool ok) {
    const std::string label =
        std::string("a failing tile that is not an edge arms no recovery [") + site + "]";

    runCheck(label.c_str(), ok);
}

/*
 * A failing reset on a distribution edge tile arms exactly one recovery.
 *
 * ADC 3 is the edge tile on this carrier and its reset is scripted to fail
 * for good, so the recovery fires and then fails too. That is the outcome
 * this project has every reason to expect on the board, and it is asserted
 * first: the counter reports one armed and none succeeded, the transaction
 * still reports the error it would have reported anyway, and the record for
 * ADC 3 still names the step it went wrong at. A recovery that did not work
 * must not suppress the diagnosis.
 *
 * The call counts and the cycle count are asserted alongside the counter,
 * because a counter incremented by an arming pass that then issued nothing
 * would satisfy the counter assertion on its own.
 */
void checkFailingEdgeTileArmsExactlyOneRecovery() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    const std::string msg = dac->errorStrValue();
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    if (ok) ok = (armed->getWord(0) == 0x00000001u);
    // Four DAC resets in the walk and four in the recovery pass, and ADC 3
    // once in each.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 8);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 2);
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    // Two cycles for every tile of the group and nothing at all for ADC 0,
    // 1 and 2, which this entry point does not own.
    if (ok) ok = (counts->getWord(0) == 0x22222000u);
    if (ok) ok = dac->errorStrCalled() && !dac->doneCalled();
    if (ok) ok = (recordFor(msg, "ADC3").find("XRFdc_Reset") != std::string::npos);

    if (!ok) {
        fprintf(stderr,
                "edge arms one: recoveries=0x%08X, counts=0x%08X, dac reset=%zu "
                "adc reset=%zu, record '%s'\n",
                armed->getWord(0), counts->getWord(0),
                countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                recordFor(msg, "ADC3").c_str());
    }

    runCheck("a failing edge tile arms exactly one recovery", ok);
}

/*
 * A failing reset on a tile that is not a distribution edge arms nothing.
 *
 * Two tiles, excluded for two different reasons.
 *
 * ADC 1 has its own clock pin and belongs to no group, so there is no group
 * to re-establish and re-cycling it would address nothing.
 *
 * DAC 0 is the group master. The recovery re-establishes a group around its
 * master, and a master that will not reset is not a group that can be
 * re-established by resetting it again. Excluding it is the difference
 * between a bounded attempt and an attempt whose first act repeats the call
 * that just failed.
 */
void checkFailingTileThatIsNotAnEdgeArmsNoRecovery() {
    // An ungrouped tile of this call's own type.
    {
        gScript.reset();
        gScript.ipType = 2;
        scriptThisCarriersDistribution();

        PyRFdcPtr device = PyRFdc::create();
        gScript.calls.clear();

        gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 1, XRFDC_SCRIPT_ANY,
                              XRFDC_FAILURE);

        driveWrite(device, kResetAllAdc, 1);
        rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

        bool ok = armed->doneCalled() && !armed->errorStrCalled();
        if (ok) ok = (armed->getWord(0) == 0x00000000u);
        // The ADC entry point owns ADC 0, 1 and 2 and nothing else, so three
        // resets and no fourth pass over any of them.
        if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 3);

        if (!ok) {
            fprintf(stderr, "ungrouped arms none: recoveries=0x%08X, adc reset=%zu\n",
                    armed->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE));
        }

        runNotAnEdgeCheck("ungrouped tile", ok);
    }

    // The master of the group itself.
    {
        gScript.reset();
        gScript.ipType = 2;
        scriptThisCarriersDistribution();

        PyRFdcPtr device = PyRFdc::create();
        gScript.calls.clear();

        gScript.scriptFailure("XRFdc_Reset", XRFDC_DAC_TILE, 0, XRFDC_SCRIPT_ANY,
                              XRFDC_FAILURE);

        driveWrite(device, kResetAllDac, 1);
        rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

        bool ok = armed->doneCalled() && !armed->errorStrCalled();
        if (ok) ok = (armed->getWord(0) == 0x00000000u);

        if (!ok) {
            fprintf(stderr, "master arms none: recoveries=0x%08X, dac reset=%zu\n",
                    armed->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE));
        }

        runNotAnEdgeCheck("group master", ok);
    }
}

/*
 * An edge tile that failed at some other step arms no recovery.
 *
 * Arming is a conjunction: the record has to name the reset as the step
 * that went wrong and the cached role has to be edge. The claims above move
 * only the role half, so a predicate simplified to the role alone would
 * satisfy every one of them and would still be wrong. It would re-cycle a
 * whole clock group whenever an edge tile failed at anything at all, for a
 * fault another pass of resets does not address, at the price of an extra
 * pass and up to a second per tile on the transaction thread.
 *
 * That tile is not hypothetical. The PLL reconfigure early-fails its
 * reference frequency check for every tile whose clock source reads
 * external, which on this carrier is the edge tile and all four tiles of
 * the group it belongs to in the state that follows a failure.
 *
 * The reset that follows the failed reconfigure is the compensating one and
 * it succeeds here, so the record keeps the first step it went wrong at and
 * the reset is not what the tile is reported under. That is the input the
 * arming predicate has to decline.
 */
void checkEdgeTileFailingAtAnotherStepArmsNoRecovery() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    const std::string msg = dac->errorStrValue();
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    if (ok) ok = (armed->getWord(0) == 0x00000000u);
    // The walk and nothing after it: four DAC resets and the one against the
    // edge tile, with no second pass over any of them.
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 4);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 1);
    if (ok) ok = dac->errorStrCalled() && !dac->doneCalled();
    // And the tile is still reported under the call that actually went
    // wrong, which is what the arming predicate read to decline.
    if (ok) {
        ok = (recordFor(msg, "ADC3").find("XRFdc_DynamicPLLConfig") != std::string::npos);
    }

    if (!ok) {
        fprintf(stderr,
                "other step arms none: recoveries=0x%08X, dac reset=%zu adc reset=%zu, "
                "record '%s'\n",
                armed->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE),
                recordFor(msg, "ADC3").c_str());
    }

    runCheck("a failing edge tile that did not fail at the reset arms no recovery", ok);
}

/*
 * A recovery that succeeded is still counted.
 *
 * This is the claim the counter exists for. The ADC 3 reset is scripted to
 * fail exactly once, so the walk's reset fails and the recovery pass's reset
 * succeeds, the records are cleared for the tiles the attempt reset, and the
 * transaction completes reporting no error at all. From the caller's side
 * the reset is indistinguishable from one that never had a problem.
 *
 * Without this claim a silent retry would be indistinguishable from a reset
 * that never needed one, and twenty clean reboots could be twenty
 * recoveries. What remains is the counter, reading one armed and one
 * succeeded, and one warning line naming the tile and its master.
 */
void checkRecoveryThatSucceededIsStillCounted() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();
    gScript.logWarnings.clear();

    gScript.scriptFailureTimes("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                               XRFDC_SCRIPT_ANY, 1, XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    if (ok) ok = (armed->getWord(0) == 0x00010001u);
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();
    if (ok) ok = gScript.logErrors.empty();
    if (ok) ok = (gScript.logWarnings.size() == 1);
    if (ok) ok = (gScript.logWarnings[0].find("ADC3") != std::string::npos);
    if (ok) ok = (gScript.logWarnings[0].find("DAC0") != std::string::npos);

    if (!ok) {
        fprintf(stderr,
                "recovery counted: recoveries=0x%08X, done=%u err=%u, %zu warning(s) '%s'\n",
                armed->getWord(0), dac->doneCalls(), dac->errorStrCalls(),
                gScript.logWarnings.size(),
                gScript.logWarnings.empty() ? "" : gScript.logWarnings[0].c_str());
    }

    runCheck("a recovery that succeeded is still counted", ok);
}

/*
 * A fired recovery is reported on a channel a default bridge admits.
 *
 * This is the report that has to survive the boot where the counter cannot
 * be read, because the register path on this carrier has been measured
 * degrading after a converter failure. The host side bridge filters log
 * output by a global level whose default admits errors and discards
 * warnings, so which channel the line goes out on is a property of the
 * change rather than an incidental detail of the call site.
 *
 * Exactly one report in total across both lists, counted together rather
 * than per list, so moving the line cannot quietly become emitting it
 * twice. This fixture drives the DAC entry point, which owns the whole
 * group and therefore defers nothing, so the recovery report is the only
 * report the pass can produce.
 *
 * The transaction still completes with no error string. That is what makes
 * the report the only evidence: from the caller's side a recovered reset is
 * indistinguishable from one that never had a problem.
 *
 * What this claim deliberately proves nothing about: whether a board will
 * print the line. The two lists are this harness's own capture of what the
 * logging shim was asked to print, and the level a real bridge runs at is a
 * runtime configuration this suite cannot observe.
 */
void checkFiredRecoveryIsReportedOnAnAdmittedChannel() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    // Construction emits on neither channel, so the total below belongs to
    // the reset and to nothing else.
    bool ok = gScript.logWarnings.empty() && gScript.logErrors.empty();

    gScript.scriptFailureTimes("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                               XRFDC_SCRIPT_ANY, 1, XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);

    if (ok) ok = ((gScript.logErrors.size() + gScript.logWarnings.size()) == 1);
    if (ok) ok = (gScript.logErrors.size() == 1);
    if (ok) ok = (gScript.logErrors[0].find("ADC3") != std::string::npos);
    if (ok) ok = (gScript.logErrors[0].find("DAC0") != std::string::npos);
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();

    if (!ok) {
        fprintf(stderr,
                "recovery channel: %zu error(s), %zu warning(s), first error '%s'\n",
                gScript.logErrors.size(), gScript.logWarnings.size(),
                gScript.logErrors.empty() ? "" : gScript.logErrors[0].c_str());
    }

    runCheck("a fired recovery is reported on a channel a default bridge admits", ok);
}

/*
 * A recovery that worked clears the record it re-ran and no other.
 *
 * The claim above establishes that a successful attempt clears what it
 * addressed. This one establishes the other half, which is the half a
 * clearing pass written over the whole group gets wrong: the group holds
 * tiles that failed at different steps, and only one of those steps is the
 * one the attempt re-ran.
 *
 * ADC 3 fails its reset once, so it arms the attempt and the attempt's own
 * reset succeeds. DAC 0, the group master, fails its quadrature settings
 * write for good, which no number of resets addresses. A pass that cleared
 * the whole group would erase that record before the sweep derives its
 * verdict from the records, and the host would be told the converter reset
 * succeeded while DAC 0 runs with settings that were never applied. That is
 * worse than the behaviour before the recovery existed, which at least
 * reported the settings failure.
 *
 * Asserted on the reported text and not only on the transaction flag,
 * because an error raised for some other reason would satisfy the flag. The
 * surviving failure has to be named, by tile and by step, in what the host
 * is handed.
 */
void checkRecoveryClearsOnlyTheTileItReRan() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailureTimes("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                               XRFDC_SCRIPT_ANY, 1, XRFDC_FAILURE);
    gScript.scriptFailure("XRFdc_SetQMCSettings", XRFDC_DAC_TILE, 0, 0, XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    const std::string msg = dac->errorStrValue();
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    // One armed and one succeeded, so the attempt ran and reported success.
    // Without that the surviving record would prove nothing: a recovery that
    // failed clears nothing at all.
    if (ok) ok = (armed->getWord(0) == 0x00010001u);
    if (ok) ok = dac->errorStrCalled();
    if (ok) ok = (msg.find("DAC0") != std::string::npos);
    if (ok) ok = (msg.find("XRFdc_SetQMCSettings") != std::string::npos);

    if (!ok) {
        fprintf(stderr,
                "clears only its own: recoveries=0x%08X, done=%u err=%u, message '%s'\n",
                armed->getWord(0), dac->doneCalls(), dac->errorStrCalls(), msg.c_str());
    }

    runCheck("a recovery clears only the tile it re-ran at the reset", ok);
}

/*
 * One tile of the group that is not enabled does not defeat the recovery.
 *
 * The sweep asks XRFdc_CheckTileEnabled before it touches a tile, and the
 * walk builder's own documentation leans on that guard as the reason a
 * wrong cache cannot reach the converter. The recovery pass is the one
 * reset path that did not ask, so the guard is a contract between the two
 * rather than a local nicety, and this claim is where the contract is
 * stated.
 *
 * DAC 2 is scripted as not enabled and ADC 3 fails its reset once, so the
 * attempt is armed and every reset it does issue succeeds. Two things have
 * to hold. No reset may be issued against DAC 2, because a refusal counted
 * against the attempt would make the recovery unreachable on any board with
 * a partially populated group. And DAC 2's cycle nibble has to stay at
 * zero, because the count is read by this project as evidence of what the
 * driver did, and a cycle published for a tile that was never touched is
 * evidence of something that did not happen.
 *
 * The group master's nibble is asserted at 2, one from the sweep and one
 * from the recovery pass, so a pass that skipped everything rather than
 * skipping the disabled tile cannot satisfy the first two assertions by
 * doing nothing at all.
 */
void checkDisabledTileInAGroupDoesNotDefeatTheRecovery() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailure("XRFdc_CheckTileEnabled", XRFDC_DAC_TILE, 2, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);
    gScript.scriptFailureTimes("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY,
                               XRFDC_SCRIPT_ANY, 1, XRFDC_FAILURE);

    rim::TransactionPtr dac = driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    if (ok) ok = (armed->getWord(0) == 0x00010001u);
    if (ok) ok = (countExactCalls("XRFdc_Reset", XRFDC_DAC_TILE, 2, XRFDC_SCRIPT_ANY) == 0);
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_DAC_TILE, 2) == 0u);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_DAC_TILE, 0) == 2u);
    if (ok) ok = dac->doneCalled() && !dac->errorStrCalled();

    if (!ok) {
        fprintf(stderr,
                "disabled in group: recoveries=0x%08X, counts=0x%08X, "
                "dac2 reset=%zu, done=%u err=%u\n",
                armed->getWord(0), counts->getWord(0),
                countExactCalls("XRFdc_Reset", XRFDC_DAC_TILE, 2, XRFDC_SCRIPT_ANY),
                dac->doneCalls(), dac->errorStrCalls());
    }

    runCheck("a disabled tile in a group does not defeat the recovery", ok);
}

/*
 * Two failing edge tiles of one group arm one recovery between them.
 *
 * The bound is one attempt per group per global reset, tracked by a set of
 * attempted masters local to the call, so a second failing edge of a group
 * already attempted arms nothing further. Each failed internal restart wait
 * costs a second on the transaction thread, so an attempt per failing tile
 * would scale the worst case with the size of the group.
 *
 * Asserted on the reset call count as well as on the counter, because a
 * bound that held in the counter and not in the calls would be no bound at
 * all: twelve DAC resets rather than eight is the shape of two passes.
 */
void checkTwoFailingEdgesInOneGroupArmOneRecovery() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);
    gScript.scriptFailure("XRFdc_Reset", XRFDC_DAC_TILE, 2, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    driveWrite(device, kResetAllDac, 1);
    rim::TransactionPtr armed = driveRead(device, kRecoveryCount);

    bool ok = armed->doneCalled() && !armed->errorStrCalled();
    if (ok) ok = (armed->getWord(0) == 0x00000001u);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE) == 8);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 2);

    if (!ok) {
        fprintf(stderr,
                "two edges one recovery: recoveries=0x%08X, dac reset=%zu adc reset=%zu\n",
                armed->getWord(0), countCallsForType("XRFdc_Reset", XRFDC_DAC_TILE),
                countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE));
    }

    runCheck("two failing edges in one group arm one recovery", ok);
}

/*
 * The recovery counter refuses a write, mutates nothing, and answers an
 * instance whose construction never produced a driver.
 *
 * The three registers beside this one were published with the write
 * refusal and the dead-driver read admission implemented in the same shape
 * and argued for rather than driven, and one of them still is. That
 * argument is worth less here than anywhere else in the block: this
 * register is the only evidence that a recovery ever fired, so a host that
 * cannot read it on a driver that is already in trouble has lost the one
 * thing it came for. Zero is the honest answer on such an instance, and it
 * is the truth rather than a placeholder: a driver that never came up ran
 * no sweep and armed no recovery.
 *
 * The scratchpad is written before and read after, so a refused word is
 * shown to have left the rest of the block alone rather than merely to
 * have reported an error.
 */
void checkRecoveryCounterIsReadOnlyAndAnswersADeadDriver() {
    const uint32_t pattern = 0x5AC33C5Au;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    driveWrite(device, kScratchPad, pattern);

    rim::TransactionPtr refused = driveWrite(device, kRecoveryCount, 0xDEADBEEFu);

    bool ok = refused->errorStrCalled() && !refused->doneCalled();
    if (ok) ok = (refused->errorStrValue().find("RecoveryCount") != std::string::npos);

    if (ok) {
        rim::TransactionPtr scratch = driveRead(device, kScratchPad);

        ok = scratch->doneCalled() && !scratch->errorStrCalled();
        if (ok) ok = (scratch->getWord(0) == pattern);
    }

    // And the read is admitted on an instance that never got a driver.
    if (ok) {
        PyRFdcPtr dead = createDeadDevice("metal_init");

        rim::TransactionPtr armed = driveRead(dead, kRecoveryCount);

        ok = armed->doneCalled() && !armed->errorStrCalled();
        if (ok) ok = (armed->getWord(0) == 0x00000000u);

        // The write stays refused there too, and by the register rather
        // than by the guard, so the refusal still names itself.
        if (ok) {
            rim::TransactionPtr deadWrite = driveWrite(dead, kRecoveryCount, 1);

            ok = deadWrite->errorStrCalled() && !deadWrite->doneCalled();
        }
    }

    if (!ok) {
        fprintf(stderr, "recovery counter read only: refusal '%s'\n",
                refused->errorStrValue().c_str());
    }

    runCheck("the recovery counter is read only and answers a dead driver", ok);
}

/*
 * The recovery pass re-runs the group master before the edge tile that
 * armed it, and starts only after the walk has finished.
 *
 * Positions rather than a selector, because the recovery issues calls
 * carrying the same name, type, tile and block as the walk's own calls
 * against the same tiles. Recorded order is the only discriminator that
 * exists, so the assertion is on second occurrences.
 *
 * Both halves are needed. The master preceding the edge inside the pass is
 * the property the recovery exists for, and the pass following the whole
 * walk is what makes it a pass rather than an interleaving that happened to
 * put two calls in the right order.
 */
void checkRecoveryRerunsTheGroupMasterFirst() {
    gScript.reset();
    gScript.ipType = 2;
    scriptThisCarriersDistribution();

    PyRFdcPtr device = PyRFdc::create();
    gScript.calls.clear();

    gScript.scriptFailure("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, XRFDC_FAILURE);

    driveWrite(device, kResetAllDac, 1);

    const size_t edgeFirst = nthCallAt("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, 1);
    const size_t masterSecond = nthCallAt("XRFdc_Reset", XRFDC_DAC_TILE, 0, XRFDC_SCRIPT_ANY, 2);
    const size_t edgeSecond = nthCallAt("XRFdc_Reset", XRFDC_ADC_TILE, 3, XRFDC_SCRIPT_ANY, 2);

    bool ok = (masterSecond < edgeSecond);
    if (ok) ok = (edgeFirst < masterSecond);

    if (!ok) {
        fprintf(stderr,
                "master first in recovery: adc3 first=%zu, dac0 second=%zu, "
                "adc3 second=%zu, of %zu recorded call(s)\n",
                edgeFirst, masterSecond, edgeSecond, gScript.calls.size());
    }

    runCheck("the recovery re-runs the group master first", ok);
}

/* ------------------------------------------------------------------------ */
/* What the cycle count is allowed to claim when a reconfigure fails.        */
/*                                                                           */
/* The fire predicate treats every non-success from XRFdc_DynamicPLLConfig   */
/* as having performed no cycle. That is true of a refusal that happened     */
/* before the call touched the tile and false of a failure that happened     */
/* after the call had already driven the tile down and back up, and the two  */
/* are the same return value. The three claims below are about the count     */
/* and not about the reset: the reset fires on every non-success either      */
/* way, which the three claims further up already pin.                       */
/* ------------------------------------------------------------------------ */

/*
 * A reconfigure that left the tile unpowered is counted as two cycles.
 *
 * The tile reads powered up before the reconfigure and not powered up after
 * it, which is only possible if the call drove the tile down. It did not
 * bring it back, so the compensating reset is still needed and still fires,
 * and the tile is driven at its state machine twice inside one sweep. A
 * nibble of one here would be the counter under-reporting the exact thing
 * it was built to detect.
 *
 * The two reads are scripted as a sticky zero plus one queued powered-up
 * value rather than as two sticky values, because one key cannot hold two
 * answers. The read count is asserted alongside the nibble: the queue is
 * spent in call order, so a claim that only checked the nibble could be
 * satisfied by a sweep that made a different number of reads and happened
 * to land on the same pair of values.
 */
void checkReconfigureFailureThatLeftTheTileDownIsCountedAsTwoCycles() {
    const uint32_t failing = 2;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus,
                               (tile == failing) ? 0x0 : kPoweredUpStatus);
    }
    // The first read of this key answers powered up and every read after it
    // falls back to the sticky zero above, so the tile looks powered up to
    // the gate and unpowered to the re-read.
    gScript.scriptRegisterOnce(XRFDC_ADC_TILE, failing, kOffsetCommonStatus, kPoweredUpStatus);
    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    const size_t failingReads =
        countExactCalls("XRFdc_RDReg", XRFDC_ADC_TILE, failing, kOffsetCommonStatus);
    const size_t healthyReads =
        countExactCalls("XRFdc_RDReg", XRFDC_ADC_TILE, 0, kOffsetCommonStatus);

    bool ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, failing) == 2u);
    // The neighbours are untouched by the discrimination: each was powered
    // up, each reconfigured cleanly, and each keeps its one cycle.
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 0) == 1u);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 1) == 1u);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 3) == 1u);
    // Two reads of the gate register on the failing tile and one on a
    // healthy one, so the nibble above came from a second read that was
    // really taken and the healthy path really did not take one.
    if (ok) ok = (failingReads == 2);
    if (ok) ok = (healthyReads == 1);
    // And the reset the count is recording did happen.
    if (ok) ok = gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY);

    fprintf(stderr,
            "left down: counts=0x%08X, failing reads=%zu healthy reads=%zu, reset=%d\n",
            counts->getWord(0), failingReads, healthyReads,
            gScript.sawCall("XRFdc_Reset", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY) ? 1 : 0);

    runCheck("a reconfigure failure that left the tile down is counted as two cycles", ok);
}

/*
 * A reconfigure failure the driver cannot resolve is reported as not exact.
 *
 * The tile reads powered up both before the reconfigure and after it failed.
 * From this code an early refusal that never touched the tile and a late
 * failure that completed its cycle and left the tile powered look identical,
 * so the true figure is one or two and the driver declines to pick. The
 * nibble carries the reserved value rather than a guess.
 *
 * The reset call is asserted as well, because the reserved value must not be
 * readable as the driver having skipped the reset. The failure this carrier
 * actually produces is the reference frequency refusal on its five external
 * clock tiles, and those tiles get no cycle at all without it.
 */
void checkReconfigureFailureThatCannotBeToldApartIsReportedAsNotExact() {
    const uint32_t failing = 2;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    // No queued value anywhere, so both reads of the failing tile's gate
    // register agree and nothing separates the two reconfigure outcomes.
    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus, kPoweredUpStatus);
    }
    gScript.scriptFailure("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY,
                          XRFDC_FAILURE);

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    const size_t failingResets =
        countExactCalls("XRFdc_Reset", XRFDC_ADC_TILE, failing, XRFDC_SCRIPT_ANY);

    bool ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, failing) == 0xFu);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 0) == 1u);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 1) == 1u);
    if (ok) ok = (tileNibble(counts->getWord(0), XRFDC_ADC_TILE, 3) == 1u);
    if (ok) ok = (failingResets == 1);

    fprintf(stderr, "not exact: counts=0x%08X, failing tile resets=%zu\n",
            counts->getWord(0), failingResets);

    runCheck("a reconfigure failure that cannot be told apart is reported as not exact", ok);
}

/*
 * The healthy path still costs one power-up read per tile.
 *
 * This is the claim that catches a second read added to the healthy path,
 * which is the cost the whole discrimination has to avoid. Every tile is
 * powered up and every reconfigure succeeds, so every tile takes the first
 * arm of the cycle decision, and the re-read must be unreachable there.
 *
 * Asserted per tile and not as a type total, because a total of four can be
 * produced by two tiles read twice and two tiles never read at all.
 */
void checkHealthyPathStillCostsOnePowerUpReadPerTile() {
    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus, kPoweredUpStatus);
    }

    rim::TransactionPtr adc = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr counts = driveRead(device, kResetCycleCount);

    size_t reads[4] = {0, 0, 0, 0};
    for (uint32_t tile = 0; tile < 4; tile++) {
        reads[tile] = countExactCalls("XRFdc_RDReg", XRFDC_ADC_TILE, tile, kOffsetCommonStatus);
    }

    bool ok = adc->doneCalled() && !adc->errorStrCalled();
    if (ok) ok = counts->doneCalled() && !counts->errorStrCalled();
    if (ok) ok = (counts->getWord(0) == 0x00001111u);
    if (ok) ok = (countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE) == 0);
    for (uint32_t tile = 0; ok && (tile < 4); tile++) {
        ok = (reads[tile] == 1);
    }

    fprintf(stderr, "healthy read cost: counts=0x%08X, reads=%zu/%zu/%zu/%zu, reset=%zu\n",
            counts->getWord(0), reads[0], reads[1], reads[2], reads[3],
            countCallsForType("XRFdc_Reset", XRFDC_ADC_TILE));

    runCheck("the healthy path still costs one power-up read per tile", ok);
}

/*
 * A later sweep clears the not-exact flag along with the count it qualifies.
 *
 * The flag and the counter are two parallel arrays describing one tile, and
 * a sweep that cleared one and not the other would publish a fresh count
 * under a stale qualifier: a tile that was unknowable during one reset would
 * keep reading the reserved value for every reset after it, however cleanly
 * those went.
 *
 * Driven as two sweeps over one device rather than as two claims, because
 * the defect is a difference between the two arrays and only a second sweep
 * over a tile the first one flagged can see it. The reconfigure is scripted
 * to fail once, so the first sweep leaves ADC 2 unknowable and the second
 * finds every tile healthy.
 */
void checkASecondSweepClearsTheNotExactFlagWithTheCount() {
    const uint32_t failing = 2;

    gScript.reset();
    PyRFdcPtr device = PyRFdc::create();

    gScript.calls.clear();
    gScript.logErrors.clear();

    for (uint32_t tile = 0; tile < 4; tile++) {
        gScript.scriptRegister(XRFDC_ADC_TILE, tile, kOffsetCommonStatus, kPoweredUpStatus);
    }
    gScript.scriptFailureTimes("XRFdc_DynamicPLLConfig", XRFDC_ADC_TILE, failing,
                               XRFDC_SCRIPT_ANY, XRFDC_SCRIPT_ANY, 1, XRFDC_FAILURE);

    driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr afterFirst = driveRead(device, kResetCycleCount);
    const uint32_t first = afterFirst->getWord(0);

    rim::TransactionPtr second = driveWrite(device, kResetAllAdc, 1);
    rim::TransactionPtr afterSecond = driveRead(device, kResetCycleCount);
    const uint32_t later = afterSecond->getWord(0);

    // The precondition: the first sweep really did leave the tile flagged,
    // so a green verdict below cannot come from a flag that was never set.
    bool ok = (tileNibble(first, XRFDC_ADC_TILE, failing) == 0xFu);
    if (ok) ok = second->doneCalled() && !second->errorStrCalled();
    if (ok) ok = afterSecond->doneCalled() && !afterSecond->errorStrCalled();
    if (ok) ok = (later == 0x00001111u);

    fprintf(stderr, "flag cleared by the next sweep: first=0x%08X, later=0x%08X\n", first, later);

    runCheck("a second sweep clears the not exact flag with the count", ok);
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
    // Construct under a known fixture rather than under whatever the claim
    // before this one left behind. The topology is read once at
    // construction, so a predecessor that pushed a distribution would make
    // ADC 3 an edge, the ADC entry point would correctly decline to reset
    // it, the scripted failure below would never fire and the dirty
    // precondition would be unsatisfiable. This claim is about reset()
    // emptying state and must not also depend on its position in main.
    gScript.reset();

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
    //
    // The reset count stays above zero for a different reason than it used
    // to. It used to be the second sweep loop's unconditional post-reset.
    // It is now the compensating reset, which fires here because this
    // fixture's power-up status read returns zero for every tile, so no
    // tile was cycled by its PLL reconfigure.
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
const int kClaimsBeforeCountCheck = 105;

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

    checkDeadDriverRejectsDriverTransaction();
    checkDeadDriverKeepsPureStateReadable();
    checkDeadDriverRejectsMetalLogLevelWrite();
    checkEachBailOutNamesItsOwnStep();
    checkRegistrationStubLeavesDevicePointerUnwrittenOnFailure();
    checkRegistrationBailOutClosesNoDevice();
    checkNotCompletedConstructorIsDeadByDefault();
    checkFailedCfgInitializeStopsTheConstructor();
    checkNoDriverCallFollowsAFailedCfgInitialize();
    checkFailedCfgInitializeLeavesSampleRateUnwritten();
    checkRepeatedRejectionIsByteIdentical();
    checkMultiWordRejectionReachesErrorStr();
    checkLiveDriverIsUnaffectedByTheGuard();

    checkReasonRegisterReportsTheFailedStep();
    checkReasonRegisterReadsOkOnLiveDriver();
    checkReasonRegisterIsReadOnly();
    checkReasonRegisterPerformsNoDriverAccess();
    checkReasonOffsetDoesNotCollide();

    checkDeadDriverAnswersAdmittedBlockBeforeWrite();
    checkDeclinedCfgInitializeKeepsAdmittedBlockReadable();
    checkAdmittedReadOnDeadDriverMutatesNothing();

    checkLibmetalIsFinishedOnceOverAFailedConstruction();
    checkDeadDriverMakesNoDriverCallWhileDestroyed();
    checkLiveDriverStillClosesItsDeviceOnTeardown();

    checkIgnoreMetalErrorCannotClearAResetDiagnostic();
    checkIgnoreMetalErrorStillClearsAnUnprotectedError();

    checkThirdReturnValueStopsTheCall();
    checkSuccessStillProceeds();
    checkFailureConstantStillStopsTheCall();

    checkClockDistributionTopologyIsCapturedAtConstruction();
    checkPreGen3DriverIsNeverAskedForTheDistribution();
    checkRefusedDistributionQueryLeavesEveryTileUngrouped();
    checkRawDecodeNamesTheSameMasterAsTheDocumentedApi();
    checkUnprogrammedClockDetectRegisterYieldsNoDistribution();
    checkGen3DriverIsNeverGivenTheRawDecode();
    checkFilledDistributionArrayThatWasRefusedIsStillNoDistribution();
    checkOutOfRangeGenerationIsAskedNothingAtAll();
    checkDistributionRegistersDoNotCollideAndAnswerADeadDriver();

    checkOneIpsmCyclePerEnabledTileOnTheHealthyPath();
    checkOutOfRangeGenerationStillResetsEveryTileAsBefore();
    checkPoweredUpTileWithSucceedingReconfigureGetsNoExplicitReset();
    checkWedgedTileStillGetsItsOneCycle();
    checkPoweredUpTileWithFailingReconfigureStillGetsItsOneCycle();
    checkReconfigureFailureThatLeftTheTileDownIsCountedAsTwoCycles();
    checkReconfigureFailureThatCannotBeToldApartIsReportedAsNotExact();
    checkHealthyPathStillCostsOnePowerUpReadPerTile();
    checkASecondSweepClearsTheNotExactFlagWithTheCount();

    checkAdcEntryPointDefersATileMasteredByADac();
    checkGroupMasterIsResetBeforeItsEdgeTiles();
    checkBothEntryPointsTogetherGiveEveryTileOneCycle();
    checkDeferredTileIsNamedInTheLog();
    checkTileClaimedByTwoGroupsIsResetExactlyOnce();
    checkGroupWhoseTwoEdgesAreTheSameTileContributesNoGroup();
    checkNoDistributionLeavesTheTileOrderUnchanged();

    checkMasterWithZeroClockDetectIsStillResetFirst();
    checkSourceOutsideItsEdgeRangeIsStillResetFirst();
    checkSlotWhoseSourceWasAlreadyTakenIsOrderedBehindAMaster();
    checkEveryNamedClockMasterNamesItself();

    checkFailingEdgeTileArmsExactlyOneRecovery();
    checkFailingTileThatIsNotAnEdgeArmsNoRecovery();
    checkEdgeTileFailingAtAnotherStepArmsNoRecovery();
    checkRecoveryThatSucceededIsStillCounted();
    checkFiredRecoveryIsReportedOnAnAdmittedChannel();
    checkRecoveryClearsOnlyTheTileItReRan();
    checkDisabledTileInAGroupDoesNotDefeatTheRecovery();
    checkTwoFailingEdgesInOneGroupArmOneRecovery();
    checkRecoveryCounterIsReadOnlyAndAnswersADeadDriver();
    checkRecoveryRerunsTheGroupMasterFirst();

    checkFixtureResetEmptiesRecordedState();
    checkRecordedCallListIsNotEmpty();
    checkClaimCountMatchesExpectedTotal();

    printf("RESULT %s\n", (gFailures == 0) ? "PASS" : "FAIL");
    return (gFailures == 0) ? 0 : 1;
}
