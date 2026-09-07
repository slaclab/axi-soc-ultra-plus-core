SD Card Imaging
===============

Write Yocto boot images to an SD card so the board can boot from it.
Three approaches are covered:

**Build a disk image, then write it to the card** (``CreateDiskImage.sh``,
preferred).  Builds a complete image file: partition table, filesystems
and boot files all in one step.  A single ``dd`` then copies it to the
card.  Reach for this when you want a reusable ``.img`` artifact to
archive, or to write the same build to several cards.  The application
partition is only as large as the image, so it needs a grow step
afterwards to fill the card.

**Format the card directly** (``FormatSdCard.sh``).  Partitions, formats
and loads the card in place in a single pass, with the application
partition sized to the whole card from the start.  No ``dd`` and no grow
step.  It refuses to touch anything that is not removable media.

**Partition by hand** (``fdisk`` and ``mkfs``).  What both scripts do
under the hood, kept as a reference.  Use it to repair a card whose data
you want to keep, or on a card reader that the interlocks in
``FormatSdCard.sh`` refuse.

Prerequisites
-------------

- A completed Yocto build.  Boot images are at:
  ``firmware/build/YoctoProjects/<your-target-dir>/linux/``
  and typically include ``system.bit``, ``BOOT.BIN``, ``image.ub``, and
  ``boot.scr``.
- The packaged Yocto tarball at
  ``firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz``.
  Replace ``<full-name>`` with the timestamped artifact name produced by
  the Yocto build (schema:
  ``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).
- ``sudo`` rights on the host doing the imaging.
- Standard util-linux and filesystem tooling on ``PATH``: ``lsblk``,
  ``mountpoint``, ``mkfs.vfat`` (dosfstools) and ``mkfs.ext4``
  (e2fsprogs) for all recipes, plus ``losetup`` for
  ``CreateDiskImage.sh`` and ``sfdisk``, ``wipefs``, ``partprobe`` and
  ``udevadm`` for ``FormatSdCard.sh``.  These are present on a normal
  desktop install but can be missing in a minimal container.

Preferred: build a disk image with ``CreateDiskImage.sh``
---------------------------------------------------------

Step 1: build the image file
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Run :repo:`scripts/CreateDiskImage.sh` with the output image path and the
packaged Yocto tarball:

.. code-block:: bash

   ./firmware/submodules/axi-soc-ultra-plus-core/scripts/CreateDiskImage.sh \
       path_to_image_file.img \
       firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

.. important::

   Run the script as a normal user.  Do **not** put ``sudo`` in front of
   it, and do **not** ``source`` it.

   The script already runs the individual steps that need root
   (``losetup``, ``mkfs``, ``mount``, ``umount``) under ``sudo``, and it
   asks for your password up front.  Running the whole script under
   ``sudo`` instead leaves the finished ``.img`` owned by root.
   ``source``-ing it runs it in your current shell, where its ``exit``
   on a failed check closes that shell instead of just the script.

The default is a 2048 MiB image holding a 1 GiB FAT32 boot partition with
the bootable flag set, plus an ext4 application partition in the
remaining space.  The boot partition is identical to the one the other
two recipes produce; the application partition is not, because it is
bounded by the image size rather than the card, which is what step 4
below exists to correct.  ``-s`` sets the total image size in MiB and
``-b`` the boot partition size; ``-H`` prints the full usage.  Setting
``-b`` equal to ``-s`` produces a boot-only image with no application
partition.

Keep the image small.  Every megabyte of it, empty or not, has to be
copied to the card in step 3.  Growing the application partition on the
card afterwards (step 4) is much faster than writing a card-sized image.

Step 2: find the card's device node
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   # Find the SD memory card device path
   ls /dev/sd*

   # Cross-check the size and model before going any further
   lsblk -o NAME,SIZE,MODEL,TRAN

.. warning::

   Confirm the device node against the card's size and model in
   ``lsblk`` output.  The ``dd`` in the next step overwrites the target
   from sector zero with no confirmation prompt, so naming a system disk
   here destroys it.  The examples below use ``/dev/sdd``; yours will
   differ.

Step 3: write the image to the card
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   # Unmount anything the desktop auto-mounted ("not mounted" errors are fine)
   sudo umount /dev/sdd?

   # Write the image, then flush
   sudo dd if=path_to_image_file.img of=/dev/sdd bs=1M status=progress conv=fsync
   sudo sync

.. note::

   Target the whole disk (``/dev/sdd``), not a partition
   (``/dev/sdd1``).  The image contains its own partition table, so
   writing it to a partition produces a card that cannot boot.

Step 4: grow the application partition (optional)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The image is smaller than the card, so after step 3 the space past the
end of the image is unallocated.  To reclaim it, delete partition 2 and
recreate it starting at the same sector, then grow the filesystem into
it.  With the default layout, partition 2 starts at sector ``2099200``.

.. code-block:: text

   $ sudo fdisk /dev/sdd

   Command (m for help): p
   # Note partition 2's Start sector before deleting it

   Command (m for help): d
   Partition number (1,2, default 2): 2

   Command (m for help): n
   Select (default p): p
   Partition number (2-4, default 2): 2
   First sector (...): 2099200
   Last sector, +/-sectors or +/-size{K,M,G,T,P} (...): <accept the default>

   # fdisk notices the existing filesystem and offers to wipe its signature.
   # Answer N: the goal is to keep that filesystem and extend it.
   Partition #2 contains a ext4 signature.
   Do you want to remove the signature? [Y]es/[N]o: N

   Command (m for help): w

.. code-block:: bash

   sudo e2fsck -f /dev/sdd2
   sudo resize2fs /dev/sdd2

.. warning::

   The recreated partition 2 must start at exactly the same sector as
   before.  Starting it anywhere else, or answering ``Y`` to the
   signature prompt, loses the filesystem.

Format the card directly with ``FormatSdCard.sh``
--------------------------------------------------

:repo:`scripts/FormatSdCard.sh` does the whole job in one pass:
partitions the card, formats both partitions, and copies the boot images
onto the boot partition.  Unlike the image recipe above it sizes the
application partition to the whole card, so there is no ``dd`` and no
grow step.

.. code-block:: bash

   ./firmware/submodules/axi-soc-ultra-plus-core/scripts/FormatSdCard.sh \
       /dev/sdd \
       firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

Pass the **whole disk** (``/dev/sdd``), not a partition (``/dev/sdd1``).
``-b`` sets the boot partition size in MiB (default 1024); the rest of
the card becomes the ext4 application partition.  ``-H`` prints the full
usage.  Run it as a normal user, exactly as with ``CreateDiskImage.sh``:
it invokes ``sudo`` itself for the steps that need root.

.. warning::

   This erases the target device.  The script prints the device size and
   model and then requires you to type the device name to proceed, so
   check that what it prints is the card you meant and not another disk.

Before doing anything the script requires positive evidence that the
target is removable media.  It refuses to continue unless the device is
a whole disk **and** either a removable disk on the USB bus or an
``/dev/mmcblk*`` SD/MMC device.  A fixed internal or external disk is
rejected on that basis, which is what stops a one-letter typo from
reformatting a system or data drive.  These checks cannot be bypassed;
``-y`` skips only the interactive confirmation, for scripted use.

If your card is on a reader that these checks reject, use the by-hand
reference below rather than looking for a way around them.

Reference: partition and format by hand
----------------------------------------

The steps below were originally published in the Xilinx wiki article
"Prepare boot medium" (https://xilinx-wiki.atlassian.net/wiki/x/EYMfAQ).
They are reproduced here in full, against a real card, so that following
them does not require translating the wiki's generic instructions to
your device node.  This is what the two scripts above automate, and the
resulting layout is the same.

Step 1: find the card's device node
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   # Find the SD memory card device path
   ls /dev/sd*

The example session below uses ``/dev/sdd``.  Substitute your own device
node throughout, and cross-check it against the card's size and model in
``lsblk -o NAME,SIZE,MODEL,TRAN`` before running ``fdisk`` against it.
Nothing here has the device interlocks that ``FormatSdCard.sh`` applies,
so naming the wrong disk by hand repartitions that disk.

Step 2: delete the old partitions and create two new ones
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``fdisk`` key sequence, in order: ``d`` to delete each existing
partition, ``n`` then ``+1G`` for the 1 GiB boot partition, ``a`` to mark
it bootable, ``n`` again accepting every default for the application
partition, and ``w`` to write the table.  ``p`` prints the table at any
point and changes nothing.

Nothing is written to the card until ``w``.  Quit with ``q`` instead if
the printed table does not look the way you expect.

.. code-block:: text

   $ sudo fdisk /dev/sdd

   Welcome to fdisk (util-linux 2.37.2).
   Changes will remain in memory only, until you decide to write them.
   Be careful before using the write command.


   Command (m for help): p
   Disk /dev/sdd: 29.72 GiB, 31914983424 bytes, 62333952 sectors
   Disk model: MassStorageClass
   Units: sectors of 1 * 512 = 512 bytes
   Sector size (logical/physical): 512 bytes / 512 bytes
   I/O size (minimum/optimal): 512 bytes / 512 bytes
   Disklabel type: dos
   Disk identifier: 0x00000000

   Device     Boot Start      End  Sectors  Size Id Type
   /dev/sdd1        8192 62333951 62325760 29.7G  c W95 FAT32 (LBA)

   Command (m for help): d
   Selected partition 1
   Partition 1 has been deleted.

   Command (m for help): n
   Partition type
      p   primary (0 primary, 0 extended, 4 free)
      e   extended (container for logical partitions)
   Select (default p): p
   Partition number (1-4, default 1):
   First sector (2048-62333951, default 2048):
   Last sector, +/-sectors or +/-size{K,M,G,T,P} (2048-62333951, default 62333951): +1G

   Created a new partition 1 of type 'Linux' and of size 1 GiB.

   Command (m for help): a
   Selected partition 1
   The bootable flag on partition 1 is enabled now.

   Command (m for help): n
   Partition type
      p   primary (1 primary, 0 extended, 3 free)
      e   extended (container for logical partitions)
   Select (default p):

   Using default response p.
   Partition number (2-4, default 2):
   First sector (2099200-62333951, default 2099200):
   Last sector, +/-sectors or +/-size{K,M,G,T,P} (2099200-62333951, default 62333951):

   Created a new partition 2 of type 'Linux' and of size 28.7 GiB.

   Command (m for help): p
   Disk /dev/sdd: 29.72 GiB, 31914983424 bytes, 62333952 sectors
   Disk model: MassStorageClass
   Units: sectors of 1 * 512 = 512 bytes
   Sector size (logical/physical): 512 bytes / 512 bytes
   I/O size (minimum/optimal): 512 bytes / 512 bytes
   Disklabel type: dos
   Disk identifier: 0x00000000

   Device     Boot   Start      End  Sectors  Size Id Type
   /dev/sdd1  *       2048  2099199  2097152    1G 83 Linux
   /dev/sdd2       2099200 62333951 60234752 28.7G 83 Linux

   Command (m for help): w
   The partition table has been altered.
   Calling ioctl() to re-read partition table.
   Syncing disks.

If the card had more than one partition to begin with, repeat ``d`` until
``p`` shows an empty table before creating the new partitions.

.. note::

   This leaves both partitions with type Id ``83`` (Linux) even though
   partition 1 gets a FAT32 filesystem in the next step, and it still
   boots: the Zynq UltraScale+ BootROM reads the FAT filesystem itself
   and does not consult the MBR partition type byte.  There is no need
   to change it.  ``FormatSdCard.sh`` tags the boot partition ``c``
   (W95 FAT32 LBA) instead, which is the more conventional label; either
   type byte works, so do not treat a mismatch between a card made by
   hand and one made by the script as a fault.

Step 3: format the two partitions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   # Format the boot partition (FAT32, label 'boot')
   sudo mkfs.vfat -F 32 -n boot /dev/sdd1

   # Format the application partition (ext4, label 'root')
   sudo mkfs.ext4 -L root /dev/sdd2

``mkfs.vfat`` warns that "lowercase labels might not work properly on
some systems".  The lowercase ``boot`` label is what the boot flow
expects, so the warning is harmless here.

Step 4: copy the boot images to the boot partition
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: bash

   sudo mkdir -p boot
   sudo mount /dev/sdd1 boot
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/system.bit boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/BOOT.BIN   boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/image.ub   boot/.
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/boot.scr   boot/.
   sudo sync boot/
   sudo umount boot

.. warning::

   Wait for ``umount`` to return before pulling the card.  ``umount``
   is what flushes buffered writes and issues the device cache flush,
   and on slow media it is not instantaneous.  Removing the card while
   it is still running is what corrupts the boot partition.

   The explicit ``sudo sync boot/`` is belt-and-braces rather than the
   thing that makes the write safe: it surfaces a late write error, such
   as a partition that turned out to be too small, as its own failure
   instead of one emerging from ``umount``.  Both scripts above do the
   same for the same reason.

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
