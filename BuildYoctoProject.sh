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
    esac
done

##############################################################################
# Generate commonly used local variables
##############################################################################

Name=$name # unmodified copy
name="${name,,}" # lower case to prevent [uppercase-pn] warning
axi_soc_ultra_plus_core=$(dirname $(readlink -f $0))
aes_stream_drivers=$(realpath $axi_soc_ultra_plus_core/../aes-stream-drivers)
hwDir=$axi_soc_ultra_plus_core/hardware/$hwType
imageDump=${xsa%.*}.linux.tar.gz
proj_dir=$(realpath "$path/$Name")
image_dir="$proj_dir/build/tmp/deploy/images/zynqmp-user"

##############################################################################
# Check total buffer size
##############################################################################

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
# Check for missing system packages before we start
##############################################################################

missing=0
for tool in bash curl chrpath diffstat git gzip lz4c mkimage; do
   command -v "$tool" >/dev/null 2>&1 || { echo "Missing package: $tool"; missing=1; }
done
if [ $missing -ne 0 ]; then
   echo ""
   echo "You can install the missing packages with:"
   echo "sudo apt update"
   echo "sudo apt install -y bash curl chrpath diffstat git gzip liblz4-tool u-boot-tools"
   exit 1
fi

##############################################################################
# Misc. file and dir checking
##############################################################################

# Check if the directory exists
if [ ! -d "$hwDir" ]
then
   echo "hwDir=$hwDir does NOT exist"
   exit 1
fi

# Check if the Yocto zynqmp-user.conf file exists
if [ ! -f "$hwDir/Yocto/zynqmp-user.conf" ]; then
   echo "File $hwDir/Yocto/zynqmp-user.conf does NOT exist"
   exit 1
fi

# Check if the directory exists
if [ ! -d "$hwDir/Yocto/recipes-bsp" ]
then
   echo "$hwDir/Yocto/recipes-bsp does NOT exist"
   exit 1
fi

# Check if the XSA file exists and has .xsa extension
if [ ! -f "$xsa" ] || [[ "$xsa" != *.xsa ]]; then
   echo "File $xsa does NOT exist or is not a .xsa file"
   exit 1
fi

##############################################################################
# Print these variables to help with debugging
##############################################################################

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
echo "$axi_soc_ultra_plus_core"
echo "$aes_stream_drivers"

##############################################################################
# Setup the 'repo' executable
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
# Create the Yocto project
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

# Create the conf/machine/zynqmp-user.conf
mkdir $proj_dir/sources/meta-user/conf/machine
cp -rfL $axi_soc_ultra_plus_core/shared/Yocto/zynqmp-user.conf $proj_dir/sources/meta-user/conf/machine/zynqmp-user.conf
cat $hwDir/Yocto/zynqmp-user.conf                           >> $proj_dir/sources/meta-user/conf/machine/zynqmp-user.conf
echo "HDF_PATH = \"$xsa\""                                  >> $proj_dir/sources/meta-user/conf/machine/zynqmp-user.conf

# Set the machine & hostname in conf/local.conf
sed -i "/MACHINE ??=/c\MACHINE ??= \"zynqmp-user\"" $proj_dir/build/conf/local.conf
echo "hostname:pn-base-files = \"$Name\""        >> $proj_dir/build/conf/local.conf

##############################################################################
# Add the hardware specific BSP
##############################################################################

cp -rfL $hwDir/Yocto/recipes-bsp $proj_dir/sources/meta-user/.

##############################################################################
# Add the axi-stream-dma & axi_memory_map kernel modules
##############################################################################

cp -rfL $aes_stream_drivers/Yocto/recipes-kernel $proj_dir/sources/meta-user/.
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS:append = \" axistreamdma aximemorymap\"" >> $proj_dir/sources/meta-user/conf/machine/zynqmp-user.conf
echo "MACHINE_ESSENTIAL_EXTRA_RRECOMMENDS:append = \" axistreamdma aximemorymap\"" >> $proj_dir/sources/meta-user/conf/machine/zynqmp-user.conf

# Update DMA engine with user configuration
sed -i "s/int cfgTxCount0 = 128;/int cfgTxCount0 = $dmaTxBuffCount;/"  $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgRxCount0 = 128;/int cfgRxCount0 = $dmaRxBuffCount;/"  $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c
sed -i "s/int cfgSize0    = 2097152;/int cfgSize0    = $dmaBuffSize;/" $proj_dir/sources/meta-user/recipes-kernel/axistreamdma/files/axistreamdma.c

##############################################################################
# Add axi-soc-ultra-plus-core's recipes-apps
##############################################################################

cp -rfL $axi_soc_ultra_plus_core/shared/Yocto/recipes-apps $proj_dir/sources/meta-user/.
echo "IMAGE_INSTALL:append = \" rogue rogue-dev\""  >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" roguetcpbridge\""   >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" axiversiondump\""   >> $proj_dir/sources/meta-user/conf/layer.conf
echo "IMAGE_INSTALL:append = \" startup-app-init\"" >> $proj_dir/sources/meta-user/conf/layer.conf

# Update Application with user configuration
sed -i "s/default  = 2,/default  = $numLane,/"  $proj_dir/sources/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge
sed -i "s/default  = 32,/default  = $numDest,/" $proj_dir/sources/meta-user/recipes-apps/roguetcpbridge/files/roguetcpbridge

# Check if including RFDC utility
if grep -q 'MACHINE_FEATURES:append = " rfsoc"' "$hwDir/Yocto/zynqmp-user.conf"; then
   echo "MACHINE_FEATURES=rfsoc detected: Including RFDC utility"
   echo "IMAGE_INSTALL:append = \" pyrfdc\"" >> $proj_dir/sources/meta-user/conf/layer.conf
fi

##############################################################################
# Add commonly used packages
##############################################################################

echo "IMAGE_INSTALL:append = \" peekpoke\"" >> $proj_dir/build/conf/local.conf
echo "IMAGE_INSTALL:append = \" nano\""     >> $proj_dir/build/conf/local.conf

# Enable debug-tweaks for development convenience; do not use in production images!!!
echo "IMAGE_FEATURES:append = \" debug-tweaks\"" >> $proj_dir/build/conf/local.conf

##############################################################################
# Build Everything!
##############################################################################

bitbake petalinux-image-minimal

##############################################################################
# Package all the images into a .tar.gz
##############################################################################

# mkdir custom image dump dir
mkdir $proj_dir/linux

# Go to deploy image dir
cd $proj_dir/build/tmp/deploy/images/zynqmp-user

# Copy over the FSBL, U-boot and .bit files
cp -rfL download-zynqmp-user.bit $proj_dir/linux/system.bit
cp -rfL boot.bin                 $proj_dir/linux/BOOT.BIN
cp -rfL boot.scr                 $proj_dir/linux/boot.scr

# Create the image.ub
cp -rfL Image linux.bin
gzip -k linux.bin
cp $axi_soc_ultra_plus_core/shared/Yocto/image.its .
mkimage -f image.its $proj_dir/linux/image.ub  > /dev/null

# Default file list
fileList="linux/system.bit linux/BOOT.BIN linux/boot.scr linux/image.ub"

if [[ -v SOC_IP_STATIC ]]; then
   # File list with static IP
   echo $SOC_IP_STATIC>>$proj_dir/linux/ip
   fileList="$fileList linux/ip"
fi

# Dump a compressed tarball of all the required build output files
cd $proj_dir && tar -czf $imageDump $fileList

echo "########################################################################"
echo "Release File List: $fileList"
echo "########################################################################"
echo "linux.tar.gz image path: $imageDump"
echo "########################################################################"

##############################################################################
