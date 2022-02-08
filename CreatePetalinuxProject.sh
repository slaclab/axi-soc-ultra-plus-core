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
echo "Build Output Path: $path";
echo "Project Name: $name";
echo "Hardware Type: $hwType";
echo "XSA File Path: $xsa";
echo "Number of DMA lanes: $numLane";
echo "Number of DEST per lane: $numDest";
echo "Number of DMA TX Buffers: $dmaTxBuffCount";
echo "Number of DMA RX Buffers: $dmaRxBuffCount";
echo "DMA Buffer Size: $dmaBuffSize Bytes";

axi_soc_ultra_plus_core=$(dirname $(readlink -f $0))
aes_stream_drivers=$(realpath $axi_soc_ultra_plus_core/../aes-stream-drivers)
hwDir=$axi_soc_ultra_plus_core/hardware/$hwType

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

# Check if the patch directory exists
if [ -d "$hwDir/patch" ]
then
   # Add the patches to the petalinux project
   for filename in $(ls -p $hwDir/patch); do
      echo SRC_URI_append = \" file://$filename\" >> project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend
      cp -f $hwDir/patch/$filename project-spec/meta-user/recipes-kernel/linux/linux-xlnx/.
   done
fi

# Customize your user device tree
cp -f $hwDir/device-tree/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi

# Add the axi-stream-dma & axi_memory_map modules
petalinux-create -t modules --name axistreamdma
petalinux-create -t modules --name aximemorymap
rm -rf project-spec/meta-user/recipes-modules/axistreamdma
rm -rf project-spec/meta-user/recipes-modules/aximemorymap
cp -rfL $aes_stream_drivers/petalinux/axistreamdma project-spec/meta-user/recipes-modules/axistreamdma
cp -rfL $aes_stream_drivers/petalinux/aximemorymap project-spec/meta-user/recipes-modules/aximemorymap
echo KERNEL_MODULE_AUTOLOAD = \"axi_stream_dma axi_memory_map\" >> project-spec/meta-user/conf/petalinuxbsp.conf
echo IMAGE_INSTALL_append = \" axistreamdma aximemorymap\" >> build/conf/local.conf

# Update DMA engine with user configuration
sed -i "s/int cfgTxCount0 = 128;/int cfgTxCount0 = $dmaTxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgRxCount0 = 128;/int cfgRxCount0 = $dmaRxBuffCount;/"  project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgSize0    = 2097152;/int cfgSize0    = $dmaBuffSize;/" project-spec/meta-user/recipes-modules/axistreamdma/files/axistreamdma.c

# Build kernel and kernel modules
petalinux-build -c kernel
petalinux-build -c axistreamdma
petalinux-build -c aximemorymap

# Add rogue to petalinux
petalinux-create -t apps --name rogue --template install
cp -f $axi_soc_ultra_plus_core/petalinux-apps/rogue.bb project-spec/meta-user/recipes-apps/rogue/rogue.bb
echo CONFIG_peekpoke=y >> project-spec/configs/rootfs_config
echo CONFIG_rogue=y >> project-spec/configs/rootfs_config
echo CONFIG_rogue-dev=y >> project-spec/configs/rootfs_config
petalinux-build -c rogue

# Known bug in rogue where we need to copy the setup.py and re-run the rogue builds again
# Issue is documented here: https://jira.slac.stanford.edu/browse/ESROGUE-523
cp build/tmp/work/cortexa72-cortexa53-xilinx-linux/rogue/1.0-r0/build/setup.py build/tmp/work/cortexa72-cortexa53-xilinx-linux/rogue/1.0-r0/rogue-*/.
petalinux-build -c rogue

# Add rogue TCP memory/stream server
petalinux-create -t apps --template install -n roguetcpbridge
echo CONFIG_roguetcpbridge=y >> project-spec/configs/rootfs_config
echo IMAGE_INSTALL_append = \" roguetcpbridge\" >> build/conf/local.conf
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/roguetcpbridge project-spec/meta-user/recipes-apps/.

# Update Application with user configuration
sed -i "s/default  = 2,/default  = $numLane,/"  project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge
sed -i "s/default  = 32,/default  = $numDest,/" project-spec/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge

# Add startup application script (loads the user's FPGA .bit file, loads the kernel drivers then kicks off the rogue TCP bridge)
petalinux-create -t apps --template install -n startupapp --enable
echo CONFIG_startupapp=y >> project-spec/configs/rootfs_config
echo IMAGE_INSTALL_append = \" startupapp\" >> build/conf/local.conf
cp -rf $axi_soc_ultra_plus_core/petalinux-apps/startupapp project-spec/meta-user/recipes-apps/.

# Build the applications
petalinux-build -c roguetcpbridge
petalinux-build -c startupapp

# Patch for supporting JTAG booting
petalinux-config --silentconfig
echo CONFIG_python3-logging=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-numpy=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-json=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-pyzmq=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-sqlalchemy=y >> project-spec/configs/rootfs_config
echo CONFIG_python3-pyyaml=y >> project-spec/configs/rootfs_config
petalinux-build

# Finalize the System Image
petalinux-build

# Create boot files
petalinux-package --boot --uboot --fpga --force
