# The is automatic generated Code by "makePipRecipes.py"
# (build by Robin Sebastian (https://github.com/robseb) (git@robseb.de) Vers.: 1.2) 

SUMMARY = "Recipe to embedded the Python PiP Package nose2"
HOMEPAGE ="https://pypi.org/project/nose2"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://setup.py;md5=fd950a5417f680f5ccd6a0d804d6a41d"

inherit pypi setuptools3
PYPI_PACKAGE = "nose2"
SRC_URI[md5sum] = "edc78a9fb6c6881feaf2512b434a4657"
SRC_URI[sha256sum] = "956e79b9bd558ee08b6200c05ad2c76465b7e3860c0c0537686089285c320113"

#######################################################################################
# Post-makePipRecipes.py additions 
# based on "pipoe --package p4p --python python3 --outdir python3-p4p" outputs
#######################################################################################

RDEPENDS:${PN} += " \
   python3-setuptools \
"

BBCLASSEXTEND = "native nativesdk"
