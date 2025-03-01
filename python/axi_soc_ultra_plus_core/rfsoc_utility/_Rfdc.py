#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter tile
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class RfdcApi(pyrogue.interfaces.OsCommandMemorySlave)
#              located in submodule/axi-soc-ultra-plus-core/petalinux-apps/roguetcpbridge/files/roguetcpbridge
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

class Rfdc(pr.Device):
    def __init__(
            self,
            gen3      = True, # True if using RFSoC GEN3 Hardware
            enAdcTile = [True,True,True,True],
            enDacTile = [True,True,True,True],
            **kwargs):
        super().__init__(**kwargs)
        self.gen3      = gen3
        self.enAdcTile = enAdcTile
        self.enDacTile = enDacTile

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_StartUp
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'StartUpAllAdc',
            description  = 'This API function restarts ALL ADC tiles',
            offset       = 0x10000,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        self.add(pr.RemoteCommand(
            name         = 'StartUpAllDac',
            description  = 'This API function restarts ALL DAC tiles',
            offset       = 0x10004,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Shutdown
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ShutdownAllAdc',
            description  = 'This API function stops ALL ADC tiles',
            offset       = 0x10008,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        self.add(pr.RemoteCommand(
            name         = 'ShutdownAllDac',
            description  = 'This API function stops ALL DAC tiles',
            offset       = 0x1000C,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Reset
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ResetAllAdc',
            description  = 'This API function resets ALL ADC tiles',
            offset       = 0x10010,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        self.add(pr.RemoteCommand(
            name         = 'ResetAllDac',
            description  = 'This API function resets ALL DAC tiles',
            offset       = 0x10014,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CustomStartUp
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllAdc_StartState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL ADC tiles',
            offset       = 0x10018,
            bitSize      = 4,
            bitOffset    = 0,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllAdc_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL ADC tiles',
            offset       = 0x10018,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllDac_StartState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL DAC tiles',
            offset       = 0x1001C,
            bitSize      = 4,
            bitOffset    = 0,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllDac_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL DAC tiles',
            offset       = 0x1001C,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFO
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'SetupFIFOAllAdc',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO ALL ADC tiles',
            offset       = 0x10020,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "UNDEFINED",
                0x2 : "False",
                0x3 : "True",
            },
        ))

        self.add(pr.RemoteVariable(
            name         = 'SetupFIFOAllDac',
            description  = 'This API function enables and disables the RF-ADC/RF-DAC FIFO ALL DAC tiles',
            offset       = 0x10024,
            bitSize      = 2,
            mode         = 'WO',
            enum         = {
                0x0 : "UNDEFINED",
                0x2 : "False",
                0x3 : "True",
            },
        ))

        if gen3:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetupFIFOObs-Gen-3/DFE
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'SetupFIFOObsAllAdc',
                description  = 'This API function enables and disables the RF-ADC observation channel FIFO.',
                offset       = 0x10028,
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
                name         = 'SetupFIFOBothAllAdc',
                description  = 'This API function enables and disables the RF-ADC actual and observation channel FIFO.',
                offset       = 0x1002C,
                bitSize      = 2,
                mode         = 'WO',
                enum         = {
                    0x0 : "UNDEFINED",
                    0x2 : "False",
                    0x3 : "True",
                },
            ))


















        for i in range(4):
            if enAdcTile[i]:
                self.add(rfsoc_utility.RfdcTile(
                    name    = f'AdcTile[{i}]',
                    isAdc   = True,
                    gen3    = gen3,
                    offset  = (0x0000+0x2000*i),
                    expand  = False,
                ))

        for i in range(4):
            if enDacTile[i]:
                self.add(rfsoc_utility.RfdcTile(
                    name    = f'DacTile[{i}]',
                    isAdc   = False,
                    gen3    = gen3,
                    offset  = (0x8000+0x2000*i),
                    expand  = False,
                ))

        self.add(pr.RemoteVariable(
            name         = 'MstAdcTiles',
            description  = 'Method to execute the RFSoC PS rfdc-mst executable remotely for ADC tiles',
            offset       = 0xF000,
            bitSize      = 4,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'MstDacTiles',
            description  = 'Method to execute the RFSoC PS rfdc-mst executable remotely for DAC tiles',
            offset       = 0xF004,
            bitSize      = 4,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'MetalLogLevel',
            description  = 'Sets the bare metal driver logging level',
            offset       = 0xFFF8,
            bitSize      = 1,
            mode         = 'RW',
            enum         = {
                0 : "METAL_LOG_ERROR",
                1 : "METAL_LOG_DEBUG",
            },
        ))

        self.add(pr.RemoteVariable(
            name         = 'Scratchpad',
            description  = 'Test register (no impact to RFDC module)',
            offset       = 0xFFFC,
            bitSize      = 32,
            mode         = 'RW',
        ))
