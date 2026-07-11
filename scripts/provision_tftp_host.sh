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
## Idempotent host-side TFTP netboot provisioning: ensures the standalone
## dnsmasq TFTP server is configured and running, then stages the newest
## image.ub build for the selected board into /tftpboot along with a
## pxelinux.cfg/default PXE config that names it. Safe to re-run --
## a no-op re-invocation does not re-prompt for sudo and does not re-copy.
##
## With -B, also stages the PL bitstream as a raw .bin (system.bit.bin, plus
## per-MAC copies via -M) for the tftp-only (diskless) build, whose U-Boot env
## fetches and 'fpga load's it before booting the kernel. Fallback-mode servers
## do not need this -- omit -B and only image.ub/PXE are staged.
##
## Reference (proven manual procedure this script formalizes):
##   axi-soc-ultra-plus-core/scripts/NETBOOT_BRINGUP_NOTES.md, sections 1 and 3.
##############################################################################

set -euo pipefail

axi_soc_ultra_plus_core=$(realpath "$(dirname "$(readlink -f "$0")")/..")
hardwareDir="$axi_soc_ultra_plus_core/hardware"

TFTP_ROOT=/tftpboot
DNSMASQ_CONF=/etc/dnsmasq.d/tftp-lab.conf
PIDFILE=/run/dnsmasq-tftp-lab.pid

board=
srcOverride=
stageBitstream=0
macs=()

function show_help {
   echo "USAGE: $0 -b BOARD [-f PATH] [-B] [-M MAC] [-H]"
   echo " -b BOARD     - Board name, must match a directory name in axi-soc-ultra-plus-core/hardware (required)"
   echo " -f PATH      - Explicit image.ub source path (bypasses build-dir auto-detect)"
   echo " -B           - Also stage the PL bitstream (system.bit -> system.bit.bin) for tftp-only/diskless netboot"
   echo " -M MAC       - Stage a per-MAC bitstream copy system.bit.bin.<mac-dashes> (repeatable; implies -B; MAC colons or dashes, stored dash-form)"
   echo " -H           - Show this help text"
   exit 1
}

function die {
    echo "$@"
    exit 1
}

while getopts b:f:BM:H flag
do
    case "${flag}" in
        b) board=${OPTARG};;
        f) srcOverride=${OPTARG};;
        B) stageBitstream=1;;
        # Store the MAC lowercased with ':' -> '-'. U-Boot's tftpboot treats the
        # first ':' in a filename as a hostIP separator, so the board's loadpl_net
        # uses 'setexpr gsub' to request system.bit.bin.<mac-dashes>; the staged
        # filename must match that dash form. Accepts -M with colons or dashes.
        M) stageBitstream=1; m="${OPTARG,,}"; macs+=("${m//:/-}");;
        H) show_help;;
        *) show_help;;
    esac
done

[ -n "$board" ] || { echo "Missing required -b BOARD"; show_help; }

##############################################################################
# Check for missing host tools before we start
##############################################################################

missing=0
for tool in dnsmasq curl find; do
   command -v "$tool" >/dev/null 2>&1 || { echo "Missing tool: $tool"; missing=1; }
done
if [ "$missing" -ne 0 ]; then
   die "Install the missing tool(s) above before running this script."
fi

##############################################################################
# Validate -b against the hardware/<Board> allow-list before using it in any
# find/cp command (never interpolate an unvalidated board name)
##############################################################################

[ -d "$hardwareDir/$board" ] || die "Unknown board '$board' (no directory $hardwareDir/$board)"

##############################################################################
# Board -> Yocto project-name glob mapping.
# This milestone serves only the RFSoC 4x2; add a case here if/when this
# script needs to support additional boards' Yocto project-dir naming.
##############################################################################

function board_to_project_glob {
    case "$1" in
        RealDigitalRfSoC4x2) echo "SimpleRfSoc4x2*" ;;
        *) die "No Yocto project-name mapping for board '$1' (add one in board_to_project_glob())" ;;
    esac
}

##############################################################################
# Step 1: ensure the dnsmasq TFTP-only config exists (check-then-act; only
# sudo tee if absent/different from the proven content)
##############################################################################

function print_dnsmasq_conf {
    cat <<'EOF'
interface=eth2
bind-interfaces
except-interface=lo
port=0
enable-tftp
tftp-root=/tftpboot
EOF
}

if cmp -s <(print_dnsmasq_conf) "$DNSMASQ_CONF" 2>/dev/null; then
   echo "$DNSMASQ_CONF already up to date"
else
   echo "Installing $DNSMASQ_CONF"
   print_dnsmasq_conf | sudo tee "$DNSMASQ_CONF" >/dev/null
fi

##############################################################################
# Step 2: (re)launch the standalone dnsmasq instance only if the pidfile does
# not name a live process (there is no dnsmasq.service on this host).
#
# dnsmasq drops privileges to user 'nobody' after start-up, so a plain
# 'kill -0' from this (non-root) invoking user returns EPERM -- not ESRCH --
# once that happens. is_running() treats EPERM against a dnsmasq process as
# alive instead of misreading it as dead.
##############################################################################

function is_running {
    local pid
    pid=$(cat "$PIDFILE" 2>/dev/null) || return 1
    [ -n "$pid" ] || return 1
    kill -0 "$pid" 2>/dev/null && return 0
    [ -r "/proc/$pid/comm" ] && grep -qa dnsmasq "/proc/$pid/comm"
}

if is_running; then
   echo "dnsmasq already running (pid $(cat "$PIDFILE"))"
else
   if [ -f "$PIDFILE" ]; then
      echo "Reaping stale dnsmasq (pid $(cat "$PIDFILE"))"
      sudo kill "$(cat "$PIDFILE")" 2>/dev/null || true
   fi
   echo "Launching standalone dnsmasq"
   sudo dnsmasq --conf-file="$DNSMASQ_CONF" --pid-file="$PIDFILE"
fi

##############################################################################
# Step 3: resolve the image.ub source (explicit -f override, or auto-detect
# the newest build for the selected board) and hard-copy it flat into
# /tftpboot/image.ub only if it differs from what is already staged
##############################################################################

if [ -n "$srcOverride" ]; then
   src="$srcOverride"
   [ -f "$src" ] || die "Source file '$src' does not exist"
else
   projectGlob=$(board_to_project_glob "$board")
   buildRoot="/u1/${USER}/build/YoctoProjects"
   src=$(find "$buildRoot" -path "*${projectGlob}*/linux/image.ub" \
           -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
   [ -n "$src" ] || die "No image.ub found for board '$board' under $buildRoot"
fi

dest="$TFTP_ROOT/image.ub" # /tftpboot/image.ub -- the FIT the board's tftpboot fetches
if cmp -s "$src" "$dest" 2>/dev/null; then
   echo "$dest already up to date"
else
   echo "Staging $src -> $dest"
   cp "$src" "$dest"
   cmp -s "$src" "$dest" || die "Post-copy verification failed: $dest does not match $src"
   echo "Staged $(stat -c%s "$dest") bytes"
fi

##############################################################################
# Step 4: stage the PXE config the board's 'pxe get' fetches. U-Boot looks up
# pxelinux.cfg/01-<MAC> (per-board override) or pxelinux.cfg/default, parses the
# KERNEL line, and boots that FIT -- so boot behavior can change server-side
# without reflashing U-Boot. KERNEL points at the same flat image.ub staged
# above. Same check-then-act idempotency as the steps before it.
##############################################################################

PXE_DIR="$TFTP_ROOT/pxelinux.cfg"
PXE_DEFAULT="$PXE_DIR/default" # /tftpboot/pxelinux.cfg/default

function print_pxe_default {
    cat <<'EOF'
LABEL Linux
KERNEL image.ub
EOF
}

mkdir -p "$PXE_DIR"
if cmp -s <(print_pxe_default) "$PXE_DEFAULT" 2>/dev/null; then
   echo "$PXE_DEFAULT already up to date"
else
   echo "Staging $PXE_DEFAULT"
   print_pxe_default > "$PXE_DEFAULT"
fi

##############################################################################
# Step 5 (optional, -B/-M): stage the PL bitstream as a raw .bin for the
# tftp-only (diskless) build. The build produces linux/system.bit (a .bit with
# a Vivado header) next to image.ub; U-Boot's 'fpga load' wants the raw PL
# config .bin, so convert with bootgen. Stage the generic system.bit.bin plus
# a per-MAC copy for each -M (U-Boot's loadpl_net tries system.bit.bin.<ethaddr>
# first, then the generic name). Same check-then-act idempotency as above.
##############################################################################

if [ "$stageBitstream" -eq 1 ]; then
   command -v bootgen >/dev/null 2>&1 || \
      die "bootgen not found in PATH (source your Vitis/Vivado settings64.sh); required for -B/-M .bit->.bin conversion"

   bitSrc="$(dirname "$src")/system.bit"  # sits next to the resolved image.ub
   [ -f "$bitSrc" ] || die "No system.bit found next to image.ub at '$bitSrc'"

   # Convert .bit -> raw .bin in an isolated temp dir so bootgen's output name
   # (which is version-dependent) does not matter -- we glob the single *.bin.
   tmpBit=$(mktemp -d)
   trap 'rm -rf "$tmpBit"' EXIT
   cp "$bitSrc" "$tmpBit/system.bit"
   printf 'all:\n{\n    system.bit\n}\n' > "$tmpBit/bitstream.bif"
   ( cd "$tmpBit" && bootgen -arch zynqmp -image bitstream.bif -process_bitstream bin >/dev/null ) \
      || die "bootgen .bit->.bin conversion failed"
   binOut=$(find "$tmpBit" -maxdepth 1 -name '*.bin' | head -1)
   [ -n "$binOut" ] || die "bootgen produced no .bin in $tmpBit"

   bitDest="$TFTP_ROOT/system.bit.bin"
   if cmp -s "$binOut" "$bitDest" 2>/dev/null; then
      echo "$bitDest already up to date"
   else
      echo "Staging $bitSrc -> $bitDest ($(stat -c%s "$binOut") bytes .bin)"
      cp "$binOut" "$bitDest"
      cmp -s "$binOut" "$bitDest" || die "Post-copy verification failed: $bitDest"
   fi

   for mac in ${macs[@]+"${macs[@]}"}; do
      macDest="$TFTP_ROOT/system.bit.bin.$mac"
      if cmp -s "$bitDest" "$macDest" 2>/dev/null; then
         echo "$macDest already up to date"
      else
         echo "Staging per-MAC $macDest"
         cp "$bitDest" "$macDest"
         cmp -s "$bitDest" "$macDest" || die "Post-copy verification failed: $macDest"
      fi
   done
fi
