SUMMARY = "Simple rfdc-api application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
   file://Makefile \
   file://xrfdc_mst.c \
   file://xrfdc_NyquistZone.c \
"

S = "${WORKDIR}"

DEPENDS += " \
   glibc \
   librfdc \
   libmetal \
"

RDEPENDS:${PN} += " \
   glibc \
   librfdc \
   libmetal \
"

EXTRA_OEMAKE = " \
    'CFLAGS=-I${STAGING_INCDIR} -D__aarch64__ -Wall -Wextra -O2' \
    'LDFLAGS=-L${STAGING_LIBDIR} -lrfdc -lmetal -Wl,--hash-style=gnu' \
    'ARCH=aarch64' \
"

do_compile (){
    oe_runmake OUTS=rfdc-mst         RFDC_OBJS=xrfdc_mst.o
    oe_runmake OUTS=rfdc-NyquistZone RFDC_OBJS=xrfdc_NyquistZone.o
}

do_install() {
   install -d ${D}/${bindir}
   install -m 0755 ${S}/rfdc-mst         ${D}/${bindir}
   install -m 0755 ${S}/rfdc-NyquistZone ${D}/${bindir}
}
