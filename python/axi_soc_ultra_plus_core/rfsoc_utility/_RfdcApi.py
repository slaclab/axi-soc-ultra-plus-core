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

class RfdcApi(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'MstAdcTiles',
            description  = 'Method to execute the RFSoC PS rfdc-mst executable remotely for ADC tiles',
            offset       = 0x00,
            bitSize      = 8,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'MstDacTiles',
            description  = 'Method to execute the RFSoC PS rfdc-mst executable remotely for DAC tiles',
            offset       = 0x04,
            bitSize      = 8,
            base         = pr.UInt,
            mode         = 'RW',
        ))
