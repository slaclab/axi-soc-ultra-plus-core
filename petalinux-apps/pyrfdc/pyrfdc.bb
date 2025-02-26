DESCRIPTION = "A Yocto package for PyRFdc with Rogue + Boost.Python bindings"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
   file://PyRFdc.cpp \
   file://PyRFdc.h \
   file://CMakeLists.txt \
"

S = "${WORKDIR}"

inherit cmake python3native

DEPENDS += " \
   python3 \
   python3-native \
   python3-numpy \
   python3-numpy-native \
   bzip2 \
   zeromq \
   boost \
   cmake \
   rogue \
   librfdc \
   libmetal \
"

RDEPENDS:${PN} += " \
   rogue \
   python3 \
   librfdc \
   libmetal \
"

FILES:${PN} += "/usr/lib/*"

do_install:append() {
   install -d ${D}${PYTHON_SITEPACKAGES_DIR}
   install -m 0755 ${B}/PyRFdc.so ${D}${PYTHON_SITEPACKAGES_DIR}/
}
