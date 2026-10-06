#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter tile
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class PyRFdc(rogue::interfaces::memory)
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import contextlib
import re
import threading
import time
import rogue
import rogue.interfaces.memory as rim
import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

rogue.Version.minVersion('6.15.0')

class Rfdc(pr.Device):

    # Transaction timeout floor for this device and everything below it. The longest
    # single transaction is a clock distribution Set of the largest span, all eight
    # tiles. Per tile it can spend 1 s in the restart precheck, 1 s waiting for the
    # tile to reach state 1 and 1 s waiting for it to reach state 15 (the driver's own
    # budgets in xrfdc.h), plus 1 s once for the source tile to reach state 7:
    # 8 x 3 s + 1 s = 25 s, plus 25 percent margin, rounded up. The 12 s restart of
    # the four tiles of one converter type (4 x 3 s) fits inside. The floor applies per
    # transaction; the state 15 wait and the re-apply of the written settings are many
    # short transactions and do not add to it.
    TIMEOUT_FLOOR_S = 32.0

    # Shared deadline of one state 15 wait over every enabled tile: the driver
    # budgets of 1 s PLL lock and 1 s restart clear for a tile that the
    # clock-distribution source knocks after the commanded restart returns, plus
    # margin. The settle times seen on the board are recorded in _lastGate.
    GATE_TIMEOUT_S = 5.0
    GATE_POLL_S = 0.05

    # The IP power-up QMC values, read right after a reboot on every enabled block of the
    # board (identical on all of them). A QMC that was never committed is returned to
    # these when it differs. The event source is never part of the default: it is what
    # the hardware restored or what the operator committed.
    QMC_POWERUP_DEFAULT = {
        'EnablePhase':            False,
        'EnableGain':             False,
        'GainCorrectionFactor':   0.0,
        'PhaseCorrectionFactor':  0.0,
        'OffsetCorrectionFactor': 0,
    }

    # The documented order in which the written settings are written back after a
    # restart. Each entry is (step, scope, name), applied step by step and, inside a
    # step, in the order listed; a variable is re-applied only when the operator wrote
    # it in this process (see ConfigVariable). A name that a converter type or
    # generation does not have is skipped.
    #   pll         the committed tile PLL, before any other write, because a PLL
    #               commit restarts the tile (see _applyPll)
    #   rates       FabClkOutDiv, the block interpolation and decimation factors and
    #               fabric words, DataPathMode and IMRPassMode (the Gen3 DAC mixer
    #               depends on the datapath mode), then the tile FIFO enables, which
    #               are written back in the order they were written (see _applyFifoWords)
    #   calibration, nyquist, dsa
    #   qmc, mixer  after the rates, calibration, Nyquist zone and DSA: XRFdc_SetMixerSettings
    #               reads the PLL rate, the Nyquist zone and the datapath mode and resets the
    #               internal FIFO width, so it must see their final values
    #   other       everything else, ending with the calibration overrides, power mode and DACVOP
    # Scopes: 'tile' and 'block' name a variable relative to the tile or block (a name
    # without an index also names every element of an array); 'group' names a block
    # sub-device committed as one unit (written from its commit snapshot, or restored to
    # its default when it was never committed); 'word' names the fields that share one
    # hardware word, written together once (see _applyWord).
    APPLY_ORDER = (
        ('pll', 'tile', 'PllConfig'),
        ('rates', 'tile', 'FabClkOutDiv'),
        ('rates', 'block', 'InterpolationFactor'),
        ('rates', 'block', 'DecimationFactor'),
        ('rates', 'block', 'DecimationFactorObs'),
        ('rates', 'block', 'FabWrVldWords'),
        ('rates', 'block', 'FabRdVldWords'),
        ('rates', 'block', 'FabRdVldWordsObs'),
        ('rates', 'block', 'DataPathMode'),
        ('rates', 'block', 'IMRPassMode'),
        ('rates', 'tile', 'SetupFIFO'),
        ('rates', 'tile', 'SetupFIFOObs'),
        ('rates', 'tile', 'SetupFIFOBoth'),
        ('calibration', 'block', 'Calibration.CalibrationMode'),
        ('nyquist', 'block', 'NyquistZone'),
        ('dsa', 'block', 'DSA.DisableRTS'),
        ('dsa', 'block', 'DSA.Attenuation'),
        ('qmc', 'group', 'QMC'),
        ('mixer', 'group', 'Mixer'),
        ('other', 'block', 'DACCompMode'),
        ('other', 'block', 'DecoderMode'),
        ('other', 'block', 'InvSincFIR'),
        ('other', 'block', 'DataScaler'),
        ('other', 'block', 'Dither'),
        ('other', 'word', ('CoarseDelay.CoarseDelay', 'CoarseDelay.EventSource')),
        ('other', 'block', 'Threshold.ThresholdMode'),
        ('other', 'block', 'Threshold.ThresholdAvgVal'),
        ('other', 'block', 'Threshold.ThresholdUnderVal'),
        ('other', 'block', 'Threshold.ThresholdOverVal'),
        ('other', 'word', ('Threshold.ThresholdClrMode_ThresholdToUpdate', 'Threshold.ThresholdClrMode_ClrMode')),
        ('other', 'block', 'SignalDetector.Mode'),
        ('other', 'block', 'SignalDetector.TimeConstant'),
        ('other', 'block', 'SignalDetector.Flush'),
        ('other', 'block', 'SignalDetector.EnableIntegrator'),
        ('other', 'block', 'SignalDetector.Threshold'),
        ('other', 'block', 'SignalDetector.ThresholdOnTriggerCnt'),
        ('other', 'block', 'SignalDetector.ThresholdOffTriggerCnt'),
        ('other', 'block', 'SignalDetector.HysteresisEnable'),
        ('other', 'block', 'PwrModeSettings.DisableIPControl'),
        ('other', 'block', 'PwrModeSettings.PwrMode'),
        ('other', 'block', 'Calibration.CAL_BLOCK_OCB1_Coeff'),
        ('other', 'block', 'Calibration.CAL_BLOCK_OCB2_Coeff'),
        ('other', 'block', 'Calibration.CAL_BLOCK_GCB_Coeff'),
        ('other', 'block', 'Calibration.CAL_BLOCK_TSCB_Coeff'),
        ('other', 'block', 'Calibration.DisableFreezePin'),
        ('other', 'block', 'Calibration.FreezeCalibration'),
        ('other', 'block', 'DACVOP'),
    )

    # Every other writable variable of the tree, with the reason it is not re-applied.
    # Keyed by the variable name; the offline walk of the whole tree fails on a writable
    # variable that is neither in APPLY_ORDER nor here.
    APPLY_EXCLUDED = {
        'RestartStateStart':           'configures the next restart, not the operating state',
        'RestartStateEnd':             'configures the next restart, not the operating state',
        'AdcTiles':                    'multi-tile sync input, owned by the MTS flow',
        'DacTiles':                    'multi-tile sync input, owned by the MTS flow',
        'AdcRefTile':                  'multi-tile sync input, owned by the MTS flow',
        'DacRefTile':                  'multi-tile sync input, owned by the MTS flow',
        'SysRefConfig':                'multi-tile sync input, owned by the MTS flow',
        'AdcSysRefEnable':             'multi-tile sync input, owned by the MTS flow',
        'DacSysRefEnable':             'multi-tile sync input, owned by the MTS flow',
        'AdcTargetLatency':            'multi-tile sync input, owned by the MTS flow',
        'DacTargetLatency':            'multi-tile sync input, owned by the MTS flow',
        'MetalLogLevel':               'driver debug setting',
        'IgnoreMetalError':            'driver debug setting, forced off by Init and restored by it',
        'Scratchpad':                  'test register with no effect on the converters',
        'DoubleTestReg':               'test register with no effect on the converters',
        'ThresholdStickyClear':        'an action, not a setting',
        'DisableCoefficientsOverride': 'an action, not a setting',
        'IntrEnable':                  'interrupt mask, not a converter setting',
        'IntrDisable':                 'interrupt mask, not a converter setting',
        'IntrClr':                     'interrupt mask, not a converter setting',
        'SetSourceType':               'clock distribution staging; a Set is transient and never re-applied',
        'SetSourceTileId':             'clock distribution staging; a Set is transient and never re-applied',
        'SetEdgeType[0]':              'clock distribution staging; a Set is transient and never re-applied',
        'SetEdgeType[1]':              'clock distribution staging; a Set is transient and never re-applied',
        'SetEdgeTileId[0]':            'clock distribution staging; a Set is transient and never re-applied',
        'SetEdgeTileId[1]':            'clock distribution staging; a Set is transient and never re-applied',
        'SetDistributedClock':         'clock distribution staging; a Set is transient and never re-applied',
        'SetShutdownMode':             'clock distribution staging; a Set is transient and never re-applied',
        'SetDistRefClkFreq':           'clock distribution staging; a Set is transient and never re-applied',
        'SetAdcSampleRate[0]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetAdcSampleRate[1]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetAdcSampleRate[2]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetAdcSampleRate[3]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetDacSampleRate[0]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetDacSampleRate[1]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetDacSampleRate[2]':         'clock distribution staging; a Set is transient and never re-applied',
        'SetDacSampleRate[3]':         'clock distribution staging; a Set is transient and never re-applied',
    }

    # The settings that keep their value across a state 0 restart, so a tile Reset does not
    # return them to the Vivado value, with the value that a setting nobody wrote in this
    # process is returned to. Keyed (converter, name relative to the block, or to the tile for
    # the FIFO words). Each value is what was read right after a reboot of the
    # SlacRfmcCarrier image. The ADC observation channel settings (DecimationFactorObs,
    # FabRdVldWordsObs and the observation FIFO enable, which SetupFIFOObs and
    # SetupFIFOBoth also drive) read their after-reboot value on that image only, where the
    # channel is unused, so they have no entry here: they are written back only when the
    # operator wrote them, until a per-board measurement (or a config ROM value) exists.
    # A setting that was never written is written back to its value only when the
    # hardware reads differently. The QMC and mixer survivors have their own defaults
    # (QMC_POWERUP_DEFAULT and the config ROM NCO). None marks a survivor that software
    # cannot return to its power-up value: an ADC coarse delay event source written to Tile
    # or Slice stays after a restart and a write of Immediate could not bring it back (a
    # high-speed ADC refuses it). An event source is never rewritten here, so the word is
    # written back with the event source the hardware holds. The FIFO values are the status
    # flags read back after the restart. SetupFIFOBoth has no default.
    SURVIVOR_DEFAULTS = {
        ('ADC', 'DSA.DisableRTS'):                0,
        ('ADC', 'DSA.Attenuation'):               0.0,
        ('ADC', 'Threshold.ThresholdMode[0]'):    0,
        ('ADC', 'Threshold.ThresholdMode[1]'):    0,
        ('ADC', 'Threshold.ThresholdAvgVal[0]'):  0,
        ('ADC', 'Threshold.ThresholdAvgVal[1]'):  0,
        ('ADC', 'Threshold.ThresholdUnderVal[0]'): 0,
        ('ADC', 'Threshold.ThresholdUnderVal[1]'): 0,
        ('ADC', 'Threshold.ThresholdOverVal[0]'): 0,
        ('ADC', 'Threshold.ThresholdOverVal[1]'): 0,
        ('ADC', 'CoarseDelay.CoarseDelay'):       0,
        ('ADC', 'CoarseDelay.EventSource'):       None,
        ('ADC', 'SetupFIFO'):                     {'FIFOStatus': True},
        ('DAC', 'DataScaler'):                    False,
        ('DAC', 'CoarseDelay.CoarseDelay'):       0,
        ('DAC', 'SetupFIFO'):                     {'FIFOStatus': True},
    }

    # A tile FIFO word and the Rfdc word that sets it on every tile of a converter type:
    # the record written last wins. FIFO_STATUS names the tile status flag that a default
    # is compared with, and FIFO_ENABLE is the word that enables a FIFO (the C++ takes the
    # enable from bit 1).
    FIFO_GLOBAL = {
        ('SetupFIFO', True):      'SetupFIFOAllAdc',
        ('SetupFIFO', False):     'SetupFIFOAllDac',
        ('SetupFIFOObs', True):   'SetupFIFOObsAllAdc',
        ('SetupFIFOBoth', True):  'SetupFIFOBothAllAdc',
    }
    FIFO_STATUS = {'SetupFIFO': 'FIFOStatus', 'SetupFIFOObs': 'FIFOStatusObs'}
    FIFO_ENABLE = 3
    FIFO_WORDS = ('SetupFIFO', 'SetupFIFOObs', 'SetupFIFOBoth')

    def __init__(
            self,
            enAdcTile = None,
            enDacTile = None,
            gen3      = True, # True if using RFSoC GEN3 Hardware
            **kwargs):
        super().__init__(**kwargs)
        self.gen3      = gen3
        self.enAdcTile = [True,True,True,True] if enAdcTile is None else enAdcTile
        self.enDacTile = [True,True,True,True] if enDacTile is None else enDacTile

        # Held across every full restart sequence and every raw command word, so two
        # clients cannot interleave a sequence or swap an error text
        self._seqLock   = threading.RLock()
        self._lastGate  = {}
        self._lastApply = {}
        # Set by _clkDistCommitCmd when the SetClkDistribution commit word went to PyRFdc
        self._clkDistCommitSent = False

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/IP-Version-Information-0x0000
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IpVerMajor',
            offset       = 0x10044,
            bitSize      = 8,
            bitOffset    = 24,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'IpVerMinor',
            offset       = 0x10044,
            bitSize      = 8,
            bitOffset    = 16,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.LinkVariable(
            name         = 'IpCoreVersion',
            description  = 'IP Version Information',
            mode         = 'RO',
            linkedGet    = lambda read: f'v{self.IpVerMajor.get(read=read)}.{self.IpVerMinor.get(read=read)}',
            dependencies = [self.IpVerMajor,self.IpVerMinor]
        ))

        self.add(pr.RemoteVariable(
            name         = 'IpCoreRevision',
            offset       = 0x10044,
            bitSize      = 8,
            bitOffset    = 8,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'StartUpAllAdcRaw',
            description  = 'Bare XRFdc_StartUp of every enabled ADC tile in PyRFdc from state 1, settings kept; does not wait for state 15',
            offset       = 0x10000,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=True),
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'StartUpAllDacRaw',
            description  = 'Bare XRFdc_StartUp of every enabled DAC tile in PyRFdc from state 1, settings kept; does not wait for state 15',
            offset       = 0x10004,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=False),
            hidden       = True,
        ))

        self.add(pr.LocalCommand(
            name         = 'StartUpAllAdc',
            description  = 'Default recovery action: restarts every enabled ADC tile from state 1 keeping its settings, then waits for every enabled tile to reach state 15 with the PLL locked. Re-applies nothing',
            function     = lambda: self._globalStartUpSequence(isAdc=True),
        ))

        self.add(pr.LocalCommand(
            name         = 'StartUpAllDac',
            description  = 'Default recovery action: restarts every enabled DAC tile from state 1 keeping its settings, then waits for every enabled tile to reach state 15 with the PLL locked. Re-applies nothing',
            function     = lambda: self._globalStartUpSequence(isAdc=False),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ShutdownAllAdc',
            description  = 'This API function stops ALL ADC tiles',
            offset       = 0x10008,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'ShutdownAllDac',
            description  = 'This API function stops ALL DAC tiles',
            offset       = 0x1000C,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ResetAllAdcRaw',
            description  = 'Bare XRFdc_Reset of every enabled ADC tile in PyRFdc, clock distribution sources first: returns each tile to state 15 with the restart-reloaded registers at their Vivado values. Re-applies nothing and does not restore mixer NCO settings',
            offset       = 0x10010,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=True),
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'ResetAllDacRaw',
            description  = 'Bare XRFdc_Reset of every enabled DAC tile in PyRFdc, clock distribution sources first: returns each tile to state 15 with the restart-reloaded registers at their Vivado values. Re-applies nothing and does not restore mixer NCO settings',
            offset       = 0x10014,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=False),
            hidden       = True,
        ))

        self.add(pr.LocalCommand(
            name         = 'ResetAllAdc',
            description  = 'Resets every enabled ADC tile once, clock distribution sources first, waits for every enabled tile, then re-applies the settings written in this session. Settings not written in this session read their Vivado value. A bare restart does not restore mixer NCO settings',
            function     = lambda: self._globalResetSequence(isAdc=True),
        ))

        self.add(pr.LocalCommand(
            name         = 'ResetAllDac',
            description  = 'Resets every enabled DAC tile once, clock distribution sources first, waits for every enabled tile, then re-applies the settings written in this session. Settings not written in this session read their Vivado value. A bare restart does not restore mixer NCO settings',
            function     = lambda: self._globalResetSequence(isAdc=False),
        ))

        self.add(pr.LocalCommand(
            name         = 'ApplyConfig',
            description  = 'Waits for every enabled tile to reach state 15, then re-applies in the documented order the settings written in this session on every enabled tile and returns untouched restart survivors (mixer NCO, QMC) to their Vivado or power-up value. Issues no restart unless a committed PLL differs from the hardware, in which case that tile is restarted (PLL commit, then StartUp); reads the mirrors back afterwards',
            function     = self._applyConfigCommand,
        ))

        #######################################################################################
        # Refresh every enabled tile's sticky restart record (used by the
        # global commands above and available standalone)
        #######################################################################################
        self.add(pr.LocalCommand(
            name         = 'RefreshResetRecords',
            description  = 'Refresh LastResetResult/StateAtFailure/FailureCount on every enabled tile',
            function     = self._refreshAllResetRecords,
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        #######################################################################################
        self.add(pr.LocalVariable(
            name         = 'CustomStartUpAllAdc_StartState',
            description  = 'StartState used by the next CustomStartUpAllAdc command. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0x1,
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.LocalVariable(
            name         = 'CustomStartUpAllAdc_EndState',
            description  = 'EndState used by the next CustomStartUpAllAdc command. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0xF,
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'CustomStartUpAllAdc',
            description  = 'Run the power-on state machine from CustomStartUpAllAdc_StartState to CustomStartUpAllAdc_EndState on ALL ADC tiles in one command (StartState bits 3:0, EndState bits 7:4)',
            offset       = 0x10018,
            bitSize      = 8,
            function     = lambda cmd: self._globalCustomStartUpCmd(cmd, isAdc=True),
            hidden       = True,
        ))

        self.add(pr.LocalVariable(
            name         = 'CustomStartUpAllDac_StartState',
            description  = 'StartState used by the next CustomStartUpAllDac command. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0x1,
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.LocalVariable(
            name         = 'CustomStartUpAllDac_EndState',
            description  = 'EndState used by the next CustomStartUpAllDac command. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0xF,
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'CustomStartUpAllDac',
            description  = 'Run the power-on state machine from CustomStartUpAllDac_StartState to CustomStartUpAllDac_EndState on ALL DAC tiles in one command (StartState bits 3:0, EndState bits 7:4)',
            offset       = 0x1001C,
            bitSize      = 8,
            function     = lambda cmd: self._globalCustomStartUpCmd(cmd, isAdc=False),
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFO
        #######################################################################################
        self.add(rfsoc_utility.ConfigVariable(
            name         = 'SetupFIFOAllAdc',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO ALL ADC tiles. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
            offset       = 0x10020,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "False",
                0x3 : "True",
            },
            hidden       = True,
        ))

        self.add(rfsoc_utility.ConfigVariable(
            name         = 'SetupFIFOAllDac',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO ALL DAC tiles. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
            offset       = 0x10024,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "False",
                0x3 : "True",
            },
            hidden       = True,
        ))

        if gen3:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOObs-Gen-3/DFE
            #######################################################################################
            self.add(rfsoc_utility.ConfigVariable(
                name         = 'SetupFIFOObsAllAdc',
                description  = 'This API function enables and disables the RF-ADC observation channel FIFO. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
                offset       = 0x10028,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "False",
                    0x3 : "True",
                },
                hidden       = True,
            ))

            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOBoth-Gen-3/DFE
            #######################################################################################
            self.add(rfsoc_utility.ConfigVariable(
                name         = 'SetupFIFOBothAllAdc',
                description  = 'This API function enables and disables the RF-ADC actual and observation channel FIFO. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
                offset       = 0x1002C,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "False",
                    0x3 : "True",
                },
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMasterTile
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'MasterAdcTile',
            description  = 'Returns the master ADC tile ID',
            offset       = 0x10030,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'MasterDacTile',
            description  = 'Returns the master DAC tile ID',
            offset       = 0x10034,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_IPBaseAddr
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IPBaseAddr',
            description  = 'Base address of the IP',
            offset       = 0x10040,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDriverVersion
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'DriverVersion',
            description  = 'Driver version number',
            offset       = 0x10048,
            bitSize      = 64,
            mode         = 'RO',
            base         = pr.Double,
            disp         = '{:1.1f}',
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckTileEnabled
        #######################################################################################
        self.addRemoteVariables(
            name         = 'CheckAdcTileEnabled',
            description  = 'This API checks whether RF-ADC tile is enabled or disabled',
            offset       = 0x10050,
            bitSize      = 1,
            mode         = 'RO',
            number       = 4,
            stride       = 4,
            base         = pr.Bool,
            pollInterval = 1,
            hidden       = True,
        )

        self.addRemoteVariables(
            name         = 'CheckDacTileEnabled',
            description  = 'This API checks whether RF-DAC tile is enabled or disabled',
            offset       = 0x10060,
            bitSize      = 1,
            mode         = 'RO',
            number       = 4,
            stride       = 4,
            base         = pr.Bool,
            pollInterval = 1,
            hidden       = True,
        )

        if gen3:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetTileLayout
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'TileLayout',
                description  = 'Gets whether the device is a DFE variant or not',
                offset       = 0x10070,
                bitSize      = 1,
                mode         = 'RO',
                enum         = {
                    0x0 : "XRFDC_4ADC_4DAC_TILES", #define XRFDC_4ADC_4DAC_TILES 0U
                    0x1 : "XRFDC_3ADC_2DAC_TILES", #define XRFDC_3ADC_2DAC_TILES 1U
                },
                hidden       = True,
            ))

        class Mts(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetSysRefSource
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'SysRefAdcSource',
                    description  = 'Returns the source of the SYSREF (internal or external).',
                    offset       = 0x10038,
                    bitSize      = 32,
                    mode         = 'RO',
                    hidden       = True,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'SysRefDacSource',
                    description  = 'Returns the source of the SYSREF (internal or external).',
                    offset       = 0x1003C,
                    bitSize      = 32,
                    mode         = 'RO',
                    hidden       = True,
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMTSEnable
                #######################################################################################

                self.add(pr.RemoteVariable(
                    name         = 'IsAdcEnabled',
                    description  = 'Method to get all the enabled MTS ADC tiles',
                    offset       = 0x11000,
                    bitSize      = 4,
                    mode         = 'RO',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'IsDacEnabled',
                    description  = 'Method to get all the enabled MTS DAC tiles',
                    offset       = 0x11004,
                    bitSize      = 4,
                    mode         = 'RO',
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_MultiConverter_Sync_Config
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Init
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_MultiConverter_Sync
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
                #######################################################################################

                self.add(pr.RemoteCommand(
                    name         = 'SyncAdcTilesRaw',
                    description  = 'Bare multi-tile sync of the ADC tiles in AdcTiles (XRFdc_MultiConverter_Sync): no precheck, no wait. PyRFdc still refuses it while a tile of the group is not at state 15 with Restart clear',
                    offset       = 0x11008,
                    bitSize      = 1,
                    function     = lambda cmd: self.parent._mtsSyncCmd(cmd),
                    hidden       = True,
                ))

                self.add(pr.RemoteCommand(
                    name         = 'SyncDacTilesRaw',
                    description  = 'Bare multi-tile sync of the DAC tiles in DacTiles (XRFdc_MultiConverter_Sync): no precheck, no wait. PyRFdc still refuses it while a tile of the group is not at state 15 with Restart clear',
                    offset       = 0x1100C,
                    bitSize      = 1,
                    function     = lambda cmd: self.parent._mtsSyncCmd(cmd),
                    hidden       = True,
                ))

                self.add(pr.LocalCommand(
                    name         = 'SyncAdcTiles',
                    description  = 'Checks that AdcTiles is not zero and that every tile in AdcTiles plus AdcRefTile is enabled, at state 15 with Restart clear and, on its internal PLL, locked; then runs the ADC multi-tile sync once and checks AdcMtsValid. Refuses with one error naming every failing tile and never waits or restarts a tile. Init and the reset commands never run it',
                    function     = lambda: self.parent._mtsSyncSequence(isAdc=True),
                ))

                self.add(pr.LocalCommand(
                    name         = 'SyncDacTiles',
                    description  = 'Checks that DacTiles is not zero and that every tile in DacTiles plus DacRefTile is enabled, at state 15 with Restart clear and, on its internal PLL, locked; then runs the DAC multi-tile sync once and checks DacMtsValid. Refuses with one error naming every failing tile and never waits or restarts a tile. Init and the reset commands never run it',
                    function     = lambda: self.parent._mtsSyncSequence(isAdc=False),
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_MultiConverter_Sync_Config
                #######################################################################################

                self.add(pr.RemoteVariable(
                    name         = 'AdcTiles',
                    description  = 'Bitmask indicating which tiles to align. BitX enables MTS for TileX. Tile0 must always be enabled.',
                    offset       = 0x11028,
                    bitSize      = 4,
                    mode         = 'RW',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DacTiles',
                    description  = 'Bitmask indicating which tiles to align. BitX enables MTS for TileX. Tile0 must always be enabled.',
                    offset       = 0x1102C,
                    bitSize      = 4,
                    mode         = 'RW',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'AdcMtsValid',
                    description  = 'True only after a successful SyncAdcTiles. Cleared by every restart of an ADC tile (Reset, StartUp, CustomStartUp, Shutdown, RestartSM, PllConfigUpdate), by a restart of the source tile of a clock distribution that feeds an ADC tile (on SlacRfmcCarrier DAC tile 0 feeds ADC tile 3, so a DAC tile 0 restart and ResetAllDac clear it), and by a SetClkDistribution that touches an ADC tile. A restart of member tiles clears only their own converter type. Reads 0 while any enabled tile of AdcTiles plus AdcRefTile is restarting or below state 15, that is not at state 15 with Restart clear; a restart that PyRFdc did not command and that has already finished is not detected. AdcTiles and AdcRefTile are never cleared',
                    offset       = 0x11030,
                    bitSize      = 1,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DacMtsValid',
                    description  = 'True only after a successful SyncDacTiles. Cleared by every restart of a DAC tile (Reset, StartUp, CustomStartUp, Shutdown, RestartSM, PllConfigUpdate), by a restart of the source tile of a clock distribution that feeds a DAC tile (on SlacRfmcCarrier a DAC tile 0 restart clears both flags), and by a SetClkDistribution that touches a DAC tile. A restart of member tiles clears only their own converter type. Reads 0 while any enabled tile of DacTiles plus DacRefTile is restarting or below state 15, that is not at state 15 with Restart clear; a restart that PyRFdc did not command and that has already finished is not detected. DacTiles and DacRefTile are never cleared',
                    offset       = 0x11034,
                    bitSize      = 1,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'AdcRefTile',
                    description  = 'Reference tile',
                    offset       = 0x11010,
                    bitSize      = 4,
                    mode         = 'RW',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DacRefTile',
                    description  = 'Reference tile',
                    offset       = 0x11014,
                    bitSize      = 4,
                    mode         = 'RW',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'SysRefConfig',
                    description  = 'Used to enable and disable the sysref',
                    offset       = 0x11100,
                    bitSize      = 1,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'AdcSysRefEnable',
                    description  = 'Set to 1 (default) to keep SYSREF capture enabled after MTS runs. Set to 0 to disable SYSREF capture',
                    offset       = 0x11018,
                    bitSize      = 1,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DacSysRefEnable',
                    description  = 'Set to 1 (default) to keep SYSREF capture enabled after MTS runs. Set to 0 to disable SYSREF capture',
                    offset       = 0x1101C,
                    bitSize      = 1,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'AdcTargetLatency',
                    description  = 'Sets the target relative latency. This is required to be set for multi-device alignment, or deterministic latency use-cases. It is not required to be set for single-device alignment.',
                    offset       = 0x11020,
                    bitSize      = 32,
                    mode         = 'RW',
                    base         = pr.Int, # s32
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DacTargetLatency',
                    description  = 'Sets the target relative latency. This is required to be set for multi-device alignment, or deterministic latency use-cases. It is not required to be set for single-device alignment.',
                    offset       = 0x11024,
                    bitSize      = 32,
                    mode         = 'RW',
                    base         = pr.Int, # s32
                ))

                class MtsStatus(pr.Device):
                    def __init__(self,**kwargs):
                        super().__init__(**kwargs)
                        #######################################################################################
                        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_MultiConverter_Sync_Config
                        #######################################################################################
                        self.addRemoteVariables(
                            name         = 'AdcLatency',
                            offset       = 0x11200,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                        self.addRemoteVariables(
                            name         = 'AdcOffset',
                            offset       = 0x11220,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                        #######################################################################################
                        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
                        #######################################################################################
                        self.addRemoteVariables(
                            name         = 'AdcDecimationFactor',
                            offset       = 0x11240,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                        #######################################################################################
                        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_MultiConverter_Sync_Config
                        #######################################################################################
                        self.addRemoteVariables(
                            name         = 'DacLatency',
                            offset       = 0x11210,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                        self.addRemoteVariables(
                            name         = 'DacOffset',
                            offset       = 0x11230,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                        #######################################################################################
                        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
                        #######################################################################################
                        self.addRemoteVariables(
                            name         = 'DacInterpolationFactor',
                            offset       = 0x11250,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 4,
                            base         = pr.Int, # s32
                            pollInterval = 1,
                        )

                # Adding the MTS device
                self.add(MtsStatus())

        # Adding the MTS device
        self.add(Mts())

        #######################################################################################
        # Read-only view of the RFDC clock distribution (Gen3 and DFE only):
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClkDistribution-Gen-3/DFE
        # PyRFdc serves the window 0x15000 to 0x157FC from one XRFdc_GetClkDistribution call per
        # read transaction. Nothing here is writable and nothing is polled: the view is
        # refreshed by a ReadAll or a read of the device.
        #######################################################################################
        class ClkDist(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)

                sourceType = {
                    0 : "ADC",
                    1 : "DAC",
                }

                distributedClock = {
                    0 : "None",
                    1 : "RX",
                    2 : "OutDiv",
                }

                rateNote = ('The driver reports the rate from the tile PLL FS register: after a Reset that followed a '
                            'PLL change it can lag the live rate, so BlockStatus.SampleRate is the live block rate. ')

                self.add(pr.RemoteVariable(
                    name         = 'Status',
                    description  = 'Result of the last XRFdc_GetClkDistribution call: Unsupported (IP below Gen3, every other ClkDist field reads 0), Ok, or GetFailed (the driver refused the call, every other ClkDist field reads 0). Read-only',
                    offset       = 0x15000,
                    bitSize      = 32,
                    mode         = 'RO',
                    enum         = {
                        0 : "Unsupported",
                        1 : "Ok",
                        2 : "GetFailed",
                    },
                ))

                self.add(pr.RemoteVariable(
                    name         = 'Count',
                    description  = 'Number of valid distributions: Distribution[d] is valid for d below Count and reads all 0 from Count on. Read-only',
                    offset       = 0x15004,
                    bitSize      = 32,
                    mode         = 'RO',
                ))

                class Distribution(pr.Device):
                    def __init__(self,**kwargs):
                        super().__init__(**kwargs)

                        self.add(pr.RemoteVariable(
                            name         = 'Valid',
                            description  = 'True when this entry holds a distribution (its index is below Count)',
                            offset       = 0x00,
                            bitSize      = 1,
                            mode         = 'RO',
                            base         = pr.Bool,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'SourceType',
                            description  = 'Converter type of the source tile of this distribution',
                            offset       = 0x04,
                            bitSize      = 32,
                            mode         = 'RO',
                            enum         = sourceType,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'SourceTileId',
                            description  = 'Tile number of the source tile of this distribution',
                            offset       = 0x08,
                            bitSize      = 32,
                            mode         = 'RO',
                        ))

                        self.addRemoteVariables(
                            name         = 'EdgeType',
                            description  = 'Converter type of the two edge tiles of this distribution (a single tile distribution reports its own tile twice)',
                            offset       = 0x0C,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 2,
                            stride       = 8,
                            enum         = sourceType,
                        )

                        self.addRemoteVariables(
                            name         = 'EdgeTileId',
                            description  = 'Tile number of the two edge tiles of this distribution (a single tile distribution reports its own tile twice)',
                            offset       = 0x10,
                            bitSize      = 32,
                            mode         = 'RO',
                            number       = 2,
                            stride       = 8,
                        )

                        self.add(pr.RemoteVariable(
                            name         = 'DistributedClock',
                            description  = 'Clock the source tile distributes: None, RX (the reference clock input) or OutDiv (the PLL output divider)',
                            offset       = 0x1C,
                            bitSize      = 32,
                            mode         = 'RO',
                            enum         = distributedClock,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'DistRefClkFreq',
                            description  = 'Reference clock frequency of the source tile in MHz',
                            offset       = 0x20,
                            bitSize      = 64,
                            mode         = 'RO',
                            base         = pr.Double,
                            disp         = '{:1.3f}',
                            units        = 'MHz',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'InfoSource',
                            description  = 'Distribution tile index of the source tile (DAC tile 3 is 0 and ADC tile 0 is 7 on a 4 ADC 4 DAC device)',
                            offset       = 0x28,
                            bitSize      = 8,
                            bitOffset    = 0,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'InfoUpperBound',
                            description  = 'Distribution tile index of the last tile of this distribution',
                            offset       = 0x28,
                            bitSize      = 8,
                            bitOffset    = 8,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'InfoLowerBound',
                            description  = 'Distribution tile index of the first tile of this distribution',
                            offset       = 0x28,
                            bitSize      = 8,
                            bitOffset    = 16,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'MaxDelay',
                            description  = 'Largest clock delay of the tiles of this distribution',
                            offset       = 0x2C,
                            bitSize      = 8,
                            bitOffset    = 0,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'MinDelay',
                            description  = 'Smallest clock delay of the tiles of this distribution',
                            offset       = 0x2C,
                            bitSize      = 8,
                            bitOffset    = 8,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'IsDelayBalanced',
                            description  = 'True when MaxDelay equals MinDelay',
                            offset       = 0x2C,
                            bitSize      = 1,
                            bitOffset    = 16,
                            mode         = 'RO',
                            base         = pr.Bool,
                        ))

                        self.addRemoteVariables(
                            name         = 'AdcSampleRate',
                            description  = rateNote + 'Sample rate of ADC tile t in this distribution in MSPS, 0 for a tile that is not a member',
                            offset       = 0x40,
                            bitSize      = 64,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 8,
                            base         = pr.Double,
                            disp         = '{:1.3f}',
                            units        = 'MSPS',
                        )

                        self.addRemoteVariables(
                            name         = 'DacSampleRate',
                            description  = rateNote + 'Sample rate of DAC tile t in this distribution in MSPS, 0 for a tile that is not a member',
                            offset       = 0x60,
                            bitSize      = 64,
                            mode         = 'RO',
                            number       = 4,
                            stride       = 8,
                            base         = pr.Double,
                            disp         = '{:1.3f}',
                            units        = 'MSPS',
                        )

                class TileClk(pr.Device):
                    def __init__(self,**kwargs):
                        super().__init__(**kwargs)

                        self.add(pr.RemoteVariable(
                            name         = 'Distribution',
                            description  = 'Index of the Distribution entry that holds this tile, 255 when no valid entry holds it (every other field then reads 0)',
                            offset       = 0x00,
                            bitSize      = 32,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'SourceType',
                            description  = 'Converter type of the tile that sources this tile clock',
                            offset       = 0x04,
                            bitSize      = 32,
                            mode         = 'RO',
                            enum         = sourceType,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'SourceTile',
                            description  = 'Tile number of the tile that sources this tile clock',
                            offset       = 0x08,
                            bitSize      = 32,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'PLLEnable',
                            description  = 'True when the tile runs its PLL',
                            offset       = 0x0C,
                            bitSize      = 1,
                            mode         = 'RO',
                            base         = pr.Bool,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'DivisionFactor',
                            description  = 'Output divider of the tile clock (1 while the PLL is enabled)',
                            offset       = 0x10,
                            bitSize      = 32,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'DistributedClock',
                            description  = 'Clock this tile distributes to its neighbours: None, RX (the reference clock input) or OutDiv (the PLL output divider)',
                            offset       = 0x14,
                            bitSize      = 32,
                            mode         = 'RO',
                            enum         = distributedClock,
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'Delay',
                            description  = 'Clock delay of this tile in the distribution',
                            offset       = 0x18,
                            bitSize      = 32,
                            mode         = 'RO',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'RefClkFreq',
                            description  = 'Reference clock frequency of this tile in MHz',
                            offset       = 0x20,
                            bitSize      = 64,
                            mode         = 'RO',
                            base         = pr.Double,
                            disp         = '{:1.3f}',
                            units        = 'MHz',
                        ))

                        self.add(pr.RemoteVariable(
                            name         = 'SampleRate',
                            description  = rateNote + 'Sample rate of this tile in MSPS',
                            offset       = 0x28,
                            bitSize      = 64,
                            mode         = 'RO',
                            base         = pr.Double,
                            disp         = '{:1.3f}',
                            units        = 'MSPS',
                        ))

                for d in range(8):
                    self.add(Distribution(
                        name        = f'Distribution[{d}]',
                        description = 'One clock distribution as XRFdc_GetClkDistribution reports it. Read-only',
                        offset      = 0x15100 + 0x80 * d,
                        expand      = False,
                    ))

                for t in range(4):
                    self.add(TileClk(
                        name        = f'AdcTileClk[{t}]',
                        description = f'Clocking of ADC tile {t} from the distribution that holds it. Read-only',
                        offset      = 0x15500 + 0x40 * t,
                        expand      = False,
                    ))

                for t in range(4):
                    self.add(TileClk(
                        name        = f'DacTileClk[{t}]',
                        description = f'Clocking of DAC tile {t} from the distribution that holds it. Read-only',
                        offset      = 0x15500 + 0x40 * (4 + t),
                        expand      = False,
                    ))

                #######################################################################################
                # Explicit change of the clock distribution (Gen3 and DFE only):
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetClkDistribution-Gen-3/DFE
                # The Set variables stage the settings the way XRFdc_Distribution_Settings holds
                # them; RefreshStaging loads them from one entry of the read view above. They are
                # plain staging words (nothing is written to a tile), the Preview variables show
                # what the staged settings would restart and whether PyRFdc would refuse them,
                # and the Last variables show what the last commit restarted. A Set is transient
                # and never re-applied, so none of the Set variables is part of the written
                # settings that Init and ApplyConfig write back.
                #######################################################################################
                self._stageVars = []

                def stage(var):
                    self._stageVars.append(var)
                    self.add(var)

                stage(pr.RemoteVariable(
                    name         = 'SetSourceType',
                    description  = 'Staged converter type of the source tile of the distribution to set',
                    offset       = 0x15800,
                    bitSize      = 32,
                    mode         = 'RW',
                    enum         = sourceType,
                ))

                stage(pr.RemoteVariable(
                    name         = 'SetSourceTileId',
                    description  = 'Staged tile number of the source tile of the distribution to set',
                    offset       = 0x15804,
                    bitSize      = 32,
                    mode         = 'RW',
                ))

                for i in range(2):
                    stage(pr.RemoteVariable(
                        name         = f'SetEdgeType[{i}]',
                        description  = 'Staged converter type of an edge tile of the distribution to set (a single tile distribution stages its own tile twice)',
                        offset       = 0x15808 + 8 * i,
                        bitSize      = 32,
                        mode         = 'RW',
                        enum         = sourceType,
                    ))
                    stage(pr.RemoteVariable(
                        name         = f'SetEdgeTileId[{i}]',
                        description  = 'Staged tile number of an edge tile of the distribution to set (a single tile distribution stages its own tile twice)',
                        offset       = 0x1580C + 8 * i,
                        bitSize      = 32,
                        mode         = 'RW',
                    ))

                stage(pr.RemoteVariable(
                    name         = 'SetDistributedClock',
                    description  = 'Staged clock the source tile distributes: None (single tile distribution), RX (the reference clock input) or OutDiv (the PLL output divider)',
                    offset       = 0x15818,
                    bitSize      = 32,
                    mode         = 'RW',
                    enum         = distributedClock,
                ))

                stage(pr.RemoteVariable(
                    name         = 'SetShutdownMode',
                    description  = 'Staged shutdown mode. Must stay 0: the commit always starts the tiles up to state 15 and a nonzero value is refused',
                    offset       = 0x1581C,
                    bitSize      = 32,
                    mode         = 'RW',
                    hidden       = True,
                ))

                stage(pr.RemoteVariable(
                    name         = 'SetDistRefClkFreq',
                    description  = 'Staged reference clock frequency of the source tile in MHz',
                    offset       = 0x15820,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    disp         = '{:1.3f}',
                    units        = 'MHz',
                ))

                for t in range(4):
                    stage(pr.RemoteVariable(
                        name         = f'SetAdcSampleRate[{t}]',
                        description  = f'Staged sample rate of ADC tile {t} in MSPS, used when the tile lies inside the staged span',
                        offset       = 0x15830 + 8 * t,
                        bitSize      = 64,
                        mode         = 'RW',
                        base         = pr.Double,
                        disp         = '{:1.3f}',
                        units        = 'MSPS',
                    ))

                for t in range(4):
                    stage(pr.RemoteVariable(
                        name         = f'SetDacSampleRate[{t}]',
                        description  = f'Staged sample rate of DAC tile {t} in MSPS, used when the tile lies inside the staged span',
                        offset       = 0x15850 + 8 * t,
                        bitSize      = 64,
                        mode         = 'RW',
                        base         = pr.Double,
                        disp         = '{:1.3f}',
                        units        = 'MSPS',
                    ))

                self.add(pr.RemoteVariable(
                    name         = 'PreviewStatus',
                    description  = 'What PyRFdc would do with the staged settings: Ok, or the first reason it would refuse the commit before touching a tile. Read-only',
                    offset       = 0x15908,
                    bitSize      = 32,
                    mode         = 'RO',
                    enum         = {
                        0 : "Ok",
                        1 : "Unsupported",
                        2 : "BadField",
                        3 : "ShutdownMode",
                        4 : "BadSpan",
                        5 : "TileDisabled",
                        6 : "BadRate",
                        7 : "Orphan",
                    },
                ))

                maskText = [
                    ('PreviewAdcAffected', 0x1590C, 'ADC tiles the staged settings would restart (bit t is ADC tile t)'),
                    ('PreviewDacAffected', 0x15910, 'DAC tiles the staged settings would restart (bit t is DAC tile t)'),
                    ('PreviewAdcSpan',     0x15914, 'ADC tiles of the staged distribution itself (bit t is ADC tile t)'),
                    ('PreviewDacSpan',     0x15918, 'DAC tiles of the staged distribution itself (bit t is DAC tile t)'),
                    ('LastAdcAffected',    0x1591C, 'ADC tiles the last commit restarted (bit t is ADC tile t), 0 after a refused commit, unchanged when a commit word never reached PyRFdc'),
                    ('LastDacAffected',    0x15920, 'DAC tiles the last commit restarted (bit t is DAC tile t), 0 after a refused commit, unchanged when a commit word never reached PyRFdc'),
                    ('LastAdcSpan',        0x15924, 'ADC tiles of the distribution the last commit set (bit t is ADC tile t), 0 after a refused commit'),
                    ('LastDacSpan',        0x15928, 'DAC tiles of the distribution the last commit set (bit t is DAC tile t), 0 after a refused commit'),
                ]
                for name, offset, text in maskText:
                    self.add(pr.RemoteVariable(
                        name         = name,
                        description  = text + '. Read-only',
                        offset       = offset,
                        bitSize      = 4,
                        mode         = 'RO',
                    ))

                self.add(pr.RemoteCommand(
                    name         = 'RefreshStaging',
                    description  = 'Load the Set variables from entry <index> of the Distribution view (the current distribution, ShutdownMode 0), then read them back. Refused for an index that is not a distribution',
                    offset       = 0x15900,
                    bitSize      = 32,
                    function     = lambda cmd, arg: self.parent._clkDistRefreshCmd(cmd, arg),
                    hidden       = True,
                ))

                self.add(pr.LocalCommand(
                    name         = 'SetClkDistribution',
                    description  = 'Commit the staged distribution as one sequence: restart the affected tiles (a busy affected tile refuses the Set, a parked one proceeds with a warning), wait for every enabled tile to reach state 15 with its PLL locked, write back the settings written in this session on the tiles the commit restarted (the Last masks, none after a refusal) and on the knocked tiles, and raise one error naming every tile that did not get there. It clears the MtsValid of every converter type it touches and drops the committed PLL record of every restarted tile. A Set is transient: Init and every Reset return the Vivado clocking. Init, Reset and StartUp never run it',
                    function     = lambda: self.parent._clkDistSetSequence(),
                ))

                self.add(pr.RemoteCommand(
                    name         = 'SetClkDistributionRaw',
                    description  = 'Bare commit of the staged distribution (XRFdc_SetClkDistribution with ShutdownMode 0): no state 15 wait, no write-back. PyRFdc still checks the whole request, refuses a busy affected tile and records one restart per tile of the distribution',
                    offset       = 0x15904,
                    bitSize      = 1,
                    function     = lambda cmd: self.parent._clkDistCommitCmd(cmd),
                    hidden       = True,
                ))

        # Adding the clock distribution view
        self.add(ClkDist(hidden=True))

        #######################################################################################
        # RFDC config ROM status: loaded at PyRFdc construction from the read-only
        # PYRFDC_CONFIG ROM in the bitstream. Read-only, no pollInterval: the status
        # is fixed once the process starts and never changes without a reboot.
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ConfigStatus',
            description  = ('RFDC config ROM validation status decided at PyRFdc construction. '
                            '8 is reserved and no longer produced: a driver bring-up failure on a valid ROM '
                            'now makes PyRFdc construction throw and roguetcpbridge leaves port 9002 unbound'),
            offset       = 0x14000,
            bitSize      = 32,
            mode         = 'RO',
            enum         = {
                0 : "NotLoaded",
                1 : "Ok",
                2 : "Missing",
                3 : "BadMagic",
                4 : "BadVersion",
                5 : "BadSize",
                6 : "IpVersionMismatch",
                7 : "TileEnableMismatch",
                8 : "DriverBringUpFailed",
                9 : "BadHash",
            },
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigFormatVersion',
            description  = 'PYRFDC_CONFIG ROM header format version',
            offset       = 0x14004,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigPayloadSize',
            description  = 'PYRFDC_CONFIG ROM header payload size in bytes (sizeof(XRFdc_Config) for the librfdc the ROM was built against)',
            offset       = 0x14008,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigIpVersion',
            description  = 'Expected RFDC IP version register value from the PYRFDC_CONFIG ROM header',
            offset       = 0x1400C,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigHash',
            description  = 'First 128 bits of the SHA-256 over the PYRFDC_CONFIG ROM payload (XRFdc_Config bytes), from the ROM header',
            offset       = 0x14010,
            bitSize      = 128,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigMagic',
            description  = 'PYRFDC_CONFIG ROM header magic word as read (0x52464443 is valid)',
            offset       = 0x14020,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigRomBytes',
            description  = 'Number of bytes roguetcpbridge read from the PYRFDC_CONFIG ROM before handing them to PyRFdc',
            offset       = 0x14024,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ConfigMessage',
            description  = 'Human-readable RFDC config ROM status message, naming the fix when not Ok',
            offset       = 0x14100,
            bitSize      = 8*256,
            mode         = 'RO',
            base         = pr.String,
        ))

        self.add(pr.LinkVariable(
            name         = 'ConfigPayloadSha256',
            description  = 'First 32 hex digits of the SHA-256 over the 1880 byte XRFdc_Config payload, matching header words 4 to 7 of PYRFDC_CONFIG.mem (not a hash of the .xci file)',
            mode         = 'RO',
            linkedGet    = lambda read: self.ConfigHash.get(read=read).to_bytes(16, 'little').hex(),
            dependencies = [self.ConfigHash],
        ))

        #######################################################################################
        #######################################################################################
        #######################################################################################

        self.add(pr.RemoteVariable(
            name         = 'MetalLogLevel',
            description  = 'Sets the bare metal driver logging level printing in the serial console',
            offset       = 0x12000,
            bitSize      = 1,
            mode         = 'RW',
            enum         = {
                0 : "METAL_LOG_ERROR",
                1 : "METAL_LOG_DEBUG",
            },
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'IgnoreMetalError',
            description  = 'Used to bypass the bare metal driver error returns (debugging only). While it is set no write or commit is recorded for re-apply after a restart, because a driver error is then not reported',
            offset       = 0x12004,
            bitSize      = 1,
            mode         = 'RW',
            base         = pr.Bool,
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'Scratchpad',
            description  = 'Test register (no impact to RFDC module)',
            offset       = 0x12008,
            bitSize      = 32,
            mode         = 'RW',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'DoubleTestReg',
            description  = 'Test register (no impact to RFDC module)',
            offset       = 0x13000,
            bitSize      = 64,
            mode         = 'RW',
            base         = pr.Double,
            disp         = '{:1.3f}',
            hidden       = True,
        ))

        #######################################################################################
        #######################################################################################
        #######################################################################################

        for i in range(4):
            if self.enAdcTile[i]:
                self.add(rfsoc_utility.RfdcTile(
                    name       = f'AdcTile[{i}]',
                    isAdc      = True,
                    gen3       = gen3,
                    offset     = (0x0000+0x2000*i),
                    expand     = False,
                    enableDeps = [self.CheckAdcTileEnabled[i]],
                ))

        for i in range(4):
            if self.enDacTile[i]:
                self.add(rfsoc_utility.RfdcTile(
                    name       = f'DacTile[{i}]',
                    isAdc      = False,
                    gen3       = gen3,
                    offset     = (0x8000+0x2000*i),
                    expand     = False,
                    enableDeps = [self.CheckDacTileEnabled[i]],
                ))

    #######################################################################################
    # Root.start applies the Root timeout to its children before it starts the
    # devices, so this runs after it: a Root timeout below the floor is raised
    # for this subtree only. A longer application timeout is never lowered, and
    # the Root, its parent and the sibling devices are not touched.
    #######################################################################################
    def _start(self):
        super()._start()
        if self.root._timeout < self.TIMEOUT_FLOOR_S:
            self._setTimeout(self.TIMEOUT_FLOOR_S)

    def _enabledAdcTiles(self):
        return [i for i in range(4) if self.enAdcTile[i] and (self.CheckAdcTileEnabled[i].get() != 0)]

    def _enabledDacTiles(self):
        return [i for i in range(4) if self.enDacTile[i] and (self.CheckDacTileEnabled[i].get() != 0)]

    def _refreshAllResetRecords(self):
        for i in self._enabledAdcTiles():
            self.AdcTile[i]._refreshResetRecord()
        for i in self._enabledDacTiles():
            self.DacTile[i]._refreshResetRecord()

    #######################################################################################
    # Restart sequence: the raw restart of the commanded tile, a wait until every
    # enabled tile is at state 15, the write-back of the settings the operator wrote
    # in this process, and a read of the commanded tile's mirrors. The sequence lock
    # and the poll pause are held for all of it. A tile that misses the wait is
    # reported and is never restarted again.
    #######################################################################################
    @contextlib.contextmanager
    def _sequence(self):
        # Lock first, then pause the polling: a poll batch already in flight never
        # needs the lock, so the two cannot deadlock
        with self._seqLock, self.root.pollBlock():
            yield

    def _tileDev(self, isAdc, i):
        return self.AdcTile[i] if isAdc else self.DacTile[i]

    def _tileLabel(self, isAdc, i):
        return f'{"ADC" if isAdc else "DAC"} tile {i}'

    def _allEnabledTiles(self):
        return [(True, i) for i in self._enabledAdcTiles()] + [(False, i) for i in self._enabledDacTiles()]

    # Reporting order: ADC tile 0 to 3, then DAC tile 0 to 3
    @staticmethod
    def _tileOrder(key):
        return (not key[0], key[1])

    @staticmethod
    def _firstLine(e):
        text = str(e)
        return text.splitlines()[0] if text else type(e).__name__

    # Runs body, then cleanup. The command exception carries the PyRFdc restart text whose
    # DiagLines Init's error reports, so a failed cleanup after a failed
    # body is logged and never raised in its place. When the body succeeds a failed cleanup
    # is the only error and is raised.
    def _runWithCleanup(self, body, cleanup, what):
        try:
            result = body()
        except BaseException:
            try:
                cleanup()
            except Exception as e:
                self._log.warning('%s: %s failed after the command failed: %s', self.path, what, e)
            raise
        cleanup()
        return result

    # A tile is ready at state 15 with the restart clear and, when it runs from
    # its internal PLL, with the PLL locked. A tile on an external clock has no
    # lock term. A read that fails counts as not ready for this pass.
    def _tileReady(self, tile):
        try:
            if int(tile.CurrentState.get(read=True)) != 15:
                return False
            if int(tile.RestartStatus.get(read=True)) != 0:
                return False
            if int(tile.PllStatus.ClockSource.get(read=True)) == 1:
                return bool(tile.PllStatus.PllLocked.get(read=True))
            return True
        except Exception:
            return False

    #######################################################################################
    # Same text and field names as PyRFdc::DiagLine. op is Reset or StartUp, call is
    # one token.
    #######################################################################################
    def _diagLine(self, op, isAdc, i, call):
        tile = self._tileDev(isAdc, i)
        pll = tile.PllStatus
        kind = 'ADC' if isAdc else 'DAC'
        try:
            state = int(tile.CurrentState.get(read=True))
            present = int(pll.ClockPresent.get(read=True))
            supply = int(pll.SupplyStable.get(read=True))
            powered = int(pll.PoweredUp.get(read=True))
            locked = int(pll.PllLocked.get(read=True))
            clkDet = str(int(pll.ClockDetector.get(read=True))) if hasattr(pll, 'ClockDetector') else 'NA'
            clkSrc = {0: 'External', 1: 'InternalPLL'}.get(int(pll.ClockSource.get(read=True)), 'Unknown')
        except Exception as e:
            return f'{op} {kind} tile {i}: {call} failed; status read error: {self._firstLine(e)}'
        return ('%s %s tile %d: %s failed; CurrentState=%d ClockPresent=%d SupplyUp=%d PowerUp=%d '
                'PllLocked=%d ClkDet=%s ClkSrc=%s') % (
            op, kind, i, call, state, present, supply, powered, locked, clkDet, clkSrc)

    #######################################################################################
    # One shared deadline over every enabled ADC and DAC tile. Returns the diagnostic line
    # of each tile that is still not ready, keyed (isAdc, index). The last pass runs
    # at or after the deadline, so a tile that reaches state 15 within the last poll
    # interval passes. Never writes and never restarts.
    #######################################################################################
    def _gate(self, op):
        start = time.monotonic()
        deadline = start + self.GATE_TIMEOUT_S
        pending = self._allEnabledTiles()
        settle = {}
        while pending:
            still = []
            for key in pending:
                if self._tileReady(self._tileDev(*key)):
                    settle[self._tileLabel(*key)] = round(time.monotonic() - start, 3)
                else:
                    still.append(key)
            pending = still
            if not pending:
                break
            now = time.monotonic()
            if now >= deadline:
                break
            time.sleep(min(self.GATE_POLL_S, deadline - now))
        misses = {key: self._diagLine(op, key[0], key[1], 'StateGate') for key in sorted(pending, key=self._tileOrder)}
        self._lastGate = {
            'settleS': settle,
            'missed':  [self._tileLabel(*key) for key in misses],
            'elapsedS': round(time.monotonic() - start, 3),
        }
        return misses

    #######################################################################################
    # Writes back, on the tiles in targets, the settings the operator wrote in this
    # process, in APPLY_ORDER. DAC tiles first, then ADC tiles. Everything is written
    # through pr.RemoteVariable.set, which bypasses the write record, so the write-back
    # never marks a setting as written.
    #   - A variable is written with the value recorded at set() time. A name that
    #     several fields share one word with (CoarseDelay, ThresholdClrMode) is written
    #     once with every field current (see _applyWord).
    #   - A tile FIFO word is written from whichever of the tile record and the Rfdc
    #     word of its converter type was written last, and the FIFO words are written
    #     back in the order they were written (see _applyFifoWords).
    #   - A QMC or Mixer group is written only from the snapshot that recordCommit took
    #     when the operator's commit succeeded, and committed with RemoteCommand.set. A
    #     value that was set and never committed is not re-applied, and the staging
    #     contents are never used to build a re-apply.
    #   - A group that was never committed gets its default (see _groupDefaults) after
    #     one RefreshStaging per tile, which reads the restart-restored hardware values
    #     back into staging. A tile whose refresh fails gets no default.
    #   - A setting in SURVIVOR_DEFAULTS that was never written is read back and written
    #     to its default only when the hardware reads differently.
    #   - A recorded setting or group whose device is disabled when this runs is skipped
    #     with a note in _lastApply and a logged warning, not reported as a tile failure.
    #     A committed DAC Mixer whose block reads DataPathMode 4 (full bandwidth bypass
    #     refuses a mixer commit) is skipped the same way.
    # Returns a list of ((isAdc, index), text) errors.
    #######################################################################################
    def _groupDefaults(self):
        return {'QMC': self._applyQmcDefault, 'Mixer': self._applyMixerDefault}

    def _needsRefresh(self, blocks):
        defaults = self._groupDefaults()
        for _, scope, name in self.APPLY_ORDER:
            if scope != 'group' or name not in defaults:
                continue
            for j in range(4):
                dev = getattr(blocks[j], name, None)
                if dev is not None and dev._commitSnapshot is None and dev.enable.value() is True:
                    return True
        return False

    # What the last write-back did per tile and step, kept in _lastApply. A text that starts
    # with 'skipped:' also goes to the log as a warning, because a skipped setting was
    # written by the operator and is not in the hardware; quiet=True keeps a skip that
    # lost nothing out of the log.
    def _note(self, label, step, text, quiet=False):
        self._lastApply.setdefault(label, {})[step] = text
        if text.startswith('skipped:') and not quiet:
            self._log.warning('%s %s: %s', label, step, text)

    # The committed PLL of every target tile, DAC tiles first (a DAC tile distributes the
    # clock to the ADC tiles that run from it, so its restart comes before theirs). A PLL
    # commit restarts the tile (a DynamicPLLConfig), so it is written only when the
    # hardware differs from the committed snapshot: the clock source exactly, the
    # reference clock within 1e-3 MHz and the sample rate within 1e-3 MSPS. The sample
    # rate is the one the driver reports for the first enabled block, not
    # PllStatus.SampleRate, because that register can stay stale after a half rate
    # commit followed by a restart. The three fields are written from the snapshot and
    # then the commit word, which PyRFdc runs as a prechecked restart, and then a
    # StartUp of the same tile (see _pllStartUp). Returns True when any commit was
    # attempted, so the caller waits for every tile again.
    def _applyPll(self, targets, errors):
        attempted = False
        for key in sorted(targets, key=lambda k: (k[0], k[1])):
            isAdc, i = key
            tile = self._tileDev(isAdc, i)
            label = self._tileLabel(isAdc, i)
            pll = tile.PllConfig
            snap = pll._commitSnapshot
            if snap is None:
                continue
            blocks = tile.AdcBlock if isAdc else tile.DacBlock
            try:
                rate = None
                for j in range(4):
                    if blocks[j].enable.value() is True:
                        rate = float(blocks[j].BlockStatus.SampleRate.get(read=True)) * 1000.0
                        break
                if rate is None:
                    self._note(label, 'pll', 'skipped: no enabled block')
                    continue
                same = (int(tile.PllStatus.ClockSource.get(read=True)) == int(snap['ClockSource'])
                        and abs(float(tile.PllStatus.RefClkFreq.get(read=True)) - float(snap['RefClkFreq'])) <= 1e-3
                        and abs(rate - float(snap['SampleRate'])) <= 1e-3)
                if same:
                    self._note(label, 'pll', 'skipped: PLL already at the committed value', quiet=True)
                    continue
                attempted = True
                self._writeFields(pll, snap)
                pll.PllConfigUpdate.set(1)
                self._pllStartUp(tile)
                self._note(label, 'pll', 'committed')
            except Exception as e:
                errors.append((key, f'ApplyConfig {label} PllConfig: {self._firstLine(e)}'))
        return attempted

    # The StartUp that follows every successful PLL commit: a prechecked restart of the
    # tile from state 1 to state 15, settings kept. The commit is a live reprogram that
    # restarts the tile only from state 6, so the supply and PLL trim states 3 to 5 do
    # not run again and RestartState keeps the window the commit left (0x060F). On the
    # board a tile in that condition stalls at state 7 (PowerUp 0, PllLocked 0) when a
    # neighbour's restart knocks it, because the automatic restart enters at state 6, and
    # only a public ResetAllDac recovers it. Rewriting the window to 0x010F does not
    # help. One full pass through states 1 to 15 clears the condition, and every later
    # knock then passes. Shutdown before the StartUp is not needed: the same sequence
    # without it holds through a DAC tile restart on a tile with a half rate PLL. The
    # tile is restarted again, so its ResetRecord sequence advances by one (command 2)
    # on top of the commit, which records nothing itself. Called with the sequence lock
    # and the poll pause held. A commit that failed raises before this runs.
    def _pllStartUp(self, tile):
        tile.StartUpRaw()

    # Operator PllConfigUpdate: the commit and its StartUp (see _pllStartUp) as one
    # sequence under the lock and the poll pause, then the wait for every enabled tile and
    # the write-back of the written settings of the commanded tile and of every tile the
    # restart knocked. A commit on DAC tile 0 takes the same path: the tile that distributes
    # the clock restarts the others, which the wait and the knocked-tile write-back cover.
    # The snapshot is stored by recordCommit as soon as the commit word succeeded, so the
    # write-back finds the hardware equal to it and does not commit again.
    def _pllCommitSequence(self, tile, pll, cmd):
        def commit():
            rfsoc_utility.recordCommit(pll, cmd)
            self._pllStartUp(tile)
        self._runSequence(f'{pll.path}.PllConfigUpdate', 'StartUp', [commit], [self._tileKey(tile)], apply=True)

    # After a PLL commit the restarted tiles may have knocked others: wait for every
    # enabled tile again. A tile that misses leaves targets and joins misses (the dict
    # the caller reports), and every tile whose ResetCount changed since the start of the
    # command and that passed joins targets. Both are updated in place.
    def _regate(self, op, targets, before, misses, errors):
        first = dict(self._lastGate)
        late = self._gate(op)
        first['regate'] = dict(self._lastGate)
        self._lastGate = first
        for key in sorted(late, key=self._tileOrder):
            if key in targets:
                targets.remove(key)
            misses.setdefault(key, late[key])
        if before is None:
            return
        added = []
        knocked = self._knockedTiles(before)
        for key in knocked:
            if key not in late and key not in targets:
                targets.append(key)
                added.append(key)
        self._lastGate['knocked'] = [self._tileLabel(*key) for key in knocked]
        if added:
            errors.extend((None, text) for text in self._refreshEnableChain(added))

    # Writes back, in place on targets and misses (see _regate), the written settings of
    # every target tile in APPLY_ORDER. before is the ResetCount snapshot taken at the
    # start of the command, misses the gate misses already reported by the caller.
    def _applyConfig(self, targets, before=None, op='ApplyConfig', misses=None):
        errors = []
        self._lastApply = {}
        if misses is None:
            misses = {}
        if self._applyPll(targets, errors):
            self._regate(op, targets, before, misses, errors)
        for key in sorted(targets, key=lambda k: (k[0], k[1])):
            errors.extend(self._applyTile(key))
        return errors

    def _applyTile(self, key):
        errors = []
        isAdc, i = key
        tile = self._tileDev(isAdc, i)
        label = self._tileLabel(isAdc, i)
        conv = 'ADC' if isAdc else 'DAC'
        blocks = tile.AdcBlock if isAdc else tile.DacBlock
        defaults = self._groupDefaults()
        refreshed = False
        if self._needsRefresh(blocks):
            try:
                tile.RefreshStaging()
                refreshed = True
            except Exception as e:
                errors.append((key, f'ApplyConfig {label}: RefreshStaging failed: {self._firstLine(e)}'))
        for step, scope, name in self.APPLY_ORDER:
            if step == 'pll':
                continue
            if scope == 'tile':
                if name in self.FIFO_WORDS:
                    if name == self.FIFO_WORDS[0]:
                        self._applyFifoWords(key, label, tile, isAdc, conv, errors)
                else:
                    self._applyVariables(key, f'ApplyConfig {label}', tile, conv, name, errors)
                continue
            for j in range(4):
                block = blocks[j]
                if scope == 'group':
                    dev = getattr(block, name, None)
                    if dev is None:
                        continue
                    if dev._commitSnapshot is not None:
                        if dev.enable.value() is not True:
                            self._note(label, f'ApplyConfig {label} block {j} {name}', 'skipped: device disabled')
                        else:
                            text = self._applyGroup(label, j, dev)
                            if text:
                                errors.append((key, text))
                    elif refreshed and name in defaults and dev.enable.value() is True:
                        text = defaults[name](tile, label, j, block, isAdc)
                        if text:
                            errors.append((key, text))
                elif scope == 'word':
                    self._applyWord(key, label, j, block, conv, name, errors)
                else:
                    self._applyVariables(key, f'ApplyConfig {label} block {j}', block, conv, name, errors)
        return errors

    # The first device above var, up to the tile, that is disabled (a disabled device
    # ignores a write without raising), or None
    @staticmethod
    def _disabledAbove(var):
        dev = var.parent
        while dev is not None and not isinstance(dev, rfsoc_utility.RfdcTile):
            if dev.enable.value() is not True:
                return dev
            dev = dev.parent
        return None

    # The variable of a dotted name relative to dev, or every element of an array when the
    # name has no index; an empty list when the device does not have it
    @staticmethod
    def _findAll(dev, name):
        parts = name.split('.')
        for part in parts[:-1]:
            dev = dev.devices.get(part)
            if dev is None:
                return []
        var = dev.variables.get(parts[-1])
        if var is not None:
            return [var]
        pat = re.compile(re.escape(parts[-1]) + r'\[(\d+)\]')
        found = sorted((int(m.group(1)), v) for k, v in dev.variables.items() for m in [pat.fullmatch(k)] if m)
        return [v for _, v in found]

    @staticmethod
    def _differs(hw, want):
        if isinstance(want, float):
            return abs(float(hw) - want) > 1e-6
        return int(hw) != int(want)

    # One tracked variable (or every element of an array): the recorded value when the
    # operator wrote it, else the survivor default when the hardware reads differently.
    # A read-only variable of the converter type is left alone.
    def _applyVariables(self, key, where, dev, conv, name, errors):
        prefix = name.rsplit('.', 1)[0] + '.' if '.' in name else ''
        for var in self._findAll(dev, name):
            if not isinstance(var, rfsoc_utility.ConfigVariable):
                continue
            rel = prefix + var.name
            off = self._disabledAbove(var)
            try:
                if var.isRecorded():
                    if off is not None:
                        kind = 'block' if isinstance(off, rfsoc_utility.RfdcBlock) else 'device'
                        self._note(self._tileLabel(*key), f'{where} {rel}', f'skipped: {kind} disabled')
                        continue
                    pr.RemoteVariable.set(var, var.replayValue(), verify=False)
                    continue
                want = self.SURVIVOR_DEFAULTS.get((conv, rel))
                if want is None or off is not None:
                    continue
                if self._differs(var.get(read=True), want):
                    pr.RemoteVariable.set(var, want, verify=False)
            except Exception as e:
                errors.append((key, f'{where} {rel}: {self._firstLine(e)}'))

    # Fields that share one hardware word (CoarseDelay and EventSource, the two
    # ThresholdClrMode fields) are written together, once, with every field current. A
    # word that has a read (the CoarseDelay handler) is read from the hardware first, so
    # the field the operator did not write keeps its hardware value; the WO
    # ThresholdClrMode word has no read, so its unwritten field keeps its shadow. The
    # recorded fields, and a survivor default that differs from the hardware, are set with
    # write disabled and the word is written by the last of them. The CoarseDelay update
    # event follows only when the word's event source is Slice or Tile.
    def _applyWord(self, key, label, j, block, conv, names, errors):
        fields = {}
        for name in names:
            found = self._findAll(block, name)
            if len(found) != 1 or not isinstance(found[0], rfsoc_utility.ConfigVariable):
                return
            fields[name] = found[0]
        first = fields[names[0]]
        off = self._disabledAbove(first)
        where = f'ApplyConfig {label} block {j}'
        recorded = any(f.isRecorded() for f in fields.values())
        if off is not None:
            if recorded:
                kind = 'block' if isinstance(off, rfsoc_utility.RfdcBlock) else 'device'
                self._note(label, f'{where} {names[0]}', f'skipped: {kind} disabled')
            return
        try:
            readable = first.mode != 'WO'
            hw = {name: f.get(read=True) for name, f in fields.items()} if readable else {}
            values = {}
            for name, f in fields.items():
                if f.isRecorded():
                    values[name] = f.replayValue()
                elif readable:
                    want = self.SURVIVOR_DEFAULTS.get((conv, name))
                    if want is not None and self._differs(hw[name], want):
                        values[name] = want
            if not values:
                return
            order = list(values)
            for name in order[:-1]:
                pr.RemoteVariable.set(fields[name], values[name], write=False, verify=False)
            pr.RemoteVariable.set(fields[order[-1]], values[order[-1]], verify=False)
            if names[0] == 'CoarseDelay.CoarseDelay' and int(values.get(names[1], hw.get(names[1], 0))) in (1, 2):
                block.CoarseDelay.UpdateEvent.set(1)
        except Exception as e:
            errors.append((key, f'{where} {names[0]}: {self._firstLine(e)}'))

    # The three tile FIFO words (FIFO_WORDS), written once per tile. Each word with a record
    # takes the later of the tile variable and the Rfdc word of its converter type
    # (SetupFIFOAllAdc and the like), written through the tile variable so a tile that is
    # not a target is untouched. The recorded words are written in the order they were
    # recorded, because the words overlap (SetupFIFOBoth drives both channels), so the
    # newest write wins as it did before the restart. With no record, a FIFO that the
    # hardware reports disabled is enabled again, unless a SetupFIFOBoth record owns both
    # channels. SetupFIFOBoth has no default of its own, and neither has the observation
    # channel (see SURVIVOR_DEFAULTS). The defaults come first, then the recorded words.
    def _applyFifoWords(self, key, label, tile, isAdc, conv, errors):
        recorded = []
        for name in self.FIFO_WORDS:
            var = tile.variables.get(name)
            if not isinstance(var, rfsoc_utility.ConfigVariable):
                continue
            try:
                records = [v for v in (var, self.variables.get(self.FIFO_GLOBAL.get((name, isAdc), ''))) if v is not None and v.isRecorded()]
                if records:
                    recorded.append((max(records, key=lambda v: v.recordSeq()), var, name))
                    continue
                status = self.FIFO_STATUS.get(name)
                want = self.SURVIVOR_DEFAULTS.get((conv, name))
                if status is None or not want:
                    continue
                both = [tile.variables.get('SetupFIFOBoth'), self.variables.get(self.FIFO_GLOBAL.get(('SetupFIFOBoth', isAdc), ''))]
                if any(v is not None and v.isRecorded() for v in both):
                    continue
                if bool(tile.variables[status].get(read=True)) != bool(want[status]):
                    pr.RemoteVariable.set(var, self.FIFO_ENABLE, verify=False)
            except Exception as e:
                errors.append((key, f'ApplyConfig {label} {name}: {self._firstLine(e)}'))
        for latest, var, name in sorted(recorded, key=lambda r: r[0].recordSeq()):
            try:
                pr.RemoteVariable.set(var, latest.replayValue(), verify=False)
            except Exception as e:
                errors.append((key, f'ApplyConfig {label} {name}: {self._firstLine(e)}'))

    # Writes the fields in values (name -> value) of a group, then returns. Fields that
    # share a word (same offset) are set with write disabled and the word is written
    # once, by its last field, so the staging word PyRFdc commits holds every field.
    @staticmethod
    def _writeFields(group, values):
        words = {}
        for name in values:
            words.setdefault(group.variables[name].offset, []).append(name)
        for names in words.values():
            for name in names[:-1]:
                pr.RemoteVariable.set(group.variables[name], values[name], write=False, verify=False)
            last = names[-1]
            pr.RemoteVariable.set(group.variables[last], values[last], verify=False)

    # Re-applies a committed group: every field from its snapshot, then the commit word.
    # RemoteCommand.set does not run the command function, so the snapshot is not
    # replaced. A Mixer on a DAC block that reads DataPathMode 4 (full bandwidth bypass)
    # is skipped with a note and a warning, because PyRFdc refuses that commit; the
    # snapshot is kept for a later mode change. Returns the error text, or None.
    def _applyGroup(self, label, j, group):
        try:
            dataPath = getattr(group.parent, 'DataPathMode', None) if group.name == 'Mixer' else None
            if dataPath is not None and int(dataPath.get(read=True)) == 4:
                self._note(label, f'ApplyConfig {label} block {j} {group.name}',
                           'skipped: DAC block in full bandwidth bypass (DataPathMode 4) refuses a mixer commit')
                return None
            self._writeFields(group, group._commitSnapshot)
            group.UpdateEvent.set(1)
        except Exception as e:
            return f'ApplyConfig {label} block {j} {group.name}: {self._firstLine(e)}'
        return None

    # The Vivado value of a mixer that was never committed: the restart-restored event
    # source, coarse frequency, mode, scale and type, read from the staging that
    # RefreshStaging just loaded, with Freq from the config ROM NCO and PhaseOffset 0.
    # A DAC block in full bandwidth bypass (DataPathMode 4) refuses a mixer commit and a
    # staged MixerType 3 (disabled) has nothing to restore. Returns the error text, or None.
    def _applyMixerDefault(self, tile, label, j, block, isAdc):
        mixer = block.Mixer
        try:
            dataPath = getattr(block, 'DataPathMode', None)
            if dataPath is not None and int(dataPath.get(read=True)) == 4:
                return None
            values = {'Freq': float(tile.ConfigNCOFreq[j].get(read=True)), 'PhaseOffset': 0.0}
            for name in ('EventSource', 'CoarseMixFreq', 'MixerMode', 'FineMixerScale', 'MixerType'):
                values[name] = int(mixer.variables[name].get(read=True))
            if values['MixerType'] == 3:
                return None
            self._writeFields(mixer, values)
            mixer.UpdateEvent.set(1)
        except Exception as e:
            return f'ApplyConfig {label} block {j} Mixer: {self._firstLine(e)}'
        return None

    # The power-up value of a QMC that was never committed. Staging was just refreshed, so
    # it holds the hardware: when the five fields already equal QMC_POWERUP_DEFAULT nothing
    # is written. Otherwise the five fields are written and committed with the staged event
    # source kept, because a commit programs the staged event source and the event source
    # is never rewritten here. A high-speed ADC rejects a QMC commit on an Immediate or
    # Slice event source, so that case is reported instead. Returns the error text, or None.
    def _applyQmcDefault(self, tile, label, j, block, isAdc):
        qmc = block.QMC
        try:
            staged = {name: qmc.variables[name].get(read=True) for name in self.QMC_POWERUP_DEFAULT}
            eventSource = int(qmc.EventSource.get(read=True))
            same = True
            for name, want in self.QMC_POWERUP_DEFAULT.items():
                if isinstance(want, float):
                    same = same and abs(float(staged[name]) - want) <= 1e-9
                else:
                    same = same and int(staged[name]) == int(want)
            if same:
                return None
            if isAdc and bool(tile.IsHighSpeedADC.get(read=True)) and eventSource in (0, 1):
                return f'ApplyConfig {label} block {j} QMC: power-up default not restorable with event source {eventSource} on a high-speed ADC'
            self._writeFields(qmc, self.QMC_POWERUP_DEFAULT)
            qmc.UpdateEvent.set(1)
        except Exception as e:
            return f'ApplyConfig {label} block {j} QMC: {self._firstLine(e)}'
        return None

    # Public ApplyConfig: gate every enabled tile, then the same write-back the full
    # sequences use on every tile that passed, then a read of the whole subtree
    def _applyConfigCommand(self):
        self._runSequence(f'{self.path}.ApplyConfig', 'ApplyConfig', [], self._allEnabledTiles(), apply=True, mirrorAll=True)

    # Reads the commanded tiles back after the write-back, so the GUI shows the hardware
    def _refreshMirrors(self, targets):
        errors = []
        for key in sorted(targets, key=self._tileOrder):
            tile = self._tileDev(*key)
            try:
                tile.readBlocks(recurse=True)
                tile.waitBlocks(recurse=True)
            except Exception as e:
                errors.append((key, f'Mirror refresh {self._tileLabel(*key)}: {self._firstLine(e)}'))
        return errors

    # Reads the whole Rfdc subtree, RW and RO alike. Only run after the write-back, so
    # the shadows show the hardware the operator ends up with
    def _refreshAllMirrors(self):
        try:
            self.readBlocks(recurse=True)
            self.waitBlocks(recurse=True)
        except Exception as e:
            return [(None, f'Mirror refresh {self.path}: {self._firstLine(e)}')]
        return []

    #######################################################################################
    # Knock-on detection. Restarting one tile can restart others (the DAC tile that
    # distributes the clock restarts the tiles that run from it), which reloads their
    # registers. ResetCount counts those automatic restarts and saturates at 255. It is
    # compared with inequality, because whether a software restart clears it is not
    # settled, and a tile that reads 255 can never show a change, so it counts as
    # knocked. A tile whose count cannot be read counts as knocked as well. Returned in
    # ADC 0 to 3, DAC 0 to 3 order.
    #######################################################################################
    def _resetCountSnapshot(self):
        counts = {}
        for key in self._allEnabledTiles():
            try:
                counts[key] = int(self._tileDev(*key).ResetCount.get(read=True))
            except Exception:
                counts[key] = None
        return counts

    def _knockedTiles(self, before):
        now = self._resetCountSnapshot()
        knocked = []
        for key in sorted(now, key=self._tileOrder):
            was, cur = before.get(key), now[key]
            if was is None or cur is None or was != cur or cur == 255:
                knocked.append(key)
        return knocked

    #######################################################################################
    # Enable chain. A device whose enable is not True ignores a write without raising,
    # and the enables arrive in three hops through the root update thread: the tile
    # enable follows CheckAdc/DacTileEnabled, a block enable follows IsADC/DACBlockEnabled
    # and the mixer enable follows BlockStatus.SampleRate through IsMixerEnabled. Each
    # hop is read, then root.waitOnUpdate() lets the listeners run before the next hop
    # is read. Only read-only variables are read here. Every read error is returned as
    # text and never stops the refresh.
    #######################################################################################
    def _readEnableVar(self, var, errors):
        try:
            var.get()
        except Exception as e:
            errors.append(f'Enable refresh {var.path}: {self._firstLine(e)}')

    def _refreshTileEnables(self):
        errors = []
        for i in range(4):
            if self.enAdcTile[i]:
                self._readEnableVar(self.CheckAdcTileEnabled[i], errors)
            if self.enDacTile[i]:
                self._readEnableVar(self.CheckDacTileEnabled[i], errors)
        self.root.waitOnUpdate()
        return errors

    def _refreshEnableChain(self, targets):
        errors = self._refreshTileEnables()
        ordered = sorted(targets, key=self._tileOrder)

        for isAdc, i in ordered:
            tile = self._tileDev(isAdc, i)
            chain = tile.IsADCBlockEnabled if isAdc else tile.IsDACBlockEnabled
            for j in range(4):
                self._readEnableVar(chain[j], errors)
        self.root.waitOnUpdate()

        for isAdc, i in ordered:
            tile = self._tileDev(isAdc, i)
            blocks = tile.AdcBlock if isAdc else tile.DacBlock
            for j in range(4):
                if blocks[j].enable.value() is not True:
                    continue
                self._readEnableVar(blocks[j].BlockStatus.SampleRate, errors)
                self._readEnableVar(blocks[j].IsMixerEnabled, errors)
        self.root.waitOnUpdate()
        return errors

    #######################################################################################
    # rawSteps are zero-argument callables that issue the raw restarts. commanded
    # are the (isAdc, index) tiles those restarts target. After the lock is released
    # one RuntimeError reports every failure: the failed tiles in ADC 0 to 3, DAC 0
    # to 3 order, then the diagnostic line of each tile that missed the wait, the full text
    # of each failed raw command, each enable refresh error, and each write-back or
    # mirror refresh error. The tile enables are read before the restarts so the wait
    # reads enabled tile devices, and the block and mixer enables of the tiles that
    # passed the wait are read before the write-back. When apply is set, the write-back
    # and the mirror refresh cover the commanded tiles plus every knocked tile that
    # passed the wait; the knocked tiles are listed in _lastGate['knocked']. With mirrorAll the mirror refresh
    # reads the whole Rfdc subtree instead of the commanded tiles.
    #######################################################################################
    def _runSequence(self, name, op, rawSteps, commanded, apply, mirrorAll=False):
        rawErrors = []
        with self._sequence():
            enableErrors = self._refreshTileEnables()
            enabled = self._allEnabledTiles()
            before = {key: self._tileDev(*key).FailureCount.value() for key in enabled}
            countsBefore = self._resetCountSnapshot()
            for step in rawSteps:
                try:
                    step()
                except Exception as e:
                    rawErrors.append(str(e))
            misses = self._gate(op)
            knocked = self._knockedTiles(countsBefore)
            self._lastGate['knocked'] = [self._tileLabel(*key) for key in knocked]
            targets = [key for key in commanded if key not in misses]
            if apply:
                targets.extend(key for key in knocked if key not in misses and key not in targets)
            enableErrors.extend(self._refreshEnableChain(targets))
            applyErrors = self._applyConfig(targets, countsBefore, op, misses) if apply else []
            mirrorErrors = self._refreshAllMirrors() if mirrorAll else self._refreshMirrors(targets)

        failed = set(misses)
        failed.update(key for key in enabled if self._tileDev(*key).FailureCount.value() > before[key])
        failed.update(key for key, _ in applyErrors if key is not None)
        if not (failed or rawErrors or enableErrors or applyErrors or mirrorErrors):
            return

        labels = [self._tileLabel(*key) for key in sorted(failed, key=self._tileOrder)]
        lines = [f'{name} failed on: ' + ', '.join(labels) if labels else f'{name} failed']
        lines.extend(misses[key] for key in sorted(misses, key=self._tileOrder))
        lines.extend(rawErrors)
        lines.extend(enableErrors)
        lines.extend(text for _, text in applyErrors)
        lines.extend(text for _, text in mirrorErrors)
        raise RuntimeError('\n'.join(lines))

    def _tileKey(self, tile):
        devs = self.AdcTile if tile.isAdc else self.DacTile
        en = self.enAdcTile if tile.isAdc else self.enDacTile
        index = [i for i in range(4) if en[i] and devs[i] is tile]
        if len(index) != 1:
            raise ValueError(f'{tile.path} is not a tile of {self.path}')
        return (tile.isAdc, index[0])

    # Public tile Reset: one raw restart of this tile, then the common sequence
    def _tileResetSequence(self, tile):
        self._runSequence(f'{tile.path}.Reset', 'Reset', [lambda: tile.ResetRaw()], [self._tileKey(tile)], apply=True)

    # Public tile StartUp: the recovery that keeps the settings, so nothing is written back
    def _tileStartUpSequence(self, tile):
        self._runSequence(f'{tile.path}.StartUp', 'StartUp', [lambda: tile.StartUpRaw()], [self._tileKey(tile)], apply=False)

    # Public ResetAllAdc and ResetAllDac: the raw command of one converter type, then the
    # common sequence
    def _globalResetSequence(self, isAdc):
        kind = 'Adc' if isAdc else 'Dac'
        raw = getattr(self, f'ResetAll{kind}Raw')
        tiles = self._enabledAdcTiles() if isAdc else self._enabledDacTiles()
        self._runSequence(f'{self.path}.ResetAll{kind}', 'Reset', [lambda: raw()], [(isAdc, i) for i in tiles], apply=True)

    # Public StartUpAllAdc and StartUpAllDac: the raw command of one converter type, then
    # the wait for every enabled tile, with nothing written back
    def _globalStartUpSequence(self, isAdc):
        kind = 'Adc' if isAdc else 'Dac'
        raw = getattr(self, f'StartUpAll{kind}Raw')
        tiles = self._enabledAdcTiles() if isAdc else self._enabledDacTiles()
        self._runSequence(f'{self.path}.StartUpAll{kind}', 'StartUp', [lambda: raw()], [(isAdc, i) for i in tiles], apply=False)

    #######################################################################################
    # Block.checkTransaction wraps a transaction error in a 600 byte rogue
    # GeneralError, which cuts the error text of a multi-tile failure after
    # about three lines. The global restart commands therefore issue their one
    # command write directly and raise the whole PyRFdc text. The write is
    # skipped on a disabled device, like the Block path. It returns False for that skip
    # and True once the transaction completed without an error text. Every raw command word is
    # checked against the width of its command before any transaction, like cmd.set does.
    #######################################################################################
    def _checkCommandWord(self, cmd, value):
        value = int(value)
        width = sum(cmd.bitSize)
        if not 0 <= value < (1 << width):
            raise ValueError(f'{cmd.path}: {value:#x} does not fit the {width} bit command word')
        return value

    def _writeCommandWord(self, cmd, value):
        value = self._checkCommandWord(cmd, value)
        if self.enable.value() is not True:
            return False
        with self._seqLock:
            self._clearError()
            data = bytearray(value.to_bytes(4, 'little'))
            txId = self._reqTransaction(self.offset + cmd.offset, data, 4, 0, rim.Write)
            self._waitTransaction(txId)
            err = self._getError()
            if err:
                self._clearError()
                raise rogue.GeneralError(f'{cmd.path}: {err}')
        return True

    #######################################################################################
    # MTS sync is an explicit operator step: Init and the reset and StartUp commands
    # never run it. The check is immediate and reads PyRFdc, never the host shadow of
    # the mask or the reference tile. AdcMtsValid and DacMtsValid belong to PyRFdc,
    # which clears one for every restart of a tile of that converter type and sets it
    # only after a good sync. The raw word goes through _writeCommandWord so a refusal
    # text reaches the caller whole.
    #######################################################################################
    def _mtsSyncCmd(self, cmd):
        self._writeCommandWord(cmd, 1)

    #######################################################################################
    # Clock distribution Set commands. Both go through _writeCommandWord so a refusal text
    # reaches the caller whole. RefreshStaging loads the Set variables of ClkDist from one
    # entry of the Get view and reads them back. The commit takes the sequence lock,
    # reseeds the record baseline of every enabled tile before the word is written and
    # refreshes the records afterwards, success or failure, like the global restarts.
    #######################################################################################
    def _clkDistRefreshCmd(self, cmd, arg):
        if arg is None:
            raise ValueError(f'{cmd.path} needs the index of a Distribution entry')
        index = int(arg)
        with self._seqLock:
            self._writeCommandWord(cmd, index)
            for var in self.ClkDist._stageVars:
                var.get(read=True)

    # Public Set: the commit under the sequence lock and the poll pause, with IgnoreMetalError held
    # off so a refusal or a failure is never swallowed, then the wait for every enabled tile at
    # state 15 (PLL locked on internal-PLL tiles) and the write-back of the settings written in
    # this session on the tiles the commit restarted, plus any tile it knocked. The tiles the
    # commit restarted are read from the Last affected masks after the commit and replace the
    # preview list in place: a refused commit reads 0 and writes nothing back. The preview list
    # is used only when those masks cannot be read. A commit word that never reached PyRFdc (the
    # device was disabled, so pyrogue skipped SetClkDistributionRaw or _writeCommandWord skipped
    # the write) leaves the Last masks of an earlier commit, so nothing is written back and no
    # PLL record is dropped.
    # The committed PLL record of every tile the commit restarted is dropped before
    # the write-back, also when the commit failed after the driver ran, so a later Reset or Init
    # cannot replay an old per-tile PLL commit on top of the restored clocking.
    def _clkDistSetSequence(self):
        clk = self.ClkDist
        with self._seqLock:
            adc = int(clk.PreviewAdcAffected.get(read=True))
            dac = int(clk.PreviewDacAffected.get(read=True))
            enabledAdc = self._enabledAdcTiles()
            enabledDac = self._enabledDacTiles()
            commanded = [(True, i) for i in enabledAdc if (adc >> i) & 1] + [(False, i) for i in enabledDac if (dac >> i) & 1]

            def step():
                # cleared first: a raw command that pyrogue skips never reaches _clkDistCommitCmd
                self._clkDistCommitSent = False
                saved = self.IgnoreMetalError.get(read=True)
                self.IgnoreMetalError.set(False)
                self._runWithCleanup(
                    lambda: clk.SetClkDistributionRaw(),
                    lambda: self._clkDistAfterCommit(saved, commanded),
                    'IgnoreMetalError restore')

            self._runSequence(f'{clk.path}.SetClkDistribution', 'SetClkDistribution', [step], commanded, apply=True)

    def _clkDistAfterCommit(self, savedIgnoreMetalError, commanded):
        try:
            self.IgnoreMetalError.set(savedIgnoreMetalError)
        finally:
            # The write-back targets are the tiles the commit restarted. _runSequence holds this
            # list and reads it after the step, so it is rebuilt in place. When the masks cannot
            # be read the preview tiles are kept. When the commit word never reached PyRFdc the
            # Last masks still hold an earlier commit, so nothing is a target and nothing is read.
            if not self._clkDistCommitSent:
                commanded[:] = []
            else:
                try:
                    lastAdc = int(self.ClkDist.LastAdcAffected.get(read=True))
                    lastDac = int(self.ClkDist.LastDacAffected.get(read=True))
                    commanded[:] = [(True, i) for i in self._enabledAdcTiles() if (lastAdc >> i) & 1] + \
                                   [(False, i) for i in self._enabledDacTiles() if (lastDac >> i) & 1]
                except Exception as e:
                    self._log.warning('%s: the Last affected masks could not be read, the preview tiles are kept: %s', self.ClkDist.path, e)
            # The committed PLL record of every tile in that list is dropped, including an
            # affected tile outside the span of the new distribution
            for isAdc, i in commanded:
                en = self.enAdcTile if isAdc else self.enDacTile
                if not en[i]:
                    continue
                pll = self._tileDev(isAdc, i).PllConfig
                pll._commitSnapshot = None
                pll._commitSeq = 0

    def _clkDistCommitCmd(self, cmd):
        with self._seqLock:
            tiles = self._allEnabledTiles()
            for key in tiles:
                self._tileDev(*key)._reseedResetSeq()

            def commit():
                # A raise leaves the flag set: _writeCommandWord raises only after its transaction
                # completed, and a PyRFdc error after the driver ran must still drop the PLL
                # records of the restarted tiles
                self._clkDistCommitSent = True
                if not self._writeCommandWord(cmd, 1):
                    self._clkDistCommitSent = False

            self._runWithCleanup(
                commit,
                lambda: [self._tileDev(*key)._refreshResetRecord() for key in tiles],
                'ResetRecord refresh of the enabled tiles')

    def _mtsPrecheck(self, isAdc):
        kind = 'Adc' if isAdc else 'Dac'
        mask = int(getattr(self.Mts, f'{kind}Tiles').get(read=True))
        ref = int(getattr(self.Mts, f'{kind}RefTile').get(read=True))
        if mask == 0:
            raise ValueError(f'{self.Mts.path}.Sync{kind}Tiles refused: {kind}Tiles is zero, so no tile would be synchronized; set {kind}Tiles first')
        # The group is the mask plus the reference tile, checked once each in ascending order
        label = 'ADC' if isAdc else 'DAC'
        present = self.enAdcTile if isAdc else self.enDacTile
        enabled = set(self._enabledAdcTiles() if isAdc else self._enabledDacTiles())
        failing = []
        lines = []
        for i in sorted({n for n in range(4) if (mask >> n) & 1} | {ref}):
            suffix = ' (reference tile)' if (i == ref and not (mask >> i) & 1) else ''
            if i > 3 or not present[i]:
                lines.append(f'MtsSync {label} tile {i}: does not exist in this design{suffix}')
            elif i not in enabled:
                lines.append(f'MtsSync {label} tile {i}: not enabled{suffix}')
            else:
                tile = self._tileDev(isAdc, i)
                if self._tileReady(tile):
                    continue
                try:
                    restart = int(tile.RestartStatus.get(read=True))
                except Exception:
                    restart = 'NA'
                lines.append(self._diagLine('MtsSync', isAdc, i, 'MtsPrecheck') + f' Restart={restart}')
            failing.append(self._tileLabel(isAdc, i))
        if failing:
            raise ValueError('\n'.join([f'{self.Mts.path}.Sync{kind}Tiles refused on: ' + ', '.join(failing)] + lines))
        return (mask, ref)

    def _mtsSyncSequence(self, isAdc):
        kind = 'Adc' if isAdc else 'Dac'
        with self._sequence():
            self._mtsPrecheck(isAdc)
            saved = self.IgnoreMetalError.get(read=True)
            self.IgnoreMetalError.set(False)
            self._runWithCleanup(
                lambda: getattr(self.Mts, f'Sync{kind}TilesRaw')(),
                lambda: self.IgnoreMetalError.set(saved),
                'IgnoreMetalError restore')
            if not bool(getattr(self.Mts, f'{kind}MtsValid').get(read=True)):
                raise RuntimeError(f'{self.Mts.path}.Sync{kind}Tiles: the sync returned without an error but {kind}MtsValid reads 0')

    #######################################################################################
    # StartUpAllAdcRaw/StartUpAllDacRaw/ResetAllAdcRaw/ResetAllDacRaw go through this
    # wrapper so every enabled tile's sticky restart record is refreshed after
    # the command, success or failure (the per-tile Reset command already does
    # this through RfdcTile._restartCmd). Each target tile's record baseline is
    # reseeded before the word is written, and the sequence lock is held across
    # reseed, write and refresh so no other client's restart lands in between. The
    # command word is written with _writeCommandWord so a multi-tile failure
    # reaches the caller whole.
    #######################################################################################
    def _globalRestartCmd(self, cmd, isAdc):
        with self._seqLock:
            tiles = self._enabledAdcTiles() if isAdc else self._enabledDacTiles()
            tileDevs = self.AdcTile if isAdc else self.DacTile
            for i in tiles:
                tileDevs[i]._reseedResetSeq()
            self._runWithCleanup(
                lambda: self._writeCommandWord(cmd, 1),
                lambda: [tileDevs[i]._refreshResetRecord() for i in tiles],
                f'ResetRecord refresh of the {"ADC" if isAdc else "DAC"} tiles')

    #######################################################################################
    # CustomStartUpAllAdc/CustomStartUpAllDac compose that type's StartState and
    # EndState into one word and write it once, so the C++ side runs exactly one
    # restart per enabled tile with exactly these two states. The record baselines
    # are reseeded before the write and the sticky restart records are refreshed
    # afterwards, success or failure, all under the sequence lock. The word is written
    # with _writeCommandWord so a multi-tile failure reaches the caller whole. A state outside
    # 0 to 15 is rejected before the lock is taken, so a bad word runs no reseed read, no write
    # and no record refresh.
    #######################################################################################
    def _customStartUpWord(self, cmd, startState, endState):
        for name, state in (('StartState', int(startState)), ('EndState', int(endState))):
            if not 0 <= state <= 15:
                raise ValueError(f'{cmd.path}: {name} {state} is outside 0 to 15')
        return self._checkCommandWord(cmd, (int(endState) << 4) | int(startState))

    def _globalCustomStartUpCmd(self, cmd, isAdc):
        kind = 'Adc' if isAdc else 'Dac'
        startState = int(getattr(self, f'CustomStartUpAll{kind}_StartState').value())
        endState = int(getattr(self, f'CustomStartUpAll{kind}_EndState').value())
        word = self._customStartUpWord(cmd, startState, endState)
        with self._seqLock:
            tiles = self._enabledAdcTiles() if isAdc else self._enabledDacTiles()
            tileDevs = self.AdcTile if isAdc else self.DacTile
            for i in tiles:
                tileDevs[i]._reseedResetSeq()
            self._runWithCleanup(
                lambda: self._writeCommandWord(cmd, word),
                lambda: [tileDevs[i]._refreshResetRecord() for i in tiles],
                f'ResetRecord refresh of the {"ADC" if isAdc else "DAC"} tiles')

    # Read-only refresh of the enable chain of every enabled tile, so a block or mixer
    # device that is disabled in the GUI follows the hardware. Reads no operator
    # variable and does not refresh the other mirrors (ReadAll or the restart commands do).
    # Returns None. A failed read of a tile enable flag (CheckAdcTileEnabled or
    # CheckDacTileEnabled) raises that read's own rogue error and stops the refresh; every
    # other failed read in the block and Mixer chain is collected into one RuntimeError
    # naming each read.
    def UpdateIsEnabled(self):
        errors = self._refreshEnableChain(self._allEnabledTiles())
        if errors:
            raise RuntimeError('\n'.join([f'{self.path}.UpdateIsEnabled failed'] + errors))

    @pr.expose
    def Init(self):
        print( f'{self.path}: Initialize RFDC')

        # Refuse to run on a bad config: PyRFdc never calls XRFdc_CfgInitialize
        # unless the PYRFDC_CONFIG ROM validated, so every reset command below
        # would be refused anyway. Fail loudly here instead of letting the
        # first reset command raise an opaque "not initialized" error.
        status = self.ConfigStatus.get(read=True)
        if status != 1:
            label = self.ConfigStatus.getDisp(read=False)
            message = self.ConfigMessage.get(read=True)
            raise ValueError(f'{self.path}.Init: RFDC driver is not initialized (ConfigStatus={label}): {message}')

        # Force IgnoreMetalError off for the duration of Init so no step's
        # failure can be silently swallowed, and restore it afterward
        # regardless of outcome (a failed restore is logged and never replaces
        # the sequence error).
        savedIgnoreMetalError = self.IgnoreMetalError.get(read=True)
        self.IgnoreMetalError.set(False)

        # One restart per enabled tile: the DAC tiles first, because a DAC tile can
        # source a clock distribution that feeds ADC tiles (DAC tile 0 feeds ADC
        # tile 3 on SlacRfmcCarrier) and its restart knocks them, then the ADC
        # tiles. Inside one converter type PyRFdc restarts the clock distribution
        # sources before their members. The all-tile state 15 wait, the write-back of the
        # settings written in this session and the read of every mirror follow.
        # A tile that misses the wait is reported and never restarted again.
        self._runWithCleanup(
            lambda: self._runSequence(
                f'{self.path}.Init', 'Reset',
                [lambda: self.ResetAllDacRaw(), lambda: self.ResetAllAdcRaw()],
                self._allEnabledTiles(), apply=True, mirrorAll=True),
            lambda: self.IgnoreMetalError.set(savedIgnoreMetalError),
            'IgnoreMetalError restore')
