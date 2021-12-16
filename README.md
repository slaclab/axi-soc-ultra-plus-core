# axi-soc-ultra-plus-core

<!--- ######################################################## -->

### How to format SD card for SD boot
https://xilinx-wiki.atlassian.net/wiki/x/EYMfAQ

<!--- ######################################################## -->

### Load the bitstream and kernel via JTAG

```bash
# Go to petalinux project directory
$ cd <MY_PROJECT>

# Execute the command
$ petalinux-boot --jtag --kernel --fpga
```

Note: Make sure you power cycle the board before JTAG boot

<!--- ######################################################## -->