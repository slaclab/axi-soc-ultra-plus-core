Remote Bitstream Update
=======================

Copy a new ``.bit`` file to the RFSoC board over the network and reboot
to load it, without physically accessing the SD card.

Prerequisites
-------------

- The board has been imaged and booted at least once (see
  :doc:`sd_card_imaging`).
- SSH access to the board as ``root`` at ``<board-ip>`` (the
  DHCP-assigned or static address of your board).
- A freshly built ``.bit`` file in the Vivado images directory, e.g.:
  ``firmware/targets/<your-target-dir>/images/<full-name>.bit``

Steps
-----

1. Copy the bitstream to the SD card boot partition on the board:

   .. code-block:: bash

      scp firmware/targets/<your-target-dir>/images/<full-name>.bit \
          root@<board-ip>:/boot/system.bit

   Replace ``<your-target-dir>`` and ``<full-name>`` with your actual
   target directory name and the timestamped filename produced by the
   build (schema:
   ``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``).

2. Flush writes and reboot the board:

   .. code-block:: bash

      ssh root@<board-ip> '/bin/sync; /sbin/reboot'

   The ``sync`` command is critical: writing to an SD card is slow and
   rebooting before the write completes may corrupt the file.  ``sync``
   blocks until all writes are flushed to the card.

Verification
------------

After the board comes back up (typically 30 to 60 s), confirm it is
reachable:

.. code-block:: bash

   ping -c 4 <board-ip>

Then connect with the Rogue GUI (see :doc:`rogue_gui_launch`) and
verify the firmware version register matches the new build.

Notes
-----

- The SD card is mounted read-write at ``/boot/`` on the running
  system.  Any file in ``/boot/`` can be updated remotely using the
  same ``scp`` + ``sync`` + ``reboot`` pattern.
- The runtime filesystem is in memory (tmpfs/initramfs), so the
  system continues operating normally while ``/boot/`` is being
  written.
- This procedure updates the bitstream on the SD card, which the running
  Linux loads with ``fpgautil`` at startup. For a board with **no SD
  card** (diskless QSPI boot), the PL can instead be fetched over the
  network at boot time; see :doc:`tftp_network_boot`
  (build a ``tftp-only`` image and stage the bitstream with
  ``provision_tftp_host.sh -B``).
