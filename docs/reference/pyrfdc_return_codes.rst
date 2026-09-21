PyRFdc Return Codes and Initialization Guard
============================================

This page documents two properties of ``shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp``,
the rogue memory slave that exposes the Xilinx RF Data Converter driver over AXI-Lite: how it
compares the status values the driver hands back, and which register offsets it still answers
on an instance whose construction never produced a usable driver instance. Both are behavior
changes against the previously released file, and this file is consumed by every project built
on this core, so the detail below is written for a reviewer auditing the change against a board
this repository has no access to. The AXI-Lite addressing model itself is documented on the
``Register Map`` page.

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

Line numbers below are as of the commit that made the change. The durable anchor for each row is
the enclosing function together with the driver call named in the same cell.

.. list-table:: Behavior change at each converted comparison site
   :header-rows: 1
   :widths: 24 26 26 24

   * - Call site
     - Before the change
     - After the change
     - Can this arise on a board with no clock distribution
   * - ``PyRFdc::PyRFdc``, line 317, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the tile's clock
       source and PLL configuration are read and its four blocks are walked.
     - Only a success reads the tile. Any other status leaves the tile's shadow configuration at
       its initialized value and skips its block loop.
     - Undetermined here. This repository has one carrier and that carrier has clock
       distribution. This row runs on every construction on every board, so it is one of the
       seven to check first.
   * - ``PyRFdc::PyRFdc``, line 320, ``XRFdc_GetClockSource``
     - A status outside the documented pair stores whatever the call left in the output
       argument as the tile's default clock source.
     - Only a success stores it. The default clock source keeps its initialized value otherwise.
     - Undetermined here, for the same reason. Runs on every construction. The stored value is
       later passed back to ``XRFdc_DynamicPLLConfig`` in the reset sweep, so a wrong value
       persists past construction.
   * - ``PyRFdc::PyRFdc``, line 325, ``XRFdc_GetPLLConfig``
     - A status outside the documented pair stores the returned PLL settings and then issues
       ``XRFdc_DynamicPLLConfig`` with them.
     - Only a success stores them and issues the reconfigure. Any other status issues no
       reconfigure at all from the constructor.
     - Undetermined here. Runs on every construction, and this is the row that changes whether
       a converter is reconfigured during construction, so a reviewer with such a board should
       check this row alongside line 317.
   * - ``PyRFdc::PyRFdc``, line 336, ``XRFdc_CheckBlockEnabled``
     - A status outside the documented pair is read as an enabled block, so the block's default
       quadrature and mixer settings are read.
     - Only a success reads them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, line 338, ``XRFdc_GetQMCSettings``
     - A status outside the documented pair stores the returned quadrature settings as the
       block's defaults.
     - Only a success stores them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, line 343, ``XRFdc_CheckDigitalPathEnabled``
     - A status outside the documented pair is read as an enabled digital path, so the block's
       mixer defaults are read. The second half of the condition, the mixer scale test, is
       unchanged.
     - Only a success reads the mixer defaults.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::PyRFdc``, line 347, ``XRFdc_GetMixerSettings``
     - A status outside the documented pair stores the returned mixer settings as the block's
       defaults.
     - Only a success stores them.
     - Undetermined here. Runs on every construction.
   * - ``PyRFdc::Reset`` global sweep, line 476, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the tile is reset,
       PLL reconfigured and its blocks restored.
     - Only a success touches the tile. Any other status skips the whole tile.
     - Undetermined here. Reached by a host write to the global reset offset, which is the
       first thing the pyrogue ``Init()`` sequence does, so it runs on every bring-up.
   * - ``PyRFdc::Reset`` global sweep, line 498, ``XRFdc_CheckBlockEnabled``
     - A status outside the documented pair is read as an enabled block, so the block's
       quadrature and mixer settings are restored.
     - Only a success restores them.
     - Undetermined here. Reached on every bring-up, as above.
   * - ``PyRFdc::Reset`` global sweep, line 512, ``qmcStatus`` set by ``XRFdc_SetQMCSettings``
       at line 508. **Different kind of change: this tests a status an earlier call produced,
       not the call it wraps.**
     - The quadrature event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, so an event is raised for settings that may never have been applied.
       The settings status is separately recorded against the tile either way.
     - The event update is raised only when the settings call reported success. The recorded
       per-tile attribution is unchanged, because it is taken before this guard.
     - Undetermined here. Reached on every bring-up. What changes is which returns reach the
       code inside the branch rather than which returns count as a failure.
   * - ``PyRFdc::Reset`` global sweep, line 521, ``XRFdc_CheckDigitalPathEnabled``
     - A status outside the documented pair is read as an enabled digital path, so the block's
       mixer settings are restored.
     - Only a success restores them.
     - Undetermined here. Reached on every bring-up.
   * - ``PyRFdc::Reset`` global sweep, line 537, ``mixerStatus`` set by
       ``XRFdc_SetMixerSettings`` at line 533. **Different kind of change: this tests a status
       an earlier call produced.**
     - The mixer event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``.
     - The event update is raised only when the settings call reported success. The recorded
       per-tile attribution is unchanged.
     - Undetermined here. Reached on every bring-up. Same kind of change as line 512.
   * - ``PyRFdc::Reset`` global sweep, line 556, ``XRFdc_CheckTileEnabled``
     - A status outside the documented pair is read as an enabled tile, so the second reset of
       that tile is issued after its settings were restored.
     - Only a success issues the second reset.
     - Undetermined here. Reached on every bring-up. This is the second of the two resets the
       sweep performs per tile.
   * - ``PyRFdc::MixerSettings``, line 947, ``status`` set by ``XRFdc_SetMixerSettings`` at
       line 946. **Different kind of change: this tests a status an earlier call produced.**
     - The mixer event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, and its own return then overwrites the status this entry point
       reports, so a status outside the documented pair could be replaced by a success and
       never reported.
     - The event update is raised only when the settings call reported success. A status
       outside the documented pair is no longer overwritten, so the entry point reports the
       error.
     - Undetermined here. Reached only by a host write to the mixer settings apply index, not
       by the bring-up sequence.
   * - ``PyRFdc::QMCSettings``, line 1033, ``status`` set by ``XRFdc_SetQMCSettings`` at line
       1032. **Different kind of change: this tests a status an earlier call produced.**
     - The quadrature event update is raised whenever the settings call did not return
       ``XRFDC_FAILURE``, and its own return then overwrites the reported status.
     - The event update is raised only when the settings call reported success, and a status
       outside the documented pair is reported rather than overwritten.
     - Undetermined here. Reached only by a host write to the quadrature settings apply index.
   * - ``PyRFdc::MtsEnabled``, line 3338, ``XRFdc_CheckTileEnabled``
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
which returns are treated as a failure. Two of them, at lines 947 and 1033, additionally stop a
status outside the documented pair from being overwritten by the event update's own return,
which is why the entry point reports an error after the change where it reported none before.

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
