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
## Check a KR260 Yocto project: usage: check_yocto_outputs.sh <YoctoProjects/<Target>>
set -u
proj=${1:?usage: $0 <Yocto project dir>}
fail=0
f() { echo "FAIL: $*"; fail=1; }
for x in BOOT.BIN boot.scr image.ub system.bit; do
   [ -f "$proj/linux/$x" ] || f "missing linux/$x"
done
if [ -f "$proj/linux/BOOT.BIN" ]; then
   size=$(stat -c %s "$proj/linux/BOOT.BIN")
   [ "$size" -le $((0xD80000)) ] || f "BOOT.BIN is $size bytes, slot holds $((0xD80000))"
   [ "$size" -le $((4 * 1024 * 1024)) ] || f "BOOT.BIN is $size bytes: bitstream probably embedded"
fi
conf="$proj/build/conf/local.conf"
grep -q '^DMA_NUM_LANES = "2"' "$conf" || f "DMA_NUM_LANES is not 2"
# image.ub packages zynqmp-user-system.dtb (shared/Yocto/image.its): the SOM
# tree with the KR260 carrier overlay merged
dtb="$proj/build/tmp/deploy/images/zynqmp-user/zynqmp-user-system.dtb"
[ -f "$dtb" ] || dtb=""
[ -n "$dtb" ] || f "no zynqmp-user-system.dtb in deploy dir"
if [ -n "$dtb" ]; then
   # The SD reader enumerates on USB; check the overlay's hub/control nodes.
   for compatible in xlnx,zynqmp-sk-kr260 microchip,usb5744 usb424,2744 usb424,5744 dlg,slg7xl45106 gpio-delay nxp,pca9546 axi_stream_dma; do
      strings "$dtb" | grep -Fq "$compatible" || f "DTB lacks $compatible"
   done
fi
cfg=$(find "$proj/build/tmp/work" -path '*linux-xlnx*' -name .config -not -path '*/sysroot*' 2>/dev/null | head -1)
[ -n "$cfg" ] || f "kernel .config not found"
if [ -n "$cfg" ]; then
   # SLG7XL45106 is driven by gpio-pca9570; gpio-delay by the GPIO aggregator
   for o in USB_STORAGE GPIO_PCA9570 GPIO_AGGREGATOR I2C_MUX_PCA954x DP83867_PHY MACB; do
      grep -q "^CONFIG_$o=[ym]" "$cfg" || f "kernel lacks CONFIG_$o"
   done
   grep -Eq '^CONFIG_USB_ONBOARD_(HUB|DEV)=[ym]' "$cfg" || f "kernel lacks the onboard USB hub driver"
fi
manifest=""
for candidate in "$proj"/build/tmp/deploy/images/zynqmp-user/petalinux-image-minimal-zynqmp-user{.rootfs,}.manifest; do
   if [ -f "$candidate" ]; then
      manifest="$candidate"
      break
   fi
done
if [ -n "$manifest" ]; then
   grep -Eq '^image-update[[:space:]]' "$manifest" || f "image-update not installed in the rootfs"
else
   f "rootfs package manifest not found for petalinux-image-minimal-zynqmp-user"
fi
[ $fail -eq 0 ] && echo "Yocto outputs OK"
exit $fail
