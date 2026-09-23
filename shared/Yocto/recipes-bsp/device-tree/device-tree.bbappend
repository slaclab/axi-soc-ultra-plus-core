FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Shipped through SRC_URI so the tool's checksum is part of the task signature
# and it lands in ${WORKDIR}, where a board's own device-tree bbappend can also
# call its encode subcommand.
SRC_URI:append = " file://rfdc_param_list.py"

# A board that has not opted in only warns. The RFSoC boards whose overlays
# still carry an empty RFDC param-list get a warning naming the board instead
# of a broken build, and a machine with no RFDC node at all (Kria, ZCU102) stays
# silent. A board that populates its node sets "fail" in its own bbappend.
RFDC_PARAM_LIST_POLICY ??= "warn"
# IPType the RFDC param-list must carry (librfdc XRFDC_GEN3 is 2); empty skips it.
RFDC_EXPECTED_IPTYPE ??= ""
# A board that encodes its param-list from an .xci sets both; the check then
# also requires the DTB to equal a fresh encode of that .xci. Empty skips it.
RFDC_XCI ??= ""
RFDC_BASEADDR ??= ""

# The check sits at deploy, on ${DEPLOYDIR}/devicetree/${DTB_FILE_NAME}: that is
# the exact file image.its packs into image.ub (through the ${MACHINE}-system.dtb
# link), so what passes here is what boots. It has to be a build-time check
# because librfdc copies XRFdc_Config out of param-list without checking how many
# bytes it read, so an empty property never fails at runtime; the driver just
# runs on uninitialized memory. The board is named from the meta-user
# recipes-bsp link, which both BuildYoctoProject.sh scripts point at
# hardware/<Board>/Yocto/recipes-bsp, so no per-board setting is needed. BitBake
# runs this body under set -e, so the exit status is captured rather than letting
# a failing check abort the task before its lines reach the log. Shell variables
# here are written without braces so BitBake does not expand them at parse time.
do_deploy:append () {
	rfdc_iptype_arg=""
	if [ -n "${RFDC_EXPECTED_IPTYPE}" ]; then
		rfdc_iptype_arg="--expect-iptype ${RFDC_EXPECTED_IPTYPE}"
	fi
	rfdc_xci_arg=""
	if [ -n "${RFDC_XCI}" ] && [ -n "${RFDC_BASEADDR}" ]; then
		rfdc_xci_arg="--xci ${RFDC_XCI} --base ${RFDC_BASEADDR}"
	fi
	rfdc_rc=0
	rfdc_out=$(python3 ${WORKDIR}/rfdc_param_list.py check \
		--dtb ${DEPLOYDIR}/devicetree/${DTB_FILE_NAME} \
		--meta-user-bsp ${TOPDIR}/../sources/meta-user/recipes-bsp \
		--policy ${RFDC_PARAM_LIST_POLICY} $rfdc_iptype_arg $rfdc_xci_arg 2>&1) || rfdc_rc=$?
	if [ $rfdc_rc -ne 0 ]; then
		bbfatal "$rfdc_out"
	elif printf '%s\n' "$rfdc_out" | grep -q '^WARNING:'; then
		bbwarn "$(printf '%s\n' "$rfdc_out" | grep '^WARNING:')"
	else
		bbnote "$rfdc_out"
	fi
}
