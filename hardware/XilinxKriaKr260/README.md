# Kria KR260 Robotics Starter Kit

Board support for the KR260 carrier with the non-production K26 SOM
(`xck26-sfvc784-2lv-c`, board part `xilinx.com:kr260_som:part0:1.1`).
Status and outstanding checks: [docs/plans/kr260-support](../../docs/plans/kr260-support/README.md).

## Boot chain

The carrier straps QSPI32 boot (`MODE[3:0] = 0010`; SCK-KR sheet 6). There is
no PS SD card: the microSD sits behind a USB2244 bridge on a USB5744 hub, so the
boot ROM cannot read it, and the starter-kit SOM has no eMMC. The KV260
SD-boot rework does not apply.

1. The boot ROM runs AMD's ImageSelector from QSPI, which starts `BOOT.BIN`
   from slot A (`0x200000`) or B (`0xF80000`). Each slot holds `0xD80000` bytes.
2. Our `BOOT.BIN` (FSBL, PMU firmware, ATF, U-Boot from the design XSA)
   contains no bitstream.
3. U-Boot runs the netboot hooks, which this board enables in `fallback` mode
   (the ptp-dev `PtpKr260` wrapper always passes `-m fallback`; another mode
   means changing that wrapper). `bootcmd` is `run netboot; run sdboot`: it
   tries TFTP first and, if that fails, `sdboot` runs
   `setenv boot_targets ${usb_boot_devices}; run distro_bootcmd`. Kria's
   `preboot` clears `boot_targets` and only Kria's own `bootcmd` refills it,
   so `UBOOT_SDBOOT_CMD:k26-kr` sets it before distro boot. That runs
   meta-kria's `boot.scr` (from `boot.cmd.kria`) on the USB SD card, which
   boots `image.ub` (kernel, device tree, initramfs). The SD path does not
   re-run `pxe`/`dhcp`. Unverified on hardware.
4. `startup-app-init` mounts `/dev/sda1` at `/boot` and `/dev/sda2` at `/app`,
   loads `/boot/system.bit` with `fpgautil` unless the TFTP path already
   programmed the PL (see Network boot), then loads the DMA drivers.

`SRC_URI:remove:kria` keeps the netboot hooks (`platform-top.h`, `bsp.cfg`) for
the `k26-kr` machine only; other Kria machines still skip them. The shared
`fallback` mode skips the bitstream step because other boards carry it in
`BOOT.BIN`; here `UBOOT_NETBOOT_FALLBACK_LOADPL:k26-kr` and
`UBOOT_NETBOOT_TFTP_LOADPL:k26-kr` select `loadpl_net_marked` instead. The "BOOT.BIN bitstream: embedded" message from `BuildYoctoProject.sh` is
wrong here: `BIF_BITSTREAM_ATTR:k26-kr` removes it.

## Network boot

Hardware behavior described here follows the code and is unverified on the
board.

- Modes: the board uses `fallback`. `sd-only` never tries the network (it
  still uses the `boot_targets` SD command); `tftp-only` never falls back to
  the SD card. The `PtpKr260` wrapper always passes `-m fallback`.
- Server: it must serve `system.bit` (or a per-board `system.bit.<mac>`, MAC
  with dashes, which takes precedence; `system.bin` forms are also probed) for
  `loadpl_net`, the kernel FIT `image.ub`, and optionally `pxelinux.cfg/`. Set
  up a host you own (rdsrv305, private network, not yet configured) with
  `scripts/provision_tftp_host.sh -b XilinxKriaKr260 -f <yocto project>/linux/image.ub -B`.
  `-b` is required, and the script maps a project name only for
  `RealDigitalRfSoC4x2`, so `-f` is needed to bypass auto-detect; `-B` takes
  `system.bit` from beside `image.ub`. The script uses sudo, writes a dnsmasq
  config bound to a fixed interface (`eth2`) with `port=0` (TFTP only, no DHCP),
  and changes the host it runs on. This repository does not configure the
  host's network.
- Interface: the supported setup is J10D (GEM0, AMD's `ethernet0`, U-Boot
  `eth0`) cabled to the TFTP network. Static `ipaddr` and `serverip` apply to
  that device. Another jack needs that device selected and its own
  `ipaddrN`, and `ipaddr` must still be non-empty, because `netboot` decides
  DHCP versus static from `${ipaddr}` alone; that setup is untested. The
  per-MAC file name comes from `ethaddr` (GEM0's MAC), which must be set: with
  it empty, the `setexpr macfn` step fails and the network chain aborts.
- Addressing: on a private network without DHCP, set static `ipaddr` and
  `serverip`. With DHCP, a set `serverip` is kept over the DHCP server's
  address. On a DHCP lab network, `fallback` tries TFTP on every boot from
  whatever next-server the DHCP server offers.
- Environment: the U-Boot environment lives in QSPI at `0x2200000` (redundant
  copy `0x2220000`), outside both A/B slots, so both slots share it. A
  `saveenv` from our U-Boot also changes what AMD's U-Boot in the recovery slot
  runs, and an environment saved earlier by AMD's U-Boot hides our netboot
  variables, because a saved environment replaces the built-in defaults. During
  bring-up, run `printenv bootcmd` (and the netboot variables) first, and
  prefer `setenv` without `saveenv`; record anything saved. To restore
  defaults, run `env default -a; saveenv` from the slot whose U-Boot should own
  the environment, or erase `0x2200000` and `0x2220000`.
- Marker: `loadpl_net_marked` runs `loadpl_net` and then sets `bootargs` from
  `netbootargs`, which carries `axisoc.pl=tftp`. `bootm` and `pxe boot` pass it
  to the kernel, and `startup-app-init` then skips loading `/boot/system.bit`
  even if an SD card is present. To reload the PL at runtime on a TFTP-booted
  board, stop the bridge and drivers first: `systemctl stop startup-app-init`,
  `rmmod axi_memory_map axi_stream_dma`, `fpgautil -b /boot/system.bit`, then
  `systemctl start startup-app-init`. A plain `systemctl restart
  startup-app-init` keeps the TFTP bitstream. `UBOOT_NETBOOT_BOOTARGS:k26-kr` holds the full
  network-path arguments (initramfs root); the SD path instead uses
  `boot.cmd.kria` with `KERNEL_COMMAND_APPEND`. Check effective arguments in
  `/proc/cmdline`, not `/proc/device-tree/chosen/bootargs`.
- Timeouts (unmeasured estimate): with no answering server, the four-name
  bitstream probe in `loadpl_net` runs first, and its failure aborts the
  network chain before the PXE walk, so the shared header's ~85 s PXE estimate
  does not apply here. Expect roughly four failed lwIP TFTP attempts (about
  6 s each, per the shared header; not measured on this board), or the DHCP
  timeout (unmeasured) if no DHCP server answers either, before `run sdboot`
  starts the SD boot.

## Rebuilding after board-support changes

`BuildYoctoProject.sh` copies `Yocto/zynqmp-user.conf` into the project's
machine file only when it configures a project; `recipes-bsp` and the shared
layers are symlinks and update live. After changing this directory's
`zynqmp-user.conf`, rebuild with the target's `BuildYoctoProject -f <xsa> -c`
(a full reconfigure; the shared-state cache is kept). Without `-c` the old
machine settings stay in effect. On October 20, 2026 a rebuild without it kept
the default `sdboot` and an empty `netbootargs` in `BOOT.BIN`.

## SD card

Build with the target's `BuildYoctoProject`, then on a Linux host:

```bash
scripts/FormatSdCard.sh /dev/sdX <target>.linux.tar.gz
```

The card holds `BOOT.BIN`, `boot.scr`, `image.ub` and `system.bit` on the
`boot` partition. `BOOT.BIN` on the card is ignored: the board boots from QSPI.

## Installing BOOT.BIN in QSPI

Keep one slot on AMD's image as the recovery image, and write our `BOOT.BIN`
only into the other slot. Use the Boot Image Recovery Tool for every write: it
lets you choose the slot. Start it by holding FWUEN/SW1 while powering on, then
open the web page on the Ethernet port it reports on the serial console. For
the first install only, `sudo xmutil bootfw_update -i BOOT.BIN` from AMD's stock
SD image also works, because under AMD's firmware the inactive slot is the
other one.

Before the first validation, back up AMD's slot from Linux (see "Image A" and
"Image B" in `/proc/mtd`; `image_update -p` shows the running slot): `dd` the
partition to a file, record its sha256 and copy it off the board.

From our image (`image-update` is installed):

```bash
command -v image_update        # required for slot validation and fallback acceptance
cat /proc/mtd                  # expect AMD's partitions; image_update uses mtd3/mtd4
image_update -p                # persistent state: running and requested image
image_update -v                # after a good boot: mark the running image bootable
```

Do not run `image_update -i` from our image. It writes the inactive slot, which
is AMD's recovery image while ours is running. Restoring validation flags
afterwards does not restore AMD's firmware. To replace our image, load the new
`BOOT.BIN` into our slot with the Recovery Tool, boot it, and run
`image_update -v`. If AMD's slot is overwritten, restore AMD's published Kria
boot firmware through the Recovery Tool; the backup is the byte-exact reference.

AMD's A/B description implies that a requested image never marked bootable
falls back to the last bootable slot on the next boot. This is not yet observed
on this board. Test it on the first, unvalidated install, not by writing a
second copy. Keep `image-update` in the image: the Recovery Tool can install or
recover firmware, but does not replace these on-target validation checks.

## USB and DMA memory checks

The carrier overlay describes the USB5744 hubs, their I2C control paths and
SLG7XL45106 reset wiring. The USB2244 SD reader enumerates through USB without
a `microchip,usb2244` DT node. Confirm enumeration with `lsusb` and the card's
`/dev/sda1` and `/dev/sda2` devices and mounts.

The kernel command line requests `cma=1024M`; this board fragment adds no fixed
CMA reservation. Record the effective bootargs and kernel log showing a
successful 1024 MiB CMA allocation and its physical range in DDR before DMA
acceptance. An allocation failure must be resolved before accepting the image.

## Recovery

- Hold FWUEN at power-on for the Boot Image Recovery Tool.
- JTAG boot without rework: write the boot-mode register through `xsdb`
  ([docs/how-to/xsct_boot_mode.rst](../../docs/how-to/xsct_boot_mode.rst)).

## Pins

The board XDC holds only SYSMON and the fan (`A12`, `HDA20`; high turns the
fan off). Targets own Pmod, SFP and PL Ethernet pins. Take them from AMD's
XilinxBoardStore `kr260_carrier` and `kr260_som` board files and the SCK-KR
schematic.
