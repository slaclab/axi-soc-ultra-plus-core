# TFTP Netboot — Bring-Up & Validation Notes (RFSoC 4x2)

Reference values proven on real hardware for the shared U-Boot netboot hooks, the host
TFTP provisioning tooling, and the how-to documentation. This file lives under `scripts/`
alongside the provisioning tooling it documents — deliberately **not** under `docs/`, so it
cannot break the warnings-fatal `sphinx-build -W` docs build.

**Dev host:** `rdsrv403`   **Lab interface:** `eth2` = `10.0.0.1/24`
**Board:** RFSoC 4x2 (`SimpleRfSoc4x2Example`), serial `/dev/ttyUSB1` @ 115200

---

## 1. Host TFTP provisioning

Provisioning is scripted by `scripts/provision_tftp_host.sh`. The script is
idempotent: it (a) writes the canonical dnsmasq config, (b) stages the built `image.ub`
into the TFTP root, and (c) launches a standalone dnsmasq — and a re-run against an
already-provisioned host is a sudo-free no-op (no duplicate daemon).

- **Config** `/etc/dnsmasq.d/tftp-lab.conf` (TFTP-only; DNS disabled):
  ```
  interface=eth2
  bind-interfaces
  except-interface=lo
  port=0            # DNS disabled
  enable-tftp
  tftp-root=/tftpboot
  ```
- **There is no `dnsmasq.service` on this host** (dnsmasq is present only as a libvirt
  dependency), so it runs as a standalone instance:
  ```
  sudo dnsmasq --conf-file=/etc/dnsmasq.d/tftp-lab.conf --pid-file=/run/dnsmasq-tftp-lab.pid
  ```
  Stop it with `sudo kill "$(cat /run/dnsmasq-tftp-lab.pid)"`.
- **TFTP root** `/tftpboot` (owned by the running user); **served FIT** `/tftpboot/image.ub`.
- The dnsmasq daemon drops privileges to `nobody`. A liveness check therefore cannot use
  `kill -0` from an unprivileged user (EPERM is indistinguishable from ESRCH) — the script
  reads `/proc/<pid>/comm` when `kill -0` returns EPERM, and reaps a stale pidfile PID
  before any relaunch.

**Independent host-side verification (no board involved):**
```
curl -sf -o /tmp/verify_image.ub tftp://10.0.0.1/image.ub    # exit 0
cmp /tmp/verify_image.ub /tftpboot/image.ub                  # identical
stat -c%s /tmp/verify_image.ub                               # matches the built image.ub
```
`curl` (7.81.0, `tftp` scheme) is used because `tftp-hpa` is not installed on the host.

---

## 2. Lab DHCP reality (shapes the DHCP design)

The `10.0.0.0/24` lab net is **not** a bare private segment. A central **ISC `dhcpd`**
(`/etc/dhcp/dhcpd.conf`, `INTERFACESv4=""` → answers on all matching interfaces including
`eth2`) already serves it and is **shared across the dev boards**:

```
subnet 10.0.0.0 netmask 255.255.255.0 {
  range 10.0.0.151 10.0.0.199;
  option routers 10.0.0.1;         # this host (rdsrv403)
  host dev_zcu111 { fixed-address 10.0.0.10;  }
  host dev_zcu208 { fixed-address 10.0.0.11;  }
  host dev_zcu216 { fixed-address 10.0.0.12;  }
  host slac_test1 { fixed-address 10.0.0.200; }
}
```

The RFSoC 4x2 (`fc:c2:3d:5a:9a:08`) takes a **dynamic lease** from this `dhcpd` (inside
`.151–.199`); the exact address rotates between reboots, so **never gate pass/fail on the
board's dynamic lease** — gate on the explicit `serverip 10.0.0.1` and on the observable
boot outcome.

**Consequence:** the host TFTP server runs **TFTP-only**. Running a second DHCP server
(e.g. dnsmasq `dhcp-range`) on this segment races the lab `dhcpd` and can hand conflicting
leases to the *other* boards.

### DHCP model (design direction)
- **Primary — site DHCP:** rely on the existing site DHCP server for board addressing; the
  host/submodule adds **only** TFTP-first + SD-fallback.
- **Isolated-lab option:** a lab with *no* existing DHCP MAY run dnsmasq as DHCP+TFTP — but
  **never run two DHCP servers on one segment.** Validate only on an isolated net, or with
  the site DHCP deliberately stopped. (Not exercised here — the shared `dhcpd` was left
  running to protect the other dev boards.)
- **No-DHCP option — static-IP override:** set `serverip`/`ipaddr`/`gatewayip`/`netmask` on
  the board. These MUST be site-configurable with documented placeholder defaults, never a
  hardcoded subnet.

### Subnet is a lab artifact, not a design constraint
`10.0.0.x` is this lab's addressing only. The boot path is **subnet-agnostic**: the board
takes its IP from whatever DHCP server is present, and `serverip` is set explicitly on the
board for TFTP. The shared hooks must not bake in any subnet.

---

## 3. Board-side netboot (positive path, proven on hardware)

At the `ZynqMP>` prompt (interrupt the autoboot) the exact working manual sequence:
```
dhcp                              # -> "DHCP client bound to address 10.0.0.xxx (N ms)" (lease from the lab dhcpd)
setenv serverip 10.0.0.1          # explicit; do NOT rely on a DHCP next-server
tftpboot 0x10000000 image.ub      # -> "Bytes transferred = <FIT size>"
bootm 0x10000000                  # -> kernel boots -> "SimpleRfSoc4x2Example-<mode> login:"
```
- Load address **`0x10000000`** (project-proven, from the repo's own `boot.scr`) — **not**
  the generic `loadaddr=0x08000000` / `kernel_addr_r=0x18000000` defaults.
- **U-Boot 2026.01**; active net stack = **lwIP (`CONFIG_NET_LWIP`)**, confirmed by the
  `DHCP client bound to address … (N ms)` message.
- The env macro to edit for the hooks is **`CFG_EXTRA_ENV_SETTINGS`** (not
  `CONFIG_EXTRA_ENV_SETTINGS`) in `include/configs/xilinx_zynqmp.h` for the pinned
  `xilinx-v2026.1` U-Boot.
- The full ~112 MiB FIT transfers cleanly over lwIP TFTP (no throughput/blocksize problem).

### Boot-mode switch (build-time)
`shared/Yocto/recipes-bsp/u-boot/u-boot-xlnx_%.bbappend` substitutes `@UBOOT_NETBOOT_MODE@`
in `platform-top.h`, so the mode is baked into U-Boot (i.e. into `BOOT.BIN`):
```
bootcmd = if run netboot; then true; else @UBOOT_NETBOOT_MODE@; fi
```
| `UBOOT_NETBOOT_MODE` | else-branch substitution | on TFTP failure |
|----------------------|--------------------------|-----------------|
| `fallback` (default) | `run sdboot`             | boots the on-SD `image.ub` |
| `tftp-only`          | `echo TFTP-only build: not falling back to SD` | prints the message and halts at `ZynqMP>` |

The two build modes produce byte-distinct `BOOT.BIN` and `image.ub`:

| mode      | `BOOT.BIN` md5                     | `image.ub` size |
|-----------|------------------------------------|-----------------|
| fallback  | `87be1df7715527b53c9e7acdc696ca17` | `116833531`     |
| tftp-only | `e5bb9930f864f9d7cc749261a95f6fc9` | `116833887`     |

On the committed build the login banner is `SimpleRfSoc4x2Example` for **both** modes: the
hostname is set from the project name (`hostname:pn-base-files = "$Name"`), independent of
`UBOOT_NETBOOT_MODE`, so it is **not** a mode distinguisher. Tell which build actually booted
from the byte-distinct `BOOT.BIN` md5 (the two modes differ — see the table above), or from
the TFTP-failure behavior itself (a `fallback` build SD-boots; a `tftp-only` build halts at
`ZynqMP>`).

### Static-IP override (no-DHCP path)
At `ZynqMP>`, set `ipaddr` (e.g. `10.0.0.50` — outside the dhcpd pool, not a fixed host,
not the gateway) plus `serverip`/`gatewayip`/`netmask`, then `run netboot`: DHCP is skipped
entirely (zero `DHCP client bound` lines) yet the FIT is still fetched and the board reaches
`login:`. Leave it **volatile** (no `saveenv`) so a plain `reset` restores the DHCP path.

---

## 4. Remote SD reflash

### JTAG recovery net — `scripts/reflash_via_jtag.tcl`
- Provides a warm-inject path to a live `ZynqMP>` prompt over JTAG (`xsdb`/`hw_server`),
  used as the ultimate recovery net.
- `warm_inject_reflash` requires **`configparams force-mem-accesses 1` before `dow`**
  (physical-address writes; otherwise the debugger's writes route through the halted core's
  active MMU and fault with `Memory write error … MMU fault`).
- **Single-core-U-Boot-only:** never run it against a fully-booted multi-core Linux (halting
  A53#0 while the other cores run an SMP kernel triggers RCU stalls). Recover with `con`,
  never `rst -system`.

### U-Boot `fatwrite` is broken on this board — reflash from Linux instead
`fatwrite mmc 0:1` under **U-Boot 2026.01** is broken on this board's 1 GiB FAT32 boot
partition: any write (even 4096 bytes) fails with a bogus `no space left` / `overflow`,
because the FAT32 **FSInfo free-count is corrupt** (the reported free byte-count is larger
than the file being written); `fatrm`-first does **not** fix it. **Never `fatwrite`, never
raw `mmc write`** (SD-corruption/brick risk).

**Working remote SD-reflash path = the Linux kernel VFAT driver (`cp`):**
```
# On the running board (root over serial); host serves the target files via TFTP on 10.0.0.1
mount | grep mmcblk0p1                 # /dev/mmcblk0p1 on /boot type vfat  (rootfs is on mmcblk0p2)
cd /tmp
tftp -g -r BOOT.BIN -l /tmp/BOOT.BIN 10.0.0.1
md5sum /tmp/BOOT.BIN                   # verify against the built artifact BEFORE writing
cp /tmp/BOOT.BIN /boot/BOOT.BIN ; sync
cmp /tmp/BOOT.BIN /boot/BOOT.BIN       # on-SD byte-identical
```
- Identify the SD FAT partition **by content** (holds `BOOT.BIN`/`boot.scr`/`system.bit`/`ssh`),
  never by an assumed device node.
- A clean `reboot`/`umount` rewrites the corrupt FSInfo correctly (`fsck.vfat` is unavailable).
- **To switch netboot mode it is sufficient to replace only `BOOT.BIN`** — the mode lives in
  U-Boot (`platform-top.h`), and the SD `image.ub`/`boot.scr` are not consulted by the
  tftp-only build (it fetches the FIT over TFTP and, on failure, halts without touching SD).

---

## 5. Fallback vs no-fallback behavior (validated on hardware)

Both build modes were exercised on the board with TFTP made unreachable two ways.

### Fallback build (`UBOOT_NETBOOT_MODE=fallback`), TFTP down → boots SD
On a failed `run netboot`, the else-branch runs `sdboot`, which boots the on-SD `image.ub`
to a kernel banner and `SimpleRfSoc4x2Example-fallback login:`. Two TFTP-down mechanisms,
two **distinct** bounded timeouts (keep them separate):

| Mechanism | Host TFTP state | Serial signature | Bounded fallback time |
|-----------|-----------------|------------------|-----------------------|
| dnsmasq stopped (port closed) | reachable, port closed | `ICMP destination unreachable (port unreachable)` ×6 → `TFTP error: -1 (Request timeout)` | **≈ 6 s** (≈ the 6.2 s host-reachable/port-closed figure) |
| board-scoped silent DROP (black hole) | reachable, packets dropped | **no ICMP** (silent) → `TFTP error: -1 (Request timeout)` | **≈ 5 s** |

- In both cases **no `Bytes transferred` line appears** — nothing is fetched, so the booted
  kernel can only have come from SD.
- The silent black-hole timeout (≈ 5 s) is **comparable to, and slightly under**, the
  port-closed case — it is **not** longer: lwIP uses a single TFTP request-timeout regardless
  of whether ICMP feedback is received. Do not assume the two numbers are equal, and do not
  assume the black hole is slower.
- The black-hole run uses a firewall rule scoped to the board's current lease only, paired
  with its own removal + verification so the shared lab TFTP is restored:
  ```
  sudo iptables -A INPUT -s <board-ip> -p udp --dport 69 -j DROP
  # ... trigger one netboot, observe the silent timeout + SD fallback ...
  sudo iptables -D INPUT -s <board-ip> -p udp --dport 69 -j DROP
  sudo iptables -L INPUT -v | grep <board-ip>     # must be empty afterward
  ```

### tftp-only build (`UBOOT_NETBOOT_MODE=tftp-only`), TFTP down → halts, no SD
On the **identical** TFTP failure, the tftp-only build's else-branch prints the literal
line and then **halts at a bare `ZynqMP>` prompt** — no kernel banner, no `run sdboot`, no
SD boot:
```
TFTP error: -1 (Request timeout)
TFTP-only build: not falling back to SD
ZynqMP>
```
This is the decisive contrast with the fallback build: same failure, opposite outcome.
With TFTP **up**, the tftp-only build netboots normally (`Bytes transferred = 116833887` →
`## Loading kernel (any) from FIT Image at 10000000` → `SimpleRfSoc4x2Example-tftponly login:`).

### Recovering a halted tftp-only board
```
sudo dnsmasq --conf-file=/etc/dnsmasq.d/tftp-lab.conf --pid-file=/run/dnsmasq-tftp-lab.pid   # TFTP back up
```
then `reset` at the `ZynqMP>` prompt → the tftp-only build netboots normally again. If the
board is unresponsive, the JTAG warm-inject path (`reflash_via_jtag.tcl`) is the ultimate
recovery net. This board also has remote power control.

---

## 6. PXE-config-driven netboot (validated on hardware)

Added after the bring-up above, in response to the PR review. The netboot hook is now
**PXE-first**: `pxe get` fetches a pxelinux config from `${serverip}` and `pxe boot` loads
and boots the FIT that config names; the direct `tftpboot 0x10000000 image.ub && bootm
0x10000000` stays as the in-`netboot` fallback for when no PXE config is served. The
expanded `netboot` env value:
```
if test -n "${ipaddr}"; then true; else dhcp; fi && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi
```

- **Config search order** (built into `pxe get`): the MAC-based name
  `pxelinux.cfg/01-<mac, dash-separated, lowercase>` first, then IP-hex names, then
  `pxelinux.cfg/default`.
- **Staged file** `/tftpboot/pxelinux.cfg/default` (written idempotently by
  `provision_tftp_host.sh`), exact content:
  ```
  LABEL Linux
  KERNEL image.ub
  ```
  `KERNEL image.ub` points at the same flat `/tftpboot/image.ub` staged for the
  direct-fetch fallback.
- **`serverip` is still required** (explicit or via DHCP) — PXE does not remove that
  dependency; it is the same requirement as the direct-tftp path in Section 3.
- **Load addresses come from `ENV_MEM_LAYOUT_SETTINGS`** (`configs/xilinx_zynqmp.h`), not
  hand-set: `pxefile_addr_r=0x10000000`, `kernel_addr_r=0x18000000` (verified in the
  u-boot-xlnx 2026.01 source).
- **No Kconfig added.** `CONFIG_CMD_PXE`, `CONFIG_PXE_UTILS`, and `CONFIG_MENU` are already
  enabled by the ZynqMP defconfig (via `CONFIG_DISTRO_DEFAULTS`) — verified in the built
  `.config` — and `bootcmd_pxe` (`run boot_net_usb_start; dhcp; if pxe get; then pxe boot;
  fi`) is present in the built default environment. `bsp.cfg` only carries a comment noting
  this so a future reader need not re-derive it.
- **Validated on hardware (RFSoC 4x2, both build modes).** The full PXE fetch/boot path was
  exercised on the board; every case below was observed on the serial console, and the
  reflashed builds' baked-in env (`printenv netboot`/`bootcmd`) byte-matches the strings in
  this section. The `bootcmd` wrappers built for each mode are exactly
  `if run netboot; then true; else run sdboot; fi` (fallback) and
  `if run netboot; then true; else echo TFTP-only build: not falling back to SD; fi`
  (tftp-only).
  - *PXE default happy path* — `run netboot` → `pxe get` fetches `pxelinux.cfg/default` →
    `pxe boot` loads the `KERNEL` FIT at `kernel_addr_r` (`0x18000000`) → boots to `login:`.
    The observed `pxe get` search order matches the documented one exactly: MAC
    (`pxelinux.cfg/01-fc-c2-3d-5a-9a-08`) → descending IP-hex (`0A00009B`…`0A`) →
    `default-<arch>`… → `default`.
  - *MAC-specific override* — a `pxelinux.cfg/01-<mac>` file naming a different FIT is taken
    in preference to `default`; the board boots the FIT the MAC file names.
  - *No PXE config served* — `pxe get` fails and the in-`netboot` fallback
    `tftpboot 0x10000000 image.ub && bootm 0x10000000` runs (FIT at `0x10000000`) → boots.
  - *`bootcmd` else-branch on total netboot failure* (the previously-open question) — with
    TFTP down, `pxe get` fails **and** the fallback `tftpboot` also fails, so `netboot`
    returns nonzero and `bootcmd`'s else-branch runs: **fallback → `run sdboot` (SD boot);
    tftp-only → prints `TFTP-only build: not falling back to SD` and halts at `ZynqMP>`.**
    Same decisive fallback-vs-halt contrast as Section 5, now with the PXE-first hook in
    front. Confirmed under both a port-closed host (dnsmasq stopped) and a MAC-scoped
    silent black hole.
  - Static-IP override (DHCP skipped, `ipaddr` set) and the distro `run bootcmd_pxe`
    one-liner also PXE-boot as expected.
- **Silent-black-hole failure is now slow (~110 s); the port-closed case stays fast
  (~6 s).** This is a behavioral change the PXE-first hook introduces relative to Section
  5's pre-PXE figures, and it is worth accounting for wherever a bounded fallback time
  matters. When TFTP packets are silently dropped, each of `pxe get`'s ~12 config-name
  probes (MAC + IP-hex prefixes + `default-*` variants) waits its full TFTP request timeout
  with no ICMP to short-circuit it, so `netboot` takes ~110 s to fail before the `bootcmd`
  else-branch (SD fallback or halt) runs. The port-closed case (dnsmasq down) stays ~6 s
  because ICMP destination-unreachable aborts each probe immediately. Section 5's ~5 s
  silent-black-hole figure was for the single pre-PXE `tftpboot`; the PXE-first worst case
  under a silent drop is ~110 s.

---

## 7. PL bitstream over TFTP (tftp-only / diskless)

Added for the diskless-QSPI use case: a `tftp-only` build has no SD card, so the runtime
`fpgautil -b /boot/system.bit` load in `startup-app-init` (which needs the SD FAT mounted at
`/boot`) never runs. To keep the PL updatable server-side without reflashing QSPI, U-Boot now
fetches the bitstream over TFTP and `fpga load`s it before booting the kernel — but **only in
`tftp-only` mode**. `fallback` builds are untouched (SD `fpgautil` still owns the PL).

### Where the PL bitstream comes from today (the gap this closes)
- **`BOOT.BIN` embeds the bitstream** — FSBL programs the PL from SD/QSPI at boot. Confirmed
  by size: RFSoC 4x2 `BOOT.BIN` is **36 382 960 B** vs `system.bit` **34 437 578 B**
  (`BOOT.BIN` = FSBL+PMU+ATF+U-Boot + the ~34 MB bitstream). So a diskless QSPI board is
  frozen at the baked-in PL until QSPI is reflashed.
- **Runtime reload** — on an SD boot, `startup-app-init` mounts the SD FAT at `/boot` and runs
  `fpgautil -b /boot/system.bit` (the "2nd stage" load). No SD ⇒ no `/boot` ⇒ this is skipped.
- The `tftp-only` `fpga load` **re-programs the PL over** the FSBL bitstream. Halt-on-failure
  (below) means the net kernel never boots over the FSBL/stale PL — so there is intentionally
  **no reliance on the BOOT.BIN bitstream as a fallback**.

### Mechanism (mode-gated, in the shared U-Boot env)
`platform-top.h` defines two helper env vars; the `u-boot-xlnx_%.bbappend` substitutes
`@LOADPL_SEL@` in `netboot` at build time (a **bareword** swap, so no `&&` ever lands in a
`sed` replacement):

| `UBOOT_NETBOOT_MODE` | `@LOADPL_SEL@` → | effect |
|----------------------|------------------|--------|
| `tftp-only`          | `loadpl_net`     | fetch bitstream + `fpga load` before the kernel; **mandatory** |
| `fallback` (default) | `loadpl_skip` (`true`) | no-op; SD `fpgautil` owns the PL (no double-program) |

```
loadpl_net = setexpr macfn gsub : - ${ethaddr} && if tftpboot 0x10000000 system.bit.bin.${macfn}; then true; else tftpboot 0x10000000 system.bit.bin; fi && fpga load 0 0x10000000 ${filesize}
netboot    = if test -n "${ipaddr}"; then true; else dhcp; fi && run @LOADPL_SEL@ && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi
```

- **Halt-on-failure by `&&` chaining.** In `tftp-only`, a failed bitstream fetch **or** a
  failed `fpga load` makes `run loadpl_net` return nonzero, so `netboot` aborts *before* any
  kernel fetch/boot and `bootcmd`'s else-branch (`echo TFTP-only build…` → halt) runs. A net
  kernel never boots over an unprogrammed PL. PXE-first kernel boot is preserved in both modes.
- **Address reuse.** `0x10000000` is reused sequentially: `fpga load` consumes the buffer
  (PCAP program) before the kernel FIT is fetched to the same address. `${filesize}` is set by
  whichever `tftpboot` (per-MAC or generic) succeeded, and is consumed by `fpga load` before
  the next `tftpboot` overwrites it.
- **Per-MAC naming is dash-form** (`system.bit.bin.fc-c2-3d-5a-9a-08`), then falls back to
  generic `system.bit.bin`. U-Boot's `tftpboot` parses the first `:` in a filename as a
  `hostIP:file` separator, so `system.bit.bin.${ethaddr}` (colon form) is silently mis-parsed
  and never fetched (observed on HW — see below). `loadpl_net` therefore runs
  `setexpr macfn gsub : - ${ethaddr}` to rewrite colons to dashes, then fetches
  `system.bit.bin.${macfn}`. `setexpr gsub` needs `CONFIG_CMD_SETEXPR` + `CONFIG_REGEX` (both
  on in the ZynqMP defconfig — verified in the built `.config`).
- **Kconfig pinned** in `bsp.cfg`: `CONFIG_FPGA`, `CONFIG_FPGA_XILINX`, `CONFIG_FPGA_ZYNQMPPL`,
  `CONFIG_CMD_FPGA` (normally already on via the ZynqMP defconfig; pinned since the feature
  hard-depends on `fpga load`). Verify present in the built `.config`.

### Host staging
`provision_tftp_host.sh -B` converts `linux/system.bit` → raw `.bin`
(`bootgen -arch zynqmp -image <bif> -process_bitstream bin`; needs Vitis/Vivado `bootgen` on
`PATH`) and stages `/tftpboot/system.bit.bin`; `-M <mac>` (repeatable) adds per-MAC copies.
Idempotent (`cmp`-guarded) like the `image.ub`/PXE steps. Fallback-mode servers omit `-B`/`-M`.

### `startup-app-init` backstop
Before the DMA `insmod`s, it checks `/sys/class/fpga_manager/fpga0/state`; if not `operating`
it prints `ERROR: PL not programmed …` and skips the driver load (clear message instead of
cryptic DMA failures). NOTE: the **primary** guard is the U-Boot `&&`-chain halt; this state
check can read `operating` from the FSBL bitstream even if `fpga load` failed, so it is a
secondary backstop, not the mode guard.

### Hardware validation — Stage A (SD-simulated diskless), all PASS on the RFSoC 4x2
Method: reflash the SD `BOOT.BIN` in place with the tftp-only build (Linux VFAT `cp`), rename
`/boot/system.bit` → `system.old` so the runtime `fpgautil` step is skipped (isolating the PL
to the U-Boot `fpga load`), stage `image.ub` + `system.bit.bin` (+ per-MAC) with
`provision_tftp_host.sh -B -M`, boot, and capture serial. `${filesize}` is set by `tftpboot`
under lwIP (confirmed — `fpga load` consumed it). Observed:

- **Per-MAC bitstream fetch + fpga load + net kernel boot.** `DHCP client bound` →
  `Filename 'system.bit.bin.fc-c2-3d-5a-9a-08'` → `Bytes transferred = 34437356` → `fpga load`
  (silent, `FPGA_RC=0` confirmed at the prompt) → `pxe get` finds `pxelinux.cfg/default` → kernel
  FIT at `0x18000000` → `SimpleRfSoc4x2Example-tftponly login:`. In Linux the DMA drivers bound
  (`axi_stream_dma … Found Version 2 Device`) and `axiversiondump` read `Root.AxiVersion`
  (`FwVersion 0x3030000`) — i.e. the PL is programmed and the app runs.
- **Per-MAC → generic fallback.** With the per-MAC file removed:
  `Filename 'system.bit.bin.fc-c2-3d-5a-9a-08'` → `TFTP error: 256 (… not found)` →
  `Filename 'system.bit.bin'` → `Bytes transferred = 34437356` → boots. The `if…else…fi` falls
  through exactly as intended.
- **Halt-on-failure (tftp-only).** With **both** bitstream files removed:
  per-MAC MISS → generic MISS → `TFTP-only build: not falling back to SD` → **halt at
  `ZynqMP>`**, and the kernel FIT is **never fetched** (the `&&` chain aborts inside `loadpl_net`,
  before the kernel) — never a net kernel over an unprogrammed PL. Restaging the bitstream and
  `reset` recovers it.

**LANDMINE (found on HW): a colon-form per-MAC name does not work.** The first cut used
`system.bit.bin.${ethaddr}` (colon form). U-Boot's `tftpboot` parses the first `:` in a filename
as a `hostIP:file` separator, so it silently skipped the per-MAC attempt and went straight to the
generic name — no per-MAC line, no error. Fix: `setexpr macfn gsub : - ${ethaddr}` (validated
live: `ethaddr=fc:c2:3d:5a:9a:08` → `macfn=fc-c2-3d-5a-9a-08`) and request
`system.bit.bin.${macfn}`; `provision_tftp_host.sh -M` stores the dash form to match.

Note: `fpga0/state` read `operating` here even in the disabled-SD case, because the tftp-only
`BOOT.BIN` still carries an FSBL bitstream that programs the PL before U-Boot runs — so the
state check cannot by itself prove the `fpga load` ran. The decisive proof is the `&&`-chain
halt (a failed `fpga load` would have halted before Linux) plus `FPGA_RC=0` at the prompt.

The literal no-SD QSPI diskless boot (Stage B) is deferred by decision — not run.
