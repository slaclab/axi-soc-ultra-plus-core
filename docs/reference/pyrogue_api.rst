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
  value generated in the VHDL crossbar, minus the parent's base address (crossbars built
  with ``genAxiLiteConfig(..., AXIL_BASE_ADDR_G, ...)`` hold absolute addresses).

AxiSocCore
----------

:repo:`python/axi_soc_ultra_plus_core/_AxiSocCore.py`

``AxiSocCore`` is the platform core PyRogue ``Device``. It maps the ``AxiSocUltraPlusReg``
register block of the ``AxiSocUltraPlusCore`` RTL; the DMA engine registers are not mapped.

Responsibilities:

- Exposes the AXI-Lite register space of ``shared/rtl/AxiSocUltraPlusReg.vhd``.
- Provides ``AxiVersion`` (``0x0_0000``), the ``AxiSysMonUltraScale`` system monitor
  (``0x1_0000``), the hidden ``SysMonLvAuxDet`` LVAUX detector (``0x1_1000``), and the
  ``DmaIbAxisMon`` / ``DmaObAxisMon`` DMA AXI stream monitors (``0x2_0000`` / ``0x3_0000``).
- Takes ``numDmaLanes`` (default 1), which sizes the DMA monitors and is checked against
  ``AxiVersion.DMA_SIZE_G`` at start (a mismatch prints a warning).
- Acts as the first-level child of the application ``RFSoC`` device at a fixed offset
  determined by the platform crossbar.

Typical instantiation (inside the application ``RFSoC`` device):

.. code-block:: python

   import axi_soc_ultra_plus_core as soc_core

   self.add(soc_core.AxiSocCore(
       offset      = 0x0000_0000,
       numDmaLanes = 2, # Must match DMA_SIZE_G
   ))

AppRingBuffer
-------------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_AppRingBuffer.py`

``AppRingBuffer`` is the PyRogue ``Device`` that controls the ADC and DAC capture ring
buffers in the RTL. It provides AXI-Lite register access to the ring buffer control and
status, and is the source of the DMA inbound data stream.

Responsibilities:

- Holds one surf ``AxiStreamRingBuffer`` per channel (``Adc.Ch[i]`` at ``0x0_0000``,
  ``Dac.Ch[i]`` at ``0x1_0000``, ``0x1000`` apart) with trigger control (``TrigBurst``,
  ``ContinuousMode``). The depth is fixed at build time (``RAM_ADDR_WIDTH_G``, read only)
  and there are no per-channel enables.
- Reports capture status (``BufferLength``, ``TrigCnt`` and the trigger and data FSM
  states); there are no fill level or overrun flags.
- Limits the outgoing frame rate with the ``RateLimiter`` (``AxiStreamFrameRateLimiter``
  at ``0x2_0000``). The frames reach the host-side ``RingBufferProcessor`` on the DMA
  streams (ADC channels on TDEST ``0x00`` to ``0x0F``, DAC channels on ``0x10`` to ``0x1F``).

``AppRingBuffer`` is instantiated inside the application ``Application`` device, not directly
in ``AxiSocCore``. Each application repo sets the ``numAdcCh`` and ``numDacCh`` arguments to
match the board's RFDC configuration.

RfdcCalFreeze
-------------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcCalFreeze.py`

``RfdcCalFreeze`` is the PyRogue ``Device`` for the ``RfdcCalFreeze`` RTL block, which
drives the RF-ADC ``int_cal_freeze`` pins. The RF-ADC background calibration (GCB and
TSCB) trains on the input signal and drifts when the input drops below -40 dBFS, so the
product guide (`PG269 <https://docs.amd.com/r/en-US/pg269-rf-data-converter>`__) asks for
it to be frozen while the input is absent for longer than 100 us.
The block freezes a channel from either of two sources: a ``ManualFreeze`` register bit,
or a per-channel signal detector on the ADC samples (``AutoEnable``) that holds the
calibration while the input is absent and releases it as soon as a signal is back.

A *channel* is one RF-ADC converter, a block of the RF Data Converter IP core, not a
tile. It is the unit with its own sample stream and its own ``int_cal_freeze``,
``cal_frozen`` and ``sig_detect`` pins; a dual-ADC tile holds two of them and a quad-ADC
tile four. Channel ``c`` is whatever the application wired to the ``adcI<c>``/``adcQ<c>``
ports of the firmware, and bit ``c`` of every per-channel register refers to it.

The signal detector works in units of *windows*:

- ``Window``: the magnitude is averaged over back to back windows of ``2^Window`` DSP
  clock cycles. The DSP clock is the clock the ADC sample stream arrives on,
  ``SAMPLE_PER_CYCLE_G`` samples per cycle, so a window covers
  ``2^Window * SAMPLE_PER_CYCLE_G`` samples. At the end of every window the mean
  magnitude per sample is compared with ``Threshold`` and published in ``Magnitude[c]``.
  With a 307.2 MHz DSP clock and ``Window = 15`` a window is 32768 cycles, 106.7 us,
  the shortest power of two above the product guide's 100 us.
- ``Threshold``: the mean magnitude below which a window counts as "no signal", on a
  16-bit full scale. A -40 dBFS tone gives a mean ``|I|+|Q|`` of 417 in I/Q mode and a
  mean ``|x|`` of 209 in real mode; the default of 416 freezes below that level.
- ``HoldOff``: the number of consecutive "no signal" windows before the detector asserts
  the freeze. The freeze lands between ``HoldOff`` and ``HoldOff + 1`` windows after the
  signal disappears, since the window in progress may still average above ``Threshold``.
  A single window at or above ``Threshold`` clears the count and releases the freeze, so
  the release lands within two windows of the signal coming back. ``HoldOff = 0``
  behaves as 1.

The polarity is "freeze while the signal is low": the calibration runs while there is
signal to train on and is held while the input is absent, which is when it would drift.
With the defaults (``Window = 15``, ``HoldOff = 1``, 307.2 MHz) a channel freezes
107 to 213 us after its input drops below -40 dBFS and releases within 213 us of the
input returning. ``AutoFreeze`` reads the detector state per channel, ``CalFreeze`` the
pin actually driven (``ManualFreeze`` or ``AutoEnable and AutoFreeze``), and
``CalFrozen`` the ``cal_frozen`` acknowledge from the RF-ADC.

PG269 references:

- The -40 dBFS / 100 us freeze rule, stated for each block:
  `Gain Calibration Block (GCB)
  <https://docs.amd.com/r/en-US/pg269-rf-data-converter/Gain-Calibration-Block-GCB>`__ and
  `Time Skew Calibration Block (TSCB)
  <https://docs.amd.com/r/en-US/pg269-rf-data-converter/Time-Skew-Calibration-Block-TSCB>`__.
- The freeze mechanism (active-High freeze, about 7 us to take effect):
  `Background Calibration Process
  <https://docs.amd.com/r/en-US/pg269-rf-data-converter/Background-Calibration-Process>`__.
- The ``int_cal_freeze``, ``cal_frozen`` and ``sig_detect`` pins:
  `Calibration Freeze Ports for Dual RF-ADC Tiles
  <https://docs.amd.com/r/en-US/pg269-rf-data-converter/Calibration-Freeze-Ports-for-Dual-RF-ADC-Tiles>`__
  and `Calibration Freeze Ports for Quad RF-ADC Tiles
  <https://docs.amd.com/r/en-US/pg269-rf-data-converter/Calibration-Freeze-Ports-for-Quad-RF-ADC-Tiles>`__.

Responsibilities:

- Exposes the build-time generics read only (``NUM_CH_G``, ``SAMPLE_PER_CYCLE_G``,
  ``IQ_MODE_G``, ``COMMON_CLK_G``, ``WINDOW_MAX_C``) so software can check the firmware
  configuration it is talking to.
- Holds the per-channel bitmask control and status registers (``ManualFreeze``,
  ``AutoEnable``, ``CalFrozen``, ``AutoFreeze``, ``SigDetect``, ``CalFreeze``) and the
  per-channel ``Magnitude[i]`` readback of the last detector window.
- In I/Q mode (``IQ_MODE_G = true``) the firmware takes I and Q on separate ``adcI<c>`` and
  ``adcQ<c>`` ports and ``Magnitude`` is the mean of ``|I|+|Q|`` per complex sample. In real
  mode only ``adcI<c>`` is used and ``Magnitude`` is the mean of ``|x|`` per sample.

The RF-ADC honours the freeze pin only while the PS driver's
``Calibration.DisableFreezePin`` of the block (see the ``RfdcBlock`` device) is 0, its
default. The ``numCh`` argument must match the ``NUM_CH_G`` generic:

.. code-block:: python

   self.add(rfsoc_utility.RfdcCalFreeze(
       name   = 'RfdcCalFreeze',
       offset = 0x0020_0000,
       numCh  = 4, # Must match NUM_CH_G
   ))

RingBufferProcessor
-------------------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RingBufferProcessor.py`

``RingBufferProcessor`` is a host-side Rogue stream receiver. It sits at the end of the
DMA TCP stream pipeline and processes captured ADC or DAC frames for display (waveform
and averaged FFT magnitude). File storage is done by the ``Root`` ``StreamWriter`` on a
separate branch of the same stream.

The stream is wired in the application ``Root`` using the Rogue ``>>`` operator:

.. code-block:: python

   # Connect TCP stream → drop FIFO → ring buffer processor
   self.ringBufferAdc[i] >> self.adcDropFifo[i] >> self.adcProcessor[i]

The drop FIFO (``maxDepth=1``) prevents host-side backpressure from stalling the DMA
path inside the firmware.

RFDC API
--------

:repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py`

The ``Rfdc`` device (and its children ``RfdcTile`` and ``RfdcBlock``) is the host-side
mapping of the board-side ``PyRFdc`` memory slave
(``shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp``), which calls the Xilinx RFdc
driver API (``XRFdc_*``) rather than exposing the IP core register map. It exposes:

- Per-tile and per-block ADC/DAC configuration registers.
- Multi-Tile Synchronization (MTS) control.
- Sample rate, decimation/interpolation factor, and mixer frequency settings.

The ``Rfdc`` device is added directly to the application ``Root``, with ``memBase`` a
``rogue.interfaces.memory.TcpClient`` to port 9002, where ``roguetcpbridge`` on the board
serves ``PyRFdc``. It is not behind the AXI-Lite crossbar: index 1 of the
``AxiSocUltraPlusReg`` crossbar holds only the read-only ``PYRFDC_CONFIG`` ROM that
``roguetcpbridge`` reads to construct ``PyRFdc``. Application ``Root.start()`` sequences
clock initialization before calling ``Rfdc.Init()`` to program the IP core.

``Rfdc.Init()`` resets every enabled tile once (DAC tiles first, and inside each
converter type the clock distribution sources before their members), waits for state 15
with the PLL locked on every tile, re-applies the settings written in this session, and
raises one exception naming every tile that missed the wait. The operator commands
behave as follows:

- ``Reset`` on a tile, ``ResetAllAdc`` and ``ResetAllDac`` run the same sequence for
  one tile or one converter type, and also re-apply any tile the restart knocked (see
  :doc:`../explanation/pyrfdc_reset`). Settings written in the session are written
  back in a fixed order. A register that a restart reloads returns to its Vivado value
  when the session never wrote it; a mixer that was never committed is returned to the
  Vivado NCO, and a QMC that was never committed to its power-up default. A bare
  restart does not restore mixer NCO settings, so ``Reset`` followed by ``ApplyConfig``
  returns to the Vivado baseline only for settings not written in the session.
- A committed PLL (``PllConfig`` with ``PllConfigUpdate``) is written back when the
  hardware differs from it. The commit reprograms the PLL live and restarts the tile
  only from state 6 (states 1 to 5 do not run), and a tile left like that can stall at
  state 7 when a neighboring tile restart knocks it. Every PLL commit, from the
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
  wrote them:

  - the FabClkOutDiv, PwrModeSettings, calibration coefficient override,
    DisableFreezePin, FreezeCalibration and DACVOP settings
  - the threshold clear mode words
  - an external ClockSource
  - the QMC phase settings of blocks that are not an IQ pair
  - the DAC mixer and interpolation factor settings that the tile refuses
  - the ADC observation channel settings (``DecimationFactorObs``, ``FabRdVldWordsObs``
    and the observation FIFO enable, which ``SetupFIFOObs`` and ``SetupFIFOBoth`` also
    drive), which were read on SlacRfmcCarrier only
  - every tile other than the ones used as representatives
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
  A sync on a disabled ``Rfdc`` device is refused at once, before any tile is checked.
  A refusal names every failing tile, in the key=value form of the reset diagnostics, and
  nothing is written. ``Init`` and the ``Reset``, ``StartUp``, ``ApplyConfig`` and
  ``PllConfigUpdate`` commands never sync. ``AdcMtsValid`` and ``DacMtsValid`` read 1
  only after a successful sync. A restart of the source tile of a clock distribution
  clears the flag of every converter type that has a tile in that distribution: on
  SlacRfmcCarrier, DAC tile 0 is the source for DAC tiles 1 to 3 and ADC tile 3, so
  ``ResetAllDac`` and a DAC tile 0 restart clear both flags. A restart of member tiles,
  or of a source with no member of the other converter type, clears only the flag of
  their own converter type: a DAC tile 1 ``Reset`` clears ``DacMtsValid`` only and an ADC
  tile 2 ``Reset`` clears ``AdcMtsValid`` only. On a device below Gen3 every restart
  clears both flags. A flag also reads 0 while any enabled tile of its group (the tile
  mask plus the reference tile) is restarting or below state 15; a restart that ``PyRFdc`` did not
  command and that has already finished is not detected, and the read never changes the
  latched value. The member rule assumes that restarting member tiles does not disturb a
  tile of the other converter type. That holds for this layout; on another layout the
  live check, which sees only a restart still in progress, is the only safeguard.
- The hidden ``ResetRaw``, ``StartUpRaw``, ``ResetAllAdcRaw``, ``ResetAllDacRaw``,
  ``StartUpAllAdcRaw`` and ``StartUpAllDacRaw`` commands are bare restarts: they do not
  wait for the other tiles and write nothing back. The hidden ``SyncAdcTilesRaw`` and
  ``SyncDacTilesRaw`` commands are the bare multi-tile sync: they do not check the mask,
  and ``PyRFdc`` still refuses them while a tile of the group is not ready.
- ``Rfdc`` raises its own transaction timeout to 32 s, so an application does not need
  a large Root timeout. The longest single transaction is the restart of all four tiles
  of one converter type: 4 tiles × 3 s of driver waits = 12 s. The floor stays at 32 s,
  well above the 12 s, so existing applications see no change in timeout behavior.

Source files:

- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_Rfdc.py`
- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py`
- :repo:`python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcBlock.py`

PyDM GUI launcher
-----------------

The platform package provides a PyDM-based GUI entry point:

.. code-block:: python

   from axi_soc_ultra_plus_core.rfsoc_utility import pydm as rfsoc_pydm
   rfsoc_pydm.runPyDM(serverList='localhost:9099', ui=uiFile, title='RFSoC Demo')

Its signature is ``runPyDM(serverList='localhost:9090', ui=None, title=None, sizeX=800,
sizeY=1000, maxListExpand=5, maxListSize=100, numAdcCh=1, numDacCh=1)``: it connects to
a running Rogue server through ``serverList`` and takes no ``root`` argument. Its default
``ui`` is a ``pydmTop.py`` that the package does not ship, so pass ``ui`` explicitly.

The reference application's ``devGui.py`` does not use it. It constructs the ``Root`` in
a ``with`` block (which starts it) and calls ``pyrogue.pydm.runPyDM()`` with
``serverList=root.zmqServer.address`` and a ``display_factory`` that builds ``GuiTop``
with ``numAdcCh`` and ``numDacCh``. The ZMQ server is the ``pyrogue.interfaces.ZmqServer``
that the application ``Root.__init__`` adds (``127.0.0.1``, port ``zmqSrvPort``, default
9099).

The ``axi_soc_ultra_plus_core.rfsoc_utility.pydm`` subpackage contains only ``runPyDM``.
The display panels live in ``axi_soc_ultra_plus_core.rfsoc_utility.gui``: ``GuiTop`` has
System, Debug Tree, ADC Waveforms and DAC Waveforms tabs, the waveform tabs being
``LiveDisplay`` panels of the ``RingBufferProcessor`` data. They are board-agnostic (the
channel counts come from ``numAdcCh`` and ``numDacCh``) and reused across every
``Simple-*-Example`` application.
