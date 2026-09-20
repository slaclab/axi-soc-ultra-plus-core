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
    uint32_t scratchPad_;
    double doubleTestReg_;
    bool metalLogLevel_;
    bool ignoreMetalError_;

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

    void MetalLogLevel();
    void IgnoreMetalError();
    void ScratchPad();
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
