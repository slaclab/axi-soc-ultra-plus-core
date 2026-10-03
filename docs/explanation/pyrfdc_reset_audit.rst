PyRFdc Reset and Initialization Audit
======================================

Scope and evidence base
------------------------

This page audits ``PyRFdc::PyRFdc()`` (construction), ``PyRFdc::Reset()`` (tile
and global reset), and the related ``StartUp``, ``Shutdown``, ``CustomStartUp``,
``DynamicPLLConfig``, ``ClockSource``, ``PLLConfig`` and transaction handlers in
the same source file, together with the RFDC configuration source that all of
them depend on. Every finding is a table row with a verdict and a citation.
The hardware section records the board state captured before any code in this
audit's subject matter changed.

Citation conventions, stated once:

- A driver citation such as ``xrfdc.c:752`` refers to the Xilinx embeddedsw
  driver sources at tag ``xlnx_rel_v2026.1.1`` (librfdc 13.1, matching the
  2026.1 PetaLinux sysroot this board runs). This repository keeps a
  byte-identical local clone of that tag for citation checking.
- A documentation citation such as ``PG269 v2.6 p.41`` refers to the AMD
  Zynq UltraScale+ RFSoC RF Data Converter Gen 1/2/3/DFE Product Guide,
  version 2.6, dated May 29, 2025, using the printed page number. Where
  ``PyRFdc.cpp`` already carries a docs.amd.com anchor for the same API, the
  anchor is given alongside the page number.
- A ``PyRFdc.cpp:105-110`` style citation refers to the file at git revision
  ``1688d06``, the audited starting point of this file before any change in
  this project landed.

Verdict and action vocabulary
-------------------------------

Every row carries exactly one verdict and one action.

Verdicts:

- ``valid`` -- the concern as originally raised is a real defect or risk in
  the audited code, confirmed against the cited evidence.
- ``not valid`` -- the concern does not hold once checked against the cited
  evidence; the code or design it questioned is sound as written.
- ``partially valid`` -- part of the concern holds and part does not, or the
  concern is valid only under a condition the row states.

Actions:

- ``fixed: config ROM`` -- resolved by moving the RFDC configuration source
  from an empty devicetree property to the read-only configuration ROM this
  project adds to the bitstream.
- ``fixed: diagnostics`` -- resolved by the per-tile failure diagnostics,
  the libmetal log capture, or the related error-handling work in this
  project.
- ``constructor change`` -- a change to ``PyRFdc::PyRFdc()`` or
  ``PyRFdc::~PyRFdc()`` not yet made in this project; left for the
  deterministic-constructor work.
- ``thin-Reset change`` -- a change to make ``PyRFdc::Reset()`` a thin
  wrapper over the driver reset call, not yet made in this project; left for
  later reset-logic work.
- ``Init rewrite`` -- a change to the Python ``Rfdc.Init()`` sequence, not
  yet made in this project; left for later Python-side work.
- ``filed`` -- recorded as a known defect with no action taken in this
  project; tracked for a later fix.
- ``none`` -- no action is needed; the concern is answered or found sound as
  designed.

Constructor
------------

Verdicts on ``PyRFdc::PyRFdc()``, confirmed against the cited lines.

.. list-table::
   :header-rows: 1
   :widths: 8 28 34 12 18

   * - ID
     - Concern
     - Evidence
     - Verdict
     - Action
   * - C1
     - Members and the ``XRFdc`` instance are not deterministically
       initialized before use, and several early ``return`` statements leave
       the object half-constructed with the rest of the member
       initialization never reached
     - ``PyRFdc.cpp:69-72, 77-81, 84-89, 93-98`` are early returns that skip
       the member initialization at ``PyRFdc.cpp:112-124``; a disabled
       driver assert on Linux compiles to an infinite loop rather than a
       crash, ``xrfdc.h:795-799``
     - valid
     - constructor change
   * - C2
     - ``metal_finish()`` is called after a failed ``metal_init()``, which
       can leave libmetal's internal reference count in a state that makes
       the next ``metal_init()`` return early without reinitializing
     - ``PyRFdc.cpp:77-81``; libmetal's own reference-counted ``init``/
       ``finish`` pairing is documented behavior of the library this driver
       links
     - valid
     - constructor change
   * - C3
     - ``metal_device_close()`` is called on a ``deviceptr`` that
       ``XRFdc_RegisterMetal()`` never set when that same call failed
     - ``PyRFdc.cpp:91-98``
     - valid
     - constructor change
   * - C4
     - The return value of ``XRFdc_CfgInitialize()`` is not checked
     - ``PyRFdc.cpp:101``; the driver returns failure when its io region is
       not set up, ``xrfdc.c:283-287``
     - valid
     - constructor change
   * - C5
     - Every board on this platform ships an empty devicetree
       ``param-list`` property, so the only configuration source the stock
       driver had (``XRFdc_LookupConfig``) handed the constructor an
       uninitialized ``XRFdc_Config`` from the heap
     - ``xrfdc_sinit.c:240-247`` performs a short device-tree property read
       into a buffer it allocates with ``malloc`` and does not zero, and
       treats a short read as success; confirmed on the stock board image,
       the ``param-list`` property is present and 0 bytes long (see Hardware
       snapshot below)
     - valid
     - fixed: config ROM
   * - C6
     - The constructor overrides ``MaxSampleRate`` to fixed values (5.9
       GSPS for ADC tiles, 10.0 GSPS for DAC tiles) for every tile
     - ``PyRFdc.cpp:105-110`` (revision 1688d06); with a valid configuration
       the correct ADC value for this board is 5.0 GSPS, taken from the
       RFDC IP core's own build configuration, not 5.9
     - partially valid
     - fixed: config ROM
   * - C7
     - A guard reads ``RFdcInstPtr_->UpdateMixerScale`` before any driver
       call has ever written it
     - ``PyRFdc.cpp:212`` (revision 1688d06); ``XRFdc_CfgInitialize()`` never
       assigns ``UpdateMixerScale``, ``xrfdc.c:268-310``; the field is only
       ever written by ``XRFdc_SetMixerSettings``, ``xrfdc_mixer.c:326-336``,
       and an unexpected value there makes ``XRFdc_GetMixerSettings`` fail
       with "Invalid Fine mixer scale", ``xrfdc_mixer.c:949-957``
     - valid
     - fixed: config ROM
   * - C8
     - The constructor calls ``XRFdc_DynamicPLLConfig`` on every enabled
       tile at process start, before the application has programmed any
       board clock, which restarts each tile's power-on state machine
     - ``PyRFdc.cpp:198``; the driver call itself restarts the tile to the
       clock-detect state and back, ``xrfdc_clock.c:1821, 2021``
     - valid
     - constructor change
   * - C9
     - The constructor caches clock, PLL, QMC and mixer settings read back
       at process start, before board clocks are programmed, and these
       cached values are later replayed by ``Reset()``
     - ``PyRFdc.cpp:126-226`` (member population at construction) and
       ``PyRFdc.cpp:320-344`` (revision 1688d06, the replay inside
       ``Reset()``)
     - valid
     - thin-Reset change
   * - C10
     - The destructor calls ``XRFdc_RegisterMetal()`` a second time, which
       opens a new libmetal device rather than closing the one already open
     - ``PyRFdc.cpp:489-494``; each call opens a device by name,
       ``xrfdc_sinit.c:293-340``
     - valid
     - constructor change
   * - C11
     - What state must the constructor actually establish for the object to
       be usable after a PetaLinux boot
     - Tile and block enable state is read directly from IP registers on
       demand, ``xrfdc.c:2353-2385``; the only state the constructor needs
       to establish is a valid ``RFdc_Config`` and the libmetal io region,
       everything cached beyond that is derivable and the audit's own
       finding that it should not be cached at construction time
     - valid
     - constructor change

Reset
------

Verdicts on ``PyRFdc::Reset()``, including every corner case named in the
original scope and the verdict on whether restoring Vivado defaults inside
``Reset()`` is sound.

.. list-table::
   :header-rows: 1
   :widths: 8 28 34 12 18

   * - ID
     - Concern
     - Evidence
     - Verdict
     - Action
   * - R1
     - Whether restoring cached Vivado defaults inside ``Reset()``, ahead
       of a second ``XRFdc_Reset`` call, is a sound design
     - ``XRFdc_Reset()`` already restores the Vivado GUI configuration by
       design when it restarts a tile from state 0, ``xrfdc.c:726-731,
       752``; ``PyRFdc.cpp:320-344`` (revision 1688d06) restores cached
       settings and then issues a second ``XRFdc_Reset()`` at
       ``PyRFdc.cpp:358``, which wipes the restore it just performed; the
       restore writes also run on tiles that may not yet be back at state
       15, see PG269 v2.6 p.41, "Do not attempt to write to any tile
       specific registers while the power-on state machine is operating"
     - not valid
     - thin-Reset change
   * - R2
     - ``Reset(-1)`` only reported the status of the second reset pass; an
       earlier failing first-pass reset on the same tile was silently
       dropped
     - ``PyRFdc.cpp:316, 358`` (revision 1688d06): the first-pass status is
       assigned to a local never checked, and the loop variable is reused
       for the second pass's own status
     - valid
     - fixed: diagnostics
   * - R3
     - The global reset path gates a tile's first-pass reset on a PLL
       enable flag captured at construction time, not on the tile's live
       state
     - ``PyRFdc.cpp:315`` (revision 1688d06): ``pllDefault_[i][j].Enabled >
       0`` is the cached value from construction
     - valid
     - thin-Reset change
   * - R4
     - The return of ``XRFdc_DynamicPLLConfig`` is not checked inside
       ``Reset()``, and the call itself restarts the tile's state machine
     - ``PyRFdc.cpp:320`` (revision 1688d06); the restart behavior is in
       ``xrfdc_clock.c:1821, 2021``
     - valid
     - thin-Reset change
   * - R5
     - An explicit-tile ``Reset()`` call takes a different code path than
       the ``Tile_Id == -1`` global reset, with no settings restore and no
       second reset
     - ``PyRFdc.cpp:365-366`` versus ``PyRFdc.cpp:297-361`` (revision
       1688d06)
     - valid
     - thin-Reset change
   * - R6
     - The restart register is written to trigger a tile restart without
       first checking whether the tile's power-on state machine is already
       busy
     - ``xrfdc.c:1050-1055`` writes the restart register unconditionally;
       on the board before any code in this project changed, five of eight
       tiles already read Restart (0x04) non-zero at rest (see Hardware
       snapshot below)
     - valid
     - none
   * - R7
     - ``XRFdc_WaitForState`` returns success immediately if the tile is
       already at or above the requested state, which can mask that a
       restart never actually ran
     - ``xrfdc.c:966``, ``while (TileState < State)``
     - valid
     - none
   * - R8
     - A PLL-disabled tile (external clock source) never reports a locked
       PLL in the status registers the same way an internal-PLL tile does
     - ``XRFdc_GetPLLLockStatus`` reports a tile on an external clock source
       as locked by definition, ``xrfdc_clock.c:1144-1147``
     - valid
     - none
   * - R9
     - Gen 1 and Gen 3 tiles differ in ways the reset and status code must
       account for, including that the Gen 3 clock detector register is
       read unconditionally regardless of IP generation
     - A Gen 3 quad-ADC tile shutdown is gated by generation,
       ``xrfdc.c:1041-1047``; the clock detector register is documented as
       Gen 3/DFE only, PG269 v2.6 p.41, Table 31, p.44, Table 36
     - partially valid
     - fixed: diagnostics
   * - R10
     - What happens when ``Reset()`` is asked to act on a disabled tile or
       a disabled block
     - An explicit disabled ``Tile_Id`` fails with "not available"; the
       global ``Tile_Id == -1`` form simply skips a disabled tile,
       ``xrfdc.c:1033-1043``
     - not valid
     - none
   * - R11
     - A tile reset path can run more resets per call than intended: the
       global reset already issues two resets per tile, and the Python
       ``Rfdc.Init()`` sequence calls both a global and a per-tile reset on
       top of that
     - ``PyRFdc.cpp:316, 358`` (revision 1688d06, two resets inside one
       global reset call); the Python side issues ``ResetAllAdc``,
       ``ResetAllDac`` and then a per-tile ``Reset`` on every enabled tile
     - valid
     - Init rewrite
   * - R12
     - Whether the DAC datapath bypass mode is checked before the mixer
       setting is restored on a DAC block
     - ``PyRFdc.cpp:336`` (revision 1688d06) checks
       ``XRFDC_DAC_INT_MODE_FULL_BW_BYPASS`` on the DAC datapath register
       before restoring the mixer, and the equivalent check in the current
       constructor path reads the same register,
       ``shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp:466``
     - not valid
     - none

Restore-during-reset verdict
+++++++++++++++++++++++++++++

The restore-during-reset design in ``PyRFdc::Reset()`` is judged **not
valid**. ``XRFdc_Reset`` already restores the Vivado-configured settings as
part of its own documented behavior whenever it restarts a tile from state 0
(``xrfdc.c:726-731``, ``:752``). The code in revision 1688d06 performed its
own cached-settings restore and then called ``XRFdc_Reset`` a second time,
which both duplicates work the driver already does and immediately
overwrites the restore it just performed. The restore writes also land on
tiles that are not guaranteed to be back at state 15 yet, which PG269 v2.6
p.41 explicitly warns against ("Do not attempt to write to any tile specific
registers while the power-on state machine is operating"). There is no
condition under which the original design is sound; it is corrected by making
``Reset()`` a thin wrapper over a single ``XRFdc_Reset`` call per tile.

Related handlers
------------------

Verdicts on defects found in the other handlers audited alongside the
constructor and ``Reset()``.

.. list-table::
   :header-rows: 1
   :widths: 8 28 34 12 18

   * - ID
     - Concern
     - Evidence
     - Verdict
     - Action
   * - H1
     - The ``CustomStartUp`` bit layout the C++ side decodes does not match
       the bit layout the PyRogue device writes, so the end state always
       decodes to 0
     - ``PyRFdc.cpp:378-379`` (revision 1688d06) decodes ``EndState`` from
       bits 11:8 of the write data, while
       ``python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py:215-221``
       writes it at bit offset 4 within a 4-bit field
     - valid
     - thin-Reset change
   * - H2
     - ``CustomStartUp(-1)`` is forwarded directly to the driver call that
       expects an explicit tile id
     - ``PyRFdc.cpp:388`` (revision 1688d06); for a tile index of -1 the
       driver's tile base-address macro wraps to an unrelated tile,
       ``xrfdc_hw.h:2225-2227``
     - valid
     - thin-Reset change
   * - H3
     - ``XRFdc_IPStatus`` is read into a stack struct that is never
       zero-initialized first
     - ``PyRFdc.cpp:399`` (revision 1688d06); the driver leaves fields for
       disabled tiles unwritten, ``xrfdc.c:1086-1095``
     - valid
     - thin-Reset change
   * - H4
     - ``XRFdc_PLL_Settings`` is read into a stack struct that is never
       zero-initialized first, and the read's own return status is not
       checked before the struct is used
     - ``PyRFdc.cpp:1616-1619`` (revision 1688d06)
     - valid
     - thin-Reset change
   * - H5
     - Several handlers check a driver return against
       ``XRFDC_FAILURE`` rather than against ``XRFDC_SUCCESS``
     - ``PyRFdc.cpp:186-227`` and similar sites (revision 1688d06); every
       cited call in this project returns only 0 or 1, so the two forms are
       equivalent today but fragile against a future call with a richer
       return domain
     - valid
     - thin-Reset change
   * - H6
     - The ``RestartSM`` read path returns a hardcoded constant instead of
       the tile's actual restart register value
     - ``PyRFdc.cpp:3089-3092`` (revision 1688d06), ``data_ = 1;`` on every
       read
     - valid
     - thin-Reset change
   * - H7
     - ``StartUp``, ``Shutdown``, ``Reset`` and ``CustomStartUp`` failures
       in the original code carried no diagnostic detail beyond the call
       name and tile id
     - ``PyRFdc.cpp:262, 281, 372, 393`` (revision 1688d06)
     - valid
     - fixed: diagnostics
   * - H8
     - A failing ``XRFdc_DynamicPLLConfig`` can leave a tile parked at the
       clock-detect state rather than restoring it to where it started
     - ``PyRFdc.cpp:2700-2735`` (constructor-cached settings path);
       ``xrfdc_clock.c:1821, 2021`` only finishes the state machine when
       the earlier steps succeeded
     - valid
     - filed
   * - H9
     - ``ClockSource`` reads depend on the IP type field of the live
       config being correct
     - ``PyRFdc.cpp:2093-2113`` calls ``XRFdc_GetClockSource``, which is
       sound once the configuration source itself is valid (see concern C5)
     - partially valid
     - fixed: config ROM
   * - H10
     - A shared ``errMsg_`` member is cleared outside the transaction lock
       and read again after the lock is released, which can let one
       transaction's error be cleared or overwritten by a concurrent one
     - ``PyRFdc.cpp:3258-3260, 3763`` (revision 1688d06, clear outside the
       lock, read after the lock is released); the only master attached to
       this slave in the deployed topology is a single-worker
       ``TcpServer``, which serializes every ``doTransaction`` call, so the
       race is unreachable as deployed today
     - valid
     - fixed: diagnostics
   * - H11
     - A captured error message is passed to the logger as the format
       string rather than as an argument
     - ``PyRFdc.cpp:3768`` (revision 1688d06), ``log_->error(errMsg_.c_str())``
     - valid
     - fixed: diagnostics
   * - H12
     - ``MaxSampleRate`` and ``MinSampleRate`` PyRogue variables are
       labeled with GSPS units but carry values in MSPS
     - ``python/axi_soc_ultra_plus_core/rfsoc_utility/_RfdcTile.py:787,
       802`` declare ``units = 'GSPS'``; the driver returns the value in
       MSPS, ``xrfdc.c:2419, 2425``
     - valid
     - filed

Configuration source
----------------------

Verdicts on the configuration-of-record question: how ``PyRFdc`` obtains the
``XRFdc_Config`` it hands to ``XRFdc_CfgInitialize``.

.. list-table::
   :header-rows: 1
   :widths: 8 28 34 12 18

   * - ID
     - Concern
     - Evidence
     - Verdict
     - Action
   * - S1
     - Whether the Vivado-driven device-tree generator can emit the RFDC
       node, with its configuration payload, directly from the hardware
       description
     - The RFDC IP core is absent from every local hardware handoff file
       checked (0 of 6 contain any ``rf_data_converter`` reference); only
       the hand-written board device-tree stub defines the node, the same
       node ``XRFdc_RegisterMetal`` matches by name and ``compatible``
       string, ``xrfdc_sinit.c:140, 293-340``; closed as generator-required
       before any code in this project changed
     - valid
     - fixed: config ROM
   * - S2
     - Whether the empty ``param-list`` devicetree property can simply be
       removed now that it is no longer the configuration source
     - The device match inside ``XRFdc_RegisterMetal`` reads the first four
       bytes of ``param-list`` as a device id and pre-initializes its
       comparison variable to 0, ``xrfdc_sinit.c:140, 162-166``; an empty
       property still reads as 0 bytes successfully and matches device id
       0, so it must stay even though its contents carry no configuration
       data any more
     - not valid
     - none
   * - S3
     - Where the real ``XRFdc_Config`` now comes from
     - The configuration moved into a read-only configuration ROM generated
       at Vivado build time from the application's RFDC IP core
       configuration and placed in the bitstream; ``PyRFdc`` validates the
       ROM's header and payload before calling ``XRFdc_CfgInitialize``,
       ``shared/Yocto/recipes-apps/pyrfdc/files/PyRFdc.cpp:264-357``
     - valid
     - fixed: config ROM

Hardware snapshot before any code change
-------------------------------------------

This section records the board state captured on the test board before any
line of code in this project's subject matter (``PyRFdc.cpp``, the PyRogue
RFDC devices, or the configuration ROM build hook) changed.

Board identity at capture time: kernel ``6.18.10-xilinx-g4f7afe14f724``,
``librfdc.so.13.1``, ``libmetal.so.1.9.1``, RFDC IP version register
``0x02060B00``. The devicetree ``param-list`` property for the RFDC node was
present and 0 bytes long, the same byte unit as ``sizeof(XRFdc_Config)`` =
1880 for this librfdc.

Capture method: a read-only out-of-band tool opened the RFDC UIO device node
with ``O_RDONLY`` and mapped it with ``mmap(PROT_READ)``, then performed one
aligned 32-bit load per register with no writes anywhere, including never
touching the read-to-clear interrupt status register at tile offset 0x200.
Two consecutive captures taken within the same boot agreed on every field for
all eight tiles. No host PyRogue Root was connected to either port 9000 or
port 9002 at capture time. The pre-change capture and the later
reboot-persistence capture are distinguished by their boot ids, which
differ because a Linux reboot ran between them.

.. list-table:: Tile register snapshot, raw masked values, before any code change
   :header-rows: 1
   :widths: 10 12 14 14 12 14 20

   * - Tile
     - Restart (0x04)
     - Restart State (0x08)
     - CurrentState (0x0C)
     - Reset Count (0x38)
     - Clock Detector (0x84)
     - Common Status (0x228), bits [ClkPresent, SupplyUp, PowerUp, PllLocked]
   * - DAC0
     - 1
     - 1551
     - 6
     - 0
     - 0
     - 2 (0,1,0,0)
   * - DAC1
     - 1
     - 15
     - 3
     - 0
     - 0
     - 3 (1,1,0,0)
   * - DAC2
     - 1
     - 15
     - 3
     - 0
     - 0
     - 2 (0,1,0,0)
   * - DAC3
     - 1
     - 15
     - 3
     - 0
     - 0
     - 2 (0,1,0,0)
   * - ADC0
     - 0
     - 15
     - 15
     - 0
     - 1
     - 15 (1,1,1,1)
   * - ADC1
     - 0
     - 15
     - 15
     - 0
     - 1
     - 15 (1,1,1,1)
   * - ADC2
     - 0
     - 15
     - 15
     - 0
     - 1
     - 15 (1,1,1,1)
   * - ADC3
     - 1
     - 15
     - 6
     - 0
     - 0
     - 3 (1,1,0,0)

Reset Count is 0 on every tile in this capture, so the IP's own automatic
restart logic (loss of clock, supply instability, or PLL lock loss, PG269
v2.6 p.43) did not fire; the parked tiles were not a result of a hardware
auto-restart.

Reboot-persistence result: after a plain Linux reboot (no power cycle, a new
boot id), DAC0 through DAC3 cleared (Restart back to 0, CurrentState 15).
ADC3 did not clear: it read Restart 1, CurrentState 7 (one state further than
its pre-reboot CurrentState of 6, with Clock Detector still 0). The stock
application's own bring-up sequence then ran its own reset path and
re-created the identical parked state recorded in the table above on every
tile, with Reset Count still 0 throughout. No power cycle was performed at
any point in this capture sequence.
