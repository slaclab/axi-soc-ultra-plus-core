/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Wrapper on the XRFDC bare metal function class for rogue access
 * ----------------------------------------------------------------------------
 * Complementary mapping to Rfdc, RfdcTile, and RfdcBlock python classes
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py
 * https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcBlock.py
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

#ifndef __PYTHON_XRFDC_MODULE_H__
#define __PYTHON_XRFDC_MODULE_H__
#include "rogue/Directives.h"

#include <stdint.h>

#include <memory>
#include <thread>
#include <vector>

#include "rogue/interfaces/memory/Slave.h"

#ifdef __BAREMETAL__
#include "xparameters.h"
#endif
#include "xrfdc.h"

#ifndef NO_PYTHON
    #include <boost/python.hpp>
#endif

//! Why the PyRFdc constructor did not leave a usable driver instance behind.
//!
//! Deliberately outside every conditional compilation block, so all six
//! values are defined in both build configurations and only which of them
//! can be produced differs. Two of the constructor's early returns are
//! build specific: the baremetal readiness check runs only under
//! __BAREMETAL__ and the libmetal device registration only without it. Codes
//! assigned by position would therefore mean different things in different
//! binaries, and a host decoding one would have to know which build it was
//! talking to. With a fixed set it does not.
//!
//! The values are read back over the register at offset 0x1200C and are part
//! of this driver's address space contract, so they are assigned explicitly
//! rather than left to declaration order.
enum PyRFdcInitFailReason {
    PYRFDC_INIT_OK                    = 0,  //!< Construction completed
    PYRFDC_INIT_FAIL_NOT_COMPLETED    = 1,  //!< Neither bailed out nor completed
    PYRFDC_INIT_FAIL_BAREMETAL_LOOKUP = 2,  //!< Baremetal readiness lookup failed
    PYRFDC_INIT_FAIL_METAL_INIT       = 3,  //!< libmetal initialization failed
    PYRFDC_INIT_FAIL_CONFIG_LOOKUP    = 4,  //!< Driver configuration lookup failed
    PYRFDC_INIT_FAIL_REGISTER_METAL   = 5   //!< libmetal device registration failed
};

//! What one tile is to its clock distribution group.
//!
//! Declared outside every conditional compilation block for the same reason
//! the failure reasons above are: the values are read back over the register
//! at offset 0x12014 and are part of this driver's address space contract,
//! so they are assigned explicitly rather than left to declaration order.
enum PyRFdcClkDistRole {
    PYRFDC_CLKDIST_UNGROUPED = 0,  //!< Takes its own clock, or nothing said otherwise
    PYRFDC_CLKDIST_MASTER    = 1,  //!< Sources the distribution its group runs on
    PYRFDC_CLKDIST_EDGE      = 2   //!< Takes its clock from another tile
};

//! Where the cached clock distribution topology came from.
//!
//! Read back over the register at offset 0x12010 and part of the same
//! address space contract, so these are assigned explicitly too. A host
//! reading a topology needs to know which layer answered, because the
//! layers do not carry the same confidence.
enum PyRFdcClkDistSource {
    PYRFDC_CLKDIST_SRC_NONE       = 0,  //!< No topology was obtained
    PYRFDC_CLKDIST_SRC_API        = 1,  //!< The documented distribution getter answered
    PYRFDC_CLKDIST_SRC_RAW_DECODE = 2,  //!< A raw clock-detect decode answered
    //! The reported IP generation was outside the range this driver knows
    //! how to ask, so no source was consulted at all. A fourth value and not
    //! a reuse of the first: none means asked and refused, and a reader of a
    //! hardware register needs asked-and-refused to stay distinct from
    //! never-asked-because-the-reported-part-type-is-not-a-part-type.
    PYRFDC_CLKDIST_SRC_UNSUPPORTED_GEN = 3
};

//! The highest reported IP generation this driver will put the documented
//! distribution question to.
//!
//! Deliberately a constant of this project's own and not a vendor symbol.
//! The real vendor header is not present on the host this work was done on,
//! so a new hard dependency on a symbol only the test shim guarantees would
//! surface as a build break in every downstream project that consumes this
//! file rather than as a compile error here.
//!
//! Set one above XRFDC_GEN3, so a genuine next-generation part still takes
//! the documented call and only a value no generation of this part could
//! carry falls off the top. The one out-of-range value this project has
//! measured on its own hardware is 255, far above it, read from a device
//! tree node whose parameter property was zero bytes.
#define PYRFDC_IPTYPE_MAX_KNOWN 3U

//! The cycle count nibble value reserved for a tile whose count is not
//! exact, and the highest value a real count is allowed to report.
//!
//! 0xF is reserved so a reader can tell a record the driver cannot stand
//! behind from a number, and a real count is capped one below it so counting
//! can never reach the reserved value.
#define PYRFDC_RESET_CYCLES_NOT_EXACT 0xFU
#define PYRFDC_RESET_CYCLES_MAX_REPORTED 0xEU

//! Memory interface Emlator device
/** This memory will respond to transactions, emilator hardware by responding to read
 * and write transactions.
 */
class PyRFdc : public rogue::interfaces::memory::Slave {
    //! Log
    std::shared_ptr<rogue::Logging> log_;

    //! Lock
    std::mutex mtx_;

    //! RFdc driver instance
    XRFdc RFdcInst_;
    XRFdc *RFdcInstPtr_ = &RFdcInst_;

    //! Local variables
    std::string errMsg_;

    //! Whether errMsg_ currently holds a diagnostic that the metal error
    //! bypass must not discard. Set together with the message by
    //! setDiagError and cleared where errMsg_ is cleared, once per
    //! transaction, so the two cannot drift apart.
    bool errMsgProtected_ = false;

    //! The four members behind the offsets the guard keeps answerable on an
    //! instance whose construction did not complete.
    //!
    //! Initialized here at their declaration for the same reason the two
    //! members below are. They are also assigned in the constructor's local
    //! variable block, but every one of the constructor's early returns
    //! happens before that block, so on exactly the paths the guard exists
    //! for they would otherwise hold whatever the storage contained. An
    //! offset the guard was extended to keep askable has to answer with a
    //! declared value, because the caller asking it is a host trying to find
    //! out why the driver is dead, and the alternative is handing that caller
    //! the contents of this process's memory.
    uint32_t scratchPad_ = 0;
    double doubleTestReg_ = 0.0;
    bool metalLogLevel_ = false;
    bool ignoreMetalError_ = false;

    //! Whether the constructor left a usable driver instance behind, and if
    //! not, which step it died at.
    //!
    //! Initialized here at their declaration and nowhere else, because every
    //! one of the constructor's early returns happens before the local
    //! variable block further down that file, so anything initialized there
    //! is never initialized on a construction that failed. Defaulting to not
    //! valid and not completed makes an unanticipated path, or one added
    //! later, dead by default rather than alive by default. The cost of a
    //! false positive is a rejected transaction and a clear message; the
    //! cost of a false negative is a read through a driver instance that was
    //! never initialized.
    bool driverValid_ = false;
    uint32_t initFailReason_ = PYRFDC_INIT_FAIL_NOT_COMPLETED;

    //! Whether metal_init succeeded and metal_finish has not run yet.
    //!
    //! Teardown of libmetal is owned by exactly one of the constructor and
    //! the destructor and never by both, and this flag is what makes that
    //! decidable: the constructor clears it wherever it releases the library
    //! itself, so the destructor releasing only while it is set means the
    //! library is released once on every path.
    //!
    //! It answers a different question from driverValid_ just above, which
    //! is why it is a second flag rather than a reuse of that one. The two
    //! diverge on the path where XRFdc_CfgInitialize reported non-success:
    //! there libmetal is up and the driver instance is not usable, and that
    //! is precisely the path on which the destructor has to perform the
    //! release. Gating the release on driverValid_ would leak the bring-up
    //! on that path for the life of the process.
    //!
    //! Declared unconditionally, outside every conditional compilation
    //! block, for the same reason the failure reason enumerators are. Note
    //! that on a baremetal build the destructor's teardown block is compiled
    //! out in its entirety, so there this flag is set and never read.
    bool metalReady_ = false;

    bool rdTxn_;
    bool isADC_;
    uint8_t tileId_;
    uint32_t tileType_;
    uint8_t blockId_;
    uint32_t data_;

    XRFdc_MultiConverter_Sync_Config mtsConfig_[2];
    uint32_t mtsfactor_[2][4];

    uint32_t clkSrcDefault_[2][4];
    uint32_t clkSrcConfig_[2][4];

    XRFdc_PLL_Settings pllDefault_[2][4];
    XRFdc_PLL_Settings pllConfig_[2][4];

    XRFdc_QMC_Settings qmcDefault_[2][4][4];
    XRFdc_QMC_Settings qmcConfig_[2][4][4];

    XRFdc_Mixer_Settings mixerDefault_[2][4][4];
    XRFdc_Mixer_Settings mixerConfig_[2][4][4];

    //! What one tile of a global reset did, and what its control and status
    //! registers said at the moment it did it.
    //!
    //! Plain old data on purpose. The global reset path fills one of these
    //! per tile while the driver is already reporting an error, so nothing
    //! here allocates: step points at a string literal rather than owning a
    //! copy, and the whole set lives in a fixed member array.
    //!
    //! diagRead is the difference between a register that read zero and a
    //! register that was never read. Both are absent from a struct of plain
    //! zeros, and reporting the second as the first is how a snapshot comes
    //! to say every tile is in state 0 when the console says otherwise.
    struct TileDiag {
        bool failed;          //!< This tile returned non-success during the sweep
        const char *step;     //!< Driver function that returned non-success, a literal
        bool diagRead;        //!< The reads below actually ran and their values mean something
        uint32_t restartState;
        uint32_t currentState;
        uint32_t clockDetector;
        uint32_t commonStatus;
        uint32_t pllLock;
    };

    //! One record per tile, indexed by tile type then tile id, the same way
    //! every shadow array above is indexed.
    TileDiag tileDiag_[2][4];

    //! What one tile is to the board's clock distribution.
    //!
    //! Plain old data, for the same reason TileDiag is: it is filled once at
    //! construction and read on a path that may be running while the driver
    //! is already reporting an error, so nothing here allocates.
    //!
    //! masterType and masterTile are the tile that sources this tile's
    //! clock, and they mean nothing when role is ungrouped. They are 0xFF
    //! rather than 0 for exactly that case: a zero pair reads as mastered by
    //! ADC tile 0, which is a real tile and a false statement.
    struct TileClkDist {
        uint8_t role;        //!< One of the PyRFdcClkDistRole values
        uint8_t masterType;  //!< Tile type of this tile's distribution master
        uint8_t masterTile;  //!< Tile id of this tile's distribution master
    };

    //! One record per tile, indexed by tile type then tile id, the same way
    //! every shadow array above is indexed.
    //!
    //! Initialized here at its declaration and not only in the constructor,
    //! for the reason stated on the four members further up: every one of
    //! the constructor's early returns happens before its local variable
    //! block, and this array is read by a register the dead-driver guard
    //! admits, so on exactly the paths that guard exists for it would
    //! otherwise hand a host the contents of this process's memory.
    TileClkDist clkDist_[2][4] = {
        {{PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF}},
        {{PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF},
         {PYRFDC_CLKDIST_UNGROUPED, 0xFF, 0xFF}}};

    //! Which layer produced the cached topology, how many distribution
    //! groups it described, and the IP generation the driver reported.
    //!
    //! Declared with values here for the same reason clkDist_ is. No source
    //! and no groups is the no-distribution answer, which is what every path
    //! that obtains no topology has to leave behind. 0xFF for the IP
    //! generation means never captured, which is distinguishable from every
    //! generation the driver can report.
    uint32_t clkDistSource_ = PYRFDC_CLKDIST_SRC_NONE;
    uint32_t clkDistGroups_ = 0;
    uint32_t ipType_ = 0xFF;

    //! How many IPSM cycles the last global reset issued per tile, indexed
    //! by tile type then tile id, the same way every shadow array above is
    //! indexed.
    //!
    //! A count of cycles and not of XRFdc_Reset calls. A tile whose PLL
    //! reconfigure performed the cycle internally receives no explicit
    //! reset and is still counted here, because what a reader needs is how
    //! many times the tile was driven through its state machine.
    //!
    //! Initialized here at its declaration and not only in the constructor,
    //! for the reason stated on the members above: every one of the
    //! constructor's early returns happens before its local variable block,
    //! and this array is read by a register the dead-driver guard admits,
    //! so on exactly the paths that guard exists for it would otherwise
    //! hand a host the contents of this process's memory.
    uint32_t resetCycles_[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};

    //! Whether the count beside it is a figure this driver can stand behind,
    //! indexed the same way.
    //!
    //! Set for a tile whose PLL reconfigure returned non-success at a point
    //! where this code cannot tell whether the call had already cycled the
    //! tile. The true figure for such a tile is one or two, and the register
    //! publishes the reserved nibble rather than picking one of them.
    //!
    //! Declared with values here for the reason the array above is: this one
    //! is read by the same register on the same paths the dead-driver guard
    //! exists for.
    bool resetCyclesInexact_[2][4] = {{false, false, false, false},
                                      {false, false, false, false}};

    //! How many clock group recoveries have been armed since construction,
    //! and how many of those succeeded.
    //!
    //! Counted since construction and not per reset, so a host reading them
    //! after a boot learns whether any recovery fired at all rather than
    //! only what the most recent reset did.
    //!
    //! Two counts and not one, because never armed, armed and failed, and
    //! armed and succeeded are three different findings and a single number
    //! collapses two of them. A recovery that succeeded leaves the reset
    //! returning success, which is the shape of a reset that never needed
    //! one, so these are the only things standing between a silent retry and
    //! a run of clean reboots that were every one of them a recovery.
    //!
    //! Nothing here is evidence that a recovery works on any board. They
    //! record that one was attempted and what it returned, and no more.
    //!
    //! Initialized here at their declaration and not only in the
    //! constructor, for the reason stated on the members above: every one of
    //! the constructor's early returns happens before its local variable
    //! block, and these are read by a register the dead-driver guard admits,
    //! so on exactly the paths that guard exists for they would otherwise
    //! hand a host the contents of this process's memory.
    uint32_t recoveriesArmed_ = 0;
    uint32_t recoveriesSucceeded_ = 0;

    //! Application functions
    void StartUp(int Tile_Id);
    void Shutdown(int Tile_Id);
    void Reset(int Tile_Id);
    void CustomStartUp(int Tile_Id);
    void GetIPStatus();
    void GetBlockStatus(uint8_t index);
    void MixerSettings(uint8_t index);
    void QMCSettings(uint8_t index);
    void CoarseDelaySettings();
    void UpdateEvent(uint32_t XRFDC_EVENT);
    void InterpolationFactor();
    void DecimationFactor();
    void DecimationFactorObs();
    void FabClkOutDiv();
    void FabWrVldWords();
    void FabWrVldWordsObs();
    void FabRdVldWords();
    void FabRdVldWordsObs();
    void ThresholdStickyClear();
    void ThresholdClrMode();
    void ThresholdSettings(uint8_t index);
    void DecoderMode();
    void ResetNCOPhase();
    void SetupFIFO(int Tile_Id);
    void SetupFIFOObs(int Tile_Id);
    void SetupFIFOBoth(int Tile_Id);
    void OutputCurr();
    void FIFOStatus();
    void FIFOStatusObs();
    void NyquistZone();
    void InvSincFIR();
    void CalibrationMode();
    void DisableCoefficientsOverride();
    void CalCoefficients(uint32_t calType, uint8_t index);
    void CalFreeze(uint8_t index);
    void Dither();
    void DataScaler();
    void ClockSource();
    void PLLConfig(uint8_t index);
    void PLLLockStatus();
    void LinkCoupling();
    void DSA(uint8_t index);
    void DACVOP();
    void DACCompMode();
    void DataPathMode();
    void IMRPassMode();
    void SignalDetector(uint8_t index);
    void ResetInternalFIFOWidth();
    void ResetInternalFIFOWidthObs();
    void PwrModeSettings(uint8_t index);
    void TileBaseAddr();
    void BlockBaseAddr();
    void NoOfADCBlocks();
    void NoOfDACBlock();
    void IsADCBlockEnabled(uint8_t index);
    void IsDACBlockEnabled(uint8_t index);
    void IsHighSpeedADC();
    void DataType();
    void DataWidth();
    void InverseSincFilter();
    void MixedMode();
    void MasterTile(uint8_t index);
    void SysRefSource(uint8_t index);
    void IPBaseAddr();
    void FabClkFreq(bool upper);
    void IsFifoEnabled();
    void DriverVersion(bool upper);
    void ConnectedIData();
    void ConnectedQData();
    void IsADCDigitalPathEnabled();
    void IsDACDigitalPathEnabled();
    void CheckDigitalPathEnabled();
    void CheckBlockEnabled(uint8_t index);
    void CheckTileEnabled(uint8_t index);
    void TileLayout();
    void MultibandConfig();
    void MaxSampleRate(bool upper);
    void MinSampleRate(bool upper);
    void DynamicPLLConfig(uint8_t index);
    void IntrEnable();
    void IntrDisable();
    void IntrClr();
    void GetIntrStatus();
    void GetEnabledInterrupts();

    void MtsEnabled();
    void MtsRefTile();
    void MtsSysrefConfig();
    void MtsSysRefEnable();
    void MtsTargetLatency();
    void MtsTiles();
    void MtsSync();
    void MtsLatency(uint8_t index);
    void MtsOffset(uint8_t index);
    void MtsFactor(uint8_t index);

    void IpVersion();
    void RestartSM();
    void RestartState();
    void ClockDetector();
    void TileCommonStatus();
    void TileCurrentState();

    //! Read one tile's control and status registers into out.
    //!
    //! Takes the tile type and tile id as arguments and writes only through
    //! its out parameter. The five bodies above are transaction bodies, not
    //! value-returning functions: each writes into data_ and reads tileType_
    //! and tileId_, which doTransaction sets per word. Calling one of them
    //! from a failure path would overwrite the word the in-flight
    //! transaction is about to hand back, and would read whichever tile that
    //! transaction addressed rather than the tile being diagnosed. This
    //! helper shares no mutable state with them, so neither can happen.
    void readTileDiagnostics(uint32_t type, uint8_t tile, TileDiag *out);

    //! Mark one tile as having failed at the named driver call, and capture
    //! its control and status registers while it is still in the state that
    //! failed.
    //!
    //! step is the name of the driver function that returned non-success,
    //! always a string literal, so nothing allocates on a failure path and
    //! the text can be grepped straight back to the call site. The first
    //! step recorded for a tile is the one kept, and the registers are read
    //! at most once per tile.
    void recordTileFailure(uint32_t type, uint8_t tile, const char *step);

    //! Reset every tileDiag_ record. Called at the top of a global reset
    //! sweep, so a record belongs to one sweep and not to one transaction.
    void clearTileDiag();

    //! Render tileDiag_ as one newline-terminated line for the caller and
    //! for the console.
    std::string buildDiagMessage(const char *entryPoint, int tileId);

    //! Assign a diagnostic error message and mark it protected, in one call.
    //!
    //! Every message this driver builds from a tile record or from a
    //! constructor outcome goes through here. The alternative is an
    //! assignment and a flag set side by side at each site, which is a pair
    //! that can drift, and a site that set the message and forgot the flag
    //! would have its diagnostic silently discarded by the metal error
    //! bypass with nothing to show that it had been.
    void setDiagError(const std::string &msg);

    //! Refuse one word of a transaction when the driver instance was never
    //! initialized, and say which constructor step failed.
    //!
    //! Returns true when it rejected, in which case the caller performs no
    //! dispatch for that word. Returns false immediately when the driver is
    //! valid, so a live driver pays one boolean test per word and behaves
    //! exactly as it did.
    bool rejectIfDriverDead(uint32_t addr);

    //! Decode a queried distribution topology into the per-tile cache.
    //!
    //! A helper rather than a transaction body, for the same reason
    //! readTileDiagnostics is one: it is called from the constructor, where
    //! there is no transaction in flight, and it writes only members of its
    //! own. It reads neither tileType_ nor tileId_ and never touches data_,
    //! so it cannot disturb a word some other caller is about to hand back.
    void cacheClkDistribution(const XRFdc_Distribution_System_Settings *dist);

    //! Decode the distribution topology out of the per-tile clock detect
    //! register, without asking the driver for it.
    //!
    //! It exists because a raw register read is not IPType gated. The
    //! documented getter refuses every call on a driver that reports a part
    //! below the third generation, and it refuses it at its first branch
    //! without reading anything, so on such a board the documented source
    //! can answer nothing at all however healthy the hardware is.
    //!
    //! A helper rather than a transaction body, for the same reason
    //! readTileDiagnostics is one: it runs from the constructor, where no
    //! transaction is in flight, and it writes only members of its own. It
    //! reads neither tileType_ nor tileId_ and never touches data_, so it
    //! cannot disturb a word some other caller is about to hand back.
    void decodeClkDistributionRaw();

    //! Establish the cached topology's ordering precondition: after this
    //! runs, every tile marked as an edge names an in-range tile that is
    //! marked as a master.
    //!
    //! It lives at cache level rather than inside either decoder because
    //! every consumer reads the cache and not one of them asks which source
    //! filled it. buildOwnedTileWalk, tileIsOwnedBy, the recovery arming
    //! pass, ClkDistStatus and ClkDistMap all take the cache as the
    //! topology, so an invariant those consumers depend on belongs to the
    //! cache and not to one of the two ways of filling it. Attaching it to a
    //! decoder would also mean a third source, whenever one arrives, has to
    //! rediscover the same rule rather than inherit it.
    //!
    //! A tile whose master chain cannot reach a marked master has its
    //! grouping withdrawn and is left ungrouped, because a tile whose clock
    //! source the cache cannot resolve to an orderable master is honestly
    //! ungrouped, and the alternative of naming a master picked by tile index
    //! or by visit order would publish an ordering the registers do not
    //! support. Nothing else is demoted and a tile already marked as a master
    //! is left untouched, and the withdrawal fires only on a cache that
    //! violated the invariant, so a cache that already satisfied it comes out
    //! byte identical.
    //!
    //! A helper rather than a transaction body, for the same reason
    //! readTileDiagnostics is one: it runs from the constructor, where no
    //! transaction is in flight, and it writes only members of its own. It
    //! makes no driver call at all, which is what lets it run on the path
    //! where the documented topology query already failed.
    void normalizeClkDistCache();

    //! Whether the global reset of this tile type is the one that owns the
    //! named tile, where a tile index is type * 4 + tile.
    //!
    //! A call owns every distribution group whose master is of its own tile
    //! type, in full and including edge tiles of the other type, plus every
    //! tile of its own type that belongs to no group. It owns no group whose
    //! master is of the other type.
    //!
    //! On this carrier that makes the ADC entry point the owner of ADC 0, 1
    //! and 2, which have their own clock pins, and the DAC entry point the
    //! owner of DAC 0 as master and of ADC 3, DAC 1, DAC 2 and DAC 3 as its
    //! edges. A standalone ADC reset declining to touch ADC 3 is the point
    //! of the rule and not a gap in it: ADC 3 has no clock of its own and
    //! cannot be restarted in isolation from DAC 0.
    bool tileIsOwnedBy(uint32_t type, uint32_t idx) const;

    //! Fill walk with the tile indices the global reset of this tile type
    //! must visit, in the order it must visit them, and return how many
    //! entries were filled.
    //!
    //! A helper rather than a transaction body, for the same reason
    //! readTileDiagnostics is one: it writes only through its out parameter,
    //! reads neither tileType_ nor tileId_ and never touches data_, so it
    //! cannot disturb a word some other caller is about to hand back.
    //!
    //! It makes no driver call at all. Membership and order are derived from
    //! the cached topology and the tile type and from nothing else, so a
    //! cache that read back wrong can produce a strange walk but cannot
    //! reach the converter: every tile the walk yields still passes the
    //! enable probe before any driver call is made against it.
    //!
    //! walk must have room for eight entries, which is every tile on the
    //! part. The caller supplies it on its own frame, so nothing allocates
    //! on a path the driver may already be reporting an error from.
    uint32_t buildOwnedTileWalk(uint32_t type, uint32_t *walk) const;

    //! Render the tiles of this tile type that the global reset of this
    //! tile type left to the other entry point, as one line, or the empty
    //! string when it left none.
    //!
    //! A tile of a call's own type that the call does not own is exactly a
    //! tile it deferred to a group mastered by the other type, so this and
    //! tileIsOwnedBy above are one statement read two ways rather than two
    //! rules that can drift apart.
    //!
    //! The empty string is the common answer. Every board with no clock
    //! distribution, and every board whose distributions are mastered
    //! inside their own tile type, defers nothing and gains no per reset
    //! log output at all.
    std::string buildDeferralMessage(uint32_t type) const;

    //! Re-run one distribution group once with the explicit reset, master
    //! first and then its edge tiles in ascending tile index order, count
    //! the attempt and its outcome, and say in the log what happened.
    //!
    //! masterIdx is the tile index of the group's master and armingIdx the
    //! tile index of the edge tile whose failure armed the attempt, carried
    //! in only so the log line can name it.
    //!
    //! The explicit reset and never the PLL reconfigure. A tile that did not
    //! come back is not powered up, and the reconfigure performs no cycle at
    //! all for a tile in that state, so a recovery built on it would be a
    //! no-op on precisely the tile it exists for.
    //!
    //! One attempt, with no loop of its own. The caller bounds it to one per
    //! group per global reset, and each failed internal restart wait inside
    //! the driver costs a second on the transaction thread, so an unbounded
    //! retry on a wedged tile would hold that thread for as long as the tile
    //! stays wedged.
    //!
    //! On success it clears the failed flag and the step of the tiles it
    //! reset and leaves their captured registers alone, so the transaction
    //! completes and a message built for some other tile still reports the
    //! recovered one as observed rather than as failed. On failure it
    //! changes no record at all.
    void recoverClkGroup(uint32_t masterIdx, uint32_t armingIdx);

    void MetalLogLevel();
    void IgnoreMetalError();
    void ScratchPad();
    void InitFailReason();

    //! Where the topology came from, which IP generation the driver
    //! reported and how many groups were found, as one word at 0x12010.
    //!
    //! The source is in bits 7:0, the reported IP generation in bits 15:8
    //! and the group count in bits 23:16. Both byte fields saturate rather
    //! than wrap.
    //!
    //! The generation byte saturating at 0xFF means a true 255 and a field
    //! nothing ever captured are not distinguishable from that byte alone,
    //! which is why the source field carries a fourth value for a generation
    //! outside the range this driver asks: the distinction a reader needs
    //! lives there rather than in the saturated byte.
    void ClkDistStatus();

    //! The per-tile distribution map, four bits per tile, at 0x12014.
    void ClkDistMap();

    //! How many IPSM cycles the last global reset issued per tile, four
    //! bits per tile, at 0x12018.
    //!
    //! A transaction body of the same shape as the two above: a read hands
    //! back members and a write is refused, and it names RFdcInstPtr_
    //! nowhere, which is what lets the dead-driver guard admit it on read.
    //!
    //! A nibble counts IPSM cycles and not XRFdc_Reset calls, so a tile the
    //! PLL reconfigure cycled internally is counted here even though no
    //! explicit reset was issued for it.
    //!
    //! A nibble reading the reserved value means the tile's reconfigure
    //! returned non-success at a point where this code cannot tell whether
    //! the call had already cycled the tile, so the true figure for that
    //! tile is one or two and the driver declines to pick. A real count
    //! saturates one below the reserved value, so the two readings are
    //! disjoint by construction.
    void ResetCycleCount();

    //! How many clock group recoveries were armed and how many succeeded,
    //! armed in the low half and succeeded in the high half, at 0x1201C.
    //!
    //! A transaction body of the same shape as the three above: a read hands
    //! back members and a write is refused, and it names RFdcInstPtr_
    //! nowhere, which is what lets the dead-driver guard admit it on read.
    void RecoveryCount();
    void DoubleTestReg(bool upper);
    uint32_t DoubleToUint32(double value, bool upper);
    double RemapDoubleWithUint32(double original, uint32_t newPart, bool upper);

  public:
    //! Class factory which returns a pointer
    static std::shared_ptr<PyRFdc> create();

    //! Setup class for use in python
    static void setup_python();

    //! Create a PyRFdc device
    PyRFdc();

    //! Destroy the PyRFdc
    ~PyRFdc();

    //! Handle the incoming memory transaction
    void doTransaction(std::shared_ptr<rogue::interfaces::memory::Transaction> transaction);
};

//! Alias for using shared pointer as PyRFdcPtr
typedef std::shared_ptr<PyRFdc> PyRFdcPtr;

#endif
