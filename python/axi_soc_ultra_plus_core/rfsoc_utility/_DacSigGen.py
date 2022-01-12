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
import click
import csv

class DacSigGen(pr.Device):
    def __init__(self,
            numCh        = 8,  # Must match NUM_CH_G config
            ramWidth     = 10, # Must match RAM_ADDR_WIDTH_G config
            smplPerCycle = 16, # Must match SAMPLE_PER_CYCLE_G config
            **kwargs):
        super().__init__(**kwargs)

        self.numCh        = numCh
        self.ramWidth     = ramWidth
        self.smplPerCycle = smplPerCycle

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
            bitSize      = ramWidth,
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
            name         = 'SoftwareTrigger',
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
            self.add(pr.RemoteVariable(
                name         = ('Waveform[%d]' % i),
                description  = 'Waveform data 16-bit samples',
                offset       = (0x1_0000+i*0x1_0000),
                bitSize      = 16 * smplPerCycle*(2**ramWidth),
                bitOffset    = 0,
                numValues    = smplPerCycle*(2**ramWidth),
                valueBits    = 16,
                valueStride  = 16,
                updateNotify = True,
                bulkOpEn     = False, # FALSE for large variables
                overlapEn    = False,
                verify       = True,
                hidden       = True,
                base         = pr.Int,
                mode         = "RW",
            ))

        self.add(pr.LocalVariable(
            name         = 'CsvFilePath',
            description  = 'Used if command\'s argument is empty',
            mode         = 'RW',
            value        = '',
        ))

        @self.command(value='',description='Load the .CSV',)
        def LoadCsvFile(arg):
            # Check if non-empty argument
            if (arg != ''):
                path = arg
            else:
                # Use the variable path instead
                path = self.CsvFilePath.get()

            # Open the .CSV file
            index = 0
            firstRead = True
            with open(path, mode='r', encoding='utf-8-sig') as csvfile:
                reader = csv.reader(csvfile, delimiter=',', quoting=csv.QUOTE_NONE)
                for row in reader:
                    if firstRead:
                        firstRead = False
                        numCh = len(row)
                    for ch in range(numCh):
                        # Update only the shadow variable value (write performance reasons)
                        self.Waveform[ch].set(value=int(row[ch]),index=index,write=False)
                    index += 1

            # Push all shadow variables to hardware
            for ch in range(numCh):
                self.Waveform[ch].write()

            # Update the BufferLength register to be normalized to smplPerCycle (zero inclusive)
            self.BufferLength.set(int(index/smplPerCycle)-1)
