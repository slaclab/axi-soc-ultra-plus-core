#!/bin/bash
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
## Programs the on-board QSPI boot memory over JTAG using Xilinx
## 'program_flash', taking the .linux.tar.gz produced by BuildYoctoProject.sh
## as its input instead of a Yocto build directory. The tarball is extracted to
## a scratch directory and BOOT.BIN, boot.scr and image.ub are written to their
## respective partition offsets.
##
## The board must be powered on and cabled for JTAG. The device is put into
## JTAG boot mode automatically before flashing (-J skips that), but the board
## is left in JTAG boot mode afterwards, so set the flash boot mode and
## power-cycle to boot what was just written. See docs/how-to/qspi_flash.rst
## and "Change Boot Mode via XSCT" for the surrounding procedure.
##############################################################################

set -euo pipefail

tarball=
flashType=qspi-x8-dual_parallel
density=512
densitySet=0
fsbl=
fsblSource="given with -e"
force=0
hwServer=
hwServerPidsAtStart=
setJtagBootMode=1
bootModeTool=
bootModeToolReq=
programFlash=
blankCheck=1
verify=1
programBootBin=1
tftpOnly=1
armDapTarget=

# Boot Mode register, and the values from docs/how-to/xsct_boot_mode.rst
bootModeReg=0xff5e0200
bootModeJtag=0x0100
bootModeTimeout=30 # seconds to wait for the arm_dap to enumerate the PSU target
bootModeFlash=0x2100 # Quad-SPI (32-bit, dual-parallel)

function show_help {
   echo "USAGE: $0 -f PATH [-t FLASH_TYPE] [-d DENSITY] [-e PATH] [-a] [-S] [-n] [-c] [-F] [-J] [-x TOOL] [-p PATH] [-H]"
   echo " -f PATH        - Path to the .linux.tar.gz produced by BuildYoctoProject.sh,"
   echo "                  containing linux/BOOT.BIN, linux/boot.scr and linux/image.ub (required)"
   echo " -t FLASH_TYPE  - Value passed to 'program_flash -flash_type' (Default: qspi-x8-dual_parallel)"
   echo "                  Must be a 'qspi-*' type; use program_nand_flash.sh for NAND"
   echo " -d DENSITY     - Total flash density in MB across the configuration, passed to"
   echo "                  'program_flash -flash_density' (Default: 512, i.e. two 2Gbit"
   echo "                  MT25QU02G at 256MB each; correct for ZCU208, ZCU216, ZCU111,"
   echo "                  ZCU670 and the SLAC RFMC carrier. The ZCU102 needs -d 128)."
   echo "                  One of 16 32 64 128 256 512 1024 2048. It selects the mini"
   echo "                  u-boot flash writer, and is required for the dual_parallel"
   echo "                  and dual_stacked types"
   echo " -e PATH        - FSBL .elf passed to 'program_flash -fsbl', which is mandatory."
   echo "                  Defaults to linux/zynqmp_fsbl.elf from the tarball, which"
   echo "                  BuildYoctoProject.sh packages from the design's own .xsa."
   echo "                  Only when that is absent, i.e. a tarball built before it was"
   echo "                  packaged, is the matching FSBL extracted from the zip Vitis"
   echo "                  bundles at data/xicom/cfgmem/cfgmem_util/cfgmem_fsbl.zip; that"
   echo "                  one carries a reference PS/DDR configuration and hangs with"
   echo "                  'Timed out while waiting for FSBL to complete' on a custom"
   echo "                  carrier, so pass -e rather than rely on it"
   echo " -a             - Program all images: also boot.scr and image.ub (Default: off)."
   echo "                  By default these scripts run in TFTP network boot mode, which"
   echo "                  needs only BOOT.BIN in flash: image.ub arrives over the network,"
   echo "                  and boot.scr is only read via distro_bootcmd, which the"
   echo "                  tftp-only bootcmd never runs. Use -a when the board must boot"
   echo "                  entirely from flash with no TFTP server"
   echo " -S             - Skip BOOT.BIN (Default: BOOT.BIN is programmed). BOOT.BIN holds"
   echo "                  the FSBL and U-Boot, so it is what TFTP network boot needs"
   echo "                  resident; build with 'BuildYoctoProject.sh -m tftp-only' to get"
   echo "                  the <2MB bitstream-less BOOT.BIN rather than the ~34MB one"
   echo " -n             - Skip the 'program_flash -blank_check -verify' QA/QC pass, which"
   echo "                  is ON by default. That pass confirms the erase, then reads the"
   echo "                  payload back and compares it against the image, which is the"
   echo "                  only positive proof the dual-parallel addressing put the data"
   echo "                  where it belongs. Cost depends strongly on the Vitis release."
   echo "                  Vitis 2026.1, 1.9MB netboot BOOT.BIN, Digilent SMT3 at its"
   echo "                  default 15 MHz: blank_check 19s and verify 19s on top of a 13s"
   echo "                  write, ~38s of a 76s run, which is why they are on by default."
   echo "                  Vitis 2025.2 measured ~2300 B/s writing and ~1354 B/s reading"
   echo "                  back, roughly 65x slower, putting the same write near 14 min"
   echo "                  and the checks near 48 min on top: pass -n on that release."
   echo "                  -a is costly on any release, since image.ub is ~60x larger"
   echo "                  than BOOT.BIN (~52 min extrapolated at 2026.1 rates)"
   echo " -c             - Accepted for compatibility and now redundant, since the checks"
   echo "                  it used to enable are the default. Use -n to turn them off"
   echo " -F             - Program even if a hw_server is already running (see the check below)"
   echo " -J             - Skip the JTAG boot mode step and flash the board as-is"
   echo " -x TOOL        - Debugger console for the boot mode step: xsdb or xsct."
   echo "                  Default: xsdb if present, else xsct. Vitis 2026.1 ships xsct"
   echo "                  but hard-disables it; releases predating xsdb have only xsct"
   echo " -p PATH        - program_flash executable to use (Default: the one on PATH)."
   echo "                  A general escape hatch for when the PATH version misbehaves:"
   echo "                  a 2026.1 mini u-boot was once seen hanging at 'Loading"
   echo "                  Environment from <NULL>', but the 2026.1 program_flash tested"
   echo "                  here prints that same line and proceeds normally, so treat it"
   echo "                  as install-specific rather than a property of the release"
   echo " -H             - Show this help text"
   exit 1
}

##############################################################################
# program_flash needs to halt the PS and download an FSBL over JTAG, so put
# the device into JTAG boot mode first: write the Boot Mode register and issue
# a system reset, which leaves the CPU halted instead of booting from flash.
# This is the JTAG sequence from docs/how-to/xsct_boot_mode.rst, plus a poll
# for the PSU target (see set_jtag_boot_mode for why a script needs it).
#
# Vitis 2026.1 ships xsct but hard-disables it ("XSCT is disabled in Vitis
# 2026.1 release"), so prefer xsdb, which takes the same commands, and only
# fall back to xsct on older installs that predate xsdb.
##############################################################################

function find_boot_mode_tool {
    # An explicit -x wins, so an install that has both can be pinned either way
    # (Vitis 2026.1 ships xsct but hard-disables it, and only xsdb works there;
    # releases before xsdb existed have only xsct).
    if [ -n "$bootModeToolReq" ]; then
       command -v "$bootModeToolReq" >/dev/null 2>&1 || \
          die "-x '$bootModeToolReq' not found in PATH"
       bootModeTool=$bootModeToolReq
    elif command -v xsdb >/dev/null 2>&1; then
       bootModeTool=xsdb
    elif command -v xsct >/dev/null 2>&1; then
       bootModeTool=xsct
    else
       die "Neither xsdb nor xsct found in PATH; needed for the JTAG boot mode step (use -J to skip)"
    fi
}

function set_jtag_boot_mode {
    local tcl="$tmpDir/jtag_boot_mode.tcl"
    # The arm_dap takes a few seconds to enumerate after connect, so the PSU
    # target does not exist yet the instant we return. The interactive recipe
    # never notices because a human types the next line seconds later; run
    # back-to-back it races and fails with "no targets found", listing only
    # PS TAP / PMU / PL. Poll for the target instead of sleeping blindly.
    cat > "$tcl" <<EOF
connect
set deadline [expr {[clock seconds] + $bootModeTimeout}]
while {1} {
    if {![catch {targets -set -nocase -filter {name =~ "*PSU*"}}]} { break }
    if {[clock seconds] > \$deadline} {
        puts "PSU target did not appear within $bootModeTimeout s. Available targets:"
        puts [targets]
        error "PSU target unavailable"
    }
    after 1000
}
stop
mwr $bootModeReg $bootModeJtag
rst -system
disconnect
EOF
    echo "########################################################################"
    echo "Setting JTAG boot mode ($bootModeReg = $bootModeJtag) via $bootModeTool"
    echo "########################################################################"
    "$bootModeTool" "$tcl" || die "Failed to set JTAG boot mode. Check the JTAG chain with:
  $bootModeTool -eval 'connect; after 4000; jtag targets'
A healthy chain lists the device IDCODEs; 'DR shift through all zeroes' means
no device is answering (board powered off, held in reset, or JTAG miscabled)."
}

##############################################################################
# program_flash connects to TCP:localhost:3121 and drives whatever JTAG cable
# that hw_server owns. It spawns its own hw_server and reaps it on exit, so an
# hw_server that is up before we start belongs to another session (a Vivado
# hardware manager, an xsct/xsdb console, or another user on this host).
# Flashing through it would contend for that session's cable, so stop unless
# -F says otherwise. Note that 'killVivado' does not reap hw_server.
##############################################################################

##############################################################################
# program_flash requires -fsbl for ZynqMP and does not fall back to anything:
# without it, it stops with
#   ERROR: Please specify a valid FSBL file for flash type: cfgmem-...
# Vitis does bundle a per-flash-type FSBL, but inside a zip rather than as a
# loose file, so extract the matching member instead of making every caller
# supply one. -e still wins, and the tarball's own FSBL wins over this.
#   qspi-x8-dual_parallel -> cfgmem_fsbl/zynqmp/zynqmp_qspi_dual_parallel_x8_fsbl.elf
##############################################################################

function extract_bundled_fsbl {
    local pf zip width config base
    command -v unzip >/dev/null 2>&1 || return 1
    pf="$programFlash"
    zip=$(readlink -f "$(dirname "$pf")/../data/xicom/cfgmem/cfgmem_util/cfgmem_fsbl.zip" 2>/dev/null) || return 1
    [ -f "$zip" ] || return 1
    width=${flashType#qspi-}; width=${width%%-*}
    config=${flashType#qspi-*-}
    base="zynqmp_qspi_${config}_${width}_fsbl.elf"
    unzip -j -o "$zip" "cfgmem_fsbl/zynqmp/$base" -d "$tmpDir" >/dev/null 2>&1 || return 1
    [ -f "$tmpDir/$base" ] || return 1
    fsbl="$tmpDir/$base"
    fsblSource="bundled with Vitis"
    return 0
}

##############################################################################
# program_flash defaults to the first device on the chain, which on ZynqMP is
# the FPGA TAP:
#   Target not specified. Selecting target_id 2 (xczu48dr) by default.
# Flash programming has to run through the ARM DAP instead. Driving the FPGA
# TAP gets as far as "Finished running FSBL." and then fails with "Problem in
# running uboot" without the mini u-boot ever producing a console; via the DAP
# the same command boots the mini u-boot. Every ZynqMP example in
# 'program_flash -help' likewise names a jsn-...-5ba00477-0 (arm_dap) target.
#
# -jtagtargets prints one line per device, e.g.
#   3  jsn-JTAG-SMT3-210357BC194BA-5ba00477-0  (name arm_dap  idcode 5ba00477)
# so match on the arm_dap name rather than hardcoding a positional id.
##############################################################################

function find_arm_dap_target {
    armDapTarget=$("$programFlash" -jtagtargets 2>/dev/null | awk '/name arm_dap/ {print $2; exit}') || true
    if [ -z "$armDapTarget" ]; then
       echo "WARNING: no arm_dap target found on the JTAG chain; letting program_flash" >&2
       echo "         pick its default target. If it reports 'Problem in running uboot'," >&2
       echo "         check 'program_flash -jtagtargets' for an arm_dap entry." >&2
    fi
}

function hw_server_holders {
    ps -eo pid,user,comm,args 2>/dev/null | awk '$3 == "hw_server" || $4 ~ /\/hw_server$/ {print "  pid "$1" (user "$2")"}'
}

##############################################################################
# Reap only the hw_server processes this run caused. xsdb leaves one behind
# when the boot mode step fails, and so does program_flash when it dies
# abnormally, which would then trip the check above on the next invocation.
# Scoped to our own uid and to pids that were not already running at start,
# so another user's session on this host is never touched.
##############################################################################

function reap_our_hw_server {
    local pid
    for pid in $(pgrep -x -u "$(id -u)" hw_server 2>/dev/null || true); do
       case " $hwServerPidsAtStart " in
          *" $pid "*) continue;;
       esac
       echo "Reaping hw_server pid $pid started by this run"
       kill "$pid" 2>/dev/null || true
    done
}

function cleanup {
    if [ -n "${tmpDir:-}" ]; then
       rm -rf "$tmpDir"
    fi
    reap_our_hw_server
}

function check_hw_server {
    local holders
    # pgrep exits 1 when nothing matches, which set -e/pipefail would treat as
    # fatal, so absorb it: no match simply means no hw_server was running
    hwServerPidsAtStart=$( (pgrep -x hw_server 2>/dev/null || true) | tr '\n' ' ')
    holders=$(hw_server_holders)
    if [ -z "$holders" ]; then
       hwServer="none running at start, program_flash will spawn its own"
       return 0
    fi
    if [ "$force" -eq 1 ]; then
       hwServer="already running, sharing its JTAG cable (-F override)"
       echo "WARNING: hw_server already running, continuing because -F was given:" >&2
       echo "$holders" >&2
       return 0
    fi
    echo "ERROR: hw_server is already running on $(hostname):" >&2
    echo "$holders" >&2
    echo "program_flash would attach to that session and share its JTAG cable." >&2
    echo "Close the other Vivado/Vitis hardware session first, or pass -F to override." >&2
    exit 1
}

function die {
    echo "$@" >&2
    exit 1
}

while getopts f:t:d:e:FJHx:p:cnaS flag
do
    case "${flag}" in
        f) tarball=${OPTARG};;
        t) flashType=${OPTARG};;
        d) density=${OPTARG}; densitySet=1;;
        e) fsbl=${OPTARG};;
        a) tftpOnly=0;;
        S) programBootBin=0;;
        c) blankCheck=1; verify=1;;
        n) blankCheck=0; verify=0;;
        F) force=1;;
        J) setJtagBootMode=0;;
        x) bootModeToolReq=${OPTARG};;
        p) programFlash=${OPTARG};;
        H) show_help;;
        *) show_help;;
    esac
done

[ -n "$tarball" ] || { echo "Missing required -f PATH"; show_help; }
[ -f "$tarball" ] || die "Tarball '$tarball' does not exist"
tarball=$(realpath "$tarball")

# The image.ub offset below is the QSPI one, so reject a non-QSPI flash type
# rather than write image.ub to an offset that does not match it
case "$flashType" in
   qspi*) ;;
   *) die "Flash type '$flashType' is not a QSPI type; use program_nand_flash.sh for NAND";;
esac

case "$density" in
   16|32|64|128|256|512|1024|2048) ;;
   *) die "Invalid -d density '$density' (expected one of 16 32 64 128 256 512 1024 2048, in MB)";;
esac

if [ -n "$fsbl" ]; then
   [ -f "$fsbl" ] || die "FSBL '$fsbl' does not exist"
   fsbl=$(realpath "$fsbl")
fi

if [ -n "$programFlash" ]; then
   [ -x "$programFlash" ] || die "-p '$programFlash' is not an executable file"
   programFlash=$(readlink -f "$programFlash")
else
   command -v program_flash >/dev/null 2>&1 || \
      die "program_flash not found in PATH (source your Vivado/Vitis settings64.sh)"
   programFlash=$(command -v program_flash)
fi

if [ "$setJtagBootMode" -eq 1 ]; then
   find_boot_mode_tool
fi

check_hw_server
find_arm_dap_target

##############################################################################
# Extract the tarball to a scratch directory that is cleaned up on exit.
# program_flash needs real files on disk, and the images inside the tarball are
# already laid out under a 'linux/' prefix.
##############################################################################

tmpDir=$(mktemp -d)
trap cleanup EXIT

echo "Extracting $tarball"
tar -xzf "$tarball" --directory="$tmpDir" || die "Failed to extract '$tarball'"

##############################################################################
# Images to program, "name:offset", in flash order. BOOT.BIN is opt-in (-B):
# BOOT.BIN carries the FSBL and U-Boot and is programmed by default (-S skips
# it). It is all that TFTP network boot needs resident, so it is the only image
# written by default:
#
#   - image.ub is fetched over the network by the netboot env, not read from
#     flash.
#   - boot.scr is only reached through distro_bootcmd, which the tftp-only
#     bootcmd never runs. Its bootcmd is "run netboot; echo TFTP-only build:
#     not falling back to SD", and netboot itself uses pxe boot or a tftpboot
#     of image.ub. Neither path touches script_offset_f.
#
# -a adds both back for a board that has to boot entirely from flash. Only the
# selected images have to be present in the tarball.
##############################################################################

imagesToProgram=()
if [ "$programBootBin" -eq 1 ]; then
   imagesToProgram+=("BOOT.BIN:0x0000000")
fi
if [ "$tftpOnly" -eq 0 ]; then
   imagesToProgram+=("boot.scr:0x3E80000")
   imagesToProgram+=("image.ub:0x4000000")
fi

[ "${#imagesToProgram[@]}" -gt 0 ] || \
   die "Nothing to program: -S skipped BOOT.BIN and tftp-only mode skips the rest. Drop -S, or add -a."

linuxDir="$tmpDir/linux"
for entry in "${imagesToProgram[@]}"; do
   image=${entry%%:*}
   [ -f "$linuxDir/$image" ] || die "Missing linux/$image in '$tarball'"
done

# BuildYoctoProject.sh packages the FSBL built from the design's own .xsa as
# linux/zynqmp_fsbl.elf, so prefer it over the generic one Vitis bundles. An
# explicit -e still wins. Tarballs built before it was packaged lack it, which
# is what the extract_bundled_fsbl fallback below is left for.
if [ -z "$fsbl" ] && [ -f "$linuxDir/zynqmp_fsbl.elf" ]; then
   fsbl="$linuxDir/zynqmp_fsbl.elf"
   fsblSource="from the tarball"
fi

if [ -z "$fsbl" ]; then
   extract_bundled_fsbl || die "program_flash requires an FSBL and none could be
extracted from Vitis for flash type '$flashType'. Supply one with -e PATH."
   # The bundled FSBLs are generic: they carry a reference PS/DDR configuration,
   # not the one from your design's .xsa. On a custom carrier that can hang with
   # "Timed out while waiting for FSBL to complete", so point at the better one.
   echo "WARNING: using the generic FSBL Vitis bundles for $flashType. It is not" >&2
   echo "         built for this board's PS/DDR configuration and may hang with" >&2
   echo "         'Timed out while waiting for FSBL to complete'. Prefer the FSBL" >&2
   echo "         your Yocto build produced, passed with -e:" >&2
   echo "         <YoctoProject>/build/tmp/deploy/images/zynqmp-user/fsbl-zynqmp-user.elf" >&2
fi

##############################################################################
# program_flash lists -flash_density as required for the dual_parallel and
# dual_stacked configurations, and does not list it for the single ones, so
# only pass it where it is called for (or where -d was given explicitly).
# Omitting it on a dual config fails with:
#   ERROR: Flash Density not specified. Please specify Flash Density using
#          -flash_density option for SPI dual configurations.
##############################################################################

case "$flashType" in
   *dual_parallel*|*dual_stacked*) useDensity=1;;
   *)                              useDensity=$densitySet;;
esac

##############################################################################
# -blank_check and -verify are ON by default. -verify reads the payload back and
# compares it against the image, which is the only positive confirmation that
# the dual-parallel addressing landed the data where it belongs; program_flash
# also emits a spurious "[Xicom 50-353] Flash Mismatched" on every dual-parallel
# run (it string-compares the selected cfgmem against a single-die ID, which can
# never match), so a passing verify is what tells you to ignore it.
#
# Throughput depends strongly on the Vitis release, so any figure here is only
# meaningful next to the release it came from.
#
# Vitis 2026.1, 1,949,892 byte netboot BOOT.BIN, qspi-x8-dual_parallel, Digilent
# SMT3 at its default 15 MHz: erase 1s, blank_check 19s, write 13s (~150 kB/s),
# verify 19s, 76s total wall time. The pair costs ~38s, hence the default.
#
# Vitis 2025.2 measured ~2300 B/s writing and ~1354 B/s reading back, roughly
# 65x slower, which put the same BOOT.BIN write near 14 min with the checks
# adding ~48 min on top. That is where this script's earlier "~1.4 kB/s" and
# "24 min each" figures came from; they were correct for 2025.2 and are simply
# not transferable. On a release that behaves like it, pass -n.
#
# The default image set is BOOT.BIN alone. -a is a different matter on any
# release: image.ub is ~60x larger, extrapolating to roughly 52 min at 2026.1
# rates versus ~14 min with -n. Unmeasured.
##############################################################################

defaultParameter=(-flash_type "$flashType")
if [ "$blankCheck" -eq 1 ]; then
   defaultParameter+=(-blank_check)
fi
if [ "$verify" -eq 1 ]; then
   defaultParameter+=(-verify)
fi
if [ "$useDensity" -eq 1 ]; then
   defaultParameter+=(-flash_density "$density")
fi
if [ -n "$fsbl" ]; then
   defaultParameter+=(-fsbl "$fsbl")
fi
if [ -n "$armDapTarget" ]; then
   defaultParameter+=(-target_name "$armDapTarget")
fi

##############################################################################
# Program each boot image at its partition offset
##############################################################################

function flash_image {
   local image=$1
   local offset=$2
   echo "########################################################################"
   echo "Programming $image at offset $offset"
   echo "########################################################################"
   "$programFlash" -f "$linuxDir/$image" -offset "$offset" "${defaultParameter[@]}" || \
      die "program_flash failed while programming $image at offset $offset"
}

echo "########################################################################"
echo "Flash type: $flashType"
if [ "$useDensity" -eq 1 ]; then
   echo "Flash density: $density MB total ($((density * 8)) Mbit)"
fi
echo "FSBL: $(basename "$fsbl") ($fsblSource)"
echo "JTAG target: ${armDapTarget:-<program_flash default>}"
echo "program_flash: $programFlash"
echo "Mode: $([ "$tftpOnly" -eq 1 ] && echo "tftp-only (BOOT.BIN only; image.ub and boot.scr come over the network)" || echo "all images from flash")"
echo "Images: $(for e in "${imagesToProgram[@]}"; do printf "%s@%s " "${e%%:*}" "${e##*:}"; done)"
echo "Checks: blank_check=$([ "$blankCheck" -eq 1 ] && echo on || echo off) verify=$([ "$verify" -eq 1 ] && echo on || echo off)"
echo "hw_server: $hwServer"
if [ "$setJtagBootMode" -eq 1 ]; then
   echo "Boot mode: setting JTAG ($bootModeJtag) via $bootModeTool before flashing"
else
   echo "Boot mode: left as-is (-J)"
fi
echo "########################################################################"

if [ "$setJtagBootMode" -eq 1 ]; then
   set_jtag_boot_mode
fi

for entry in "${imagesToProgram[@]}"; do
   flash_image "${entry%%:*}" "${entry##*:}"
done

echo "########################################################################"
echo "Flash programming complete"
if [ "$setJtagBootMode" -eq 1 ]; then
   echo "The board is left in JTAG boot mode via the $bootModeReg override."
   echo "A power-cycle clears that override and the M[3:0] straps take effect"
   echo "again, so if the straps already select QSPI (0b0010 = QSPI32), just"
   echo "power-cycle. To switch without a power-cycle, write $bootModeFlash to"
   echo "$bootModeReg then 'rst -system' (see docs/how-to/xsct_boot_mode.rst)."
fi
echo "########################################################################"
