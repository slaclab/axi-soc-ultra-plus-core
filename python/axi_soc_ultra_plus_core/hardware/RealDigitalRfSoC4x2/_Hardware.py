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

import surf.devices.ti as ti
import surf.xilinx     as xil

class Hardware(pr.Device):
    def __init__(self,hidden=True,**kwargs):
        # Force Offset 0x0
        super().__init__(offset=0x0,hidden=hidden,**kwargs)

        # Create default lists
        mioConfig   = [dict(name = f'MIO_{x}', enable = False) for x in range(78)]

        # Customize the GPIO names
        mioConfig[7]  = dict(name = 'LMK_RST', enable = True)
        mioConfig[8]  = dict(name = 'LMK_CLK_IN_SEL0', enable = True)
        mioConfig[12] = dict(name = 'LMK_CLK_IN_SEL1', enable = True)
        mioConfig[17] = dict(name = 'PS_LED1', enable = True)
        mioConfig[20] = dict(name = 'PS_LED0', enable = True)

        # Add Zynq PS GPIO
        self.add(xil.GpioPs(
            offset    = 0x00FF0A0000,
            mioConfig = mioConfig,
            # expand    = True,
        ))

        # Add SPI Bridge for the Zynq PS SPI0
        self.add(xil.SpiPs(
            name   = 'SpiBridge',
            offset = 0x00FF040000,
        ))

        # Add LMK device
        virtualOffset  = ( 0 << 48) # SPI_Device_Index  = 0
        virtualOffset |= ( 2 << 44) # SPI_Address_Bytes = 2
        virtualOffset |= ( 1 << 40) # SPI_Data_Bytes    = 1
        self.add(ti.Lmk04828(
            name            = 'Lmk',
            offset          = virtualOffset,
            memBase         = self.SpiBridge.proxy,
            allowHexFileRst = False,
            enabled         = False, # Do not access until after SPI bridge configured
            hidden          = True,
        ))

        # Add LMX device
        for i in range(2):
            virtualOffset  = ( i+1 << 48) # SPI_Device_Index  = 0
            virtualOffset |= ( 1   << 44) # SPI_Address_Bytes = 1
            virtualOffset |= ( 2   << 40) # SPI_Data_Bytes    = 2
            self.add(ti.Lmx2594(
                name    = f'Lmx[{i}]',
                offset  = virtualOffset,
                memBase = self.SpiBridge.proxy,
                enabled = False, # Do not access until after SPI bridge configured
                hidden  = True,
            ))
