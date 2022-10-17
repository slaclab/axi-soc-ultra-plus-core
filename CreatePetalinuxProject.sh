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

while getopts p:n:h:x:l:d:t:r:s: flag
do
    case "${flag}" in
        p) path=${OPTARG};;
        n) name=${OPTARG};;
        h) hwType=${OPTARG};;
        x) xsa=${OPTARG};;
        l) numLane=${OPTARG};;
        d) numDest=${OPTARG};;
        t) dmaTxBuffCount=${OPTARG};;
        r) dmaRxBuffCount=${OPTARG};;
        s) dmaBuffSize=${OPTARG};;
    esac
done

# Check the petalinux version
if awk "BEGIN {exit !($PETALINUX_VER < 2022.1)}"; then
   echo "PETALINUX_VER must be >= 2022.1"
   exit 1
fi

##############################################################################

axi_soc_ultra_plus_core=$(dirname $(readlink -f $0))
aes_stream_drivers=$(realpath $axi_soc_ultra_plus_core/../aes-stream-drivers)
hwDir=$axi_soc_ultra_plus_core/hardware/$hwType
imageDump=${xsa%.*}.petalinux.tar.gz

# Check if the dts directory exists
if [ ! -d "$hwDir" ]
then
   echo "hwDir=$hwDir does NOT exist"
   exit 1
fi

echo "Build Output Path: $path";
echo "Project Name: $name";
echo "Hardware Type: $hwType";
echo "XSA File Path: $xsa";
echo "Image File Path: $imageDump";
echo "Number of DMA lanes: $numLane";
echo "Number of DEST per lane: $numDest";
echo "Number of DMA TX Buffers: $dmaTxBuffCount";
echo "Number of DMA RX Buffers: $dmaRxBuffCount";
echo "DMA Buffer Size: $dmaBuffSize Bytes";
echo "$axi_soc_ultra_plus_core"
echo "$aes_stream_drivers"

##############################################################################

# Remove existing project if it already exists
cd $path
rm -rf $name

# Create the project
petalinux-create --type project --template zynqMP --name $name
cd $name

# Importing Hardware Configuration
petalinux-config --silentconfig --get-hw-description $xsa

# Check if the dts directory exists
if [ -d "$hwDir/dts_dir" ]
then
   cp -rf $hwDir/dts_dir project-spec/.
fi

# Check if the hardware has custom u-boot
if [ -f "$hwDir/u-boot/platform-top.h" ]
then
   cp -rf $hwDir/u-boot/platform-top.h project-spec/meta-user/recipes-bsp/u-boot/files/platform-top.h
fi

# Customize your user device tree
cp -f $hwDir/device-tree/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
if [ -f "$hwDir/device-tree/device-tree.bbappend" ]
then
   # Add new configuration
   cat $hwDir/device-tree/device-tree.bbappend >> project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend
fi

# Check if the hardware has custom configuration
if [ -f "$hwDir/config" ]
then
   # Add new configuration
   cat $hwDir/config >> project-spec/configs/config
   # Reload the configurations
   petalinux-config --silentconfig
fi

##############################################################################

# Check if the patch directory exists
if [ -d "$hwDir/patch" ]
then
   # Add the patches to the petalinux project
   for filename in $(ls -p $hwDir/patch); do
      echo SRC_URI_append = \" file://$filename\" >> project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend
      cp -f $hwDir/patch/$filename project-spec/meta-user/recipes-kernel/linux/linux-xlnx/.
   done
fi

# Build kernel
petalinux-build -c kernel

##############################################################################

# Add the axi-stream-dma & axi_memory_map kernel modules
petalinux-create -t modules --name axistreamdma
petalinux-create -t modules --name aximemorymap
rm -rf project-spec/meta-user/recipes-modules/axistreamdma
rm -rf project-spec/meta-user/recipes-modules/aximemorymap
cp -rfL $aes_stream_drivers/petalinux/axistreamdma project-spec/meta-user/recipes-modules/axistreamdma
cp -rfL $aes_stream_drivers/petalinux/aximemorymap project-spec/meta-user/recipes-modules/aximemorymap
echo IMAGE_INSTALL:append = \" axistreamdma aximemorymap\" >> build/conf/local.conf

# Update DMA engine with user configuration
sed -i "s/int cfgTxCount0 = 128;/int cfgTxCount0 = $dmaTxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgRxCount0 = 128;/int cfgRxCount0 = $dmaRxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgSize0    = 2097152;/int cfgSize0    = $dmaBuffSize;/" project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c

# Build kernel modules
petalinux-build -c axistreamdma
petalinux-build -c aximemorymap

##############################################################################

# Add rogue to petalinux
petalinux-create -t apps --name rogue --template install
cp -f $axi_soc_ultra_plus_core/petalinux-apps/rogue.bb project-spec/meta-user/recipes-apps/rogue/rogue.bb
echo CONFIG_rogue=y >> project-spec/configs/rootfs_config
echo CONFIG_rogue-dev=y >> project-spec/configs/rootfs_config

# Build the application
petalinux-build -c rogue

##############################################################################

# Add rogue TCP memory/stream server application
petalinux-create -t apps --template install -n roguetcpbridge
echo CONFIG_roguetcpbridge=y >> project-spec/configs/rootfs_config
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/roguetcpbridge project-spec/meta-user/recipes-apps/.
echo IMAGE_INSTALL:append = \" roguetcpbridge\" >> build/conf/local.conf

# Update Application with user configuration
sed -i "s/default  = 2,/default  = $numLane,/"  project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge
sed -i "s/default  = 32,/default  = $numDest,/" project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge

# Build the application
petalinux-build -c roguetcpbridge

##############################################################################

# Add rogue AxiVersion Dump application
petalinux-create -t apps --template install -n axiversiondump
echo CONFIG_axiversiondump=y >> project-spec/configs/rootfs_config
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/axiversiondump project-spec/meta-user/recipes-apps/.
echo IMAGE_INSTALL:append = \" axiversiondump\" >> build/conf/local.conf

# Build the application
petalinux-build -c axiversiondump

##############################################################################

# Add startup application script (loads the user's FPGA .bit file, loads the kernel drivers then kicks off the rogue TCP bridge)
petalinux-create -t apps --template install -n startup-app-init --enable
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/startup-app-init project-spec/meta-user/recipes-apps/.
echo IMAGE_INSTALL:append = \" startup-app-init\" >> build/conf/local.conf

# Build the application
petalinux-build -c startup-app-init

##############################################################################

# Load commonly used packages
echo CONFIG_imagefeature-debug-tweaks=y >> project-spec/configs/rootfs_config
echo CONFIG_packagegroup-petalinux-jupyter=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-qtconsole=y >> project-spec/configs/rootfs_config
echo CONFIG_nano=y >> project-spec/configs/rootfs_config
echo CONFIG_htop=y >> project-spec/configs/rootfs_config
echo CONFIG_peekpoke=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-logging=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-numpy=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-json=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-pyzmq=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-sqlalchemy=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-pyyaml=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-parse=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-click=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-pyserial=y >> project-spec/configs/rootfs_config

# Check if the hardware has custom packages that need installed
if [ -f "$hwDir/rootfs_config" ]
then
   cat $hwDir/rootfs_config >> project-spec/configs/rootfs_config
fi

##############################################################################

# Finalize the System Image
petalinux-build

# Create boot files
petalinux-package --boot --uboot --fpga --force

# Dump a compressed tarball of all the required build output files
cd $path/$name/images/ && tar -czf $imageDump linux/system.bit linux/BOOT.BIN linux/image.ub linux/boot.scr
echo "########################################################################"
echo "petalinux.tar.gz image path: $imageDump"
echo "########################################################################"

##############################################################################