#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter tile
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class PyRFdc(rogue::interfaces::memory)
#-----------------------------------------------------------------------------
# This file is part of the 'SLAC Firmware Standard Library'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'SLAC Firmware Standard Library', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

enumCustomStartUp = {
    0x0 : "XRFDC_STATE_OFF",
    0x1 : "XRFDC_STATE_SHUTDOWN",
    0x3 : "XRFDC_STATE_PWRUP",
    0x6 : "XRFDC_STATE_CLK_DET",
    0xB : "XRFDC_STATE_CAL",
    0xF : "XRFDC_STATE_FULL",
}

enumState = {
    0:  'Device_Power-up_and_Configuration[0]',
    1:  'Device_Power-up_and_Configuration[1]',
    2:  'Device_Power-up_and_Configuration[2]',
    3:  'Power_Supply_Adjustment[0]',
    4:  'Power_Supply_Adjustment[1]',
    5:  'Power_Supply_Adjustment[2]',
    6:  'Clock_Configuration[0]',
    7:  'Clock_Configuration[1]',
    8:  'Clock_Configuration[2]',
    9:  'Clock_Configuration[3]',
    10: 'Clock_Configuration[4]',
    11: 'Converter_Calibration[0]',
    12: 'Converter_Calibration[1]',
    13: 'Converter_Calibration[2]',
    14: 'Wait_for_deassertion_of_AXI4-Stream_reset',
    15: 'Done',
}

enumRefClkSource = {
    0 : "XRFDC_EXTERNAL_CLK",     #define XRFDC_EXTERNAL_CLK 0x0U
    1 : "XRFDC_INTERNAL_PLL_CLK", #define XRFDC_INTERNAL_PLL_CLK 0x1U
}

class RfdcTile(pr.Device):
    def __init__(
            self,
            gen3        = True,  # True if using RFSoC GEN3 Hardware
            isAdc       = False, # True if this is an ADC tile
            description = 'RFSoC data converter tile registers',
            **kwargs):
        super().__init__(description=description, **kwargs)
        self.gen3  = gen3
        self.isAdc = isAdc

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-Power-On-State-Machine-Register-0x0004
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'RestartSM',
            description  = 'Write 1 to start power-on state machine.  Auto-clear.  SM stops at stages programmed in RestartState',
            offset       = 0x800,
            bitSize      = 1,
            overlapEn    = True, # Shares the offset with the RestartStatus read-back below
            function     = lambda cmd: cmd.set(1),
        ))

        self.add(pr.RemoteVariable(
            name         = 'RestartStatus',
            description  = 'Restart Power-On State Machine register (0x04) bit 0 read back from hardware: 1 while the power-on state machine runs toward its end state, 0 when idle. Read-only view of the offset the RestartSM command writes; never written by bulk operations',
            offset       = 0x800,
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RO',
            overlapEn    = True,
            bulkOpEn     = False,
            pollInterval = 1,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Restart-State-Register-0x0008
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'RestartStateStart',
            description  = 'Start state for power-on sequence',
            offset       =  0x804,
            bitSize      =  4,
            bitOffset    =  8,
            mode         = 'RW',
            enum         = enumState,
        ))

        self.add(pr.RemoteVariable(
            name         = 'RestartStateEnd',
            description  = 'End state for power-on sequence',
            offset       =  0x804,
            bitSize      =  4,
            bitOffset    =  0,
            mode         = 'RW',
            enum         = enumState,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Current-State-Register-0x000C
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'CurrentState',
            description  = 'Current state register',
            offset       =  0x810,
            bitSize      =  4,
            bitOffset    =  0,
            mode         = 'RO',
            enum         = enumState,
            pollInterval = 1,
        ))

        #######################################################################################
        # Tile register 0x38, PG269 v2.6 p.43. Counts automatic restarts after a
        # supply, clock, or PLL loss; saturates at 255.
        # Software restarts (Reset/StartUp/CustomStartUp) do not increment it.
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ResetCount',
            description  = 'Automatic restart count after a supply, clock, or PLL loss (PG269 p.43). Saturates at 255. Software restarts do not increment it.',
            offset       =  0x814,
            bitSize      =  8,
            bitOffset    =  0,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        #######################################################################################
        # Per-tile restart record, read-only, written by PyRFdc before
        # IgnoreMetalError can swallow a failure. Sequence bits 31:16, command
        # bits 11:8, flags bits 7:4, result bits 3:0.
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ResetRecord',
            description  = 'Per-tile restart record: sequence[31:16], command[11:8] (1 Reset, 2 StartUp, 3 CustomStartUp, 5 SetClkDistribution), flags[7:4] (0x10 parked override, 0x20 staging not refreshed), result[3:0] (1 ok, 2 failed, 3 refused: state machine busy)',
            offset       =  0x818,
            bitSize      =  32,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ResetRecordStateAtFailure',
            description  = 'CurrentState captured at the moment of the last recorded restart failure',
            offset       =  0x81C,
            bitSize      =  8,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ResetRecordCommonStatusAtFailure',
            description  = 'Common Status (0x228) captured at the moment of the last recorded restart failure',
            offset       =  0x820,
            bitSize      =  8,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ResetRecordClockDetectorAtFailure',
            description  = 'Clock detector (0x84) captured at the moment of the last recorded restart failure',
            offset       =  0x824,
            bitSize      =  8,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # Sticky results fed only from the read-only record registers above,
        # never from exception text.
        #######################################################################################
        self.add(pr.LocalVariable(
            name         = 'LastResetResult',
            description  = 'Result of the last Reset/StartUp/CustomStartUp on this tile',
            mode         = 'RO',
            value        = 'None',
        ))

        self.add(pr.LocalVariable(
            name         = 'StateAtFailure',
            description  = 'CurrentState at the time of the last recorded restart failure (-1 if none)',
            mode         = 'RO',
            value        = -1,
        ))

        self.add(pr.LocalVariable(
            name         = 'FailureCount',
            description  = 'Number of distinct recorded restart failures on this tile',
            mode         = 'RO',
            value        = 0,
        ))

        self.add(pr.LocalCommand(
            name         = 'RefreshResetRecord',
            description  = 'Re-read ResetRecord and update LastResetResult/StateAtFailure/FailureCount if it changed',
            function     = self._refreshResetRecord,
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'StartUpRaw',
            description  = 'Bare XRFdc_StartUp of this tile from state 1, settings kept; does not wait for state 15',
            offset       = 0x000,
            bitSize      = 1,
            function     = lambda cmd: self._restartCmd(cmd),
            hidden       = True,
        ))

        self.add(pr.LocalCommand(
            name         = 'StartUp',
            description  = 'Default recovery action: restarts this tile from state 1 keeping its settings, then waits for every enabled tile to reach state 15 with the PLL locked. Re-applies nothing',
            function     = lambda: self.parent._tileStartUpSequence(self),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'Shutdown',
            description  = 'This API function stops a given tile',
            offset       = 0x004,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ResetRaw',
            description  = 'Bare restart of this tile from state 0 (XRFdc_Reset): returns the tile to state 15 with the restart-reloaded registers at their Vivado values. Re-applies nothing and does not restore mixer NCO settings',
            offset       = 0x008,
            bitSize      = 1,
            function     = lambda cmd: self._restartCmd(cmd),
            hidden       = True,
        ))

        self.add(pr.LocalCommand(
            name         = 'Reset',
            description  = 'Returns the tile to state 15, waits for every enabled tile, then re-applies the settings written in this session. Settings not written in this session read their Vivado value. A bare restart does not restore mixer NCO settings.',
            function     = lambda: self.parent._tileResetSequence(self),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        #######################################################################################
        self.add(pr.LocalVariable(
            name         = 'CustomStartUp_StartState',
            description  = 'StartState used by the next CustomStartUp command on this tile. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0x1,
            enum         = enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.LocalVariable(
            name         = 'CustomStartUp_EndState',
            description  = 'EndState used by the next CustomStartUp command on this tile. Setting it does not touch hardware',
            mode         = 'RW',
            value        = 0xF,
            enum         = enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'CustomStartUp',
            description  = 'Run the power-on state machine from CustomStartUp_StartState to CustomStartUp_EndState on this tile in one command (StartState bits 3:0, EndState bits 7:4)',
            offset       = 0x00C,
            bitSize      = 8,
            function     = lambda cmd: self._customStartUpCmd(cmd),
            hidden       = True,
        ))

        class TileStatus(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_TileStatus
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_IPStatus
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIPStatus
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'IsEnabled',
                    description  = 'Indicates tile is enabled (1) or disabled (0)',
                    offset       = 0x010,
                    bitSize      = 1,
                    bitOffset    = 0,
                    mode         = 'RO',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'TileState',
                    description  = 'Indicates current tile state',
                    offset       = 0x010,
                    bitSize      = 4,
                    bitOffset    = 1,
                    mode         = 'RO',
                    pollInterval = 1,
                    enum         = enumState,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'BlockStatus',
                    description  = 'Bit mask for converter status. 1 indicates converter enable',
                    offset       = 0x010,
                    bitSize      = 2,
                    bitOffset    = 5,
                    mode         = 'RO',
                    hidden       = True,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PowerUpState',
                    description  = 'Indicates power-up status',
                    offset       = 0x010,
                    bitSize      = 1,
                    bitOffset    = 7,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PLLState',
                    description  = 'Indicates power-up status',
                    offset       = 0x010,
                    bitSize      = 1,
                    bitOffset    = 8,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

        # Adding the TileStatus device
        self.add(TileStatus())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabClkOutDiv
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabClkOutDiv
        #######################################################################################
        self.add(rfsoc_utility.ConfigVariable(
            name         = 'FabClkOutDiv',
            description  = 'Use this function to set the divider for PL clock out.',
            offset       = 0x014,
            bitSize      = 3,
            mode         = 'RW',
            enum         = {
                0x0 : "UNDEFINED",
                0x1 : "XRFDC_FAB_CLK_DIV1",
                0x2 : "XRFDC_FAB_CLK_DIV2",
                0x3 : "XRFDC_FAB_CLK_DIV4",
                0x4 : "XRFDC_FAB_CLK_DIV8",
                0x5 : "XRFDC_FAB_CLK_DIV16",
            },
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFO
        #######################################################################################
        self.add(rfsoc_utility.ConfigVariable(
            name         = 'SetupFIFO',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
            offset       = 0x018,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "False",
                0x3 : "True",
            },
            hidden       = True,
        ))

        if gen3 and isAdc:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOObs-Gen-3/DFE
            #######################################################################################
            self.add(rfsoc_utility.ConfigVariable(
                name         = 'SetupFIFOObs',
                description  = 'This API function enables and disables the RF-ADC observation channel FIFO. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
                offset       = 0x01C,
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
                name         = 'SetupFIFOBoth',
                description  = 'This API function enables and disables the RF-ADC actual and observation channel FIFO. The enable is bit 1 of the word: True (3) enables the FIFO, False (0) disables it',
                offset       = 0x020,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "False",
                    0x3 : "True",
                },
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFIFOStatus
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'FIFOStatus',
            description  = 'This API function gets the current status of the RF-ADC/RF-DAC FIFO.',
            offset       = 0x024,
            bitSize      = 1,
            mode         = 'RO',
            base         = pr.Bool,
            hidden       = True,
        ))

        if gen3 and isAdc:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFIFOStatusObs-Gen-3/DFE
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'FIFOStatusObs',
                description  = 'This API function gets the current status of the RF-ADC observation FIFO.',
                offset       = 0x028,
                bitSize      = 1,
                mode         = 'RO',
                base         = pr.Bool,
                hidden       = True,
            ))

        class PllStatus(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetClockSource
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'ClockSource',
                    description  = 'This API function gets the clock source for the RF-ADCs/RF-DACs.',
                    offset       = 0x02C,
                    bitSize      = 1,
                    mode         = 'RO',
                    enum         = enumRefClkSource,
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_PLL_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPLLConfig
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'IsEnabled',
                    description  = 'Indicates if the PLL is enabled (1) or disabled (0).',
                    offset       = 0x030,
                    bitSize      = 1,
                    mode         = 'RO',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'RefClkFreq',
                    description  = 'Reference clock frequency (MHz).',
                    offset       = 0x034,
                    bitSize      = 64,
                    mode         = 'RO',
                    base         = pr.Double,
                    units        = 'MHz',
                    disp         = '{:1.1f}',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'SampleRate',
                    description  = 'Sampling rate (GSPS).',
                    offset       = 0x03C,
                    bitSize      = 64,
                    mode         = 'RO',
                    base         = pr.Double,
                    units        = 'GSPS',
                    disp         = '{:1.3f}',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'RefClkDivider',
                    description  = 'Reference clock divider.',
                    offset       = 0x044,
                    bitSize      = 32,
                    mode         = 'RO',
                    disp         = '{:d}',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'FeedbackDivider',
                    description  = 'Feedback divider.',
                    offset       = 0x048,
                    bitSize      = 32,
                    mode         = 'RO',
                    disp         = '{:d}',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'OutputDivider',
                    description  = 'Output divider.',
                    offset       = 0x04C,
                    bitSize      = 32,
                    mode         = 'RO',
                    disp         = '{:d}',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'FractionalMode',
                    description  = 'Fractional mode. Currently not supported.',
                    offset       = 0x050,
                    bitSize      = 32,
                    mode         = 'RO',
                    hidden       = True,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'FractionalData',
                    description  = 'Fractional part of the feedback divider. Currently not supported.',
                    offset       = 0x054,
                    bitSize      = 64,
                    mode         = 'RO',
                    hidden       = True,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'FractWidth',
                    description  = 'Fractional data width. Currently not supported.',
                    offset       = 0x05C,
                    bitSize      = 32,
                    mode         = 'RO',
                    hidden       = True,
                ))

                self.add(pr.LinkVariable(
                    name         = 'VCO',
                    mode         = 'RO',
                    units        = 'GHz',
                    disp         = '{:1.3f}',
                    linkedGet    = lambda read: 0.0 if (self.RefClkDivider.get(read=read)<=0.0) else 1.0E-3*float(self.FeedbackDivider.get(read=read))*(self.RefClkFreq.get(read=read))/float(self.RefClkDivider.get(read=read)),
                    dependencies = [self.RefClkFreq, self.RefClkDivider, self.FeedbackDivider],
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPLLLockStatus
                #######################################################################################
#######################################################################################
# Commented out because there is an annoying metal debug print when autopolling via the XRFdc_GetPLLLockStatus API
# Using directly memory read access instead off PllLocked at offset instead
#######################################################################################
#                self.add(pr.RemoteVariable(
#                    name         = 'PllLocked',
#                    description  = 'This API function gets the PLL lock status for the RF-ADCs/RF-DACs.',
#                    offset       = 0x060,
#                    bitSize      = 1,
#                    mode         = 'RO',
#                    base         = pr.Bool,
#                    pollInterval = 1,
#                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Clock-Detector-Register-0x0084-Gen-3/DFE
                #######################################################################################
                if gen3:
                    self.add(pr.RemoteVariable(
                        name         = 'ClockDetector',
                        description  = 'Clock detector status. Asserted High when the tile clock detector has detected a valid clock on its local clock input.',
                        offset       =  0x808,
                        bitSize      =  1,
                        bitOffset    =  0,
                        mode         = 'RO',
                        base         = pr.Bool,
                        pollInterval = 1,
                    ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/RF-DAC/RF-ADC-Tile-n-Common-Status-Register-0x0228
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'ClockPresent',
                    description  = 'Clock present: Asserted when the reference clock for the tile is present.',
                    offset       =  0x80C,
                    bitSize      =  1,
                    bitOffset    =  0,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'SupplyStable',
                    description  = 'Supplies up: Asserted when the external supplies to the tile are stable.',
                    offset       =  0x80C,
                    bitSize      =  1,
                    bitOffset    =  1,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PoweredUp',
                    description  = 'Power-up state: Asserted when the tile is in operation.',
                    offset       =  0x80C,
                    bitSize      =  1,
                    bitOffset    =  2,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))


                self.add(pr.RemoteVariable(
                    name         = 'PllLocked',
                    description  = 'PLL locked: Asserted when the tile PLL has achieved lock.',
                    offset       =  0x80C,
                    bitSize      =  1,
                    bitOffset    =  3,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

        # Adding the PllStatus device
        self.add(PllStatus())

        class PllConfig(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                self._commitSnapshot = None
                self._commitSeq      = 0

                self.add(rfsoc_utility.ConfigVariable(
                    name         = 'ClockSource',
                    description  = 'This API function gets the clock source for the RF-ADCs/RF-DACs.',
                    offset       = 0x110,
                    bitSize      = 1,
                    mode         = 'RW',
                    enum         = enumRefClkSource,
                ))

                self.add(rfsoc_utility.ConfigVariable(
                    name         = 'RefClkFreq',
                    description  = 'Reference clock frequency (MHz).',
                    offset       = 0x100,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'MHz',
                    disp         = '{:1.1f}',
                ))

                self.add(rfsoc_utility.ConfigVariable(
                    name         = 'SampleRate',
                    description  = 'Sampling rate (MSPS).',
                    offset       = 0x108,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'MSPS',
                    disp         = '{:1.1f}',
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_DynamicPLLConfig
                #######################################################################################
                self.add(pr.RemoteCommand(
                    name         = 'PllConfigUpdate',
                    description  = 'Commit the staged ClockSource, RefClkFreq and SampleRate: a prechecked XRFdc_DynamicPLLConfig, which restarts this tile, followed by a StartUp of the same tile (state 1 to 15, settings kept), then the wait for every enabled tile and the write-back of the written settings of this tile and of any tile the restart knocked. Stage the fields after a tile RefreshStaging; a later tile Reset re-applies the committed PLL (and its StartUp) when the hardware differs from it',
                    offset       = 0x114,
                    bitSize      = 1,
                    function     = lambda cmd: self.parent.parent._pllCommitSequence(self.parent, self, cmd),
                ))

        # Adding the PllConfig device
        self.add(PllConfig())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_TileBaseAddr
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'TileBaseAddr',
            description  = 'base address of the tile',
            offset       = 0x064,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNoOfADCBlocks
        #######################################################################################
        if isAdc:
            self.add(pr.RemoteVariable(
                name         = 'NoOfADCBlocks',
                description  = 'number of RF-ADCs enabled in the tile',
                offset       = 0x068,
                bitSize      = 32,
                mode         = 'RO',
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNoOfDACBlock
        #######################################################################################
        if not isAdc:
            self.add(pr.RemoteVariable(
                name         = 'NoOfDACBlock',
                description  = 'number of RF-DACs enabled in the tile',
                offset       = 0x06C,
                bitSize      = 32,
                mode         = 'RO',
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsADCBlockEnabled
        #######################################################################################
        if isAdc:
            self.addRemoteVariables(
                name         = 'IsADCBlockEnabled',
                description  = 'If the requested RF-ADC is enabled, the function returns 1; otherwise, it returns 0',
                offset       = 0x070,
                bitSize      = 1,
                mode         = 'RO',
                number       = 4,
                stride       = 4,
                pollInterval = 1,
                hidden       = True,

            )

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsDACBlockEnabled
        #######################################################################################
        if not isAdc:
            self.addRemoteVariables(
                name         = 'IsDACBlockEnabled',
                description  = 'If the requested RF-DAC is enabled, the function returns 1; otherwise, it returns 0',
                offset       = 0x080,
                bitSize      = 1,
                mode         = 'RO',
                number       = 4,
                stride       = 4,
                pollInterval = 1,
                hidden       = True,
            )

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsHighSpeedADC
        #######################################################################################
        if isAdc:
            self.add(pr.RemoteVariable(
                name         = 'IsHighSpeedADC',
                description  = 'whether the tile is high speed or not',
                offset       = 0x090,
                bitSize      = 1,
                mode         = 'RO',
                base         = pr.Bool,
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMultibandConfig
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'MultibandConfig',
            description  = 'Multiband Config data',
            offset       = 0x094,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabClkFreq
        #######################################################################################
        #######################################################################################
        # The reason why FabClkFreq variable is commented out is because it always returns 0.0
        # It was returns zeros because InstancePtr->RFdc_Config.ADCTile_Config[Tile_Id].FabClkFreq
        # and InstancePtr->RFdc_Config.DACTile_Config[Tile_Id].FabClkFreq is never set by the driver
        #######################################################################################
#        self.add(pr.RemoteVariable(
#            name         = 'FabClkFreq',
#            description  = 'Returns the PL clock frequency',
#            offset       = 0x098,
#            bitSize      = 64,
#            mode         = 'RO',
#            base         = pr.Double,
#        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckBlockEnabled
        #######################################################################################
        if isAdc:
            self.addRemoteVariables(
                name         = 'CheckAdcBlockEnabled',
                description  = 'This API checks whether RF-ADC block is enabled or disabled',
                offset       = 0x0A0,
                bitSize      = 1,
                mode         = 'RO',
                number       = 4,
                stride       = 4,
                base         = pr.Bool,
                hidden       = True,
            )

        if not isAdc:
            self.addRemoteVariables(
                name         = 'CheckDacBlockEnabled',
                description  = 'This API checks whether RF-DAC block is enabled or disabled',
                offset       = 0x0B0,
                bitSize      = 1,
                mode         = 'RO',
                number       = 4,
                stride       = 4,
                base         = pr.Bool,
                hidden       = True,
            )

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMinSampleRate
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'MinSampleRate',
            description  = 'Tile minimum sampling rate',
            offset       = 0x0C8,
            bitSize      = 64,
            mode         = 'RO',
            base         = pr.Double,
            units        = 'GSPS',
            disp         = '{:1.0f}',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMaxSampleRate
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'MaxSampleRate',
            description  = 'Tile maximum sampling rate',
            offset       = 0x0C0,
            bitSize      = 64,
            mode         = 'RO',
            base         = pr.Double,
            units        = 'GSPS',
            disp         = '{:1.0f}',
            hidden       = True,
        ))

        #######################################################################################
        # Vivado mixer configuration of each block, served by PyRFdc from the PYRFDC_CONFIG ROM
        # (XRFdc_Config) fixed at PyRFdc construction. Read-only; used to restore a mixer that
        # was never written, since a tile restart returns the mixer NCO to these values.
        #######################################################################################
        self.addRemoteVariables(
            name         = 'ConfigMixerType',
            description  = 'Vivado mixer type of block n from the PYRFDC_CONFIG ROM (XRFdc_Config), fixed at PyRFdc construction',
            offset       = 0x200,
            bitSize      = 32,
            mode         = 'RO',
            number       = 4,
            stride       = 0x10,
            enum         = {
                0x0 : "XRFDC_MIXER_TYPE_OFF",
                0x1 : "XRFDC_MIXER_TYPE_COARSE",
                0x2 : "XRFDC_MIXER_TYPE_FINE",
                0x3 : "XRFDC_MIXER_TYPE_DISABLED",
            },
            hidden       = True,
        )

        self.addRemoteVariables(
            name         = 'ConfigMixerInputDataType',
            description  = 'Vivado mixer input data type of block n from the PYRFDC_CONFIG ROM (XRFdc_Config), fixed at PyRFdc construction',
            offset       = 0x204,
            bitSize      = 32,
            mode         = 'RO',
            number       = 4,
            stride       = 0x10,
            hidden       = True,
        )

        self.addRemoteVariables(
            name         = 'ConfigNCOFreq',
            description  = 'Vivado mixer NCO frequency of block n from the PYRFDC_CONFIG ROM (XRFdc_Config), fixed at PyRFdc construction',
            offset       = 0x208,
            bitSize      = 64,
            mode         = 'RO',
            number       = 4,
            stride       = 0x10,
            base         = pr.Double,
            units        = 'MHz',
            hidden       = True,
        )

        #######################################################################################
        # Reads the tile back through the RFDC driver getters; no hardware register is written
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'RefreshStaging',
            description  = "Re-read this tile's clock source, PLL, QMC and mixer settings from the hardware into the PyRFdc staging words that the PllConfig, QMC and Mixer variables read; writes no hardware register and is refused unless the tile is at state 15 with Restart clear",
            offset       = 0x828,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
        ))

        #######################################################################################
        #######################################################################################
        #######################################################################################

        for i in range(4):
            self.add(rfsoc_utility.RfdcBlock(
                name       = f'AdcBlock[{i}]' if isAdc else f'DacBlock[{i}]',
                gen3       = gen3,
                isAdc      = isAdc,
                offset     = 0x1000+0x400*i,
                enableDeps = [self.IsADCBlockEnabled[i]] if isAdc else [self.IsDACBlockEnabled[i]],
            ))

        # Last ResetRecord sequence number seen by _refreshResetRecord (a
        # re-read with no new restart in between must not re-count the same
        # failure). Every restart command reseeds it from the live record just
        # before the restart word is written (_reseedResetSeq)
        self._lastResetSeq = None

    #######################################################################################
    # Read-only RfdcTile.ResetRecord decode. The DIAG key names ClockPresent,
    # SupplyUp, PowerUp, PllLocked, ClkDet, ClkSrc map 1:1 to this device's
    # ClockPresent, SupplyStable, PoweredUp, PllLocked, ClockDetector,
    # ClockSource (PllStatus sub-device). No parsing of exception text; this
    # reads only the record registers PyRFdc wrote.
    #######################################################################################
    def _refreshResetRecord(self):
        record = self.ResetRecord.get(read=True)
        seq = (record >> 16) & 0xFFFF
        if seq == 0:
            # No restart is recorded for this tile in the running PyRFdc
            # (never restarted, or the process restarted since): drop the
            # remembered number, leave LastResetResult/StateAtFailure/
            # FailureCount at their current values and do not treat this as
            # a record.
            self._lastResetSeq = None
            return
        if seq == self._lastResetSeq:
            return
        self._lastResetSeq = seq

        command = (record >> 8) & 0xF
        flags = (record >> 4) & 0xF
        result = record & 0xF
        opName = {1: 'Reset', 2: 'StartUp', 3: 'CustomStartUp', 5: 'SetClkDistribution'}.get(command, 'Unknown')
        ok = (result == 1)

        if ok:
            text = 'Ok'
        elif result == 3:
            text = 'Failed (state machine busy)'
        else:
            text = 'Failed'
        if flags & 0x1:
            text += ' [parked]'
        if flags & 0x2:
            text += ' [staging not refreshed]'

        self.LastResetResult.set(f'{opName} {text}')

        if not ok:
            self.FailureCount.set(self.FailureCount.value() + 1)
            self.StateAtFailure.set(self.ResetRecordStateAtFailure.get(read=True))

    #######################################################################################
    # A PyRFdc restart starts every tile sequence at 0 again, so a number
    # remembered from the earlier process can equal the next record of the new
    # process and hide it. The live sequence is read just before a restart word
    # is written and becomes the baseline (0 becomes None), so the refresh after
    # the command processes exactly the record this command produced. Only the
    # baseline is set: no record is processed here, so FailureCount cannot
    # change before the command.
    #######################################################################################
    def _reseedResetSeq(self):
        seq = (self.ResetRecord.get(read=True) >> 16) & 0xFFFF
        self._lastResetSeq = seq if seq != 0 else None

    #######################################################################################
    # Reset and StartUp go through this wrapper so a failure (raised or
    # swallowed by IgnoreMetalError) always refreshes the sticky variables from
    # the read-only record before re-raising. The restart takes the parent Rfdc
    # sequence lock so it cannot interleave with a full restart sequence, and
    # reseeds the record baseline under that lock before the word is written. A
    # refresh failure is logged and never replaces the command error.
    #######################################################################################
    def _restartCmd(self, cmd):
        with self.parent._seqLock:
            self._reseedResetSeq()
            self.parent._runWithCleanup(
                lambda: cmd.set(1), self._refreshResetRecord, f'{self.path} ResetRecord refresh')

    #######################################################################################
    # CustomStartUp composes both states into one word and writes it once, so
    # the C++ side runs exactly one restart with exactly these two states. It takes the
    # parent Rfdc sequence lock so it cannot land inside another sequence's state 15 wait
    # or write-back; the lock is an RLock, so a call from a thread that already holds it
    # does not block. The record baseline is reseeded under the lock before the word is
    # written, as for Reset and StartUp; a refresh failure is logged and never replaces the
    # command error. A state outside 0 to 15 is rejected before the lock is taken, so a bad
    # word runs no reseed read and no write.
    #######################################################################################
    def _customStartUpCmd(self, cmd):
        word = self.parent._customStartUpWord(
            cmd, self.CustomStartUp_StartState.value(), self.CustomStartUp_EndState.value())
        with self.parent._seqLock:
            self._reseedResetSeq()
            self.parent._runWithCleanup(
                lambda: cmd.set(word), self._refreshResetRecord, f'{self.path} ResetRecord refresh')
