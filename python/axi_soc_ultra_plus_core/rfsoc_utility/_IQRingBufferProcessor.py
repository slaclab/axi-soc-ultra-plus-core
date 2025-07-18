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
class RingBufferProcessor(pr.DataReceiver):
    # Init method must call the parent class init
    def __init__( self,
            maxSize     = 2**14,
            sampleRate  = 5.0E+9, # Units of Hz
            maxAve      = 4,
            liveDisplay = True,
            hidden      = True,
            **kwargs):
        pr.Device.__init__(self, hidden=hidden, **kwargs)
        ris.Slave.__init__(self)
        pr.DataReceiver.__init__(self, enableOnStart=True, hideData=True, hidden=hidden, **kwargs)

        self._liveDisplay = liveDisplay

        # Not saving config/state to YAML
        guiGroups = ['NoStream','NoState','NoConfig']

        # Remove data variable from stream and server
        self.Data.addToGroup('NoServe')
        self.Data.addToGroup('NoStream')
        self.Data.addToGroup('NoStatus')

        # Configurable variables
        self._maxSize  = maxSize
        self._timeBin  = (1.0E+9/sampleRate) # Units of ns
        self._maxAve   = maxAve

        # Init variables
        self._freqBin  = ((0.5E+3/self._timeBin)/float(self._maxSize>>1)) # Units of MHz
        self._adcLsb   = 500.0/float(2**15) # units of mV
        self._idx      = 0
        self._aveSize  = 1

        # Calculate the time/frequency x-axis arrays
        timeSteps = np.linspace(0, self._timeBin*(self._maxSize-1), num=self._maxSize)
        freqSteps = np.linspace(0, self._freqBin*((self._maxSize>>1)-1), num=(self._maxSize>>1))

        self.add(pr.LocalVariable(
            name        = 'Time',
            description = 'Time steps (ns)',
            typeStr     = 'Float[np]',
            value       = timeSteps,
            hidden      = True,
            groups      = guiGroups,
        ))

        self.add(pr.LocalVariable(
            name        = 'WaveformData',
            description = 'Data Frame Container',
            typeStr     = 'Compl128[np]', # ????
            value       = np.zeros(shape=self._maxSize, dtype=np.complex128, order='C'),
            hidden      = True,
            groups      = guiGroups,
        ))

        if (self._liveDisplay):

            self.add(pr.LocalVariable(
                name        = 'Freq',
                description = 'Freq steps (MHz)',
                typeStr     = 'Float[np]',
                value       = freqSteps,
                hidden      = True,
                groups      = guiGroups,
            ))

            self.add(pr.LocalVariable(
                name        = 'Magnitude',
                description = 'Magnitude Frame Container',
                typeStr     = 'Float[np]',
                value       = np.zeros(shape=(self._maxSize>>1), dtype=np.float32, order='C'),
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

            self._mag = np.zeros(shape=[self._maxAve,(self._maxSize>>1)], dtype=np.float32, order='C')

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
            # Get the frame data directly as 16 bit adc samples
            waveformData = frame.getNumpy(dtype=np.int16) # Extract frame as 16-bit ADC samples with alternating I and Q

            # Check frame size
            if (frame.getPayload()//4) != self._maxSize: # each IQ sample is 4 bytes
                print( f'{self.path}: Invalid frame size.  Got {frame.getPayload()//4}, expected {self._maxSize}' )
            else:
                # Get data from frame
                self.WaveformData.set(waveformData,write=True)
                complexData = waveformData[::2] + 1j*waveformData[1::2] # Convert ADC sample pairs to complex128
                with self.waveformData.lock:
                   self.waveformData.value()[:] = waveformData
                # Write the data into the LocalVariable
                self.WaveformData.set(waveformData,write=True)

                # Check if live display
                if (self._liveDisplay):

                    # Calculate the FFT
                    freq = np.fft.fft(waveformData)/float(len(waveformData))
                    freq = freq[range(len(waveformData)//2)]

                    # Prevent warning message when for divide by zero encountered in log10
                    # Checking for inf later to fix this in the display
                    np.seterr(divide = 'ignore')

                    # Calculate the average magnitude
                    mag = 20.0*np.log10(np.abs(freq)/32767.0) # Units of dBFS
                    self._mag[self._idx] = mag
                    magnitude = self.running_mean(self._mag)
                    self.Magnitude.set(magnitude,write=True)

                self.NewDataReady.set(True)
