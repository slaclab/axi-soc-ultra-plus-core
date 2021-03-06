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

class SysMonLvAuxDet(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'AdcBus',
            offset       = 0x000,
            bitSize      = 12,
            bitOffset    = 4,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'LvAuxThreshRaw',
            offset       = 0x100,
            bitSize      = 12,
            bitOffset    = 4,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'ForceAdcBusLockUp',
            offset       = 0x100,
            bitSize      = 1,
            bitOffset    = 16,
            mode         = 'RW',
        ))

        self.add(pr.LinkVariable(
            name         = 'LvAuxThresh',
            units        = 'V',
            mode         = 'RO',
            disp         = '{:1.1f}',
            dependencies = [self.LvAuxThreshRaw],
            linkedGet    = lambda: round(self.LvAuxThreshRaw.value() * 244e-6,3)
        ))
