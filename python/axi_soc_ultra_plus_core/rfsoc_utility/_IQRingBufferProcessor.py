#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import rogue.interfaces.stream as ris
import pyrogue as pr
import numpy as np

import rogue
rogue.Version.minVersion('6.2.0')

# Class for streaming RX
class IQRingBufferProcessor(pr.DataReceiver):
    # Init method must call the parent class init
    def __init__( self,
            maxSize     = 2**14,
            sampleRate  = 5.0E+9, # Units of Hz
            maxAve      = 4,
            liveDisplay = True,
            hidden      = True,
            iq          = False,
            **kwargs):
        pr.Device.__init__(self, hidden=hidden, **kwargs)
        ris.Slave.__init__(self)
        pr.DataReceiver.__init__(self, enableOnStart=True, hideData=True, hidden=hidden, **kwargs)

        self._liveDisplay = liveDisplay
        self._iq = iq # Supports alternating IQ data from ADC
        self._maxSize = maxSize

        # Not saving config/state to YAML
        guiGroups = ['NoStream','NoState','NoConfig']

        # Remove data variable from stream and server
        self.Data.addToGroup('NoServe')
        self.Data.addToGroup('NoStream')
        self.Data.addToGroup('NoStatus')

        # Averaging config managment -- udpate this later
        self._idx      = 0
        self._aveSize  = 1
        self._maxAve   = maxAve

        # Each real sample is 2 bytes, each IQ sample is 4 bytes
        n_samples = self._maxSize // 4 if iq else self._maxSize // 2

        # Compute FFT Freqs
        if iq:
            fft_freqs = np.fft.fftshift(np.fft.fftfreq(n_samples,d=1/sampleRate))
        else:
            fft_freqs = np.fft.fftfreq(n_samples, d=1/sampleRate)

        self.add(pr.LocalVariable(
            name        = 'times',
            description = 'Time steps (ns)',
            typeStr     = 'Float[np]',
            value       = np.arange(n_samples) / sampleRate * 1E9, # Convert to nanoseconds
            hidden      = True,
            groups      = guiGroups,
        ))

        self.add(pr.LocalVariable(
            name        = 'fft_freqs',
            description = '1D np array of fft freqs',
            typeStr     = 'Float[np]',
            value       = fft_freqs,
            hidden      = True,
            groups      = guiGroups,
        ))

        # Store waveform data as either complex or real
        if iq:
            self.add(pr.LocalVariable(
                name        = 'WaveformData',
                description = 'Data Frame Container',
                typeStr     = 'Complex128[np]',
                value       = np.zeros(shape=n_samples, dtype=np.complex128),
                hidden      = True,
                groups      = guiGroups,
            ))
        
        else:
            self.add(pr.LocalVariable(
                name        = 'WaveformData',
                description = 'Data Frame Container',
                typeStr     = 'Int16[np]',
                value       = np.zeros(n_samples, dtype=np.int16),
                hidden      = True,
                groups      = guiGroups,
            ))

        if (self._liveDisplay):
            # real FFT is 2x redundant so only store the real frequency info if real
            empty_mag = np.zeros(n_samples) if iq else np.zeros(n_samples // 2 + 1)

            self.add(pr.LocalVariable(
                name        = 'Magnitude',
                description = 'Waveform FFT Magnitude (dB)',
                typeStr     = 'Float[np]',
                value       = empty_mag,
                hidden      = True,
                groups      = guiGroups,
            ))                

            self.add(pr.LocalVariable(
                name        = 'FftAveraging',
                description = 'Number of FFTs to average together',
                localSet    = self._fftAveraging,
                mode        = 'RW',
                typeStr     = 'UInt12',
                value       = self._maxAve,
                minimum     = 1,
                maximum     = self._maxAve,
                groups      = guiGroups,
            ))


        self.add(pr.LocalVariable(
            name   = 'NewDataReady',
            value  = False,
            groups = guiGroups,
        ))

    def _start(self):
        super()._start()
        if self._liveDisplay:
            self.RxEnable.set(value=False) # blow off data by default
        else:
            self.RxEnable.set(value=True)

    # Lots of averaging control -- will update this later
    def _fftAveraging(self,value,changed):
        if changed:
            self.FftAveraging.set(int(value))
            self.rstFftAveraging()

    def rstFftAveraging(self):
        self._idx     = 0
        self._aveSize = 1

    def running_mean(self,x):
        # Calculate running average
        retVar = np.sum(x[:self._aveSize], axis=0, dtype=np.float32)/float(self._aveSize)

        # Increment counters
        if (self._idx == self.FftAveraging.get()-1):
            self._idx = 0
        else:
            self._idx += 1
        if (self._aveSize != self.FftAveraging.get()):
            self._aveSize += 1

        # Return the results
        return retVar

    # Method which is called when a frame is received
    def process(self,frame):
        with self.root.updateGroup():
            wvfm_ints = frame.getNumpy().view(np.int16) # Extract frame as 16-bit ADC samples
            # Check frame size
            if self._iq:
                if (frame.getPayload()//4) != self._maxSize: # each IQ sample is 4 bytes
                    print( f'{self.path}: Invalid frame size.  Got {frame.getPayload()//4}, expected {self._maxSize}' )
                else:
                    wvfm_complex = wvfm_ints[::2] + 1j*wvfm_ints[1::2] # Convert interleaved IQ ADC sample pairs to complex128
                    #print(f'waveform real ints {wvfm_ints[::2]}')
                    #print(f'waveform imag ints?? {wvfm_ints[1::2]}')
                    self.WaveformData.set(wvfm_complex,write=True)
            else:
                if frame.getPayload()//2 != self._maxSize: # each real sample is 2 bytes
                    print( f'{self.path}: Invalid frame size.  Got {frame.getPayload()//2}, expected {self._maxSize}' )
                else:
                    wvfm_real = self.Data.value()[:].view(np.int16)
                    self.WaveformData.set(wvfm_real,write=True)
            # Check if live display
            if (self._liveDisplay):
                # Prevent warning message when for divide by zero encountered in log10
                # Checking for inf later to fix this in the display
                np.seterr(divide = 'ignore')
                    
                # Calculate the FFT
                if self._iq:
                    fft_dB = 20*np.log10(np.abs(np.fft.fftshift(np.fft.fft(wvfm_complex, norm='ortho'))))
                else:
                    ffb_dB = 20*np.log10(np.abs(np.fft.rfft(wvfm_real, norm='ortho')))
                fft_norm = fft_dB - np.max(fft_dB) # Normalize FFT to max value
                # Update live display variable
                self.Magnitude.set(fft_norm, write=True)

                self.NewDataReady.set(True)
