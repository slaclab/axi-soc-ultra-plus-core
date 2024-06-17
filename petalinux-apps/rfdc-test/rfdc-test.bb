SUMMARY = "Simple rfdc-test application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://Makefile \
       file://xrfdc_selftest_example.c \
       file://xrfdc_mts_example.c \
          "

S = "${WORKDIR}"

DEPENDS += " \
   rfdc \
   libmetal \
"

RDEPENDS:${PN} += " \
   rfdc \
   libmetal \
"

EXTRA_OEMAKE = " \
    'CFLAGS=-I${STAGING_INCDIR} -D__aarch64__ -Wall -Wextra -O2' \
    'LDFLAGS=-L${STAGING_LIBDIR} -lrfdc -lmetal -Wl,--hash-style=gnu' \
    'ARCH=aarch64' \
"

do_compile (){
    oe_runmake OUTS=rfdc-test RFDC_OBJS=xrfdc_selftest_example.o
    oe_runmake OUTS=rfdc-mst  RFDC_OBJS=xrfdc_mts_example.o
}

do_install() {
   install -d ${D}/${bindir}
   install -m 0755 ${S}/rfdc-test ${D}/${bindir}
   install -m 0755 ${S}/rfdc-mst  ${D}/${bindir}
}
