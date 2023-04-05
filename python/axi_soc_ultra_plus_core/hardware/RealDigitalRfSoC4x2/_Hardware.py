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

    def _start(self):
        super()._start()

        # Configure the PS GPIO DIR
        self.GpioPs.LMK_CLK_IN_SEL1_DIR.set(1)
        self.GpioPs.LMK_CLK_IN_SEL0_DIR.set(1)
        self.GpioPs.LMK_RST_DIR.set(1)

        # Configure the PS GPIO OEN
        self.GpioPs.LMK_CLK_IN_SEL1_OEN.set(1)
        self.GpioPs.LMK_CLK_IN_SEL0_OEN.set(1)
        self.GpioPs.LMK_RST_OEN.set(1)

        # Configure the PS GPIO OUT
        self.GpioPs.LMK_CLK_IN_SEL1_OUT.set(0)
        # self.GpioPs.LMK_CLK_IN_SEL0_OUT.set(0) # External reference
        self.GpioPs.LMK_CLK_IN_SEL0_OUT.set(1) # Local 10 MHz reference
        self.GpioPs.LMK_RST_OUT.set(0)

        # Initialize the SPI bridge
        self.SpiBridge.Regs.BAUD_RATE_DIV.setDisp('div256')
        self.SpiBridge.Init()

    def InitClock(self, lmkConfig=None, lmxConfig=[None]):

        # Check if same LMX configuration for both devices
        if len(lmxConfig) == 1:
            lmxCfg = [lmxConfig[0] for i in range(2)]
        else:
            lmxCfg = lmxConfig

        # Seems like 1st time after power up that need to load twice
        for x in range(2):

            # Configure the LMK for 4-wire SPI
            self.Lmk.enable.set(True)
            self.Lmk.LmkReg_0x0000.set(value=0x90,verify=False) # 4-wire SPI + RESET
            self.Lmk.LmkReg_0x0000.set(value=0x10,verify=False) # 4-wire SPI
            self.Lmk.LmkReg_0x014A.set(value=0x06,verify=False) # RESET/GPO as open drain
            self.Lmk.LmkReg_0x016E.set(value=0x3B,verify=False) # STATUS_LD2 = SPI readback

            # Load the LMK configuration from the TICS Pro software HEX export
            self.Lmk.PwrDwnLmkChip()
            self.Lmk.PwrUpLmkChip()
            self.Lmk.LoadCodeLoaderHexFile(lmkConfig)
            self.Lmk.Init()
            self.Lmk.LmkReg_0x016E.set(value=0x13,verify=False) # STATUS_LD2 = PLL2 DLD
            self.Lmk.enable.set(False)

            # Load the LMX configuration from the TICS Pro software HEX export
            for i in range(2):
                self.Lmx[i].enable.set(True)
                self.Lmx[i].DataBlock.set(value=0x002410,index=0, write=True) # MUXOUT_LD_SEL=readback
                self.Lmx[i].LoadCodeLoaderHexFile(lmxCfg[i])
                self.Lmx[i].DataBlock.set(value=0x002414,index=0, write=True) # MUXOUT_LD_SEL=LockDetect
                self.Lmx[i].enable.set(False)
