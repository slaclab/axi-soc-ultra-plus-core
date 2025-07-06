#
# This file is the axiversiondump recipe.
#

SUMMARY = "Simple axiversiondump application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://axiversiondump \
        "

S = "${WORKDIR}"

RDEPENDS:${PN} += " \
   rogue \
"

do_install() {
             install -d ${D}/${bindir}
             install -m 0755 ${S}/axiversiondump ${D}/${bindir}
}
