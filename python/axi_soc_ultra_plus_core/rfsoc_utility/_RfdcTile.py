#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter tile
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class PyRFdc(rogue::interfaces::memory)
# https://github.com/slaclab/axi-soc-ultra-plus-core/blob/main/petalinux-apps/pyrfdc/files/PyRFdc.cpp
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
            function     = lambda cmd: cmd.set(1),
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
            enum         = rfsoc_utility.enumState,
        ))

        self.add(pr.RemoteVariable(
            name         = 'RestartStateEnd',
            description  = 'End state for power-on sequence',
            offset       =  0x804,
            bitSize      =  4,
            bitOffset    =  0,
            mode         = 'RW',
            enum         = rfsoc_utility.enumState,
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
            enum         = rfsoc_utility.enumState,
            pollInterval = 1,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'StartUp',
            description  = 'This API function restarts a given tile',
            offset       = 0x000,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
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
            name         = 'Reset',
            description  = 'This API function resets a given tile',
            offset       = 0x008,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'CustomStartUp_StartState',
            description  = 'This API function runs the IPSM from StartState to EndState a given tile',
            offset       = 0x00C,
            bitSize      = 4,
            bitOffset    = 0,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUp_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState a given tile',
            offset       = 0x00C,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
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
                    enum         = rfsoc_utility.enumState,
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
        self.add(pr.RemoteVariable(
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
        self.add(pr.RemoteVariable(
            name         = 'SetupFIFO',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO.',
            offset       = 0x018,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "UNDEFINED",
                0x2 : "False",
                0x3 : "True",
            },
            hidden       = True,
        ))

        if gen3 and isAdc:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOObs-Gen-3/DFE
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'SetupFIFOObs',
                description  = 'This API function enables and disables the RF-ADC observation channel FIFO.',
                offset       = 0x01C,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "UNDEFINED",
                    0x2 : "False",
                    0x3 : "True",
                },
                hidden       = True,
            ))

            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOBoth-Gen-3/DFE
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'SetupFIFOBoth',
                description  = 'This API function enables and disables the RF-ADC actual and observation channel FIFO.',
                offset       = 0x020,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "UNDEFINED",
                    0x2 : "False",
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
                    enum         = rfsoc_utility.enumRefClkSource,
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

                self.add(pr.RemoteVariable(
                    name         = 'ClockSource',
                    description  = 'This API function gets the clock source for the RF-ADCs/RF-DACs.',
                    offset       = 0x110,
                    bitSize      = 1,
                    mode         = 'RW',
                    enum         = rfsoc_utility.enumRefClkSource,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'RefClkFreq',
                    description  = 'Reference clock frequency (MHz).',
                    offset       = 0x100,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'MHz',
                    disp         = '{:1.1f}',
                ))

                self.add(pr.RemoteVariable(
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
                    description  = 'This API function update the PLL configration',
                    offset       = 0x114,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.set(1),
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
