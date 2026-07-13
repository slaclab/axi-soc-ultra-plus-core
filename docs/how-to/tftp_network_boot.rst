TFTP Network Boot
=================

Boot the Linux kernel over Ethernet using TFTP instead of the SD card,
with automatic fallback to the on-SD image if the network path fails.

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
server-side without reflashing. It tries a per-MAC file
(``system.bit.bin.<mac>``, the MAC dash-separated and lowercased, e.g.
``system.bit.bin.fc-c2-3d-5a-9a-08``) first, then a generic
``system.bit.bin``. (U-Boot's ``tftpboot`` treats a ``:`` in a filename
as a ``hostIP:file`` separator, so the env uses ``setexpr gsub`` to
rewrite ``${ethaddr}``'s colons to dashes before the fetch.) This step
is **mandatory** in ``tftp-only`` mode:
if the bitstream fetch or ``fpga load`` fails, netboot aborts before the
kernel boots, so a net kernel never runs over an unprogrammed or stale
PL. A ``fallback`` build does **not** fetch a bitstream in U-Boot — its
PL is programmed later, after Linux boots, by ``startup-app-init``
running ``fpgautil`` on the SD card's ``system.bit`` (unchanged from a
normal SD boot). ``BOOT.BIN`` itself embeds an FSBL-programmed bitstream
in both modes; the ``tftp-only`` ``fpga load`` re-programs the PL over
it.

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
boot); a ``fallback`` board always reaches Linux.

.. warning::

   Because ``BOOT.BIN`` embeds a bitstream that the FSBL programs at
   power-on, the FPGA manager reports ``operating`` on every boot — even
   when no ``system.bit`` is present on the SD card and none is fetched
   over the network. This guarantees a programmed PL for the driver-load
   guard above, but it also means a ``fallback`` board that is missing its
   runtime ``system.bit`` will bind the DMA drivers against the bitstream
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
   U-Boot can fetch and ``fpga load`` it (see **How It Works**). Add
   ``-B`` to stage the generic ``system.bit.bin``, and ``-M <mac>``
   (repeatable) to stage per-MAC copies ``system.bit.bin.<mac>``:

   .. code-block:: bash

      scripts/provision_tftp_host.sh -b RealDigitalRfSoC4x2 -B -M fc:c2:3d:5a:9a:08

   The build produces ``linux/system.bit`` (a Vivado ``.bit`` with a
   header) next to ``image.ub``; U-Boot's ``fpga load`` needs the raw PL
   configuration ``.bin``, so ``-B`` converts it with ``bootgen``
   (``bootgen -arch zynqmp -image <bif> -process_bitstream bin``). Source
   your Vitis/Vivado ``settings64.sh`` first so ``bootgen`` is on
   ``PATH`` — the script exits with a clear message if it is not. The MAC
   in ``-M`` may be given with colons or dashes; it is stored
   **dash-separated and lowercased** (e.g.
   ``system.bit.bin.fc-c2-3d-5a-9a-08``) to match what the board's
   ``loadpl_net`` requests (it rewrites ``${ethaddr}``'s colons to dashes
   with ``setexpr``). ``fallback``-mode servers do not need any of this —
   omit ``-B``/``-M`` and only ``image.ub`` and the PXE config are staged.

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

2. Choose the board's boot mode at build time. The netboot mode is
   baked into U-Boot — and therefore into ``BOOT.BIN`` — when the Yocto
   image is built, via the ``-m`` flag to ``BuildYoctoProject.sh`` (see
   :doc:`../tutorial/first_soc_bringup` for the full build invocation):

   .. list-table::
      :header-rows: 1
      :widths: 25 75

      * - ``-m`` value
        - On a failed TFTP fetch
      * - ``fallback`` (default)
        - Boots the known-good ``image.ub`` from the SD card.
      * - ``tftp-only``
        - Prints ``TFTP-only build: not falling back to SD`` and halts
          at the ``ZynqMP>`` prompt; the SD image is never consulted.

   ``fallback`` is the default, so it is applied even when ``-m`` is
   omitted. The two modes produce byte-distinct ``BOOT.BIN`` images and
   embed distinct login-banner hostnames (see **Verification** below),
   so you can always tell which mode a board is actually running.

   The mode also decides where the PL bitstream comes from. A
   ``tftp-only`` build additionally requires the bitstream to be staged
   on the TFTP server (Step 1, ``-B``/``-M``) and will **halt** rather
   than boot if it is missing. A ``fallback`` build ignores any staged
   bitstream and programs the PL from the SD card's ``system.bit`` after
   Linux boots, exactly as a normal SD boot does.

   To get the resulting ``BOOT.BIN`` onto the board, see
   :doc:`sd_card_imaging` (for a fresh SD card) or
   :doc:`remote_bitstream_update` (for updating a board that is already
   imaged) in this how-to section.

   .. warning::

      To switch a board between ``fallback`` and ``tftp-only`` mode, it
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
   explicitly (``netboot`` itself does not) and watch each stage. At
   boot time it is U-Boot's ``bootcmd`` — ``if run netboot; then true;
   else ...; fi`` — that runs ``netboot`` and, on failure, triggers the
   mode-specific action from Step 2 (SD boot for ``fallback``, halt for
   ``tftp-only``).

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
      tftpboot 0x10000000 system.bit.bin
      fpga load 0 0x10000000 ${filesize}
      tftpboot 0x10000000 image.ub
      bootm 0x10000000

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

   Bytes transferred = 116833531

On a ``tftp-only`` build, the bitstream fetch and ``fpga load`` run
first — a successful load prints its own ``Bytes transferred`` line for
``system.bit.bin`` followed by no error from ``fpga load``. Once Linux
is up, ``startup-app-init`` confirms the PL is programmed before it
loads the DMA drivers:

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
in Step 1. It is the same for **both** boot modes, so it does not tell
you which mode's ``BOOT.BIN`` is running -- distinguish the modes by the
``BOOT.BIN`` md5, or by the TFTP-failure behavior (a ``fallback`` build
SD-boots; a ``tftp-only`` build halts). Finally, confirm the board is
reachable over the network:

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
   * - ``netboot`` takes ~2 minutes to fail (repeated ``TFTP error: -1
       (Request timeout)`` with no ``ICMP`` lines) before the SD
       fallback or halt runs
     - A silent TFTP black hole -- packets dropped rather than refused
       (e.g. a ``DROP`` firewall rule). Each of ``pxe get``'s config-name
       probes waits its full request timeout with no ICMP to
       short-circuit it (~110 s total)
     - Expected under a silent drop. A *port-closed* host (TFTP daemon
       stopped) instead fails in ~6 s because ICMP destination-unreachable
       aborts each probe; restore TFTP reachability to get the fast path
       back
   * - ``TFTP-only build: not falling back to SD`` followed by a halt
       at a bare ``ZynqMP>`` prompt, no kernel banner
     - Expected behavior: a ``tftp-only`` build halts by design when
       TFTP fails, instead of silently falling back to an SD image
     - Bring the TFTP host back up and run ``reset``, or reflash the
       board with a ``fallback`` build's ``BOOT.BIN``
   * - ``tftp-only`` netboot halts after a failed ``system.bit.bin``
       fetch, before any kernel fetch
     - The PL bitstream is not staged on the server; the mandatory
       ``loadpl_net`` step aborts netboot (never boots a net kernel over
       an unprogrammed PL)
     - Stage it with ``-B`` (and ``-M`` for a per-MAC copy) in Step 1,
       and confirm ``curl -sf tftp://10.0.0.1/system.bit.bin`` fetches
       it back from the host
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

On a ``fallback`` build, the SD fallback completes within roughly 6
seconds of the TFTP failure. This holds whether the network actively
refuses the connection or silently drops packets — both time out on
the same schedule. If more than ~6 seconds have passed with no kernel
banner, the fallback has already happened (or a ``tftp-only`` build
has halted); there is nothing to gain by waiting longer.

Notes
-----

- The full JTAG-based recovery procedure for a board that becomes
  unresponsive is not covered here; it is an emergency recovery path
  documented alongside this platform's bring-up tooling.

- The ``tftp-only`` bitstream step is deliberately mode-gated and
  all-or-nothing: in ``tftp-only`` it is mandatory (halt on failure),
  and in ``fallback`` it is skipped entirely so the SD ``fpgautil`` load
  stays authoritative and there is no double-program. It does **not**
  fetch a network bitstream to override an inserted SD in ``fallback``
  mode.

  .. note::

     A "best-effort" variant — where U-Boot programs a network bitstream
     if one is served but continues (rather than halting) if none is —
     would let a net bitstream override an inserted SD. That is out of
     scope here; the current design keeps each mode a coherent stack
     (full-network in ``tftp-only``, SD-owned PL in ``fallback``).
     If such a mode is added later, it must preserve netboot's ``&&`` failure
     chain — a fetch or ``fpga load`` failure must still be able to abort the
     boot — rather than relaxing the chain to ``;``. Weakening it to ``;``
     would run the kernel-fetch stages even after a failed ``dhcp`` (paying
     both timeouts on a dead network) and, because the env string is shared,
     would also disable the ``tftp-only`` guarantee that a net kernel never
     boots over an unprogrammed or stale PL.

- The per-MAC bitstream filename is dash-separated
  (``system.bit.bin.fc-c2-3d-5a-9a-08``), like the ``pxelinux.cfg``
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
