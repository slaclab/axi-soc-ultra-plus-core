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
3. meta-kria's U-Boot runs `boot.scr` (`boot.cmd.kria`), which scans `usb0`/`usb1`
   and boots `image.ub` (kernel, device tree, initramfs) from the SD card.
4. `startup-app-init` mounts `/dev/sda1` at `/boot` and `/dev/sda2` at `/app`,
   loads `/boot/system.bit` with `fpgautil`, then loads the DMA drivers.

The shared U-Boot netboot hooks are not used on Kria, so
`BuildYoctoProject.sh -m` has no effect on this board. Its "BOOT.BIN bitstream:
embedded" message is also wrong here: `BIF_BITSTREAM_ATTR:k26-kr` removes it.

## SD card

Build with the target's `BuildYoctoProject`, then on a Linux host:

```bash
scripts/FormatSdCard.sh /dev/sdX <target>.linux.tar.gz
```

The card holds `BOOT.BIN`, `boot.scr`, `image.ub` and `system.bit` on the
`boot` partition. `BOOT.BIN` on the card is ignored: the board boots from QSPI.

## Installing BOOT.BIN in QSPI

First install, while the board still runs AMD's firmware: use the Boot Image
Recovery Tool (hold FWUEN/SW1 while powering on, then open the web page on the
Ethernet port it reports on the serial console). Alternatively, run
`sudo xmutil bootfw_update -i BOOT.BIN` from AMD's stock SD image. Load our
`BOOT.BIN` into one slot and keep the other slot on AMD's image.

From our image, once Linux runs (`image-update` is installed):

```bash
command -v image_update        # required for slot validation and fallback acceptance
cat /proc/mtd                  # expect AMD's partitions; image_update uses mtd3/mtd4
image_update -p                # persistent state: running and requested image
image_update -i /boot/BOOT.BIN # writes the inactive slot and requests it
reboot
image_update -v                # after a good boot: mark the running image bootable
```

An image that is not marked bootable falls back to the other slot on the next
boot. Keep `image-update` in the image: the Recovery Tool can install or recover
firmware, but does not replace these on-target validation and fallback checks.

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
