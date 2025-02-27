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

class RfdcApi(pr.Device):
    def __init__(
            self,
            gen3      = True, # True if using RFSoC GEN3 Hardware
            enAdcTile = [True,True,True,True],
            enDacTile = [True,True,True,True],
            **kwargs):
        super().__init__(**kwargs)

        for i in range(4):
            if enAdcTile[i]:
                self.add(rfsoc_utility.RfTile(
                    name    = f'AdcTile[{i}]',
                    isAdc   = True,
                    gen3    = gen3,
                    offset  = (0x0000+0x1000*i),
                    expand  = False,
                ))

        for i in range(4):
            if enDacTile[i]:
                self.add(rfsoc_utility.RfTile(
                    name    = f'DacTile[{i}]',
                    isAdc   = False,
                    gen3    = gen3,
                    offset  = (0x4000+0x1000*i),
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
