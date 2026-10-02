FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
 
SYSTEM_USER_DTSI ?= "system-user.dtsi"
 
SRC_URI:append = " file://${SYSTEM_USER_DTSI}"
 
do_configure:append() {
   cp ${WORKDIR}/${SYSTEM_USER_DTSI} ${B}/device-tree
   echo "#include \"${SYSTEM_USER_DTSI}\"" >> ${B}/device-tree/system-top.dts   
}

# The RF data converter IP is instantiated from HDL outside the block design, so
# the device tree generator never sees it and cannot write the param-list that
# librfdc reads its XRFdc_Config from. rfdc_param_list.py (shipped by the
# shared/Yocto device-tree bbappend) encodes it from the IP's .xci instead, and
# the shared deploy check fails this board's build unless the DTB carries a full
# XRFdc_Config with the Gen3 IPType.
RFDC_PARAM_LIST_POLICY = "fail"
RFDC_EXPECTED_IPTYPE = "2"
# The reg base of usp_rf_data_converter@490000000 in system-user.dtsi. It only
# fills BaseAddr: librfdc on Linux maps the registers from reg.
RFDC_BASEADDR = "0x490000000"
# Set per project in <project>/shared/Yocto/local.conf.
RFDC_XCI ??= ""
# Re-run do_configure whenever the .xci content changes, so sstate cannot hand
# back a DTB encoded from an older .xci.
do_configure[file-checksums] += "${@'${RFDC_XCI}:True' if d.getVar('RFDC_XCI') else ''}"

# system-user.dtsi includes the generated fragment inside the RFDC node, so this
# does not depend on the order the layers' do_configure appends run in, and a
# missing fragment fails the device tree compile loudly.
do_configure:append() {
   if [ -z "${RFDC_XCI}" ]; then
      bbfatal "SlacRfmcCarrier: RFDC_XCI is not set. Point RFDC_XCI at the usp_rf_data_converter .xci that the design's bitstream is built from; set it in <project>/shared/Yocto/local.conf, which the Yocto build script appends to build/conf/local.conf on a fresh or -c configure."
   fi
   python3 ${WORKDIR}/rfdc_param_list.py encode --xci "${RFDC_XCI}" --base ${RFDC_BASEADDR} -o ${B}/device-tree/rfdc-param-list.dtsi
}
