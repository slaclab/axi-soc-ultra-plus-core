# Kria KR260 board support

## Goal

Add `hardware/XilinxKriaKr260` in the current Vivado 2026.1/Yocto layout so
that a KR260 Starter Kit boots our own `BOOT.BIN`, Linux and bitstream. Runtime
PL reload with `fpgautil` must work. The first consumer is the `ptp-dev`
`PtpKr260` target, which uses the KR260 as a Zynq UltraScale+ PTP-offload
platform. This directory stays board-only: application pins, clocking and
Ethernet/PTP logic belong to the consuming target.

## Current state, October 20, 2026

Branch `kr260-support` created from `main` at `7.1.0` (`909aa43`). No files
added yet.

## Background

- KR260 support was added in `87e2d22` and removed in `ea9f4d7` (first absent
  in `v2.5.1`): "not able to bypass QSPI boot, which makes reprogramming
  w/ fpgautil not possible".
- That flow was PetaLinux and Vivado 2022.1. Its XDC was a renamed KV260 copy.
  The final device tree (USB5744 hubs, USB2244 SD bridge, SLG7XL45106 resets,
  PCA9546 mux) exists only at `283638c`.
- The KV260 flow switches the board to SD boot by removing carrier straps
  R162/R163, giving `1110` (SD1). That does not carry over:
  - The KR260 has no PS SD card. Its microSD sits behind the USB2244 bridge on a
    USB5744 hub, and the boot ROM cannot reach it.
  - The starter-kit SOM is non-production and has no eMMC.
  - The carrier straps QSPI32 (`0010`).

## Decisions

| Topic | Decision |
| --- | --- |
| Boot firmware | Our `BOOT.BIN` (FSBL, PMU firmware, ATF, U-Boot from the design XSA, without the bitstream) is written into a Kria QSPI A/B slot with `xmutil bootfw_update`. This keeps AMD's ImageSelector, A/B fallback and Ethernet recovery tool. It is not written raw at offset 0. |
| Second stage | meta-kria's U-Boot configuration and `boot.cmd.kria` scan `usb0`/`usb1` and load `image.ub` from the USB SD card. Linux runs from the initramfs. Because a QSPI A/B slot holds 13.5 MB (`0x200000`, `0xF80000`), `BOOT.BIN` omits the bitstream; the startup script loads `/boot/system.bit` with `fpgautil` before inserting the DMA drivers. TFTP boot is a follow-up: `shared/Yocto/recipes-bsp/u-boot` removes the netboot hooks on `:kria` machines, so `-m` has no effect on this board. The existing startup script mounts `/dev/sda1` at `/boot` and `/dev/sda2` at `/app`. |
| Recovery and iteration | JTAG boot-mode override through `xsdb` (`docs/how-to/xsct_boot_mode.rst`). |
| PS configuration | Start from the KV260 2026.1 `AxiSocUltraPlusCpuCore`, keeping the PL clock, HPM/HP ports and DMA. Apply the KR260 peripherals: GEM0 SGMII on PS-GTR lane 0, GEM1 RGMII on MIO 38–49 with MDIO on 50–51, USB0 on MIO 52–63 and USB1 on MIO 64–75 with USB3 on PS-GTR lanes 2/3, SD0/SD1 off, I2C1, UART1, QSPI. Enable the GEM0 timestamp unit. Cross-check against AMD's KR260 board preset. |
| Device tree and Yocto | Mirror the KV260 `zynqmp-user.conf` with the KR carrier override and AMD's `zynqmp-sck-kr-g-revB` overlay. Enable USB storage, the onboard-hub driver, the SLG7XL45106 GPIO driver and DP83867 in U-Boot and the kernel. The MAC address comes from the EEPROM over `nvmem`. AMD's `zynqmp-sck-kr-g-revB` overlay enables both GEMs after the base tree is built, so both stay enabled; bring-up records which jack is `eth0`. `283638c` is a cross-check only. |
| Pins | Board-fixed pins only (fan, SYSMON), taken from the SCK-KR schematic. |
| Deferred | eMMC boot (needs a production SOM; removing R163 gives `0110`). |

## Work items

- **M1:**
  - `ruckus.tcl` (K26/KR260 board part, Vivado 2026.1 or later).
  - `bd/2026.1/AxiSocUltraPlusCpuCore.{tcl,bd}`.
  - `rtl/HardwareTypePkg.vhd` and `xdc/XilinxKriaKr260.xdc`.
  - `Yocto/zynqmp-user.conf` and the device-tree recipe.
  - `README.md` covering boot, QSPI update and recovery.
  - USB boot in the U-Boot environment and the SD/disk-image scripts.
  - QSPI A/B packaging of `BOOT.BIN`.
- **Later (PTP offload):**
  - An option in `roguetcpbridge` to exclude a DMA lane reserved by a kernel
    network driver.
  - A Yocto recipe and device-tree properties for that driver.

## Acceptance (M1)

1. A Vivado 2026.1 build of a consuming target routes with timing met.
2. Yocto produces `BOOT.BIN`, `boot.scr`, `image.ub` and `system.bit`.
3. The QSPI slot boots and is validated, and an unvalidated image falls back once.
4. `BOOT.BIN` is at most `0xD80000` bytes; the board boots from the USB SD card.
5. Linux sees the USB hubs and `/dev/sda`. The jack Linux names `eth0` is identified and links with a stable MAC.
6. Host access works through `roguetcpbridge`: AxiVersion reads, DMA PRBS
   loopback runs, XVC connects.
7. `fpgautil` reloads the PL without a reboot.

## Open checks

- K26 QSPI part and density, and the `xmutil bootfw_update` input format.
- U-Boot USB boot with our configuration, including the hub and reset-chip
  drivers.
- KR260 fan and SYSMON pins.

## References

The `ptp-dev` repository keeps copies under `docs/reference/kr260/`:

- SCK-KR carrier schematic 038-05101-01 Rev A02. Sheets used: 6 (mode
  straps), 14 (SFP+), 16 (clocks, resets), 22 (USB2244) and 24–25 (USB5744).
- AMD UG1092 v1.1.

## Next step

Execute [the M1 implementation plan](m1-implementation.md), starting with the
hardware type and board directory.
