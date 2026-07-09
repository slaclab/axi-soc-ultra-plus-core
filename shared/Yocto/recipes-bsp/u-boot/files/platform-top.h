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
	"netboot=if test -n \"${ipaddr}\"; then true; else dhcp; fi && tftpboot 0x10000000 image.ub && bootm 0x10000000\0" \
	/* Reuse the existing, unmodified SD-boot mechanism verbatim. */ \
	"sdboot=run distro_bootcmd\0" \
	"bootcmd=if run netboot; then true; else @UBOOT_NETBOOT_MODE@; fi\0"
