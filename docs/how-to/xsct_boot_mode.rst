Change Boot Mode via XSCT
==========================

Use the Xilinx System Debugger over JTAG to change the persistent boot
mode register of a Zynq UltraScale+ MPSoC without physically moving a
DIP switch.  This is useful for switching between SD, QSPI, NAND, and
JTAG boot modes on boards that expose JTAG headers.

The sequences below use ``xsdb``, the current debugger console.  They are
unchanged apart from the launch command if you are on an older release
that still has a working ``xsct``: every command in them is common to both.

See also:
https://www.zachpfeffer.com/single-post/change-the-boot-mode-of-the-xilinx-zynq-ultrascale-mpsoc-from-xsct

Prerequisites
-------------

- Vivado / Vitis installed, with ``xsdb`` (or ``xsct`` on older releases)
  on ``PATH``.
- JTAG cable connected between host and the board's JTAG header.
- Board powered on.

.. warning::

   Do not substitute ``xsct`` on Vitis 2026.1.  That release still ships the
   ``xsct`` launcher but hard-disables it: it prints
   ``XSCT is disabled in Vitis 2026.1 release`` and exits without connecting.

.. tip::

   Each sequence can also be run non-interactively by putting its commands
   (everything after the ``xsdb`` line) in a file and running
   ``xsdb <script.tcl>``.  ``xsdb`` returns a non-zero exit status if a
   command fails, so this form is safe to call from a shell script.

.. note::

   The QSPI and NAND flashing scripts run the **JTAG boot mode** sequence
   below automatically before programming, so you do not need to apply it by
   hand first.  See :doc:`qspi_flash` and :doc:`nand_flash`.

.. note::

   Each sequence below writes to the Boot Mode register at ``0xff5e0200``
   and issues a system reset (``rst -system``).  The boot mode takes effect
   on the **next** power-cycle or reset.

   The flash and SD sequences then reconnect (``con``) to leave the
   processor running.  The JTAG sequence deliberately omits ``con``: in JTAG
   boot mode there is nothing to boot, and leaving the CPU halted is what
   lets ``program_flash`` download its FSBL.

.. warning::

   ``rst -system`` arms a **reset catch that survives ``disconnect``**.  After
   any of these sequences the debugger halts the CPU at the reset vector on the
   *next* reset, including a ``reset`` typed at the U-Boot prompt.  The board
   then looks dead: no console output whatsoever, and ``targets`` shows::

      9  Cortex-A53 #0 (Reset Catch, EL3(S)/A64)

   A power-cycle clears it, which is why the flashing scripts tell you to
   power-cycle rather than warm-reset when they finish.  To release it without
   a power-cycle, reconnect and continue::

      xsdb -eval 'connect; after 3000; \
        targets -set -nocase -filter {name =~ "*Cortex-A53 #0*"}; con'

.. note::

   The override survives the reset that consumes it: reading ``0xff5e0200``
   after ``rst -system`` still shows ``0x00000100``, and all four A53 cores
   report ``APU Reset`` rather than booting, which is how you confirm JTAG boot
   mode actually took effect::

      xsdb -eval 'connect; after 3000; \
        targets -set -nocase -filter {name =~ "*PSU*"}; mrd 0xff5e0200; targets'

   Note that ``program_flash`` reports the ``M[3:0]`` straps instead, so its
   ``BOOT_MODE REG`` line and its ``[Xicom 50-100]`` warning are not a reliable
   indication either way.  See :doc:`qspi_flash`.

JTAG boot mode
--------------

.. code-block:: text

   xsdb
   connect
   targets -set -nocase -filter {name =~ "*PSU*"}
   stop
   mwr  0xff5e0200 0x0100
   rst -system
   disconnect

Quad-SPI (32-bit, dual-parallel) boot mode
-------------------------------------------

.. code-block:: text

   xsdb
   connect
   targets -set -nocase -filter {name =~ "*PSU*"}
   stop
   mwr  0xff5e0200 0x2100
   rst -system
   con
   disconnect

SD0 (SD 2.0) boot mode
-----------------------

.. code-block:: text

   xsdb
   connect
   targets -set -nocase -filter {name =~ "*PSU*"}
   stop
   mwr  0xff5e0200 0x3100
   rst -system
   con
   disconnect

NAND boot mode
--------------

.. code-block:: text

   xsdb
   connect
   targets -set -nocase -filter {name =~ "*PSU*"}
   stop
   mwr  0xff5e0200 0x4100
   rst -system
   con
   disconnect

Verification
------------

After the reset completes, the board will attempt to boot from the
newly configured source.  Monitor the serial console (115200 baud,
``/dev/ttyUSB1`` or similar) to confirm the bootloader selects the
correct boot device.

Register reference:
https://www.xilinx.com/html_docs/registers/ug1087/ug1087-zynq-ultrascale-registers.html
