#-----------------------------------------------------------------------------
# This file is part of the 'SPACE SMURF RFSOC'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'SPACE SMURF RFSOC', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import os
from pydm import Display
from qtpy.QtWidgets import (QVBoxLayout, QTabWidget)

from pyrogue.pydm.widgets import DebugTree
from pyrogue.pydm.widgets import SystemWindow

Channel = 'rogue://0/root'

import axi_soc_ultra_plus_core.rfsoc_utility.gui as gui

class GuiTop(Display):
    def __init__(self, parent=None, args=[], macros=None):
        super(GuiTop, self).__init__(parent=parent, args=args, macros=None)

        self.setStyleSheet("*[dirty='true']\
                           {background-color: orange;}")

        self.sizeX  = None
        self.sizeY  = None
        self.title  = None
        self.numAdcCh  = None
        self.numDacCh  = None

        for a in args:
            if 'sizeX=' in a:
                self.sizeX = int(a.split('=')[1])
            if 'sizeY=' in a:
                self.sizeY = int(a.split('=')[1])
            if 'title=' in a:
                self.title = a.split('=')[1]
            if 'numAdcCh=' in a:
                self.numAdcCh = int(a.split('=')[1])
            if 'numDacCh=' in a:
                self.numDacCh = int(a.split('=')[1])

        if self.title is None:
            self.title = "Rogue Server: {}".format(os.getenv('ROGUE_SERVERS'))

        if self.sizeX is None:
            self.sizeX = 800
        if self.sizeY is None:
            self.sizeY = 1000
        if self.numAdcCh is None:
            self.numAdcCh = 1
        if self.numDacCh is None:
            self.numDacCh = 1

        self.setWindowTitle(self.title)

        vb = QVBoxLayout()
        self.setLayout(vb)

        self.tab = QTabWidget()
        vb.addWidget(self.tab)

        # Live Display (Tab Index=0)
        sys = SystemWindow(parent=None, init_channel=Channel)
        self.tab.addTab(sys,'System')

        # Live Display (Tab Index=1)
        var = DebugTree(parent=None, init_channel=Channel)
        self.tab.addTab(var,'Debug Tree')

        # ADC Live Display (Tab Index=2)
        adcDisplay = gui.LiveDisplay(parent=None, init_channel=Channel, dispType='Adc', numCh=self.numAdcCh)
        self.tab.addTab(adcDisplay,'ADC Waveforms')

        # DAC Live Display (Tab Index=3)
        dacDisplay = gui.LiveDisplay(parent=None, init_channel=Channel, dispType='Dac', numCh=self.numDacCh)
        self.tab.addTab(dacDisplay,'DAC Waveforms')

        # Set the default Tab view
        self.tab.setCurrentIndex(1)

        # Resize the window
        self.resize(self.sizeX, self.sizeY)

    def ui_filepath(self):
        # No UI file is being used
        return None
