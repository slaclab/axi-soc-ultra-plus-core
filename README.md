# axi-soc-ultra-plus-core

<!--- ######################################################## -->

### How to format SD card for SD boot

https://xilinx-wiki.atlassian.net/wiki/x/EYMfAQ

1) Copy For the boot images, simply copy the files to the FAT partition.  
This typically will include BOOT.BIN, image.ub, and boot.scr

```bash
sudo mount /dev/sdd1 /u1/boot
sudo cp /u1/ruckman/build/petalinux/SpaceRfSocXilinxZcu208DevBoard/images/linux/BOOT.BIN /u1/boot/.
sudo cp /u1/ruckman/build/petalinux/SpaceRfSocXilinxZcu208DevBoard/images/linux/image.ub /u1/boot/.
sudo cp /u1/ruckman/build/petalinux/SpaceRfSocXilinxZcu208DevBoard/images/linux/boot.scr /u1/boot/.
sudo umount /u1/boot
```

2) For the root file system, the process will depend on the format of your root file system image. 

`roofts.ext4 -  This is an uncompressed ext4 file system image. To copy the contents to the root partition, you can use the following command: `

```bash
sudo dd if=/u1/ruckman/build/petalinux/SpaceRfSocXilinxZcu208DevBoard/images/linux/rootfs.ext4 of=/dev/sdd2
```

<!--- ######################################################## -->

### Load the bitstream and kernel via JTAG

```bash
# Go to petalinux project directory
cd <MY_PROJECT>

# Execute the command
petalinux-boot --jtag --kernel --fpga
```

Note: Make sure you power cycle the board before JTAG boot

<!--- ######################################################## -->