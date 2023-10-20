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

class SigToAxiStream(pr.Device):
    def __init__(self,axiCtrl=False,**kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'DATA_BYTES_G',
            description  = 'Data Width VHDL Generic configuration',
            offset       = 0x0,
            bitSize      = 8,
            bitOffset    = 0,
            mode         = 'RO',
            disp         = '{:d}',
            units        = 'bytes',
        ))

        self.add(pr.RemoteVariable(
            name         = 'WRD_CNT_WIDTH_G',
            description  = 'Word Counter Bit Width VHDL Generic configuration',
            offset       = 0x0,
            bitSize      = 8,
            bitOffset    = 8,
            mode         = 'RO',
            disp         = '{:d}',
            units        = 'Bit',
        ))

        self.add(pr.RemoteVariable(
            name         = 'WordSize',
            description  = 'Length of AXI stream burst (zero inclusive)',
            offset       = 0x4,
            bitSize      = 32,
            mode         = 'RW' if axiCtrl else 'RO',
            units        = 'DATA_BYTES_G',
        ))

        self.add(pr.RemoteCommand(
            name         = 'SwTrig',
            description  = 'Force a AXI stream burst from software',
            offset       = 0x8,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
            # hidden       = True,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ContinuousMode',
            description  = 'Continuous trigger mode',
            offset       = 0xC,
            bitSize      = 1,
            mode         = 'RW',
            hidden       = not axiCtrl,
        ))
