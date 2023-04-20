# The is automatic generated Code by "makePipRecipes.py"
# (build by Robin Sebastian (https://github.com/robseb) (git@robseb.de) Vers.: 1.2) 

SUMMARY = "Recipe to embedded the Python PiP Package epicscorelibs"
HOMEPAGE ="https://pypi.org/project/epicscorelibs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2eeea17a15fc6ba8501fdcec09b854dc"

inherit pypi setuptools3
PYPI_PACKAGE = "epicscorelibs"
SRC_URI[md5sum] = "69f1d18a7e0e72e7d6572b1c865b8358"
SRC_URI[sha256sum] = "d08cd4b228d7087fd172b9b48d5ebc1c763b6c1fbda26369776d5b274f1c73b4"

#######################################################################################
# Post-makePipRecipes.py additions 
# based on "pipoe --package p4p --python python3 --outdir python3-p4p" outputs
#######################################################################################

DEPENDS += " \
   python3-setuptools \
   python3-setuptools_dso \
   python3-setuptools_dso-native \
   python3-numpy \
"

RDEPENDS:${PN} += " \
   python3-setuptools \
   python3-setuptools_dso \
   python3-setuptools_dso-native \
   python3-numpy \
"

BBCLASSEXTEND = "native nativesdk"
