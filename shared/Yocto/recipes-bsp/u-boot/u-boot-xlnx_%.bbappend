FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://platform-top.h file://bsp.cfg"
# Exclude every Kria machine — meta-kria's own u-boot-xlnx bbappend already
# proves ":kria" is an active override for this exact recipe on this exact machine family.
SRC_URI:remove:kria = " file://platform-top.h file://bsp.cfg"

UBOOT_NETBOOT_MODE ??= "fallback"

# Kria strips platform-top.h from SRC_URI (Kria configures u-boot from
# xilinx_zynqmp_kria_defconfig + .cfg fragments), so this body must no-op when the
# file was not fetched. A do_configure:append:kria would not help — task appends are
# additive, so the base append still runs on Kria.
do_configure:append () {
	if [ -f ${WORKDIR}/platform-top.h ]; then
		install ${WORKDIR}/platform-top.h ${S}/include/configs/
		if [ "${UBOOT_NETBOOT_MODE}" = "tftp-only" ]; then
			sed -i "s|@UBOOT_NETBOOT_MODE@|echo TFTP-only build: not falling back to SD|" \
				${S}/include/configs/platform-top.h
		else
			sed -i "s|@UBOOT_NETBOOT_MODE@|run sdboot|" ${S}/include/configs/platform-top.h
		fi
	fi
}
