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

import surf.devices.ti  as ti
import surf.devices.nxp as nxp

class Hardware(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(ti.Lmk04828(
            name            = 'Lmk',
            offset          = 0x0052_0000,
            allowHexFileRst = False,
        ))

        for i in range(2):
            self.add(ti.Lmx2594(
                name   = f'Lmx[{i}]',
                offset = 0x0054_0000 + i*0x2_0000,
            ))

        self.add(nxp.Sc18Is602(
            name   = 'I2cToSpi',
            offset = 0x0058_0000,
        ))

    def _start(self):
        super()._start()

        # Set the SPI clock rate
        self.I2cToSpi.SpiClockRate.setDisp('115kHz')

        # Configure the LMK for 4-wire SPI
        self.Lmk.LmkReg_0x0000.set(value=0x10) # 4-wire SPI
        self.Lmk.LmkReg_0x015F.set(value=0x3B) # STATUS_LD1 = SPI readback

    def InitClock(self, lmkConfig=None, lmxConfig=[None]):

        # Check if same LMX configuration for both devices
        if len(lmxConfig) == 1:
            lmxCfg = [lmxConfig[0] for i in range(2)]
        else:
            lmxCfg = lmxConfig

        # Seems like 1st time after power up that need to load twice
        for x in range(2):

            # Load the LMK configuration from the TICS Pro software HEX export
            self.Lmk.enable.set(True)
            self.Lmk.PwrDwnLmkChip()
            self.Lmk.PwrUpLmkChip()
            self.Lmk.LoadCodeLoaderHexFile(lmkConfig)
            self.Lmk.Init()
            self.Lmk.SYNC_EN.set(1)
            self.Lmk.enable.set(False)

            # Load the LMX configuration from the TICS Pro software HEX export
            for i in range(2):
                if lmxCfg[i] is not None:
                    self.Lmx[i].enable.set(True)
                    self.Lmx[i].LoadCodeLoaderHexFile(lmxCfg[i])
                    self.Lmx[i].enable.set(False)
