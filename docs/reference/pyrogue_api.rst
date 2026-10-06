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

- Always pass ``**kwargs``: never enumerate base-class arguments explicitly.
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

``Rfdc.Init()`` resets every enabled tile once (DAC tiles first, and inside each
converter type the clock distribution sources before their members), waits for state 15
with the PLL locked on every tile, re-applies the settings written in this session and
raises one exception naming every tile that missed the wait. The operator commands
behave as follows:

- ``Reset`` on a tile, ``ResetAllAdc`` and ``ResetAllDac`` run the same sequence for
  one tile or one converter type, and also re-apply the tiles the restart knocked.
  Settings written in the session are written back in a fixed order. Settings not
  written in the session read their Vivado value for the registers a restart reloads;
  a mixer that was never committed is returned to the Vivado NCO, and a QMC that was
  never committed to its power-up default. A bare restart
  does not restore mixer NCO settings, so ``Reset`` followed by ``ApplyConfig``
  returns to the Vivado baseline only for settings not written in the session.
- A committed PLL (``PllConfig`` with ``PllConfigUpdate``) is written back when the
  hardware differs from it. The commit reprograms the PLL live and restarts the tile
  only from state 6 (states 1 to 5 do not run), and a tile left like that can stall at
  state 7 when a neighbouring tile restart knocks it. Every PLL commit, from the
  operator ``PllConfigUpdate`` or from the write-back, is therefore followed by a
  ``StartUp`` of the same tile (state 1 to 15, settings kept), which advances that
  tile's restart record by one, and then by the wait for every enabled tile. A commit
  on DAC tile 0 takes the same path: that tile distributes the clock to the others,
  which are waited for and re-applied if the restart knocked them.
- ``StartUp`` on a tile, ``StartUpAllAdc`` and ``StartUpAllDac`` are the default
  recovery: they restart from state 1, keep every setting and wait for every enabled
  tile. They write nothing back.
- ``ApplyConfig`` waits for every enabled tile, then writes back the settings written
  in this session on demand, without a restart unless a committed PLL differs from the
  hardware.
- Settings that were not surveyed on the board are written back only when the operator
  wrote them: the FabClkOutDiv, PwrModeSettings, calibration coefficient overrides,
  DisableFreezePin, FreezeCalibration and DACVOP settings, the threshold clear mode
  words, an external ClockSource, the QMC phase settings of blocks that are not an IQ
  pair, the DAC mixer and interpolation factor settings that the tile refuses, the ADC
  observation channel settings (``DecimationFactorObs``, ``FabRdVldWordsObs`` and the
  observation FIFO enable, which ``SetupFIFOObs`` and ``SetupFIFOBoth`` also drive),
  which were read on SlacRfmcCarrier only, and every tile other than the ones used as
  representatives.
- A setting counts as written only when its write returned without an error on an
  enabled device. A value that pyrogue or the memory slave refuses records nothing and
  keeps the setting's earlier record. A write under a disabled block or device (for
  example from ``LoadConfig`` of a file that covers every block) is ignored by the
  hardware, records nothing and also keeps the earlier record. A ``LoadConfig`` value
  is recorded once pyrogue accepts it, even if the bulk write that follows is refused.
  An unconfirmed write on an enabled device is not recorded and drops that setting's
  earlier record (for a commit, the earlier committed snapshot): a ``set()`` made with
  ``wait=False`` (or the deprecated ``check=False``) returns before its write is
  confirmed, and any write or commit made while the hidden ``IgnoreMetalError`` debug
  setting is on cannot be told from success. A later restart then returns the setting
  to its Vivado value, as for a setting never written, until a confirmed write records
  it again. A recorded setting whose device is disabled when the write-back runs, and a
  committed DAC ``Mixer`` whose block reads ``DataPathMode 4`` (full bandwidth bypass
  refuses a mixer commit) at that time, are skipped with a logged warning and are not
  reported as a tile failure.
- The FIFO words ``SetupFIFO``, ``SetupFIFOObs`` and ``SetupFIFOBoth`` of a tile, and the
  ``Rfdc`` words ``SetupFIFOAllAdc``, ``SetupFIFOAllDac``, ``SetupFIFOObsAllAdc`` and
  ``SetupFIFOBothAllAdc``, take ``False`` (0, disables) or ``True`` (3, enables). They are
  written back in the order they were written, the newest of a tile word and its
  ``Rfdc`` twin winning. The old ``UNDEFINED`` label and the value 2 are gone from the
  enum, so a saved configuration holding ``UNDEFINED`` no longer loads, while a raw
  write of 2 still enables the FIFO.
- A ``Mixer.Freq`` or ``Mixer.PhaseOffset`` ``set()`` (or GUI edit) commits the mixer
  through ``UpdateEvent`` right after its write, so no separate commit is needed. Set
  the block's ``Mixer.AutoUpdate`` (default ``True``) to ``False`` to stage several
  fields and commit them with one ``UpdateEvent``. The other mixer fields, a
  ``LoadConfig``, a ``set()`` with ``wait=False`` and ``post()`` still need an explicit
  ``UpdateEvent``, and a refused commit raises to the caller of ``set()``.
- The block ``Mixer`` device is enabled whenever the block sample rate is non-zero. It
  used to follow ``BlockStatus.MixerMode``, which reads 0 for the bypass mixers of this
  design, so a ``Mixer`` write now reaches ``PyRFdc`` and can be refused, for example on
  a DAC block in full bandwidth bypass (``DataPathMode 4``).
- ``UpdateIsEnabled()`` re-reads only the tile, block and ``Mixer`` enable chain, never
  an operator setting, and does not refresh the other variables (use ``ReadAll``). It
  returns ``None`` and gives the same result when run twice. A failed read of a tile
  enable flag (``CheckAdcTileEnabled`` or ``CheckDacTileEnabled``) raises that read's
  error directly, and every other failed read is collected into one ``RuntimeError``
  that lists them.
- ``Mts.SyncAdcTiles`` and ``Mts.SyncDacTiles`` check, at once and without waiting, that
  ``AdcTiles`` (``DacTiles``) is not zero and that every tile of the mask plus the
  reference tile (``AdcRefTile`` or ``DacRefTile``) is enabled and passes the same state
  15, Restart clear and PLL lock test as the ``Init`` wait. They then run the multi-tile
  sync once with ``IgnoreMetalError`` held off and check ``AdcMtsValid`` (``DacMtsValid``).
  A refusal names every failing tile, in the key=value form of the reset diagnostics, and
  nothing is written. ``Init`` and the ``Reset``, ``StartUp``, ``ApplyConfig`` and
  ``PllConfigUpdate`` commands never sync. ``AdcMtsValid`` and ``DacMtsValid`` read 1
  only after a successful sync. A restart of the source tile of a clock distribution
  clears the flag of every converter type that has a tile in that distribution: on
  SlacRfmcCarrier, DAC tile 0 is the source for DAC tiles 1 to 3 and ADC tile 3, so
  ``ResetAllDac`` and a DAC tile 0 restart clear both flags. A restart of member tiles,
  or of a source with no member of the other converter type, clears only the flag of
  their own converter type: a DAC tile 1 ``Reset`` clears ``DacMtsValid`` only and an ADC
  tile 2 ``Reset`` clears ``AdcMtsValid`` only. A ``SetClkDistribution`` clears every
  converter type it touches, and on a device below Gen3 every restart clears both flags.
  A flag also reads 0 while any enabled tile of its group (the tile mask plus the
  reference tile) is restarting or below state 15; a restart that ``PyRFdc`` did not
  command and that has already finished is not detected, and the read never changes the
  latched value. The member rule assumes that restarting member tiles does not disturb a
  tile of the other converter type. That holds for this layout; on another layout the
  live check, which sees only a restart still in progress, is the only safeguard.
- ``Rfdc.ClkDist`` is a read-only view of ``XRFdc_GetClkDistribution`` (Gen3 and DFE
  only). ``PyRFdc`` serves it at 0x15000 to 0x157FC from one driver read per read
  transaction, so a ``ReadAll`` refreshes it and nothing polls it. A write to the window
  is refused with ``ClkDist(): read-only``, and every address of it is refused while
  ``PyRFdc`` is not initialized. ``Status`` reads ``Unsupported`` with every other field 0
  below Gen3, and ``GetFailed`` with every other field 0 when the driver refuses the call.
  ``Count`` bounds the valid ``Distribution[d]`` entries; each gives the source tile, the
  two edge tiles, ``DistRefClkFreq`` (MHz), ``DistributedClock`` and the per-tile sample
  rates (MSPS). The ``AdcTileClk[t]`` and ``DacTileClk[t]`` views name the one
  distribution that holds the tile and give its source, PLL enable, division factor,
  delay, reference clock and sample rate. A reported sample rate comes from the tile PLL
  FS register and can lag after a ``Reset`` that followed a PLL change, so
  ``BlockStatus.SampleRate`` is the live block rate.
- ``Rfdc.ClkDist.SetClkDistribution`` changes the clock distribution (Gen3 and DFE only) as
  one explicit command. Stage the settings with the hidden ``RefreshStaging(<entry>)``,
  which loads the ``Set`` variables (``SetSourceType``, ``SetSourceTileId``,
  ``SetEdgeType``, ``SetEdgeTileId``, ``SetDistributedClock``, ``SetDistRefClkFreq``,
  ``SetAdcSampleRate`` and ``SetDacSampleRate``) from entry ``<entry>`` of
  ``Distribution`` and refuses an entry that does not exist. Edit them, read
  ``PreviewStatus`` and the ``PreviewAdcAffected``, ``PreviewDacAffected``,
  ``PreviewAdcSpan`` and ``PreviewDacSpan`` tile masks, which show what ``PyRFdc`` would
  restart or why it would refuse, then run ``SetClkDistribution``. The hidden
  ``SetClkDistributionRaw`` is the bare commit with no wait and no write-back; ``PyRFdc``
  still prechecks it, and a refusal is never hidden by ``IgnoreMetalError``. The ``Last``
  masks show what the last commit restarted and read 0 after a refused one. The Set
  variables are staging words only: nothing reaches a tile before the commit, and none of
  them is written back by ``Init`` or ``ApplyConfig``.

  - ``ShutdownMode`` is always disabled, so every enabled tile of the distribution is
    started up to state 15 inside the commit. A nonzero ``SetShutdownMode`` is refused.
  - The affected tiles are the new span plus every current distribution that holds the
    new source or a tile of the span; a neighbour that only touches the span is left
    alone. A same-value Set is not a no-op: it restarts its whole span. A Set that would
    leave an enabled tile of such a distribution without any clock is refused and the
    tile is named. A span tile that is not enabled, a type, tile number or
    ``DistributedClock`` out of range, an illegal span, and a sample rate or reference
    clock that is not a finite number inside the limits of its tile are refused too, all
    before the driver is called.
  - A busy affected tile (Restart stays set while the state machine moves) refuses the
    whole Set, naming every busy tile, and nothing is restarted. A parked tile (Restart
    stays set with the state frozen) proceeds with a logged warning and the parked flag
    in its ``ResetRecord``. Each tile of the distribution gets one ``ResetRecord`` with
    command 5, read as ``SetClkDistribution``.
  - The sequence waits for every enabled tile to reach state 15 with its PLL locked,
    writes back the settings written in this session on the tiles the commit restarted
    (the ``Last`` affected masks) and on the knocked tiles, and raises one error that
    names every tile that did not get there; a refused Set writes nothing back. It clears
    ``AdcMtsValid`` and ``DacMtsValid`` of every converter type that has an affected
    tile, and drops the committed PLL record of every restarted tile, so a later
    ``Reset`` does not replay an old ``PllConfigUpdate`` on top of the new clocking.
  - A Set is transient. ``Init`` and every ``Reset`` restart from state 0 and return the
    tiles to the Vivado clocking, and nothing re-applies a Set, so run it again after
    ``Init`` when it is wanted. While a distribution other than the Vivado one is active,
    partial resets (one tile, or one converter type) stay allowed and the state 15 wait
    names every tile that fails; ``Init`` of both converter types is the recovery and
    needs no power cycle. ``Init``, ``Reset`` and ``StartUp`` never run a Set.
  - The longest Set restarts all eight tiles and can spend up to 25 s in the driver's own
    waits, which is why the ``Rfdc`` transaction timeout floor is 32 s.

- The hidden ``ResetRaw``, ``StartUpRaw``, ``ResetAllAdcRaw``, ``ResetAllDacRaw``,
  ``StartUpAllAdcRaw`` and ``StartUpAllDacRaw`` commands are bare restarts: they do not
  wait for the other tiles and write nothing back. The hidden ``SyncAdcTilesRaw`` and
  ``SyncDacTilesRaw`` commands are the bare multi-tile sync: they do not check the mask,
  and ``PyRFdc`` still refuses them while a tile of the group is not ready.
- ``Rfdc`` raises its own transaction timeout to 32 s, so an application does not need
  a large Root timeout. The floor covers a Set of the largest span: 8 tiles x 3 s of
  driver waits plus 1 s for the source, plus 25 percent margin.

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
control: all board-agnostic and reused across every ``Simple-*-Example`` application.
