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

import surf.devices.ti  as ti
import surf.devices.nxp as nxp

class Hardware(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(ti.Lmk04828(
            name            = 'Lmk',
            offset          = 0x0052_0000,
            allowHexFileRst = False,
            expand          = True,
        ))

        self.add(nxp.Sc18Is602(
            name   = 'I2cToSpi',
            offset = 0x0058_0000,
            # expand = True,
        ))
