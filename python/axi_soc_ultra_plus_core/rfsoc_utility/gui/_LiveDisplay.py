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
from qtpy.QtWidgets import QVBoxLayout, QFormLayout, QGroupBox

from pyrogue.pydm.data_plugins.rogue_plugin import nodeFromAddress

class LiveDisplay(PyDMFrame):
    def __init__(self, parent=None, init_channel=None, dispType='Adc', numCh=8):
        PyDMFrame.__init__(self, parent, init_channel)
        self._node     = None
        self._dispType = dispType
        self.numCh     = numCh
        self.color     = ["white","red", "dodgerblue","forestgreen","yellow","magenta","turquoise","deeppink","white","red", "dodgerblue","forestgreen","yellow","magenta","turquoise","deeppink"]
        self.path      = [f'{self.channel}.{self._dispType}Processor[{i}]' for i in range(self.numCh)]
        self.idx       = 0

    def setRxEnable(self, idx, value):
        # Resolve the node on every access instead of caching it. The PyDM rogue
        # plugin tears down the shared VirtualClient whenever a channel is
        # disconnected, which permanently invalidates any node handle held here.
        nodeFromAddress(f'{self.path[idx]}.RxEnable').set(value)

    def showPlotCh(self):
        # Only the selected channel's curves are visible. Curves are never removed
        # because that disconnects their PyDM channels, which stops the
        # VirtualClient shared by the rest of the GUI.
        for i in range(self.numCh):
            self.timePlot.curveAtIndex(i).setVisible(i == self.idx)
            self.freqPlot.curveAtIndex(i).setVisible(i == self.idx)

    def resetScales(self):
        # Reset the auto-ranging
        self.timePlot.resetAutoRangeX()
        self.timePlot.resetAutoRangeY()
        self.freqPlot.resetAutoRangeX()
        self.freqPlot.setMinYRange(-140.0)
        self.freqPlot.setMaxYRange(0.0)

    def changePlotCh(self, ch):
        # Disable processing on current channel
        self.setRxEnable(self.idx, False)

        # Convert float to int
        if int(ch)< self.numCh:
            self.idx = int(ch)

        # Enable processing on new channel
        self.setRxEnable(self.idx, True)

        # Show the curve items with respect to channel select
        self.showPlotCh()

        # Reset the auto-ranging
        self.resetScales()

    def connection_changed(self, connected):
        build = (self._node is None) and (self._connected != connected and connected is True)
        super(LiveDisplay, self).connection_changed(connected)

        if not build:
            return

        self._node = nodeFromAddress(self.channel)

        # Enable processing on new channel
        self.setRxEnable(self.idx, True)

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
        for i in range(self.numCh):
            self.timePlot.addChannel(
                name       = 'Counts',
                x_channel  = f'{self.path[i]}.Time',
                y_channel  = f'{self.path[i]}.WaveformData',
                color      = self.color[i],
                symbol     = 'o',
                symbolSize = 3,
            )
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
        for i in range(self.numCh):
            self.freqPlot.addChannel(
                name       = 'Amplitude (dBFS)',
                x_channel  = f'{self.path[i]}.Freq',
                y_channel  = f'{self.path[i]}.Magnitude',
                color      = self.color[i],
                symbol     = 'o',
                symbolSize = 3,
            )
        self.freqPlot.setAutoRangeY(False)
        self.freqPlot.setMinYRange(-140.0)
        self.freqPlot.setMaxYRange(0.0)
        fl.addWidget(self.freqPlot)

        # Show only the selected channel's curves
        self.showPlotCh()

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
