First SoC Bring-up
==================

This tutorial walks through building and booting a SLAC RFSoC SoC platform design from
source. ``Simple-rfsoc-4x2-Example`` (board: RealDigital RFSoC 4x2, FPGA part
``xczu48dr-ffvg1517-2-e``) is used as the verified concrete example throughout. Paths
under ``firmware/targets/`` are parameterized with the ``<your-target-dir>`` placeholder;
see the :doc:`../reference/supported_boards` page for the target directory name for your board.

Output filenames embed the build timestamp, user name, and git hash, following the
schema ``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``. The
``<full-name>`` placeholder is used below wherever the exact filename is build-specific.

.. admonition:: Reference toolchain

   This walkthrough was authored against:

   - **Vivado:** ``v2025.2`` (64-bit)
   - **OS:** Ubuntu 22.04 LTS (x86_64)
   - **Firmware version:** ``v3.2.0.0`` (``PRJ_VERSION = 0x03020000``)
   - **Target FPGA part:** ``xczu48dr-ffvg1517-2-e``

   Approximate end-to-end build time on a typical Linux build host with the firmware
   tree on local-disk storage is **~60 min**: firmware (~17 min) plus Yocto (~45 min).
   See *Build-output redirection* below for why local-disk storage matters.

Clone
-----

Install `git-lfs <https://git-lfs.com>`_ in your shell profile (one-time step per
environment) before cloning, so that any large binary files tracked with LFS are fetched
correctly:

.. code-block:: bash

   git lfs install

Clone the repository with all submodules:

.. code-block:: bash

   # Replace <board> with the board-specific repo name, e.g. rfsoc-4x2
   git clone --recursive https://github.com/slaclab/Simple-<board>-Example.git

Verified example (RFSoC 4x2):

.. code-block:: bash

   git clone --recursive https://github.com/slaclab/Simple-rfsoc-4x2-Example.git

The ``--recursive`` flag initializes all git submodules (``surf``, ``axi-soc-ultra-plus-core``,
``ruckus``, ``aes-stream-drivers``) in a single step. Omitting it leaves the firmware
build unable to find required RTL and TCL sources.

Setup environment
-----------------

**Vivado toolchain (firmware build)**

Source the Vivado environment script. On SLAC S3DF hosts it sources ``settings64.sh`` from
the Vivado install under ``/sdf/group/faders/tools/xilinx/``:

.. code-block:: bash

   source firmware/vivado_setup.sh

This sets ``PATH``, ``XILINX_VIVADO``, and the ``LM_LICENSE_FILE`` license server variable
required by ``make``. On a non-SLAC host, source the equivalent ``settings64.sh`` from your local
Vivado installation instead.

**Rogue / PyRogue environment (software)**

Activate the ``rogue_v6.15.0`` conda environment:

.. code-block:: bash

   source software/setup_env_slac.sh

On a non-SLAC host, install Rogue first; see :doc:`../how-to/rogue_install`.

**Yocto host packages**

The Yocto build script (``BuildYoctoProject.sh``) verifies that the following host tools
are on ``PATH`` before it starts:

.. code-block:: text

   bash  curl  chrpath  diffstat  git  gzip  lz4c  mkimage

On Ubuntu 22.04 these can be installed with:

.. code-block:: bash

   sudo apt-get install -y bash curl chrpath diffstat git gzip liblz4-tool u-boot-tools

.. note::

   If all eight tools are already present on the host, the script's host check passes and
   the bare-metal Yocto path documented below can be used without Docker; BitBake's own
   sanity check reports any other missing Yocto host dependency when the build starts.
   If any are absent, install them or use the Docker path.

**Build-output redirection (required before the Yocto build)**

The Yocto build produces roughly 133 GB of intermediates and deploy files under
``firmware/build/``. If ``firmware/build/`` resolves to an NFS home directory or any
partition with less than ~150 GB free, the build will exhaust the quota or run very slowly
over NFS.

Redirect ``firmware/build`` to a local-disk partition before building:

.. code-block:: bash

   rm -rf firmware/build 2>/dev/null
   ln -s <your-local-disk-path>/build firmware/build

Replace ``<your-local-disk-path>`` with a locally mounted partition on your host that has
at least ~150 GB free (a local NVMe or SSD is strongly recommended).

.. note::

   This symlink is not tracked by git (``firmware/build`` is gitignored). It must be
   created once per host and survives across builds. The Vivado firmware build also writes
   its intermediates under ``firmware/build/``, so the symlink benefits both steps.

Firmware build
--------------

Source the Vivado environment (if not already done), change into the target directory, and
run ``make``:

.. code-block:: bash

   source firmware/vivado_setup.sh
   cd firmware/targets/<your-target-dir>/
   make

Verified example (RFSoC 4x2):

.. code-block:: bash

   source firmware/vivado_setup.sh
   cd firmware/targets/SimpleRfSoc4x2Example/
   make

**Approximate timing:** ~17 min on a typical Linux build host with Vivado 2025.2.

After a successful build, the ``.bit`` and ``.xsa`` artifacts are written to:

.. code-block:: text

   firmware/targets/<your-target-dir>/images/<full-name>.bit
   firmware/targets/<your-target-dir>/images/<full-name>.xsa

Concrete example (RFSoC 4x2; sizes are approximate):

.. code-block:: text

   firmware/targets/SimpleRfSoc4x2Example/images/SimpleRfSoc4x2Example-0x04010000-<timestamp>-<user>-<sha>.bit   (~33 MiB)
   firmware/targets/SimpleRfSoc4x2Example/images/SimpleRfSoc4x2Example-0x04010000-<timestamp>-<user>-<sha>.xsa   (~3 MiB)

The filename encodes ``<TargetName>-<PRJ_VERSION>-<YYYYMMDDHHMMSS>-<user>-<git-short-SHA>``.
The current ``PRJ_VERSION = 0x04010000`` corresponds to firmware version ``v4.1.0.0``
(``firmware/targets/shared_version.mk``).

To review Vivado results in GUI mode after a successful build:

.. code-block:: bash

   make gui

Yocto build
-----------

The Yocto build produces the embedded Linux boot images (``BOOT.BIN``, ``image.ub``,
``boot.scr``, ``system.bit``) that run on the RFSoC Processing System (PS). Two paths are
documented: **bare-metal** (recommended; works on any host with the Yocto host package
set installed) and **Docker** (currently blocked by a Dockerfile defect; see below).

Bare-metal path
~~~~~~~~~~~~~~~

From the target directory, pass the ``.xsa`` file from the firmware build to
``BuildYoctoProject.sh``:

.. code-block:: bash

   cd firmware/targets/<your-target-dir>/
   ./BuildYoctoProject.sh -f images/<full-name>.xsa

Concrete example (RFSoC 4x2):

.. code-block:: bash

   cd firmware/targets/SimpleRfSoc4x2Example/
   ./BuildYoctoProject.sh -f images/SimpleRfSoc4x2Example-0x04010000-<timestamp>-<user>-<sha>.xsa

**Approximate timing:** ~45 min on a typical Linux build host with the firmware tree on
local-disk storage (~7200 bitbake tasks).

After a successful build, the boot images are in:

.. code-block:: text

   firmware/build/YoctoProjects/<your-target-dir>/linux/BOOT.BIN    (~35 MiB)
   firmware/build/YoctoProjects/<your-target-dir>/linux/image.ub    (~120 MiB)
   firmware/build/YoctoProjects/<your-target-dir>/linux/boot.scr    (~5 KiB)
   firmware/build/YoctoProjects/<your-target-dir>/linux/system.bit  (~33 MiB)

.. note::

   The deploy path is ``firmware/build/YoctoProjects/<target>/linux/…``; there is
   **no** ``images/`` segment in this path.

A packaged tarball is also produced at:

.. code-block:: text

   firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

The tarball contains ``linux/{BOOT.BIN, boot.scr, image.ub, system.bit, zynqmp_fsbl.elf}``
(plus ``linux/ip`` when ``SOC_IP_STATIC`` is set at build time) and is convenient
when using the scripted SD card imaging approach (see `SD card`_ below).

Docker path
~~~~~~~~~~~

.. warning::

   The Docker build path is currently blocked by a defect in
   ``dockers/yocto/Dockerfile`` (line 4).

   **Root cause:** ``DEBIAN_FRONTEND=noninteractive`` is set as a bash env-prefix before
   ``apt-get update``. This one-shot scope does **not** propagate to the subsequent chained
   ``apt-get install`` commands, so ``xorg`` and ``console-setup`` packages prompt on stdin
   and the build hangs indefinitely.

   **Fix:** declare the variable with ``ENV`` directly after ``FROM`` so it applies to
   every later instruction, then drop the inline prefix:

   .. code-block:: dockerfile

      FROM ubuntu:22.04
      ENV DEBIAN_FRONTEND=noninteractive

   Alternatively, try the BuildKit host-side workaround (untested):

   .. code-block:: bash

      cd dockers/yocto
      DOCKER_BUILDKIT=1 source build_docker.sh

Once the Dockerfile defect is resolved, the Docker path is:

.. code-block:: bash

   # Build the Docker image (one-time step)
   cd dockers/yocto
   source build_docker.sh

   # Launch the container (drops into a shell inside the container)
   source run_docker.sh

Inside the container, run ``BuildYoctoProject.sh`` pointing at the ``.xsa`` file:

.. code-block:: bash

   cd firmware/targets/<your-target-dir>/
   ./BuildYoctoProject.sh -f images/<full-name>.xsa

SD card
-------

Once the Yocto build is complete, write the boot images to an SD card. Full instructions,
covering both scripted approaches and by-hand partitioning, are on the
:doc:`../how-to/sd_card_imaging` page.

The four files that have to end up on the SD card FAT32 boot partition are:

.. code-block:: text

   firmware/build/YoctoProjects/<your-target-dir>/linux/BOOT.BIN
   firmware/build/YoctoProjects/<your-target-dir>/linux/image.ub
   firmware/build/YoctoProjects/<your-target-dir>/linux/boot.scr
   firmware/build/YoctoProjects/<your-target-dir>/linux/system.bit

Every example below uses ``/dev/sdd`` for the card and ``/dev/sdd1`` for its boot
partition. Substitute your own device node, and confirm it against the card's size
and model with ``lsblk -o NAME,SIZE,MODEL,TRAN`` before writing to it.

**Scripted approach** (preferred, using ``CreateDiskImage.sh`` from the platform
submodule). This builds a fully partitioned image containing those four files, then
writes it to the card. Run the script as a normal user: it invokes ``sudo`` itself
for the steps that need root, and it must be executed, not sourced.

.. code-block:: bash

   # Build the image file
   ./firmware/submodules/axi-soc-ultra-plus-core/scripts/CreateDiskImage.sh \
       path_to_image_file.img \
       firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

   # Identify the card, then write the image to the whole disk (not a partition).
   # Check the size and model first: dd overwrites the target with no prompt.
   lsblk -o NAME,SIZE,MODEL,TRAN
   sudo dd if=path_to_image_file.img of=/dev/sdd bs=1M status=progress conv=fsync
   sudo sync

The image is smaller than the card, so the application partition it contains does not
fill the card. That is fine for a first boot; see the how-to page for the grow step.

**One-pass alternative** (``FormatSdCard.sh``). If you just want this one card ready and
do not need a reusable ``.img``, this partitions, formats and loads the card in a single
command, with the application partition sized to the whole card from the start, so there
is no ``dd`` and no grow step. It erases the target and refuses any device that is not
removable media:

.. code-block:: bash

   ./firmware/submodules/axi-soc-ultra-plus-core/scripts/FormatSdCard.sh \
       /dev/sdd \
       firmware/targets/<your-target-dir>/images/<full-name>.linux.tar.gz

**Manual copy recipe**, for a card that is already partitioned and formatted, with
its FAT32 boot partition on ``/dev/sdd1``:

.. code-block:: bash

   sudo mkdir -p boot
   sudo mount /dev/sdd1 boot
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/system.bit boot/
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/BOOT.BIN   boot/
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/image.ub   boot/
   sudo cp firmware/build/YoctoProjects/<your-target-dir>/linux/boot.scr   boot/
   sudo sync boot/
   sudo umount boot

Boot
----

1. Power down the RFSoC board.
2. Confirm the mode slide switch is in the **SD** (not JTAG) position.
3. Insert the SD card.
4. Power up the board.
5. Confirm the board is reachable on the network. The board takes its address from DHCP
   (or from ``SOC_IP_STATIC`` if set at Yocto build time); the examples use ``10.0.0.10``:

   .. code-block:: bash

      ping 10.0.0.10

**Serial console** (for troubleshooting boot failures or network issues):

Connect a USB cable from the host to the board's serial-to-USB bridge. A serial device
appears (e.g. ``/dev/ttyUSB1``). Open a terminal at 115200 baud, 8 data bits,
no parity:

.. code-block:: bash

   cu --line /dev/ttyUSB1 --speed 115200 --parity=none

On Windows, use Tera Term or PuTTY configured for 115200 8N1.
