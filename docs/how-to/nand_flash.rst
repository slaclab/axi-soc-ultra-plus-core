Program NAND Flash Memory
=========================

Use Xilinx ``program_flash`` (part of Vivado / Vitis) to write the Yocto
boot images directly to on-board NAND flash.  The workflow is identical to
:doc:`qspi_flash`; only the ``-flash_type`` argument and the ``image.ub``
offset differ.

Prerequisites
-------------

- Vivado / Vitis installed with ``program_flash`` on ``PATH``.
- JTAG cable connected and board powered on.
- A board that actually has NAND flash.  Check for a NAND controller node
  in the design's device tree before flashing; the SLAC RFMC carrier, for
  example, exposes only QSPI and SD.
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
   and the tarball carry one, and the scripted recipe picks it up with no
   ``-e``.  Pass it to the manual recipe as ``-fsbl linux/zynqmp_fsbl.elf``.

.. warning::

   NAND has no fallback when that FSBL is absent.  Unlike QSPI, Vitis bundles
   no NAND FSBL at all: its
   ``data/xicom/cfgmem/cfgmem_util/cfgmem_fsbl.zip`` ships only
   ``zynqmp_qspi_*_fsbl.elf`` variants, and those are unusable here both for
   the wrong flash type and because they carry a reference PS/DDR
   configuration that hangs on a custom carrier.  On a tarball produced before
   the FSBL was packaged, ``program_flash`` therefore refuses outright, and you
   must pass ``-e`` pointing at
   ``<YoctoProject>/build/tmp/deploy/images/zynqmp-user/fsbl-zynqmp-user.elf``.

.. note::

   ``-flash_density`` is not needed here.  ``program_flash`` lists it as
   required only for the QSPI / OSPI dual configurations, not for NAND, so
   the recipes below omit it and ``program_nand_flash.sh`` has no ``-d``
   option.

Manual recipe
-------------

.. code-block:: bash

   # Go to the Yocto project output directory
   cd firmware/build/YoctoProjects/<your-target-dir>

   # Define default parameters for NAND
   default_parameter="\
   -flash_type nand-x8-single \
   -blank_check -verify"

   # Program each boot image at its partition offset
   program_flash -f linux/BOOT.BIN  -offset 0x0000000 $default_parameter
   program_flash -f linux/boot.scr  -offset 0x3E80000 $default_parameter
   program_flash -f linux/image.ub  -offset 0x4180000 $default_parameter

.. note::

   The ``image.ub`` offset differs between NAND (``0x4180000``, from
   ``NAND_FIT_IMAGE_OFFSET``) and QSPI (``0x4000000``).  ``BOOT.BIN`` and
   ``boot.scr`` sit at the same offsets on both; U-Boot reads ``boot.scr``
   from ``CONFIG_BOOT_SCRIPT_OFFSET`` regardless of flash type.

.. warning::

   ``shared/Yocto/zynqmp-user.conf`` overrides only the ``QSPI_*`` FIT image
   variables, so NAND inherits the meta-xilinx default
   ``NAND_FIT_IMAGE_SIZE = 0x6400000`` (100 MiB) while ``QSPI_FIT_IMAGE_SIZE``
   is raised to 192 MiB.  An ``image.ub`` larger than 100 MiB is written to
   flash correctly but truncated by the ``nand read`` that the generated
   ``boot.scr`` issues, and ``bootm`` then fails on the incomplete FIT.
   Before relying on NAND boot, check the offsets and sizes in the generated
   ``boot.scr`` against your ``image.ub`` size and add ``NAND_FIT_IMAGE_SIZE``
   / ``NAND_FIT_IMAGE_OFFSET`` overrides for the machine if needed.

Scripted recipe (``program_nand_flash.sh``)
-------------------------------------------

The ``program_nand_flash.sh`` script, provided in the
:repo:`scripts/program_nand_flash.sh` of the platform repository, automates
the recipe above.  Instead of a Yocto build directory it takes the packaged
``.linux.tar.gz``, extracts it to a scratch directory, and programs each
image at its partition offset.

.. code-block:: bash

   firmware/submodules/axi-soc-ultra-plus-core/scripts/program_nand_flash.sh \
       -f firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

Replace ``<full-name>`` with the timestamped artifact name produced by
the Yocto build (schema:
``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).

Options:

- ``-f PATH`` -- path to the ``.linux.tar.gz`` (required).
- ``-t FLASH_TYPE`` -- value passed to ``-flash_type``
  (default ``nand-x8-single``).  Must be a ``nand-*`` type; the script
  rejects ``qspi-*`` types and points at ``program_qspi_flash.sh``, since
  the ``image.ub`` offset it programs is fixed at the NAND one.
- ``-e PATH`` -- FSBL ``.elf`` passed to ``-fsbl``.  Defaults to
  ``linux/zynqmp_fsbl.elf`` from the tarball, which ``BuildYoctoProject.sh``
  packages, so this is normally not needed.  Required only for tarballs that
  predate that packaging, because NAND has no bundled fallback (see the FSBL
  warning under **Prerequisites**).
- ``-n`` -- skip the ``-blank_check -verify`` QA/QC pass, which is **on by
  default**.  ``-verify`` reads the payload back and compares it against the
  image, the only positive proof the data landed where it belongs.  The cost
  has not been measured on NAND, and on QSPI it varied by roughly two orders
  of magnitude between Vitis releases (see the throughput warning in
  :doc:`qspi_flash`), so measure before planning around it.  Whatever the NAND
  rates turn out to be, ``-a`` costs far more than the default image set either
  way, since ``image.ub`` is roughly 60x larger than ``BOOT.BIN``.
- ``-c`` -- accepted for compatibility and now redundant, since the checks it
  used to enable are the default.  Use ``-n`` to turn them off.
- ``-F`` -- program even if an ``hw_server`` is already running.  By default
  the script stops, because ``program_flash`` would attach to that session
  and share its JTAG cable.  Note that ``killVivado`` does not reap
  ``hw_server``.
- ``-J`` -- skip the JTAG boot mode step and flash the board as-is.
- ``-H`` -- show the help text.

Boot mode handling
~~~~~~~~~~~~~~~~~~

Identical to :doc:`qspi_flash`, except for the value you write afterwards.
The script puts the device into JTAG boot mode first (``0x0100`` to
``0xff5e0200``, then ``rst -system``), using ``xsdb`` rather than the disabled
``xsct``, and leaves the board in JTAG boot mode.  Afterwards, either
power-cycle to fall back to the ``M[3:0]`` straps (``0b0100`` = NAND), or
write ``0x4100`` to ``0xff5e0200`` and ``rst -system`` to switch without a
power-cycle.  Do not do both: a POR clears the override you just wrote.  The
script prints this reminder when it finishes.  Pass ``-J`` to skip the step.

The two cosmetic ``program_flash`` warnings documented in :doc:`qspi_flash`
apply here as well: ``[Xicom 50-100] The current boot mode is ...`` reports the
``M[3:0]`` straps rather than the ``0xff5e0200`` override and does not mean the
override failed, and it must not be read as the cause of a later
``[Xicom 50-331] Timed out while waiting for FSBL to complete``, which is
almost always the wrong FSBL.

After flashing
--------------

Switch the board to boot from the newly programmed flash using XSCT
(**Change Boot Mode via XSCT** in this how-to section), then
power-cycle the board.
