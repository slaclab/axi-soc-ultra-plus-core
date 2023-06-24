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

import numpy as np
import click

from fractions import Fraction
from functools import reduce
from math import gcd

def lcm(a, b):
    return a * b // gcd(a, b)

def common_integer(*numbers):
    fractions = [Fraction(n).limit_denominator() for n in numbers]
    multiple  = reduce(lcm, [f.denominator for f in fractions])
    ints      = [int(f * multiple) for f in fractions]
    divisor   = reduce(gcd, ints)
    return [int(n / divisor) for n in ints]

class SigGenLoader(pr.Device):
    def __init__(self,
            DacSigGen    = None,
            numCh        = 8,  # Must match NUM_CH_G config
            ramWidth     = 10, # Must match RAM_ADDR_WIDTH_G config
            smplPerCycle = 16, # Must match SAMPLE_PER_CYCLE_G config
            sampleRate   = 5.0E+9, # Units of Hz
            defaultFreq  = 200.0E+6, # Units of Hz
        **kwargs):
        super().__init__(**kwargs)

        self.DacSigGen    = DacSigGen
        self.numCh        = numCh
        self.ramDepth     = 2**ramWidth
        self.smplPerCycle = smplPerCycle
        self._smplRate    = sampleRate
        self._timeBin     = (1.0/sampleRate)

        self.add(pr.LocalVariable(
            name    = 'Frequency',
            typeStr = 'Float[np]',
            units   = 'Hz',
            value   = defaultFreq,
        ))

        phase = [90.0, 0.0, 90.0, 0.0, 90.0, 0.0, 90.0, 0.0, 90.0, 0.0, 90.0, 0.0, 90.0, 0.0, 90.0, 0.0]

        for i in range(self.numCh):

            self.add(pr.LocalVariable(
                name    = f'Amplitude[{i}]',
                typeStr = 'Int16[np]',
                units   = 'Counts',
                value   = 10000,
            ))

            self.add(pr.LocalVariable(
                name    = f'Phase[{i}]',
                typeStr = 'Float[np]',
                units   = 'degrees',
                value   = phase[i],
            ))

        @self.command()
        def LoadSingleTones():
            click.secho(f'{self.path}.LoadSingleTones(freq={self.Frequency.value()})', fg='green')

            # Calculate the frequency ratio
            freqRatio = (self._smplRate/self.Frequency.value())

            # Calculate the common integers
            commonInt = common_integer(freqRatio,self.smplPerCycle)

            # Find an integer multiple of unit interval with respect to # of sample per clock cycle
            wordLength = -1
            for x in commonInt:
                value = float(x)*freqRatio
                # Check for zero remainer and multiple of samples per cycle
                if (np.mod(value, 1) == 0) and (np.mod(value, self.smplPerCycle) == 0):
                    wordLength = int(value)
                    break

            ## Check if couldn't find integer multiple
            if wordLength < 0:
                click.secho('Unable to find an integer multiple of unit interval with respect to # of sample per clock cycle', fg='red')
                return

            ## Check if couldn't find integer multiple
            if wordLength >= self.ramDepth:
                click.secho('waveform length longer than buffering', fg='red')
                return

            # Loop through the channels
            for ch in range(self.numCh):

                # Calculate angular frequency & phase
                w   = 2.0*np.pi*self.Frequency.value()
                phi = self.Phase[ch].value()*np.pi/180.0

                # Load the waveforms data
                for t in range(wordLength):

                    # Calculate the value
                    timeStep = float(t)*self._timeBin
                    value    = float(self.Amplitude[ch].value())*np.sin(w*timeStep + phi)

                    # Update only the shadow variable value (write performance reasons)
                    self.DacSigGen.Waveform[ch].set(value=int(value),index=t,write=False)

                # Push all shadow variables to hardware
                self.DacSigGen.Waveform[ch].write()

            # Update the BufferLength register to be normalized to smplPerCycle (zero inclusive)
            self.DacSigGen.BufferLength.set((wordLength//self.smplPerCycle)-1)

            # Toggle flags (if flags already active)
            self.DacSigGen.RefreshDacFsm()
