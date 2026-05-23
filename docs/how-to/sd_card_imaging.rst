SD Card Imaging
===============

Write Yocto boot images to an SD card so the board can boot from it.
Two approaches are covered: a manual ``mount``/``cp``/``sync``/``umount``
sequence and an automated script provided by the platform.

Prerequisites
-------------

- A completed Yocto build.  Boot images are at:
  ``firmware/build/YoctoProjects/<your-target-dir>/linux/``
  and typically include ``system.bit``, ``BOOT.BIN``, ``image.ub``, and
  ``boot.scr``.
- An SD card with a FAT32 boot partition (see the Xilinx partition guide:
  https://xilinx-wiki.atlassian.net/wiki/x/EYMfAQ).
- For the scripted method: the packaged Yocto tarball at
  ``firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz``.

Manual recipe
-------------

The example below assumes the SD card FAT32 partition is ``/dev/sde1``.
Adjust the device node to match your system.

.. code-block:: bash

   sudo mkdir -p boot
   sudo mount /dev/sde1 boot
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/system.bit boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/BOOT.BIN   boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/image.ub   boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/boot.scr   boot/.
   sudo sync boot/
   sudo umount boot

.. warning::

   Always run ``sudo sync boot/`` before ``umount``.  SD card writes
   are buffered; unmounting without syncing can produce a corrupt
   boot partition.

Scripted recipe (``CreateDiskImage.sh``)
-----------------------------------------

The ``CreateDiskImage.sh`` script, provided in the
:repo:`scripts/CreateDiskImage.sh` of the platform repository, automates
partition creation and image writing.  It takes a target image file and
the packaged Yocto tarball as arguments.

.. code-block:: bash

   source firmware/submodules/axi-soc-ultra-plus-core/scripts/CreateDiskImage.sh \
       path_to_image_file.img \
       firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

Replace ``<full-name>`` with the timestamped artifact name produced by
the Yocto build (schema:
``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).

After imaging
-------------

1. Power down the board.
2. Confirm the mode slide switch is in the **SD** (not **JTAG**) position.
3. Insert the SD card and power on the board.
4. Verify the board boots by pinging it:

   .. code-block:: bash

      ping -c 4 10.0.0.10

5. Connect a serial console (115200 baud, e.g. ``/dev/ttyUSB1``) if the
   network does not come up; the serial console shows the bootloader and
   kernel messages.

   .. code-block:: bash

      cu --line /dev/ttyUSB1 --speed 115200 --parity=none
