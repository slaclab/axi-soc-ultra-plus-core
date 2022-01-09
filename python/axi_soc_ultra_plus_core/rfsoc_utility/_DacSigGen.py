#-----------------------------------------------------------------------------
# This file is part of the 'Camera link gateway'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'Camera link gateway', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue as pr

class DacSigGen(pr.Device):
    def __init__(self,
            numCh        = 8,  # Must match NUM_CH_G config
            ramWidth     = 10, # Must match RAM_ADDR_WIDTH_G config
            smplPerCycle = 16, # Must match SAMPLE_PER_CYCLE_G config
            **kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'NUM_CH_G',
            description  = 'Number of DAC channels',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 0,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'RAM_ADDR_WIDTH_G',
            description  = 'RAM Width configuration',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 8,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'SAMPLE_PER_CYCLE_G',
            description  = 'Number of DAC samples per clock cycle',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 16,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'DAC_BIT_WIDTH_C',
            description  = 'Number of DAC samples per clock cycle',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 24,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'BurstCnt',
            description  = 'current value of the burst counter',
            offset       = 0x04,
            bitSize      = 32,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'BurstSize',
            description  = 'Number of burst iterations (zero inclusive)',
            offset       = 0x08,
            bitSize      = 32,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'BufferLength',
            description  = 'Number of clock cycles per burst (zero inclusive), related to SAMPLE_PER_CYCLE_G)',
            offset       = 0x0C,
            bitSize      = 32,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'Continuous',
            description  = 'Continuous mode configuration',
            offset       = 0x10,
            bitSize      = 1,
            mode         = 'RW',
        ))

        self.add(pr.RemoteCommand(
            name         = 'StartBurst',
            description  = 'Start the burst mode',
            offset       = 0x14,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        self.add(pr.RemoteVariable(
            name         = 'IdleValue',
            description  = 'DAC IDLE value while not running',
            offset       = 0x18,
            bitSize      = 16,
            mode         = 'RW',
            base         = pr.Int,
        ))

        self.add(pr.RemoteCommand(
            name         = 'Reset',
            description  = 'Force reset to the FSM',
            offset       = 0x1C,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        self.add(pr.RemoteVariable(
            name         = 'Enabled',
            description  = 'Sets the DAC output mode .... 0: bypass mode, 1: DAC Signal Generator mode',
            offset       = 0x20,
            bitSize      = 1,
            mode         = 'RW',
        ))

        for i in range(numCh):
            pr.MemoryDevice(
                name        = f'MemCh{i}',
                offset      = (0x1_0000+i*0x1_0000),
                size        = smplPerCycle*(2**ramWidth),
                wordBitSize = 16, # 16-bit word
                stride      = 2,  # 16-bit (2 byte) stride
                base        = pr.Int,
            )
