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

import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

class Rfdc(pr.Device):
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
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=True),
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'StartUpAllDac',
            description  = 'This API function restarts ALL DAC tiles',
            offset       = 0x10004,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=False),
            hidden       = True,
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
            name         = 'ResetAllAdc',
            description  = 'This API function resets ALL ADC tiles',
            offset       = 0x10010,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=True), # Make sure to set rogue.root timeout > 2.0
        ))

        self.add(pr.RemoteCommand(
            name         = 'ResetAllDac',
            description  = 'This API function resets ALL DAC tiles',
            offset       = 0x10014,
            bitSize      = 1,
            function     = lambda cmd: self._globalRestartCmd(cmd, isAdc=False), # Make sure to set rogue.root timeout > 2.0
        ))

        #######################################################################################
        # DIAG-04: refresh every enabled tile's sticky restart record (used by
        # the global commands above and available standalone for the soak)
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
        # RFDC config ROM status: loaded at PyRFdc construction from the read-only
        # PYRFDC_CONFIG ROM in the bitstream. Read-only, no pollInterval: the status
        # is fixed once the process starts and never changes without a reboot.
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ConfigStatus',
            description  = 'RFDC config ROM validation status decided at PyRFdc construction',
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
    # D-12, D-17, D-22: StartUpAllAdc/StartUpAllDac/ResetAllAdc/ResetAllDac go
    # through this wrapper so every enabled tile's sticky restart record is
    # refreshed after the command, success or failure (the per-tile Reset
    # command already does this through RfdcTile._restartCmd).
    #######################################################################################
    def _globalRestartCmd(self, cmd, isAdc):
        try:
            cmd.set(1)
        finally:
            tiles = self._enabledAdcTiles() if isAdc else self._enabledDacTiles()
            tileDevs = self.AdcTile if isAdc else self.DacTile
            for i in tiles:
                tileDevs[i]._refreshResetRecord()

    def UpdateIsEnabled(self):
        # Reset ADC Tiles
        for i in range(4):
            if self.enAdcTile[i] and (self.CheckAdcTileEnabled[i].get() != 0):
                for j in range(4):
                    self.AdcTile[i].IsADCBlockEnabled[j].get() # Update shadow variable
                    self.AdcTile[i].AdcBlock[j].BlockStatus.MixerMode.get() # Update shadow variable
                    self.AdcTile[i].AdcBlock[j].BlockStatus.SampleRate.get() # Update shadow variable
                    self.AdcTile[i].AdcBlock[j].IsMixerEnabled.get() # Update shadow variable

        # Reset DAC Tiles
        for i in range(4):
            if self.enDacTile[i] and (self.CheckDacTileEnabled[i].get() != 0):
                for j in range(4):
                    self.DacTile[i].IsDACBlockEnabled[j].get() # Update shadow variable
                    self.DacTile[i].DacBlock[j].BlockStatus.MixerMode.get() # Update shadow variable
                    self.DacTile[i].DacBlock[j].BlockStatus.SampleRate.get() # Update shadow variable
                    self.DacTile[i].DacBlock[j].IsMixerEnabled.get() # Update shadow variable

        # Update all the remote variables after the reset
        self.readBlocks(recurse=True)
        self.checkBlocks(recurse=True)

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

        # D-17, D-22: force IgnoreMetalError off for the duration of Init so no
        # step's failure can be silently swallowed, and restore it afterward
        # regardless of outcome.
        savedIgnoreMetalError = self.IgnoreMetalError.get(read=True)
        self.IgnoreMetalError.set(False)

        adcTiles = self._enabledAdcTiles()
        dacTiles = self._enabledDacTiles()
        beforeFailureCounts = {}
        for i in adcTiles:
            beforeFailureCounts[('ADC', i)] = self.AdcTile[i].FailureCount.value()
        for i in dacTiles:
            beforeFailureCounts[('DAC', i)] = self.DacTile[i].FailureCount.value()

        failedSteps = []
        try:
            # Keep the existing order and calls exactly; the double reset is a
            # later change (Phase 2/3), not this plan's concern.
            try:
                self.ResetAllAdc()
            except Exception as e:
                failedSteps.append(('ResetAllAdc', str(e).splitlines()[0] if str(e) else ''))

            try:
                self.ResetAllDac()
            except Exception as e:
                failedSteps.append(('ResetAllDac', str(e).splitlines()[0] if str(e) else ''))

            for i in adcTiles:
                try:
                    self.AdcTile[i].Reset()
                except Exception as e:
                    failedSteps.append((f'AdcTile[{i}].Reset', str(e).splitlines()[0] if str(e) else ''))

            for i in dacTiles:
                try:
                    self.DacTile[i].Reset()
                except Exception as e:
                    failedSteps.append((f'DacTile[{i}].Reset', str(e).splitlines()[0] if str(e) else ''))

            try:
                self.UpdateIsEnabled()
            except Exception as e:
                failedSteps.append(('UpdateIsEnabled', str(e).splitlines()[0] if str(e) else ''))

        finally:
            self.IgnoreMetalError.set(savedIgnoreMetalError)

        # Aggregate: a tile is failed if its FailureCount rose during this
        # Init, regardless of whether the step above raised or was swallowed
        # by a previous IgnoreMetalError state (DIAG-04). Order ADC 0 to 3
        # then DAC 0 to 3.
        failedTiles = []
        for i in sorted(adcTiles):
            if self.AdcTile[i].FailureCount.value() > beforeFailureCounts[('ADC', i)]:
                failedTiles.append(f'ADC tile {i}')
        for i in sorted(dacTiles):
            if self.DacTile[i].FailureCount.value() > beforeFailureCounts[('DAC', i)]:
                failedTiles.append(f'DAC tile {i}')

        if failedSteps or failedTiles:
            parts = []
            if failedTiles:
                parts.append('tiles: ' + ', '.join(failedTiles))
            if failedSteps:
                parts.append('steps: ' + '; '.join(f'{step}: {msg}' for step, msg in failedSteps))
            raise RuntimeError(f'{self.path}.Init failed on: ' + ' | '.join(parts))
