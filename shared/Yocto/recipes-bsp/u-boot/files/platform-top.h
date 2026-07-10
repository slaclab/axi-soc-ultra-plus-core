#include <configs/xilinx_zynqmp.h>

#undef CFG_EXTRA_ENV_SETTINGS
#define CFG_EXTRA_ENV_SETTINGS \
	ENV_MEM_LAYOUT_SETTINGS \
	"usb_pgood_delay=1000\0" \
	BOOTENV \
	/* Static-IP override placeholders — deliberately empty; site-configurable, */ \
	/* never a hardcoded subnet (scripts/NETBOOT_BRINGUP_NOTES.md Sec. 2). */ \
	"serverip=\0" \
	"ipaddr=\0" \
	"gatewayip=\0" \
	"netmask=\0" \
	/* Prefer a preset static IP over DHCP when one is configured. */ \
	/* && short-circuits so a dhcp or tftp failure each bail out at their own */ \
	/* independent, hardcoded lwIP timeout (~6.2s measured for TFTP-fail; DHCP-fail unmeasured, */ \
	/* see Common Pitfalls Pitfall 4) rather than summing both stages' timeouts. */ \
	/* PXE-first: 'pxe get' downloads pxelinux.cfg/01-<MAC> (else .../default) from */ \
	/* ${serverip} and 'pxe boot' loads the KERNEL FIT it names (FIT -> bootm). This */ \
	/* lets boot behavior change server-side without reflashing U-Boot or editing the */ \
	/* board env. If no PXE config is served, fall back to fetching image.ub directly. */ \
	/* Addresses (pxefile_addr_r/kernel_addr_r) come from ENV_MEM_LAYOUT_SETTINGS -- do */ \
	/* not hand-set them here. Either branch returns nonzero on failure, so bootcmd's */ \
	/* else-branch (@UBOOT_NETBOOT_MODE@) still runs. */ \
	"netboot=if test -n \"${ipaddr}\"; then true; else dhcp; fi && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi\0" \
	/* Reuse the existing, unmodified SD-boot mechanism verbatim. */ \
	"sdboot=run distro_bootcmd\0" \
	"bootcmd=if run netboot; then true; else @UBOOT_NETBOOT_MODE@; fi\0"
