#!/bin/sh
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################

while getopts p:n:h:x:j: flag
do
    case "${flag}" in
        p) path=${OPTARG};;
        n) name=${OPTARG};;
        h) hwType=${OPTARG};;
        x) xsa=${OPTARG};;
        j) jtag=${OPTARG};;
    esac
done
echo "Build Output Path: $path";
echo "Project Name: $name";
echo "Hardware Type: $hwType";
echo "XSA File Path: $xsa";

axi_soc_ultra_plus_core=$(dirname $(readlink -f $0))
aes_stream_drivers=$(realpath $axi_soc_ultra_plus_core/../aes-stream-drivers)

echo "$axi_soc_ultra_plus_core"
echo "$aes_stream_drivers"

# Remove existing project if it already exists
cd $path
rm -rf $name

# Create the project
petalinux-create --type project --template zynqMP --name $name
cd $name

# Importing Hardware Configuration
petalinux-config --silentconfig --get-hw-description $xsa

# Customize your user device tree
cp -rf $axi_soc_ultra_plus_core/hardware/$hwType/device-tree/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi

# Add the axi-stream-dma & axi_memory_map modules
petalinux-create -t modules --name axistreamdma --enable
petalinux-create -t modules --name aximemorymap --enable
rm -rf project-spec/meta-user/recipes-modules/axistreamdma
rm -rf project-spec/meta-user/recipes-modules/aximemorymap
cp -rfL $aes_stream_drivers/petalinux/axistreamdma project-spec/meta-user/recipes-modules/axistreamdma
cp -rfL $aes_stream_drivers/petalinux/aximemorymap project-spec/meta-user/recipes-modules/aximemorymap
echo KERNEL_MODULE_AUTOLOAD = \"axi_stream_dma axi_memory_map\" >> project-spec/meta-user/conf/petalinuxbsp.conf
echo IMAGE_INSTALL_append = \" axistreamdma aximemorymap\" >> build/conf/local.conf
petalinux-build -c kernel
petalinux-build -c axistreamdma
petalinux-build -c aximemorymap

# Add rogue to petalinux
petalinux-create -t apps --name rogue --template install
cp -f $axi_soc_ultra_plus_core/rogue.bb project-spec/meta-user/recipes-apps/rogue/rogue.bb
echo CONFIG_rogue=y >> project-spec/configs/rootfs_config
echo CONFIG_rogue-dev=y >> project-spec/configs/rootfs_config
petalinux-build -c rogue
cp build/tmp/work/cortexa72-cortexa53-xilinx-linux/rogue/1.0-r0/build/setup.py build/tmp/work/cortexa72-cortexa53-xilinx-linux/rogue/1.0-r0/rogue-*/.
petalinux-build -c rogue

# Add rogue TCP memory/server server
petalinux-create -t apps --template install -n roguetcpbridge --enable
echo CONFIG_roguetcpbridge=y >> project-spec/configs/rootfs_config
echo IMAGE_INSTALL_append = \" roguetcpbridge\" >> build/conf/local.conf
cp -rf $axi_soc_ultra_plus_core/roguetcpbridge project-spec/meta-user/recipes-apps/.
petalinux-build -c roguetcpbridge

# Check for JTAG booting
if [ $jtag != "0" ]
then
   echo "Enable jtag Boot Build: $jtag";
   # Patch for JTAG booting
   petalinux-config --silentconfig
   echo CONFIG_python3-logging=y >> project-spec/configs/rootfs_config
   echo CONFIG_python3-numpy=y >> project-spec/configs/rootfs_config
   echo CONFIG_python3-json=y >> project-spec/configs/rootfs_config
   echo CONFIG_python3-pyzmq=y >> project-spec/configs/rootfs_config
   echo CONFIG_python3-sqlalchemy=y >> project-spec/configs/rootfs_config
   echo CONFIG_python3-pyyaml=y >> project-spec/configs/rootfs_config
   petalinux-build
fi

# Finalize the System Image
petalinux-build

# Create boot files
petalinux-package --boot --uboot --fpga --force
