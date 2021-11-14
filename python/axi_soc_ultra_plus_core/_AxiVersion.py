#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------
import pyrogue       as pr
import surf.axi      as axi
from surf.ethernet import udp

class AxiVersion(axi.AxiVersion):
    def __init__(self,
            name             = 'AxiVersion',
            description      = 'AXI-Lite Version Module',
            numUserConstants = 0,
            **kwargs):
        super().__init__(
            name        = name,
            description = description,
            **kwargs
        )

        self.add(pr.RemoteVariable(
            name         = 'LocalMacRaw',
            description  = 'MacAddress (big-Endian configuration)',
            offset       = 0x400+(4*0),
            bitSize      = 48,
            mode         = 'RO',
            hidden       = True,
        ))

        self.add(pr.LinkVariable(
            name         = 'LocalMac',
            description  = 'MacAddress (human readable)',
            mode         = 'RO',
            linkedGet    = udp.getMacValue,
            dependencies = [self.variables['LocalMacRaw']],
        ))

        self.add(pr.RemoteVariable(
            name         = 'DMA_SIZE_G',
            offset       = 0x400+(4*2),
            bitSize      = 32,
            mode         = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name         = 'DMA_CLK_FREQ_C',
            offset       = 0x400+(4*3),
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:d}',
            units        = 'Hz',
        ))

        self.add(pr.RemoteVariable(
            name         = 'AppReset',
            offset       = 0x400+(4*4),
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RO',
            base         = pr.Bool,
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = "AppClkFreq",
            description  = "Application Clock Frequency",
            offset       = 0x400+(4*5),
            units        = 'Hz',
            disp         = '{:d}',
            mode         = "RO",
            pollInterval = 1
        ))
