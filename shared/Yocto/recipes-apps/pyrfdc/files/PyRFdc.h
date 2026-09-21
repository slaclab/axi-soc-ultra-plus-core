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
    PYRFDC_CLKDIST_SRC_RAW_DECODE = 2   //!< A raw clock-detect decode answered
};

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

    void MetalLogLevel();
    void IgnoreMetalError();
    void ScratchPad();
    void InitFailReason();

    //! Where the topology came from, which IP generation the driver
    //! reported and how many groups were found, as one word at 0x12010.
    void ClkDistStatus();

    //! The per-tile distribution map, four bits per tile, at 0x12014.
    void ClkDistMap();
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
