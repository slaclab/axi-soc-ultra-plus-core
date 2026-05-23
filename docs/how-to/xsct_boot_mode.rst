Change Boot Mode via XSCT
==========================

Use the Xilinx System Debugger (XSCT) over JTAG to change the
persistent boot mode register of a Zynq UltraScale+ MPSoC without
physically moving a DIP switch.  This is useful for switching between
SD, QSPI, NAND, and JTAG boot modes on boards that expose JTAG headers.

See also:
https://www.zachpfeffer.com/single-post/change-the-boot-mode-of-the-xilinx-zynq-ultrascale-mpsoc-from-xsct

Prerequisites
-------------

- Vivado / Vitis XSCT installed and on ``PATH`` (``xsct`` command
  available).
- JTAG cable connected between host and the board's JTAG header.
- Board powered on.

.. note::

   Each sequence below writes to the Boot Mode register at
   ``0xff5e0200``, issues a system reset (``rst -system``), then
   reconnects (``con``) to leave the processor running.  The boot mode
   takes effect on the **next** power-cycle or reset.

JTAG boot mode
--------------

.. code-block:: text

   xsct
   connect
   targets -set -nocase -filter {name =~ "*PSU*"}
   stop
   mwr  0xff5e0200 0x0100
   rst -system
   disconnect

Quad-SPI (32-bit, dual-parallel) boot mode
-------------------------------------------

.. code-block:: text

   xsct
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

   xsct
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

   xsct
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
