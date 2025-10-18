# Fix for Qemu compile on glibc 2.41
SRC_URI += "file://0001-qemu-do-not-define-sched_attr.patch"
FILESEXTRAPATHS:prepend := "${THISDIR}/patches:"