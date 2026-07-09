##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
##
## reflash_via_jtag.tcl -- JTAG-driven SD-boot reflash for the Zynq
## UltraScale+ boards in this submodule, with a documented recovery net.
##
## Usage (from an xsct/xsdb shell):
##   cd submodule/axi-soc-ultra-plus-core
##   xsct
##   source scripts/reflash_via_jtag.tcl
##   warm_inject_reflash /path/to/known-good/u-boot.elf
##
## Two paths are provided, each independently bracketed by `connect` at the
## top and `disconnect` at the bottom, selecting targets only via
## `targets -set -filter {name =~ "..."}` (`-nocase` for the A53 core) --
## never a bare numeric target index.
##
## 1. warm_inject_reflash (normal path): halts the already-running APU core
##    (DDR is already initialized by the power-on FSBL) and downloads a
##    known-good u-boot.elf directly into DDR. Deliberately does not perform
##    a full system reset, which would re-sample the SD boot strap and boot
##    the OLD image, discarding the entire point of the warm inject.
##
##    Sets `configparams force-mem-accesses 1` before `stop`/`dow` --
##    without it, `dow` into a live target hits "Memory write error ...
##    MMU fault" because the debugger's memory writes go through whatever
##    address translation is currently active on the halted core; the
##    force flag routes them as raw physical-address writes instead.
##
##    Only run this against a target sitting at a single-core U-Boot
##    prompt (DDR warm, no OS loaded yet) -- not against a fully-booted
##    multi-core Linux OS. Halting only the A53 #0 core while the other
##    cores keep running an SMP kernel starves the halted core of its
##    scheduler/IPI duties and triggers RCU stalls on the still-running
##    cores. If this is attempted against a running OS by mistake, resume
##    the halted core with `con` (no `dow`, no reset) to let the kernel
##    catch back up; do not `rst -system` to "fix" it.
##
##    From the resulting JTAG-loaded U-Boot prompt, drive the actual SD write
##    over the serial console:
##      setenv serverip 10.0.0.1
##      mmc dev 0 && mmc rescan
##      tftpboot ${scratch_addr} BOOT.BIN
##      fatwrite mmc 0:1 ${scratch_addr} BOOT.BIN ${filesize}
##      tftpboot ${scratch_addr} image.ub
##      fatwrite mmc 0:1 ${scratch_addr} image.ub ${filesize}
##      reset
##    `fatwrite mmc 0:1` is the FAT-partition, filename-addressed write
##    primitive -- it mirrors the read-side `fatload` the distro-boot script
##    already uses. Never use raw `mmc write`: it targets absolute LBA
##    offsets with no FAT-filesystem awareness and can corrupt the
##    partition table, image.ub, or a stray uboot.env living in the same
##    FAT partition.
##
## 2. cold_recovery_reflash (last-resort path): used only if
##    warm_inject_reflash cannot reach a live U-Boot prompt (DDR was never
##    initialized this power cycle -- FSBL never ran). Overrides the sampled
##    boot-mode register (no physical SD/JTAG switch access is available)
##    and drives the full PMU -> FSBL -> U-Boot download chain. The
##    jtag_mode_val override MUST be verified against UG1085's
##    CRP.BOOT_MODE_USER encoding -- or empirically, by comparing the
##    mrd -force reads below before and after the override -- before this
##    path is trusted for a real recovery; do not trust a third-party value
##    blind. Rehearse this path once against the current known-good image
##    before ever depending on it for a real recovery.
##############################################################################

proc warm_inject_reflash {uboot_elf} {
    connect
    configparams force-mem-accesses 1
    targets -set -nocase -filter {name =~ "*A53*#0*"}
    stop
    dow $uboot_elf
    con
    disconnect
    puts "warm_inject_reflash: $uboot_elf loaded and running from DDR."
    puts "Drive the fatwrite reflash sequence from the serial console now (see header comment)."
}

proc cold_recovery_reflash {pmufw_elf fsbl_elf uboot_elf {jtag_mode_val 0x0100}} {
    connect

    targets -set -filter {name =~ "PSU"}
    stop
    set before [mrd -force 0xFF5E0200]
    puts "BOOT_MODE_USER before override = $before"
    mwr 0xFF5E0200 $jtag_mode_val
    set after [mrd -force 0xFF5E0200]
    puts "BOOT_MODE_USER after override  = $after"
    rst -system

    targets -set -filter {name =~ "MicroBlaze PMU"}
    dow $pmufw_elf
    con

    targets -set -filter {name =~ "PSU"}
    dow $fsbl_elf
    con

    targets -set -nocase -filter {name =~ "*A53*#0*"}
    dow $uboot_elf
    con

    disconnect
    puts "cold_recovery_reflash: PMU/FSBL/U-Boot chain loaded from JTAG. Verify at the serial console."
}
