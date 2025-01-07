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

# Set default values
rfdc=1

while getopts p:n:h:x:l:d:t:r:s:f: flag
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
        f) rfdc=${OPTARG};;
    esac
done

# Check if the version is 2022 or newer
UBUNTU_VERSION=$(lsb_release -rs | cut -d'.' -f1)
if [[ "$UBUNTU_VERSION" -lt 22 ]]; then
   echo "Error: This script requires Ubuntu 2022 or newer."
   exit 1
fi

# Check the petalinux version
EXPECTED_VERSION="2024.2"
if ! awk -v current="$PETALINUX_VER" -v expected="$EXPECTED_VERSION" \
   'BEGIN {exit !(current == expected)}'; then
   echo "Error: PETALINUX_VER is not set to $EXPECTED_VERSION"
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
echo "Include RFDC utility: $rfdc";
echo "$axi_soc_ultra_plus_core"
echo "$aes_stream_drivers"

##############################################################################

# Remove existing project if it already exists
cd $path
rm -rf $name

# Create the project
petalinux-create project --template zynqMP --name $name
cd $name

# Increase QSPI image.ub size to 128MB
echo CONFIG_SUBSYSTEM_UBOOT_QSPI_FIT_IMAGE_OFFSET=0x4000000 >> project-spec/configs/config
echo CONFIG_SUBSYSTEM_UBOOT_QSPI_FIT_IMAGE_SIZE=0x8000000  >> project-spec/configs/config

# Importing Hardware Configuration
petalinux-config --silentconfig --get-hw-description $xsa

# Check if the hardware has custom u-boot
if [ -f "$hwDir/petalinux/u-boot/platform-top.h" ]
then
   cp -rf $hwDir/petalinux/u-boot/platform-top.h project-spec/meta-user/recipes-bsp/u-boot/files/platform-top.h
fi

# Customize your user device tree
cp -f $hwDir/petalinux/device-tree/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
if [ -f "$hwDir/petalinux/device-tree/device-tree.bbappend" ]
then
   # Add new configuration
   cat $hwDir/petalinux/device-tree/device-tree.bbappend >> project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend
fi

# Check if the hardware has custom petalinuxbsp configuration
if [ -f "$hwDir/petalinux/petalinuxbsp.conf" ]
then
   # Add new configuration
   cat $hwDir/petalinux/petalinuxbsp.conf >> project-spec/meta-user/conf/petalinuxbsp.conf
fi

# Check if the hardware has custom local.conf
if [ -f "$hwDir/petalinux/local.conf" ]
then
   # Add new configuration
   cat $hwDir/petalinux/local.conf >> build/conf/local.conf
fi

# Check if the dts directory exists
if [ -d "$hwDir/petalinux/dts_dir" ]
then
   cp -rf $hwDir/petalinux/dts_dir project-spec/.
fi

# Check if the hardware has custom configuration
if [ -f "$hwDir/petalinux/config" ]
then
   # Add new configuration
   cat $hwDir/petalinux/config >> project-spec/configs/config
fi

# Check if the patch directory exists
if [ -d "$hwDir/petalinux/patch" ]
then
   # Add the patches to the petalinux project
   for filename in $(ls -p $hwDir/petalinux/patch); do
      echo SRC_URI:append = \" file://$filename\" >> project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend
      cp -f $hwDir/petalinux/patch/$filename project-spec/meta-user/recipes-kernel/linux/linux-xlnx/.
   done
fi

##############################################################################

# Re-configure before building kernel
petalinux-config --silentconfig

# Build kernel
petalinux-build -c kernel

##############################################################################

# Add the axi-stream-dma & axi_memory_map kernel modules
petalinux-create modules --name axistreamdma
petalinux-create modules --name aximemorymap
rm -rf project-spec/meta-user/recipes-modules/axistreamdma
rm -rf project-spec/meta-user/recipes-modules/aximemorymap
cp -rfL $aes_stream_drivers/petalinux/axistreamdma project-spec/meta-user/recipes-modules/axistreamdma
cp -rfL $aes_stream_drivers/petalinux/aximemorymap project-spec/meta-user/recipes-modules/aximemorymap
echo IMAGE_INSTALL:append = \" axistreamdma aximemorymap\" >> build/conf/local.conf

# Update DMA engine with user configuration
sed -i "s/int cfgTxCount0 = 128;/int cfgTxCount0 = $dmaTxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgRxCount0 = 128;/int cfgRxCount0 = $dmaRxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgSize0    = 2097152;/int cfgSize0    = $dmaBuffSize;/" project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c

##############################################################################

# Add rogue to petalinux
petalinux-create apps --name rogue --template install
cp -f $axi_soc_ultra_plus_core/petalinux-apps/rogue.bb project-spec/meta-user/recipes-apps/rogue/rogue.bb
echo CONFIG_rogue=y >> project-spec/configs/rootfs_config
echo CONFIG_rogue-dev=y >> project-spec/configs/rootfs_config

##############################################################################

# Add rogue TCP memory/stream server application
petalinux-create apps --template install -n roguetcpbridge
echo CONFIG_roguetcpbridge=y >> project-spec/configs/rootfs_config
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/roguetcpbridge project-spec/meta-user/recipes-apps/.
echo IMAGE_INSTALL:append = \" roguetcpbridge\" >> build/conf/local.conf

# Update Application with user configuration
sed -i "s/default  = 2,/default  = $numLane,/"  project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge
sed -i "s/default  = 32,/default  = $numDest,/" project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge

##############################################################################

# Add rogue AxiVersion Dump application
petalinux-create apps --template install -n axiversiondump
echo CONFIG_axiversiondump=y >> project-spec/configs/rootfs_config
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/axiversiondump project-spec/meta-user/recipes-apps/.
echo IMAGE_INSTALL:append = \" axiversiondump\" >> build/conf/local.conf

##############################################################################

# Check if including RFDC utility
if [ "$rfdc" -eq 1 ]
then
    # Add RFDC selftest application
    petalinux-create apps --template install -n rfdc-test
    echo CONFIG_rfdc-test=y >> project-spec/configs/rootfs_config
    cp -rf $axi_soc_ultra_plus_core/petalinux-apps/rfdc-test project-spec/meta-user/recipes-apps/.
    echo IMAGE_INSTALL:append = \" rfdc-test\" >> build/conf/local.conf
fi

##############################################################################

# Add startup application script (loads the user's FPGA .bit file, loads the kernel drivers then kicks off the rogue TCP bridge)
petalinux-create apps --template install -n startup-app-init --enable
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/startup-app-init project-spec/meta-user/recipes-apps/.

##############################################################################

# Add commonly used packages
echo CONFIG_imagefeature-debug-tweaks=y >> project-spec/configs/rootfs_config
echo CONFIG_peekpoke=y >> project-spec/configs/rootfs_config

echo CONFIG_nano=y >> project-spec/configs/rootfs_config
echo CONFIG_nano   >> project-spec/configs/rootfsconfigs/user-rootfsconfig
echo CONFIG_nano   >> project-spec/meta-user/conf/user-rootfsconfig

echo CONFIG_htop=y >> project-spec/configs/rootfs_config
echo CONFIG_htop   >> project-spec/configs/rootfsconfigs/user-rootfsconfig
echo CONFIG_htop   >> project-spec/meta-user/conf/user-rootfsconfig

##############################################################################

# Finalize the System Image
petalinux-build

# Create boot files
petalinux-package boot --uboot --fpga --force

##############################################################################

# Default file list
fileList="linux/system.bit linux/BOOT.BIN linux/image.ub linux/boot.scr"

if [[ -v SOC_IP_STATIC ]]; then
   # File list with static IP
   echo $SOC_IP_STATIC>>$path/$name/images/linux/ip
   fileList="$fileList linux/ip"
fi

# Dump a compressed tarball of all the required build output files
cd $path/$name/images/ && tar -czf $imageDump $fileList

echo "########################################################################"
echo "Release File List: $fileList"
echo "########################################################################"
echo "petalinux.tar.gz image path: $imageDump"
echo "########################################################################"

##############################################################################
