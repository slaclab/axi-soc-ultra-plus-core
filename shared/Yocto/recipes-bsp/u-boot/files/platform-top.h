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
	/* Bitstream fetch+program, run by netboot only in the tftp-only (diskless) */ \
	/* build. 'setexpr gsub' rewrites ${ethaddr}'s colons to dashes into ${macfn} */ \
	/* (U-Boot's tftpboot parses the first ':' in a filename as a hostIP separator, */ \
	/* so a colon-form name is unusable). Tries the per-MAC file first */ \
	/* (system.bit.bin.<mac-dashes>), then a generic system.bit.bin, then 'fpga */ \
	/* load's the raw .bin to the PL via PCAP. Reuses 0x10000000 sequentially: fpga */ \
	/* load consumes the buffer before netboot fetches the kernel FIT to the same */ \
	/* address. It is && so a total fetch or program failure aborts netboot before */ \
	/* the kernel boots (never a net kernel over an unprogrammed PL). ${filesize} is */ \
	/* set by whichever tftpboot succeeded. */ \
	"loadpl_net=setexpr macfn gsub : - ${ethaddr} && if tftpboot 0x10000000 system.bit.bin.${macfn}; then true; else tftpboot 0x10000000 system.bit.bin; fi && fpga load 0 0x10000000 ${filesize}\0" \
	/* fallback build: no U-Boot bitstream load -- the SD's startup-app-init fpgautil */ \
	/* owns the PL exactly as before (avoids a double-program). */ \
	"loadpl_skip=true\0" \
	/* PXE-first: 'pxe get' downloads pxelinux.cfg/01-<MAC> (else .../default) from */ \
	/* ${serverip} and 'pxe boot' loads the KERNEL FIT it names (FIT -> bootm). This */ \
	/* lets boot behavior change server-side without reflashing U-Boot or editing the */ \
	/* board env. If no PXE config is served, fall back to fetching image.ub directly. */ \
	/* Addresses (pxefile_addr_r/kernel_addr_r) come from ENV_MEM_LAYOUT_SETTINGS -- do */ \
	/* not hand-set them here. Either branch returns nonzero on failure, so bootcmd's */ \
	/* else-branch (@UBOOT_NETBOOT_MODE@) still runs. 'run @LOADPL_SEL@' is substituted */ \
	/* at build time by the u-boot bbappend to loadpl_net (tftp-only) or loadpl_skip */ \
	/* (fallback); the bareword swap keeps '&&' out of the bbappend sed replacement. */ \
	"netboot=if test -n \"${ipaddr}\"; then true; else dhcp; fi && run @LOADPL_SEL@ && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi\0" \
	/* Reuse the existing, unmodified SD-boot mechanism verbatim. */ \
	"sdboot=run distro_bootcmd\0" \
	"bootcmd=if run netboot; then true; else @UBOOT_NETBOOT_MODE@; fi\0"
