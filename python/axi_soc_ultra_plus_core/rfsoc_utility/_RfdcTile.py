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

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIPStatus
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'TileStatus_IsEnabled',
            description  = 'Indicates tile is enabled (1) or disabled (0)',
            offset       = 0x010,
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'TileStatus_TileState',
            description  = 'Indicates current tile state',
            offset       = 0x010,
            bitSize      = 4,
            bitOffset    = 1,
            mode         = 'RO',
            pollInterval = 1,
            enum         = rfsoc_utility.powerOnSequenceSteps,
        ))

        self.add(pr.RemoteVariable(
            name         = 'TileStatus_BlockStatus',
            description  = 'Bit mask for converter status. 1 indicates converter enable',
            offset       = 0x010,
            bitSize      = 2,
            bitOffset    = 5,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'TileStatus_PowerUpState',
            description  = 'Indicates power-up status',
            offset       = 0x010,
            bitSize      = 1,
            bitOffset    = 7,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'TileStatus_PLLState',
            description  = 'Indicates power-up status',
            offset       = 0x010,
            bitSize      = 1,
            bitOffset    = 8,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

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

















        for i in range(4):
            self.add(rfsoc_utility.RfdcBlock(
                name      = f'Block[{i}]',
                gen3      = gen3,
                isAdc     = isAdc,
                offset    = 0x1000+0x400*i,
            ))
