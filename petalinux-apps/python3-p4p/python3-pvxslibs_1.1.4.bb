# The is automatic generated Code by "makePipRecipes.py"
# (build by Robin Sebastian (https://github.com/robseb) (git@robseb.de) Vers.: 1.2) 

SUMMARY = "Recipe to embedded the Python PiP Package pvxslibs"
HOMEPAGE ="https://pypi.org/project/pvxslibs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3775480a712fc46a69647678acb234cb"

inherit pypi setuptools3
PYPI_PACKAGE = "pvxslibs"
SRC_URI[md5sum] = "142b64df03ce6011a9a079e0c073c2a2"
SRC_URI[sha256sum] = "a9bfb16547bbf522b39ca2ac2479dfcdb217496f563a5793fde6b5d1925c8d57"

#######################################################################################
# Post-makePipRecipes.py additions 
# based on "pipoe --package p4p --python python3 --outdir python3-p4p" outputs
#######################################################################################

DEPENDS += " \
   python3-setuptools \
   python3-setuptools_dso \
   python3-setuptools_dso-native \
   python3-epicscorelibs \
   python3-epicscorelibs-native \
"

RDEPENDS:${PN} += " \
   python3-setuptools_dso \
   python3-epicscorelibs \
"

BBCLASSEXTEND = "native nativesdk"

#######################################################################################
# Current receipe fails during do_configure() and do_install() in the python setup.py
# Overriding the bbfatal_log() so that they petalinux-build doesn't fail for project
#######################################################################################
bbfatal_log() {
	if [ -p ${S}/../temp/fifo.1531538 ] ; then
		printf "%b\0" "bbfatal_log $*" > ${S}/../temp/fifo.1531538
	else
		echo "ERROR: $*"
	fi
#	exit 1
}
