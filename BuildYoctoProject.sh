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

# Clear terminal output
echo -ne "\033c"

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

##############################################################################

Name=$name # unmodified copy
name="${name,,}" # lower case to prevent [uppercase-pn] warning
axi_soc_ultra_plus_core=$(dirname $(readlink -f $0))
aes_stream_drivers=$(realpath $axi_soc_ultra_plus_core/../aes-stream-drivers)
hwDir=$axi_soc_ultra_plus_core/hardware/$hwType
imageDump=${xsa%.*}.yocto.tar.gz
proj_dir=$(realpath "$path/$Name")

##############################################################################

# Check total buffer size
TOTAL_BUFFER_SIZE=$(( (dmaTxBuffCount + dmaRxBuffCount) * dmaBuffSize ))
MAX_BUFFER_SIZE=$((0x60000000)) # 1.5 GB (1610612736 bytes)
if (( TOTAL_BUFFER_SIZE > MAX_BUFFER_SIZE )); then
    HUMAN_SIZE=$(numfmt --to=iec-i --suffix=B --format="%.2f" "$TOTAL_BUFFER_SIZE")
    HEX_SIZE=$(printf "0x%X" "$TOTAL_BUFFER_SIZE")
    echo "Error: Total buffer size exceeds 1.5 GB (0x60000000)."
    echo "Current size: $HUMAN_SIZE ($HEX_SIZE)"
    exit 1
fi

##############################################################################

missing=0
for tool in git bash curl chrpath diffstat lz4c; do
    command -v "$tool" >/dev/null 2>&1 || { echo "Missing package: $tool"; missing=1; }
done
[ $missing -ne 0 ] && exit 1

##############################################################################

# Check if the directory exists
if [ ! -d "$hwDir" ]
then
   echo "hwDir=$hwDir does NOT exist"
   exit 1
fi

# Check if the Yocto zynqmp.conf file exists
if [ ! -f "$hwDir/Yocto/zynqmp.conf" ]; then
   echo "File $hwDir/Yocto/zynqmp.conf does NOT exist"
   exit 1
fi

##############################################################################

# Print these variables to help with debugging
echo "Build Output Path: $path";
echo "Project Name: $Name";
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

# Download the Repo script & Make it executable
repo_dir=$(realpath "$path/bin")
repo="$repo_dir/repo"
mkdir -p $repo_dir
if [ ! -f $repo ]
then
   curl https://storage.googleapis.com/git-repo-downloads/repo > $repo
   chmod a+x $repo
fi

# Add repo_dir to your $PATH
if [[ ":$PATH:" != *":$repo_dir:"* ]]; then
   export PATH="$repo_dir:$PATH"
fi

##############################################################################

# Remove older build and start from clean state
if [ -d $proj_dir ]
then
   echo "Remove existing project if it already exists ..."
   rm -rf $proj_dir
fi

# Create the project
mkdir $proj_dir && cd $proj_dir
yes y | repo init -u https://github.com/Xilinx/yocto-manifests.git -b rel-v2025.1
repo sync

# Xilinx environment specific Yocto setup and automation scripts
BDIR=build source setupsdk > /dev/null

##############################################################################
# Importing Hardware Configuration
##############################################################################

# Create a new layer for your custom hardware configuration
bitbake-layers create-layer $proj_dir/sources/meta-user
bitbake-layers add-layer    $proj_dir/sources/meta-user

# Base this machine configuration off of
mkdir $proj_dir/sources/meta-user/conf/machine
cp -rf $hwDir/Yocto/zynqmp.conf $proj_dir/sources/meta-user/conf/machine/$name-zynqmp.conf
echo "HDF_PATH = \"$xsa\""   >> $proj_dir/sources/meta-user/conf/machine/$name-zynqmp.conf

# Set the machine in conf/local.conf
sed -i "/MACHINE ??=/c\MACHINE ??= \"$name-zynqmp\"" $proj_dir/build/conf/local.conf

##############################################################################

# # Increase QSPI image.ub size to 128MB
# echo CONFIG_SUBSYSTEM_UBOOT_QSPI_FIT_IMAGE_OFFSET=0x4000000 >> project-spec/configs/config
# echo CONFIG_SUBSYSTEM_UBOOT_QSPI_FIT_IMAGE_SIZE=0x8000000  >> project-spec/configs/config

# # Check if the hardware has custom u-boot config
# if [ -f "$hwDir/petalinux/u-boot/bsp.cfg" ]
# then
   # cp -rf $hwDir/petalinux/u-boot/bsp.cfg project-spec/meta-user/recipes-bsp/u-boot/files/bsp.cfg
# fi

# # Check if the hardware has custom u-boot header
# if [ -f "$hwDir/petalinux/u-boot/platform-top.h" ]
# then
   # cp -rf $hwDir/petalinux/u-boot/platform-top.h project-spec/meta-user/recipes-bsp/u-boot/files/platform-top.h
# fi

# # Customize your user device tree
# cp -f $hwDir/petalinux/device-tree/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
# if [ -f "$hwDir/petalinux/device-tree/device-tree.bbappend" ]
# then
   # # Add new configuration
   # cat $hwDir/petalinux/device-tree/device-tree.bbappend >> project-spec/meta-user/recipes-bsp/device-tree/device-tree.bbappend
# fi

# # Check if the hardware has custom petalinuxbsp configuration
# if [ -f "$hwDir/petalinux/petalinuxbsp.conf" ]
# then
   # # Add new configuration
   # cat $hwDir/petalinux/petalinuxbsp.conf >> project-spec/meta-user/conf/petalinuxbsp.conf
# fi

# # Check if the dts directory exists
# if [ -d "$hwDir/petalinux/dts_dir" ]
# then
   # cp -rf $hwDir/petalinux/dts_dir project-spec/.
# fi

# # Check if the hardware has custom configuration
# if [ -f "$hwDir/petalinux/config" ]
# then
   # # Add new configuration
   # cat $hwDir/petalinux/config >> project-spec/configs/config
# fi

# # Check if the patch directory exists
# if [ -d "$hwDir/petalinux/patch" ]
# then
   # # Add the patches to the petalinux project
   # for filename in $(ls -p $hwDir/petalinux/patch); do
      # echo SRC_URI:append = \" file://$filename\" >> project-spec/meta-user/recipes-kernel/linux/linux-xlnx_%.bbappend
      # cp -f $hwDir/petalinux/patch/$filename project-spec/meta-user/recipes-kernel/linux/linux-xlnx/.
   # done
# fi

##############################################################################

# Add the axi-stream-dma & axi_memory_map kernel modules
cp -rfL $aes_stream_drivers/Yocto/recipes-kernel $proj_dir/sources/meta-user/.
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += \" axistreamdma aximemorymap\"" >> $proj_dir/sources/meta-user/conf/machine/$name-zynqmp.conf
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS += \" axistreamdma aximemorymap\"" >> $proj_dir/sources/meta-user/conf/machine/$name-zynqmp.conf

# Update DMA engine with user configuration
sed -i "s/int cfgTxCount0 = 128;/int cfgTxCount0 = $dmaTxBuffCount;/"  $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgRxCount0 = 128;/int cfgRxCount0 = $dmaRxBuffCount;/"  $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgSize0    = 2097152;/int cfgSize0    = $dmaBuffSize;/" $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c

##############################################################################

# Add axi-soc-ultra-plus-core's recipes-apps
cp -rfL $axi_soc_ultra_plus_core/shared/Yocto/recipes-apps $proj_dir/sources/meta-user/.
echo "IMAGE_INSTALL:append = \" rogue rogue-dev\""  >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" roguetcpbridge\""   >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" axiversiondump\""   >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" startup-app-init\"" >> $proj_dir/sources/meta-user/conf/layer.conf

# Check if including RFDC utility
if [ "$rfdc" -eq 1 ]
then
   echo "IMAGE_INSTALL:append = \" pyrfdc\"" >> $proj_dir/sources/meta-user/conf/layer.conf
fi

# Update Application with user configuration
sed -i "s/default  = 2,/default  = $numLane,/"  $proj_dir/sources/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge
sed -i "s/default  = 32,/default  = $numDest,/" $proj_dir/sources/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge

##############################################################################

# Add commonly used packages
echo "IMAGE_INSTALL:append = \" peekpoke\""     >> $proj_dir/build/conf/local.conf
echo "IMAGE_INSTALL:append = \" nano\""         >> $proj_dir/build/conf/local.conf
# echo "IMAGE_INSTALL:append = \" debug-tweaks\"" >> $proj_dir/build/conf/local.conf

##############################################################################

# Build Everything!
bitbake petalinux-image-minimal

# Create boot files
bitbake xilinx-bootbin

##############################################################################

# # Default file list
# fileList="linux/system.bit linux/BOOT.BIN linux/image.ub linux/boot.scr"

# if [[ -v SOC_IP_STATIC ]]; then
   # # File list with static IP
   # echo $SOC_IP_STATIC>>$path/$name/images/linux/ip
   # fileList="$fileList linux/ip"
# fi

# # Dump a compressed tarball of all the required build output files
# cd $path/$name/images/ && tar -czf $imageDump $fileList

echo "########################################################################"
echo "Release File List: $fileList"
echo "########################################################################"
echo "petalinux.tar.gz image path: $imageDump"
echo "########################################################################"

##############################################################################
