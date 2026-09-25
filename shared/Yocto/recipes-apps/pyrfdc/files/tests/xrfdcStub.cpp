/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Bodies for every AMD RF Data Converter driver entry point and
 * every libmetal entry point that PyRFdc.cpp reaches, plus the definition of
 * the single harness fixture gScript. Compiled only into the host harness
 * under files/tests/; the Yocto image build links the real driver instead.
 *
 * Every body does the same three things, in the same order:
 *
 *   1. record the call on gScript as name/type/tile/block, so a check can
 *      assert the sequence a production body performed and not only the
 *      value it returned
 *   2. consult the scripted-failure selector for a matching entry
 *   3. return XRFDC_SUCCESS unless the selector scripted otherwise
 *
 * Two behaviors are chosen rather than incidental, and a check would break
 * without them:
 *
 *   XRFdc_CheckTileEnabled, XRFdc_CheckBlockEnabled and
 *   XRFdc_CheckDigitalPathEnabled default to enabled. The production code
 *   guards its reset sweep on != XRFDC_FAILURE, so a stub that reported
 *   disabled would make the sweep skip every tile and leave nothing to
 *   exercise.
 *
 *   Every output parameter is zero-filled. Several production bodies pass
 *   the address of an uninitialized local and then read it back, so leaving
 *   outputs untouched would have the harness reading indeterminate values.
 *   Zero is also what an unscripted XRFdc_ReadReg returns, so the two
 *   unscripted paths agree.
 *
 * One body departs from step 3: XRFdc_SetQMCSettings also refuses the two
 * update sources the driver refuses on a high speed ADC tile. See its body.
 *
 * Recording convention for the four register primitives: the fourth field of
 * the recorded call is the register offset, not a block id. Those calls
 * carry no block, and an offset is what a check about them needs to name.
 * The tile type and tile id are recovered from the base address by matching
 * it against the eight bases XRFDC_CTRL_STS_BASE can produce; a base from
 * anywhere else records both as the wildcard dash.
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xrfdc.h"
#include "xrfdc_hw.h"
#include "xrfdcScript.h"

//! The one fixture instance every shim collaborator consults.
XRFdcScript gScript;

#define ANY XRFDC_SCRIPT_ANY

namespace {

//! Record the call and return the scripted status.
inline u32 rec(const char *name, u32 type, u32 tile, u32 block) {
    return static_cast<u32>(gScript.call(name, type, tile, block));
}

//! Zero an output parameter, tolerating the null the production code never
//! actually passes but that a future caller might.
template <typename T>
inline void zero(T *out) {
    if (out != nullptr) memset(out, 0, sizeof(T));
}

//! Recover the tile type and tile id from a control and status base address.
//! Matches against the bases XRFDC_CTRL_STS_BASE can produce rather than
//! inverting its arithmetic, so the two cannot disagree if the layout in
//! shim/xrfdc_hw.h is ever changed.
void decodeBase(u32 base, u32 *type, u32 *tile) {
    for (u32 t = 0; t < 2; t++) {
        for (u32 i = 0; i < 4; i++) {
            if (XRFDC_CTRL_STS_BASE(t, i) == base) {
                *type = t;
                *tile = i;
                return;
            }
        }
    }
    *type = ANY;
    *tile = ANY;
}

//! Storage the lookup and registration entry points hand back. Static, so
//! the pointers stay valid for the life of the process.
XRFdc_Config gConfig;
struct metal_device gMetalDevice;

}  // namespace

/* ------------------------------------------------------------------------ */
/* libmetal                                                                  */
/* ------------------------------------------------------------------------ */

int metal_init(struct metal_init_params *params) {
    (void)params;
    return static_cast<int>(rec("metal_init", ANY, ANY, ANY));
}

void metal_finish(void) {
    rec("metal_finish", ANY, ANY, ANY);
}

void metal_set_log_level(enum metal_log_level level) {
    rec("metal_set_log_level", static_cast<u32>(level), ANY, ANY);
}

void metal_device_close(struct metal_device *device) {
    //! Recorded, never dereferenced. The constructor's registration bail-out
    //! is the one caller that can reach here with a value the registration
    //! never produced, so reading through it would reproduce the fault a
    //! claim about this path is trying to observe.
    gScript.closedDevice = static_cast<const void *>(device);
    rec("metal_device_close", ANY, ANY, ANY);
}

//! Recorded by level, and the formatted text is kept beside the rogue log
//! lines so a check can tell a libmetal diagnostic from a rogue one.
void metal_log(enum metal_log_level level, const char *format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    if (vsnprintf(buf, sizeof(buf), format, args) < 0) {
        buf[0] = '\0';
    }
    va_end(args);
    gScript.metalLogs.push_back(buf);
    rec("metal_log", static_cast<u32>(level), ANY, ANY);
}

/* ------------------------------------------------------------------------ */
/* Initialization                                                            */
/* ------------------------------------------------------------------------ */

XRFdc_Config *XRFdc_LookupConfig(u16 DeviceId) {
    //! A scripted failure makes this return null, which is the one condition
    //! the production constructor tests it for.
    if (rec("XRFdc_LookupConfig", DeviceId, ANY, ANY) != XRFDC_SUCCESS) return nullptr;
    // The fields of this structure the production code branches on. The
    // constructor issues its clock distribution query only when IPType is at
    // least XRFDC_GEN3, so a configuration that never carried it would leave
    // that call site unreachable and any claim about it unprovable. The
    // constructor also decides per tile whether MaxSampleRate is kept or
    // replaced by its workaround value, so each tile's rate is scripted too.
    //
    // Written on every lookup rather than once, because gConfig is a static
    // that outlives each claim: a rate one claim scripted would otherwise
    // still be here for the next claim, which reset() cannot reach.
    gConfig.IPType = gScript.ipType;
    for (int j = 0; j < 4; j++) {
        gConfig.ADCTile_Config[j].MaxSampleRate = gScript.adcMaxRate;
        gConfig.DACTile_Config[j].MaxSampleRate = gScript.dacMaxRate;
    }
    return &gConfig;
}

u32 XRFdc_CfgInitialize(XRFdc *InstancePtr, XRFdc_Config *ConfigPtr) {
    //! Recorded whatever the scripted status, because the question a claim
    //! asks through it is what the constructor did to the instance after
    //! this call declined, and there is no other handle on it.
    gScript.cfgInstance = InstancePtr;
    if (InstancePtr != nullptr && ConfigPtr != nullptr) InstancePtr->RFdc_Config = *ConfigPtr;
    //! Not a write the driver makes. The driver leaves UpdateMixerScale as
    //! the caller's memory held it, and this stands in for that memory, so
    //! the value the constructor's mixer capture guard reads is the one a
    //! claim chose instead of whatever the heap held.
    if (InstancePtr != nullptr) InstancePtr->UpdateMixerScale = gScript.instanceUpdateMixerScale;
    return rec("XRFdc_CfgInitialize", ANY, ANY, ANY);
}

u32 XRFdc_RegisterMetal(XRFdc *InstancePtr, u16 DeviceId, struct metal_device **DevicePtr) {
    (void)InstancePtr;
    const u32 status = rec("XRFdc_RegisterMetal", DeviceId, ANY, ANY);
    //! Left unwritten on a failure, which is what the driver does. Both of
    //! the vendor call's failure exits, the device name lookup and the
    //! device open, return without touching the out parameter, so a stub
    //! that writes it before consulting the status is kinder than the driver
    //! and hides a whole class of defect from every claim that drives this
    //! path: the caller's local keeps whatever it already held and any use
    //! of it looks like a use of a pointer the registration produced.
    if (status == XRFDC_SUCCESS && DevicePtr != nullptr) *DevicePtr = &gMetalDevice;
    return status;
}

/* ------------------------------------------------------------------------ */
/* Tile control                                                              */
/* ------------------------------------------------------------------------ */

u32 XRFdc_StartUp(XRFdc *InstancePtr, u32 Type, int Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_StartUp", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_Shutdown(XRFdc *InstancePtr, u32 Type, int Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_Shutdown", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_Reset(XRFdc *InstancePtr, u32 Type, int Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_Reset", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_CustomStartUp(XRFdc *InstancePtr, u32 Type, int Tile_Id, u32 StartState, u32 EndState) {
    (void)InstancePtr;
    (void)StartState;
    (void)EndState;
    return rec("XRFdc_CustomStartUp", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_CheckTileEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_CheckTileEnabled", Type, Tile_Id, ANY);
}

u32 XRFdc_CheckBlockEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_CheckBlockEnabled", Type, Tile_Id, Block_Id);
}

u32 XRFdc_CheckDigitalPathEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_CheckDigitalPathEnabled", Type, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Status                                                                    */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetIPStatus(XRFdc *InstancePtr, XRFdc_IPStatus *IPStatusPtr) {
    (void)InstancePtr;
    zero(IPStatusPtr);
    return rec("XRFdc_GetIPStatus", ANY, ANY, ANY);
}

u32 XRFdc_GetBlockStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_BlockStatus *BlockStatusPtr) {
    (void)InstancePtr;
    zero(BlockStatusPtr);
    return rec("XRFdc_GetBlockStatus", Type, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Mixer, QMC and coarse delay                                               */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetMixerSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                           XRFdc_Mixer_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetMixerSettings", Type, Tile_Id, Block_Id);
}

//! Also keeps the two settings fields that tell the constructor's declared
//! off default from a captured one, so a claim can ask what the reset sweep
//! wrote into a block and not only whether it wrote.
u32 XRFdc_SetMixerSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                           XRFdc_Mixer_Settings *Settings) {
    (void)InstancePtr;
    if (Settings != nullptr) {
        XRFdcScriptMixerWrite write = {Type, Tile_Id, Block_Id, Settings->MixerType,
                                       Settings->CoarseMixFreq};
        gScript.mixerWrites.push_back(write);
    }
    return rec("XRFdc_SetMixerSettings", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetQMCSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_QMC_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    if (Settings != nullptr) Settings->EventSource = gScript.qmcEventSource;
    return rec("XRFdc_GetQMCSettings", Type, Tile_Id, Block_Id);
}

//! Refuses the two update sources the driver refuses on a high speed ADC
//! tile, which is the check in xrfdc_ap.c that fails with "event source is
//! not supported in 4GSPS ADC". A stub that accepted them would be kinder
//! than the driver and would hide a reset that replays a captured source the
//! setter cannot take. The tile's speed is read through the selector without
//! recording a call, because the driver asks it internally and no production
//! code made that call.
u32 XRFdc_SetQMCSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_QMC_Settings *Settings) {
    (void)InstancePtr;
    const u32 status = rec("XRFdc_SetQMCSettings", Type, Tile_Id, Block_Id);
    if (status != XRFDC_SUCCESS) return status;
    if ((Type == XRFDC_ADC_TILE) && (Settings != nullptr) &&
        (gScript.statusFor("XRFdc_IsHighSpeedADC", ANY, Tile_Id, ANY) == 1) &&
        ((Settings->EventSource == XRFDC_EVNT_SRC_IMMEDIATE) ||
         (Settings->EventSource == XRFDC_EVNT_SRC_SLICE))) {
        metal_log(METAL_LOG_ERROR,
                  "\n Invalid Event Source, event source is not supported in 4GSPS ADC (%u)"
                  " for ADC %u block %u in %s\r\n",
                  Settings->EventSource, Tile_Id, Block_Id, __func__);
        return XRFDC_FAILURE;
    }
    return status;
}

u32 XRFdc_GetCoarseDelaySettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                                 XRFdc_CoarseDelay_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetCoarseDelaySettings", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetCoarseDelaySettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                                 XRFdc_CoarseDelay_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetCoarseDelaySettings", Type, Tile_Id, Block_Id);
}

//! The event is passed to the selector but deliberately not added to the
//! recorded form. The reset sweep reaches this entry point twice per block
//! with the same type, tile and block, once for the quadrature event and
//! once for the mixer event, so a selector that could not tell them apart
//! would make the two call sites indistinguishable to any claim about
//! either. Widening the recorded string instead would change what every
//! check already written against name/type/tile/block matches.
u32 XRFdc_UpdateEvent(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 Event) {
    (void)InstancePtr;
    return static_cast<u32>(
        gScript.callDetail("XRFdc_UpdateEvent", Type, Tile_Id, Block_Id, Event));
}

/* ------------------------------------------------------------------------ */
/* Interpolation and decimation                                              */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetInterpolationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor) {
    (void)InstancePtr;
    zero(Factor);
    return rec("XRFdc_GetInterpolationFactor", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetInterpolationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor) {
    (void)InstancePtr;
    (void)Factor;
    return rec("XRFdc_SetInterpolationFactor", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDecimationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor) {
    (void)InstancePtr;
    zero(Factor);
    return rec("XRFdc_GetDecimationFactor", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDecimationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor) {
    (void)InstancePtr;
    (void)Factor;
    return rec("XRFdc_SetDecimationFactor", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDecimationFactorObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor) {
    (void)InstancePtr;
    zero(Factor);
    return rec("XRFdc_GetDecimationFactorObs", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDecimationFactorObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor) {
    (void)InstancePtr;
    (void)Factor;
    return rec("XRFdc_SetDecimationFactorObs", ANY, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Fabric clocking and valid words                                           */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetFabClkOutDiv(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u16 *Div) {
    (void)InstancePtr;
    zero(Div);
    return rec("XRFdc_GetFabClkOutDiv", Type, Tile_Id, ANY);
}

u32 XRFdc_SetFabClkOutDiv(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u16 Div) {
    (void)InstancePtr;
    (void)Div;
    return rec("XRFdc_SetFabClkOutDiv", Type, Tile_Id, ANY);
}

u32 XRFdc_GetFabWrVldWords(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words) {
    (void)InstancePtr;
    zero(Words);
    return rec("XRFdc_GetFabWrVldWords", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetFabWrVldWords(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words) {
    (void)InstancePtr;
    (void)Words;
    return rec("XRFdc_SetFabWrVldWords", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetFabWrVldWordsObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words) {
    (void)InstancePtr;
    zero(Words);
    return rec("XRFdc_GetFabWrVldWordsObs", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetFabRdVldWords(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words) {
    (void)InstancePtr;
    zero(Words);
    return rec("XRFdc_GetFabRdVldWords", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetFabRdVldWords(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words) {
    (void)InstancePtr;
    (void)Words;
    return rec("XRFdc_SetFabRdVldWords", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetFabRdVldWordsObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words) {
    (void)InstancePtr;
    zero(Words);
    return rec("XRFdc_GetFabRdVldWordsObs", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetFabRdVldWordsObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words) {
    (void)InstancePtr;
    (void)Words;
    return rec("XRFdc_SetFabRdVldWordsObs", ANY, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Thresholds                                                                */
/* ------------------------------------------------------------------------ */

u32 XRFdc_ThresholdStickyClear(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                               u32 ThresholdToUpdate) {
    (void)InstancePtr;
    (void)ThresholdToUpdate;
    return rec("XRFdc_ThresholdStickyClear", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetThresholdClrMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 ThresholdToUpdate,
                              u32 ClrMode) {
    (void)InstancePtr;
    (void)ThresholdToUpdate;
    (void)ClrMode;
    return rec("XRFdc_SetThresholdClrMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetThresholdSettings(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                               XRFdc_Threshold_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetThresholdSettings", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetThresholdSettings(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                               XRFdc_Threshold_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetThresholdSettings", ANY, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Decoder, NCO and FIFO                                                     */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetDecoderMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *DecoderMode) {
    (void)InstancePtr;
    zero(DecoderMode);
    return rec("XRFdc_GetDecoderMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDecoderMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 DecoderMode) {
    (void)InstancePtr;
    (void)DecoderMode;
    return rec("XRFdc_SetDecoderMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_ResetNCOPhase(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_ResetNCOPhase", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetupFIFO(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable) {
    (void)InstancePtr;
    (void)Enable;
    return rec("XRFdc_SetupFIFO", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_SetupFIFOObs(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable) {
    (void)InstancePtr;
    (void)Enable;
    return rec("XRFdc_SetupFIFOObs", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_SetupFIFOBoth(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable) {
    (void)InstancePtr;
    (void)Enable;
    return rec("XRFdc_SetupFIFOBoth", Type, static_cast<u32>(Tile_Id), ANY);
}

u32 XRFdc_GetFIFOStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 *Enable) {
    (void)InstancePtr;
    zero(Enable);
    return rec("XRFdc_GetFIFOStatus", Type, Tile_Id, ANY);
}

u32 XRFdc_GetFIFOStatusObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 *Enable) {
    (void)InstancePtr;
    zero(Enable);
    return rec("XRFdc_GetFIFOStatusObs", Type, Tile_Id, ANY);
}

u32 XRFdc_IsFifoEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsFifoEnabled", Type, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Analog path                                                               */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetOutputCurr(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *OutputCurr) {
    (void)InstancePtr;
    zero(OutputCurr);
    return rec("XRFdc_GetOutputCurr", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetNyquistZone(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         u32 *NyquistZone) {
    (void)InstancePtr;
    zero(NyquistZone);
    return rec("XRFdc_GetNyquistZone", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetNyquistZone(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 NyquistZone) {
    (void)InstancePtr;
    (void)NyquistZone;
    return rec("XRFdc_SetNyquistZone", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetInvSincFIR(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u16 *Mode) {
    (void)InstancePtr;
    zero(Mode);
    return rec("XRFdc_GetInvSincFIR", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetInvSincFIR(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u16 Mode) {
    (void)InstancePtr;
    (void)Mode;
    return rec("XRFdc_SetInvSincFIR", ANY, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Calibration                                                               */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetCalibrationMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u8 *CalibrationMode) {
    (void)InstancePtr;
    zero(CalibrationMode);
    return rec("XRFdc_GetCalibrationMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetCalibrationMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u8 CalibrationMode) {
    (void)InstancePtr;
    (void)CalibrationMode;
    return rec("XRFdc_SetCalibrationMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_DisableCoefficientsOverride(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType) {
    (void)InstancePtr;
    (void)CalType;
    return rec("XRFdc_DisableCoefficientsOverride", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetCalCoefficients(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType,
                             XRFdc_Calibration_Coefficients *Coeffs) {
    (void)InstancePtr;
    (void)CalType;
    zero(Coeffs);
    return rec("XRFdc_GetCalCoefficients", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetCalCoefficients(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType,
                             XRFdc_Calibration_Coefficients *Coeffs) {
    (void)InstancePtr;
    (void)CalType;
    (void)Coeffs;
    return rec("XRFdc_SetCalCoefficients", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetCalFreeze(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                       XRFdc_Cal_Freeze_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetCalFreeze", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetCalFreeze(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                       XRFdc_Cal_Freeze_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetCalFreeze", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDither(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode) {
    (void)InstancePtr;
    zero(Mode);
    return rec("XRFdc_GetDither", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDither(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode) {
    (void)InstancePtr;
    (void)Mode;
    return rec("XRFdc_SetDither", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDACDataScaler(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Enable) {
    (void)InstancePtr;
    zero(Enable);
    return rec("XRFdc_GetDACDataScaler", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDACDataScaler(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Enable) {
    (void)InstancePtr;
    (void)Enable;
    return rec("XRFdc_SetDACDataScaler", ANY, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Clocking                                                                  */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetClockSource(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *ClockSource) {
    (void)InstancePtr;
    zero(ClockSource);
    return rec("XRFdc_GetClockSource", Type, Tile_Id, ANY);
}

u32 XRFdc_GetPLLConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, XRFdc_PLL_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    // The one field of this structure the production code branches on. The
    // reset sweep performs its first XRFdc_Reset of a tile only when this
    // is non-zero, so a zero-filled structure leaves that call site
    // unreachable and any claim about it unprovable.
    if (Settings != nullptr) Settings->Enabled = gScript.pllEnabled;
    return rec("XRFdc_GetPLLConfig", Type, Tile_Id, ANY);
}

u32 XRFdc_GetPLLLockStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *LockStatus) {
    (void)InstancePtr;
    zero(LockStatus);
    return rec("XRFdc_GetPLLLockStatus", Type, Tile_Id, ANY);
}

u32 XRFdc_GetClkDistribution(XRFdc *InstancePtr,
                             XRFdc_Distribution_System_Settings *DistributionArrayPtr) {
    (void)InstancePtr;

    // The selector is consulted before anything is written rather than after,
    // which inverts the order the file header states for every other body.
    // The reason is the one the registration stub gives: the driver's own
    // refusal path returns without touching the out parameter, so a stub that
    // filled it and then reported a failure would be kinder than the driver
    // and would hide the ungrouped fallback that the whole of this work rests
    // on. Recording still happens through rec below, so the call list is
    // unchanged either way.
    //
    // gScript.distributionFillsOnRefusal opts out of that ordering and fills
    // the array whatever the scripted status says. It is opt-in rather than
    // the default on purpose: the default models the driver's documented
    // refusal path, and making the kinder shape the default would change what
    // every claim already written against this stub is asserting. With the
    // flag set, what a passing claim rests on is the production success test
    // around the cache write and nothing else, which is exactly the
    // separation the flag exists to make.
    const bool fill =
        (static_cast<u32>(gScript.statusFor("XRFdc_GetClkDistribution", ANY, ANY, ANY)) ==
         XRFDC_SUCCESS) ||
        gScript.distributionFillsOnRefusal;

    if (fill && DistributionArrayPtr != nullptr) {
        // What happens to the slots the fixture did not push is the third
        // shape this body can take, and gScript.distributionFillsFoundSlotsOnly
        // selects it.
        //
        // Stated as a property of a driver rather than of this project. A
        // driver that fills only the slots it found leaves the caller's own
        // bytes in every other slot, so the consumer's unused-slot test is
        // meaningful only if the caller wrote the sentinel there before the
        // call. A stub that always writes the sentinel itself is kinder than
        // such a driver and hides that dependency, which is the same class of
        // problem the ordering argument above records for the refusal path.
        //
        // Opt in and defaulting off, so the two shapes below are exactly what
        // every claim written before this branch existed is still asserting.
        if (!gScript.distributionFillsFoundSlotsOnly) {
            zero(DistributionArrayPtr);

            // A memset alone would leave every slot reading as sourced by tile 0,
            // which is a real tile, so every unused slot is marked explicitly.
            for (size_t slot = 0; slot < 8; slot++) {
                DistributionArrayPtr->Distributions[slot].SourceTileId = XRFDC_CLK_DST_INVALID;
            }
        }

        for (size_t slot = 0; slot < gScript.distributions.size() && slot < 8; slot++) {
            const XRFdcScriptDistribution &src = gScript.distributions[slot];
            XRFdc_Distribution_Settings &dst = DistributionArrayPtr->Distributions[slot];

            dst.SourceType = src.sourceType;
            dst.SourceTileId = src.sourceTileId;
            dst.EdgeTypes[0] = src.edgeTypes[0];
            dst.EdgeTypes[1] = src.edgeTypes[1];
            dst.EdgeTileIds[0] = src.edgeTileIds[0];
            dst.EdgeTileIds[1] = src.edgeTileIds[1];
        }
    }

    return rec("XRFdc_GetClkDistribution", ANY, ANY, ANY);
}

u32 XRFdc_DynamicPLLConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 Source, double RefClkFreq,
                           double SampleRate) {
    (void)InstancePtr;
    (void)Source;
    (void)RefClkFreq;
    (void)SampleRate;
    return rec("XRFdc_DynamicPLLConfig", Type, Tile_Id, ANY);
}

/* ------------------------------------------------------------------------ */
/* Datapath and power                                                        */
/* ------------------------------------------------------------------------ */

u32 XRFdc_GetCoupling(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Mode) {
    (void)InstancePtr;
    zero(Mode);
    return rec("XRFdc_GetCoupling", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetDSA(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, XRFdc_DSA_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetDSA", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDSA(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, XRFdc_DSA_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetDSA", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDACVOP(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 uACurrent) {
    (void)InstancePtr;
    (void)uACurrent;
    return rec("XRFdc_SetDACVOP", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDACCompMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Enable) {
    (void)InstancePtr;
    zero(Enable);
    return rec("XRFdc_GetDACCompMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDACCompMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Enable) {
    (void)InstancePtr;
    (void)Enable;
    return rec("XRFdc_SetDACCompMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetDataPathMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode) {
    (void)InstancePtr;
    zero(Mode);
    return rec("XRFdc_GetDataPathMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetDataPathMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode) {
    (void)InstancePtr;
    (void)Mode;
    return rec("XRFdc_SetDataPathMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetIMRPassMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode) {
    (void)InstancePtr;
    zero(Mode);
    return rec("XRFdc_GetIMRPassMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetIMRPassMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode) {
    (void)InstancePtr;
    (void)Mode;
    return rec("XRFdc_SetIMRPassMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetSignalDetector(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                            XRFdc_Signal_Detector_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetSignalDetector", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_SetSignalDetector(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                            XRFdc_Signal_Detector_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetSignalDetector", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_ResetInternalFIFOWidth(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_ResetInternalFIFOWidth", Type, Tile_Id, Block_Id);
}

u32 XRFdc_ResetInternalFIFOWidthObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_ResetInternalFIFOWidthObs", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetPwrMode(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                     XRFdc_Pwr_Mode_Settings *Settings) {
    (void)InstancePtr;
    zero(Settings);
    return rec("XRFdc_GetPwrMode", Type, Tile_Id, Block_Id);
}

u32 XRFdc_SetPwrMode(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                     XRFdc_Pwr_Mode_Settings *Settings) {
    (void)InstancePtr;
    (void)Settings;
    return rec("XRFdc_SetPwrMode", Type, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Topology queries                                                          */
/* ------------------------------------------------------------------------ */

u32 XRFdc_Get_TileBaseAddr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_Get_TileBaseAddr", Type, Tile_Id, ANY);
}

u32 XRFdc_Get_BlockBaseAddr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_Get_BlockBaseAddr", Type, Tile_Id, Block_Id);
}

u32 XRFdc_Get_IPBaseAddr(XRFdc *InstancePtr) {
    (void)InstancePtr;
    return rec("XRFdc_Get_IPBaseAddr", ANY, ANY, ANY);
}

u32 XRFdc_GetNoOfADCBlocks(XRFdc *InstancePtr, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetNoOfADCBlocks", ANY, Tile_Id, ANY);
}

u32 XRFdc_GetNoOfDACBlock(XRFdc *InstancePtr, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetNoOfDACBlock", ANY, Tile_Id, ANY);
}

u32 XRFdc_IsADCBlockEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsADCBlockEnabled", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_IsDACBlockEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsDACBlockEnabled", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_IsADCDigitalPathEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsADCDigitalPathEnabled", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_IsDACDigitalPathEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsDACDigitalPathEnabled", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_IsHighSpeedADC(XRFdc *InstancePtr, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_IsHighSpeedADC", ANY, Tile_Id, ANY);
}

u32 XRFdc_GetDataType(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetDataType", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetDataWidth(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetDataWidth", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetInverseSincFilter(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetInverseSincFilter", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetMixedMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetMixedMode", ANY, Tile_Id, Block_Id);
}

u32 XRFdc_GetMasterTile(XRFdc *InstancePtr, u32 Type) {
    (void)InstancePtr;
    return rec("XRFdc_GetMasterTile", Type, ANY, ANY);
}

u32 XRFdc_GetSysRefSource(XRFdc *InstancePtr, u32 Type) {
    (void)InstancePtr;
    return rec("XRFdc_GetSysRefSource", Type, ANY, ANY);
}

u32 XRFdc_GetTileLayout(XRFdc *InstancePtr) {
    (void)InstancePtr;
    return rec("XRFdc_GetTileLayout", ANY, ANY, ANY);
}

u32 XRFdc_GetMultibandConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id) {
    (void)InstancePtr;
    return rec("XRFdc_GetMultibandConfig", Type, Tile_Id, ANY);
}

u32 XRFdc_GetMaxSampleRate(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, double *MaxSampleRate) {
    (void)InstancePtr;
    zero(MaxSampleRate);
    return rec("XRFdc_GetMaxSampleRate", Type, Tile_Id, ANY);
}

u32 XRFdc_GetMinSampleRate(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, double *MinSampleRate) {
    (void)InstancePtr;
    zero(MinSampleRate);
    return rec("XRFdc_GetMinSampleRate", Type, Tile_Id, ANY);
}

double XRFdc_GetDriverVersion(void) {
    return static_cast<double>(rec("XRFdc_GetDriverVersion", ANY, ANY, ANY));
}

double XRFdc_GetFabClkFreq(XRFdc *InstancePtr, u32 Type, u32 Tile_Id) {
    (void)InstancePtr;
    return static_cast<double>(rec("XRFdc_GetFabClkFreq", Type, Tile_Id, ANY));
}

int XRFdc_GetConnectedIData(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return static_cast<int>(rec("XRFdc_GetConnectedIData", Type, Tile_Id, Block_Id));
}

int XRFdc_GetConnectedQData(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id) {
    (void)InstancePtr;
    return static_cast<int>(rec("XRFdc_GetConnectedQData", Type, Tile_Id, Block_Id));
}

/* ------------------------------------------------------------------------ */
/* Interrupts                                                                */
/* ------------------------------------------------------------------------ */

u32 XRFdc_IntrEnable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask) {
    (void)InstancePtr;
    (void)IntrMask;
    return rec("XRFdc_IntrEnable", Type, Tile_Id, Block_Id);
}

u32 XRFdc_IntrDisable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask) {
    (void)InstancePtr;
    (void)IntrMask;
    return rec("XRFdc_IntrDisable", Type, Tile_Id, Block_Id);
}

u32 XRFdc_IntrClr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask) {
    (void)InstancePtr;
    (void)IntrMask;
    return rec("XRFdc_IntrClr", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetIntrStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *IntrStsPtr) {
    (void)InstancePtr;
    zero(IntrStsPtr);
    return rec("XRFdc_GetIntrStatus", Type, Tile_Id, Block_Id);
}

u32 XRFdc_GetEnabledInterrupts(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                               u32 *IntrMask) {
    (void)InstancePtr;
    zero(IntrMask);
    return rec("XRFdc_GetEnabledInterrupts", Type, Tile_Id, Block_Id);
}

/* ------------------------------------------------------------------------ */
/* Multi-tile synchronization                                                */
/* ------------------------------------------------------------------------ */

void XRFdc_MultiConverter_Init(XRFdc_MultiConverter_Sync_Config *ConfigPtr, int *PLL_CodesPtr,
                               int *T1_CodesPtr, u32 RefTile) {
    (void)PLL_CodesPtr;
    (void)T1_CodesPtr;
    zero(ConfigPtr);
    if (ConfigPtr != nullptr) ConfigPtr->RefTile = RefTile;
    rec("XRFdc_MultiConverter_Init", ANY, RefTile, ANY);
}

u32 XRFdc_MultiConverter_Sync(XRFdc *InstancePtr, u32 Type,
                              XRFdc_MultiConverter_Sync_Config *ConfigPtr) {
    (void)InstancePtr;
    (void)ConfigPtr;
    return rec("XRFdc_MultiConverter_Sync", Type, ANY, ANY);
}

u32 XRFdc_GetMTSEnable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *EnablePtr) {
    (void)InstancePtr;
    zero(EnablePtr);
    return rec("XRFdc_GetMTSEnable", Type, Tile_Id, ANY);
}

void XRFdc_MTS_Sysref_Config(XRFdc *InstancePtr, XRFdc_MultiConverter_Sync_Config *DACSyncConfigPtr,
                             XRFdc_MultiConverter_Sync_Config *ADCSyncConfigPtr, u32 SysRefEnable) {
    (void)InstancePtr;
    (void)DACSyncConfigPtr;
    (void)ADCSyncConfigPtr;
    rec("XRFdc_MTS_Sysref_Config", SysRefEnable, ANY, ANY);
}

/* ------------------------------------------------------------------------ */
/* Register primitives. Fourth recorded field is the register offset.        */
/* ------------------------------------------------------------------------ */

u32 XRFdc_ReadReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr) {
    (void)InstancePtr;
    u32 type = ANY;
    u32 tile = ANY;
    decodeBase(BaseAddr, &type, &tile);
    rec("XRFdc_ReadReg", type, tile, RegAddr);
    return gScript.registerValue(type, tile, RegAddr);
}

void XRFdc_WriteReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 RegData) {
    (void)InstancePtr;
    (void)RegData;
    u32 type = ANY;
    u32 tile = ANY;
    decodeBase(BaseAddr, &type, &tile);
    rec("XRFdc_WriteReg", type, tile, RegAddr);
}

u32 XRFdc_RDReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 Mask) {
    (void)InstancePtr;
    u32 type = ANY;
    u32 tile = ANY;
    decodeBase(BaseAddr, &type, &tile);
    rec("XRFdc_RDReg", type, tile, RegAddr);
    return gScript.registerValue(type, tile, RegAddr) & Mask;
}

void XRFdc_ClrSetReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 Mask, u32 Data) {
    (void)InstancePtr;
    (void)Mask;
    (void)Data;
    u32 type = ANY;
    u32 tile = ANY;
    decodeBase(BaseAddr, &type, &tile);
    rec("XRFdc_ClrSetReg", type, tile, RegAddr);
}
