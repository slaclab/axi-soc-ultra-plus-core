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

        if (numAdcCh > 0):
            self.add(AppRingBufferEngine(
                name   = 'Adc',
                offset = 0x0000_0000,
                numCh  = numAdcCh,
                # expand = True,
            ))

        if (numDacCh > 0):
            self.add(AppRingBufferEngine(
                name   = 'Dac',
                offset = 0x0001_0000,
                numCh  = numDacCh,
                # expand = True,
            ))

        self.add(axi.AxiStreamFrameRateLimiter(
            name   = 'RateLimiter',
            offset = 0x0002_0000,
            # expand = True,
        ))
