#
# This file is the roguetcpbridge recipe.
#

SUMMARY = "Simple roguetcpbridge application"
SECTION = "Yocto/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://roguetcpbridge \
        "

S = "${WORKDIR}"

RDEPENDS:${PN} += " \
   rogue \
"

do_install() {
             install -d ${D}/${bindir}
             install -m 0755 ${S}/roguetcpbridge ${D}/${bindir}
}
