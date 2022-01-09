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

import surf.axi as axi

class AppRingBufferEngine(pr.Device):
    def __init__(self,numCh=1,**kwargs):
        super().__init__(**kwargs)

        for i in range(numCh):
            self.add(axi.AxiStreamRingBuffer(
                name   = f'Ch[{i}]',
                offset = (i*0x1000),
            ))

class AppRingBuffer(pr.Device):
    def __init__(self,numAdcCh=1,numDacCh=1,**kwargs):
        super().__init__(**kwargs)

        self.add(AppRingBufferEngine(
            name   = 'Adc',
            offset = 0x0000_0000,
            numCh  = numAdcCh,
        ))

        self.add(AppRingBufferEngine(
            name   = 'Dac',
            offset = 0x0001_0000,
            numCh  = numDacCh,
        ))
