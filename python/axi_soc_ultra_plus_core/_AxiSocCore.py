#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue                 as pr
import surf.axi                as axi
import surf.xilinx             as xil
import axi_soc_ultra_plus_core as core

import click
import time

class AxiSocCore(pr.Device):
    """This class maps to axi-soc-ultra-plus-core/shared/rtl/AxiSocUltraPlusReg.vhd"""
    def __init__(self,
                 numDmaLanes = 1,
                 sim         = False,
                 **kwargs):
        super().__init__(**kwargs)

        self.numDmaLanes = numDmaLanes
        self.startArmed  = True
        self.sim         = sim

        # AxiVersion Module
        self.add(core.AxiVersion(
            offset       = 0x0_0000,
            expand       = False,
        ))

        # SYSMON Module
        self.add(xil.AxiSysMonUltraScale(
            offset         = 0x1_0000,
            XIL_DEVICE_G   = 'ULTRASCALE_PLUS',
            simpleViewList = ['Temperature', 'VccInt', 'VccAux', 'VccBram', 'VpVn', 'RawVpVn'],
            expand         = False
        ))

        # SYSMON LVAUX Module
        self.add(core.SysMonLvAuxDet(
            name        = 'SysMonLvAuxDet',
            offset      = 0x1_1000,
            expand      = False,
        ))

        # DMA AXI Stream Inbound Monitor
        self.add(axi.AxiStreamMonAxiL(
            name        = 'DmaIbAxisMon',
            offset      = 0x2_0000,
            numberLanes = self.numDmaLanes,
            expand      = False,
        ))

        # DMA AXI Stream Outbound Monitor
        self.add(axi.AxiStreamMonAxiL(
            name        = 'DmaObAxisMon',
            offset      = 0x3_0000,
            numberLanes = self.numDmaLanes,
            expand      = False,
        ))

    def _start(self):
        super()._start()
        if not (self.sim) and (self.startArmed):
            DMA_SIZE_G = self.AxiVersion.DMA_SIZE_G.get()
            if ( self.numDmaLanes is not DMA_SIZE_G ):
                click.secho(f'WARNING: {self.path}.numDmaLanes = {self.numDmaLanes} != {self.path}.AxiVersion.DMA_SIZE_G = {DMA_SIZE_G}', bg='cyan')
        self.startArmed = False

    def UserRst(self):
        # Send a local reset
        self.AxiVersion.UserRst()

        # Initialize watchdog counter
        watchdog_counter = 0
        watchdog_limit = 10  # 10 iterations of 0.1s = 1 second

        # Wait for the AXI-Lite to recover from reset
        while (watchdog_counter < watchdog_limit):
            if (not self.AxiVersion.AppReset.get()):
                watchdog_counter += 1
            else:
                watchdog_counter = 0  # Reset watchdog if condition is broken
            time.sleep(0.1)

    def DspRstWait(self):
        # Initialize watchdog counter
        watchdog_counter = 0
        watchdog_limit = 10  # 10 iterations of 0.1s = 1 second

        # Wait for the AXI-Lite to recover from reset
        while (watchdog_counter < watchdog_limit):
            if (not self.AxiVersion.DspReset.get()):
                watchdog_counter += 1
            else:
                watchdog_counter = 0  # Reset watchdog if condition is broken
            time.sleep(0.1)
