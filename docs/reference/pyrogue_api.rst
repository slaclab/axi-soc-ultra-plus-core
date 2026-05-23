PyRogue API Reference
=====================

This page documents the **platform-level** PyRogue classes provided by the
``axi_soc_ultra_plus_core`` Python package (installed from the
``firmware/submodules/axi-soc-ultra-plus-core/`` submodule). These classes form the
foundation that every ``Simple-*-Example`` application repo extends.

Application-level PyRogue (the per-board ``Root`` / ``RFSoC`` / ``Application`` device tree)
is documented in the reference pages of each application repo.

Device tree pattern
-------------------

The PyRogue device tree is a direct mirror of the RTL AXI-Lite address hierarchy.
Each ``pr.Device`` subclass holds its **offset relative to its parent**; the absolute
address of any register is the sum of offsets along the path from root to leaf.

.. code-block:: python

   class MyDevice(pr.Device):
       def __init__(self, **kwargs):
           super().__init__(**kwargs)

           self.add(SomeChild(
               offset = 0x00_000000,
               param  = value,
           ))

Key rules:

- Always pass ``**kwargs`` — never enumerate base-class arguments explicitly.
- Use ``self.add(...)`` for every child device or variable.
- Offset addresses use hex with underscore byte groupings: ``0x00_000000``,
  ``0xA000_0000``, ``0x04_0000_0000``.
- Each ``pr.Device`` offset must match the corresponding ``AXIL_CONFIG_C(INDEX).baseAddr``
  value generated in the VHDL crossbar.

AxiSocCore
----------

:repo:`python/axi_soc_ultra_plus_core/_AxiSocCore.py`

``AxiSocCore`` is the platform core PyRogue ``Device``. It wraps the AXI-Lite bridge from
the Zynq PS to the PL, the DMA engine, and the ``SysMon`` subsystem.

Responsibilities:

- Exposes the AXI-Lite register space of the ``AxiSocUltraPlusCore`` RTL block.
- Provides ``AxiVersion``, system monitor (``SysMonLvAuxDet``), and DMA status registers.
- Acts as the first-level child of the application ``RFSoC`` device at a fixed offset
  determined by the platform crossbar.

Typical instantiation (inside the application ``RFSoC`` device):

.. code-block:: python

   import axi_soc_ultra_plus_core as soc_core

   self.add(soc_core.AxiSocCore(
       offset = 0x0000_0000,
       expand = True,
   ))

AppRingBuffer
-------------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_AppRingBuffer.py`

``AppRingBuffer`` is the PyRogue ``Device`` that controls the ADC and DAC capture ring
buffers in the RTL. It provides AXI-Lite register access to the ring buffer control and
status, and is the source of the DMA inbound data stream.

Responsibilities:

- Sets the ring buffer trigger mode, depth, and channel enables.
- Reports capture status (fill level, overrun flags).
- Coordinates with the host-side ``RingBufferProcessor`` to frame captured ADC/DAC samples.

``AppRingBuffer`` is instantiated inside the application ``Application`` device, not directly
in ``AxiSocCore``. Each application repo sets the ``numAdcCh`` and ``numDacCh`` generics to
match the board's RFDC configuration.

RingBufferProcessor
-------------------

:repo:`python/axi_soc_ultra_plus_core/hardware/_RingBufferProcessor.py`

``RingBufferProcessor`` is a host-side Rogue stream receiver. It sits at the end of the
DMA TCP stream pipeline and processes captured ADC or DAC frames for display or storage.

The stream is wired in the application ``Root`` using the Rogue ``>>`` operator:

.. code-block:: python

   # Connect TCP stream → drop FIFO → ring buffer processor
   self.ringBufferAdc[i] >> self.adcDropFifo[i] >> self.adcProcessor[i]

The drop FIFO (``maxDepth=1``) prevents host-side backpressure from stalling the DMA
path inside the firmware.

RFDC API
--------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py`

The ``Rfdc`` device (and its children ``RfdcTile`` and ``RfdcBlock``) wraps the Xilinx RF
Data Converter IP core register map. It exposes:

- Per-tile and per-block ADC/DAC configuration registers.
- Multi-Tile Synchronization (MTS) control.
- Sample rate, decimation/interpolation factor, and mixer frequency settings.

The ``Rfdc`` device is instantiated at the RFDC crossbar slot (index 1 in the top-level
crossbar). Application ``Root.start()`` sequences clock initialization before calling
``Rfdc.Init()`` to program the IP core.

Source files:

- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py`
- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py`
- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcBlock.py`

PyDM GUI launcher
-----------------

The platform package provides a PyDM-based GUI entry point:

.. code-block:: python

   from axi_soc_ultra_plus_core.rfsoc_utility import pydm as rfsoc_pydm
   rfsoc_pydm.runPyDM(root=root, title='RFSoC Demo')

The GUI launcher is invoked by each application repo's ``devGui.py`` script after
constructing the ``Root`` object and calling ``root.start()``. It launches the
``pyrogue.pydm.runPyDM()`` GUI backed by the platform ZMQ server at the address
configured in ``Root.__init__``.

The ``axi_soc_ultra_plus_core.rfsoc_utility.pydm`` subpackage contains PyDM display
panels for ADC/DAC ring buffer waveforms, RFDC tile configuration, and ring buffer
control — all board-agnostic and reused across every ``Simple-*-Example`` application.
