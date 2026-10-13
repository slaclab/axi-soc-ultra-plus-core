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
   * - ``Rfdc.StartUpDist``
     - ``StartUpDistRaw`` (word 0x10080, state 1 to 15 of every tile of the distribution)
     - None: see Group start below
   * - ``Rfdc.QuiesceTiles``
     - ``Shutdown`` of every tile that is not idle, member first
     - None: see Quiesce below
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

Group start
-----------

``Rfdc.StartUpDist`` starts every enabled tile of the clock distribution together. It
applies to tiles that are idle at state 1 with Restart clear, which is where
``Shutdown`` leaves them, and never to live tiles. The ADC tiles of a distribution gate
each other: while any one of them idles at state 1 none of the others completes its
start, so tiles that were shut down cannot be started one by one, and a tile started alone
stops at state 3 with Restart set. ``StartUp``, ``StartUpAllAdc`` / ``StartUpAllDac`` and
``RestartTiles`` keep the per-tile path described below; the group start is a separate
command and nothing falls back from one to the other.

The hidden ``StartUpDistRaw`` command writes the one ``PyRFdc`` word 0x10080 (a read
returns 1). ``PyRFdc::StartUpDist`` rebuilds ``XRFdc_StartUpDist`` from public driver
calls, because the vendor function is static:

1. **Plan.** ``PlanGroupStart`` picks the span, the one valid distribution whose inclusive
   bounds hold every enabled tile, and the package order from its lower to its upper
   bound (DAC 3 to DAC 0, then ADC 3 to ADC 0 on the nominal topology).
2. **Arm, then trigger.** Write RestartState (start state 1, end state 15) to every
   span tile, then write Restart to every span tile, in package order with nothing in
   between. Arming first matters because the ADC calibration of one tile waits until the
   others have been restarted. Triggering one tile and waiting before arming the next
   would stall exactly as the per-tile start does.
3. **Source wait.** Wait for the source tile of the distribution to reach state 7, the
   state where it can distribute its clock, as the vendor function does.
4. **Shared settle.** Give every span tile one common 2000 ms deadline (``GroupSettleMs``)
   to read state 15 with Restart clear. One shared deadline bounds the whole wait, where a
   deadline per tile would add up.
5. **Records.** Write one restart record per span tile with command 6 (``StartUpDist``),
   result 1 when the tile is ready and 2 when it is not. A source that misses state 7 is
   listed first for its type in the error, and every span tile still gets its record. One
   error text names every tile that failed.

Nothing is retried. A second Restart after a partial group could not be told from a
first, so a failed group is reported as it is.

**Refusals.** The planner refuses before any write, with one line and no change to any
register or record. The checks run in this order and the first one that applies is the
only one reported:

1. The ``ClkDist`` status is not Ok (``ClkDist Status Unsupported``, ``GetFailed`` or the
   raw value).
2. No tile is enabled (``no enabled tiles``). The group never degrades into a start of
   whatever tiles are left.
3. No single valid distribution holds every enabled tile. This is also what a board whose
   ADC tiles have their own reference clock gets, because that board reports two
   distributions.
4. A span tile is not enabled. The group would write a tile with no device behind it, and
   every such tile is named.
5. The source tile is not in the span, so the clock would come from a tile the group does
   not restart.
6. A span tile does not read state 1 with Restart clear. Every such tile is named with its
   state and Restart (``ADC tile 2 12/1``), because a live or restarting tile must not
   be restarted under its dependents.

A tile whose distribution index equals the lower or the upper bound of the span is inside
it. A distribution one index short on either side refuses with check 3.

**Host side.** ``Rfdc.StartUpDist`` reads CurrentState and RestartStatus of every enabled
tile first. When any tile does not read state 1 with Restart clear it raises a
``RuntimeError`` that names each one as ``<ADC|DAC> tile i state s restart r`` and writes no
command word. Otherwise it issues the word through the common sequence, which waits
for every enabled tile and writes nothing back. A board image whose ``PyRFdc`` predates the
word answers ``Undefined memory``. That is raised as an error saying the image has no group
start command and the Yocto image built from this repository must be deployed. The per-tile
start is not used instead, since a per-tile start of idle tiles cannot complete and would
only hang.

Quiesce
-------

``Rfdc.QuiesceTiles(context='start', budgetS=None)`` shuts every enabled tile down to the
idle state, state 1 with Restart clear, which is the state the group start needs. It runs
at both ends of a session. At the start, ``RfmcCarrierCore._start`` calls it ahead of the
LMK software reset, because the reset and both ``InitClock`` passes disturb the clock of
every running tile, and an ADC calibration that starts under that disturbance can park
tiles at state 12. At the stop, ``Root.stop()`` of rfmc-carrier-core calls it with
``context='stop'`` before the transports close, so the next launch finds idle tiles. The
LMK stays programmed after a stop, and a launch that finds every tile idle commands no
``Shutdown`` at all.

**Member first.** The order comes from ``Rfdc.ClkDist``, read while the tiles still run.
Tiles are ordered by their chain distance to the source of the distribution. The start
order is the source tile, then the other tiles of its converter type by ascending distance,
then the other type the same way, and the shutdown order is its exact reverse: ADC 0, ADC 1,
ADC 2, ADC 3, DAC 3, DAC 2, DAC 1, DAC 0 on SlacRfmcCarrier. A tile that sources a clock must
go down after the tiles that run from it, because a member that loses its source while it is
still up is knocked, so the sources go last. The per-tile ``Delay`` of ``ClkDist`` is not used
for the order. The order is a function of the topology only, so two runs on one board shut the
tiles down in the same order.

**Refusals.** Every refusal is decided before the first ``Shutdown`` and raises a
``RuntimeError`` that reads ``<path>.QuiesceTiles refused before any Shutdown: <reason>``.
Tiles that were shut down under a topology the group start cannot restart would never start
again, so nothing is shut down unless the whole distribution can be started afterwards. The
checks run in this order and the first one that applies is the only one reported:

1. ``ClkDist device not enabled``.
2. ``no enabled tiles``.
3. ``tile devices not enabled: ...``: a tile device did not become enabled within
   ``TILE_ENABLE_WAIT_S`` (2 s).
4. ``ClkDist Status <name>`` (anything but Ok) or ``ClkDist Count <n>`` (no distribution).
5. ``a ClkDist read returned None``.
6. ``no single valid distribution holds every enabled tile``. A board whose ADC tiles have
   their own reference clock reports two distributions and gets this refusal, so the start
   raises it and no tile is touched.
7. ``ADC 3 is not strictly nearer the source than ADC 2, ADC 1 and ADC 0``: the ADC chain
   is not the one the order assumes.
8. ``distribution span holds tiles that are not enabled: ...``.
9. ``distribution source <tile> is not in the span``.

**Already idle tiles.** The state of every tile is read first, and a tile that reads state 1
with Restart clear is skipped and listed under ``skipped``. Each of the other tiles gets one
``Shutdown`` and must read state 1 with Restart clear within ``TILE_STATE_WAIT_S`` (1 s).
A ``Shutdown`` that raises runs once more after ``QUIESCE_RETRY_S`` (1 s); a tile that
recovers on the retry is logged and listed under ``retried`` and is not an error. A failure
never stops the other tiles. Every failure is collected and raised once, naming each tile
and its state. The poller is never touched, and only ``Exception`` is caught, so an
interrupt ends the call.

**Budgets.** The start quiesce is bounded by ``QUIESCE_BUDGET_S`` (20 s), checked before
every ``Shutdown`` and before the retry. A tile left when the budget runs out is reported as
``not attempted`` with its state. The stop quiesce is shorter, because it must never hold up
a closing application. ``Root.stop()`` skips it for an emulated root, a root without an
``Rfdc`` device, a root that is not running, and a bridge that does not answer one
``waitReady`` slice of 0.5 s. Otherwise it lowers the transaction timeout of the ``Rfdc``
subtree to 5 s, so one dead transaction cannot cost the 32 s start floor, and runs
``QuiesceTiles(context='stop', budgetS=10.0)``. Any failure is logged with the tile and its
state and never raised, and the parent ``stop()`` always runs afterwards. A process that is
killed never reaches the stop, which is why the start quiesce handles any entry state on its
own: tiles at state 15, at state 1 or parked.

**Diagnostics.** ``TileStates()`` returns ``[CurrentState, RestartStatus]`` of every enabled
tile, with ``None`` for a value that could not be read. ``LastQuiesce`` shows a one-line
summary of the last call, and ``_lastQuiesce[context]`` keeps the record of the last call of
each context: ``order``, ``entry``, ``skipped``, ``commanded``, ``retried``, ``states``,
``errors``, ``refused``, ``elapsedS`` and ``ok``.

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
3. It refreshes the tile enable flags and reads the state of every enabled tile. When every
   one of them reads state 1 with Restart clear, which is what ``QuiesceTiles`` leaves
   behind, it starts the whole distribution as one group, as ``StartUpDist`` does (see
   Group start). When any tile is not idle, no group start word is written, and the
   per-tile resets below restart the live tiles in place.
4. It runs the common sequence with two raw steps: ``ResetAllDacRaw`` first, because a
   DAC tile can source the clock distribution that feeds ADC tiles, then
   ``ResetAllAdcRaw``. Within each type, ``PyRFdc`` restarts the distribution sources
   first. Each enabled tile is reset once. These resets follow the group start because a
   start from state 1 restores neither the Vivado register values nor the write-back of
   this session.

On a board image whose ``PyRFdc`` predates the group start word, ``Init`` of idle tiles
raises the error of ``StartUpDist`` that names the image, and ``Init`` of live tiles is
unchanged.

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
       SetClkDistribution, 6 StartUpDist), flags [7:4] (0x10 parked, 0x20 staging not refreshed),
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

The quiesce has its own limits (see Quiesce): ``QUIESCE_BUDGET_S`` (20 s) at the start, and
at the stop a 5 s transaction timeout for the ``Rfdc`` subtree, a 0.5 s liveness slice and
a 10 s budget. The group start waits inside one transaction, with the 2000 ms shared
deadline of Group start.

PL reload
---------

A tile that parks at state 12 after a restart cannot be recovered by any software restart.
Loading the PL again from the bitstream on the SD card does, and ``PyRFdc`` can start that
reload in-band, over the same port 9002 the host already uses, so no ssh from an operator
host is needed. The reload runs in a board helper, ``/usr/bin/rfsoc-pl-reload``
(installed by the ``pyrfdc`` recipe), which stops the bridge, so ``PyRFdc`` starts the
helper outside the bridge and answers before the bridge stops.

Words
+++++

.. list-table::
   :header-rows: 1
   :widths: 14 22 64

   * - Word
     - Name
     - Read and write
   * - 0x10084
     - ``PlReload``
     - A read returns 1. A write runs the checks below and starts the helper. Hidden
       command ``Rfdc.PlReloadRaw``
   * - 0x10088
     - ``PlReloadStatus``
     - Read-only. 0 ``Idle`` when this bridge instance scheduled no reload, 1 ``Scheduled``
       when it did and the helper log has no ``ABORT`` or ``FAILED`` line, 2
       ``HelperFailed`` when it has one. Reads the log, so read it only while the bridge answers
   * - 0x1008C
     - ``InstanceToken``
     - Read-only, never 0. Fixed when the bridge process starts, from the seconds since boot
       and the pid, so a restarted bridge answers a different value

None of the three words is gated by the restart write gate: the reload is requested exactly
when a tile is parked, so it never waits for state 15. A write to ``PlReloadStatus`` or
``InstanceToken`` is refused as read-only. Like every other word, all three are refused while
the RF data converter config ROM has not validated (see ConfigStatus).

Checks
++++++

A write is refused with the text below, and nothing is started, when:

- this bridge instance already scheduled a reload (``PL reload already scheduled by this bridge
  instance``). A bridge schedules at most one reload in its life; the next bridge starts
  with none.
- ``/boot/system.bit`` is missing or ``/proc/mounts`` has no ``/boot`` mount (``PL reload
  unavailable: no /boot/system.bit or /boot not mounted (tftp-only boot)``). On a tftp-only boot
  U-Boot loads the PL and Linux has no bitstream, so there is nothing to reload from.
  The helper checks the same two facts again before it stops anything.
- ``/usr/bin/rfsoc-pl-reload`` is not executable.
- the command line of the bridge cannot be read.

A refusal reaches the host as the whole error text of the command word. On a board image
whose ``PyRFdc`` has no such word the answer is ``Undefined memory``, which
``Rfdc.PlReloadRequest`` reports as an image that is too old and never retries.

Start of the helper
+++++++++++++++++++

The write starts ``systemd-run`` with ``posix_spawn`` and a fixed argv, with no shell::

   /usr/bin/systemd-run --no-block --collect --unit=rfsoc-pl-reload \
       /usr/bin/rfsoc-pl-reload <bridge pid> <bridge exe> <bridge args>

The pid is the bridge's own, the executable is the target of ``/proc/self/exe`` and the
arguments are the words of ``/proc/self/cmdline`` after the first, so the helper restarts
the bridge exactly as it was started. Nothing of the transaction reaches the argv. ``PyRFdc``
waits for ``systemd-run`` for at most ``PlReloadSpawnWaitMs`` (3000 ms), a longer
wait ends it and is an error, and replies before the bridge stops: the helper sleeps 2 s
first, and the reply leaves when the transaction returns.

The helper
++++++++++

The helper keeps one line per step in ``/tmp/rfsoc-pl-reload.log`` (the same lines go to the journal of
the ``rfsoc-pl-reload`` unit). Every step has a cap in seconds and all of them sum to 72 s,
which is also checked against ``/proc/uptime`` between steps.

.. list-table::
   :header-rows: 1
   :widths: 14 10 76

   * - Step
     - Cap
     - What it does
   * - pre-sleep
     - 2
     - Lets the reply of the command reach the host
   * - unit
     - 0
     - Reads the unit of the bridge from its own cgroup: ``startup-app-init.service`` or
       ``rfsoc-bridge.service``, anything else is an abort
   * - stop
     - 10
     - ``systemctl stop`` of that unit
   * - holders
     - 5
     - Sends TERM to each process that holds ``/dev/mem`` or ``/dev/axi_memory_map``,
       never by name
   * - rmmod
     - 5
     - Removes ``axi_memory_map``
   * - fpgautil
     - 25
     - Loads ``/boot/system.bit``, at most 5 attempts one second apart. ``fpgautil`` is
       never killed
   * - fpga0
     - 2
     - The fpga manager state reads ``operating``
   * - insmod
     - 5
     - Inserts ``axi_memory_map``
   * - uptime
     - 15
     - ``UpTimeCnt`` of the new PL is nonzero
   * - mac
     - 0
     - Writes the PS MAC address into the BSI words again, because the reload clears them. A
       failure is logged and does not fail the reload
   * - bridge
     - 3
     - Starts the bridge as the transient unit ``rfsoc-bridge``

A failure before the load is an ``ABORT`` line: the PL was not touched and the line names the
step. From ``fpgautil`` on a failure is a ``FAILED`` line and nothing is retried. The
helper never starts ``startup-app-init``, whose own ``fpgautil`` would run again with the
driver loaded, and it starts ``sshd`` when none is running, so a failed reload leaves the
board reachable.

After a reload
++++++++++++++

The bridge runs as the transient unit ``rfsoc-bridge`` (``Restart=on-failure``), no
longer under ``startup-app-init``. Its journal is ``journalctl -u rfsoc-bridge``. A second
reload resolves that unit from the cgroup and stops it the same way. The connection of every
client of the bridge drops, and the new bridge answers a different ``InstanceToken``.

Anyone who can reach port 9002 can trigger a reload: the bridge has no authentication and
already lets any client write the PL, and it keeps no list of its clients, so other sessions
only see their connection drop.

Host side
+++++++++

``Rfdc.PlReloadRequest`` is the one call site that triggers a reload. It writes the word
and raises the refusal text whole, and it raises when the device is not enabled, because no
word was sent. ``Rfdc.DiagnosticSnapshot`` returns one status line per enabled tile (ADC 0 to
3, then DAC 0 to 3) with ``CurrentState``, ``RestartStatus``, ``ClockPresent``, ``SupplyUp``,
``PowerUp``, ``PllLocked``, ``ClkDet`` and ``ClkSrc``, then one line for the clock
distribution with its status and count and the shutdown order or the reason there is none. A
read that fails is part of its line.
