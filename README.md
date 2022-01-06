# axi-soc-ultra-plus-core

<!--- ######################################################## -->

### Zynq UltraScale+ Devices Register Reference

https://www.xilinx.com/html_docs/registers/ug1087/ug1087-zynq-ultrascale-registers.html

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

### Change the Boot Mode of the Xilinx Zynq UltraScale+ MPSoC from XSCT

https://www.zachpfeffer.com/single-post/change-the-boot-mode-of-the-xilinx-zynq-ultrascale-mpsoc-from-xsct


#### following sequence changes to JTAG boot mode
```bash
xsct
connect
targets -set -nocase -filter {name =~ "*PSU*"}
stop
mwr  0xff5e0200 0x0100
rst -system
disconnect
```

#### following sequence changes to NAND mode
```bash
xsct
connect
targets -set -nocase -filter {name =~ "*PSU*"}
stop
mwr  0xff5e0200 0x4100
rst -system
con
disconnect
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


### How to create a .BSP file (Board Support File)

```bash
# Go to petalinux project directory
cd <MY_PROJECT>

# Execute the command
petalinux-package --bsp -p SpaceRfSocXilinxZcu208DevBoard -o SpaceRfSocXilinxZcu208DevBoard.bsp
```

Note: Make sure you power cycle the board before JTAG boot


<!--- ######################################################## -->


### How to Program the NAND flash

```bash
# Go to petalinux project directory
cd <MY_PROJECT>

# Define default parameters
default_parameter="\
-flash_type nand-x8-single \
-fsbl images/linux/zynqmp_fsbl.elf \
-verify -cable type xilinx_tcf url TCP:127.0.0.1:3121"

# Execute the commands
program_flash -f images/linux/BOOT.BIN -offset 0x0000000 $default_parameter
program_flash -f images/linux/boot.scr -offset 0x3E80000 $default_parameter
program_flash -f images/linux/image.ub -offset 0x4180000 $default_parameter

<!--- ######################################################## -->

### How to force PS_ERROR_OUT for testing only

This procedure will force EM_ERR_ID_CSU_ROM=0x1, which will trigger PS_ERROR_OUT. 
EM_ERR_ID_CSU_ROM is BIT0 of pmuErrorToPl[46:0] bus (A.K.A. "JTAG Error Register")
Refer to "JTAG Error Register" on pg 138 of Zynq UltraScale+ Device TRM UG1085 (v2.2).

```bash
xsct
connect
targets -set -nocase -filter {name =~ "*PSU*"}
mwr -force 0x00FFD80528  1
disconnect
```

<!--- ######################################################## -->
