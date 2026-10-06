PyRFdc Reset and Restart Process
================================

This page explains how a tile restart works end to end: what the operator commands do,
how the board-side ``PyRFdc`` memory slave drives the Xilinx RFDC driver, and how the
host-side ``Rfdc`` PyRogue device waits for the tiles and writes the session's settings
back. The per-command behavior and the variable list are in
:doc:`../reference/pyrogue_api`.

Why a restart needs more than one driver call
---------------------------------------------

``XRFdc_Reset`` restarts a tile's power-on state machine from state 0. On the way back
to state 15 the IP reloads most of the tile's registers with the values Vivado built
into the bitstream, so every setting the operator wrote since boot is lost. A few
registers (DSA, thresholds, coarse delay, the QMC and mixer among them) keep their
value across the restart instead.

A useful restart therefore has four parts, split across two layers:

- **PyRFdc** (C++, on the board, port 9002) runs exactly one prechecked driver call per
  tile and records the outcome. It never replays settings into the tile.
- **Rfdc** (Python, on the host) owns the sequence: it issues the raw restart, waits for
  every enabled tile to reach state 15, writes back the settings recorded in this
  process, and reads the tiles back so the GUI shows the hardware.

Commands
--------

Each public command is a ``LocalCommand`` that runs the common sequence described
below. The hidden ``*Raw`` commands are the bare ``PyRFdc`` restart words, with no wait
and no write-back.

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Command
     - Raw restart
     - Write-back
   * - ``Rfdc.Init``
     - ``ResetAllDacRaw``, then ``ResetAllAdcRaw``
     - Every enabled tile, then a read of the whole ``Rfdc`` subtree
   * - ``RfdcTile.Reset``
     - ``ResetRaw`` (state 0 to 15)
     - The tile, plus any tile the restart knocked
   * - ``ResetAllAdc`` / ``ResetAllDac``
     - ``ResetAllAdcRaw`` / ``ResetAllDacRaw``
     - Every enabled tile of that type, plus any knocked tile
   * - ``RfdcTile.StartUp``, ``StartUpAllAdc`` / ``StartUpAllDac``
     - ``StartUpRaw`` and the like (state 1 to 15)
     - None: a StartUp keeps the settings, so it is the default recovery action
   * - ``ApplyConfig``
     - None (unless a committed PLL differs from the hardware)
     - Every enabled tile, then a read of the whole subtree
   * - ``PllConfig.PllConfigUpdate``
     - PLL commit, then ``StartUpRaw`` of the same tile
     - The tile, plus any knocked tile
   * - ``ClkDist.SetClkDistribution``
     - The clock distribution commit
     - The tiles the commit restarted, plus any knocked tile

``CustomStartUp`` (per tile, and ``CustomStartUpAllAdc`` / ``CustomStartUpAllDac``) is a
raw command only. It packs StartState into bits 3:0 and EndState into bits 7:4 of one
word, so ``PyRFdc`` runs exactly one ``XRFdc_CustomStartUp`` per tile with exactly those
two states.

The restart sequence
--------------------

``Rfdc._runSequence`` is the common sequence behind every public command in the table.

1. **Lock.** Take the ``Rfdc`` sequence lock, then pause polling
   (``root.pollBlock()``). Every raw restart command takes the same lock, so two clients
   cannot interleave a sequence, and polling cannot read a tile mid-restart.
2. **Snapshot.** Refresh the tile enable flags, then record each enabled tile's
   ``FailureCount`` and ``ResetCount``.
3. **Raw restart.** Issue the raw command words. A failure is collected; the sequence
   continues, so the wait and the report still cover every tile.
4. **State 15 gate.** Wait for every enabled tile, not only the commanded ones (see
   below).
5. **Knock detection.** Compare ``ResetCount`` with the snapshot to find tiles the
   restart disturbed (see below).
6. **Enable chain.** Re-read the block and mixer enables of the target tiles, so the
   write-back sees which devices exist after the restart.
7. **Write-back.** Write the recorded settings to the commanded tiles that passed the
   gate, plus the knocked tiles that passed (StartUp commands skip this step).
8. **Mirror refresh.** Read the target tiles back (``Init`` and ``ApplyConfig`` read the
   whole subtree).
9. **Report.** Release the lock, then raise one ``RuntimeError`` if anything failed. It
   names the failed tiles (ADC 0 to 3, then DAC 0 to 3), followed by each gate miss
   diagnostic, each raw command error text, and each enable, write-back and mirror
   error.

A tile that misses the gate is reported and is never restarted again by the same
command.

Inside PyRFdc: one raw restart
------------------------------

``PyRFdc::RestartTiles`` handles Reset, StartUp, CustomStartUp and Shutdown, for one
tile or for every enabled tile of a converter type (``Tile_Id`` -1 is never passed to
the driver):

1. **MTS invalidation.** Clear ``AdcMtsValid`` / ``DacMtsValid`` for every converter type
   the restarted tiles feed. The operator's sync masks and reference tiles are kept.
2. **Ordering.** For an all-tile restart, read the clock distribution topology and visit
   the tiles that source a distribution first, then the others (``RestartOrder``). A
   member restarted while its source is down stops at state 6 with no clock until the
   driver times out. A Shutdown uses the reverse order.
3. **Precheck.** If the tile's Restart register (0x04) is set, poll it for up to
   1000 x 1 ms (the driver's own restart-clear budget). Three outcomes:

   - *clear*: proceed.
   - *busy* (CurrentState moved during the wait): refuse the tile, record result 3, and
     raise ``txnRefused_`` so ``IgnoreMetalError`` cannot swallow the refusal. Nothing
     is written.
   - *parked* (CurrentState frozen): log a warning and restart from the requested start
     state, with the parked flag in the record.

4. **One driver call.** ``XRFdc_Reset``, ``XRFdc_StartUp``, ``XRFdc_CustomStartUp`` or
   ``XRFdc_Shutdown``, explicit tile id, exactly once.
5. **Driver cache resync.** After a restart from state 0, reset the driver's software
   copies of the PLL settings and the per-block mixer type and frequency to what
   ``XRFdc_CfgInitialize`` would set, so the instance matches a freshly constructed
   one. No register is written.
6. **Staging refresh.** If the restart ends at state 15, re-read the clock source, PLL,
   QMC and mixer settings into the staging words that ``PllConfig``, ``QMC`` and
   ``Mixer`` read. This runs only when the tile is at state 15 with Restart clear;
   otherwise the record gets the "staging not refreshed" flag.
7. **Restart record.** Write the outcome to the tile's ``ResetRecord`` (Shutdown is not
   recorded). On a failure, also capture CurrentState, Common Status (0x228) and the
   clock detector (0x84) before ``IgnoreMetalError`` can hide the error.

A failure on one tile does not stop the loop. All failures are reported together in
tile order, followed by the driver log lines captured during the call.

The state 15 gate
-----------------

``Rfdc._gate`` polls every enabled ADC and DAC tile against one shared deadline
(``GATE_TIMEOUT_S`` = 5 s, polled every ``GATE_POLL_S`` = 50 ms). A tile is ready when:

- ``CurrentState`` reads 15,
- ``RestartStatus`` reads 0, and
- ``PllStatus.PllLocked`` reads 1, if the tile runs from its internal PLL. A tile on an
  external clock has no lock term.

The gate covers every enabled tile because a restart can disturb tiles that were not
commanded, and a tile is not safe to write until it is back at state 15. ``PyRFdc``
enforces the same rule by itself: gated writes are refused while a tile in their scope
reads Restart set or CurrentState below 15. The gate never writes and never restarts. The
settle time of each tile and the list of misses are kept in ``Rfdc._lastGate``.

Knocked tiles
-------------

Restarting one tile can restart others: the DAC tile that sources a clock distribution
restarts every tile that runs from it (on SlacRfmcCarrier, DAC tile 0 feeds DAC tiles 1
to 3 and ADC tile 3). Those tiles reload their Vivado registers too, so their settings
must be written back as well.

``ResetCount`` (tile register 0x38) counts the IP's automatic restarts and saturates at
255. A software restart does not increment it, so a change across the command marks a
knocked tile. A tile that reads 255, or whose count cannot be read, is treated as
knocked. Knocked tiles that passed the gate join the write-back targets and are listed
in ``_lastGate['knocked']``.

What gets written back
----------------------

**What counts as written.** A ``ConfigVariable`` (the ``RemoteVariable`` subclass used
for every re-applied setting) records its value when an operator ``set()``, a GUI edit
or ``LoadConfig`` writes it. The value is recorded only when the write is confirmed,
on an enabled device, with ``IgnoreMetalError`` off:

- An unconfirmed write (``wait=False``, or any write under ``IgnoreMetalError``) drops
  the earlier record, because the hardware may now hold a value that was never
  recorded.
- A write that raises, or that lands on a disabled device, keeps the earlier record.

For the ``QMC``, ``Mixer`` and ``PllConfig`` groups, ``recordCommit`` stores a snapshot
of every field when the operator's commit (``UpdateEvent`` or ``PllConfigUpdate``)
succeeds. Only that snapshot is ever re-applied, never the uncommitted staging values.

The write-back itself goes through ``pr.RemoteVariable.set``, which bypasses the
record, so re-applying a setting never marks it as written.

**Order.** Settings are written back tile by tile (DAC tiles first, then ADC tiles), in
the step order of ``Rfdc.APPLY_ORDER``:

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Step
     - Contents and reason
   * - ``pll``
     - The committed tile PLL, before any other write, because a PLL commit restarts
       the tile. It is written only when the hardware differs from the snapshot (clock
       source, reference clock, sample rate), and each commit is followed by a
       ``StartUpRaw`` of the tile and a second gate.
   * - ``rates``
     - ``FabClkOutDiv``, interpolation and decimation factors, fabric words,
       ``DataPathMode``, ``IMRPassMode``, then the tile FIFO enables
   * - ``calibration``, ``nyquist``, ``dsa``
     - Calibration mode, Nyquist zone, DSA
   * - ``qmc``, ``mixer``
     - After the rates, Nyquist zone and datapath mode, because
       ``XRFdc_SetMixerSettings`` reads them and resets the internal FIFO width
   * - ``other``
     - Everything else, ending with the calibration overrides, power mode and DACVOP

Some writes need special handling:

- **Shared words.** Fields that share one hardware word (``CoarseDelay`` and its event
  source, the two ``ThresholdClrMode`` fields) are written together, once, with every
  field current.
- **FIFO words.** The tile FIFO words and their ``Rfdc`` all-tile twins
  (``SetupFIFOAllAdc`` and the like) overlap, so they are replayed in the order they
  were written; the newest write wins.
- **PLL StartUp.** A PLL commit reprograms the PLL live and restarts the tile only from
  state 6, which leaves the tile able to stall at state 7 when a neighbor's restart
  knocks it. The ``StartUpRaw`` after the commit runs one full pass through states 1
  to 15, which clears that condition.

**Settings nobody wrote.** A setting that a restart reloads simply reads its Vivado
value afterwards; nothing is written. Settings that survive a restart need a default to
return to:

- ``Rfdc.SURVIVOR_DEFAULTS`` holds the after-reboot value of each surviving register
  (DSA, thresholds, coarse delay, DAC data scaler, FIFO enable). The default is written
  only when the hardware reads differently.
- A ``QMC`` that was never committed is returned to ``QMC_POWERUP_DEFAULT``.
- A ``Mixer`` that was never committed is returned to the Vivado NCO frequency from the
  config ROM, with phase offset 0.

Both group defaults run only after one ``RefreshStaging`` of the tile, which loads the
restart-restored values into staging.

**Skips.** A recorded setting whose device is disabled when the write-back runs is
skipped, with a logged warning and a note in ``Rfdc._lastApply``. The same applies to a
committed DAC mixer whose block reads ``DataPathMode`` 4 (full bandwidth bypass refuses a
mixer commit). A skip is not a tile failure. The full list of writable variables that are
deliberately not re-applied, with the reason for each, is ``Rfdc.APPLY_EXCLUDED``.

Init
----

``Rfdc.Init`` is the restart the application runs at start-up, after the board clocks
are programmed:

1. It refuses to run unless ``ConfigStatus`` reads ``Ok`` (see below), raising a
   ``ValueError`` that carries ``ConfigMessage``.
2. It forces ``IgnoreMetalError`` off for the whole sequence and restores the saved
   value afterwards, whatever the outcome.
3. It runs the common sequence with two raw steps: ``ResetAllDacRaw`` first, because a
   DAC tile can source the clock distribution that feeds ADC tiles, then
   ``ResetAllAdcRaw``. Within each type, ``PyRFdc`` restarts the distribution sources
   first. Each enabled tile is reset once.

``Init`` never runs MTS sync: ``Mts.SyncAdcTiles`` and ``Mts.SyncDacTiles`` are explicit
operator steps after the restart.

ConfigStatus
++++++++++++

``PyRFdc`` reads the RFDC configuration from a read-only config ROM in the bitstream and
calls ``XRFdc_CfgInitialize`` only when the ROM validates. The status is decided once at
process start. While it is not ``Ok``, every register outside the config status block
(0x14000 to 0x141FC) and a few driver-free registers is refused.

.. list-table::
   :header-rows: 1
   :widths: 8 22 70

   * - Code
     - Name
     - Meaning
   * - 0
     - NotLoaded
     - Initial value only; never read once construction completes
   * - 1
     - Ok
     - ROM valid and driver initialized
   * - 2
     - Missing
     - No ROM could be read, or too few bytes to read the magic word
   * - 3
     - BadMagic
     - First word is not the magic value: the bitstream has no config ROM
   * - 4
     - BadVersion
     - ROM format version does not match this build of ``PyRFdc``
   * - 5
     - BadSize
     - Declared payload size does not match ``sizeof(XRFdc_Config)`` for this librfdc,
       or fewer bytes were read than declared
   * - 6
     - IpVersionMismatch
     - Live RFDC IP version register disagrees with the ROM header
   * - 7
     - TileEnableMismatch
     - Live tile-enable register disagrees with the ROM payload
   * - 8
     - DriverBringUpFailed
     - Reserved and no longer produced: a driver bring-up failure on a valid ROM now
       makes ``PyRFdc`` construction throw, and port 9002 stays unbound
   * - 9
     - BadHash
     - Payload hash does not match the hash in the ROM header

Codes 2 to 7 and 9 are fixed by rebuilding the firmware so the bitstream carries a
matching config ROM.

Restart records and diagnostics
-------------------------------

**ResetRecord.** Each tile exposes a read-only restart record served by ``PyRFdc``:

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Register
     - Offset
     - Contents
   * - ``ResetCount``
     - 0x814
     - Automatic restart count (tile register 0x38), saturates at 255
   * - ``ResetRecord``
     - 0x818
     - Sequence [31:16], command [11:8] (1 Reset, 2 StartUp, 3 CustomStartUp, 5
       SetClkDistribution), flags [7:4] (0x10 parked, 0x20 staging not refreshed),
       result [3:0] (1 ok, 2 failed, 3 refused: state machine busy)
   * - ``ResetRecordStateAtFailure``
     - 0x81C
     - CurrentState at the last failure
   * - ``ResetRecordCommonStatusAtFailure``
     - 0x820
     - Common Status (0x228) at the last failure
   * - ``ResetRecordClockDetectorAtFailure``
     - 0x824
     - Clock detector (0x84) at the last failure; 0xFF below Gen3

Every restart command reads the live sequence number just before it writes its word,
then decodes the new record afterwards (success or failure) into the sticky
``LastResetResult``, ``StateAtFailure`` and ``FailureCount`` variables. Because of that
baseline, a ``PyRFdc`` restart (which starts the sequence numbers at 0 again) cannot hide
a record, and re-reading an unchanged record never counts a failure twice.

**Diagnostic line.** A restart failure in ``PyRFdc`` and a gate miss in ``Rfdc`` produce
the same one-line diagnostic per tile:

.. code-block:: text

   <Op> <ADC|DAC> tile <n>: <call> failed; CurrentState=<n> ClockPresent=<0|1>
   SupplyUp=<0|1> PowerUp=<0|1> PllLocked=<0|1> ClkDet=<0|1|NA> ClkSrc=<name>

``<call>`` is the driver call (``XRFdc_Reset`` and the like), ``RestartPrecheck`` for a
busy refusal, or ``StateGate`` for a tile that missed the state 15 wait. The keys map
onto the ``RfdcTile.PllStatus`` variables:

.. list-table::
   :header-rows: 1
   :widths: 20 30 50

   * - Key
     - Variable
     - Source
   * - ClockPresent
     - ``PllStatus.ClockPresent``
     - Common Status (0x228) bit 0
   * - SupplyUp
     - ``PllStatus.SupplyStable``
     - Common Status (0x228) bit 1
   * - PowerUp
     - ``PllStatus.PoweredUp``
     - Common Status (0x228) bit 2
   * - PllLocked
     - ``PllStatus.PllLocked``
     - Common Status (0x228) bit 3
   * - ClkDet
     - ``PllStatus.ClockDetector``
     - Clock detector (0x84), Gen3 and DFE only; ``NA`` below Gen3
   * - ClkSrc
     - ``PllStatus.ClockSource``
     - ``XRFdc_GetClockSource``

Timeouts
--------

A restart transaction can block in ``PyRFdc`` far longer than the usual PyRogue
timeout. For each tile, the driver can spend up to 1 s in the restart precheck, 1 s
reaching state 1 and 1 s reaching state 15. The longest single transaction is a clock
distribution Set over all eight tiles: 8 x 3 s, plus 1 s for the source tile, plus
margin. ``Rfdc._start`` therefore raises the transaction timeout of the ``Rfdc`` subtree
to ``TIMEOUT_FLOOR_S`` (32 s) when the Root timeout is lower. It never lowers a longer
application timeout and never touches the rest of the tree. The gate and the write-back
are many short transactions and are not affected.
