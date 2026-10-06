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

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rogue/interfaces/memory/Slave.h"

#ifdef __BAREMETAL__
#include "xparameters.h"
#endif
#include "xrfdc.h"

#ifndef NO_PYTHON
    #include <boost/python.hpp>
#endif

//! Restart operations (tile register 0x818 bits 11:8). Reset, StartUp, CustomStartUp
//! and the SetClkDistribution commit are recorded; Shutdown is a dispatch and
//! naming value only.
static const uint32_t RestartOpReset         = 1;
static const uint32_t RestartOpStartUp       = 2;
static const uint32_t RestartOpCustomStartUp = 3;
static const uint32_t RestartOpShutdown      = 4;
static const uint32_t RestartOpClkDist       = 5;

//! Restart result (tile register 0x818 bits 3:0)
static const uint32_t ResetResultOk     = 1;
static const uint32_t ResetResultFailed = 2;
static const uint32_t ResetResultBusy   = 3;

//! Restart flags (tile register 0x818 bits 7:4)
static const uint32_t ResetFlagParked               = 0x10;
static const uint32_t ResetFlagStagingNotRefreshed  = 0x20;

//! Outcome of waiting on a tile whose Restart register is set before a restart
enum RestartWait { RestartClear = 0, RestartParked = 1, RestartBusy = 2 };

//! Restart still set after the whole wait: CurrentState moved means the state
//! machine is busy, CurrentState frozen means the tile is parked
inline RestartWait ClassifyRestartWait(bool clearedInTime, bool stateMoved) {
    if (clearedInTime) {
        return RestartClear;
    }
    return stateMoved ? RestartBusy : RestartParked;
}

//! The busy text of every restart precheck refusal (RestartProceed, RestartTiles and the
//! SetClkDistribution commit).
inline std::string BusyText(uint32_t firstState, uint32_t lastState) {
    return "state machine busy: Restart stayed set for " + std::to_string(uint32_t(XRFDC_RESTART_CLR_DLY_CNT)) +
           " ms while CurrentState moved " + std::to_string(firstState) + " to " + std::to_string(lastState) +
           "; no restart issued";
}

//! Tile register 0x818: sequence 31:16, op 11:8, flags 7:4, result 3:0
inline uint32_t PackResetRecord(uint16_t seq, uint32_t op, uint32_t result, uint32_t flags) {
    return (uint32_t(seq) << 16) | ((op & 0xF) << 8) | (flags & 0xF0) | (result & 0xF);
}

//! One failed tile of a restart command
struct TileFailure {
    uint32_t tile;
    const char* call;
    std::string detail;
};

//! Longest transaction error text sent to a host. Rogue's memory TcpClient
//! keeps a 1000 byte buffer for the reply and drops a reply whose error text
//! is longer than 999 bytes, so the host would see a timeout instead of the
//! failure.
static const std::size_t ErrorTextMaxChars = 999;

//! Join lines with one newline between them while the result stays within
//! maxChars. A line that does not fit is dropped whole together with every
//! later line (a line is never cut), then the line "(<n> more lines not shown)"
//! is appended when it fits too. No percent sign is ever added: the host
//! treats the error text as a printf format.
inline std::string JoinWholeLines(const std::vector<std::string>& lines, std::size_t maxChars) {
    std::string out;
    std::size_t kept = 0;
    for (; kept < lines.size(); kept++) {
        std::size_t need = lines[kept].size() + ((kept > 0) ? 1 : 0);
        if (out.size() + need > maxChars) {
            break;
        }
        if (kept > 0) {
            out += "\n";
        }
        out += lines[kept];
    }
    if (kept < lines.size()) {
        std::string note = "(" + std::to_string(lines.size() - kept) + " more lines not shown)";
        std::size_t need = note.size() + ((kept > 0) ? 1 : 0);
        if (out.size() + need <= maxChars) {
            if (kept > 0) {
                out += "\n";
            }
            out += note;
        }
    }
    return out;
}

//! How a register write is treated by the write gate in doTransaction.
//! WriteNone: read-only, software-only or undefined address, the handler answers.
//! WriteStaging: only fills a software staging word, always accepted.
//! WriteRestart: a restart command, it runs its own restart precheck instead.
//! WriteGated: reaches a driver setter or a tile or block register, so every
//! tile in its scope must read Restart clear and CurrentState 15 first.
enum WriteKind { WriteNone = 0, WriteStaging = 1, WriteGated = 2, WriteRestart = 3 };

//! Which tiles a gated write must find ready
//! ScopeTile: the addressed tile. ScopeTypeAll: every enabled tile of the type.
//! ScopeMtsGroup: the enabled tiles of the type in Tiles | (1 << RefTile).
//! ScopeMtsSysref: DAC tile 0 plus the enabled tiles of both Tiles masks.
enum WriteScope { ScopeTile = 0, ScopeTypeAll = 1, ScopeMtsGroup = 2, ScopeMtsSysref = 3 };

//! Result of ClassifyWrite. type is 0 for ADC, 1 for DAC, -1 for the type of the
//! address window. name is the handler name used in the refusal text.
struct WriteGateInfo {
    uint8_t kind;
    uint8_t scope;
    int8_t type;
    const char* name;
};

inline WriteGateInfo WriteInfo(WriteKind kind, WriteScope scope, int type, const char* name) {
    WriteGateInfo gi;
    gi.kind = uint8_t(kind);
    gi.scope = uint8_t(scope);
    gi.type = int8_t(type);
    gi.name = name;
    return gi;
}

//! True when a mixer or QMC commit must issue XRFdc_UpdateEvent. XRFdc_UpdateEvent
//! applies only the Tile and Slice event sources and rejects the others. With
//! the Immediate source the Set call already takes effect, and with SYSREF, PL
//! or MARKER the new values apply at the next event the application issues, so
//! the commit programs the settings and leaves the staged event source alone.
inline bool CommitNeedsUpdateEvent(uint32_t eventSource) {
    return (eventSource == XRFDC_EVNT_SRC_TILE) || (eventSource == XRFDC_EVNT_SRC_SLICE);
}

//! Config ROM mixer registers of a tile at tile-only offsets 0x200 + 0x10*block:
//! +0x0 MixerType, +0x4 MixerInputDataType, +0x8 and +0xC the low and high words
//! of NCOFreq. Split a tile-only address in 0x200 to 0x23C into block and field.
inline void RomMixerIndex(uint32_t tileAddr, uint32_t& block, uint32_t& field) {
    block = ((tileAddr - 0x200) >> 4) & 0x3;
    field = ((tileAddr - 0x200) >> 2) & 0x3;
}

//! Field index of a block address inside the Threshold (0x090 to 0x0AC), CalCoefficients
//! (0x0D0, 0x0F0, 0x110 and 0x130, eight words each) and SignalDetector (0x180 to 0x19C)
//! ranges: the offset from the range base in words. The switches of ThresholdSettings,
//! CalCoefficients and SignalDetector count from 0 in the order of the PyRogue offsets.
inline uint32_t ThresholdIndex(uint32_t blockAddr) {
    return ((blockAddr - 0x090) >> 2) & 0x7;
}

inline uint32_t CalCoefficientsIndex(uint32_t blockAddr, uint32_t base) {
    return ((blockAddr - base) >> 2) & 0x7;
}

inline uint32_t SignalDetectorIndex(uint32_t blockAddr) {
    return ((blockAddr - 0x180) >> 2) & 0x7;
}

//! Clock distribution tile index of (type, tile), as XRFdc_TypeTile2DistTile:
//! ADC tile 0 is index 7 and counts down, the DAC edge tile is index 3 (4 ADC and
//! 4 DAC tiles) or 4 (3 ADC and 2 DAC tiles) and the DAC tiles count down from it
inline uint32_t DistIndex(uint32_t layout, uint32_t type, uint32_t tile) {
    uint32_t dacEdge = (layout == XRFDC_3ADC_2DAC_TILES) ? XRFDC_CLK_DST_TILE_227 : XRFDC_CLK_DST_TILE_228;
    return (type == XRFDC_ADC_TILE) ? (XRFDC_CLK_DST_TILE_224 - tile) : (dacEdge - tile);
}

//! Inverse of DistIndex, as XRFdc_DistTile2TypeTile
inline void DistTypeTile(uint32_t layout, uint32_t index, uint32_t& type, uint32_t& tile) {
    uint32_t dacEdge = (layout == XRFDC_3ADC_2DAC_TILES) ? XRFDC_CLK_DST_TILE_227 : XRFDC_CLK_DST_TILE_228;
    if (index > dacEdge) {
        type = XRFDC_ADC_TILE;
        tile = XRFDC_CLK_DST_TILE_224 - index;
    } else {
        type = XRFDC_DAC_TILE;
        tile = dacEdge - index;
    }
}

//! One 32-bit half of a double, low word first (the order DoubleToUint32 uses)
inline uint32_t DoubleWord(double value, bool upper) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return upper ? uint32_t(bits >> 32) : uint32_t(bits & 0xFFFFFFFFULL);
}

//! Valid distributions of a Get result: the entries before the first one that
//! the driver marked with XRFDC_CLK_DST_INVALID, at most XRFDC_DIST_MAX
inline uint32_t ClkDistCount(const XRFdc_Distribution_System_Settings& s) {
    uint32_t n = 0;
    while ((n < XRFDC_DIST_MAX) && (s.Distributions[n].SourceTileId != XRFDC_CLK_DST_INVALID)) {
        n++;
    }
    return n;
}

inline bool ClkDistTileValid(uint32_t layout, uint32_t type, uint32_t tile);

//! Clock distribution read window, 0x15000 to 0x157FC, read-only. Every word is
//! ClkDistWord of one XRFdc_GetClkDistribution result. Offsets from 0x15000:
//!   0x000 Status: 0 unsupported (IP below Gen3), 1 ok, 2 Get failed
//!   0x004 Count: number of valid distributions
//!   0x008 to 0x0FC reserved, read 0
//!   Distribution d (0 to 7) at 0x100 + 0x80 * d, all zero from Count on:
//!     +0x00 Valid, +0x04 SourceType (0 ADC, 1 DAC), +0x08 SourceTileId
//!     +0x0C EdgeTypes[0], +0x10 EdgeTileIds[0], +0x14 EdgeTypes[1], +0x18 EdgeTileIds[1]
//!     +0x1C DistributedClock (0 none, 1 RX, 2 OUTDIV)
//!     +0x20 and +0x24 DistRefClkFreq in MHz (double, low word first)
//!     +0x28 Info.Source 7:0, Info.UpperBound 15:8, Info.LowerBound 23:16
//!     +0x2C Info.MaxDelay 7:0, Info.MinDelay 15:8, Info.IsDelayBalanced bit 16
//!     +0x30 to +0x3C reserved, read 0
//!     +0x40 + 0x8 * t SampleRates[0][t] (ADC tile t) and +0x60 + 0x8 * t SampleRates[1][t]
//!     (DAC tile t), in MSPS as the driver reports them, two words each
//!   Tile view of (type, tile) at 0x500 + 0x40 * (4 * type + tile):
//!     +0x00 index of the valid entry whose LowerBound to UpperBound holds the tile
//!     (0xFF when none, then every other word of the view is 0; the same holds for a tile
//!     the layout does not have, which no entry can hold: ADC tile 3 and DAC tiles 2 and 3
//!     of a 3 ADC and 2 DAC device)
//!     +0x04 SourceType, +0x08 SourceTile, +0x0C PLLEnable, +0x10 DivisionFactor,
//!     +0x14 DistributedClock, +0x18 Delay, +0x1C reserved, all from that entry's
//!     Info.ClkSettings[type][tile]
//!     +0x20 and +0x24 RefClkFreq in MHz, +0x28 and +0x2C SampleRate in MSPS
//!     +0x30 to +0x3C reserved, read 0
//!   0x700 to 0x7FC reserved, read 0
//! A status other than 1 reads the status at 0x000 and 0 everywhere else.
inline uint32_t ClkDistWord(const XRFdc_Distribution_System_Settings& s, uint32_t layout, uint32_t status, uint32_t off) {
    if (off == 0x000) {
        return status;
    }
    if ((status != 1) || ((off & 0x3) != 0) || (off > 0x7FC)) {
        return 0;
    }
    uint32_t count = ClkDistCount(s);
    if (off == 0x004) {
        return count;
    }
    if (off < 0x100) {
        return 0;
    }

    const XRFdc_Distribution_Settings* e = nullptr;
    uint32_t w = 0;
    uint32_t viewType = 0;
    uint32_t viewTile = 0;
    uint32_t viewIndex = 0xFF;

    if (off < 0x500) {
        uint32_t d = (off - 0x100) >> 7;
        w = (off - 0x100) & 0x7F;
        if (d >= count) {
            return 0;
        }
        e = &s.Distributions[d];
        const XRFdc_Distribution_Info& i = e->Info;
        switch (w) {
            case 0x00: return 1;
            case 0x04: return e->SourceType;
            case 0x08: return e->SourceTileId;
            case 0x0C: return e->EdgeTypes[0];
            case 0x10: return e->EdgeTileIds[0];
            case 0x14: return e->EdgeTypes[1];
            case 0x18: return e->EdgeTileIds[1];
            case 0x1C: return e->DistributedClock;
            case 0x20: return DoubleWord(e->DistRefClkFreq, false);
            case 0x24: return DoubleWord(e->DistRefClkFreq, true);
            case 0x28: return uint32_t(i.Source) | (uint32_t(i.UpperBound) << 8) | (uint32_t(i.LowerBound) << 16);
            case 0x2C: return uint32_t(i.MaxDelay) | (uint32_t(i.MinDelay) << 8) | ((i.IsDelayBalanced ? 1U : 0U) << 16);
            default: break;
        }
        if ((w >= 0x40) && (w <= 0x5C)) {
            return DoubleWord(e->SampleRates[0][(w - 0x40) >> 3], ((w >> 2) & 1) != 0);
        }
        if ((w >= 0x60) && (w <= 0x7C)) {
            return DoubleWord(e->SampleRates[1][(w - 0x60) >> 3], ((w >> 2) & 1) != 0);
        }
        return 0;
    }

    if (off < 0x700) {
        uint32_t v = (off - 0x500) >> 6;
        w = (off - 0x500) & 0x3F;
        viewType = v >> 2;
        viewTile = v & 0x3;
        if (!ClkDistTileValid(layout, viewType, viewTile)) {
            return (w == 0x00) ? 0xFFU : 0U;
        }
        uint32_t idx = DistIndex(layout, viewType, viewTile);
        for (uint32_t d = 0; d < count; d++) {
            const XRFdc_Distribution_Info& i = s.Distributions[d].Info;
            if ((idx >= i.LowerBound) && (idx <= i.UpperBound)) {
                viewIndex = d;
                e = &s.Distributions[d];
                break;
            }
        }
        if (w == 0x00) {
            return viewIndex;
        }
        if (e == nullptr) {
            return 0;
        }
        const XRFdc_Tile_Clock_Settings& c = e->Info.ClkSettings[viewType][viewTile];
        switch (w) {
            case 0x04: return c.SourceType;
            case 0x08: return c.SourceTile;
            case 0x0C: return c.PLLEnable;
            case 0x10: return c.DivisionFactor;
            case 0x14: return c.DistributedClock;
            case 0x18: return c.Delay;
            case 0x20: return DoubleWord(c.RefClkFreq, false);
            case 0x24: return DoubleWord(c.RefClkFreq, true);
            case 0x28: return DoubleWord(c.SampleRate, false);
            case 0x2C: return DoubleWord(c.SampleRate, true);
            default: break;
        }
    }
    return 0;
}

//! SetClkDistribution block, 0x15800 to 0x15928 (the read window ends at 0x157FC).
//! Staging words 0x15800 to 0x1586C mirror XRFdc_Distribution_Settings; they are
//! plain software words, never gated, and no hardware is touched by a write.
//! Offsets from 0x15800:
//!   0x00 SourceType, 0x04 SourceTileId, 0x08 EdgeTypes[0], 0x0C EdgeTileIds[0],
//!   0x10 EdgeTypes[1], 0x14 EdgeTileIds[1], 0x18 DistributedClock,
//!   0x1C ShutdownMode (a nonzero value is refused at the commit)
//!   0x20 and 0x24 DistRefClkFreq in MHz (double, low word first), 0x28 and 0x2C reserved
//!   0x30 + 0x8 * t ADC tile t SampleRates in MSPS, 0x50 + 0x8 * t DAC tile t SampleRates
//!   0x100 write the index of a Get entry: load the staging from it (read 1)
//!   0x104 write: commit the staging (read 1)
//!   0x108 preview status (ClkDistCheck of the staging), 0x10C ADC affected, 0x110 DAC
//!   affected, 0x114 ADC span, 0x118 DAC span (tile masks of the staging)
//!   0x11C to 0x128 the same four masks of the last commit (all 0 after a refused commit)

//! Outcome of checking the staged distribution before the driver sees it
enum ClkDistCheck {
    ClkDistOk = 0,
    ClkDistUnsupported = 1,   // IP below Gen3, or the current distribution could not be read
    ClkDistBadField = 2,      // a type, a tile id or DistributedClock outside its range
    ClkDistShutdownMode = 3,  // a nonzero staged ShutdownMode
    ClkDistBadSpan = 4,       // the staged span is not a legal distribution
    ClkDistTileDisabled = 5,  // a span tile is not enabled
    ClkDistBadRate = 6,       // a span tile rate or the reference frequency is out of range
    ClkDistOrphan = 7         // an enabled tile would end up outside every distribution
};

//! Bits lower to upper of a mask over distribution tile indices (0 to 7)
inline uint32_t SpanMask(uint32_t lower, uint32_t upper) {
    uint32_t mask = 0;
    for (uint32_t i = lower; (i <= upper) && (i < 8); i++) {
        mask |= 1U << i;
    }
    return mask;
}

//! True when (type, tile) is a tile of the layout: 4 tiles of each type, or 3 ADC
//! and 2 DAC tiles
inline bool ClkDistTileValid(uint32_t layout, uint32_t type, uint32_t tile) {
    if (type > XRFDC_DAC_TILE) {
        return false;
    }
    uint32_t n = 4;
    if (layout == XRFDC_3ADC_2DAC_TILES) {
        n = (type == XRFDC_ADC_TILE) ? 3 : 2;
    }
    return tile < n;
}

//! The span rules of XRFdc_CheckClkDistValid that need no hardware: lower, upper and
//! source are the distribution tile indices of the staged settings (the two edges
//! are put in order the way the driver does). Types are 0 or 1, tile ids are tiles of
//! the layout, DistributedClock is at most OutDiv, the source lies between the edges,
//! a single tile span distributes nothing and a multi tile span distributes RX or
//! OutDiv, an ADC source may not reach a DAC tile, and on 4 ADC 4 DAC an edge tile
//! (tile id 0 or 3) source may not distribute OutDiv or a full rate clock without
//! its PLL. ShutdownMode, the enabled tiles and the rates are checked elsewhere.
inline ClkDistCheck ClkDistGeometry(uint32_t layout, const XRFdc_Distribution_Settings& s,
                                    uint32_t& lower, uint32_t& upper, uint32_t& source) {
    lower = 0;
    upper = 0;
    source = 0;
    if (!ClkDistTileValid(layout, s.SourceType, s.SourceTileId) ||
        !ClkDistTileValid(layout, s.EdgeTypes[0], s.EdgeTileIds[0]) ||
        !ClkDistTileValid(layout, s.EdgeTypes[1], s.EdgeTileIds[1]) ||
        (s.DistributedClock > XRFDC_DIST_OUT_OUTDIV)) {
        return ClkDistBadField;
    }

    source = DistIndex(layout, s.SourceType, s.SourceTileId);
    upper = DistIndex(layout, s.EdgeTypes[0], s.EdgeTileIds[0]);
    lower = DistIndex(layout, s.EdgeTypes[1], s.EdgeTileIds[1]);
    if (upper < lower) {
        uint32_t t = upper;
        upper = lower;
        lower = t;
    }

    if ((source < lower) || (source > upper)) {
        return ClkDistBadSpan;
    }
    if (upper == lower) {
        if (s.DistributedClock != XRFDC_DIST_OUT_NONE) {
            return ClkDistBadSpan;
        }
    } else if (s.DistributedClock == XRFDC_DIST_OUT_NONE) {
        return ClkDistBadSpan;
    }

    if (layout == XRFDC_4ADC_4DAC_TILES) {
        // The driver runs the PLL when the rate is above the reference. A reference or rate that is
        // not a number counts as a PLL here, so it is refused by the rate checks that name it
        // instead of by this rule.
        bool pll = !(s.SampleRates[s.SourceType][s.SourceTileId] <= s.DistRefClkFreq);
        if (((s.SourceTileId == 0) || (s.SourceTileId == 3)) &&
            ((s.DistributedClock == XRFDC_DIST_OUT_OUTDIV) || ((s.DistributedClock == XRFDC_DIST_OUT_RX) && !pll))) {
            return ClkDistBadSpan;
        }
    }

    uint32_t adcEdge = (layout == XRFDC_3ADC_2DAC_TILES) ? XRFDC_CLK_DST_TILE_226 : XRFDC_CLK_DST_TILE_227;
    if ((s.SourceType == XRFDC_ADC_TILE) && (lower < adcEdge)) {
        return ClkDistBadSpan;
    }
    return ClkDistOk;
}

//! Tile masks of a mask over distribution indices: bit t of adcMask or dacMask for
//! each index that is a tile of the layout
inline void DistMaskToTiles(uint32_t layout, uint32_t mask, uint32_t& adcMask, uint32_t& dacMask) {
    adcMask = 0;
    dacMask = 0;
    for (uint32_t i = 0; i < 8; i++) {
        if (((mask >> i) & 1U) == 0) {
            continue;
        }
        uint32_t type = 0;
        uint32_t tile = 0;
        DistTypeTile(layout, i, type, tile);
        if (!ClkDistTileValid(layout, type, tile)) {
            continue;
        }
        if (type == XRFDC_ADC_TILE) {
            adcMask |= 1U << tile;
        } else {
            dacMask |= 1U << tile;
        }
    }
}

//! The tiles of two tile masks as (type, tile), ADC 0 to 3 first and then DAC 0 to 3
inline std::vector<std::pair<uint32_t, uint32_t> > ClkDistOrderedTiles(uint32_t adcMask, uint32_t dacMask) {
    std::vector<std::pair<uint32_t, uint32_t> > out;
    for (uint32_t t = 0; t < 4; t++) {
        if ((adcMask >> t) & 1U) {
            out.push_back(std::make_pair(uint32_t(XRFDC_ADC_TILE), t));
        }
    }
    for (uint32_t t = 0; t < 4; t++) {
        if ((dacMask >> t) & 1U) {
            out.push_back(std::make_pair(uint32_t(XRFDC_DAC_TILE), t));
        }
    }
    return out;
}

//! The affected set of a Set, as a mask over distribution indices: the new span plus
//! every current distribution (from Get) that holds the new source or a tile of the
//! new span. orphanMask gets the members of those distributions that the Set cuts
//! off from their clock: when the old source is inside the new span every member
//! outside the span, otherwise the members outside the span that are not joined to the
//! old source by a run of tiles outside the span (the source itself never is).
inline uint32_t ClkDistAffected(const XRFdc_Distribution_System_Settings& cur, uint32_t spanMask,
                                uint32_t source, uint32_t& orphanMask) {
    orphanMask = 0;
    uint32_t touch = spanMask | ((source < 8) ? (1U << source) : 0U);
    uint32_t affected = spanMask;
    uint32_t count = ClkDistCount(cur);

    for (uint32_t d = 0; d < count; d++) {
        const XRFdc_Distribution_Info& info = cur.Distributions[d].Info;
        if ((info.LowerBound > info.UpperBound) || (info.UpperBound > 7)) {
            continue;
        }
        uint32_t members = SpanMask(info.LowerBound, info.UpperBound);
        if ((members & touch) == 0) {
            continue;
        }
        affected |= members;

        uint32_t outside = members & ~spanMask;
        uint32_t src = info.Source;
        if ((src < 8) && (((spanMask >> src) & 1U) != 0)) {
            orphanMask |= outside;
            continue;
        }

        uint32_t joined = 0;
        if ((src < 8) && (((outside >> src) & 1U) != 0)) {
            for (int k = int(src); (k >= int(info.LowerBound)) && (((outside >> k) & 1U) != 0); k--) {
                joined |= 1U << k;
            }
            for (uint32_t k = src + 1; (k <= info.UpperBound) && (((outside >> k) & 1U) != 0); k++) {
                joined |= 1U << k;
            }
        }
        orphanMask |= outside & ~joined;
    }
    return affected;
}

//! Converter types whose MTS outputs a restart of the given tiles makes stale, as a
//! mask (bit 0 ADC, bit 1 DAC). The restarted tiles' own types, plus every type with a
//! member in a distribution whose source tile (Info.Source) is among the restarted
//! tiles: a restarted source drops the clock of every member. Both types when the
//! topology is not valid (below Gen3, a failed Get, or a tile that was not ready).
inline uint32_t MtsTypesToClear(uint32_t layout, const XRFdc_Distribution_System_Settings& topo, bool topoValid,
                                uint32_t adcRestarted, uint32_t dacRestarted) {
    if (!topoValid) {
        return 3;
    }

    uint32_t types = ((adcRestarted != 0) ? 1U : 0U) | ((dacRestarted != 0) ? 2U : 0U);

    uint32_t restarted = 0;
    for (uint32_t t = 0; t < 4; t++) {
        if ((((adcRestarted >> t) & 1U) != 0) && ClkDistTileValid(layout, XRFDC_ADC_TILE, t)) {
            restarted |= 1U << DistIndex(layout, XRFDC_ADC_TILE, t);
        }
        if ((((dacRestarted >> t) & 1U) != 0) && ClkDistTileValid(layout, XRFDC_DAC_TILE, t)) {
            restarted |= 1U << DistIndex(layout, XRFDC_DAC_TILE, t);
        }
    }

    uint32_t count = ClkDistCount(topo);
    for (uint32_t d = 0; d < count; d++) {
        const XRFdc_Distribution_Info& info = topo.Distributions[d].Info;
        if ((info.LowerBound > info.UpperBound) || (info.UpperBound > 7) || (info.Source > 7)) {
            continue;
        }
        if (((restarted >> info.Source) & 1U) == 0) {
            continue;
        }
        uint32_t adcMembers = 0;
        uint32_t dacMembers = 0;
        DistMaskToTiles(layout, SpanMask(info.LowerBound, info.UpperBound), adcMembers, dacMembers);
        types |= ((adcMembers != 0) ? 1U : 0U) | ((dacMembers != 0) ? 2U : 0U);
    }
    return types;
}

//! Order in which a restart of every tile of one converter type visits the tiles:
//! the tiles of that type that source a clock distribution (Info.Source), then the
//! others, each group in index order. A member restarted while its source is down
//! stops at state 6 with no clock until the driver times out, so the source goes
//! first. reverse gives the opposite order, the members before their source, for a
//! Shutdown. Index order when the topology is not valid.
inline void RestartOrder(uint32_t layout, const XRFdc_Distribution_System_Settings& topo, bool topoValid,
                         uint32_t type, bool reverse, uint32_t order[4]) {
    uint32_t sources = 0;
    uint32_t count = topoValid ? ClkDistCount(topo) : 0;
    for (uint32_t d = 0; d < count; d++) {
        uint32_t srcType = 0;
        uint32_t srcTile = 0;
        if (topo.Distributions[d].Info.Source > 7) {
            continue;
        }
        DistTypeTile(layout, topo.Distributions[d].Info.Source, srcType, srcTile);
        if ((srcType == type) && (srcTile < 4)) {
            sources |= 1U << srcTile;
        }
    }

    uint32_t n = 0;
    for (uint32_t pass = 0; pass < 2; pass++) {
        for (uint32_t t = 0; t < 4; t++) {
            if ((((sources >> t) & 1U) != 0) == (pass == 0)) {
                order[n++] = t;
            }
        }
    }
    if (reverse) {
        std::swap(order[0], order[3]);
        std::swap(order[1], order[2]);
    }
}

//! Tiles of an MTS sync group: the operator mask plus the reference tile, which the
//! driver reads even when the mask leaves it out. The reference aliases to 0 to 3.
inline uint32_t MtsGroupMask(uint32_t tiles, uint32_t refTile) {
    return tiles | (1U << (refTile & 0x3));
}

//! The failure lines of a Set in report order: the ADC diagnostic lines, the DAC
//! diagnostic lines, the ADC detail lines, the DAC detail lines, then the metal lines
inline std::vector<std::string> ClkDistComposeLines(const std::vector<std::string>& adcDiag,
                                                    const std::vector<std::string>& dacDiag,
                                                    const std::vector<std::string>& adcDetail,
                                                    const std::vector<std::string>& dacDetail,
                                                    const std::vector<std::string>& metal) {
    std::vector<std::string> lines;
    lines.insert(lines.end(), adcDiag.begin(), adcDiag.end());
    lines.insert(lines.end(), dacDiag.begin(), dacDiag.end());
    lines.insert(lines.end(), adcDetail.begin(), adcDetail.end());
    lines.insert(lines.end(), dacDetail.begin(), dacDetail.end());
    lines.insert(lines.end(), metal.begin(), metal.end());
    return lines;
}

//! True when a staged sample rate is a finite number inside the limits of its tile (MSPS)
inline bool ClkDistRateOk(double rate, double minRate, double maxRate) {
    return std::isfinite(rate) && (rate >= minRate) && (rate <= maxRate);
}

//! True when the staged reference frequency (MHz) is finite and positive and, when the
//! source rate is above it so the source runs its PLL, inside the PLL reference range
inline bool ClkDistRefOk(double ref, double sourceRate) {
    if (!std::isfinite(ref) || (ref <= 0.0)) {
        return false;
    }
    if (sourceRate > ref) {
        return (ref >= XRFDC_REFFREQ_MIN) && (ref <= XRFDC_REFFREQ_MAX);
    }
    return true;
}

//! Classify a write to a register address with the same address decode that
//! doTransaction uses. Pure function of the address; reads are never classified.
inline WriteGateInfo ClassifyWrite(uint32_t addr) {
    const WriteGateInfo none = WriteInfo(WriteNone, ScopeTile, -1, nullptr);

    // Global registers
    if (addr >= 0x10000) {
        if ((addr <= 0x1001C) && ((addr & 0x3) == 0)) {
            return WriteInfo(WriteRestart, ScopeTile, -1, "RestartAll");
        }
        switch (addr) {
            case 0x10020: return WriteInfo(WriteGated, ScopeTypeAll, 0, "SetupFIFOAllAdc");
            case 0x10024: return WriteInfo(WriteGated, ScopeTypeAll, 1, "SetupFIFOAllDac");
            case 0x10028: return WriteInfo(WriteGated, ScopeTypeAll, 0, "SetupFIFOObsAllAdc");
            case 0x1002C: return WriteInfo(WriteGated, ScopeTypeAll, 0, "SetupFIFOBothAllAdc");
            case 0x11008: return WriteInfo(WriteGated, ScopeMtsGroup, 0, "MtsSync");
            case 0x1100C: return WriteInfo(WriteGated, ScopeMtsGroup, 1, "MtsSync");
            case 0x11100: return WriteInfo(WriteGated, ScopeMtsSysref, -1, "MtsSysrefConfig");
            default: break;
        }
        if ((addr >= 0x11010) && (addr <= 0x1102C) && ((addr & 0x3) == 0)) {
            return WriteInfo(WriteStaging, ScopeTile, -1, "MtsConfig");
        }
        // 0x15000 to 0x157FC is the read-only clock distribution window: the handler
        // answers a write with a refusal and nothing reaches a tile or the driver
        if ((addr >= 0x15000) && (addr <= 0x157FC)) {
            return WriteInfo(WriteNone, ScopeTile, -1, "ClkDist");
        }
        // 0x15800 to 0x1586C are the SetClkDistribution staging words: software only, never
        // gated. 0x15904 is the commit, a restart command that runs its own restart
        // precheck. 0x15900 (driver read into the staging) and 0x15908 to 0x15928 (read-only
        // preview and last-commit words) are answered by the handler and fall to none.
        if ((addr >= 0x15800) && (addr <= 0x1586C)) {
            return WriteInfo(WriteStaging, ScopeTile, -1, "ClkDistStaging");
        }
        if (addr == 0x15904) {
            return WriteInfo(WriteRestart, ScopeTile, -1, "SetClkDistribution");
        }
        return none;
    }

    uint32_t tileAddr = addr & 0xFFF;
    uint32_t blockAddr = addr & 0x3FF;

    // Tile only registers
    if (((addr >> 12) & 0x1) == 0x0) {
        switch (tileAddr) {
            case 0x000: case 0x004: case 0x008: case 0x00C: case 0x114: case 0x800:
                return WriteInfo(WriteRestart, ScopeTile, -1, "Restart");
            case 0x014: return WriteInfo(WriteGated, ScopeTile, -1, "FabClkOutDiv");
            case 0x018: return WriteInfo(WriteGated, ScopeTile, -1, "SetupFIFO");
            case 0x01C: return WriteInfo(WriteGated, ScopeTile, -1, "SetupFIFOObs");
            case 0x020: return WriteInfo(WriteGated, ScopeTile, -1, "SetupFIFOBoth");
            case 0x804: return WriteInfo(WriteGated, ScopeTile, -1, "RestartState");
            default: break;
        }
        // DynamicPLLConfig words 0 to 4 are staging, word 5 is the restart commit
        if ((tileAddr >= 0x100) && (tileAddr <= 0x110)) {
            return WriteInfo(WriteStaging, ScopeTile, -1, "DynamicPLLConfig");
        }
        // 0x200 to 0x23C (config ROM mixer fields, read-only) and 0x828 (staging
        // refresh, reads the driver only) are software-only and fall through to none
        return none;
    }

    // Tile and block registers
    if ((blockAddr >= 0x020) && (blockAddr <= 0x03C)) {
        if (((blockAddr >> 2) & 0x7) == 7) {
            return WriteInfo(WriteGated, ScopeTile, -1, "MixerSettings(7)");
        }
        return WriteInfo(WriteStaging, ScopeTile, -1, "MixerSettings");
    }
    if ((blockAddr >= 0x040) && (blockAddr <= 0x05C)) {
        if (((blockAddr >> 2) & 0x7) == 7) {
            return WriteInfo(WriteGated, ScopeTile, -1, "QMCSettings(7)");
        }
        return WriteInfo(WriteStaging, ScopeTile, -1, "QMCSettings");
    }
    if ((blockAddr >= 0x090) && (blockAddr <= 0x0AC)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "ThresholdSettings");
    }
    if ((blockAddr >= 0x0D0) && (blockAddr <= 0x14C)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "CalCoefficients");
    }
    if ((blockAddr >= 0x150) && (blockAddr <= 0x158)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "CalFreeze");
    }
    if ((blockAddr >= 0x168) && (blockAddr <= 0x16C)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "DSA");
    }
    if ((blockAddr >= 0x180) && (blockAddr <= 0x19C)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "SignalDetector");
    }
    if ((blockAddr >= 0x1A8) && (blockAddr <= 0x1AC)) {
        return WriteInfo(WriteGated, ScopeTile, -1, "PwrModeSettings");
    }
    switch (blockAddr) {
        case 0x060: return WriteInfo(WriteGated, ScopeTile, -1, "CoarseDelaySettings");
        case 0x064: return WriteInfo(WriteGated, ScopeTile, -1, "UpdateEvent");
        case 0x068: return WriteInfo(WriteGated, ScopeTile, -1, "InterpolationFactor");
        case 0x070: return WriteInfo(WriteGated, ScopeTile, -1, "DecimationFactor");
        case 0x074: return WriteInfo(WriteGated, ScopeTile, -1, "DecimationFactorObs");
        case 0x078: return WriteInfo(WriteGated, ScopeTile, -1, "FabWrVldWords");
        case 0x080: return WriteInfo(WriteGated, ScopeTile, -1, "FabRdVldWords");
        case 0x084: return WriteInfo(WriteGated, ScopeTile, -1, "FabRdVldWordsObs");
        case 0x088: return WriteInfo(WriteGated, ScopeTile, -1, "ThresholdStickyClear");
        case 0x08C: return WriteInfo(WriteGated, ScopeTile, -1, "ThresholdClrMode");
        case 0x0B0: return WriteInfo(WriteGated, ScopeTile, -1, "DecoderMode");
        case 0x0B4: return WriteInfo(WriteGated, ScopeTile, -1, "ResetNCOPhase");
        case 0x0BC: return WriteInfo(WriteGated, ScopeTile, -1, "NyquistZone");
        case 0x0C0: return WriteInfo(WriteGated, ScopeTile, -1, "InvSincFIR");
        case 0x0C4: return WriteInfo(WriteGated, ScopeTile, -1, "CalibrationMode");
        case 0x0C8: return WriteInfo(WriteGated, ScopeTile, -1, "DisableCoefficientsOverride");
        case 0x15C: return WriteInfo(WriteGated, ScopeTile, -1, "Dither");
        case 0x160: return WriteInfo(WriteGated, ScopeTile, -1, "DataScaler");
        case 0x170: return WriteInfo(WriteGated, ScopeTile, -1, "DACVOP");
        case 0x174: return WriteInfo(WriteGated, ScopeTile, -1, "DACCompMode");
        case 0x178: return WriteInfo(WriteGated, ScopeTile, -1, "DataPathMode");
        case 0x17C: return WriteInfo(WriteGated, ScopeTile, -1, "IMRPassMode");
        case 0x1A0: return WriteInfo(WriteGated, ScopeTile, -1, "ResetInternalFIFOWidth");
        case 0x1A4: return WriteInfo(WriteGated, ScopeTile, -1, "ResetInternalFIFOWidthObs");
        case 0x1DC: return WriteInfo(WriteGated, ScopeTile, -1, "IntrEnable");
        case 0x1E0: return WriteInfo(WriteGated, ScopeTile, -1, "IntrDisable");
        case 0x1E4: return WriteInfo(WriteGated, ScopeTile, -1, "IntrClr");
        default: break;
    }
    return none;
}

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
    XRFdc RFdcInst_{};
    XRFdc *RFdcInstPtr_ = &RFdcInst_;

    //! libmetal state owned by this object: the RFDC device set by the single
    //! XRFdc_RegisterMetal call, and whether metal_init succeeded. Both are
    //! released exactly once by CloseMetal.
    struct metal_device* deviceptr_ = nullptr;
    bool metalInited_ = false;

    //! Local variables
    std::string errMsg_;
    uint32_t scratchPad_ = 0;
    double doubleTestReg_ = 0.0;
    bool metalLogLevel_ = false;
    bool ignoreMetalError_ = false;

    //! RFDC config ROM status (loaded from the PYRFDC_CONFIG ROM at construction)
    uint32_t cfgStatus_ = 0;
    std::string cfgMessage_;
    uint32_t cfgHeader_[8] = {};
    uint32_t cfgRomBytes_ = 0;

    bool rdTxn_ = false;
    bool isADC_ = false;
    uint8_t tileId_ = 0;
    uint32_t tileType_ = 0;
    uint8_t blockId_ = 0;
    uint32_t data_ = 0;

    //! Set when the current transaction was refused (not initialized, or a restart
    //! refused on a busy tile), so IgnoreMetalError can never swallow the refusal
    bool txnRefused_ = false;

    XRFdc_MultiConverter_Sync_Config mtsConfig_[2] = {};
    uint32_t mtsfactor_[2][4] = {};

    //! True only after a successful MtsSync of that converter type; cleared through
    //! ClearMtsValidTypes by every operation that restarts or powers down a tile of the
    //! type, and by a restart of a clock source that feeds a tile of the type. The read
    //! also checks the sync group live (MtsValid) and never changes the latch.
    bool mtsValid_[2] = {false, false};

    //! Staging buffers: filled read-only from hardware, never written back by a restart
    uint32_t clkSrcConfig_[2][4] = {};
    XRFdc_PLL_Settings pllConfig_[2][4] = {};
    XRFdc_QMC_Settings qmcConfig_[2][4][4] = {};
    XRFdc_Mixer_Settings mixerConfig_[2][4][4] = {};

    //! Per-tile restart records: [type][tile], type 0=ADC 1=DAC
    uint32_t resetRecord_[2][4] = {};
    uint32_t stateAtFailure_[2][4] = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}};
    uint32_t commonAtFailure_[2][4] = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}};
    uint32_t clkDetAtFailure_[2][4] = {{0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF}};
    uint16_t resetSeq_[2][4] = {};

    //! Clock distribution read window: the last XRFdc_GetClkDistribution result and
    //! its status (0 unsupported, 1 ok, 2 Get failed). The result is fetched once per
    //! transaction, on the first window word, and clkDistFetched_ is cleared where
    //! doTransaction clears errMsg_, so one block read never mixes two snapshots.
    XRFdc_Distribution_System_Settings clkDist_ = {};
    uint32_t clkDistStatus_ = 0;
    bool clkDistFetched_ = false;

    //! SetClkDistribution: the staged settings (filled by the staging words or by a
    //! staging refresh from a Get entry) and the masks of the last commit: ADC affected,
    //! DAC affected, ADC span, DAC span. A commit zeroes the masks when it starts.
    XRFdc_Distribution_Settings clkDistStage_ = {};
    uint32_t clkDistLast_[4] = {};

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
    void MtsValid();

    //! The one place a flag is cleared outside MtsSync: typeMask bit 0 ADC, bit 1 DAC.
    //! ClearMtsValidForRestart picks the types for a restart of the given tile masks
    //! from a fresh topology read (MtsTypesToClear) and calls it before the first write
    void ClearMtsValidTypes(uint32_t typeMask);
    void ClearMtsValidForRestart(uint32_t adcMask, uint32_t dacMask);
    void MtsLatency(uint8_t index);
    void MtsOffset(uint8_t index);
    void MtsFactor(uint8_t index);

    void IpVersion();
    void RestartSM();
    void RestartState();
    void ClockDetector();
    void TileCommonStatus();
    void TileCurrentState();

    void MetalLogLevel();
    void IgnoreMetalError();
    void ScratchPad();
    void DoubleTestReg(bool upper);
    uint32_t DoubleToUint32(double value, bool upper);
    double RemapDoubleWithUint32(double original, uint32_t newPart, bool upper);

    //! RFDC config ROM status block (0x14000 to 0x141FC) and the not-initialized guard
    void ConfigStatusReg(uint32_t addr);

    //! Read-only clock distribution window (0x15000 to 0x157FC) and its once per
    //! transaction driver read
    void ClkDistFetch();
    void ClkDistReg(uint32_t addr);

    //! SetClkDistribution block (0x15800 to 0x15928): staging words, the staging refresh
    //! from a Get entry, the precheck, the preview and the commit
    void ClkDistStagingReg(uint32_t addr);
    void ClkDistRefreshCmd();
    ClkDistCheck ClkDistValidate(uint32_t& adcAffected, uint32_t& dacAffected, uint32_t& adcSpan, uint32_t& dacSpan, std::vector<std::string>* lines);
    void ClkDistPreviewReg(uint32_t addr);
    void ClkDistCommit();

    //! Read-only Vivado mixer fields of the config ROM (tile-only 0x200 to 0x23C)
    void RomMixerConfigReg(uint32_t tileAddr);

    //! Tile-only 0x828: re-read the tile's driver state into the staging words
    void RefreshStagingCmd();
    bool DriverFree(uint32_t addr) const;
    void SetConfigStatus(uint32_t status, const std::string& msg);

    //! Close the metal device when set, then finish libmetal when it was initialized
    void CloseMetal();

    //! Per-tile restart records, Reset Count, and the diagnostic string builders
    void RecordRestart(uint32_t type, uint32_t tile, uint32_t op, uint32_t result, uint32_t flags);
    std::string DiagLine(const char* op, uint32_t type, uint32_t tile, const char* call);
    std::string RestartFailureError(const char* op, uint32_t type, const std::vector<TileFailure>& failures, uint64_t seq0);
    std::string RestartFailureError(const char* op, const std::vector<TileFailure>& adcFailures, const std::vector<TileFailure>& dacFailures, uint64_t seq0);
    void ResetRecordReg(uint32_t addr);

    //! Restart helpers: one prechecked driver restart per tile, read-only staging refresh
    RestartWait RestartPrecheck(uint32_t type, uint32_t tile, uint32_t& firstState, uint32_t& lastState);
    bool RestartProceed(const char* op, uint32_t type, uint32_t tile, RestartWait wait, uint32_t firstState, uint32_t lastState, uint64_t seq0);
    void RestartTiles(uint32_t op, uint32_t type, int tile, uint32_t startState, uint32_t endState);
    bool TileReady(uint32_t type, uint32_t tile);
    bool RefreshStaging(uint32_t type, uint32_t tile);
    void ResyncDriverCache(uint32_t type, uint32_t tile);

    //! Write gate: read-only check that every tile in the write's scope is at
    //! state 15 with Restart clear; fills why with the refusal text when it is not
    bool GateWrite(const WriteGateInfo& info, uint32_t addr, std::string& why);
    bool WriteAllowed(uint32_t addr, std::string& why);

  public:
    //! Class factory which returns a pointer
    static std::shared_ptr<PyRFdc> create(const std::string& cfg);

    //! Setup class for use in python
    static void setup_python();

    //! Create a PyRFdc device, decoding the RFDC config ROM bytes read by the launcher
    PyRFdc(const std::string& cfg);

    //! Destroy the PyRFdc
    ~PyRFdc();

    //! Handle the incoming memory transaction
    void doTransaction(std::shared_ptr<rogue::interfaces::memory::Transaction> transaction);
};

//! Alias for using shared pointer as PyRFdcPtr
typedef std::shared_ptr<PyRFdc> PyRFdcPtr;

#endif
