#
# This file is the startupapp recipe.
#

SUMMARY = "Simple startupapp application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://startupapp \
	"

S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "startupapp"
INITSCRIPT_PARAMS = "start 99 5 ."

do_install() {
	     install -d ${D}${sysconfdir}/init.d
	     install -m 0755 ${S}/startupapp ${D}${sysconfdir}/init.d/startupapp
}
FILES_${PN} += "${sysconfdir}/*"
