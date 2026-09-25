/**
 * ----------------------------------------------------------------------------
 * Company    : SLAC National Accelerator Laboratory
 * ----------------------------------------------------------------------------
 * Description: Host-build shim for <xrfdc.h>, the AMD RF Data Converter
 * bare-metal driver header, plus the small slice of libmetal that PyRFdc.cpp
 * names. Shadows the real headers, which ship inside a bare-metal BSP or a
 * target sysroot and are not installed on a plain development host.
 *
 * This header exists so that PyRFdc.cpp can be compiled on the host with no
 * edits to it, by a harness that can then drive PyRFdc::doTransaction and
 * observe what it reports. It declares the driver surface the production
 * source actually reaches and nothing else. The surface was derived from the
 * file rather than from memory: the symbol lists come from
 *
 *   grep -o 'XRFdc_[A-Za-z0-9_]*' PyRFdc.cpp | sort -u
 *   grep -o 'XRFDC_[A-Za-z0-9_]*' PyRFdc.cpp | sort -u
 *   grep -o 'metal_[A-Za-z0-9_]*\|METAL_[A-Za-z0-9_]*' PyRFdc.cpp | sort -u
 *
 * run in files/. Struct members are only those the compiler demanded, so a
 * member the production code never reads is deliberately absent.
 *
 * Test-build only. See shim/rogue/Directives.h for why files/tests/ cannot
 * reach the Yocto image build.
 *
 * ----------------------------------------------------------------------------
 * Shim fidelity
 * ----------------------------------------------------------------------------
 * This shim is a second definition of the driver surface and can drift from
 * the real headers. What follows is the provenance of everything whose
 * numeric value the production code depends on, so a future reader can audit
 * that drift risk without re-deriving it.
 *
 * Five load-bearing constants, listed with the value used and its source:
 *
 *   XRFDC_SUCCESS   0   AMD PG269 RF Data Converter driver API reference.
 *                       Every return-code comparison in PyRFdc.cpp is made
 *                       against this value.
 *   XRFDC_FAILURE   1   AMD PG269, same reference. Distinct from SUCCESS is
 *                       the only property most call sites need, but the
 *                       != XRFDC_FAILURE guards in the constructor and in
 *                       Reset depend on the exact value: a third status
 *                       passes those guards and fails the SUCCESS checks.
 *   XRFDC_ADC_TILE  0   AMD PG269, corroborated in this tree by
 *                       PyRFdc.cpp:213-214 and :499-500, where the comment
 *                       "Check for ADC tile" guards the literal test
 *                       (i==0) and (tileType_==0) against the same concept.
 *   XRFDC_DAC_TILE  1   AMD PG269, corroborated by the same two sites: the
 *                       DAC is the other of two tile types.
 *   XRFDC_TILE_ID0  0   AMD PG269, the first tile index.
 *
 * The tile-type pair is the one drift that would be silent and catastrophic.
 * PyRFdc.cpp:3281 assigns tileType_ from those two constants and PyRFdc.h
 * lines 72 to 85 index [2][4] and [2][4][4] shadow arrays with it, so any
 * other values swap the ADC and DAC groups rather than failing. That is why
 * PyRFdc.cpp carries a static_assert on the pair: the assertion compiles in
 * the Yocto build as well as here, so the real xrfdc.h is pinned to the same
 * two values by the same line.
 *
 * No sysroot copy of the real xrfdc.h is reachable on this host, so none of
 * the five could be cross-checked against the header itself.
 *
 * The clock distribution symbols have stronger provenance than those five.
 * Every one of them was read out of xrfdc.h and xrfdc_hw.h at upstream tag
 * xilinx_v2026.1, which is the release the installed image was built from,
 * and those files were confirmed byte-identical to master when they were
 * read. Listed with the value used and where it came from:
 *
 *   XRFDC_GEN3              2      xrfdc.h. The constructor issues the
 *                                  distribution query only when
 *                                  RFdc_Config.IPType is at least this, and
 *                                  PyRFdc.cpp carries a static_assert on the
 *                                  number for the reason stated there.
 *   XRFDC_CLK_DST_TILE_231  0      xrfdc.h. The distribution chain's package
 *   .. _TILE_224            .. 7   tile indices, highest numbered tile
 *                                  first. The production decode maps a tile
 *                                  type and tile id onto this index space,
 *                                  so the span and the direction are load
 *                                  bearing even where the names are not
 *                                  referenced individually.
 *   XRFDC_CLK_DST_INVALID   0xFF   xrfdc.h. Marks a slot carrying no
 *                                  distribution. Compared against by the
 *                                  production decode.
 *   XRFDC_DIST_OUT_NONE     0      xrfdc.h. Opaque to the harness today:
 *   XRFDC_DIST_OUT_RX       1      the production code neither assigns nor
 *   XRFDC_DIST_OUT_OUTDIV   2      compares any of the three.
 *   XRFDC_ENABLED           1      xrfdc.h. The value a shifted clock
 *                                  detect register equals at the bit pair
 *                                  naming a tile's clock source. Compared
 *                                  against by the production raw decode,
 *                                  which copies the driver's own
 *                                  comparison, so the number is load
 *                                  bearing.
 *
 * Two more distribution symbols live in the companion shim rather than
 * here, because the real driver puts them in the register-level header:
 * XRFDC_CLOCK_DETECT_OFFSET and XRFDC_CLOCK_DETECT_SRC_MASK are defined in
 * shim/xrfdc_hw.h, with their own provenance at the definition site.
 *
 * Two structural facts about the distribution types belong here as well.
 *
 * XRFdc_Distribution_System_Settings hard-codes eight distribution slots.
 * The real header hard-codes the same eight, because the driver's own loop
 * bound XRFDC_MAX_DISTRS is defined privately inside xrfdc_clock.c and is
 * not exported, so neither the real header nor this shim can borrow it.
 *
 * The four distribution structures carry members the production code never
 * reads. That is deliberate and is the one place this file departs from its
 * own rule of carrying only what the production code touches: the
 * constructor declares one of these on its frame, and a trimmed structure
 * would make that frame a small fraction of its real size and would make
 * the scoped block it sits in look like caution about nothing.
 *
 * Every other declaration in this file is shaped to satisfy the compiler
 * rather than copied from the real header. Argument types are widened to u32
 * where the real driver uses a narrower type, parameter names are the
 * harness's own, and the struct layouts carry only the members the
 * production code touches, in whatever order reads well. The consequence is
 * the useful one: a signature change upstream shows up as a compile error in
 * this harness, not as a wrong answer from it. The reverse also holds, and
 * is the limit of this harness: a PASS here is a claim about the shim's
 * behavior, not automatically about the Yocto build's.
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

#ifndef __XRFDC_H__
#define __XRFDC_H__

#include <stdint.h>

#include "xrfdcScript.h"

/* ------------------------------------------------------------------------ */
/* Scalar aliases. The real driver gets these from <xil_types.h>.            */
/* ------------------------------------------------------------------------ */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;

/* ------------------------------------------------------------------------ */
/* Load-bearing constants. Provenance is in the Shim fidelity section above. */
/* ------------------------------------------------------------------------ */

//! Source: AMD PG269 XRFdc driver API reference.
#define XRFDC_SUCCESS 0U

//! Source: AMD PG269 XRFdc driver API reference.
#define XRFDC_FAILURE 1U

//! Source: AMD PG269, corroborated at PyRFdc.cpp:213-214 and :499-500.
#define XRFDC_ADC_TILE 0U

//! Source: AMD PG269, corroborated at PyRFdc.cpp:213-214 and :499-500.
#define XRFDC_DAC_TILE 1U

//! Source: AMD PG269 XRFdc driver API reference.
#define XRFDC_TILE_ID0 0U

/* ------------------------------------------------------------------------ */
/* Remaining constants. Each is either compared against something by the     */
/* production code, in which case the comparison is named, or assigned into  */
/* a struct field and never read back, in which case the value is opaque to  */
/* the harness and is harness-chosen.                                        */
/* ------------------------------------------------------------------------ */

//! Highest tile index. Compared against at PyRFdc.cpp:2914, which rejects a
//! reference-tile write above it. Source: AMD PG269, where the converter has
//! four tiles per type, so the maximum index is 3.
#define XRFDC_TILE_ID_MAX 3U

//! Multi-tile sync success code. Compared against at PyRFdc.cpp:2989.
//! Source: AMD PG269, where XRFDC_MTS_OK is 0.
#define XRFDC_MTS_OK 0U

//! Opaque to the harness: assigned into clkSrcDefault_ and never compared.
//! Value from AMD PG269.
#define XRFDC_EXTERNAL_CLK 0x1U

//! Opaque to the harness: assigned into EventSource fields and never
//! compared. Value from AMD PG269.
#define XRFDC_EVNT_SRC_TILE 0x2U

//! The two update sources XRFdc_SetQMCSettings refuses on a high speed ADC
//! tile. Compared by the production constructor, which replaces either one
//! in a captured quadrature default, and by the XRFdc_SetQMCSettings stub,
//! which models the refusal. Values read out of xrfdc.h at embeddedsw
//! 7637d1bd (xlnx_rel_v2026.1.1), byte-identical to the image sysroot copy.
#define XRFDC_EVNT_SRC_IMMEDIATE 0x0U
#define XRFDC_EVNT_SRC_SLICE 0x1U

//! The mixer values the constructor declares as every block's default. NOT
//! opaque: the XRFdc_SetMixerSettings stub records the settings it was
//! handed, and a claim compares MixerType and CoarseMixFreq against the off
//! pair to tell the declared default from a captured one. The zero fill the
//! getter stub performs must therefore read as that default, which it does
//! only with the driver's own values. Read out of the rfdc_v13_1 xrfdc.h
//! shipped with Vitis 2026.1.
#define XRFDC_COARSE_MIX_OFF 0x0U
#define XRFDC_MIXER_MODE_OFF 0x0U
#define XRFDC_MIXER_TYPE_OFF 0x0U

//! Opaque to the harness: assigned into mixer defaults and never compared.
//! Value from AMD PG269.
#define XRFDC_MIXER_SCALE_0P7 0x2U

//! Opaque to the harness: passed to XRFdc_UpdateEvent, which is a stub that
//! records its argument rather than acting on it. Values from AMD PG269.
#define XRFDC_EVENT_MIXER 0x1U
#define XRFDC_EVENT_CRSE_DLY 0x2U
#define XRFDC_EVENT_QMC 0x4U

//! Opaque to the harness: assigned into a threshold settings field and never
//! compared. Value from AMD PG269.
#define XRFDC_UPDATE_THRESHOLD_BOTH 0x3U

//! The IP generation at which the clock distribution API becomes available.
//! Compared against at the constructor's topology capture, which issues
//! XRFdc_GetClkDistribution only when RFdc_Config.IPType is at least this
//! value. PyRFdc.cpp carries a static_assert on the exact number, because a
//! header that defined it differently would silently flip which boards take
//! the query path rather than failing.
//! Source: xrfdc.h at upstream tag xilinx_v2026.1, the release the installed
//! image was built from, confirmed byte-identical to master.
#define XRFDC_GEN3 2U

//! Package tile indices of the clock distribution chain, highest numbered
//! tile first. The production decode maps a tile type and tile id onto this
//! index space and walks the inclusive range between a distribution's two
//! edges, so the span and the direction of these values are load bearing even
//! though not every name below is referenced by name.
//! Source: xrfdc.h at upstream tag xilinx_v2026.1, confirmed byte-identical
//! to master.
#define XRFDC_CLK_DST_TILE_231 0U
#define XRFDC_CLK_DST_TILE_230 1U
#define XRFDC_CLK_DST_TILE_229 2U
#define XRFDC_CLK_DST_TILE_228 3U
#define XRFDC_CLK_DST_TILE_227 4U
#define XRFDC_CLK_DST_TILE_226 5U
#define XRFDC_CLK_DST_TILE_225 6U
#define XRFDC_CLK_DST_TILE_224 7U

//! Marks a distribution slot that carries no distribution. Compared against
//! by the production decode, which skips a slot whose SourceTileId reads this
//! value, and written into every slot by the stub so an unfilled slot cannot
//! be mistaken for a slot sourced by tile 0.
//! Source: xrfdc.h at upstream tag xilinx_v2026.1, confirmed byte-identical
//! to master.
#define XRFDC_CLK_DST_INVALID 0xFFU

//! The value a shifted clock detect register equals when the bit pair at
//! that position names the source of a tile's clock. Load bearing rather
//! than opaque: the driver's own distribution decode performs exactly this
//! comparison, and the production raw decode copies it, so a different
//! value here would silently name a different source tile.
//! Source: xrfdc.h at upstream tag xilinx_v2026.1, confirmed byte-identical
//! to master.
#define XRFDC_ENABLED 1U

//! Distribution output modes. Opaque to the harness: the production code
//! assigns none of them and compares against none of them today. They are
//! declared here because they are part of the same structure family and a
//! later decode that names one should not have to reopen this shim.
//! Source: xrfdc.h at upstream tag xilinx_v2026.1, confirmed byte-identical
//! to master.
#define XRFDC_DIST_OUT_NONE 0U
#define XRFDC_DIST_OUT_RX 1U
#define XRFDC_DIST_OUT_OUTDIV 2U

/* ------------------------------------------------------------------------ */
/* libmetal surface. The real declarations live in <metal/init.h>,           */
/* <metal/device.h> and <metal/log.h>, none of which is installed here.      */
/* ------------------------------------------------------------------------ */

enum metal_log_level { METAL_LOG_ERROR = 3, METAL_LOG_DEBUG = 7 };

//! The production code only ever holds a pointer to one of these and passes
//! it back to metal_device_close, so the real member set is irrelevant. It
//! is defined rather than left incomplete so the stub can hand back the
//! address of one.
struct metal_device {
    const char *name;
};

struct metal_init_params {
    enum metal_log_level log_level;
};

//! Default init parameters, spelled as the real header spells them.
#define METAL_INIT_DEFAULTS \
    { METAL_LOG_ERROR }

int metal_init(struct metal_init_params *params);
void metal_finish(void);
void metal_set_log_level(enum metal_log_level level);
void metal_device_close(struct metal_device *device);
void metal_log(enum metal_log_level level, const char *format, ...);

/* ------------------------------------------------------------------------ */
/* Driver data structures. Members are only those PyRFdc.cpp reads or        */
/* writes; the real structures are considerably wider.                       */
/* ------------------------------------------------------------------------ */

typedef struct {
    double MaxSampleRate;
} XRFdc_Tile_Config;

typedef struct {
    XRFdc_Tile_Config ADCTile_Config[4];
    XRFdc_Tile_Config DACTile_Config[4];
    //! The IP generation the driver believes this part is. The only field of
    //! this structure the production code branches on, and the one the
    //! topology capture gates its query on.
    u32 IPType;
} XRFdc_Config;

/* Tagged rather than anonymous, so xrfdcScript.h can forward declare it and
 * hold a pointer to it without including this header. This header already
 * includes xrfdcScript.h, so an include in the other direction would be a
 * cycle whose outcome depended on which of the two a translation unit named
 * first. Nothing else about the type changes. */
typedef struct XRFdc {
    XRFdc_Config RFdc_Config;
    u32 UpdateMixerScale;
} XRFdc;

typedef struct {
    u32 IsEnabled;
    u32 TileState;
    u32 BlockStatusMask;
    u32 PowerUpState;
    u32 PLLState;
} XRFdc_TileStatus;

typedef struct {
    XRFdc_TileStatus ADCTileStatus[4];
    XRFdc_TileStatus DACTileStatus[4];
} XRFdc_IPStatus;

typedef struct {
    double SamplingFreq;
    u32 AnalogDataPathStatus;
    u32 DigitalDataPathStatus;
    u32 DataPathClocksStatus;
    u32 IsFIFOFlagsEnabled;
    u32 IsFIFOFlagsAsserted;
} XRFdc_BlockStatus;

typedef struct {
    u32 Enabled;
    double RefClkFreq;
    double SampleRate;
    u32 RefClkDivider;
    u32 FeedbackDivider;
    u32 OutputDivider;
    u32 FractionalMode;
    u64 FractionalData;
    u32 FractWidth;
} XRFdc_PLL_Settings;

//! Per-tile clock settings carried inside a distribution's info block. The
//! production code reads none of these members today, so they exist only to
//! give the enclosing structures the size and layout the real header gives
//! them.
typedef struct {
    u8 SourceType;
    u8 SourceTile;
    u32 PLLEnable;
    double RefClkFreq;
    double SampleRate;
    u8 DivisionFactor;
    u8 DistributedClock;
    u8 Delay;
} XRFdc_Tile_Clock_Settings;

//! What the driver worked out about one distribution while it was reading
//! the registers. The production code reads nothing from here either.
typedef struct {
    u8 MaxDelay;
    u8 MinDelay;
    u8 IsDelayBalanced;
    u8 Source;
    u8 UpperBound;
    u8 LowerBound;
    XRFdc_Tile_Clock_Settings ClkSettings[2][4];
} XRFdc_Distribution_Info;

//! One distribution: which tile sources it and which two tiles bound it.
//! The four members the production decode reads are SourceType,
//! SourceTileId, EdgeTileIds and EdgeTypes. The rest are present so this
//! structure and the array of eight below have the real footprint, which is
//! what makes the constructor's scoped block a fair test of the real cost.
typedef struct {
    u32 SourceType;
    u32 SourceTileId;
    u32 EdgeTileIds[2];
    u32 EdgeTypes[2];
    double DistRefClkFreq;
    u32 DistributedClock;
    double SampleRates[2][4];
    u32 ShutdownMode;
    XRFdc_Distribution_Info Info;
} XRFdc_Distribution_Settings;

//! Every distribution the part can carry. The eight is a literal here
//! because it is a literal in the real header too: the driver's own loop
//! bound XRFDC_MAX_DISTRS is defined privately inside xrfdc_clock.c and is
//! not exported, so this shim cannot borrow it and has to state it.
typedef struct {
    XRFdc_Distribution_Settings Distributions[8];
} XRFdc_Distribution_System_Settings;

typedef struct {
    u32 EnablePhase;
    u32 EnableGain;
    double GainCorrectionFactor;
    double PhaseCorrectionFactor;
    s32 OffsetCorrectionFactor;
    u32 EventSource;
} XRFdc_QMC_Settings;

typedef struct {
    double Freq;
    double PhaseOffset;
    u32 EventSource;
    u32 CoarseMixFreq;
    u32 MixerMode;
    u32 FineMixerScale;
    u32 MixerType;
} XRFdc_Mixer_Settings;

typedef struct {
    u32 CoarseDelay;
    u32 EventSource;
} XRFdc_CoarseDelay_Settings;

typedef struct {
    u32 UpdateThreshold;
    u32 ThresholdMode[2];
    u32 ThresholdAvgVal[2];
    u32 ThresholdUnderVal[2];
    u32 ThresholdOverVal[2];
} XRFdc_Threshold_Settings;

typedef struct {
    u32 Coeff0;
    u32 Coeff1;
    u32 Coeff2;
    u32 Coeff3;
    u32 Coeff4;
    u32 Coeff5;
    u32 Coeff6;
    u32 Coeff7;
} XRFdc_Calibration_Coefficients;

typedef struct {
    u32 CalFrozen;
    u32 DisableFreezePin;
    u32 FreezeCalibration;
} XRFdc_Cal_Freeze_Settings;

typedef struct {
    u32 DisableRTS;
    float Attenuation;
} XRFdc_DSA_Settings;

typedef struct {
    u8 Mode;
    u8 TimeConstant;
    u8 Flush;
    u8 EnableIntegrator;
    u16 Threshold;
    u16 ThreshOnTriggerCnt;
    u16 ThreshOffTriggerCnt;
    u8 HysteresisEnable;
} XRFdc_Signal_Detector_Settings;

typedef struct {
    u32 DisableIPControl;
    u32 PwrMode;
} XRFdc_Pwr_Mode_Settings;

typedef struct {
    u32 RefTile;
    u32 Tiles;
    u32 SysRef_Enable;
    int Target_Latency;
    int Latency[4];
    int Offset[4];
} XRFdc_MultiConverter_Sync_Config;

/* ------------------------------------------------------------------------ */
/* Driver entry points. Bodies are in xrfdcStub.cpp. Every one records its   */
/* call on gScript and consults the scripted-failure selector.               */
/* ------------------------------------------------------------------------ */

XRFdc_Config *XRFdc_LookupConfig(u16 DeviceId);
u32 XRFdc_CfgInitialize(XRFdc *InstancePtr, XRFdc_Config *ConfigPtr);
u32 XRFdc_RegisterMetal(XRFdc *InstancePtr, u16 DeviceId, struct metal_device **DevicePtr);

u32 XRFdc_StartUp(XRFdc *InstancePtr, u32 Type, int Tile_Id);
u32 XRFdc_Shutdown(XRFdc *InstancePtr, u32 Type, int Tile_Id);
u32 XRFdc_Reset(XRFdc *InstancePtr, u32 Type, int Tile_Id);
u32 XRFdc_CustomStartUp(XRFdc *InstancePtr, u32 Type, int Tile_Id, u32 StartState, u32 EndState);

u32 XRFdc_CheckTileEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id);
u32 XRFdc_CheckBlockEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_CheckDigitalPathEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);

u32 XRFdc_GetIPStatus(XRFdc *InstancePtr, XRFdc_IPStatus *IPStatusPtr);
u32 XRFdc_GetBlockStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_BlockStatus *BlockStatusPtr);

u32 XRFdc_GetMixerSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                           XRFdc_Mixer_Settings *Settings);
u32 XRFdc_SetMixerSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                           XRFdc_Mixer_Settings *Settings);
u32 XRFdc_GetQMCSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_QMC_Settings *Settings);
u32 XRFdc_SetQMCSettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                         XRFdc_QMC_Settings *Settings);
u32 XRFdc_GetCoarseDelaySettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                                 XRFdc_CoarseDelay_Settings *Settings);
u32 XRFdc_SetCoarseDelaySettings(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                                 XRFdc_CoarseDelay_Settings *Settings);
u32 XRFdc_UpdateEvent(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 Event);

u32 XRFdc_GetInterpolationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor);
u32 XRFdc_SetInterpolationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor);
u32 XRFdc_GetDecimationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor);
u32 XRFdc_SetDecimationFactor(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor);
u32 XRFdc_GetDecimationFactorObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Factor);
u32 XRFdc_SetDecimationFactorObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Factor);

u32 XRFdc_GetFabClkOutDiv(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u16 *Div);
u32 XRFdc_SetFabClkOutDiv(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u16 Div);

u32 XRFdc_GetFabWrVldWords(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words);
u32 XRFdc_SetFabWrVldWords(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words);
u32 XRFdc_GetFabWrVldWordsObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words);
u32 XRFdc_GetFabRdVldWords(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words);
u32 XRFdc_SetFabRdVldWords(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words);
u32 XRFdc_GetFabRdVldWordsObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Words);
u32 XRFdc_SetFabRdVldWordsObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Words);

u32 XRFdc_ThresholdStickyClear(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 ThresholdToUpdate);
u32 XRFdc_SetThresholdClrMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 ThresholdToUpdate,
                              u32 ClrMode);
u32 XRFdc_GetThresholdSettings(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                               XRFdc_Threshold_Settings *Settings);
u32 XRFdc_SetThresholdSettings(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                               XRFdc_Threshold_Settings *Settings);

u32 XRFdc_GetDecoderMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *DecoderMode);
u32 XRFdc_SetDecoderMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 DecoderMode);
u32 XRFdc_ResetNCOPhase(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);

u32 XRFdc_SetupFIFO(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable);
u32 XRFdc_SetupFIFOObs(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable);
u32 XRFdc_SetupFIFOBoth(XRFdc *InstancePtr, u32 Type, int Tile_Id, u8 Enable);
u32 XRFdc_GetFIFOStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 *Enable);
u32 XRFdc_GetFIFOStatusObs(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 *Enable);
u32 XRFdc_IsFifoEnabled(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);

u32 XRFdc_GetOutputCurr(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *OutputCurr);
u32 XRFdc_GetNyquistZone(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *NyquistZone);
u32 XRFdc_SetNyquistZone(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 NyquistZone);
u32 XRFdc_GetInvSincFIR(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u16 *Mode);
u32 XRFdc_SetInvSincFIR(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u16 Mode);

u32 XRFdc_GetCalibrationMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u8 *CalibrationMode);
u32 XRFdc_SetCalibrationMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u8 CalibrationMode);
u32 XRFdc_DisableCoefficientsOverride(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType);
u32 XRFdc_GetCalCoefficients(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType,
                             XRFdc_Calibration_Coefficients *Coeffs);
u32 XRFdc_SetCalCoefficients(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 CalType,
                             XRFdc_Calibration_Coefficients *Coeffs);
u32 XRFdc_GetCalFreeze(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                       XRFdc_Cal_Freeze_Settings *Settings);
u32 XRFdc_SetCalFreeze(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                       XRFdc_Cal_Freeze_Settings *Settings);

u32 XRFdc_GetDither(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode);
u32 XRFdc_SetDither(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode);
u32 XRFdc_GetDACDataScaler(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Enable);
u32 XRFdc_SetDACDataScaler(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Enable);

u32 XRFdc_GetClockSource(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *ClockSource);
u32 XRFdc_GetPLLConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, XRFdc_PLL_Settings *Settings);
u32 XRFdc_GetPLLLockStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *LockStatus);
u32 XRFdc_DynamicPLLConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u8 Source, double RefClkFreq,
                           double SampleRate);
u32 XRFdc_GetClkDistribution(XRFdc *InstancePtr,
                             XRFdc_Distribution_System_Settings *DistributionArrayPtr);

u32 XRFdc_GetCoupling(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *Mode);
u32 XRFdc_GetDSA(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, XRFdc_DSA_Settings *Settings);
u32 XRFdc_SetDSA(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, XRFdc_DSA_Settings *Settings);
u32 XRFdc_SetDACVOP(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 uACurrent);
u32 XRFdc_GetDACCompMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Enable);
u32 XRFdc_SetDACCompMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Enable);
u32 XRFdc_GetDataPathMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode);
u32 XRFdc_SetDataPathMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode);
u32 XRFdc_GetIMRPassMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 *Mode);
u32 XRFdc_SetIMRPassMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id, u32 Mode);

u32 XRFdc_GetSignalDetector(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                            XRFdc_Signal_Detector_Settings *Settings);
u32 XRFdc_SetSignalDetector(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id,
                            XRFdc_Signal_Detector_Settings *Settings);
u32 XRFdc_ResetInternalFIFOWidth(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_ResetInternalFIFOWidthObs(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_GetPwrMode(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                     XRFdc_Pwr_Mode_Settings *Settings);
u32 XRFdc_SetPwrMode(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                     XRFdc_Pwr_Mode_Settings *Settings);

u32 XRFdc_Get_TileBaseAddr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id);
u32 XRFdc_Get_BlockBaseAddr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_Get_IPBaseAddr(XRFdc *InstancePtr);

u32 XRFdc_GetNoOfADCBlocks(XRFdc *InstancePtr, u32 Tile_Id);
u32 XRFdc_GetNoOfDACBlock(XRFdc *InstancePtr, u32 Tile_Id);
u32 XRFdc_IsADCBlockEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_IsDACBlockEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_IsADCDigitalPathEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_IsDACDigitalPathEnabled(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_IsHighSpeedADC(XRFdc *InstancePtr, u32 Tile_Id);
u32 XRFdc_GetDataType(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_GetDataWidth(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_GetInverseSincFilter(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_GetMixedMode(XRFdc *InstancePtr, u32 Tile_Id, u32 Block_Id);
u32 XRFdc_GetMasterTile(XRFdc *InstancePtr, u32 Type);
u32 XRFdc_GetSysRefSource(XRFdc *InstancePtr, u32 Type);
u32 XRFdc_GetTileLayout(XRFdc *InstancePtr);
u32 XRFdc_GetMultibandConfig(XRFdc *InstancePtr, u32 Type, u32 Tile_Id);
u32 XRFdc_GetMaxSampleRate(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, double *MaxSampleRate);
u32 XRFdc_GetMinSampleRate(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, double *MinSampleRate);
double XRFdc_GetDriverVersion(void);
double XRFdc_GetFabClkFreq(XRFdc *InstancePtr, u32 Type, u32 Tile_Id);
int XRFdc_GetConnectedIData(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);
int XRFdc_GetConnectedQData(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id);

u32 XRFdc_IntrEnable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask);
u32 XRFdc_IntrDisable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask);
u32 XRFdc_IntrClr(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 IntrMask);
u32 XRFdc_GetIntrStatus(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id, u32 *IntrStsPtr);
u32 XRFdc_GetEnabledInterrupts(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 Block_Id,
                               u32 *IntrMask);

void XRFdc_MultiConverter_Init(XRFdc_MultiConverter_Sync_Config *ConfigPtr, int *PLL_CodesPtr,
                               int *T1_CodesPtr, u32 RefTile);
u32 XRFdc_MultiConverter_Sync(XRFdc *InstancePtr, u32 Type,
                              XRFdc_MultiConverter_Sync_Config *ConfigPtr);
u32 XRFdc_GetMTSEnable(XRFdc *InstancePtr, u32 Type, u32 Tile_Id, u32 *EnablePtr);
void XRFdc_MTS_Sysref_Config(XRFdc *InstancePtr, XRFdc_MultiConverter_Sync_Config *DACSyncConfigPtr,
                             XRFdc_MultiConverter_Sync_Config *ADCSyncConfigPtr, u32 SysRefEnable);

u32 XRFdc_ReadReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr);
void XRFdc_WriteReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 RegData);
u32 XRFdc_RDReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 Mask);
void XRFdc_ClrSetReg(XRFdc *InstancePtr, u32 BaseAddr, u32 RegAddr, u32 Mask, u32 Data);

#endif  /* __XRFDC_H__ */
