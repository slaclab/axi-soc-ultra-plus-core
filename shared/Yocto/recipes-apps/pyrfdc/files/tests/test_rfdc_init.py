#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RF data converter Init() reset request tests
#-----------------------------------------------------------------------------
# Description: Board-free unittest suite for the reset requests Rfdc.Init() issues
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

"""Board-free proof of which reset requests Rfdc.Init() issues.

The real rfsoc_utility.Rfdc device model from this tree is mounted on an
emulated register map that records the address of every write transaction
reaching it. A reset is requested by writing a RemoteCommand, so the list of
recorded addresses during Init() is exactly the list of resets the driver
would be asked for. Every expected address below is a literal number, never
recomputed from the model under test, so an offset error in the model cannot
be mirrored by the same error in its test.

The model is imported from this tree's python/ directory, which is put first
on sys.path, and one test asserts the imported file lies under it, so a stale
installed copy cannot be the one tested. The model imports surf, which is
taken from PYTHONPATH; a host without surf fails these tests rather than
skipping them.

Run from the submodule root, with PYTHONPATH naming a surf checkout's
python/ directory:

    PYTHONPATH=<surf>/python python3 -B -m unittest discover -s shared/Yocto/recipes-apps/pyrfdc/files/tests -p 'test_*.py' -v
"""

import contextlib
import io
import os
import sys
import threading
import unittest
import warnings

TESTS_DIR      = os.path.dirname(os.path.abspath(__file__))
SUBMODULE_ROOT = os.path.normpath(os.path.join(TESTS_DIR, '..', '..', '..', '..', '..', '..'))
PYTHON_DIR     = os.path.join(SUBMODULE_ROOT, 'python')

if sys.path[:1] != [PYTHON_DIR]:
    sys.path.insert(0, PYTHON_DIR)

import pyrogue as pr                                      # noqa: E402
import rogue                                              # noqa: E402
import pyrogue.interfaces.simulation                      # noqa: E402
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility  # noqa: E402
import axi_soc_ultra_plus_core.rfsoc_utility._Rfdc as rfdcModule  # noqa: E402

# Global reset commands of the Rfdc device
RESET_ALL_ADC_ADDR_C = 0x10010
RESET_ALL_DAC_ADDR_C = 0x10014

# Per-tile Reset command, tile offset 0x008, ADC tiles at 0x2000*i and DAC
# tiles at 0x8000+0x2000*i
ADC_TILE_RESET_ADDRS_C = [0x0008, 0x2008, 0x4008, 0x6008]
DAC_TILE_RESET_ADDRS_C = [0x8008, 0xA008, 0xC008, 0xE008]

# Cached clock distribution map, four bits per tile
CLK_DIST_MAP_ADDR_C = 0x12014

# The carrier's map: ADC 3 and DAC 1 to 3 are clocked by DAC 0
CARRIER_CLK_DIST_MAP_C = 0x44444FFF

# Every tile ungrouped, as on a board without a clock distribution
UNGROUPED_CLK_DIST_MAP_C = 0xFFFFFFFF

TILE_RESET_NAMES_C = {
    0x0008 : 'ADC 0',
    0x2008 : 'ADC 1',
    0x4008 : 'ADC 2',
    0x6008 : 'ADC 3',
    0x8008 : 'DAC 0',
    0xA008 : 'DAC 1',
    0xC008 : 'DAC 2',
    0xE008 : 'DAC 3',
}

GLOBAL_RESET_PAIR_C = [RESET_ALL_ADC_ADDR_C, RESET_ALL_DAC_ADDR_C]

ALL_ENABLED_C = [True, True, True, True]
NONE_ENABLED_C = [False, False, False, False]


class WriteRecordingMemory(pyrogue.interfaces.simulation.MemEmulate):
    """MemEmulate that records the address of every write and post
    transaction before the base class stores its data."""

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._logLock = threading.Lock()
        self._writes  = []

    def _doTransaction(self, transaction):
        if transaction.type() in (rogue.interfaces.memory.Write, rogue.interfaces.memory.Post):
            with self._logLock:
                self._writes.append(transaction.address())
        super()._doTransaction(transaction)

    def writes(self):
        with self._logLock:
            return list(self._writes)

    def clearWrites(self):
        with self._logLock:
            self._writes.clear()

    def seedWord(self, address, value):
        """Place a 4-byte little-endian word straight into the emulated
        byte store, bypassing the bus, so a seed is never recorded."""
        for i in range(4):
            self._data[address + i] = (value >> (8 * i)) & 0xFF


def hexList(addresses):
    return '[' + ', '.join(f'{a:#06x}' for a in addresses) + ']'


class RfdcInitResetSequenceTest(unittest.TestCase):

    def buildRoot(self, adcEnabled=ALL_ENABLED_C, dacEnabled=ALL_ENABLED_C, clkDistMap=None):
        """Start a root with this tree's Rfdc on a write-recording map, seed
        the eight enabled checks (and the clock distribution map when
        given), read the whole tree so the tile devices are enabled as they
        are on the board when the bring-up sequence reaches Init(), and
        return the root and the memory with the write log cleared."""
        memory = WriteRecordingMemory()
        root = pr.Root(name='RfdcInitTestRoot', pollEn=False, timeout=5.0)
        root.add(rfsoc_utility.Rfdc(memBase=memory))
        root._doHeartbeat = False
        with contextlib.redirect_stdout(io.StringIO()):
            root.start()
        self.addCleanup(root.stop)

        rfdc = root.Rfdc
        for i in range(4):
            for var, enabled in ((rfdc.CheckAdcTileEnabled[i], adcEnabled[i]),
                                 (rfdc.CheckDacTileEnabled[i], dacEnabled[i])):
                memory.seedWord(var.address, (1 if enabled else 0) << var.bitOffset[0])

        if clkDistMap is not None:
            memory.seedWord(CLK_DIST_MAP_ADDR_C, clkDistMap)

        root.ReadAll()
        root.waitOnUpdate()
        memory.clearWrites()
        return root, memory

    def runInit(self, root):
        # Init() prints a banner and UpdateIsEnabled() still calls the
        # deprecated checkBlocks(). Neither says anything about the reset
        # requests, and both would land in the verbose verdict output, so
        # they are kept out of it for this call only.
        with contextlib.redirect_stdout(io.StringIO()), warnings.catch_warnings():
            warnings.filterwarnings('ignore', message=r'`checkBlocks\(\)` is deprecated', category=DeprecationWarning)
            root.Rfdc.Init()

    def initWrites(self, **kwargs):
        root, memory = self.buildRoot(**kwargs)
        self.runInit(root)
        return memory.writes()

    def test_init_issues_only_the_global_reset_pair(self):
        """Init() with every tile enabled writes ResetAllAdc then ResetAllDac and nothing else."""
        writes = self.initWrites()
        self.assertEqual(writes, GLOBAL_RESET_PAIR_C,
                         f'Init() recorded {hexList(writes)}, expected {hexList(GLOBAL_RESET_PAIR_C)}')

    def test_init_restarts_no_tile_on_its_own(self):
        """No write during Init() reaches a per-tile Reset command."""
        writes = self.initWrites()
        hits = [a for a in writes if a in TILE_RESET_NAMES_C]
        self.assertEqual(hits, [],
                         'Init() restarted tiles one at a time: '
                         + ', '.join(f'{a:#06x} ({TILE_RESET_NAMES_C[a]})' for a in hits)
                         + '; 0x6008 is ADC 3, clocked by DAC 0, and 0x8008 is DAC 0, which clocks it')

    def test_tile_reset_commands_stay_available(self):
        """Every tile keeps its Reset command, and a direct ADC 3 Reset() writes only 0x6008."""
        root, memory = self.buildRoot()
        rfdc = root.Rfdc
        for i in range(4):
            self.assertEqual(rfdc.AdcTile[i].Reset.address, ADC_TILE_RESET_ADDRS_C[i])
            self.assertEqual(rfdc.DacTile[i].Reset.address, DAC_TILE_RESET_ADDRS_C[i])

        rfdc.AdcTile[3].Reset()
        writes = memory.writes()
        self.assertEqual(writes, [0x6008], f'AdcTile[3].Reset() recorded {hexList(writes)}')

    def test_init_sequence_does_not_depend_on_the_topology(self):
        """Init() writes the same pair with the carrier's map and with every tile ungrouped."""
        recorded = {}
        for clkDistMap in (CARRIER_CLK_DIST_MAP_C, UNGROUPED_CLK_DIST_MAP_C):
            root, memory = self.buildRoot(clkDistMap=clkDistMap)
            self.assertEqual(root.Rfdc.ClkDistMap.value(), clkDistMap)
            self.runInit(root)
            recorded[clkDistMap] = memory.writes()

        self.assertEqual(recorded[CARRIER_CLK_DIST_MAP_C], recorded[UNGROUPED_CLK_DIST_MAP_C])
        self.assertEqual(recorded[UNGROUPED_CLK_DIST_MAP_C], GLOBAL_RESET_PAIR_C,
                         f'Init() recorded {hexList(recorded[UNGROUPED_CLK_DIST_MAP_C])}')

    def test_init_with_one_enabled_tile_or_none(self):
        """Init() writes exactly the pair with only ADC 0 enabled and with no tile enabled."""
        cases = {
            'only ADC 0 enabled' : ([True, False, False, False], NONE_ENABLED_C),
            'no tile enabled'    : (NONE_ENABLED_C, NONE_ENABLED_C),
        }
        for label, (adcEnabled, dacEnabled) in cases.items():
            with self.subTest(label):
                writes = self.initWrites(adcEnabled=adcEnabled, dacEnabled=dacEnabled)
                self.assertEqual(writes, GLOBAL_RESET_PAIR_C,
                                 f'{label}: Init() recorded {hexList(writes)}, expected {hexList(GLOBAL_RESET_PAIR_C)}')

    def test_model_under_test_is_this_tree(self):
        """The imported _Rfdc.py is the one under this tree's python/ directory."""
        imported = os.path.realpath(rfdcModule.__file__)
        expected = os.path.realpath(PYTHON_DIR) + os.sep
        self.assertTrue(imported.startswith(expected), f'{imported} is not under {expected}')


if __name__ == '__main__':
    unittest.main()
