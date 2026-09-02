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
## Builds a bootable SD card image (.img) from the .linux.tar.gz that
## BuildYoctoProject.sh produces. Write the result to a card with dd; see
## docs/how-to/sd_card_imaging.rst.
##
## Run this as a NORMAL USER, and do not run it with sudo. Only the steps that
## actually need root (losetup, mkfs, mount, the extract into the mounted
## partition, umount) are individually sudo'd; the image file, its partition
## table and the temporary mount point are created as the invoking user, so the
## finished .img is owned by that user. Running the whole script under sudo
## instead leaves a root-owned image. The sudo timestamp is refreshed up front
## so the password prompt appears immediately rather than minutes into the run,
## behind the dd.
##
## Partition layout matches the manual fdisk procedure in the how-to:
##   p1  1 GiB  FAT32, label 'boot', bootable flag set -- holds BOOT.BIN,
##              image.ub, boot.scr and system.bit
##   p2  rest   ext4, label 'root'  -- the application partition
##
## p2 is only as large as the image, which is much smaller than a typical card.
## Grow it after writing the card to reclaim the remaining space (see the
## how-to); or pass a larger -s here at the cost of a much slower dd.
##############################################################################

set -euo pipefail

# Partition sizes in MiB. The defaults give the 1 GiB boot + application
# partition layout above while keeping the image small enough to dd quickly.
size_mb=2048
boot_size_mb=1024
bs=512

# Assigned before the cleanup trap is armed so 'set -u' cannot trip inside it.
# Each is set only once this run actually owns the resource, and cleared again
# once the resource is released, which keeps cleanup from double-unwinding.
loopdev=
img_boot_mount=
img_created=

function show_help {
   echo "USAGE: $0 [-s SIZE_MB] [-b BOOT_MB] [-H] IMAGE_FILE LINUX_TARBALL"
   echo " IMAGE_FILE     - Output image file to create; must not already exist"
   echo " LINUX_TARBALL  - <full-name>.linux.tar.gz from BuildYoctoProject.sh"
   echo " -s SIZE_MB     - Total image size in MiB (default $size_mb)"
   echo " -b BOOT_MB     - Boot partition size in MiB (default $boot_size_mb)."
   echo "                  Setting it equal to -s creates a single boot"
   echo "                  partition and no application partition."
   echo " -H             - Show this help text"
   exit 1
}

function die {
    echo "$@" >&2
    exit 1
}

function cleanup {
    status=$?
    # Best effort unwind of whatever this run got as far as setting up, so a
    # failure cannot leave a mounted loop partition or an attached loop device
    # behind.
    if [ -n "$img_boot_mount" ]; then
        if mountpoint -q "$img_boot_mount"; then
            sudo umount "$img_boot_mount" || true
        fi
        rmdir "$img_boot_mount" 2>/dev/null || true
    fi
    if [ -n "$loopdev" ]; then
        sudo losetup -d "$loopdev" 2>/dev/null || true
    fi
    # A half-built image cannot boot, and leaving it in place would trip the
    # 'already exists' check on the next attempt and force a manual rm. Only
    # ever set once this run created the file itself, so this cannot discard
    # anything pre-existing.
    if [ "$status" -ne 0 ] && [ -n "$img_created" ]; then
        echo "Discarding incomplete image $img_created" >&2
        rm -f "$img_created"
    fi
}
trap cleanup EXIT

while getopts s:b:H flag
do
   case $flag in
      s) size_mb=$OPTARG ;;
      b) boot_size_mb=$OPTARG ;;
      H) show_help ;;
      *) show_help ;;
   esac
done
shift $((OPTIND - 1))

[ $# -eq 2 ] || show_help

# realpath -m so the output path resolves even though it does not exist yet
img=$(realpath -m "$1")
archive_file=$(realpath -m "$2")
img_dir=$(dirname "$img")

##############################################################################
# Validate everything before creating files or asking for a password, so a
# typo costs nothing.
##############################################################################

case $size_mb in ''|*[!0-9]*) die "-s must be a whole number of MiB, got '$size_mb'" ;; esac
case $boot_size_mb in ''|*[!0-9]*) die "-b must be a whole number of MiB, got '$boot_size_mb'" ;; esac
[ "$boot_size_mb" -gt 0 ] || die "-b must be greater than zero"
[ "$boot_size_mb" -le "$size_mb" ] || die "Boot partition (-b $boot_size_mb MiB) cannot be larger than the image (-s $size_mb MiB)"

# Make sure to not accidentally overwrite an existing file
[ ! -e "$img" ] || die "Image file $img already exists. Aborting..."
[ -d "$img_dir" ] || die "Directory $img_dir does not exist"
[ -w "$img_dir" ] || die "Directory $img_dir is not writable by $(id -un)"
[ -f "$archive_file" ] || die "Linux tarball $archive_file does not exist"

# Come up with some temporary mount point, and make sure the name is not taken
mount_point="$img".boot.mount
[ ! -e "$mount_point" ] || die "The path to be used as a mount point ($mount_point) already exists. Cannot mount and copy files to image. Aborting..."

##############################################################################
# Refresh the sudo timestamp before the long-running dd. The first privileged
# step is losetup, well below, and a password prompt surfacing there is easy
# to walk away from and miss.
##############################################################################

sudo -v || die "sudo authentication failed"

echo "Preparing image file of $size_mb MiB ($boot_size_mb MiB of which is the boot partition) with block size $bs"

# Prepare empty image file
size=$((size_mb * 1024 * 1024))  # in bytes
count=$((size / bs))
dd if=/dev/zero of="$img" bs=$bs count=$count
img_created=$img

# Partition the file
if [ "$boot_size_mb" -lt "$size_mb" ]
then
    echo "Boot partition size is smaller than image size, creating a partition in the remaining space"
    printf 'n\n\n\n\n+%sM\na\nn\n\n\n\n\np\nw\n' "$boot_size_mb" | fdisk "$img"
else
    echo "Boot partition size is image size, only creating a single partition"
    printf 'n\n\n\n\n\na\np\nw\n' | fdisk "$img"
fi

# Setup loopdevice and get its name
loopdev=$(sudo losetup -f)
[ -n "$loopdev" ] || die "Could not find a free loop device"
sudo losetup -P -b $bs "$loopdev" "$img"
[ -e "$loopdev"p1 ] || die "The kernel did not expose ${loopdev}p1; the partition table written to $img is not what was expected"

# Make filesystems
echo "Format boot partition as fat32"
sudo mkfs.vfat -F 32 -n boot "$loopdev"p1
if [ "$boot_size_mb" -lt "$size_mb" ]
then
    echo "Format the the space which is not the boot partition as ext4"
    sudo mkfs.ext4 -L root "$loopdev"p2
fi

# Mount the boot partition. Verifying the mount took is what keeps the extract
# below from unpacking onto the host filesystem instead of into the image.
mkdir "$mount_point"
img_boot_mount=$mount_point
sudo mount "$loopdev"p1 "$img_boot_mount"
mountpoint -q "$img_boot_mount" || die "Failed to mount ${loopdev}p1 on $img_boot_mount"

# Option 1: Copy over files. $2 must point to directory where linux put the images
# images_dir=$2
# sudo cp $images_dir/linux/system.bit $img_boot_mount/.
# sudo cp $images_dir/linux/BOOT.BIN   $img_boot_mount/.
# sudo cp $images_dir/linux/image.ub   $img_boot_mount/.
# sudo cp $images_dir/linux/boot.scr   $img_boot_mount/.

# Option 2: Directly process the tar.gz produced by BuildYoctoProject.sh
echo "Extracting linux files to image"
sudo tar xfo "$archive_file" --directory="$img_boot_mount"/.
# The files end up in a folder, we want them outside that folder
[ -d "$img_boot_mount/linux" ] || die "$archive_file does not contain the expected linux/ directory"
sudo mv "$img_boot_mount"/linux/* "$img_boot_mount"/.
sudo rm -r "$img_boot_mount"/linux
# Make sure changes are written
sudo sync "$img_boot_mount"/

echo "Cleaning up..."
# Unmount the boot partition
sudo umount "$img_boot_mount"
# Remove the mount directory
rmdir "$img_boot_mount"
img_boot_mount=
# Close loopdevice
sudo losetup -d "$loopdev"
loopdev=

echo "Done. Wrote $img"
echo "Write it to an SD card with:"
echo "  sudo dd if=$img of=/dev/sdX bs=1M status=progress conv=fsync"
