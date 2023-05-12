#
# This file is the rogue recipe.
#

ROGUE_VERSION = "5.18.4"
ROGUE_MD5SUM  = "c5f37ebd8cbf241b6f717bb6b0aeec8e"

SUMMARY = "Recipe to build Rogue"
HOMEPAGE ="https://github.com/slaclab/rogue"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "https://github.com/slaclab/rogue/archive/v${ROGUE_VERSION}.tar.gz"
SRC_URI[md5sum] = "${ROGUE_MD5SUM}"

S = "${WORKDIR}/rogue-${ROGUE_VERSION}"
PROVIDES = "rogue"
EXTRA_OECMAKE += "-DROGUE_INSTALL=system -DROGUE_VERSION=v${ROGUE_VERSION}"

inherit cmake python3native distutils3

DEPENDS += " \
   python3 \
   python3-native \
   python3-numpy \
   python3-numpy-native \
   python3-pyzmq \
   python3-parse \
   python3-pyyaml \
   python3-click \
   python3-sqlalchemy \
   python3-pyserial \
   bzip2 \
   zeromq \
   boost \
   cmake \
"

RDEPENDS:${PN} += " \
   python3-numpy \
   python3-pyzmq \
   python3-parse \
   python3-pyyaml \
   python3-click \
   python3-sqlalchemy \
   python3-pyserial \
   python3-json \
   python3-logging \
"

FILES:${PN}-dev += "/usr/include/rogue/*"
FILES:${PN} += "/usr/lib/*"

do_configure() {
   cmake_do_configure
   bbplain $(cp -vH ${WORKDIR}/build/setup.py ${S}/.)
}

do_install() {
   cmake_do_install
   distutils3_do_install
}
