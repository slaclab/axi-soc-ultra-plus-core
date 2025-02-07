#!/bin/sh
# First argument is output image file
# Second argument points to file produced by CreatePetalinuxProject.sh
# Use as ./CreateDiskImage.sh image.img <FileFromYourBuild>.petalinux.tar
img=$(realpath $1)

# Make sure to not accidentally overwrite an existing file
if [ -f $img ]; then
    echo "Image file already exists. Aborting..."
    exit
fi

# Set image size
# Copying over the image will take longer for a larger image (even if it is
# empty), so keep it reasonably small.
# Only boot partition should be sufficient, so sizing the boot partition size
# to be the image size should be fine.
size_mb=512 # in megabytes
boot_size_mb=512 # in megabytes
bs=512
size=$((size_mb * 1000000))  # in bytes
count=$(($size / $bs))

echo "Preparing image file of $size_mb mb ($boot_size_mb mb of which is the boot partition) with block size $bs"

# Prepare empty image file
dd if=/dev/zero of=$img bs=$bs count=$count

# Partition the file
if [ $boot_size_mb -lt $size_mb ]
then
    echo "Boot partition size is smaller than image size, creating a partition in the remaining space"
    printf 'n\n\n\n\n+'$boot_size_mb'M\na\nn\n\n\n\n\np\nw\n' | fdisk $img
else
    echo "Boot partition size is image size, only creating a single partition"
    printf 'n\n\n\n\n\na\np\nw\n' | fdisk $img
fi

# Setup loopdevice and get its name
loopdev=$(sudo losetup -f)
sudo losetup -P -b $bs $loopdev $img

# Make filesystems
echo "Format boot partition as fat32"
sudo mkfs.vfat -F 32 -n boot "$loopdev"p1
if [ $boot_size_mb -lt $size_mb ]
then
    echo "Format the the space which is not the boot partition as ext4"
    sudo mkfs.ext4 -L root "$loopdev"p2
fi

# Come up with some temporary mount point
img_boot_mount="$img".boot.mount

# Make sure the file/directory name is not yet taken
if [ -f $img_boot_mount ]; then
    echo "The directory to be used as a mount point ($img_boot_mount) already exists. Cannot mount and copy files to image. Aborting..."
    exit
fi
# Mount the boot partition
mkdir $img_boot_mount
sudo mount "$loopdev"p1 $img_boot_mount

# Option 1: Copy over files. $2 must point to directory where petalinux put the images
# images_dir=$2
# sudo cp $images_dir/linux/system.bit $img_boot_mount/.
# sudo cp $images_dir/linux/BOOT.BIN   $img_boot_mount/.
# sudo cp $images_dir/linux/image.ub   $img_boot_mount/.
# sudo cp $images_dir/linux/boot.scr   $img_boot_mount/.

# Option 2: Directly process the tar.gz produced by CreatePetalinuxProject.sh
archive_file=$2
echo "Extracting petalinux files to image"
sudo tar xfo $archive_file --directory=$img_boot_mount/.
# The files end up in a folder, we want them outside that folder
sudo mv $img_boot_mount/linux/* $img_boot_mount/.
sudo rm -r $img_boot_mount/linux
# Make sure changes are written
sudo sync $img_boot_mount/

echo "Cleaning up..."
# Unmount the boot partition
sudo umount $img_boot_mount
# Remove the mount directory
rm -r $img_boot_mount

# Close loopdevice
sudo losetup -d $loopdev
echo "Done"
