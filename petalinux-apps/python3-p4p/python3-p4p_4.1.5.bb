# The is automatic generated Code by "makePipRecipes.py"
# (build by Robin Sebastian (https://github.com/robseb) (git@robseb.de) Vers.: 1.2) 

SUMMARY = "Recipe to embedded the Python PiP Package p4p"
HOMEPAGE ="https://pypi.org/project/p4p"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3775480a712fc46a69647678acb234cb"

inherit pypi setuptools3
PYPI_PACKAGE = "p4p"
SRC_URI[md5sum] = "14ac58b22d57b8df9cc3e7582040c21e"
SRC_URI[sha256sum] = "25130597c4333590a4b2fc98fea2a0cd8615647d4e9454ddeddc6700112f8f04"

#######################################################################################
# Post-makePipRecipes.py additions 
# based on "pipoe --package p4p --python python3 --outdir python3-p4p" outputs
#######################################################################################

RDEPENDS:${PN} += " \
   python3-setuptools \
   python3-epicscorelibs \
   python3-pvxslibs \
   python3-numpy \
   python3-nose2 \
   python3-ply \
"

BBCLASSEXTEND = "native nativesdk"
