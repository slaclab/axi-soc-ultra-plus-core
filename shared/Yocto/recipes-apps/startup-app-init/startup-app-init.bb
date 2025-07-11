#
# This file is the startup-app-init recipe.
#

SUMMARY = "Simple startup-app-init application"
SECTION = "Yocto/apps"
LICENSE = "MIT"

LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://startup-app-init \
           file://startup-app-init.service \
"

S = "${WORKDIR}"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

inherit update-rc.d systemd

RDEPENDS:${PN} += " \
   python3 \
   axiversiondump \
   roguetcpbridge \
"

INITSCRIPT_NAME = "startup-app-init"
INITSCRIPT_PARAMS = "start 99 5 ."

SYSTEMD_PACKAGES = "${PN}"
SYSTEMD_SERVICE:${PN} = "startup-app-init.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
        if ${@bb.utils.contains('DISTRO_FEATURES', 'sysvinit', 'true', 'false', d)}; then
                install -d ${D}${sysconfdir}/init.d/
                install -m 0755 ${WORKDIR}/startup-app-init ${D}${sysconfdir}/init.d/
        fi

        install -d ${D}${bindir}
        install -m 0755 ${WORKDIR}/startup-app-init ${D}${bindir}/
        install -d ${D}${systemd_system_unitdir}
        install -m 0644 ${WORKDIR}/startup-app-init.service ${D}${systemd_system_unitdir}
}

FILES:${PN} += "${@bb.utils.contains('DISTRO_FEATURES','sysvinit','${sysconfdir}/*', '', d)}"
