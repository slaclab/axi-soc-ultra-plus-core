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
import time

import surf.devices.micron as micron
import surf.devices.ti as ti

import axi_soc_ultra_plus_core.hardware.SlacRfmcCarrier as hw

class Hardware(pr.Device):
    def __init__(self,**kwargs):
        super().__init__(**kwargs)

        self.add(ti.Lmk04828(
            name            = 'Lmk',
            offset          = 0x02000000,
            allowHexFileRst = False,
        ))

        for i in range(2):
            self.add(ti.Lmx2594(
                name   = f'Lmx[{i}]',
                offset = 0x04000000 + i*0x01000000,
            ))

        self.add(hw.PMbus(
            offset  = 0x0604_0000,
            enabled = False, # Not enabled as this is a slow I2C transaction
        ))

        spdDomain = {0: 'PS', 1: 'PL'}
        spdMuxChannel = {0: 0, 1: 1}
        for i in range(2):
            self.add(micron.DdrSpd(
                name        = f'DdrSpd[{i}]',
                offset      = 0x0600_0000 + i * 0x0001_0000,
                enabled     = False,  # Not enabled as this is a slow I2C transaction
                description = (
                    f"DDR4 {spdDomain[i]} SO-DIMM SPD EEPROM, behind I2C mux "
                    f"channel {spdMuxChannel[i]}, slave address 0x50. The mux "
                    "crossbar sets its channel control byte inline per "
                    "transaction and exposes no channel-select register, so "
                    "this description is the only place in the tree "
                    "recording the domain and mux channel this device "
                    "answers behind."
                ),
            ))

        self.add(hw.I2cGpio(
            offset = 0x0607_0000,
        ))

    def _start(self):
        super()._start()

        # Configure the LMK for 4-wire SPI
        self.Lmk.LmkReg_0x0000.set(value=0x90,verify=False) # 4-wire SPI + RESET
        self.Lmk.LmkReg_0x0000.set(value=0x10,verify=False) # 4-wire SPI
        self.Lmk.LmkReg_0x014A.set(value=0x33,verify=False) # RESET/GPO = SPI readback

    def InitClock(self, lmkConfig=None, lmxConfig=[None]):

        # Check if same LMX configuration for both devices
        if len(lmxConfig) == 1:
            lmxCfg = [lmxConfig[0] for i in range(2)]
        else:
            lmxCfg = lmxConfig

        # Power up the LMX chips if used
        for i in range(2):
            if lmxCfg[i] is not None:
                self.I2cGpio.LMX_ENABLE[i].set(1)
            else:
                self.I2cGpio.LMX_ENABLE[i].set(0)

        # Let the LMX supply and internal LDOs settle before the first SPI access.
        # The LMX2594 datasheet (SNAS696C) gives no minimum power-up to SPI delay,
        # so this is a conservative margin, not a datasheet value
        time.sleep(0.1)

        # Seems like 1st time after power up that need to load twice
        lmxLocked = [False, False]
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

                    # Check the LMX PLL lock via the LMX_SDO inputs of the I2C GPIO. The SDO pin is
                    # the LMX MUXout, which is SPI readback during programming, so it is switched to
                    # lock detect and left there so that I2cGpio.LMX_PLL_LOCK keeps reporting the PLL lock.
                    # SPI readback of the LMX is invalid until the next LoadCodeLoaderHexFile()
                    r0 = self.Lmx[i].DataBlock.value(index=0) & 0xFFF7 # FCAL_EN=0 to not recalibrate
                    self.Lmx[i].DataBlock.set(value=r0 | 0x0004, index=0, write=True) # MUXOUT_LD_SEL=lock detect
                    lmxLocked[i] = False
                    for retry in range(10):
                        if self.I2cGpio.LMX_PLL_LOCK.get(index=i, read=True):
                            lmxLocked[i] = True
                            break
                        time.sleep(0.1)
                    self.Lmx[i].enable.set(False)

        # Only the last pass counts, since each pass power cycles the LMK and unlocks the LMX
        unlocked = [i for i in range(2) if lmxCfg[i] is not None and not lmxLocked[i]]
        if unlocked:
            status = ', '.join(f'Lmx[{i}] (I2cGpio.LMX_PLL_LOCK[{i}]=0)' for i in unlocked)
            raise RuntimeError(f'{self.path}.InitClock: PLL not locked: {status}')
