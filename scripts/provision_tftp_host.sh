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
## image.ub build for the selected board into /tftpboot. Safe to re-run --
## a no-op re-invocation does not re-prompt for sudo and does not re-copy.
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

function show_help {
   echo "USAGE: $0 -b BOARD [-f PATH] [-H]"
   echo " -b BOARD     - Board name, must match a directory name in axi-soc-ultra-plus-core/hardware (required)"
   echo " -f PATH      - Explicit image.ub source path (bypasses build-dir auto-detect)"
   echo " -H           - Show this help text"
   exit 1
}

function die {
    echo "$@"
    exit 1
}

while getopts b:f:H flag
do
    case "${flag}" in
        b) board=${OPTARG};;
        f) srcOverride=${OPTARG};;
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
