Program Flash Memory
====================

Use Xilinx ``program_flash`` (part of Vivado / Vitis) to write the
Yocto boot images directly to on-board QSPI or NAND flash.  Both flash
types use the same parametric ``program_flash`` workflow; only the
``-flash_type`` argument and one image offset differ.

Prerequisites
-------------

- Vivado / Vitis installed with ``program_flash`` on ``PATH``.
- JTAG cable connected and board powered on.
- A completed Yocto build.  Boot images are located at:
  ``firmware/build/YoctoProjects/<your-target-dir>/linux/``

  .. note::

     Set the correct boot mode before or after flashing using XSCT
     (see **Change Boot Mode via XSCT** in this how-to section).

QSPI
----

The following recipe targets a ``qspi-x8-dual_parallel`` configuration,
which is typical for Zynq UltraScale+ boards with dual-parallel QSPI
flash.

.. code-block:: bash

   # Go to the Yocto project output directory
   cd firmware/build/YoctoProjects/<your-target-dir>

   # Define default parameters for QSPI
   default_parameter="\
   -flash_type qspi-x8-dual_parallel \
   -blank_check -verify \
   -fsbl linux/zynqmp_fsbl.elf"

   # Program each boot image at its partition offset
   program_flash -f linux/BOOT.BIN  -offset 0x0000000 $default_parameter
   program_flash -f linux/boot.scr  -offset 0x3E80000 $default_parameter
   program_flash -f linux/image.ub  -offset 0x4000000 $default_parameter

NAND
----

The NAND recipe is identical except for the ``-flash_type`` value and
the ``image.ub`` offset.

.. code-block:: bash

   # Go to the Yocto project output directory
   cd firmware/build/YoctoProjects/<your-target-dir>

   # Define default parameters for NAND
   default_parameter="\
   -flash_type nand-x8-single \
   -blank_check -verify \
   -fsbl linux/zynqmp_fsbl.elf"

   # Program each boot image at its partition offset
   program_flash -f linux/BOOT.BIN  -offset 0x0000000 $default_parameter
   program_flash -f linux/boot.scr  -offset 0x3E80000 $default_parameter
   program_flash -f linux/image.ub  -offset 0x4180000 $default_parameter

.. note::

   The ``image.ub`` offset differs between QSPI (``0x4000000``) and
   NAND (``0x4180000``).

After flashing
--------------

Switch the board to boot from the newly programmed flash using XSCT
(**Change Boot Mode via XSCT** in this how-to section), then
power-cycle the board.
