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
            function     = lambda cmd: cmd.set(1),
            hidden       = True,
        ))

        self.add(pr.RemoteCommand(
            name         = 'StartUpAllDac',
            description  = 'This API function restarts ALL DAC tiles',
            offset       = 0x10004,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
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
            function     = lambda cmd: cmd.set(1), # Make sure to set rogue.root timeout > 2.0
        ))

        self.add(pr.RemoteCommand(
            name         = 'ResetAllDac',
            description  = 'This API function resets ALL DAC tiles',
            offset       = 0x10014,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1), # Make sure to set rogue.root timeout > 2.0
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

        # No poll interval on purpose. A polled variable adds a background
        # transaction every interval to a driver that may be dead, on a register
        # path that has been measured degrading once a converter fails. The
        # matching driver body is PyRFdc::InitFailReason(), which reads a member
        # and never touches the driver instance, so this register still answers
        # when every other one is being refused. It is read when someone asks.
        self.add(pr.RemoteVariable(
            name         = 'InitFailureReason',
            description  = 'Reports which driver initialization step failed. A non-zero value means the driver instance was never initialized, so register access is refused',
            offset       = 0x1200C,
            bitSize      = 32,
            mode         = 'RO',
            enum         = {
                0 : "PYRFDC_INIT_OK",
                1 : "PYRFDC_INIT_FAIL_NOT_COMPLETED",
                2 : "PYRFDC_INIT_FAIL_BAREMETAL_LOOKUP",
                3 : "PYRFDC_INIT_FAIL_METAL_INIT",
                4 : "PYRFDC_INIT_FAIL_CONFIG_LOOKUP",
                5 : "PYRFDC_INIT_FAIL_REGISTER_METAL",
            },
            hidden       = True,
        ))

        # No poll interval on any of the four below, for the same reason the
        # register above states. A polled variable adds a background
        # transaction every interval to a register path that has been measured
        # degrading once a converter fails. The matching driver bodies are
        # PyRFdc::ClkDistStatus(), PyRFdc::ClkDistMap(),
        # PyRFdc::ResetCycleCount() and PyRFdc::RecoveryCount(), and each of
        # them reads a member and never touches the driver instance, so all
        # four still answer when every other register is being refused. They
        # are read when someone asks.
        #
        # Adding these four is a deliberate choice and not an oversight of the
        # rule that this file leaves the reset path alone. That rule exists to
        # avoid host and driver version skew on the path that performs a
        # reset: ResetAllAdc at 0x10010 and ResetAllDac at 0x10014 keep their
        # offsets and their RemoteCommand form, and Init() is untouched, so a
        # host and a driver built from different trees still agree on how a
        # reset is requested. A read-only variable creates no such skew, since
        # a driver that does not implement the offset simply refuses the read.
        # Without these four the registers cannot be reached from the host at
        # all, because the read-only state capture tool reads by variable path
        # through this device model and never by literal address.
        #
        # disp is a full width hexadecimal word on all four because every one
        # of them packs fields into a word rather than reporting a quantity.
        # Printed as 0x11111111 the per tile nibbles and the two counter
        # halves are read off directly, which is the point of publishing them.
        self.add(pr.RemoteVariable(
            name         = 'ClkDistStatus',
            description  = 'Where the cached clock distribution topology came from. Bits 7:0 report the source (0 nothing was obtained, 1 the documented distribution getter answered, 2 a raw clock detect decode answered, 3 the reported IP generation was outside the range this driver knows how to ask so no source was consulted at all), bits 15:8 report the IP generation the driver holds for this part, and bits 23:16 report how many distribution groups this driver counted while it built the cache, a count with more than one producer rather than one. The documented getter counts one for every distribution slot it accepted, whether or not it could mark that slot a master, while the raw clock detect decode counts one only where it marks a master, and the normalization contributes plus one for each tile the normalization promoted to master because an edge named it and no decode marked it, so a board with one distribution slot whose source lies outside its own edge range publishes a count of two. The count is never decremented, so it is not the same as how many groupings the cache still holds: a grouping the normalization could not resolve to an orderable master is withdrawn from the map without changing this count, so a non-zero count beside a map reading every tile ungrouped is the register-only signature of a withdrawn grouping. Both byte fields saturate rather than wrap, so a generation byte reading 0xFF is either a true 255 or a field that was never set and the two cannot be told apart from that byte alone, which is what the fourth source value exists to resolve',
            offset       = 0x12010,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ClkDistMap',
            description  = 'Cached clock distribution map, four bits per tile, ADC 0 in bits 3:0 through DAC 3 in bits 31:28. A nibble reads 0xF when the tile is ungrouped, and otherwise the tile index (tile type times four plus tile id) of the master that tile takes its clock from. A nibble naming a master always names a tile whose own nibble equals its own index, which is an invariant of the cache rather than an accident of a decode, so a word violating it indicates a driver older than this one. A tile whose grouping the cache cannot order has its grouping withdrawn and reads the ungrouped nibble instead, for any of three causes: the master chain contains a cycle, the chain names a master index out of range, or the tile is marked as a master that does not name itself, in which case every edge naming that tile is withdrawn with it. One report on the error channel names every tile that was withdrawn. A word reading every tile ungrouped therefore has two readings an operator has to tell apart: on a board with no clock distribution it is the expected reading, and on a board that has one it means the grouping was withdrawn. The register pair tells them apart as well as the report does, because a non-zero group count in ClkDistStatus at 0x12010 sitting beside this word is the register-only signature of a withdrawn grouping, and the pair is what a host attaching later, a host after a bridge restart and a host after a log rotation still has, where the report is a construction-time console line. The report is what says which tiles, and that is the part the register pair cannot supply',
            offset       = 0x12014,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ResetCycleCount',
            description  = 'IPSM cycles the last global reset issued per tile, four bits per tile, ADC 0 in bits 3:0 through DAC 3 in bits 31:28. A nibble counts the cycles that reset issued for that tile, whether from an explicit reset or from the internal restart the PLL reconfigure performs, and one per tile is the expected reading on a healthy boot. A nibble reading 0xF means the count for that tile is not exact, because the tile PLL reconfigure returned non-success at a point where the driver cannot tell whether the call had already cycled it, so the true figure is one or two; a real count is capped at 0xE so it can never be confused with the reserved value; and a nibble of 2 means the tile took two cycles, of which there are two producers rather than one: a tile PLL reconfigure that measurably left the tile unpowered followed by the compensating reset, or one cycle taken in the reset sweep followed by a clock group recovery that re-ran the tile. RecoveryCount at 0x1201C rules one producer out but does not identify the other: its armed half reading zero means no recovery has ever fired on this driver instance, so the nibble came from the reconfigure, while a non-zero armed half attributes nothing, because that half counts groups rather than tiles and is cumulative since construction while this count is cleared at the start of every reset. What attributes a recovery to a tile is the clock group recovery report, which names the arming tile and the group master. On a boot where the register path has degraded that register may not answer, and the clock group recovery report on the log is then the remaining evidence',
            offset       = 0x12018,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
            hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'RecoveryCount',
            description  = 'Clock group recoveries counted since construction. Bits 15:0 count the recoveries armed and bits 31:16 count the ones that succeeded. A word reading zero on a clean boot means no recovery was ever armed, which is a different statement from a recovery that was not needed',
            offset       = 0x1201C,
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:#010x}',
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

    def Init(self):
        print( f'{self.path}: Initialize RFDC')
        # Global RFDC Reset
        self.ResetAllAdc()
        self.ResetAllDac()

        # Reset ADC Tiles
        for i in range(4):
            if self.enAdcTile[i] and (self.CheckAdcTileEnabled[i].get() != 0):
                self.AdcTile[i].Reset()

        # Reset DAC Tiles
        for i in range(4):
            if self.enDacTile[i] and (self.CheckDacTileEnabled[i].get() != 0):
                self.DacTile[i].Reset()

        # Update all the remote variables after the reset
        self.UpdateIsEnabled()
