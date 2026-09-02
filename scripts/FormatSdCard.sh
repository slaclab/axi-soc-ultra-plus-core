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
## Partitions, formats and loads a physical SD card in one step, from the
## .linux.tar.gz that BuildYoctoProject.sh produces:
##
##   p1  1 GiB  FAT32, label 'boot', bootable -- BOOT.BIN, image.ub, boot.scr
##                                               and system.bit are copied here
##   p2  rest   ext4,  label 'root'           -- the application partition,
##                                               sized to the rest of the card
##
## This is the in-place counterpart to CreateDiskImage.sh. Use this one when
## you have the card in a reader and want the application partition sized to
## the whole card in a single pass. Use CreateDiskImage.sh instead when you
## want a reusable .img artifact to archive or to write to many cards.
##
## *** THIS ERASES THE TARGET DEVICE. ***
##
## Interlocks that CANNOT be bypassed, because naming the wrong device here
## destroys whatever is on it:
##   - the target must be a whole disk, never a partition
##   - the target must be either a removable disk on the USB bus, or an
##     SD/MMC device (/dev/mmcblk*). Fixed/internal disks are refused.
## Only the interactive confirmation can be skipped, with -y. If your card is
## on some reader that these checks refuse, use the manual recipe in
## docs/how-to/sd_card_imaging.rst rather than reaching for a bigger hammer.
##
## Run this as a NORMAL USER, and do not run it with sudo. Only the steps that
## actually need root are individually sudo'd, and the sudo timestamp is
## refreshed up front so the password prompt appears before the confirmation
## rather than in the middle of the run.
##############################################################################

set -euo pipefail

# Boot partition size in MiB. The remainder of the card becomes p2.
boot_size_mb=1024
assume_yes=0

# Assigned before the cleanup trap is armed so 'set -u' cannot trip inside it.
mount_dir=

function show_help {
   echo "USAGE: $0 [-b BOOT_MB] [-y] [-H] DEVICE LINUX_TARBALL"
   echo " DEVICE         - Whole-disk device node of the SD card, e.g. /dev/sdd"
   echo "                  (not /dev/sdd1). Must be a removable USB disk or an"
   echo "                  /dev/mmcblk* device. ALL DATA ON IT IS ERASED."
   echo " LINUX_TARBALL  - <full-name>.linux.tar.gz from BuildYoctoProject.sh"
   echo " -b BOOT_MB     - Boot partition size in MiB (default $boot_size_mb)."
   echo "                  The rest of the card becomes the ext4 partition."
   echo " -y             - Skip the confirmation prompt (for automation). The"
   echo "                  device interlocks still apply."
   echo " -H             - Show this help text"
   exit 1
}

function die {
    echo "$@" >&2
    exit 1
}

function cleanup {
    if [ -n "$mount_dir" ]; then
        if mountpoint -q "$mount_dir"; then
            sudo umount "$mount_dir" || true
        fi
        rmdir "$mount_dir" 2>/dev/null || true
    fi
}
trap cleanup EXIT

while getopts b:yH flag
do
   case $flag in
      b) boot_size_mb=$OPTARG ;;
      y) assume_yes=1 ;;
      H) show_help ;;
      *) show_help ;;
   esac
done
shift $((OPTIND - 1))

[ $# -eq 2 ] || show_help

dev=$1
archive_file=$(realpath -m "$2")
base=${dev#/dev/}

##############################################################################
# Validate the arguments and the target device before anything is written.
##############################################################################

case $boot_size_mb in ''|*[!0-9]*) die "-b must be a whole number of MiB, got '$boot_size_mb'" ;; esac
[ "$boot_size_mb" -gt 0 ] || die "-b must be greater than zero"

[ -b "$dev" ] || die "$dev is not a block device"
[ "$(lsblk -dno TYPE "$dev")" = "disk" ] \
    || die "$dev is not a whole disk. Pass the disk (e.g. /dev/sdd), not a partition (e.g. /dev/sdd1)"

##############################################################################
# Device interlock. A typo of one letter here is the difference between an SD
# card and a system or data disk, so require positive evidence that the target
# is removable media before erasing it.
##############################################################################

case $base in
    mmcblk*)
        # An SD/MMC host controller only ever presents SD/MMC cards
        ;;
    *)
        removable=$(cat "/sys/block/$base/removable" 2>/dev/null || echo 0)
        # '|| true' because head closing the pipe early would otherwise SIGPIPE
        # udevadm and, under pipefail, fail the assignment
        bus=$(udevadm info --query=property --name="$dev" 2>/dev/null | sed -n 's/^ID_BUS=//p' | head -1 || true)
        [ "$removable" = "1" ] \
            || die "$dev is not removable (/sys/block/$base/removable = $removable). Refusing to erase it."
        [ "${bus:-unknown}" = "usb" ] \
            || die "$dev is not on the USB bus (ID_BUS=${bus:-unknown}). Refusing to erase it."
        ;;
esac

##############################################################################
# Validate the images only after the target has cleared the interlock, so a
# refused device is always the first thing reported.
##############################################################################

[ -f "$archive_file" ] || die "Linux tarball $archive_file does not exist"
# The tarball packs the boot files under linux/, which --strip-components=1
# below removes. Check that up front rather than after the card is erased.
# '|| true' because head closing the pipe early would otherwise SIGPIPE tar
# and, under pipefail, fail the assignment on a perfectly good tarball.
first_entry=$(tar tzf "$archive_file" 2>/dev/null | head -1 || true)
case $first_entry in
    linux/*) ;;
    *) die "$archive_file does not look like a .linux.tar.gz (expected a top-level linux/ directory, found '${first_entry:-nothing}')" ;;
esac

dev_size=$(lsblk -dno SIZE "$dev" | tr -d ' ')
dev_model=$(lsblk -dno MODEL "$dev" | sed 's/ *$//')

##############################################################################
# Confirm, showing what the interlocks let through so the size and model can
# be eyeballed against the card that is actually in the reader.
##############################################################################

echo "About to ERASE and reformat:"
echo "  Device : $dev"
echo "  Size   : $dev_size"
echo "  Model  : ${dev_model:-(unknown)}"
echo "  Layout : ${boot_size_mb} MiB FAT32 'boot' (bootable) + rest ext4 'root'"
echo "  Images : $archive_file"

# Anything currently mounted from this card has to go away before repartitioning
mounted=$(lsblk -rno NAME,MOUNTPOINT "$dev" | awk 'NF>1 {print "    /dev/"$1" mounted on "$2}')
if [ -n "$mounted" ]; then
    echo "  The following will be unmounted first:"
    echo "$mounted"
fi

if [ "$assume_yes" -eq 0 ]; then
    printf "Type '%s' to proceed, anything else to abort: " "$base"
    read -r reply
    [ "$reply" = "$base" ] || die "Aborted, nothing was written."
fi

##############################################################################
# Refresh the sudo timestamp before doing any work, so the password prompt
# does not surface partway through the reformat.
##############################################################################

sudo -v || die "sudo authentication failed"

# Partition device naming: /dev/sdd -> /dev/sdd1, /dev/mmcblk0 -> /dev/mmcblk0p1
if [ -z "${base##*[0-9]}" ]; then
    part="${dev}p"
else
    part="$dev"
fi

if [ -n "$mounted" ]; then
    echo "Unmounting existing partitions on $dev"
    # Not every child is mounted, so tolerate "not mounted" on the others
    for p in $(lsblk -rno NAME "$dev" | tail -n +2); do
        sudo umount "/dev/$p" 2>/dev/null || true
    done
    still=$(lsblk -rno NAME,MOUNTPOINT "$dev" | awk 'NF>1 {print $1}')
    [ -z "$still" ] || die "Could not unmount: $still. Close anything using the card and retry."
fi

##############################################################################
# Partition and format.
##############################################################################

echo "Clearing old filesystem and partition signatures"
sudo wipefs -a "$dev" >/dev/null

echo "Writing partition table (${boot_size_mb} MiB boot + remainder)"
# sfdisk takes the layout declaratively, which is why this does not have to
# replay the interactive fdisk keystrokes. 2048 sectors of alignment offset and
# 2048 sectors per MiB at the 512-byte sector size fdisk also assumes. Type c
# (W95 FAT32 LBA) is the conventional tag for the boot partition; the BootROM
# reads the FAT filesystem itself and does not consult the type byte.
printf 'label: dos\nunit: sectors\nstart=2048, size=%s, type=c, bootable\nstart=%s, type=83\n' \
    $((boot_size_mb * 2048)) $((2048 + boot_size_mb * 2048)) \
    | sudo sfdisk --quiet "$dev"

# Wait for the kernel and udev to expose the new partition nodes
sudo partprobe "$dev" 2>/dev/null || true
sudo udevadm settle 2>/dev/null || true
for _ in $(seq 1 20); do
    [ -b "${part}1" ] && [ -b "${part}2" ] && break
    sleep 0.5
done
[ -b "${part}1" ] || die "The kernel did not expose ${part}1 after partitioning $dev"
[ -b "${part}2" ] || die "The kernel did not expose ${part}2 after partitioning $dev"

echo "Formatting ${part}1 as FAT32 (label 'boot')"
sudo mkfs.vfat -F 32 -n boot "${part}1"

echo "Formatting ${part}2 as ext4 (label 'root')"
sudo mkfs.ext4 -q -L root "${part}2"

##############################################################################
# Load the boot images onto the boot partition.
##############################################################################

mount_dir=$(mktemp -d)
sudo mount "${part}1" "$mount_dir"
mountpoint -q "$mount_dir" || die "Failed to mount ${part}1 on $mount_dir"

echo "Extracting boot images to ${part}1"
# --strip-components=1 drops the tarball's linux/ prefix so the four boot files
# land at the root of the partition, which is where the BootROM looks
sudo tar xfo "$archive_file" --strip-components=1 --directory="$mount_dir"
# umount below flushes on its own, but syncing here surfaces a late write error
# (a too-small -b draining into ENOSPC, say) as its own attributable failure
# rather than as something emerging from umount while cleanup unwinds
sudo sync "$mount_dir/"

echo "Contents of ${part}1:"
ls -la "$mount_dir" | tail -n +2

sudo umount "$mount_dir"
rmdir "$mount_dir"
mount_dir=

# umount has returned, so the card is flushed and safe to physically remove
echo "Done. $dev is flushed and unmounted; it is safe to remove and boot."
