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

        # Per-channel bookkeeping. BufferLength is a single register shared by
        # every channel, so loading channels at different frequencies has to
        # keep it a whole number of periods for all of them.
        self._freqHz     = {} # Tone frequency currently loaded, in Hz
        self._wordLength = {} # Shortest whole-period length, in samples
        self._fillLength = {} # Samples actually written to the channel

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

            # Seeded to 0.0 Hz, never a valid loaded tone, so a channel that
            # has never been loaded cannot report a plausible frequency.
            # Read only so a configuration restore cannot write a stale
            # frequency back over one that was actually loaded.
            self.add(pr.LocalVariable(
                name    = f'LoadedFrequency[{i}]',
                typeStr = 'Float[np]',
                units   = 'Hz',
                mode    = 'RO',
                value   = 0.0,
            ))

        @self.command()
        def LoadSingleTones():
            self._loadAllTones()

    def _toneWordLength(self, freqHz):
        # Calculate the frequency ratio
        freqRatio = (self._smplRate/freqHz)

        # Calculate the common integers
        commonInt = common_integer(freqRatio,self.smplPerCycle)

        # Find an integer multiple of unit interval with respect to # of sample per clock cycle
        for x in commonInt:
            value = float(x)*freqRatio
            # Check for zero remainer and multiple of samples per cycle
            if (np.mod(value, 1) == 0) and (np.mod(value, self.smplPerCycle) == 0):
                return int(value)

        return -1

    def _loadTone(self, ch, freqHz, wordLength):
        # Calculate angular frequency & phase
        w   = 2.0*np.pi*freqHz
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

        # Track how much of the channel is actually filled
        self._fillLength[ch] = wordLength

    def LoadSingleTone(self, index, freq):
        # Load a single tone on one channel. index selects the channel and freq
        # sets its tone in units of MHz. Use the LoadSingleTones() command to
        # drive every channel from the Frequency variable instead.
        ch = int(index)
        if (ch < 0) or (ch >= self.numCh):
            click.secho(f'{self.path}.LoadSingleTone(): index={ch} outside of 0:{self.numCh-1}', fg='red')
            return

        freqHz = float(freq)*1.0E+6
        click.secho(f'{self.path}.LoadSingleTone(index={ch}, freq={freq} MHz)', fg='green')

        wordLength = self._toneWordLength(freqHz)

        ## Check if couldn't find integer multiple
        if wordLength < 0:
            click.secho('Unable to find an integer multiple of unit interval with respect to # of sample per clock cycle', fg='red')
            return

        # BufferLength is shared by every channel, so use the least common
        # multiple of the loaded channels. Nothing is committed until the
        # common length is known to fit.
        wordLengths     = dict(self._wordLength)
        wordLengths[ch] = wordLength
        bufLength       = reduce(lcm, wordLengths.values())

        ## Check if couldn't find integer multiple
        if bufLength >= self.ramDepth:
            click.secho(f'waveform length longer than buffering ({bufLength} >= {self.ramDepth})', fg='red')
            return

        self._wordLength = wordLengths
        self._freqHz[ch] = freqHz
        self.LoadedFrequency[ch].set(freqHz)

        # Reload the requested channel, plus any channel that no longer spans
        # the common buffer length
        for c in sorted(self._freqHz):
            if (c == ch) or (self._fillLength.get(c) != bufLength):
                self._loadTone(c, self._freqHz[c], bufLength)

        # Update the BufferLength register to be normalized to smplPerCycle (zero inclusive)
        self.DacSigGen.BufferLength.set((bufLength//self.smplPerCycle)-1)

        # Toggle flags (if flags already active)
        self.DacSigGen.RefreshDacFsm()

    def _loadAllTones(self):
        click.secho(f'{self.path}.LoadSingleTones(freq={self.Frequency.value()})', fg='green')

        freqHz     = self.Frequency.value()
        wordLength = self._toneWordLength(freqHz)

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
            self._loadTone(ch, freqHz, wordLength)

        self._freqHz     = {ch : freqHz     for ch in range(self.numCh)}
        self._wordLength = {ch : wordLength for ch in range(self.numCh)}

        for ch in range(self.numCh):
            self.LoadedFrequency[ch].set(freqHz)

        # Update the BufferLength register to be normalized to smplPerCycle (zero inclusive)
        self.DacSigGen.BufferLength.set((wordLength//self.smplPerCycle)-1)

        # Toggle flags (if flags already active)
        self.DacSigGen.RefreshDacFsm()
