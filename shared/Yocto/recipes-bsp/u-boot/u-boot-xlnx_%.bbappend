FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://platform-top.h file://bsp.cfg"
# Exclude every Kria machine — meta-kria's own u-boot-xlnx bbappend already
# proves ":kria" is an active override for this exact recipe on this exact machine family.
SRC_URI:remove:kria = " file://platform-top.h file://bsp.cfg"

UBOOT_NETBOOT_MODE ??= "sd-only"

# Kria strips platform-top.h from SRC_URI (Kria configures u-boot from
# xilinx_zynqmp_kria_defconfig + .cfg fragments), so this body must no-op when the
# file was not fetched. A do_configure:append:kria would not help — task appends are
# additive, so the base append still runs on Kria.
do_configure:append () {
	if [ -f ${WORKDIR}/platform-top.h ]; then
		install ${WORKDIR}/platform-top.h ${S}/include/configs/
		# @BOOTCMD_SEL@ is the WHOLE bootcmd value, so sd-only can drop 'run netboot'
		# entirely and never pay the PXE/TFTP timeouts on a board with no TFTP server.
		# @LOADPL_SEL@ selects whether netboot programs the PL from a TFTP-fetched
		# bitstream (loadpl_net, tftp-only/diskless) or does nothing (loadpl_skip,
		# fallback and sd-only -- SD fpgautil owns the PL). Barewords keep '&&' out of
		# the sed replacements, and the literals stay inline rather than in shell
		# variables because BitBake would capture a ${...} expansion at parse time.
		# Every replacement is plain text on purpose: no '&' (sed's whole-match),
		# no '|' (the delimiter), and no '\' or '"' (the text lands inside a C
		# string literal). ';' is safe inside a sed replacement, but the sed
		# script MUST stay double-quoted or the shell would fork at it.
		case "${UBOOT_NETBOOT_MODE}" in
			tftp-only)
				sed -i "s|@BOOTCMD_SEL@|run netboot; echo TFTP-only build: not falling back to SD|" \
					${S}/include/configs/platform-top.h
				sed -i "s|@LOADPL_SEL@|loadpl_net|" ${S}/include/configs/platform-top.h
				;;
			fallback)
				sed -i "s|@BOOTCMD_SEL@|run netboot; run sdboot|" ${S}/include/configs/platform-top.h
				sed -i "s|@LOADPL_SEL@|loadpl_skip|" ${S}/include/configs/platform-top.h
				;;
			*)
				# sd-only (the default). The leading echo is not decoration: it is
				# the only mode whose bootcmd would otherwise be indistinguishable
				# from the stock upstream 'run distro_bootcmd', so it gives the mode
				# both a unique 'strings BOOT.BIN' signature and a serial-console one.
				if [ -n "${UBOOT_NETBOOT_MODE}" ] && [ "${UBOOT_NETBOOT_MODE}" != "sd-only" ]; then
					bbwarn "Unrecognized UBOOT_NETBOOT_MODE '${UBOOT_NETBOOT_MODE}'; building 'sd-only'"
				fi
				sed -i "s|@BOOTCMD_SEL@|echo SD-only build: skipping netboot; run sdboot|" \
					${S}/include/configs/platform-top.h
				sed -i "s|@LOADPL_SEL@|loadpl_skip|" ${S}/include/configs/platform-top.h
				;;
		esac
	fi
}
