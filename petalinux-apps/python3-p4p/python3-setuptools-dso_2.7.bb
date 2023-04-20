# The is automatic generated Code by "makePipRecipes.py"
# (build by Robin Sebastian (https://github.com/robseb) (git@robseb.de) Vers.: 1.2) 

SUMMARY = "Recipe to embedded the Python PiP Package setuptools_dso"
HOMEPAGE ="https://pypi.org/project/setuptools_dso"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3775480a712fc46a69647678acb234cb"

inherit pypi setuptools3
PYPI_PACKAGE = "setuptools_dso"
SRC_URI[md5sum] = "dc5e0257261b08e062edff08a1ef53a8"
SRC_URI[sha256sum] = "860a2c4ed32139029f7ba63babe77a6901695d7661ddf4d63f021a9a49dc66e2"

#######################################################################################
# Post-makePipRecipes.py additions 
# based on "pipoe --package p4p --python python3 --outdir python3-p4p" outputs
#######################################################################################

PN="python3-setuptools_dso"

RDEPENDS:${PN} += " \
   python3-setuptools \
"

BBCLASSEXTEND = "native nativesdk"
