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
	/* so a colon-form name is unusable). Probe order is most- to least-specific, */ \
	/* preferring the Vivado .bit at each level: */ \
	/*   system.bit.<mac-dashes> -> system.bin.<mac-dashes> -> system.bit -> system.bin */ \
	/* then 'fpga load's whichever hit to the PL via PCAP. Both formats are accepted: */ \
	/* on PMUFW > v1.0 zynqmp_load() skips zynqmp_validate_bitstream() entirely and */ \
	/* hands the buffer verbatim to the PMU. The .bit is not just the .bin plus a */ \
	/* ~222-byte header: bootgen also byte-reverses every 32-bit word (sync AA995566 */ \
	/* vs 665599AA), so a .bit loads because the PCAP auto-detects bus width and */ \
	/* endianness, and it costs ~16.6s versus ~0.2s for a .bin on a 34MB bitstream -- */ \
	/* paid on every boot, since .bit is preferred. A .bit is rejected only on PMUFW */ \
	/* <= v1.0, where the validator runs and fails the header-offset 'diff' check. */ \
	/* A miss costs one immediate TFTP NAK, not a timeout, so the extra probes are */ \
	/* effectively free. Reuses 0x10000000 sequentially: fpga */ \
	/* load consumes the buffer before netboot fetches the kernel FIT to the same */ \
	/* address. It is && so a total fetch or program failure aborts netboot before */ \
	/* the kernel boots (never a net kernel over an unprogrammed PL). ${filesize} is */ \
	/* set by whichever tftpboot succeeded. */ \
	"loadpl_net=setexpr macfn gsub : - ${ethaddr} && if tftpboot 0x10000000 system.bit.${macfn}; then true; elif tftpboot 0x10000000 system.bin.${macfn}; then true; elif tftpboot 0x10000000 system.bit; then true; else tftpboot 0x10000000 system.bin; fi && fpga load 0 0x10000000 ${filesize}\0" \
	/* fallback and sd-only builds: no U-Boot bitstream load -- the SD's */ \
	/* startup-app-init fpgautil owns the PL exactly as before (avoids a */ \
	/* double-program). */ \
	"loadpl_skip=true\0" \
	/* PXE-first: 'pxe get' downloads pxelinux.cfg/01-<MAC> (else .../default) from */ \
	/* ${serverip} and 'pxe boot' loads the KERNEL FIT it names (FIT -> bootm). This */ \
	/* lets boot behavior change server-side without reflashing U-Boot or editing the */ \
	/* board env. If no PXE config is served, fall back to fetching image.ub directly. */ \
	/* Addresses (pxefile_addr_r/kernel_addr_r) come from ENV_MEM_LAYOUT_SETTINGS -- do */ \
	/* not hand-set them here. 'run @LOADPL_SEL@' is substituted at build time by the */ \
	/* u-boot bbappend to loadpl_net (tftp-only) or loadpl_skip (fallback, sd-only); the */ \
	/* bareword swap keeps '&&' out of the bbappend sed replacement. */ \
	/* DHCP + serverip (lwIP): 'dhcp' unconditionally overwrites ${serverip} with the */ \
	/* DHCP server's own address, and sets ${tftpserverip} from the next-server (siaddr) */ \
	/* field when present; 'tftpboot'/'pxe' prefer ${tftpserverip} over ${serverip}. The */ \
	/* legacy CONFIG_BOOTP_SERVERIP knob does NOT exist in the lwIP DHCP path, so we work */ \
	/* around it here: when ${serverip} is already set (authoritative site config), stash */ \
	/* it, run dhcp, then restore serverip and clear tftpserverip. A failed dhcp short- */ \
	/* circuits the && chain with no restore (correct -- nothing was bound/clobbered), */ \
	/* still bailing at the hardcoded lwIP timeout. To hand TFTP addressing back to DHCP, */ \
	/* clear serverip ('setenv serverip'). */ \
	"netboot=if test -n \"${ipaddr}\"; then true; else if test -n \"${serverip}\"; then setenv _sip ${serverip}; dhcp && setenv serverip ${_sip} && setenv tftpserverip && setenv _sip; else dhcp; fi; fi && run @LOADPL_SEL@ && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi\0" \
	/* Reuse the existing, unmodified SD-boot mechanism verbatim. */ \
	"sdboot=run distro_bootcmd\0" \
	/* The WHOLE bootcmd value is substituted at build time by the u-boot bbappend, not */ \
	/* just a trailing mode action, so a mode can opt out of netboot entirely: */ \
	/*   sd-only (default) -> echo <skipping netboot>; run sdboot            */ \
	/*   fallback          -> run netboot; run sdboot                        */ \
	/*   tftp-only         -> run netboot; echo <not falling back to SD>     */ \
	/* sd-only's leading echo gives it a unique 'strings BOOT.BIN' signature and a */ \
	/* serial-console one; a bare 'run sdboot' would be indistinguishable from the */ \
	/* stock upstream 'run distro_bootcmd' that U-Boot also emits into the env. */ \
	/* sd-only exists because netboot is PXE-first: with no TFTP server answering, */ \
	/* 'pxe get' walks 13 pxelinux.cfg names and the direct image.ub fetch follows, and */ \
	/* each of those 14 attempts waits its full ~6s request timeout (~85s server-down, */ \
	/* ~110s under a silent DROP). A board that never had a TFTP server paid that on */ \
	/* every boot. 'run sdboot' is 'run distro_bootcmd', i.e. the stock upstream ZynqMP */ \
	/* bootcmd, so sd-only introduces no new boot path. The netboot and loadpl_* env */ \
	/* vars above stay DEFINED in every mode -- they cost nothing at boot and keep */ \
	/* 'run netboot' available by hand for recovery on an sd-only board. */ \
	/* Where 'run netboot' IS in bootcmd, the sequence is deliberately sequential and */ \
	/* NOT exit-code-gated: a successful boot never returns to the U-Boot shell, so */ \
	/* merely reaching the mode action proves netboot failed -- no exit code needed. */ \
	/* This stays correct even for boot methods that lie about their status: upstream */ \
	/* 'pxe boot' returns 0 after a failed label_boot() (handle_pxe_menu is void, */ \
	/* pxe_process() discards the nonzero), so an exit-code-gated fallback would */ \
	/* silently never fire now that netboot is PXE-first. */ \
	"bootcmd=@BOOTCMD_SEL@\0"
