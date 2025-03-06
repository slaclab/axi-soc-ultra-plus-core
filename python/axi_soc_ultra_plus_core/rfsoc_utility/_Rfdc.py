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
            enAdcTile = [True,True,True,True],
            enDacTile = [True,True,True,True],
            gen3      = True, # True if using RFSoC GEN3 Hardware
            **kwargs):
        super().__init__(**kwargs)
        self.gen3      = gen3
        self.enAdcTile = enAdcTile
        self.enDacTile = enDacTile

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
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllAdc_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL ADC tiles',
            offset       = 0x10018,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllDac_StartState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL DAC tiles',
            offset       = 0x1001C,
            bitSize      = 4,
            bitOffset    = 0,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CustomStartUpAllDac_EndState',
            description  = 'This API function runs the IPSM from StartState to EndState ALL DAC tiles',
            offset       = 0x1001C,
            bitSize      = 4,
            bitOffset    = 4,
            mode         = 'WO',
            enum         = rfsoc_utility.enumCustomStartUp,
            hidden       = True,
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
            hidden       = True,
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
            hidden       = True,
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
                hidden       = True,
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
            ))

        class Mst(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
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
                    description  = 'MMethod to get all the enabled MTS ADC tiles',
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
                    name         = 'SyncAdcTiles',
                    description  = 'Method to execute the MTS SYNC for ADC tiles',
                    offset       = 0x11008,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.set(1),
                ))

                self.add(pr.RemoteCommand(
                    name         = 'SyncDacTiles',
                    description  = 'Method to execute the MTS SYNC for DAC tiles',
                    offset       = 0x1100C,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.set(1),
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

        # Adding the MTS device
        self.add(Mst())

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
            description  = 'Used to bypass the bare metal driver error returns (debugging only)',
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

    def Init(self):
        print( f'{self.path}: Initialize RFDC')
        # Global RFDC Reset
        self.ResetAllAdc
        self.ResetAllDac

        # Title Reset
        for i in range(4):
            if self.enAdcTile[i] and (self.CheckAdcTileEnabled[i].get() != 0):
                self.AdcTile[i].Reset()
            if self.enDacTile[i] and (self.CheckDacTileEnabled[i].get() != 0):
                self.DacTile[i].Reset()
