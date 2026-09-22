/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: The single configurable fixture behind the host build of
 * PyRFdc.cpp under files/tests/. Every shim collaborator, the driver stubs
 * in xrfdcStub.cpp and the Logging shim alike, records into and reads from
 * the one extern instance gScript declared at the bottom of this file.
 *
 * One configurable fixture, not one fake per collaborator. The reason is the
 * one recorded in software/scripts/probeRfdcInit.py at the _standInRoot
 * docstring: separate fakes drift, and once they have drifted a test is
 * asserting about the fake rather than about the code under test. A single
 * object that every stub consults keeps the scripted failure, the recorded
 * call list and the register contents consistent by construction.
 *
 * It carries twelve things:
 *
 *   calls        an ordered record of every driver call, each formatted as
 *                name/type/tile/block, so a check can assert the sequence a
 *                body performed and not only its return value
 *   failures     a scripted-failure selector keyed on driver function name
 *                plus tile type, tile id, block id and one optional extra
 *                distinguishing argument, any field wildcarded with
 *                XRFDC_SCRIPT_ANY, so one entry can fail one tile or every
 *                tile, and each entry carrying how many further matching
 *                calls it still applies to, so a fault that clears on the
 *                retry can be scripted as well as one that never clears
 *   registers    scripted register contents keyed on type, tile and offset,
 *                consulted by the XRFdc_ReadReg stub, so a diagnostic read
 *                can be made to return a chosen value, each key optionally
 *                carrying a queue of values spent one per read before the
 *                sticky one answers, so two reads of one key inside one body
 *                can be made to disagree
 *   logErrors    the strings the Logging shim was asked to print, kept
 *                beside the transaction record so the console line and the
 *                caller-visible error can be asserted separately
 *   pllEnabled   the Enabled field XRFdc_GetPLLConfig hands back, which
 *                decides whether the reset sweep performs its first
 *                XRFdc_Reset per tile at all. Zero by default, matching the
 *                zero-filled output every other getter stub produces, so a
 *                claim that wants that call site reached has to say so.
 *   ipType       the IP generation the configuration lookup reports. The
 *                production constructor gates its clock distribution query
 *                on it, and zero by default is pre-Gen3, so the query is
 *                unreachable unless a claim asks for it.
 *   distributions the clock distribution topology the getter hands back,
 *                empty by default so the default board has none.
 *   distributionFillsOnRefusal
 *                whether the distribution getter writes its out parameter
 *                before it decides to report a failure. False by default,
 *                which is the driver's own documented refusal path.
 *   distributionFillsFoundSlotsOnly
 *                whether the distribution getter fills only the slots the
 *                fixture pushed and leaves every other slot exactly as the
 *                caller left it, with no zero fill and no sentinel of its
 *                own. False by default, so the getter keeps the two shapes
 *                every claim written before this field existed was
 *                asserted under.
 *   cfgInstance  the driver instance pointer XRFdc_CfgInitialize was handed.
 *                Recorded because the constructor writes two fields of that
 *                instance per tile directly rather than through a call, so
 *                the ordered call list is structurally blind to those
 *                writes, and because the member the production code holds
 *                the instance in is private and the check functions here
 *                are free functions with no access to it. The pointer the
 *                stub receives is the one caller-observable handle on it.
 *   closedDevice the pointer metal_device_close was handed. Recorded for the
 *                same reason cfgInstance is: the constructor's registration
 *                bail-out passes a pointer this file cannot otherwise see,
 *                the close stub discards its argument, and a claim about
 *                which pointer was closed has no other handle on it. Stored
 *                as an opaque address and never dereferenced, because on the
 *                path it exists to observe the value is not a device at all.
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

#ifndef __XRFDC_SCRIPT_H__
#define __XRFDC_SCRIPT_H__

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

//! The driver instance type, declared rather than included. shim/xrfdc.h
//! includes this header, so naming it here would close a cycle.
struct XRFdc;

//! Wildcard for any of the tile type, tile id or block id selector fields.
//! Chosen well outside the 0 to 3 range every real index occupies, so it can
//! never collide with a value a driver call actually passes.
#define XRFDC_SCRIPT_ANY 0xFFFFFFFFu

//! Sentinel for the remaining-uses field of a scripted failure: the entry
//! never runs out and applies to every matching call forever. Every selector
//! written before that field existed is created with this value, so nothing
//! written against the old fixture changes behaviour.
#define XRFDC_SCRIPT_UNLIMITED 0xFFFFFFFFu

//! One scripted failure. An entry matches a call when the name matches, every
//! non-wildcard index field matches, and the entry has uses left.
//!
//! detail is a fifth selector field for a call that carries one more
//! distinguishing argument than the four the recorded form names. The two
//! XRFdc_UpdateEvent call sites inside the reset sweep are the reason it
//! exists: they pass the same type, tile and block and differ only in the
//! event, so without it no scripted failure can reach one of them without
//! also reaching the other, and a claim about one site would be satisfied by
//! the other. An entry created by scriptFailure leaves it wildcarded, so
//! every selector written before this field existed matches exactly as it
//! did. A detail-qualified entry matches only a call that supplies that
//! detail, never one that carries none.
//!
//! remaining is how many further matching calls the entry still applies to,
//! or XRFDC_SCRIPT_UNLIMITED for an entry that never runs out. It exists
//! because a fault that clears on the retry was previously unscriptable: a
//! scripted failure applied to every matching call forever, so a call that
//! fails once and then succeeds could not be expressed, and the only
//! provable outcome of a retry was the one that fails again.
struct XRFdcScriptFailure {
    std::string name;
    uint32_t type;
    uint32_t tile;
    uint32_t block;
    uint32_t detail;
    uint32_t remaining;
    int status;
};

//! One scripted clock distribution, named in the real structure's own terms
//! so a reader can put this side by side with XRFdc_Distribution_Settings.
//! Only the four fields the production decode reads are here: which tile
//! sources the distribution, and which two tiles bound it.
struct XRFdcScriptDistribution {
    uint32_t sourceType;
    uint32_t sourceTileId;
    uint32_t edgeTypes[2];
    uint32_t edgeTileIds[2];
};

//! Key for a scripted register value.
struct XRFdcScriptRegKey {
    uint32_t type;
    uint32_t tile;
    uint32_t offset;

    bool operator<(const XRFdcScriptRegKey &other) const {
        if (type != other.type) return type < other.type;
        if (tile != other.tile) return tile < other.tile;
        return offset < other.offset;
    }
};

class XRFdcScript {
  public:
    //! Ordered record of every driver call, each as name/type/tile/block.
    std::vector<std::string> calls;

    //! Strings the Logging shim was asked to print.
    std::vector<std::string> logErrors;
    std::vector<std::string> logWarnings;
    std::vector<std::string> logDebugs;

    //! Text the libmetal log stub was asked to print, kept apart from the
    //! rogue log lines because they reach different places on the target.
    std::vector<std::string> metalLogs;

    //! The Enabled field XRFdc_GetPLLConfig reports. Read by the production
    //! constructor into pllDefault_, which the reset sweep then tests before
    //! it performs its first XRFdc_Reset of each tile. Set it before
    //! PyRFdc::create(), because the constructor is where it is consulted.
    uint32_t pllEnabled = 0;

    //! The IPType the configuration lookup reports, which the production
    //! constructor gates its clock distribution query on. Zero by default,
    //! which is pre-Gen3, so the query is not issued unless a claim says so
    //! and every claim written before this field existed keeps its meaning.
    //! Set it before PyRFdc::create(), because the constructor is where it
    //! is consulted.
    uint32_t ipType = 0;

    //! The clock distribution topology XRFdc_GetClkDistribution hands back.
    //! Empty by default, which is no distribution at all, so the production
    //! cache keeps the ungrouped values it was declared with unless a claim
    //! pushes a topology here. Set it before PyRFdc::create(), for the same
    //! reason ipType has to be set there.
    std::vector<XRFdcScriptDistribution> distributions;

    //! Whether XRFdc_GetClkDistribution writes the caller's array before it
    //! consults the scripted status, rather than after.
    //!
    //! What it models is a driver version that fills its out parameter and
    //! then decides to report a failure. The review flags that shape as one
    //! this repository cannot rule out, because the driver source is not
    //! present on this host, and the fixture exists so the production guard
    //! can be tested against it instead of assumed against it.
    //!
    //! False by default, which keeps the consult-before-write ordering every
    //! claim written before this field existed was asserted under. Set it
    //! before PyRFdc::create(), for the same reason ipType has to be set
    //! there.
    bool distributionFillsOnRefusal = false;

    //! Whether XRFdc_GetClkDistribution fills only the slots this fixture
    //! pushed and leaves every other slot exactly as the caller left it.
    //!
    //! What it models is a driver version that writes only the slots it
    //! found. Such a driver never zeroes the caller's structure and never
    //! marks a slot unused, so the bytes in every slot it did not fill are
    //! the caller's own, and the consumer's unused-slot test is meaningful
    //! only because the caller wrote the sentinel there first. The two
    //! shapes above both supply that sentinel themselves, whichever of them
    //! is selected, so under either of them the caller's obligation is
    //! invisible.
    //!
    //! False by default, because making this the default would change what
    //! every claim already written against this stub is asserting. Set it
    //! before PyRFdc::create(), for the same reason ipType has to be set
    //! there: the getter is consulted during construction and not later.
    bool distributionFillsFoundSlotsOnly = false;

    //! The driver instance pointer XRFdc_CfgInitialize was handed. The
    //! constructor writes two fields of that instance per tile without
    //! making a call, so this is the only handle a claim has on a write the
    //! ordered call list cannot see.
    const XRFdc *cfgInstance = nullptr;

    //! The pointer metal_device_close was handed. The constructor's
    //! registration bail-out closes a local this file cannot see and the
    //! close stub discards its argument, so this is the only handle a claim
    //! has on which pointer that bail-out actually passed. Held as an opaque
    //! address so nothing here can be tempted to read through it.
    const void *closedDevice = nullptr;

    //! Clear every recorded and scripted item. Called between claims so one
    //! claim cannot pass on state another claim left behind.
    //!
    //! The remaining-uses field of a scripted failure needs no clear of its
    //! own: failures_ is emptied whole below, and the claim that catches an
    //! uncleared field is written against the container rather than against
    //! each field, so a field added to the entry is covered by construction.
    void reset() {
        calls.clear();
        logErrors.clear();
        logWarnings.clear();
        logDebugs.clear();
        metalLogs.clear();
        failures_.clear();
        registers_.clear();
        registerQueues_.clear();
        pllEnabled = 0;
        ipType = 0;
        distributions.clear();
        distributionFillsOnRefusal = false;
        distributionFillsFoundSlotsOnly = false;
        cfgInstance = nullptr;
        closedDevice = nullptr;
    }

    //! Script a non-success return for the matching calls. A field left at
    //! XRFDC_SCRIPT_ANY matches every value of that field.
    void scriptFailure(const std::string &name,
                       uint32_t type,
                       uint32_t tile,
                       uint32_t block,
                       int status) {
        scriptFailureDetail(name, type, tile, block, XRFDC_SCRIPT_ANY, status);
    }

    //! Script a non-success return for the matching calls, narrowed further
    //! by the one extra distinguishing argument the call carries. Only a
    //! call that supplies that argument can match.
    void scriptFailureDetail(const std::string &name,
                             uint32_t type,
                             uint32_t tile,
                             uint32_t block,
                             uint32_t detail,
                             int status) {
        pushFailure(name, type, tile, block, detail, XRFDC_SCRIPT_UNLIMITED, status);
    }

    //! Script a non-success return for the next times matching calls only,
    //! after which the entry stops matching and a later entry, or plain
    //! success, answers instead.
    //!
    //! The same selector arguments as scriptFailureDetail plus the count.
    //! This is the only way to express a fault that clears, which is what a
    //! retry that works looks like from outside: the first call fails, the
    //! second returns success, and nothing but the count distinguishes that
    //! from a call that never failed at all.
    void scriptFailureTimes(const std::string &name,
                            uint32_t type,
                            uint32_t tile,
                            uint32_t block,
                            uint32_t detail,
                            uint32_t times,
                            int status) {
        pushFailure(name, type, tile, block, detail, times, status);
    }

    //! Script the value an XRFdc_ReadReg of this base and offset returns.
    void scriptRegister(uint32_t type, uint32_t tile, uint32_t offset, uint32_t value) {
        XRFdcScriptRegKey key = {type, tile, offset};
        registers_[key] = value;
    }

    //! Queue one value for the next read of this base and offset, after which
    //! the key falls back to whatever scriptRegister set for it, or to zero
    //! when nothing did.
    //!
    //! It exists because the production reset sweep now reads one register
    //! twice inside one tile's body, once before the PLL reconfigure and once
    //! after it has failed, and the whole point of the second read is that it
    //! can disagree with the first. A sticky map cannot express that: one key
    //! holds one value, so both reads are forced to agree and the case the
    //! second read was added to detect is unscriptable.
    //!
    //! The queue is consumed in call order by every read of the key, not only
    //! by the two the sweep's cycle decision makes. A diagnostic capture of
    //! the same register sits between them on the failing path and spends an
    //! entry of its own. A claim leaning on this is therefore leaning on the
    //! recorded order as much as on the value, which is why the claims that
    //! use it assert the recorded read counts alongside the nibble they are
    //! really about.
    void scriptRegisterOnce(uint32_t type, uint32_t tile, uint32_t offset, uint32_t value) {
        XRFdcScriptRegKey key = {type, tile, offset};
        registerQueues_[key].push_back(value);
    }

    //! Record one driver call and return the status it should produce.
    //! Both halves live in one method so a stub body cannot record a call it
    //! then fails to consult the selector for, or the reverse.
    int call(const char *name, uint32_t type, uint32_t tile, uint32_t block) {
        return callDetail(name, type, tile, block, XRFDC_SCRIPT_ANY);
    }

    //! Record one call that carries an extra distinguishing argument and
    //! return the status it should produce. The recorded form is unchanged,
    //! so a check written against the four-field record keeps its meaning;
    //! only the selector sees the fifth field.
    int callDetail(const char *name, uint32_t type, uint32_t tile, uint32_t block,
                   uint32_t detail) {
        calls.push_back(describe(name, type, tile, block));
        return consumeStatusFor(name, type, tile, block, detail);
    }

    //! The status the selector produces for this call, without recording it
    //! and without consuming a use of the entry that answered.
    int statusFor(const char *name, uint32_t type, uint32_t tile, uint32_t block) const {
        return statusFor(name, type, tile, block, XRFDC_SCRIPT_ANY);
    }

    //! The status the selector produces for a call carrying an extra
    //! distinguishing argument, without recording it and without consuming a
    //! use of the entry that answered.
    //!
    //! Const, and it stays const. A peek is a different question from a
    //! call: the fixture lifecycle claim calls this directly to ask what the
    //! fixture would return, and a peek that consumed a use would change the
    //! answer it was asking about. An entry with no uses left is skipped
    //! here exactly as it is on the recording path, so the two agree about
    //! which entries are still live.
    int statusFor(const char *name, uint32_t type, uint32_t tile, uint32_t block,
                  uint32_t detail) const {
        for (size_t i = 0; i < failures_.size(); i++) {
            if (!matches(failures_[i], name, type, tile, block, detail)) continue;
            return failures_[i].status;
        }
        return 0;  // XRFDC_SUCCESS. Spelled numerically so this header does
                   // not have to include the driver shim it is consulted by.
    }

    //! The scripted contents of a register, or 0 when nothing scripted it.
    //!
    //! A queued value takes precedence over the sticky one and is spent by
    //! the read that took it, so the same key can answer differently on two
    //! successive reads. Non-const for that reason, and it is the one
    //! accessor the register stubs already call, so the queue needs no stub
    //! change to be reached.
    uint32_t registerValue(uint32_t type, uint32_t tile, uint32_t offset) {
        XRFdcScriptRegKey key = {type, tile, offset};
        std::map<XRFdcScriptRegKey, std::vector<uint32_t> >::iterator q =
            registerQueues_.find(key);
        if (q != registerQueues_.end() && !q->second.empty()) {
            const uint32_t queued = q->second.front();
            q->second.erase(q->second.begin());
            return queued;
        }
        std::map<XRFdcScriptRegKey, uint32_t>::const_iterator it = registers_.find(key);
        if (it == registers_.end()) return 0;
        return it->second;
    }

    //! How many recorded calls carry this exact name, whatever their indices.
    size_t countCalls(const std::string &name) const {
        size_t n = 0;
        for (size_t i = 0; i < calls.size(); i++) {
            if (calls[i].compare(0, name.size() + 1, name + "/") == 0) n++;
        }
        return n;
    }

    //! Whether this exact name and index tuple was recorded.
    bool sawCall(const char *name, uint32_t type, uint32_t tile, uint32_t block) const {
        std::string want = describe(name, type, tile, block);
        for (size_t i = 0; i < calls.size(); i++) {
            if (calls[i] == want) return true;
        }
        return false;
    }

    //! The recorded form of one call. A wildcard field prints as a dash, so
    //! a call that never carried a block id cannot be confused with one that
    //! carried block 0.
    static std::string describe(const char *name, uint32_t type, uint32_t tile, uint32_t block) {
        return std::string(name) + "/" + field(type) + "/" + field(tile) + "/" + field(block);
    }

  private:
    static std::string field(uint32_t value) {
        if (value == XRFDC_SCRIPT_ANY) return "-";
        return std::to_string(value);
    }

    //! Whether one entry answers this call. The one place the selector rule
    //! is written, so the consuming path and the const peek cannot drift
    //! into disagreeing about which entries are live.
    static bool matches(const XRFdcScriptFailure &entry, const char *name, uint32_t type,
                        uint32_t tile, uint32_t block, uint32_t detail) {
        if (entry.remaining == 0) return false;
        if (entry.name != name) return false;
        if (entry.type != XRFDC_SCRIPT_ANY && entry.type != type) return false;
        if (entry.tile != XRFDC_SCRIPT_ANY && entry.tile != tile) return false;
        if (entry.block != XRFDC_SCRIPT_ANY && entry.block != block) return false;
        if (entry.detail != XRFDC_SCRIPT_ANY && entry.detail != detail) return false;
        return true;
    }

    //! The status the selector produces for this call, spending one use of
    //! the entry that answered. The only path that consumes, reached from
    //! call and callDetail and from nowhere else, so an entry's uses are
    //! spent by calls the code under test actually made.
    int consumeStatusFor(const char *name, uint32_t type, uint32_t tile, uint32_t block,
                         uint32_t detail) {
        for (size_t i = 0; i < failures_.size(); i++) {
            XRFdcScriptFailure &entry = failures_[i];

            if (!matches(entry, name, type, tile, block, detail)) continue;
            if (entry.remaining != XRFDC_SCRIPT_UNLIMITED) {
                entry.remaining--;
            }
            return entry.status;
        }
        return 0;  // XRFDC_SUCCESS, for the reason statusFor states.
    }

    //! Append one selector. Both public scripting entry points come through
    //! here, so the unlimited default is one value in one place rather than
    //! a literal repeated at each of them.
    void pushFailure(const std::string &name,
                     uint32_t type,
                     uint32_t tile,
                     uint32_t block,
                     uint32_t detail,
                     uint32_t remaining,
                     int status) {
        XRFdcScriptFailure entry;
        entry.name = name;
        entry.type = type;
        entry.tile = tile;
        entry.block = block;
        entry.detail = detail;
        entry.remaining = remaining;
        entry.status = status;
        failures_.push_back(entry);
    }

    std::vector<XRFdcScriptFailure> failures_;
    std::map<XRFdcScriptRegKey, uint32_t> registers_;

    //! Values queued for the next reads of a key, oldest first. Kept apart
    //! from the sticky map rather than folded into it, so a key with nothing
    //! queued answers exactly as it did before this existed.
    std::map<XRFdcScriptRegKey, std::vector<uint32_t> > registerQueues_;
};

//! The one fixture instance. Defined in xrfdcStub.cpp.
extern XRFdcScript gScript;

#endif  /* __XRFDC_SCRIPT_H__ */
