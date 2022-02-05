#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

from pydm.widgets.frame import PyDMFrame
from pydm.widgets import PyDMWaveformPlot, PyDMSpinbox, PyDMPushButton

from qtpy.QtCore import Qt
from qtpy.QtWidgets import QVBoxLayout, QHBoxLayout, QFormLayout, QGroupBox, QDoubleSpinBox

from pyrogue.pydm.data_plugins.rogue_plugin import nodeFromAddress
from pyrogue.pydm.widgets import PyRogueLineEdit

import pyrogue as pr

class LiveDisplay(PyDMFrame):
    def __init__(self, parent=None, init_channel=None, dispType='Adc', numCh=8):
        PyDMFrame.__init__(self, parent, init_channel)
        self._node     = None
        self._dispType = dispType
        self.numCh     = numCh
        self.color     = ["white","red", "dodgerblue","forestgreen","yellow","magenta","turquoise","deeppink","white","red", "dodgerblue","forestgreen","yellow","magenta","turquoise","deeppink"]
        self.path      = [f'{self.channel}.{self._dispType}Processor[{i}]' for i in range(self.numCh)]
        self.idx       = 0
        self.RxEnable  = [nodeFromAddress(self.path[i]+'.RxEnable') for i in range(self.numCh)]

    def resetScales(self):
        # Reset the auto-ranging
        self.timePlot.resetAutoRangeX()
        self.timePlot.resetAutoRangeY()
        self.freqPlot.resetAutoRangeX()
        self.freqPlot.setMinYRange(-140.0)
        self.freqPlot.setMaxYRange(0.0)

    def changePlotCh(self, ch):
        # Disable processing on current channel
        self.RxEnable[self.idx].set(False)

        # Convert float to int
        self.idx = int(ch)

        # Enable processing on new channel
        self.RxEnable[self.idx].set(True)

        # Remove curve items
        self.timePlot.removeChannelAtIndex(0)
        self.freqPlot.removeChannelAtIndex(0)

        # Add new curve item with respect to channel select
        self.timePlot.addChannel(x_channel=f'{self.path[self.idx]}.Time', y_channel=f'{self.path[self.idx]}.Data', color=self.color[self.idx])
        self.freqPlot.addChannel(x_channel=f'{self.path[self.idx]}.Freq', y_channel=f'{self.path[self.idx]}.Magnitude', color=self.color[self.idx])

        # Reset the auto-ranging
        self.resetScales()

    def connection_changed(self, connected):
        build = (self._node is None) and (self._connected != connected and connected is True)
        super(LiveDisplay, self).connection_changed(connected)

        if not build:
            return

        self._node = nodeFromAddress(self.channel)

        # Enable processing on new channel
        self.RxEnable[self.idx].set(True)

        vb = QVBoxLayout()
        self.setLayout(vb)

        #-----------------------------------------------------------------------------

        gb = QGroupBox('Time Domain')
        vb.addWidget(gb)

        fl = QFormLayout()
        fl.setRowWrapPolicy(QFormLayout.DontWrapRows)
        fl.setFormAlignment(Qt.AlignHCenter | Qt.AlignTop)
        fl.setLabelAlignment(Qt.AlignRight)
        gb.setLayout(fl)

        self.timePlot = PyDMWaveformPlot()
        self.timePlot.setLabel("bottom", text='Time (ns)')
        self.timePlot.setLabel("left",   text='Counts')
        self.timePlot.addChannel(x_channel=f'{self.path[self.idx]}.Time', y_channel=f'{self.path[self.idx]}.Data', color=self.color[self.idx])
        fl.addWidget(self.timePlot)

        #-----------------------------------------------------------------------------

        gb = QGroupBox('Frequency Domain')
        vb.addWidget(gb)

        fl = QFormLayout()
        fl.setRowWrapPolicy(QFormLayout.DontWrapRows)
        fl.setFormAlignment(Qt.AlignHCenter | Qt.AlignTop)
        fl.setLabelAlignment(Qt.AlignRight)
        gb.setLayout(fl)

        self.freqPlot = PyDMWaveformPlot()
        self.freqPlot.setLabel("bottom", text='Frequency (MHz)')
        self.freqPlot.setLabel("left",   text='Amplitude (dBFS)')
        self.freqPlot.addChannel(x_channel=f'{self.path[self.idx]}.Freq', y_channel=f'{self.path[self.idx]}.Magnitude', color=self.color[self.idx])
        self.freqPlot.setAutoRangeY(False)
        self.freqPlot.setMinYRange(-140.0)
        self.freqPlot.setMaxYRange(0.0)
        fl.addWidget(self.freqPlot)

        #-----------------------------------------------------------------------------

        gb = QGroupBox( f'{self._dispType} Display Controls')
        vb.addWidget(gb)

        fl = QFormLayout()
        fl.setRowWrapPolicy(QFormLayout.DontWrapRows)
        fl.setFormAlignment(Qt.AlignHCenter | Qt.AlignTop)
        fl.setLabelAlignment(Qt.AlignRight)
        gb.setLayout(fl)

        chSel = PyDMSpinbox()
        chSel.writeOnPress = True
        chSel.setMinimum(0)
        chSel.setMaximum(self.numCh-1)
        chSel.setEnabled(True)
        chSel.valueChanged.connect(self.changePlotCh)
        fl.addWidget(chSel)

        rstButton = PyDMPushButton(label="Full Scale")
        rstButton.clicked.connect(self.resetScales)
        fl.addWidget(rstButton)

        #-----------------------------------------------------------------------------
