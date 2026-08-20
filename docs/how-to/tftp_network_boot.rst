TFTP Network Boot
=================

Boot the Linux kernel over Ethernet using TFTP instead of the SD card,
with automatic fallback to the on-SD image if the network path fails.

.. note::

   Netboot is **opt-in**. The default build mode is ``sd-only``, which never
   attempts DHCP or TFTP at all, so a board on a network with no TFTP server
   pays none of the timeouts described below. Everything on this page applies
   to boards built with ``-m fallback`` or ``-m tftp-only``; see
   :ref:`Step 2 <tftp-boot-modes>` for the full mode table.

How It Works
------------

On power-up, the boot ROM loads ``BOOT.BIN`` (FSBL + PMU firmware + ATF +
U-Boot) from the SD card, exactly as in a normal SD boot. From there,
U-Boot fetches **only the kernel FIT image** (``image.ub``) over TFTP
instead of reading it from the SD card, then boots it with ``bootm``.
The SD card stays in the board and still holds a known-good
``image.ub``, so if the TFTP fetch fails, U-Boot falls back to booting
that on-SD image automatically. Nothing about the first boot stage
changes: only where the kernel FIT comes from.

The kernel FIT is fetched **PXE-first**. U-Boot first tries to download
a PXE ("pxelinux") config from the TFTP server —
``pxelinux.cfg/01-<MAC>`` (the board's MAC, dash-separated and
lowercased, with the ``01-`` ARP-hardware-type prefix) if a
board-specific file exists, otherwise ``pxelinux.cfg/default`` — parses
its ``KERNEL`` line, and boots the FIT that line names. Only if no PXE
config is served does U-Boot fall back to fetching ``image.ub``
directly by name. The PXE config lets you change a board's boot
behavior server-side — pointing it at a different FIT, for example —
**without reflashing U-Boot or editing the board's U-Boot
environment**. Either path still relies on ``serverip`` (the TFTP
server address) being set, explicitly or via DHCP.

On boards built **tftp-only** for diskless (no-SD) operation, U-Boot
also fetches the **PL bitstream** over TFTP and programs it with
``fpga load`` before booting the kernel, so the PL can be updated
server-side without reflashing. It probes four filenames, most- to
least-specific, preferring the Vivado ``.bit`` at each level:

.. code-block:: text

   system.bit.<mac>       (e.g. system.bit.fc-c2-3d-5a-9a-08)
   system.bin.<mac>
   system.bit
   system.bin

The MAC is dash-separated and lowercased. (U-Boot's ``tftpboot`` treats
a ``:`` in a filename as a ``hostIP:file`` separator, so the env uses
``setexpr gsub`` to rewrite ``${ethaddr}``'s colons to dashes before the
fetch.) A miss costs one immediate TFTP "not found" reply rather than a
timeout, so the extra probes are effectively free — **provided the server
is reachable**. If nothing answers at all, each probe instead waits its
full request timeout; see **Troubleshooting**.

``fpga load`` accepts **either** a Vivado ``.bit`` or a bootgen-produced
raw ``.bin`` — see :ref:`bitstream formats <tftp-bitstream-formats>`
below. This step is **mandatory** in ``tftp-only`` mode:
if the bitstream fetch or ``fpga load`` fails, netboot aborts before the
kernel boots, so a net kernel never runs over an unprogrammed or stale
PL. A ``fallback`` or ``sd-only`` build does **not** fetch a bitstream in
U-Boot — its PL is programmed later, after Linux boots, by
``startup-app-init`` running ``fpgautil`` on the SD card's ``system.bit``
(unchanged from a normal SD boot). ``BOOT.BIN`` itself embeds an
FSBL-programmed bitstream in every mode; the ``tftp-only`` ``fpga load``
re-programs the PL over it.

Why the PL must be programmed before the drivers load: the
``axi_memory_map`` and ``axi_stream_dma`` kernel modules bind to AXI
endpoints that only exist once the PL is configured, so loading them
against an unprogrammed PL produces cryptic DMA/AXI errors (or an AXI
bus hang). To prevent that, ``startup-app-init`` gates the ``insmod``
step on the FPGA manager: it reads ``/sys/class/fpga_manager/fpga0/state``
and loads the drivers only when the state is ``operating``. If the PL is
not programmed, it logs an error, **skips the driver load, and lets Linux
continue booting** rather than halting — so the board still comes up with
networking and a shell (a minimal recovery environment) instead of
stopping. The only path that deliberately halts before Linux is a failed
bitstream fetch in ``tftp-only`` mode (the netboot ``&&`` chain aborts the
boot); a ``fallback`` or ``sd-only`` board always reaches Linux.

.. warning::

   Because ``BOOT.BIN`` embeds a bitstream that the FSBL programs at
   power-on, the FPGA manager reports ``operating`` on every boot — even
   when no ``system.bit`` is present on the SD card and none is fetched
   over the network. This guarantees a programmed PL for the driver-load
   guard above, but it also means a ``fallback`` or ``sd-only`` board that
   is missing its runtime ``system.bit`` will bind the DMA drivers against
   the bitstream
   **frozen into BOOT.BIN at Yocto build time**, which may be stale
   relative to the firmware you intend to run. An ``operating`` state
   therefore confirms only that *a* bitstream is loaded, not that it is the
   *correct* one; use ``axiversiondump`` (printed near the end of
   ``startup-app-init``) to confirm the running firmware version and build
   timestamp.

The active U-Boot network stack is lwIP (U-Boot 2026.01,
``CONFIG_NET_LWIP``); a bound DHCP lease prints a line starting with
``DHCP client bound to address``. The netboot hooks are added to
U-Boot's ``CFG_EXTRA_ENV_SETTINGS`` macro, which is shared across boards
built on this platform.

Prerequisites
-------------

- The board has been imaged and booted at least once (see
  :doc:`sd_card_imaging` in this how-to section), or has had a bitstream
  loaded via :doc:`remote_bitstream_update`.
- A development host on the same network as the board, with
  ``dnsmasq`` and ``curl`` installed. The provisioning script in Step 1
  uses ``sudo`` to write the server configuration and launch the
  daemon.
- Serial console access for observing boot messages:

  .. code-block:: bash

     cu --line /dev/ttyUSB1 --speed 115200 --parity=none

.. note::

   The host IP, network interface, board IP, and serial device shown
   below (e.g. ``10.0.0.1``, ``eth2``, ``/dev/ttyUSB1``) are this lab's
   values — substitute your own. The netboot path does not depend on any
   particular subnet; the board takes its IP from whatever DHCP server is
   present on your network, and only ``serverip`` needs to be set
   explicitly to point at your TFTP host.

Steps
-----

1. Set up the host TFTP server using the provisioning script,
   :repo:`scripts/provision_tftp_host.sh`, provided in the
   platform repository. The ``-b BOARD`` argument is required and must
   match a directory name under ``axi-soc-ultra-plus-core/hardware``
   (``RealDigitalRfSoC4x2`` shown here as an example):

   .. code-block:: bash

      scripts/provision_tftp_host.sh -b RealDigitalRfSoC4x2

   The script is idempotent: it writes the TFTP server configuration
   (``/etc/dnsmasq.d/tftp-lab.conf``), stages the latest built
   ``image.ub`` for that board into the TFTP root (``/tftpboot``), stages
   a PXE config at ``/tftpboot/pxelinux.cfg/default``, and launches a
   standalone ``dnsmasq`` instance serving ``/tftpboot``. Re-running it
   against an already-provisioned host is a no-op — it does not re-prompt
   for ``sudo`` and does not start a second daemon. Do not hand-derive
   the TFTP server configuration yourself.

   The staged ``/tftpboot/pxelinux.cfg/default`` is minimal — it simply
   names the FIT to boot:

   .. code-block:: text

      LABEL Linux
      KERNEL image.ub

   To give one board different boot behavior than the rest, add a
   MAC-specific override file beside it, named for that board's MAC
   address as ``pxelinux.cfg/01-aa-bb-cc-dd-ee-ff`` (the ``01-`` prefix
   plus the MAC dash-separated and lowercased). U-Boot prefers the
   MAC-specific file over ``default`` when both are present, so you can
   repoint a single board — at a different FIT, say — without touching
   the shared ``default`` config or reflashing that board's U-Boot.

   For a **tftp-only** (diskless) board, also stage the PL bitstream so
   U-Boot can fetch and ``fpga load`` it (see **How It Works**).

   Add ``-B`` to stage the bitstream, and ``-M <mac>`` (repeatable) for
   per-board copies:

   .. code-block:: bash

      scripts/provision_tftp_host.sh -b RealDigitalRfSoC4x2 -B -M fc:c2:3d:5a:9a:08

   ``-B`` copies ``linux/system.bit`` across unconverted. That needs no
   Vitis environment, and it is the first name ``loadpl_net`` probes.
   Use ``-F bin`` instead to convert with ``bootgen``
   (``bootgen -arch zynqmp -image <bif> -process_bitstream bin``); that
   requires sourcing your Vitis/Vivado ``settings64.sh`` first to put
   ``bootgen`` on ``PATH``, and the script exits with a clear message if it
   is not there. ``-F`` implies ``-B``.

   The MAC in ``-M`` may be given with colons or dashes; it is stored
   **dash-separated and lowercased** (e.g.
   ``system.bit.fc-c2-3d-5a-9a-08``) to match what the board's
   ``loadpl_net`` requests (it rewrites ``${ethaddr}``'s colons to dashes
   with ``setexpr``). ``fallback``-mode servers do not need any of this —
   omit ``-B``/``-F``/``-M`` and only ``image.ub`` and the PXE config are
   staged. ``sd-only`` boards need no TFTP server at all, so this whole
   step is unnecessary for them.

   .. note::

      The formats are staged exclusively: whichever you pick, the other's
      names are removed, because a ``.bit`` and a ``.bin`` cannot both be
      useful at once. See
      :ref:`bitstream formats <tftp-bitstream-formats>` for why, and for
      the substantial load-time difference between them.

   The server is **TFTP-only** (``dnsmasq`` runs with DNS and DHCP
   disabled), so it is safe to run alongside an existing site DHCP
   server on the same segment — the board still gets its lease from
   that DHCP server, and this host only answers TFTP requests.

   If the script cannot auto-detect a built ``image.ub`` for the board
   (``No image.ub found for board ...``), point it at the file
   explicitly with ``-f``:

   .. code-block:: bash

      scripts/provision_tftp_host.sh -b RealDigitalRfSoC4x2 -f /path/to/linux/image.ub

   Before involving the board, confirm the host is serving the FIT by
   fetching it back over TFTP from the host itself:

   .. code-block:: bash

      curl -sf -o /tmp/verify_image.ub tftp://10.0.0.1/image.ub
      cmp /tmp/verify_image.ub /tftpboot/image.ub

   A clean ``curl`` exit and a matching ``cmp`` prove the TFTP path is
   good end-to-end without needing the board. To stop the server, kill
   the PID it recorded:

   .. code-block:: bash

      sudo kill "$(cat /run/dnsmasq-tftp-lab.pid)"

.. _tftp-boot-modes:

2. Choose the board's boot mode at build time. The boot mode is
   baked into U-Boot — and therefore into ``BOOT.BIN`` — when the Yocto
   image is built, via the ``-m`` flag to ``BuildYoctoProject.sh`` (see
   :doc:`../tutorial/first_soc_bringup` for the full build invocation):

   .. list-table::
      :header-rows: 1
      :widths: 25 75

      * - ``-m`` value
        - Boot behavior
      * - ``sd-only`` (default)
        - Never attempts netboot: no DHCP, no TFTP, and none of the
          ~85-110 s of timeouts a ``fallback`` board pays on a network with
          no reachable TFTP server. ``run netboot`` is still defined and
          available by hand at the ``ZynqMP>`` prompt for recovery.
      * - ``fallback``
        - Tries netboot first; on any failure boots the known-good
          ``image.ub`` from the SD card.
      * - ``tftp-only``
        - Tries netboot only; on failure prints ``TFTP-only build: not
          falling back to SD`` and halts at the ``ZynqMP>`` prompt. The SD
          image is never consulted.

   ``sd-only`` is the default, so it is applied even when ``-m`` is
   omitted, and a default build therefore does **not** netboot. The three
   modes produce byte-distinct ``BOOT.BIN`` images. To read a board's mode
   straight out of the artifact — no board required — grep the ``bootcmd``
   that was baked into it:

   .. code-block:: bash

      strings -a BOOT.BIN | grep -a '^bootcmd='

   .. code-block:: text

      bootcmd=echo SD-only build: skipping netboot; run sdboot           <- sd-only
      bootcmd=run netboot; run sdboot                                    <- fallback
      bootcmd=run netboot; echo TFTP-only build: not falling back to SD  <- tftp-only

   That works on a freshly built image or on the SD card's copy at
   ``/boot/BOOT.BIN``, and unlike a checksum it does not go stale between
   rebuilds.

   .. note::

      ``strings -a BOOT.BIN | grep -aE '^(netboot|loadpl_net|loadpl_skip)='``
      is a **weaker** check: ``loadpl_skip`` is selected by both ``fallback``
      and ``sd-only``, so that grep separates ``tftp-only`` from the other
      two but cannot tell those two apart. Only the ``bootcmd=`` line
      identifies a mode uniquely.

   Runtime behavior also distinguishes the modes: a ``fallback`` board
   SD-boots after its TFTP attempts fail, a ``tftp-only`` board halts, and
   an ``sd-only`` board prints ``SD-only build: skipping netboot`` and shows
   no ``DHCP client bound`` or TFTP lines at all. The login-banner hostname
   is **not** a mode indicator: it comes from the target name via
   ``hostname:pn-base-files`` and is identical in all three modes (see
   **Verification** below).

   The mode also decides where the PL bitstream comes from. A
   ``tftp-only`` build additionally requires the bitstream to be staged
   on the TFTP server (Step 1, ``-B``/``-F``/``-M``) and will **halt** rather
   than boot if it is missing. It also requires ``/boot/system.bit`` to be
   **absent** on the board: ``startup-app-init`` re-programs the PL from
   that file whenever it exists, overriding whatever U-Boot just fetched.
   A ``fallback`` or ``sd-only`` build ignores any staged
   bitstream and programs the PL from the SD card's ``system.bit`` after
   Linux boots, exactly as a normal SD boot does.

   To get the resulting ``BOOT.BIN`` onto the board, see
   :doc:`sd_card_imaging` (for a fresh SD card) or
   :doc:`remote_bitstream_update` (for updating a board that is already
   imaged) in this how-to section.

   .. warning::

      To switch a board between any two of the three modes, it
      is sufficient to replace only ``BOOT.BIN`` on the SD card's FAT
      boot partition. On boards imaged by this platform's tooling
      (1 GiB FAT32 boot partition), do that with a plain Linux ``cp``
      to the mounted ``/boot`` partition. **Never use U-Boot's
      ``fatwrite`` command and never use a raw ``mmc write``** to do
      this: U-Boot 2026.01's ``fatwrite`` is broken on that FAT32 boot
      partition (it corrupts the FSInfo free-count and fails every
      write with a bogus "no space left" error), and a raw ``mmc
      write`` risks bricking the boot partition entirely.

3. Run netboot manually from the ``ZynqMP>`` prompt. To reach the
   prompt, press any key on the serial console during the autoboot
   countdown to interrupt it, then step through the fetch by hand.

   These commands mirror the built-in ``netboot`` environment command
   (installed via ``CFG_EXTRA_ENV_SETTINGS``, see **How It Works**),
   which runs ``dhcp`` — skipped if a static ``ipaddr`` is already
   set — then tries ``pxe get`` / ``pxe boot`` and, only if no PXE
   config is served, falls back to ``tftpboot 0x10000000 image.ub`` then
   ``bootm 0x10000000``; ``run netboot`` performs the whole
   fetch-and-boot. Doing it by hand lets you set ``serverip``
   explicitly (``netboot`` itself does not) and watch each stage.

   On a ``fallback`` or ``tftp-only`` board, boot time reaches ``netboot``
   through U-Boot's ``bootcmd`` — ``run netboot; <mode-action>`` — which
   runs ``netboot`` and then the mode-specific action from Step 2 (SD boot
   for ``fallback``, halt for ``tftp-only``). The mode action runs whenever
   ``netboot`` **returns to U-Boot at all**: a successful boot hands control
   to the kernel and never comes back, so simply reaching the mode action is
   the failure signal. The fallback therefore does **not** depend on
   ``netboot`` reporting a nonzero exit code — some boot methods (notably
   ``pxe boot``) return 0 even when no kernel booted.

   An ``sd-only`` board's ``bootcmd`` is not of that form at all: it omits
   ``run netboot`` entirely, which is the whole point of the mode. The
   ``netboot``, ``loadpl_net``, and ``loadpl_skip`` environment variables are
   still defined there, though, so this manual sequence is exactly how you
   exercise netboot on such a board without rebuilding it.

   .. code-block:: text

      dhcp
      setenv serverip 10.0.0.1
      pxe get
      pxe boot

   ``pxe get`` downloads the ``pxelinux.cfg`` file (MAC-specific first,
   then ``default``) from ``serverip``, and ``pxe boot`` loads and boots
   the FIT its ``KERNEL`` line names. This build also provides distro
   boot, so ``run bootcmd_pxe`` is a one-line equivalent — but it runs
   its own ``dhcp`` and takes ``serverip`` from the DHCP response, so
   prefer setting ``serverip`` explicitly and running ``pxe get`` /
   ``pxe boot`` when your DHCP server does not hand out a TFTP
   ``next-server``.

   **Mixed addressing — board IP from DHCP, TFTP server set by hand.**
   When your DHCP server assigns the board's IP but does not advertise a
   usable TFTP ``next-server`` (or advertises the wrong one), set
   ``serverip`` yourself and let DHCP handle only the board address, then
   run the built-in ``netboot``:

   .. code-block:: text

      setenv serverip 10.0.0.1
      saveenv                    # optional: persist across reboots
      run netboot

   The shipped ``netboot`` **preserves a non-empty ``serverip`` across
   its own internal ``dhcp`` call**, and clears ``tftpserverip`` (which
   ``tftpboot`` and ``pxe`` would otherwise prefer over ``serverip``), so
   your TFTP-server choice stays authoritative. This is required because
   U-Boot's lwIP ``dhcp`` always overwrites ``serverip`` with the DHCP
   server's own address and may set ``tftpserverip`` from the DHCP
   next-server field. To hand TFTP addressing back to DHCP, clear it
   again with ``setenv serverip`` (and ``saveenv`` if you had persisted
   it).

   To fetch the FIT directly instead — the fallback path ``netboot``
   takes when no PXE config is served — skip the ``pxe`` commands and
   fetch ``image.ub`` by name:

   .. code-block:: text

      dhcp
      setenv serverip 10.0.0.1
      tftpboot 0x10000000 image.ub
      bootm 0x10000000

   ``dhcp`` acquires a lease from your network's DHCP server (a bound
   lease prints ``DHCP client bound to address``). Set ``serverip``
   explicitly to your TFTP host rather than relying on a DHCP
   ``next-server`` option. Use the load address ``0x10000000`` exactly
   as shown — this is the address this platform's boot flow is built
   around, not a generic default.

   On a ``tftp-only`` build, ``netboot`` first runs its ``loadpl_net``
   step to program the PL from the TFTP-served bitstream before fetching
   the kernel. To reproduce that by hand, fetch and ``fpga load`` the
   bitstream (``0x10000000`` is reused — ``fpga load`` consumes the
   buffer before the kernel is fetched to the same address):

   .. code-block:: text

      dhcp
      setenv serverip 10.0.0.1
      tftpboot 0x10000000 system.bit
      fpga load 0 0x10000000 ${filesize}
      tftpboot 0x10000000 image.ub
      bootm 0x10000000

   Substitute ``system.bin`` for ``system.bit`` if that is what you
   staged — ``fpga load`` handles both identically.

   If your network has no DHCP server, set a static IP instead (keep
   it volatile — do not ``saveenv`` — so a plain ``reset`` restores the
   DHCP path):

   .. code-block:: text

      setenv ipaddr 10.0.0.50
      setenv serverip 10.0.0.1
      setenv gatewayip 10.0.0.1
      setenv netmask 255.255.255.0
      run netboot

Verification
------------

A bound DHCP lease is the first sign networking is up:

.. code-block:: text

   DHCP client bound to address 10.0.0.xxx (123 ms)

A successful TFTP fetch reports the size of the FIT image (roughly
112 MiB for this platform's image):

.. code-block:: text

   Bytes transferred = 116835243

On a ``tftp-only`` build, the bitstream fetch and ``fpga load`` run
first — a successful load prints a ``Filename`` line naming whichever of
the four probed names hit, its own ``Bytes transferred`` line, and no
error from ``fpga load``. A ``.bit`` transfers 222 bytes more than the
equivalent ``.bin`` (the Vivado header); both program the same PL
configuration. ``fpga load`` prints nothing at all on success, so the only
sign it ran is the next command appearing — after roughly 16 s for a
``.bit``, or a fraction of a second for a ``.bin``. A long silent pause
directly after the bitstream's ``Bytes transferred`` line is therefore
expected, not a hang. Once Linux is up, ``startup-app-init`` confirms the
PL is programmed before it loads the DMA drivers:

.. code-block:: text

   /sys/class/fpga_manager/fpga0/state: operating

If the PL is not ``operating``, ``startup-app-init`` prints an
``ERROR: PL not programmed`` line and skips the driver load instead of
failing later with cryptic DMA errors.

Reaching a login prompt confirms the board booted:

.. code-block:: text

   SimpleRfSoc4x2Example login:

The banner hostname comes from the project name of the built image
(``SimpleRfSoc4x2Example`` here, set via ``hostname:pn-base-files``),
not from the ``hardware`` directory name ``RealDigitalRfSoC4x2`` used
in Step 1. It is the same for **all three** boot modes, so it does not tell
you which mode's ``BOOT.BIN`` is running -- distinguish the modes with the
``strings -a BOOT.BIN | grep -a '^bootcmd='`` check from Step 2, or by the
runtime behavior (a ``fallback`` build SD-boots after its TFTP attempts
fail; a ``tftp-only`` build halts; an ``sd-only`` build prints ``SD-only
build: skipping netboot`` and never touches the network). Finally, confirm
the board is reachable over the network:

.. code-block:: bash

   ping -c 4 10.0.0.xxx

Troubleshooting
---------------

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Symptom
     - Cause
     - Fix
   * - No ``Bytes transferred`` line; ``TFTP error: -1 (Request
       timeout)``, then a kernel banner boots anyway
     - TFTP fetch failed; a ``fallback`` build booted the on-SD
       ``image.ub`` instead
     - Confirm the host is serving the FIT with the host-side ``curl``
       check in Step 1, and that ``serverip`` on the board points at
       that host
   * - ``pxe get`` fails or is skipped; ``netboot`` silently used the
       direct ``image.ub`` fetch instead of a PXE config
     - No ``pxelinux.cfg`` file is being served (missing, misnamed, or
       not fetchable over TFTP)
     - Confirm ``/tftpboot/pxelinux.cfg/default`` exists and fetch it
       back from the host with the same TFTP check as Step 1
       (``curl -sf tftp://10.0.0.1/pxelinux.cfg/default``)
   * - ``netboot`` takes 1-2 minutes to fail (repeated ``TFTP error: -1
       (Request timeout)``) before the SD fallback or halt runs
     - No TFTP server is answering. ``netboot`` is PXE-first, so
       ``pxe get`` walks 13 ``pxelinux.cfg`` names before the direct FIT
       fetch, and each of those 14 attempts waits its **full** request
       timeout (~6 s) when nothing replies: ~85 s with the daemon stopped,
       ~110 s under a silent ``DROP`` firewall rule
     - Expected when TFTP is unreachable, and **not** a hang. ICMP
       ``destination unreachable`` does *not* shorten it -- a stopped
       daemon does emit it, and each attempt still times out anyway, so
       the presence or absence of ICMP lines does not distinguish the two
       cases. Restore TFTP reachability: once the server answers, a
       missing file draws an immediate ``TFTP error: 256`` refusal and the
       whole 14-name walk costs almost nothing. If the board has **no**
       TFTP server by design, rebuild it with ``-m sd-only`` (Step 2) --
       that removes ``run netboot`` from ``bootcmd`` entirely and is the
       structural fix rather than a workaround
   * - ``TFTP-only build: not falling back to SD`` followed by a halt
       at a bare ``ZynqMP>`` prompt, no kernel banner
     - Expected behavior: a ``tftp-only`` build halts by design when
       TFTP fails, instead of silently falling back to an SD image.
       (An ``sd-only`` build never prints this, since it never runs
       ``netboot`` from ``bootcmd``.)
     - Bring the TFTP host back up and run ``reset``, or reflash the
       board with a ``fallback`` or ``sd-only`` build's ``BOOT.BIN``
   * - An ``sd-only`` board drops to ``ZynqMP>``, or pays TFTP timeouts
       anyway, despite ``bootcmd`` containing no ``run netboot``
     - SD boot itself failed, so ``distro_bootcmd`` fell through the
       ``mmc`` targets to its trailing ``pxe``/``dhcp`` targets. A missing
       or corrupt ``/boot/boot.scr`` or ``/boot/image.ub`` is the usual
       cause; in ``sd-only`` there is no netboot path masking it
     - Confirm both files are present on the FAT boot partition (they are
       staged by ``BuildYoctoProject.sh`` and validated in its release file
       list). Re-image per :doc:`sd_card_imaging` if either is missing
   * - ``tftp-only`` netboot halts after all four bitstream fetches fail,
       before any kernel fetch
     - The PL bitstream is not staged on the server under any of the
       probed names; the mandatory ``loadpl_net`` step aborts netboot
       (never boots a net kernel over an unprogrammed PL)
     - Stage it in Step 1 with ``-B`` (and ``-M`` for a per-MAC copy), then
       confirm ``curl -sf tftp://10.0.0.1/system.bit`` (or
       ``.../system.bin``) fetches it back from the host
   * - Board loads a stale bitstream even after re-running
       ``provision_tftp_host.sh``
     - A per-MAC name outranks the generic one, and a run without ``-M``
       cannot clean per-MAC leftovers — it only warns about them
     - Re-run with ``-M <board-MAC>``, which refreshes that per-MAC copy and
       removes the other format's per-MAC name
   * - ``tftp-only`` board fetches and loads a bitstream, but the PL ends up
       running the SD card's one instead
     - ``startup-app-init`` re-programs the PL with ``fpgautil`` whenever
       ``/boot/system.bit`` exists, roughly 15 s into boot, discarding what
       U-Boot loaded. ``tftp-only`` presumes a diskless board with no such
       file
     - Remove ``/boot/system.bit`` on boards that boot ``tftp-only``; the
       served bitstream is only authoritative when it is absent
   * - ``ERROR: PL not programmed`` at the end of boot; no runtime
       application starts
     - ``startup-app-init`` found ``fpga_manager/fpga0/state`` not
       ``operating`` and skipped the DMA driver load
     - On a ``tftp-only`` board confirm the bitstream staged and
       ``fpga load`` succeeded; on an SD board confirm ``/boot/system.bit``
       is present and valid
   * - No ``DHCP client bound`` line appears at all
     - The board is not getting a DHCP lease on this network
     - Use the static-IP override shown in Step 3
   * - ``fatwrite`` fails with "no space left" even though the FAT
       partition has free space
     - U-Boot 2026.01's ``fatwrite`` is broken on this platform's
       1 GiB FAT32 boot partition (corrupts the FSInfo free count)
     - Do not use ``fatwrite`` or raw ``mmc write``; boot Linux and
       ``cp`` the new ``BOOT.BIN`` to the mounted boot partition (see
       the warning in Step 2)
   * - TFTP fetches fail after ``dhcp`` even though ``serverip`` was set
       correctly by hand
     - lwIP ``dhcp`` overwrote ``serverip`` with the DHCP server's own
       address, or set ``tftpserverip`` from the DHCP next-server (which
       ``tftpboot`` and ``pxe`` prefer over ``serverip``)
     - Set ``serverip`` **before** ``run netboot`` — the shipped
       ``netboot`` preserves a non-empty ``serverip`` across its internal
       ``dhcp`` and clears ``tftpserverip``. To return to DHCP-supplied
       addressing, clear it with ``setenv serverip``
   * - A PXE config is served and ``pxe boot`` runs, but no kernel boots
       and the board does not fall back to SD (or halt) as expected
     - The PXE label's ``KERNEL`` FIT could not be retrieved; upstream
       ``pxe boot`` returns exit status 0 even on that failure
     - Handled by design: ``bootcmd`` is sequential
       (``run netboot; <mode-action>``), so the SD fallback (or
       ``tftp-only`` halt) runs regardless of ``netboot``'s exit code.
       Confirm the ``KERNEL`` file named in the ``pxelinux.cfg`` is
       actually served (``curl -sf tftp://<serverip>/<name>``)

How long the SD fallback takes depends entirely on whether the TFTP
server *answers*. A single ``tftpboot`` gives up after roughly 6 seconds,
but ``netboot`` is PXE-first: ``pxe get`` tries 13 ``pxelinux.cfg`` names
ahead of the direct FIT fetch, so a ``fallback`` board on a network with
**no reachable TFTP server** pays that timeout 14 times over — about
85 seconds with the daemon stopped, and about 110 seconds if packets are
silently dropped. When the server *is* reachable and merely missing a
file, every attempt is refused immediately and the fallback is effectively
instant. Allow up to ~2 minutes before concluding a board has hung, and
see the ``Request timeout`` entry in **Troubleshooting** above.

That full 14-attempt cost needs a **reachable DHCP server** as well as an
unreachable TFTP one. ``netboot`` starts with ``dhcp`` in an ``&&`` chain, so
on a network with no DHCP server at all it short-circuits at the DHCP
timeout and never reaches the TFTP attempts. An ``sd-only`` board pays
neither: its ``bootcmd`` contains no ``run netboot``, so there is no DHCP
attempt and no TFTP attempt on the boot path at all.

.. note::

   ``sd-only`` removes netboot from the **success** path, not from every
   possible path. ``sdboot`` is ``run distro_bootcmd``, which walks
   ``boot_targets`` in order; the ``mmc`` targets come first and a healthy SD
   card short-circuits there, but a board whose SD boot *fails* still falls
   through to ``distro_bootcmd``'s trailing ``pxe`` and ``dhcp`` targets and
   can pay the usual timeouts there. That tail is identical to a
   ``fallback`` build's and is unchanged by this mode. Read the list off an
   artifact with ``strings -a BOOT.BIN | grep -a '^boot_targets='``.

Notes
-----

- The full JTAG-based recovery procedure for a board that becomes
  unresponsive is not covered here; it is an emergency recovery path
  documented alongside this platform's bring-up tooling.

- The ``tftp-only`` bitstream step is deliberately mode-gated and
  all-or-nothing: in ``tftp-only`` it is mandatory (halt on failure),
  and in ``fallback`` and ``sd-only`` it is skipped entirely so the SD
  ``fpgautil`` load stays authoritative and there is no double-program. It
  does **not** fetch a network bitstream to override an inserted SD in
  either of those modes.

  .. note::

     A "best-effort" variant — where U-Boot programs a network bitstream
     if one is served but continues (rather than halting) if none is —
     would let a net bitstream override an inserted SD. That is out of
     scope here; the current design keeps each mode a coherent stack
     (full-network in ``tftp-only``, SD-owned PL in ``fallback``, and no
     network on the boot path at all in ``sd-only``).
     If such a mode is added later, it must preserve netboot's ``&&`` failure
     chain — a fetch or ``fpga load`` failure must still be able to abort the
     boot — rather than relaxing the chain to ``;``. Weakening it to ``;``
     would run the kernel-fetch stages even after a failed ``dhcp`` (paying
     both timeouts on a dead network) and, because the env string is shared,
     would also disable the ``tftp-only`` guarantee that a net kernel never
     boots over an unprogrammed or stale PL.

- The per-MAC bitstream filename is dash-separated
  (``system.bit.fc-c2-3d-5a-9a-08``), like the ``pxelinux.cfg``
  MAC form but without the ``01-`` prefix. U-Boot's ``tftpboot`` parses
  the first ``:`` in a filename as a ``hostIP:file`` separator, so a
  colon-form name (``${ethaddr}`` verbatim) is silently mis-parsed and
  never fetched; ``loadpl_net`` therefore rewrites the colons to dashes
  with ``setexpr gsub`` (requires ``CONFIG_CMD_SETEXPR`` + ``CONFIG_REGEX``,
  both on in the ZynqMP defconfig) before the fetch.

- ``BOOT.BIN`` embeds the XSA bitstream by default. This follows from
  meta-xilinx's ``xilinx-bootbin`` recipe: on the ``zynqmp-user`` machine
  (which does not set the ``fpga-overlay`` ``MACHINE_FEATURE``),
  ``BIF_BITSTREAM_ATTR`` defaults to ``bitstream``, so the FSBL programs the
  PL from ``download-zynqmp-user.bit`` at power-on and the FPGA manager reads
  ``operating`` before Linux starts. To build a ``BOOT.BIN`` that contains
  **no** bitstream — for example, to exercise the ``startup-app-init``
  driver-load guard on a board with no ``system.bit`` and no network
  bitstream — set ``BIF_BITSTREAM_ATTR = ""`` in ``build/conf/local.conf``
  (or the machine ``.conf``) and rebuild ``xilinx-bootbin``. With no embedded
  bitstream the FPGA manager stays out of ``operating`` until something (the
  SD ``fpgautil`` load, or the ``tftp-only`` ``fpga load``) programs the PL,
  and the guard then skips the driver load as intended.

.. _tftp-bitstream-formats:

Bitstream formats: ``.bit`` and ``.bin``
----------------------------------------

``fpga load`` on this platform accepts a Vivado ``.bit`` and a
bootgen-produced raw ``.bin``, and both program the same PL design. They
are not, however, the same bytes. Beyond the ``.bit``'s 222-byte Vivado
header (34,437,578 B versus 34,437,356 B for this design), ``bootgen``
also byte-reverses every 32-bit word — the sync word is ``AA 99 55 66`` in
the ``.bit`` and ``66 55 99 AA`` in the ``.bin``, and the bus-width-detect
pattern ``00 00 00 BB`` / ``11 22 00 44`` is reversed the same way.
Stripping the header off a ``.bit`` therefore does **not** produce the
``.bin``: 2,363,968 of 34,437,356 bytes differ, a figure that looks small
only because ``0x00000000`` and ``0xFFFFFFFF`` words are invariant under a
word swap. An unconverted ``.bit`` loads because the PCAP auto-detects bus
width and endianness from that pattern ahead of the sync word — not
because the two payloads agree.

The reason is in U-Boot's ZynqMP driver (``drivers/fpga/zynqmppl.c``).
``zynqmp_load()`` calls its format validator **only** on ancient PMU
firmware:

.. code-block:: c

   if (zynqmp_firmware_version() <= PMUFW_V1_0) {
           ...
           if (zynqmp_validate_bitstream(desc, buf, bsize, bsize, &swap))
                   return FPGA_FAIL;
           ...
   } else {
           bstype = 0;          /* modern PMUFW: no validation at all */
   }

On any current PMUFW the ``else`` branch is taken: U-Boot performs no
sync-word scan, no byte-swap, and no format check, and hands the buffer
verbatim to the PMU via ``PM_FPGA_LOAD``. The PMU's PCAP loader skips the
``.bit`` header itself.

.. important::

   The two formats do not cost the same to load. Measured on this design's
   34 MB bitstream (RFSoC 4x2, PMUFW from Vivado 2026.1):

   .. list-table::
      :header-rows: 1
      :widths: 20 25 25

      * - Format
        - ``fpga load``
        - Total netboot
      * - ``.bin``
        - ~0.2 s
        - ~40 s
      * - ``.bit``
        - ~16.6 s
        - ~60 s

   The TFTP transfer is identical either way (~17.6 MiB/s for both), so the
   entire difference falls inside ``fpga load`` — consistent with the PMU
   performing the word swap in software. Because ``loadpl_net`` prefers
   ``.bit``, a board served one pays this on **every** boot. Prefer
   ``-F bin`` where boot time matters and ``bootgen`` is available.

.. warning::

   None of this holds on PMUFW ≤ v1.0. There the validator runs,
   ``check_data()`` locates the sync word at a nonzero offset, and the
   load fails with ``Bitstream is not validated yet (diff ...)``. A raw
   ``.bin`` is the only format that works across both, which is why
   ``provision_tftp_host.sh -F bin`` still converts.

   Staging both formats is **not** a way to cover both PMUFW generations.
   ``loadpl_net``'s if/elif chain selects on *fetch* success, and the
   ``fpga load`` result for whichever name it fetched is final. On
   PMUFW ≤ v1.0 a co-present ``.bit`` would be fetched first, fail to load,
   and halt the boot with the ``.bin`` sitting there untried.

``loadpl_net`` probes ``.bit`` before ``.bin`` at each specificity level,
so a leftover ``system.bit`` outranks a ``system.bin``.
``provision_tftp_host.sh`` removes the unselected format's names — the
generic one and one per ``-M`` MAC — so re-running it cannot leave a stale
file of the other format behind. Without ``-M`` it cannot know the board's
MAC and so cannot clean a per-MAC leftover; it warns about any it finds,
since a per-MAC name outranks the generic one.
