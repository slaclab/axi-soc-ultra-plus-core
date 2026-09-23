PyRFdc Return Codes and Initialization Guard
============================================

This page documents three properties of ``shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp``,
the rogue memory slave that exposes the Xilinx RF Data Converter driver over AXI-Lite: how it
compares the status values the driver hands back, which register offsets it still answers
on an instance whose construction never produced a usable driver instance, and how a global
reset now sequences the converter tiles against the clock distribution topology the part is
wired for. All three are behavior changes against the previously released file, and this file
is consumed by every project built on this core, so the detail below is written for a reviewer
auditing the change against a board this repository has no access to. The AXI-Lite addressing
model itself is documented on the ``Register Map`` page.

Driver return code comparisons
------------------------------

``XRFDC_SUCCESS`` is 0 and ``XRFDC_FAILURE`` is 1. The driver API is not documented to return
only those two values. Every guard in this file that tested a driver return used to ask whether
the return was **not** ``XRFDC_FAILURE``, so any status outside the documented pair was read as
a success and the guarded code ran on it. All 16 such comparisons now ask whether the return
**is** ``XRFDC_SUCCESS``.

This is the one risk in the change that this repository cannot test. A board on which some
driver call answers outside the documented pair is a board that proceeds silently today and
reports an error after the change. That is the safe direction, but it is a behavior change, and
seven of the 16 sites are in the constructor, which runs on every board on every construction.

Out of scope, and deliberately unchanged: the 118 assignments of the form
``status = XRFDC_FAILURE;``. Those are not comparisons of a driver return. They are this file's
way of marking an access invalid before any driver call is made, and the value is tested against
``XRFDC_SUCCESS`` afterwards, so converting one would be meaningless.

This page cites no source line numbers, and that is deliberate rather than an omission: a number
written beside prose in a file this size goes stale on the next change that moves the file, and a
reader has no signal that it has. Every row below is located instead by its enclosing function
together with the driver call named in the same cell.

.. list-table:: Behavior change at each converted comparison site
   :header-rows: 1
   :widths: 24 26 26 24

   * - Call site
     - Before the change
     - After the change
     - Can this arise on a board with no clock distribution
   * - ``PyRFdc::PyRFdc``, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the tile's clock
       source and PLL configuration are read and its four blocks are walked.
     - Only a success reads the tile. Any other status leaves the tile's shadow configuration at
       its initialized value and skips its block loop.
     - Undetermined here. This repository has one carrier and that carrier has clock
       distribution. This row runs on every construction on every board, so it is one of the
       seven to check first.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_GetClockSource``
     - A status outside the documented pair stores whatever the call left in the output
       argument as the tile's default clock source.
     - Only a success stores it. The default clock source keeps its initialized value otherwise.
     - Undetermined here, for the same reason. Runs on every construction. The stored value is
       later passed back to ``XRFdc_DynamicPLLConfig`` in the reset sweep, so a wrong value
       persists past construction.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_GetPLLConfig``
     - A status outside the documented pair stores the returned PLL settings and then issues
       ``XRFdc_DynamicPLLConfig`` with them.
     - Only a success stores them and issues the reconfigure. Any other status issues no
       reconfigure at all from the constructor.
     - Undetermined here. Runs on every construction, and this is the row that changes whether
       a converter is reconfigured during construction, so a reviewer with such a board should
       check this row alongside the ``XRFdc_CheckTileEnabled`` row above.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_CheckBlockEnabled``
     - A status outside the documented pair is read as an enabled block, so the block's default
       quadrature and mixer settings are read.
     - Only a success reads them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_GetQMCSettings``
     - A status outside the documented pair stores the returned quadrature settings as the
       block's defaults.
     - Only a success stores them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_CheckDigitalPathEnabled``
     - A status outside the documented pair is read as an enabled digital path, so the block's
       mixer defaults are read. The second half of the condition, the mixer scale test, is
       unchanged.
     - Only a success reads the mixer defaults.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, ``XRFdc_GetMixerSettings``
     - A status outside the documented pair stores the returned mixer settings as the block's
       defaults.
     - Only a success stores them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::Reset`` global sweep, the tile enable test before the tile is touched,
       ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the tile is reset,
       PLL reconfigured and its blocks restored.
     - Only a success touches the tile. Any other status skips the whole tile.
     - Undetermined here. Reached by a host write to the global reset offset, which is the
       first thing the pyrogue ``Init()`` sequence does, so it runs on every bring-up.
   * - ``PyRFdc::Reset`` global sweep, ``XRFdc_CheckBlockEnabled``
     - A status outside the documented pair is read as an enabled block, so the block's
       quadrature and mixer settings are restored.
     - Only a success restores them.
     - Undetermined here. Reached on every bring-up, as above.
   * - ``PyRFdc::Reset`` global sweep, ``qmcStatus`` set by ``XRFdc_SetQMCSettings``.
       **Different kind of change: this tests a status an earlier call produced, not the call it
       wraps.**
     - The quadrature event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, so an event is raised for settings that may never have been applied.
       The settings status is separately recorded against the tile either way.
     - The event update is raised only when the settings call reported success. The recorded
       per-tile attribution is unchanged, because it is taken before this guard.
     - Undetermined here. Reached on every bring-up. What changes is which returns reach the
       code inside the branch rather than which returns count as a failure.
   * - ``PyRFdc::Reset`` global sweep, ``XRFdc_CheckDigitalPathEnabled``
     - A status outside the documented pair is read as an enabled digital path, so the block's
       mixer settings are restored.
     - Only a success restores them.
     - Undetermined here. Reached on every bring-up.
   * - ``PyRFdc::Reset`` global sweep, ``mixerStatus`` set by ``XRFdc_SetMixerSettings``.
       **Different kind of change: this tests a status an earlier call produced.**
     - The mixer event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``.
     - The event update is raised only when the settings call reported success. The recorded
       per-tile attribution is unchanged.
     - Undetermined here. Reached on every bring-up. Same kind of change as the ``qmcStatus`` row
       above.
   * - ``PyRFdc::Reset`` global sweep, the tile enable test guarding the reset issued after the
       settings were restored, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the second reset of
       that tile is issued after its settings were restored.
     - Only a success issues the second reset.
     - Undetermined here. Reached on every bring-up. This is the second of the two resets the
       sweep performs per tile.
   * - ``PyRFdc::MixerSettings``, ``status`` set by ``XRFdc_SetMixerSettings``.
       **Different kind of change: this tests a status an earlier call produced.**
     - The mixer event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, and its own return then overwrites the status this entry point
       reports, so a status outside the documented pair could be replaced by a success and
       never reported.
     - The event update is raised only when the settings call reported success. A status
       outside the documented pair is no longer overwritten, so the entry point reports the
       error.
     - Undetermined here. Reached only by a host write to the mixer settings apply index, not
       by the bring-up sequence.
   * - ``PyRFdc::QMCSettings``, ``status`` set by ``XRFdc_SetQMCSettings``.
       **Different kind of change: this tests a status an earlier call produced.**
     - The quadrature event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, and its own return then overwrites the reported status.
     - The event update is raised only when the settings call reported success, and a status
       outside the documented pair is reported rather than overwritten.
     - Undetermined here. Reached only by a host write to the quadrature settings apply index.
   * - ``PyRFdc::MtsEnabled``, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so that tile's
       multi-tile synchronization enable bit is merged into the returned mask.
     - Only a success merges the bit. A tile whose check answered otherwise reads back as not
       enabled.
     - Undetermined here. This is a read-only path reached by a host read, and it reports a
       mask rather than driving the converter, so it is the lowest consequence row in the
       table.

Four of the 16 rows are marked as a different kind of change. Those four do not guard a call at
its own call site. They gate a follow-on action on a status an earlier call already produced, so
what the conversion changes there is which returns reach the code inside the branch, rather than
which returns are treated as a failure. Two of them, in ``PyRFdc::MixerSettings`` and in
``PyRFdc::QMCSettings``, additionally stop a status outside the documented pair from being
overwritten by the event update's own return, which is why the entry point reports an error
after the change where it reported none before.

Driver initialization guard
---------------------------

The constructor has four early returns. Each logs a line and leaves the object constructed with
its driver instance never initialized, and roughly ninety dispatch bodies read through that
instance. A fifth outcome behaves the same way: the configuration initialize call can report a
non-success, in which case the driver instance was never configured and the constructor returns
at that point. That call is not one of the four named bail-outs, so the reason reported for it
is that construction did not complete rather than a named step.

A single guard now sits after the address decode and before the first dispatch branch. On an
instance whose construction did not produce a usable driver it refuses the transaction with a
message naming the constructor step that failed and the rejected address, so a write to a reset
offset is refused before the reset entry point is entered and the per-tile diagnostic register
reads that a failing sweep performs are unreachable.

The admitted set is expressed as a rule rather than as a hand-kept list: exactly the offsets
whose bodies never dereference the driver instance. A body added later at a new offset is
therefore refused by default, and admitting it has to be a deliberate edit.

.. list-table:: Offsets admitted while the driver instance is unusable
   :header-rows: 1
   :widths: 16 20 64

   * - Offset
     - Admitted
     - Why the body is safe on an unusable driver
   * - ``0x12000``
     - Reads only
     - The read hands back a stored log level and touches nothing else. The write calls into
       libmetal, which on this path may never have been initialized, so the write is refused.
   * - ``0x12004``
     - Reads and writes
     - The body reads and writes one member of this class. It never names the driver instance.
   * - ``0x12008``
     - Reads and writes
     - Scratchpad. The body reads and writes one member and nothing else.
   * - ``0x1200C``
     - Reads only
     - Initialization failure reason. The body reads one member and names the driver instance
       nowhere. The write is refused.
   * - ``0x13000``
     - Reads and writes
     - Double test register, lower word. The body reads and writes one member of this class.
   * - ``0x13004``
     - Reads and writes
     - Double test register, upper word. Same body, same member.

The members behind every offset in that table are initialized at their declaration in
``PyRFdc.h`` and not in the constructor's local variable block. Every one of the constructor's
early returns, including the one on a declined configuration initialize described below,
happens before that block, so a member initialized only there would hold whatever the storage
contained on exactly the paths the guard exists for. The declared values are ``scratchPad_``
zero, ``doubleTestReg_`` positive zero, ``metalLogLevel_`` false and ``ignoreMetalError_``
false, alongside ``initFailReason_`` at ``PYRFDC_INIT_FAIL_NOT_COMPLETED`` and ``driverValid_``
false, which were already declared that way. An offset the guard was extended to keep
answerable therefore answers with a declared value on every path that can reach it, rather
than with the contents of this process's memory.

Two departures from a minimal reading are worth naming, because a reviewer checking the rule
against the requirement will find both.

The first is the double test pair at ``0x13000`` and ``0x13004``. A minimal reading would admit
only the registers a host needs in order to ask why the driver is dead, and the double test pair
is not one of those. It is admitted because the rule is mechanical and its body provably never
touches the driver instance, and carving it out would replace the rule with a list.

The second is ``0x12000``, the only offset admitted in one direction. Its read is admitted
because it returns a stored value, and its write is refused because the write path calls into a
library that may never have been initialized on this path. Reads and writes of the same offset
are not always the same question, and this is the one place in the admitted set where they
differ.

Constructor bail-out on a declined configuration initialize
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The return on a non-success ``XRFdc_CfgInitialize`` is itself a behavior change, on a call that
runs on every board, so it is written out here rather than left to be found in the diff.

The return sits immediately after the ``XRFdc_CfgInitialize`` call and before the sample rate
workaround loop, so that loop's two direct writes per tile to the ``MaxSampleRate`` field of the
instance's ADC and DAC tile configurations do not happen either.

A board on which that call reports a non-success previously continued through the rest of the
constructor and did all of the following through an instance the driver had just declined to
configure:

- wrote ``MaxSampleRate`` on all four ADC tile configurations and all four DAC tile
  configurations held inside the instance
- ``XRFdc_CheckTileEnabled`` once per tile
- ``XRFdc_GetClockSource`` and ``XRFdc_GetPLLConfig`` once per enabled tile
- ``XRFdc_DynamicPLLConfig`` once per enabled tile. This is the one entry in the list that
  writes the converter rather than reading it
- ``XRFdc_CheckBlockEnabled`` and ``XRFdc_GetQMCSettings`` once per enabled block
- ``XRFdc_CheckDigitalPathEnabled``, and on the digital path branch ``XRFdc_RDReg``, a raw
  register read
- ``XRFdc_GetMixerSettings`` once per enabled block

It now returns at that call. None of the above is issued, the instance is marked unusable, the
reason reports that construction did not complete, and every later transaction that would reach
the driver instance is refused with a message naming that outcome and the rejected address. The
offsets in the table above stay readable throughout.

This repository cannot test that change, for the same reason it cannot test the comparison
conversions above. There is one carrier available to this work, its configuration initialize
succeeds, and the vendor driver implementation is not present in this repository, so the
conditions under which that call reports a non-success can be neither reproduced nor read here.
The reviewer who can close it is one who owns a board on which that call does not succeed. It is
not a corner case: the call runs on every construction on every board, and this is the path
taken whenever it answers anything other than success.

Teardown after a construction that did not complete
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

One rule governs both of the changes described below, so it is stated first. libmetal and the
driver's device registration are released by whichever of the constructor and the destructor
acquired them, exactly once, and the driver instance is never handed to the registration entry
point after the driver declined to configure it.

The registration bail-out now closes only what the registration handed back. Before the change,
the local pointer passed to ``metal_device_close`` on that bail-out was declared without an
initializer, and ``XRFdc_RegisterMetal`` writes through its out parameter on success alone, so on
the failure branch the close entry point was handed whatever the stack held and dereferenced it.
The pointer is now initialized at its declaration and only a non-null pointer is closed, which is
the shape the destructor has always used. The consequence for a board that takes that path is
concrete: a fault inside libmetal during construction was previously possible before the driver
could answer the reason register this page documents, and the path now returns so that register
answers.

The destructor now releases only what it still owns. Before the change, its teardown ran
unconditionally on every outcome. It released libmetal a second time on the three bail-outs where
the constructor had already released it, and it called ``XRFdc_RegisterMetal`` with the driver
instance on all of them, including the outcomes where the driver had declined to configure that
instance or had never seen it. A private ``metalReady_``, declared in ``PyRFdc.h`` beside the
validity flag, now records whether the libmetal bring-up succeeded and has not been released yet.
The destructor's teardown is gated on that flag, and the registration call and the device close
inside it are additionally gated on the instance being usable. The resulting property is a number
a reviewer can check rather than a description to be taken on trust: counted across construction
and destruction together, libmetal is released exactly once on every constructor outcome. On the
three named bail-outs the constructor performs that release and the destructor performs none. On
the declined configuration initialize path the constructor performs none and the destructor
performs one.

Two residuals are left knowingly, and are named here rather than omitted.

- On the declined configuration initialize path a metal device was registered during construction
  and is not closed individually. The libmetal release is what ends its lifetime, and whether that
  release closes devices that are still open is a property of a library whose source is not
  present in this repository, so it is recorded here rather than asserted in either direction.
- The destructor's teardown block is compiled out in its entirety in the baremetal build, while
  the constructor's libmetal bring-up is not. On that build a construction that completes raises
  the flag and nothing ever releases the library. The flag makes the asymmetry explicit and does
  not remove it.

Unlike the comparison conversions above, both of these changes are exercised in this repository,
and it is worth being precise about how far that goes. Both are driven by the board-free host
harness whose invocation is recorded in the reference facts section below: it scripts each
constructor outcome, drops the instance, and asserts on the recorded driver call list what
teardown did. That is a claim about the shim's behavior and not automatically about the Yocto
build's, in the same terms that section states for every other claim this harness makes. What
remains untestable here is the behavior on a board where the device registration or the
configuration initialize actually reports a non-success, which no board available to this work
does.

The reviewer who can close what remains is one who owns a board on which either of those two
calls reports a non-success. The first residual above can also be closed by anyone who can read
the release implementation in libmetal's own source, which this repository does not carry.

Global reset sequencing and clock distribution
----------------------------------------------

A converter tile whose sample clock is distributed from another tile cannot be restarted on its
own. The previously released file did not know which tile clocks which: a global reset walked
the four tiles of one type in tile id order, and issued up to three IPSM cycles per tile. This
section records, per site, what a global reset does now.

The topology is read and never programmed. Nothing on this path calls
``XRFdc_SetClkDistribution``. A board whose topology reports no distribution keeps the tile
order, the call sequence and the log output it has today, and the rows below say for each site
whether it can reach such a board at all.

This section cites no source line numbers either, for the reason given above. Every row below is
located by its enclosing function together with the driver call or the register offset named
beside it.

.. list-table:: Behavior change at each sequencing site
   :header-rows: 1
   :widths: 18 16 22 24 20

   * - Enclosing function
     - Driver call
     - Before the change
     - After the change
     - Can this arise on a board with no clock distribution
   * - ``PyRFdc::PyRFdc``, the IP generation gate
     - ``XRFdc_GetClkDistribution``
     - Never called. This file named no distribution symbol at all and carried the support as a
       standing TODO.
     - Called once at construction, and only when ``RFdc_Config.IPType`` is at least
       ``XRFDC_GEN3`` and at most ``PYRFDC_IPTYPE_MAX_KNOWN``, a constant defined in
       ``PyRFdc.h`` at 3, one above ``XRFDC_GEN3``, so a genuine next generation part still
       takes this call. The structure handed to the driver is zeroed and has the unused
       sentinel written into all eight slots before the call, because the decode decides a
       slot is unused by testing that one field. The result is decoded into a per tile cache
       of role, master type and master tile. The call sits inside an ``== XRFDC_SUCCESS`` test
       and the cache is written only on success, so a refused query leaves every tile
       ungrouped.
     - Yes, and on such a board the gate is the point. A driver reporting pre-Gen3 is never
       asked and therefore gains no new driver error line at bridge start. A Gen3 driver that
       refuses the call falls back to ungrouped rather than to the raw decode below, because the
       decode is selected by the IP generation gate and not by the return value. A generation
       above ``PYRFDC_IPTYPE_MAX_KNOWN`` is not asked either, and is not a board type: it is a
       driver reporting something no part of this family carries, which this project has read
       as 255 off a real carrier whose device tree parameter property was empty. Such a
       construction consults no source, makes no driver call, cannot fail and cannot raise,
       and the board falls back to the same ungrouped per type sweep.
   * - ``PyRFdc::decodeClkDistributionRaw``, reached from the pre-Gen3 arm of the constructor IP
       generation gate
     - ``XRFdc_CheckTileEnabled``, then ``XRFdc_RDReg`` of the per tile clock detect register at
       offset ``0x0080``
     - No such function. A driver that would refuse the documented query left this file with no
       topology and no second source.
     - Runs in place of the documented query for a driver reporting pre-Gen3. It reads the clock
       detect register of each enabled tile and reproduces the arithmetic the driver itself
       performs inside ``XRFdc_GetClkDistribution``, which is the search for the source package
       tile and the mapping from a package index back to a tile type and tile id. A tile whose
       register names no source stays ungrouped, and a tile that names only itself is a master
       only if at least one other tile names it.
     - Yes. It runs on every pre-Gen3 board, which is the population most likely to have no
       distribution at all, and there every clock detect register names no source, so the decode
       finds nothing and every tile stays ungrouped. This path reads registers and nothing else:
       it issues no reset and writes no tile.
   * - ``PyRFdc::Reset`` global sweep, the removed pre-reset and second loop, and the
       compensating reset
     - ``XRFdc_Reset``, decided by the ``XRFdc_RDReg`` power up status read of the tile common
       status register at offset ``0x0228`` taken before the reconfigure, and by
       ``XRFdc_DynamicPLLConfig``
     - Every enabled tile received a conditional reset before its PLL reconfigure, guarded on
       the tile PLL being enabled, and an unconditional reset in a second loop over the tiles
       after the settings had been restored. A healthy powered up tile with its PLL enabled
       therefore received three IPSM cycles per global reset: the pre-reset, the cycle the
       reconfigure performs internally, and the post-reset.
     - Both explicit resets are gone and the entire second loop with them, so the sweep walks
       the tiles once rather than twice. One compensating ``XRFdc_Reset`` is issued for a tile
       only when the power up status read taken before the reconfigure was zero, or the
       reconfigure did not return success. The same healthy powered up tile now receives one
       cycle, and that cycle is performed by the PLL reconfigure through the same restart
       primitive the explicit resets reach. The two cases the compensating reset covers are the
       tile that was not powered up and the tile whose reconfigure failed, which are exactly the
       tiles that would otherwise have received no cycle at all.
     - Yes, on every board. This row is reached by a host write to either global reset offset,
       which is the first thing the pyrogue bring up sequence does, and the per tile cycle count
       it changes does not depend on the topology in any way.
   * - ``PyRFdc::Reset`` global sweep, the owned tile walk, with ``PyRFdc::buildOwnedTileWalk``
       and ``PyRFdc::buildDeferralMessage``
     - None in the walk itself. Every tile it yields still passes ``XRFdc_CheckTileEnabled`` in
       the sweep before any driver call is made against it.
     - Each of the two global reset commands walked the four tiles of its own type in tile id
       order. A tile taking its clock from a tile of the other type was therefore restarted
       without its master.
     - Work is divided between the two commands by ownership of the distribution master. Each
       command takes the groups whose master is of its own tile type, in full and master first
       with the edge tiles in ascending tile index order, including edge tiles of the other
       type, and then its own tiles that belong to no group. No state is carried between the two
       calls. A command that passes over a tile of its own type emits one ``log_->warning``, the
       line ``PyRFdc::buildDeferralMessage`` assembles, naming each deferred tile and the master
       it was deferred to.
     - Yes, and there the behavior is unchanged: every tile is ungrouped, each command walks its
       own four tiles in ascending tile id exactly as before, and the deferral line is not built
       at all. **The consequence for a board owner who does have a distribution:** a standalone
       call to one of the two commands no longer resets a tile of that type whose clock master
       is of the other type, and the warning line names the tile it deferred and the master it
       deferred to.
   * - ``PyRFdc::recoverClkGroup``, armed by the pass over the recorded tile failures at the end
       of the ``PyRFdc::Reset`` global sweep
     - ``XRFdc_Reset``
     - No retry construct of any kind existed in this file. A tile that returned non-success
       from its reset was recorded and reported, and nothing further was attempted.
     - A reset that returns non-success on a tile the cached topology calls a distribution edge
       arms exactly one further pass of ``XRFdc_Reset`` over that tile's whole group, master
       first and then the edge tiles in ascending tile index order. The arming predicate is a
       conjunction: the recorded step must be ``XRFdc_Reset`` and the cached role must be edge,
       so a tile that failed at some other step arms nothing. The bound is one attempt per group
       per global reset however many of that group's edge tiles failed, held in a set local to
       the call. A member of the group that is not enabled is skipped before any call is made
       against it, so it receives no reset and no cycle, and its refusal is not counted against
       the attempt: without that guard the recovery would be unreachable on any board with a
       partially populated group. A recovery whose resets all succeed clears the failure records
       of the tiles it reset and of no others, so the transaction completes reporting no error
       while a tile that failed outside this group still reports, and it remains visible in
       three places: the armed and succeeded halves of ``0x1201C``, a second cycle in the per
       tile cycle count, and one ``log_->error`` line naming the arming tile and the master.
       The outcome word on that line and the succeeded half of the counter are derived from
       the same test, so a reader who finds the two disagreeing is looking at a driver defect
       rather than at a distinction the driver is drawing. That word has three values:
       ``succeeded`` when the attempt drove at least one tile and every reset it issued
       returned success, ``failed`` when it drove at least one tile and a reset did not, and
       ``nothing driven`` when every member of the group answered not enabled so the attempt
       issued no reset at all. On a ``nothing driven`` line the reset count of zero printed
       beside the word is the corroborating figure rather than a contradiction, and the
       succeeded half of the counter does not move either.
       The second of those three is not unique to a recovery, so a reader who finds a cycle
       count nibble of 2 should consult the ``PyRFdc::ResetCycleCount`` row below rather than
       assume the PLL reconfigure produced it. That row states both producers and states the one
       direction this register can be read in: an armed half of zero rules one producer out, and
       a non-zero armed half attributes nothing.
       That line is emitted at the error level and the deferral line above is emitted at the
       warning level, which is deliberate and is explained under
       `Reading the five reports on a board`_.
     - No. Arming requires the cached topology to call the tile a distribution edge, so on a
       board reporting no distribution no tile ever qualifies, the arming pass finds nothing,
       and the reset path is identical to the one before this was added.
   * - ``PyRFdc::ClkDistStatus``, at offset ``0x12010``
     - None. The body reads members and names the driver instance nowhere.
     - The offset decoded to nothing, so a transaction against it was refused as undefined
       memory.
     - Reports where the cached topology came from in bits 7:0, as 0 for nothing obtained, 1 for
       the documented getter, 2 for the raw decode and 3 for a reported IP generation outside
       the range this driver asks, meaning no source was consulted at all; the IP generation the
       driver reports for this part in bits 15:8; and how many distribution groups this driver
       counted while it built the cache in bits 23:16. Both byte fields saturate rather than
       wrap. A write is refused by name.

       **What the group count counts, read from the three places that increment it and the one
       that publishes it.** The count has more than one producer rather than one. The documented
       getter counts one for every distribution slot it accepted, whether or not it could mark
       that slot a master, while the raw clock detect decode counts one only where it marks a
       master. The normalization then contributes
       plus one for each tile the normalization promoted to master
       because an edge named it and no decode marked it. So a board with one distribution slot
       whose source lies outside its own edge range publishes a count of two, and that second
       count is a master the normalization recovered rather than a second slot the documented
       getter reported. The count is never decremented, so it is not the same as how many
       groupings the cache still holds: a grouping the normalization could not resolve to an
       orderable master is withdrawn from the map without changing this count. What the count
       means beside a map at ``0x12014`` reading every tile ungrouped depends on the source byte
       and is stated per source value in the ``PyRFdc::ClkDistMap`` row below, not here. What
       it counts is asserted in tree rather than only written down: the
       board-free claim
       ``the published group count counts what the normalization recovered as well as what the decode marked``
       scripts exactly one distribution slot on each of two topologies and requires a count of 1
       where the getter marked the master itself and a count of 2 where the normalization had to
       recover it.

       **What the fourth source value costs, stated because the upper bound that produces it is
       a tradeoff and not pure protection.** What the bound buys: a part reporting a generation
       this driver cannot interpret is not sent down the branch that reads Gen3 clock
       distribution registers, which on a genuinely pre-Gen3 part whose generation byte happens
       to read high would be reads against registers that are not backed. What it costs, in the
       same breath rather than in a footnote: a genuinely high generation part that the previous
       unbounded lower-bound-only test served correctly now obtains no topology at all, so its
       reset falls back to the per type ordering and the master before edge guarantee does not
       apply on that part. That cost is accepted and it is not discharged. No such part is
       available to this work, so nothing here measures it.

       **The reading that decided the direction rather than an argument that preferred it.** On
       the one carrier available to this work, a session dated 2026-09-21 read ``0xFF`` in the
       generation byte of this register with the source field reading 1, the documented getter.
       On that reading the previous unbounded test selected the documented getter, obtained a
       topology, and published ``0xFF55FFFF`` at ``0x12014``: DAC 1 mastering DAC 0, and ADC 3
       ungrouped and therefore reset in isolation from the tile that sources its clock. That is
       the same failure direction the bound is accused of causing, so widening the bound back
       would not recover the outcome on this board. It would exchange one way of isolating ADC 3
       for another while reopening the unbacked-register hazard above.
       A reviewer holding a different part should weigh the two against their own hardware rather
       than treat this as settled: the reading above is one sample, on one carrier, whose device
       tree parameter property was empty.

       **What an operator sees now.** A boot that lands in the fourth source value emits one line
       at the error level, ``clock distribution topology not obtained because the reported IP
       generation`` followed by the reported value and the statement that the reset falls back to
       per type ordering. So the loss of the ordering guarantee is no longer visible only to a
       host that knows to read this register, on a boot where the register path may itself have
       degraded.

       A boot that lands in the first source value, no topology obtained, now emits its own
       line too, at the same level, stating that the documented getter returned non-success.
       So a reader who finds a 0 in bits 7:0 does not have to work out from the register alone
       whether the getter was asked and refused or whether construction never reached the
       branch at all.
     - Yes, and this is the register that says so. A board with no distribution reads a group
       count of zero beside source 1 or 2, while source 0 and source 3 read a zero count as well,
       whether or not the board has a distribution, because no topology was obtained there, so a
       zero count is read beside the source byte and never alone. The IP generation field is the
       only place this driver publishes what
       the driver thinks the part is. Note that the generation byte saturating at ``0xFF`` means
       a true 255 and a field nothing ever captured are not distinguishable from that byte
       alone, which is why the fourth source value exists: the distinction a reader needs lives
       in the source field rather than in the saturated byte. Partition hardware readings by the
       source field rather than pooling them.

       What the group count says beside a map at ``0x12014`` reading every tile ungrouped is
       stated once per source value in the ``PyRFdc::ClkDistMap`` row below rather than here,
       because it differs by source value.
   * - ``PyRFdc::ClkDistMap``, at offset ``0x12014``
     - None. The body reads members and names the driver instance nowhere.
     - The offset decoded to nothing, as above.
     - Four bits per tile at tile index type times four plus tile, so ADC 0 occupies bits 3:0 and
       DAC 3 occupies bits 31:28. A nibble reads ``0xF`` when the tile is ungrouped, and
       otherwise the tile index of that tile's distribution master, so a master's own nibble
       holds its own index. A write is refused by name. That invariant, every nibble naming a
       master naming a tile whose own nibble is its own index, holds without a proviso, because
       the normalization now handles both of the classes that can violate it: an edge whose
       chain reaches no marked master, and a tile marked as a master that does not name itself.
       A cache the normalization cannot order has the grouping of those tiles withdrawn, so
       their nibbles read ``0xF`` and none of them names a non-master, for any of three causes:
       the edge graph contains a cycle, a named master index is out of range, or a tile is
       marked as a master that does not name itself, in which case every edge naming that tile
       is withdrawn with it. The reset sequence for such a cache is the per type ordering, each
       global reset driving the four tiles of its own type, and it is unchanged by the
       withdrawal in the sense that matters: a
       topology no ordering can honor is not made orderable by publishing a master for it, so
       what the withdrawal changes is what is published and reported rather than whether any
       ordering guarantee is delivered. The withdrawal is announced once on the error channel,
       described below.
     - No, not the withdrawal case. Such a board reads ``0xFFFFFFFF`` already, which is every
       tile ungrouped, because no tile is ever marked as an edge or as a master there, so
       neither half of the withdrawal has anything to withdraw and neither fires. That
       all-ungrouped reading is also what distinguishes a grouping that did not engage from one
       that did, and on a board with a
       distribution it is now additionally what a withdrawn grouping reads as, and also what a
       boot that obtained no topology reads as. What the register pair can tell apart depends on
       the source in bits 7:0 of the status word at ``0x12010``, so the pair is read beside that
       byte and readings of different source values are not pooled.

       When the source byte reads 0, no topology was obtained, and that value has two producers.
       Either the documented getter was asked and refused, which the construction-time line
       beginning ``clock distribution topology not obtained because the documented getter returned
       non-success`` reports, or the driver never initialized far enough to ask, which
       InitFailureReason at ``0x1200C`` reports by reading non-zero. A driver whose construction
       returned early never reaches the path selection and keeps the declared source value, so it
       publishes ``0x0000FF00`` at ``0x12010``, source 0 with the generation byte still at its
       declared ``0xFF``. Either way the cache keeps its declared ungrouped values and the group
       count is zero, so this word reads every tile ungrouped whether or not the board has a clock
       distribution, nothing can have been withdrawn, and the pair says nothing about the
       distribution. The line or the reason register is the evidence.

       When the source byte reads 1, the documented getter answered, and beside this word a
       non-zero group count means a grouping was withdrawn and a zero count means none was. On
       that source the count is non-zero only if the getter accepted a slot, and the first slot it
       accepts places at least two tiles in a group, because a slot whose two edges share a
       package index is skipped and every tile starts ungrouped. Only the normalization's
       withdrawal returns a placed tile to ungrouped, and it always reports doing so, while a zero
       count means no slot was accepted and nothing was placed. On that source the pair therefore
       decides the question, and unlike a construction-time console line it is still there for a
       host attaching later, a host after a bridge restart and a host after a log rotation.

       When the source byte reads 2, the raw clock detect decode answered, and the pair then
       carries no withdrawal information. Every group that source counts is a master that names
       itself, whether the decode marked it or the normalization promoted it, and the
       normalization never withdraws a master that names itself, so a count above zero always
       leaves a master in this word. A word reading every tile ungrouped on that source therefore
       always sits beside a zero count whether or not a grouping was withdrawn, which is the pair
       a board with no clock distribution publishes on that source. One reachable withdrawal
       there is a topology in which two tiles name each other, ADC 3 naming DAC 0 and DAC 0
       naming ADC 3: the decode counts no group because neither tile names itself, the
       normalization promotes nothing because each edge names another edge, and both tiles are
       then withdrawn, leaving a zero count beside ``0xFFFFFFFF``. On that source the withdrawal
       report is the only evidence a withdrawal happened at all.

       When the source byte reads 3, the reported IP generation was above the range this driver
       asks, so no source was consulted and no topology query was made. The cache keeps its
       declared ungrouped values and the group count is zero, so this word reads every tile
       ungrouped whether or not the board has a clock distribution, nothing can have been
       withdrawn, and the construction-time line beginning ``clock distribution topology not
       obtained because the reported IP generation`` is the evidence.

       A withdrawal can happen only on the two sources that answered, 1 and 2, and there the
       withdrawal report is the only thing that says which tiles.

       These statements are asserted in tree rather than only written down. The board-free claim
       ``an all-ungrouped map is read beside the topology source``
       enumerates every documented getter topology of at most two distribution slots, 262,657
       constructions, and every raw clock detect script in which at most three tiles name a
       source, 30,529 constructions, and requires both directions on source 1 and the zero count,
       beside both a withdrawn and a not withdrawn reading, on source 2. It also refuses the
       documented getter on every one of those getter topologies with the refusing driver writing
       each topology out first, fails driver initialization at three constructor steps with a
       distribution scripted, and reports every generation from 4 to 255 and three values above
       255, 255 constructions, with a distribution scripted, and requires each of those readings
       to publish every tile ungrouped with a zero count and no withdrawal, beside exactly one
       construction-time line naming the cause or a non-zero reason. The board-free claim
       ``a raw decode withdrawal and a board with no distribution publish the same register pair``
       constructs the ADC 3 and DAC 0 cycle beside a board with nothing scripted, requires both to
       publish the same two words, and requires the withdrawal line in the first and none in the
       second, which is one instance on the raw decode path and asserts nothing about the
       documented getter. The wording of this row, of the ``PyRFdc::ClkDistStatus`` row and of the
       two matching host model descriptions is itself read by the board-free claim
       ``the published register pair text is stated per source value``,
       which fails when either document cannot be read. Past the enumerated domain the statements
       rest on reading the counting and withdrawal conditions of ``PyRFdc::cacheClkDistribution``,
       ``PyRFdc::decodeClkDistributionRaw`` and ``PyRFdc::normalizeClkDistCache``, and for source
       values 0 and 3 the path selection in ``PyRFdc::PyRFdc``, and not on a measurement.
   * - ``PyRFdc::ResetCycleCount``, at offset ``0x12018``
     - None. The body reads members and names the driver instance nowhere.
     - The offset decoded to nothing, as above.
     - Four bits per tile in the same nibble layout, holding how many IPSM cycles the last global
       reset covering that tile issued for it. A cycle is counted whether it came from an
       explicit reset or from the internal restart the PLL reconfigure performs, because a count
       of explicit calls would read zero for every tile of a healthy reset. That remains true on
       every path but one. A nibble reading ``0xF`` is not a count at all: it is the reserved
       value for a tile whose PLL reconfigure returned non-success at a point where the driver
       cannot tell whether the call had already cycled it, so the true figure for that tile is
       one or two and the driver declines to pick between them. A real count saturates at
       ``0xE`` rather than at 15, one below the reserved value, so counting can never produce it
       and the two readings are disjoint by construction. A nibble of 2 has two producers rather
       than one, so it does not by itself say which of them occurred. The first is a tile PLL
       reconfigure that measurably left the tile unpowered, read powered up before the call and
       unpowered after it, followed by the compensating reset that drove it again. The second is
       a tile that took its one cycle in the sweep and was then re-run by a clock group recovery,
       which counts a cycle for the group master and for every enabled edge tile it drives.
       ``0x1201C`` rules one producer out and does not identify the other. Its armed half reading
       zero means no recovery has ever fired on this driver instance, so the nibble came from the
       reconfigure. A non-zero armed half attributes nothing, because that half counts groups
       rather than tiles and is cumulative since construction while this count is cleared at the
       start of every reset, so it can stand for a recovery that fired on an earlier reset or on
       another group entirely. What attributes a recovery to a tile is the clock group recovery
       report, which names the arming tile and the group master, and on a boot where the register
       path has itself degraded that register may not answer, in which case that report is the
       remaining evidence. The second producer is demonstrated in tree
       rather than asserted here: the board-free claim
       ``a disabled tile in a group does not defeat the recovery`` scripts one reset failure on a
       single edge tile of this carrier's distribution and then requires the group master's
       nibble to read exactly 2, that master having taken one sweep cycle and one recovery cycle
       with no reconfigure having dropped it. Each global reset clears only the tiles it covers,
       and clears the
       exactness qualifier alongside the count it qualifies, so a host that has driven both
       reads all eight nibbles and no tile carries a stale qualifier into a later reset. A write
       is refused by name.
     - Yes. On such a board both commands walk their own four tiles, so a host that has driven
       both reads one per tile, and a nibble reading zero after a reset that covered the tile
       means the tile received no cycle at all. The reserved value is reachable on any board,
       distribution or not, because it depends only on how the reconfigure failed.
   * - ``PyRFdc::RecoveryCount``, at offset ``0x1201C``
     - None. The body reads members and names the driver instance nowhere.
     - The offset decoded to nothing, as above.
     - Bits 15:0 count the recoveries armed since construction and bits 31:16 count the ones that
       succeeded. Both halves saturate at ``0xFFFF``. Two counts and not one, because never
       armed, armed and failed, and armed and succeeded are three findings and a single number
       collapses two of them: a successful recovery leaves the reset returning success, so
       without this register a boot with several silent recoveries and a boot that needed none
       would look identical. A write is refused by name.
     - Yes, and it reads zero there forever, because no tile on such a board can arm a recovery.
       A zero on any board means no recovery was ever armed, which is not the same statement as a
       recovery that was not needed.

All four registers are admitted on read by the guard described above,
``PyRFdc::rejectIfDriverDead``, which admits ``0x12010`` through ``0x1201C`` for reads because
none of the four bodies dereferences the driver instance. On an instance whose construction never
produced a usable driver they answer with the values their members were declared with, which is
the truth for a driver that ran no sweep. All four are exposed on the host side as read-only
variables with no polling interval, for the reason recorded above for the initialization failure
reason register.

Reading the five reports on a board
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This driver emits five reports about the reset path and they are deliberately not all at the
same level. A board owner looking for any one of them needs to know which, because the host side
log is filtered by a global level whose default admits errors and discards warnings, so a report
emitted below that level does not reach a console at all however carefully it is worded.

The clock group recovery report, the line beginning ``clock group recovery armed by``, is
emitted through ``log_->error``. It is readable on a bridge running the default level with no
configuration of any kind. That is the level it is at because a recovery that fired means a
reset failed and had to be retried, which is never a routine outcome, and because it is the
half of the recovery evidence that has to survive the boot where the register path itself is
degraded and ``0x1201C`` cannot be read.

The withdrawn grouping report, the line beginning
``clock distribution grouping withdrawn from``, is emitted through ``log_->error`` for the same
reason the recovery report is: it is readable on a bridge running the default level with no
configuration of any kind, and a boot on which the driver could not resolve the cached topology
to an orderable master has to be visible without anyone knowing to read ``0x12014`` for it. It
fires once at construction and only when at least one tile's grouping was actually withdrawn,
so a board whose cache already satisfied the map invariant gains no new console output at all.
It names every tile it withdrew, as a tile type followed by a tile id, and states that the reset
falls back to per type ordering. It is emitted through the log channel and never through the
diagnostic error path, so no transaction verdict moves on account of it: a cache that cannot be
ordered is a topology outcome and not a transaction failure, and a reset that then succeeds
still reports success.

The uninterpretable generation report, the line beginning ``clock distribution topology not
obtained because the reported IP generation``, is emitted through ``log_->error`` for the same
reason the two above are: it is readable on a bridge running the default level with no
configuration of any kind. It fires once at construction and only when the reported IP
generation is above ``PYRFDC_IPTYPE_MAX_KNOWN``, which is the fourth source value at
``0x12010``, so a board in any other generation partition sees nothing new on the console. In
particular a pre-Gen3 board, which is the population most likely to have no distribution at
all, takes the raw decode and gains no line from this. It names the reported generation in
hexadecimal and states that the reset falls back to per type ordering. It is emitted through the
log channel and never through the diagnostic error path, so no transaction verdict moves on
account of it and construction does not fail: a generation this driver cannot interpret is one
more way for the topology to be unavailable, not a new class of error.

The refused topology query report, the line beginning ``clock distribution topology not
obtained because the documented getter returned non-success``, is emitted through
``log_->error`` for the same reason the three above are: it is readable on a bridge running
the default level with no configuration of any kind. It fires once at construction and only
when the reported IP generation is inside the range this driver asks and the documented getter
was then asked and answered with something other than success, which is the state the source
field at ``0x12010`` publishes as asked and refused. A board whose getter answers gains no line
from this, and neither does a pre-Gen3 board, which takes the raw decode and is never asked. It
names the refusal as the cause and deliberately does not name the reported generation, because
the generation is not what went wrong on this arm and naming it would make this line and the
one above harder rather than easier to tell apart. It states that the reset falls back to per
type ordering. It is emitted through the log channel and never through the diagnostic error
path, so no transaction verdict moves on account of it and construction does not fail: a
topology that was asked for and refused is a topology outcome and not a transaction failure.
As with the report above, this arm has never been observed on this carrier either, so this
line has no hardware evidence for or against it and has never been seen on a board.

The tile deferral report, the line beginning ``ADC global reset deferred`` or
``DAC global reset deferred``, is emitted through ``log_->warning`` and is **not** readable at
the default level. A deferral is a correct outcome that occurs on every global reset of every
board that has a clock distribution at all, so emitting it at the error level would print a
line per reset on a healthy board and train a reader to skip it.

**What an operator has to do to read the deferral report.** Lower the rogue log level to
warning on the process that hosts this memory slave, before the boot or the reset whose
deferral is wanted. In a rogue application that is a ``rogue.Logging.setLevel`` call taking
``rogue.Logging.Warning``, issued during bridge construction rather than afterwards, since a
reset the bridge performs at startup has already happened by the time a later call takes
effect. The setting is not retroactive: a session that did not lower the level before the
reset cannot recover the line afterwards, and the deferral has to be evidenced instead from
``0x12014``, which reports which tile each group is mastered by, together with which of the two
global reset entry points raised. Neither the deferral's absence nor the recovery's absence at
the default level says anything about whether that deferral or that recovery happened.

What this change is not proven to do
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A reviewer auditing this against a board that this repository cannot test should read the
following as part of the change itself, not as a caveat appended to it.

The grouping and the recovery are proven board-free and nothing more. Both are driven by the host
harness recorded in the reference facts section below, against hand-written shim headers and a
scripted driver stub. That is a claim about the shim's behavior and not automatically about the
Yocto build's, in the same terms that section states for every other claim this harness makes.

Neither topology source has been executed against a real driver in this repository. Both are
transcriptions of driver behavior read at upstream tag ``xilinx_v2026.1``, which is not present
on the host where this work was done. The documented getter is called through the vendor API and
so is only as correct as the IP generation gate in front of it; the raw decode reproduces
arithmetic read out of the driver source. A board whose topology reads back differently from what
the driver would have reported is a possibility this repository cannot exclude, and ``0x12010``
and ``0x12014`` exist so that a reader can see which source answered and what it found rather
than assume either.

No software recovery has ever been observed to work on the one carrier available to this work.
Across the converter failures recorded on it, a bridge relaunch, a service restart and a full
warm reboot all reported that nothing recovered; the only two things measured to clear the fault
were several hours of elapsed time and a debug channel system reset, each observed once. The
recovery described above is a mechanism, and the existence of that mechanism is not evidence that
it recovers anything. It may be a no-op on that board.

The accumulation of evidence on hardware is therefore limited to what the four registers report
on a healthy boot: which topology source answered, what it found, how many IPSM cycles each of
the eight tiles received, and whether any recovery armed. Nothing here has been observed clearing
a wedged converter, and no row above should be read as claiming that it does.

Reference facts
---------------

The initialization failure reason is a read-only register at ``0x1200C``. Its body reads one
member and names the driver instance nowhere, which is what lets it answer while everything else
is being refused. A write to it is refused. It is deliberately not exposed with a polling
interval on the host side, because a polled variable adds background transactions to a driver
that may be dead.

.. list-table:: Values reported at 0x1200C
   :header-rows: 1
   :widths: 10 34 56

   * - Value
     - Enumerator
     - Meaning
   * - 0
     - ``PYRFDC_INIT_OK``
     - Construction completed and the driver instance was configured.
   * - 1
     - ``PYRFDC_INIT_FAIL_NOT_COMPLETED``
     - Construction neither bailed out at a named step nor completed. Reached when the
       configuration initialize call reported a non-success.
   * - 2
     - ``PYRFDC_INIT_FAIL_BAREMETAL_LOOKUP``
     - The configuration lookup performed by the baremetal readiness check failed. Produced
       only in the baremetal build.
   * - 3
     - ``PYRFDC_INIT_FAIL_METAL_INIT``
     - The libmetal initialization call failed.
   * - 4
     - ``PYRFDC_INIT_FAIL_CONFIG_LOOKUP``
     - The configuration lookup for the driver instance failed.
   * - 5
     - ``PYRFDC_INIT_FAIL_REGISTER_METAL``
     - The libmetal device registration failed. Not produced in the baremetal build.

The six enumerators are declared at file scope in ``PyRFdc.h``, inside the include guard and
outside every conditional compilation block, so a host decoding the value does not need to know
which build produced the binary.

The metal error bypass register is retained at ``0x12004`` and its address is unchanged, because
it is published on the host side and removing it would break that interface. What it does has
been narrowed. Writing 1 to it previously discarded whatever error the current transaction had
recorded, which after these changes would have included every per-tile reset report and every
refusal produced by the guard above, so one register write would have turned a board that was
failing into a board that reported nothing. It now clears only errors that are not one of those
diagnostics, and it still clears the libmetal layer noise it exists for.

Nothing in this repository compiles this code in continuous integration. The documentation and
release workflows build documentation, check trailing whitespace and lint the Python tree, and
none of them touches the C++ under ``shared/Yocto/recipes-apps/pyrfdc/``, which is built only by
the Yocto recipe on a target build host. A board-free host harness compiles the production
source unchanged against hand-written shim headers and drives it through its real transaction
entry point. It is run by hand, and because nothing else records the invocation, it is recorded
here::

   make -s -C shared/Yocto/recipes-apps/pyrfdc/files/tests test

It prints one line per claim and a final ``RESULT PASS`` or ``RESULT FAIL``, and exits non-zero
when any claim failed. A pass is a claim about the shim's behavior and not automatically about
the Yocto build's: the shim is a second definition of the driver surface and can drift from the
real ``xrfdc.h``. The limits of that claim are written down in the shim fidelity section of
``shared/Yocto/recipes-apps/pyrfdc/files/tests/shim/xrfdc.h``.
