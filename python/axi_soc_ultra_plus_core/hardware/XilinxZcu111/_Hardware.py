#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import time
import pyrogue as pr

import surf.devices.ti  as ti
import surf.devices.nxp as nxp

class Lmk04208(pr.Device):
    def __init__(self, allowHexFileRst=True,**kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'LmkReg',
            offset       = 0x0,
            bitSize      = 32,
            mode         = 'WO',
            overlapEn    = True,
        ))

        @self.command(description='Load the CodeLoader .HEX file',value='',)
        def LoadCodeLoaderHexFile(arg):
            with open(arg, 'r') as ifd:
                for i, line in enumerate(ifd):
                    s = str.split(line)
                    if len(s) == 3:
                        data = int(s[2], 16)
                    else:
                        data = int(s[1], 16)
                    self.LmkReg.set(data)

class Hardware(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(nxp.Pca9555( # TCA6416A & PCA9555 have same register map
            name    = 'Tca6416a',
            offset  = 0x0000_0000,
            enabled = False, # Do not access until after SPI bridge configured
        ))

        i2cToSpiStride = 0x0000_0200
        i2cToSpiOffset = 0x0150_0000
        self.add(nxp.Sc18Is602(
            name   = 'I2cToSpi',
            offset = (i2cToSpiOffset+4*i2cToSpiStride),
        ))

        self.add(Lmk04208(
            name    = 'Lmk',
            offset  = (i2cToSpiOffset+1*i2cToSpiStride),
            enabled = False, # Do not access until after SPI bridge configured
        ))

        lmxOffset = [
            (i2cToSpiOffset+0*i2cToSpiStride),
            (i2cToSpiOffset+2*i2cToSpiStride),
            (i2cToSpiOffset+3*i2cToSpiStride),
        ]
        for i in range(3):
            self.add(ti.Lmx2594(
                name    = f'Lmx[{i}]',
                offset  = lmxOffset[i],
                enabled = False, # Do not access until after SPI bridge configured
            ))

    def _start(self):
        super()._start()

        # Set the SPI clock rate
        self.I2cToSpi.SpiClockRate.setDisp('58kHz')

        # Configure the output ports
        self.Tca6416a.enable.set(True)
        self.Tca6416a.IOC[1].set(0xF9)
        self.Tca6416a.enable.set(False)

    def InitClock(self, lmkConfig=None, lmxConfig=[None]):

        # LMX CLK_SPI_MUX_SEL[1:0] register map
        self.Tca6416a.enable.set(True)
        spiMuxSel = [
            (0x3<<1),
            (0x1<<1),
            (0x0<<1),
        ]

        # Check if same LMX configuration for both devices
        if len(lmxConfig) == 1:
            lmxCfg = [lmxConfig[0] for i in range(3)]
        else:
            lmxCfg = lmxConfig

        # Configure the LMK for 4-wire SPI
        self.Lmk.enable.set(True)
        self.Lmk.LoadCodeLoaderHexFile(lmkConfig)
        self.Lmk.enable.set(False)
        time.sleep(1.0)

        # Load the LMX configuration from the TICS Pro software HEX export
        for i in range(3):

            self.Tca6416a.OP[1].set(spiMuxSel[i]) # Set the CLK_SPI_MUX_SEL

            self.Lmx[i].enable.set(True)
            for x in range(2):
                self.Lmx[i].LoadCodeLoaderHexFile(lmxCfg[i])
            self.Lmx[i].enable.set(False)

        self.Tca6416a.enable.set(False)
        time.sleep(1.0)

