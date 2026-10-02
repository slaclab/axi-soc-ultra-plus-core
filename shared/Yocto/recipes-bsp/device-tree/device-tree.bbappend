# Generate the RFDC "param-list" devicetree property from the RFDC IP core .xci
# for every machine with the rfsoc MACHINE_FEATURE. XRFdc_LookupConfig() reads
# the property into the driver's XRFdc_Config, so an empty or wrong-size list
# hands the driver uninitialized memory. The build fails instead of shipping one.
#
# RFDC_XCI is set in local.conf by BuildYoctoProject.sh (-R option).

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

RFDC_XCI ??= ""
RFDC_PARAM_LIST_EN = "${@bb.utils.contains('MACHINE_FEATURES', 'rfsoc', '1', '0', d)}"

SRC_URI:append = " ${@bb.utils.contains('MACHINE_FEATURES', 'rfsoc', 'file://rfdc_param_list.py', '', d)}"
# The cross compiler and libc headers are needed by the compile-only size probe
DEPENDS:append = " ${@bb.utils.contains('MACHINE_FEATURES', 'rfsoc', 'librfdc libmetal virtual/${TARGET_PREFIX}gcc virtual/libc', '', d)}"

python () {
    if d.getVar('RFDC_PARAM_LIST_EN') != '1':
        return
    xci = d.getVar('RFDC_XCI') or ''
    # Rebuild the devicetree when the .xci content changes
    if os.path.isfile(xci):
        d.appendVarFlag('do_configure', 'file-checksums', ' %s:True' % xci)
    d.appendVarFlag('do_configure', 'vardeps', ' RFDC_XCI')
}

python rfdc_check_xci() {
    if d.getVar('RFDC_PARAM_LIST_EN') != '1':
        return
    xci = d.getVar('RFDC_XCI') or ''
    if not xci or not os.path.isfile(xci):
        bb.fatal("RFSoC target needs the RFDC .xci: pass -R RFDC_XCI to BuildYoctoProject.sh (RFDC_XCI='%s')" % xci)
}
do_configure[prefuncs] += "rfdc_check_xci"

do_configure:append() {
   if [ "${RFDC_PARAM_LIST_EN}" = "1" ]; then
      size=$(python3 ${WORKDIR}/rfdc_param_list.py --xci "${RFDC_XCI}" --out ${B}/device-tree/rfdc-param-list.dtsi --print-size) \
         || bbfatal "RFDC param-list generation failed for ${RFDC_XCI}"
      # Compile-only probe: the generated size must equal sizeof(XRFdc_Config) of the librfdc in the sysroot
      printf '#include <xrfdc.h>\n_Static_assert(sizeof(XRFdc_Config) == %s, "param-list size mismatch");\n' "$size" \
         | ${CC} ${CFLAGS} -fsyntax-only -x c - -I${STAGING_INCDIR} \
         || bbfatal "RFDC param-list size ($size bytes) differs from sizeof(XRFdc_Config) in the sysroot xrfdc.h, or the size probe could not compile (see the compiler output above)"
      echo "#include \"rfdc-param-list.dtsi\"" >> ${B}/device-tree/system-top.dts
      bbplain "RFDC param-list: $size bytes from ${RFDC_XCI}"
   fi
}
