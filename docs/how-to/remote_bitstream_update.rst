Remote Bitstream Update
=======================

Copy a new ``.bit`` file to the RFSoC board over the network and reboot
to load it, without physically accessing the SD card.

Prerequisites
-------------

- The board has been imaged and booted at least once (see
  **SD Card Imaging** in this how-to section).
- SSH access to the board as ``root``.  Default IP is ``10.0.0.10``
  (DHCP-assigned; adjust if your network differs).
- A freshly built ``.bit`` file in the Vivado images directory, e.g.:
  ``firmware/targets/<your-target-dir>/images/<full-name>.bit``

Steps
-----

1. Copy the bitstream to the SD card boot partition on the board:

   .. code-block:: bash

      scp firmware/targets/<your-target-dir>/images/<full-name>.bit \
          root@10.0.0.10:/boot/system.bit

   Replace ``<your-target-dir>`` and ``<full-name>`` with your actual
   target directory name and the timestamped filename produced by the
   build (schema:
   ``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).

2. Flush writes and reboot the board:

   .. code-block:: bash

      ssh root@10.0.0.10 '/bin/sync; /sbin/reboot'

   The ``sync`` command is critical: writing to an SD card is slow and
   rebooting before the write completes may corrupt the file.  ``sync``
   blocks until all writes are flushed to the card.

Verification
------------

After the board comes back up (typically 30–60 s), confirm it is
reachable:

.. code-block:: bash

   ping -c 4 10.0.0.10

Then connect with the Rogue GUI (see **Launch the Rogue GUI** in this
how-to section) and
verify the firmware version register matches the new build.

Notes
-----

- The SD card is mounted read-write at ``/boot/`` on the running
  system.  Any file in ``/boot/`` can be updated remotely using the
  same ``scp`` + ``sync`` + ``reboot`` pattern.
- The runtime filesystem is in memory (tmpfs/initramfs), so the
  system continues operating normally while ``/boot/`` is being
  written.
