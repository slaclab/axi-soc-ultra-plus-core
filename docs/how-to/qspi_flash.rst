Program QSPI Flash Memory
=========================

Use Xilinx ``program_flash`` (part of Vivado / Vitis) to write the Yocto
boot images directly to on-board QSPI flash.  For NAND flash, see
:doc:`nand_flash`, which uses the same workflow with a different
``-flash_type`` and ``image.ub`` offset.

Prerequisites
-------------

- Vivado / Vitis installed with ``program_flash`` on ``PATH``.
- JTAG cable connected and board powered on.
- A completed Yocto build.  Boot images are located at:
  ``firmware/build/YoctoProjects/<your-target-dir>/linux/``
- For the scripted recipe: the packaged Yocto tarball at
  ``firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz``

.. note::

   Set the correct boot mode before or after flashing using XSCT
   (see **Change Boot Mode via XSCT** in this how-to section).

.. note::

   ``BuildYoctoProject.sh`` packages the FSBL built from the design's own
   ``.xsa`` as ``linux/zynqmp_fsbl.elf``, so both the ``linux/`` build output
   and the tarball carry one.  The scripted recipe picks it up automatically
   with no ``-e``; pass it to the manual recipe as
   ``-fsbl linux/zynqmp_fsbl.elf``.

.. warning::

   Do **not** rely on the generic FSBL that Vitis bundles in
   ``data/xicom/cfgmem/cfgmem_util/cfgmem_fsbl.zip`` (for this flash type,
   ``zynqmp_qspi_dual_parallel_x8_fsbl.elf``).  It carries a reference PS/DDR
   configuration rather than this board's, so on a custom carrier it never
   finishes bringing up DDR and ``program_flash`` gives up with::

      Downloading FSBL...
      Running FSBL...
      ERROR: [Xicom 50-331] Timed out while waiting for FSBL to complete.
      Problem in Initializing Hardware

   Confirmed on the SLAC RFMC carrier, where the same run succeeds as soon as
   the FSBL from the Yocto build is used instead.  Tarballs produced before the
   FSBL was packaged do not contain one, so for those pass ``-e`` explicitly,
   pointing at
   ``<YoctoProject>/build/tmp/deploy/images/zynqmp-user/fsbl-zynqmp-user.elf``.

.. warning::

   ``program_flash`` requires ``-flash_density`` for the ``dual_parallel``
   and ``dual_stacked`` configurations.  Omitting it fails with
   ``ERROR: Flash Density not specified``.  The value is the **total** size in
   MB across the configuration, one of ``16 32 64 128 256 512 1024 2048``, as
   in the tool's own example: "Zynq MP (1GB QSPI Dual Parallel) ...
   ``-flash_density 1024``".  It also selects the mini u-boot flash writer.

Every board this platform supports wires QSPI as ``Dual Parallel`` with a
``x4`` data mode, which is the ``qspi-x8-dual_parallel`` flash type.  Only
the density differs.  Read it from the ``parallel-memories`` property of the
``&qspi`` node in the board's device tree, which lists the size of each of
the two parallel devices:

==================== ======================== ========== ==========
Board                ``parallel-memories``    per device ``-d``
==================== ======================== ========== ==========
ZCU208 / ZCU216      ``0x10000000`` each      256 MB     ``512``
ZCU111 / ZCU670      ``0x10000000`` each      256 MB     ``512``
SLAC RFMC carrier    ``0x10000000`` each      256 MB     ``512``
ZCU102               ``0x4000000`` each       64 MB      ``128``
==================== ======================== ========== ==========

``-d`` is the sum of the pair, so it is twice the per-device size.  The ``512``
default therefore covers every supported board except the ZCU102, which needs
``-d 128``.

.. note::

   The ``compatible`` comments in the Xilinx device trees are unreliable:
   ``zynqmp-zcu208-revA.dts`` and ``zynqmp-zcu216-revA.dts`` both label the
   MT25QU02G as ``1Gb`` while ``zynqmp-zcu670-revB.dts`` labels the same part
   ``2Gb``, and the ZCU102 and ZCU111 trees both say ``32MB``.  Trust
   ``parallel-memories``, not the comment.

Manual recipe
-------------

The following recipe targets a ``qspi-x8-dual_parallel`` configuration,
which is typical for Zynq UltraScale+ boards with dual-parallel QSPI
flash.

.. code-block:: bash

   # Go to the Yocto project output directory
   cd firmware/build/YoctoProjects/<your-target-dir>

   # Define default parameters for QSPI
   # -flash_density is the total size in MB across the pair and is required
   # for the dual_parallel and dual_stacked configurations
   default_parameter="\
   -flash_type qspi-x8-dual_parallel \
   -flash_density 512 \
   -blank_check -verify"

   # Program each boot image at its partition offset
   program_flash -f linux/BOOT.BIN  -offset 0x0000000 $default_parameter
   program_flash -f linux/boot.scr  -offset 0x3E80000 $default_parameter
   program_flash -f linux/image.ub  -offset 0x4000000 $default_parameter

.. note::

   The ``image.ub`` offset above is the QSPI one (``0x4000000``, from
   ``QSPI_FIT_IMAGE_OFFSET`` in ``shared/Yocto/zynqmp-user.conf``).  NAND
   uses ``0x4180000`` instead; see :doc:`nand_flash`.

Scripted recipe (``program_qspi_flash.sh``)
-------------------------------------------

The ``program_qspi_flash.sh`` script, provided in the
:repo:`scripts/program_qspi_flash.sh` of the platform repository, automates
the recipe above.  Instead of a Yocto build directory it takes the packaged
``.linux.tar.gz``, extracts it to a scratch directory, and programs each
image at its partition offset.

.. code-block:: bash

   firmware/submodules/axi-soc-ultra-plus-core/scripts/program_qspi_flash.sh \
       -f firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

Replace ``<full-name>`` with the timestamped artifact name produced by
the Yocto build (schema:
``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).

Options:

- ``-f PATH`` -- path to the ``.linux.tar.gz`` (required).
- ``-t FLASH_TYPE`` -- value passed to ``-flash_type``
  (default ``qspi-x8-dual_parallel``).  Must be a ``qspi-*`` type; the
  script rejects ``nand-*`` types and points at ``program_nand_flash.sh``,
  since the ``image.ub`` offset it programs is fixed at the QSPI one.
- ``-d DENSITY`` -- total flash density in MB passed to
  ``-flash_density`` (default ``512``).  Passed only for the
  ``dual_parallel`` / ``dual_stacked`` configurations that require it, or
  whenever ``-d`` is given explicitly.
- ``-e PATH`` -- FSBL ``.elf`` passed to ``-fsbl``.  Defaults to
  ``linux/zynqmp_fsbl.elf`` from the tarball, which ``BuildYoctoProject.sh``
  packages, so this is normally not needed.  Only when the tarball predates
  that packaging does the script fall back to the generic Vitis FSBL, which
  does not work on a custom carrier (see the FSBL warning under
  **Prerequisites**).
- ``-n`` -- skip the ``-blank_check -verify`` QA/QC pass, which is **on by
  default**.  ``-verify`` reads the payload back and compares it against the
  image, the only positive proof the dual-parallel addressing put the data
  where it belongs.  The cost depends strongly on the Vitis release, so see
  the throughput warning below before assuming it is cheap.
- ``-c`` -- accepted for compatibility and now redundant, since the checks it
  used to enable are the default.  Use ``-n`` to turn them off.
- ``-F`` -- program even if an ``hw_server`` is already running.  By default
  the script stops, because ``program_flash`` would attach to that session
  and share its JTAG cable.  Note that ``killVivado`` does not reap
  ``hw_server``.
- ``-J`` -- skip the JTAG boot mode step and flash the board as-is.
- ``-H`` -- show the help text.

Throughput
~~~~~~~~~~

.. warning::

   Throughput varies by roughly two orders of magnitude between Vitis
   releases, so treat every figure below as specific to the release it was
   taken on, and re-measure before planning around it.

   ================  ==========================  ====================
   Release           Write                       Read back
   ================  ==========================  ====================
   Vitis 2026.1      ~150 kB/s                   ~103 kB/s
   Vitis 2025.2      ~2.3 kB/s                   ~1.35 kB/s
   ================  ==========================  ====================

   Both measured over a Digilent JTAG-SMT3 at its default 15 MHz with
   ``qspi-x8-dual_parallel``.  On **2026.1**, a 1,949,892 byte ``BOOT.BIN``
   takes 76 s end to end: erase 1 s, ``blank_check`` 19 s, write 13 s,
   ``verify`` 19 s.  The checks cost about 38 s, which is why they are on by
   default.  On **2025.2** the same write is nearer 14 min with the checks
   adding roughly 48 min on top, so pass ``-n`` there.

   ``-a`` is expensive on any release, since ``image.ub`` is roughly 60x
   larger than ``BOOT.BIN``.  At 2026.1 rates a full ``-a`` run extrapolates
   to about 52 min, against roughly 14 min with ``-n``.  That extrapolation
   has not been measured.

Boot mode handling
~~~~~~~~~~~~~~~~~~

``program_flash`` has to halt the PS and download an FSBL over JTAG, so the
script puts the device into JTAG boot mode first, running the JTAG sequence
from **Change Boot Mode via XSCT** verbatim: write ``0x0100`` to the Boot Mode
register at ``0xff5e0200``, then ``rst -system``, which leaves the CPU halted
instead of booting from flash.  Pass ``-J`` to skip this if you manage boot
modes yourself.

The board is **left in JTAG boot mode** afterwards, via the ``0xff5e0200``
override.  A power-cycle clears that override and the ``M[3:0]`` straps take
effect again, so if the straps already select QSPI (``0b0010`` = QSPI32) a
plain power-cycle is all you need.  To switch boot mode *without* a
power-cycle, write ``0x2100`` (Quad-SPI 32-bit dual-parallel) to
``0xff5e0200`` and ``rst -system``.  The script prints this reminder when it
finishes.

.. note::

   Do not combine the two: writing ``0x2100`` and then power-cycling is
   self-defeating, because the POR clears the very override you just wrote.
   The register write is for changing boot mode while the board stays
   powered; the power-cycle is for falling back to the straps.

.. warning::

   ``program_flash`` reports the ``M[3:0]`` strap value rather than the
   ``0xff5e0200`` override, so you will see this warning whenever the straps
   are not JTAG::

      BOOT_MODE REG = 0x0222
      WARNING: [Xicom 50-100] The current boot mode is QSPI32.
      Flash programming is not supported with the selected boot mode.

   ``0x0222`` has bit 8 clear and ``[3:0] = 2``, which is the ``M[3:0]``
   strap (``0b0010`` = QSPI32), not the override.  It is **cosmetic**: the
   override did take effect, the tool proceeds, and the run goes on to
   ``Finished running FSBL.`` and ``Flash Operation Successful``.  Verified on
   the SLAC RFMC carrier from a true cold boot with the straps at ``0b0010``,
   including a passing ``-verify``.  JTAG straps (``0b0000``) are not required,
   though they do silence the warning.

   Do not read this warning as the cause of a failure that follows it.  An
   ``ERROR: [Xicom 50-331] Timed out while waiting for FSBL to complete``
   later in the same run is a separate problem, almost always the wrong FSBL
   (see the FSBL warning under **Prerequisites**).  The two appear together
   often enough to be easy to conflate.

.. warning::

   Every dual-parallel run also emits::

      WARNING: [Xicom 50-353] WARNING: Flash Mismatched. If flash programming
      fails, select the correct Flash and try again.
      Selected Flash: cfgmem-512-qspi-x8-dual_parallel
      Detected Flash: mt25qu02g

   This is cosmetic too.  ``program_flash`` string-compares the selected
   ``cfgmem`` name against the single-die ID the mini u-boot probed, and those
   can never match for a dual-parallel pair.  A passing ``-verify`` (on by
   default) is what confirms the addressing was right regardless.

.. note::

   The script runs this sequence with ``xsdb``, not ``xsct``.  Vitis 2026.1
   ships ``xsct`` but hard-disables it, printing
   ``XSCT is disabled in Vitis 2026.1 release``.  ``xsdb`` accepts the same
   commands.  The script falls back to ``xsct`` only on older installs that
   predate ``xsdb``.

.. tip::

   If the boot mode step fails with
   ``no targets found with "name =~ "*PSU*""`` and the available target is
   ``whole scan chain (DR shift through all zeroes)``, no device is answering
   on the JTAG chain: the board is powered off, held in reset, or miscabled.
   Check it directly with::

      xsdb -eval 'connect; after 4000; jtag targets'

   A healthy chain lists the device IDCODEs.  Catching this here is
   deliberate: if you skip the boot mode step with ``-J``, ``program_flash``
   segfaults on the same condition instead of reporting it.

After flashing
--------------

Switch the board to boot from the newly programmed flash using XSCT
(**Change Boot Mode via XSCT** in this how-to section), then
power-cycle the board.

.. note::

   The first boot from freshly programmed flash prints::

      Loading Environment from SPIFlash... SF: Detected mt25qu02g ...
      *** Warning - bad CRC, using default environment

   Expected and harmless.  U-Boot keeps its environment in a separate sector at
   ``0x1E00000`` (``CONFIG_ENV_OFFSET`` for ZynqMP with
   ``CONFIG_ENV_IS_IN_SPI_FLASH``, size ``0x40000``), which none of the images
   programmed here touch, so it reads blank and the CRC cannot match.  U-Boot
   falls back to the environment compiled into ``BOOT.BIN``, which is exactly
   what the ``tftp-only`` flow depends on: ``bootcmd`` comes from
   ``platform-top.h``, not from stored state.

.. warning::

   Do not silence that warning with ``saveenv``.  A saved environment takes
   precedence over the compiled-in default permanently, and reflashing
   ``BOOT.BIN`` does **not** update it, because the write never reaches
   ``0x1E00000``.  A later build with a changed ``bootcmd``, ``netboot``, or
   ``loadpl_net`` is then silently ignored, and that symptom (board still boots
   the old way after a successful reflash) is far more confusing than the
   warning it replaced.  To undo one, erase the sector at the U-Boot prompt::

      sf probe 0 0 0
      sf erase 0x1E00000 0x80000

   There is a second reason on boards flashed with a bitstream-carrying
   ``BOOT.BIN``: at roughly 34 MB it extends past ``0x1E00000``, so the boot
   image and the environment sector share flash and a ``saveenv`` would corrupt
   the boot image.  Only the ``tftp-only`` ``BOOT.BIN`` (under 2 MB) clears it.
