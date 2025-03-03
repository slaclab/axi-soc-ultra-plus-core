#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter tile
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class RfdcApi(pyrogue.interfaces.OsCommandMemorySlave)
#              located in submodule/axi-soc-ultra-plus-core/petalinux-apps/roguetcpbridge/files/roguetcpbridge
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
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'StartUp',
            description  = 'This API function restarts a given tile',
            offset       = 0x000,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'Shutdown',
            description  = 'This API function stops a given tile',
            offset       = 0x004,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'Reset',
            description  = 'This API function resets a given tile',
            offset       = 0x008,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
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
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUp_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState a given tile',
            offset       = 0x00C,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
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
                    pollInterval = 1,
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
                    enum         = rfsoc_utility.powerOnSequenceSteps,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'BlockStatus',
                    description  = 'Bit mask for converter status. 1 indicates converter enable',
                    offset       = 0x010,
                    bitSize      = 2,
                    bitOffset    = 5,
                    mode         = 'RO',
                    pollInterval = 1,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PowerUpState',
                    description  = 'Indicates power-up status',
                    offset       = 0x010,
                    bitSize      = 1,
                    bitOffset    = 7,
                    mode         = 'RO',
                    pollInterval = 1,
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PLLState',
                    description  = 'Indicates power-up status',
                    offset       = 0x010,
                    bitSize      = 1,
                    bitOffset    = 8,
                    mode         = 'RO',
                    pollInterval = 1,
                    base         = pr.Bool,
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
            pollInterval = 1,
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
                pollInterval = 1,
            ))

        class Pll(pr.Device):
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
                    enum         = {
                        0 : "XRFDC_EXTERNAL_CLK",     #define XRFDC_EXTERNAL_CLK 0x0U
                        1 : "XRFDC_INTERNAL_PLL_CLK", #define XRFDC_INTERNAL_PLL_CLK 0x1U
                    },
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
                self.add(pr.RemoteVariable(
                    name         = 'PllLocked',
                    description  = 'This API function gets the PLL lock status for the RF-ADCs/RF-DACs.',
                    offset       = 0x060,
                    bitSize      = 1,
                    mode         = 'RO',
                    base         = pr.Bool,
                    pollInterval = 1,
                ))

        # Adding the PLL device
        self.add(Pll())

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
                pollInterval = 1,
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
                pollInterval = 1,
                hidden       = True,
            )

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
        ))

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
