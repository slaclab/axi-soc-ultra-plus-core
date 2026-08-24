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
(`/etc/dhcp/dhcpd.conf`, `INTERFACES="eth1 eth2"`) already serves it and is **shared across
the dev boards**:

```
subnet 10.0.0.0 netmask 255.255.255.0 {
  range 10.0.0.151 10.0.0.199;
  option routers 10.0.0.1;         # this host (rdsrv403)
  host dev_zcu111   { fixed-address 10.0.0.10;  }
  host dev_zcu208   { fixed-address 10.0.0.11;  }
  host dev_zcu216   { fixed-address 10.0.0.12;  }
  host dev_rfsoc4x2 { fixed-address 10.0.0.13;  }   # fc:c2:3d:5a:9a:08
  host slac_test1   { fixed-address 10.0.0.200; }
}
```

The RFSoC 4x2 (`fc:c2:3d:5a:9a:08`) now has a **static reservation** at `10.0.0.13`, so its
address is stable across reboots. It does **not** advertise `next-server`/`filename`, so
`tftpserverip` is never set from DHCP and `serverip` resolves to the DHCP server's own
address (`10.0.0.1`), which is also the TFTP server here. A board *without* a reservation
takes a dynamic lease from `.151–.199` that rotates between reboots — for those, **never
gate pass/fail on the lease address**; gate on `serverip` and on the observable boot outcome.

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

### `serverip` vs. lwIP `dhcp` — save/restore (PR #107 review)
lwIP `dhcp` **unconditionally overwrites `serverip`** with the DHCP server's own address on
every successful bind, and additionally sets `tftpserverip` from the DHCP next-server (siaddr)
field when one is advertised. Both `tftpboot` and the PXE fetch resolve the TFTP server in
priority order: explicit `ip:` prefix → `tftpserverip` → `serverip` — so a DHCP-provided
`tftpserverip` outranks a manually-set `serverip`, and a `saveenv`-persisted `serverip` is
clobbered by every `dhcp`. The legacy `CONFIG_BOOTP_SERVERIP` knob that suppresses this in the
old net stack **does not exist in the lwIP DHCP path**, so there is no Kconfig fix — it is
handled in the shipped U-Boot env. `netboot` therefore treats a **non-empty `serverip` as
authoritative site configuration**: it stashes `serverip` (scratch var `_sip`), runs `dhcp`,
then restores `serverip` and clears `tftpserverip`. A failed `dhcp` short-circuits the `&&`
chain *before* the restore — correct, because nothing was bound or clobbered — and preserves
the fail-fast timeout. **Practical rule: to hand TFTP addressing back to DHCP, clear `serverip`**
(`setenv serverip`, optional `saveenv`). This covers the reviewer's mixed case — board IP from
DHCP, but `serverip` set manually because the site DHCP advertises a wrong next-server.

**Verified on hardware (RFSoC 4x2, manual env at `ZynqMP>`):** with a manual
`serverip=1.2.3.4`, a plain `dhcp` overwrote it to the DHCP server address `10.0.0.1`
(clobber reproduced); the shipped save/restore
(`setenv _sip ${serverip}; dhcp && setenv serverip ${_sip} && setenv tftpserverip && setenv _sip`)
left `serverip=1.2.3.4`, cleared `tftpserverip` (undefined afterward), and cleaned `_sip`.

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
bootm 0x10000000                  # -> kernel boots -> "SimpleRfSoc4x2Example login:"
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
`shared/Yocto/recipes-bsp/u-boot/u-boot-xlnx_%.bbappend` substitutes `@BOOTCMD_SEL@`
in `platform-top.h`, so the mode is baked into U-Boot (i.e. into `BOOT.BIN`):
```
bootcmd = @BOOTCMD_SEL@
```
The placeholder is the **whole** `bootcmd` value, not just a trailing mode action, so a mode
can decline to run `netboot` at all. (The BitBake variable is still named
`UBOOT_NETBOOT_MODE`; only the in-file placeholder is `@BOOTCMD_SEL@`.)

| `UBOOT_NETBOOT_MODE` | `@BOOTCMD_SEL@` substitution | boot behavior |
|----------------------|------------------------------|---------------|
| `sd-only` (default)  | `echo SD-only build: skipping netboot; run sdboot` | never attempts DHCP or TFTP; boots SD directly |
| `fallback`           | `run netboot; run sdboot`    | on TFTP failure, boots the on-SD `image.ub` |
| `tftp-only`          | `run netboot; echo TFTP-only build: not falling back to SD` | on TFTP failure, prints the message and halts at `ZynqMP>` |

`sd-only` exists because the netboot path is expensive when it cannot succeed: see §5 for the
measured ~85-110 s of TFTP timeouts a `fallback` board pays on every boot on a network with a
reachable DHCP server and no reachable TFTP server. Its leading `echo` is deliberate, not
decoration: a bare `run sdboot` would be textually identical to the stock upstream
`run distro_bootcmd`, so the `echo` is what gives the mode a unique `strings BOOT.BIN`
signature and a serial-console one.

`netboot`, `loadpl_net`, and `loadpl_skip` are defined **unconditionally** in
`platform-top.h`, in all three modes. Only `bootcmd` changes, so `run netboot` by hand at
`ZynqMP>` remains available on an `sd-only` board for recovery.

**Sequential, not exit-code-gated (PR #107 review).** In the two modes whose `bootcmd`
contains `run netboot` (`fallback`, `tftp-only`), `bootcmd` runs `netboot` and then the
mode action **unconditionally in sequence** — it does *not* branch on `run netboot`'s exit
status. The invariant: a successful boot hands control to the kernel and **never returns to the
U-Boot shell**, so merely reaching the mode action proves netboot failed. This is robust even
for boot methods that report a dishonest exit code: upstream `pxe boot` returns **0** after a
failed `label_boot()` (kernel retrieval failed) because `handle_pxe_menu()` is `void` and
`pxe_process()` discards the nonzero (verified in `boot/pxe_utils.c` / `cmd/pxe.c` of the pinned
`xilinx-v2026.1` tag). Because `netboot` is PXE-first, an exit-code-gated `bootcmd`
(`if run netboot; then true; else ...; fi`) would read that lie as success and **silently skip
the fallback**; the sequential form closes that gap without patching U-Boot. The decisive
`pxe get` succeeds / `pxe boot` returns-0 case is re-validated on hardware in §6.

The three build modes produce byte-distinct `BOOT.BIN` and `image.ub`. Exact sizes and md5s
change with every rebuild, so **do not compare against committed figures** — read the mode
out of the artifact instead. The baked-in `bootcmd` is the definitive discriminator and never
goes stale:

```
strings -a BOOT.BIN | grep -a '^bootcmd='
  bootcmd=echo SD-only build: skipping netboot; run sdboot          -> sd-only
  bootcmd=run netboot; run sdboot                                  -> fallback
  bootcmd=run netboot; echo TFTP-only build: not falling back to SD -> tftp-only
```
`strings -a BOOT.BIN | grep -aE '^(netboot|loadpl_net|loadpl_skip)='` is a **weaker** check
and no longer identifies a mode on its own: `run loadpl_net` means `tftp-only`, but
`run loadpl_skip` is selected by **both** `fallback` and `sd-only`, so it cannot tell those
two apart. Anything (or anyone) using that grep as a mode check will misread an `sd-only`
board as `fallback`. Only the `bootcmd=` line above is conclusive. Both work on a
built artifact *or* on the SD copy at `/boot/BOOT.BIN`, with no board involved. For scale, the
build current at the time of writing produced `image.ub` = `116835243` B and `BOOT.BIN` =
`36383360` B in `fallback` mode.

On the committed build the login banner is `SimpleRfSoc4x2Example` for **all** modes: the
hostname is set from the project name (`hostname:pn-base-files = "$Name"`), independent of
`UBOOT_NETBOOT_MODE`, so it is **not** a mode distinguisher. Besides the `bootcmd` check
above, runtime behavior also tells the modes apart (a `fallback` build SD-boots after its
TFTP attempts fail; a `tftp-only` build halts at `ZynqMP>`; an `sd-only` build prints
`SD-only build: skipping netboot` and emits no `DHCP client bound` or TFTP lines at all).

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

Both **netboot** build modes (`fallback`, `tftp-only`) were exercised on the board with TFTP
made unreachable two ways. `sd-only` was added after this section was written and is **not
covered by any measurement here** — see "sd-only is not yet hardware-validated" at the end of
§8.

### Fallback build (`UBOOT_NETBOOT_MODE=fallback`), TFTP down → boots SD
On a failed `run netboot`, the else-branch runs `sdboot`, which boots the on-SD `image.ub`
to a kernel banner and `SimpleRfSoc4x2Example login:`. Two TFTP-down mechanisms,
two **distinct** bounded timeouts (keep them separate).

> **These are per-`tftpboot` figures, measured before the PXE-first hook existed.** They are
> still correct per attempt, but they are **not** the end-to-end fallback time on the current
> code: `pxe get` now issues 13 config-name attempts ahead of the FIT fetch, each paying the
> timeout below. See §6 for the ~85–110 s aggregate.

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
With TFTP **up**, the tftp-only build netboots normally (`Bytes transferred = <FIT size>` →
`## Loading kernel (any) from FIT Image at 10000000` → `SimpleRfSoc4x2Example login:`).

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
expanded `netboot` env value (as shipped today, including the `serverip` save/restore from
§2 and the `@LOADPL_SEL@` bitstream hook from §7 — `run loadpl_skip` shown here for a
`fallback` build):
```
if test -n "${ipaddr}"; then true; else if test -n "${serverip}"; then setenv _sip ${serverip}; dhcp && setenv serverip ${_sip} && setenv tftpserverip && setenv _sip; else dhcp; fi; fi && run loadpl_skip && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi
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
- **Validated on hardware (RFSoC 4x2, both netboot build modes).** The full PXE fetch/boot path was
  exercised on the board; every case below was observed on the serial console, and the
  reflashed builds' baked-in env (`printenv netboot`/`bootcmd`) byte-matches the strings in
  this section. The `bootcmd` wrappers built for each mode at the time were exactly
  `if run netboot; then true; else run sdboot; fi` (fallback) and
  `if run netboot; then true; else echo TFTP-only build: not falling back to SD; fi`
  (tftp-only).

  **Update (PR #107 review) — sequential `bootcmd`.** `bootcmd` has since been changed to the
  exit-code-agnostic sequential form `run netboot; <mode-action>` (fallback →
  `run netboot; run sdboot`; tftp-only → `run netboot; echo TFTP-only build: not falling back to
  SD`). The validation recorded here was performed with the earlier exit-code-gated wrapper; the
  sequential form is behaviorally identical for every case below (a successful boot never returns
  to the shell, so the mode action still runs only on failure) and additionally closes the one
  case the old wrapper could not — `pxe get` **succeeds** but `pxe boot` fails to retrieve the
  kernel and returns 0, where `if run netboot; then true; else ...; fi` reads the lie as success
  and skips the fallback. The upstream `pxe boot` exit-0 behavior is confirmed in the pinned
  `xilinx-v2026.1` source; the decisive case is additionally re-checked on the RFSoC 4x2 in this
  session (see the added case in the validated list below).
  - *PXE default happy path* — `run netboot` → `pxe get` fetches `pxelinux.cfg/default` →
    `pxe boot` loads the `KERNEL` FIT at `kernel_addr_r` (`0x18000000`) → boots to `login:`.
    The observed `pxe get` search order matches the documented one exactly: MAC
    (`pxelinux.cfg/01-fc-c2-3d-5a-9a-08`) → descending IP-hex (`0A00000D`…`0`, for
    `10.0.0.13`) → `default-<arch>`… → `default`, 13 names in total.
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
  - *`pxe boot` exit-0 lie / sequential-`bootcmd` gap (PR #107 review, re-validated on
    the RFSoC 4x2)* — with a board-MAC `pxelinux.cfg/01-fc-c2-3d-5a-9a-08` naming a
    **nonexistent** kernel FIT, `pxe get` succeeded (`GET_RC=0`, 41-byte config fetched)
    but `pxe boot` failed to retrieve the kernel (`TFTP error: 256 (... not found)` →
    `Skipping Linux for failure retrieving kernel`) and **still returned `BOOT_RC=0`** —
    the upstream lie, reproduced. Consequences observed at `ZynqMP>`: the old
    exit-code-gated wrapper (`if run netboot; then true; else ...; fi`) took the `true`
    branch and **skipped the fallback** (`OLD_SAID_OK_NO_FALLBACK`), while the new
    sequential `bootcmd` (`run netboot; <mode-action>`) **always ran the mode action**
    (`NEW_FALLBACK_RAN`) — the gap is closed without patching U-Boot. The reviewer fixes
    were exercised by loading the new env at the prompt (the board's SD `BOOT.BIN` was an
    older stock build predating the hooks, so a baked-in `printenv` byte-match awaits the
    next full rebuild+reflash). The positive path was also re-run end-to-end: the full new
    `netboot` (serverip preserved across `dhcp`) → `pxe get` default → `pxe boot` →
    `SimpleRfSoc4x2Example login:`.
- **PXE-first multiplies every TFTP-failure timeout by the probe count (~85–110 s).** This is
  a behavioral change the PXE-first hook introduces relative to Section 5's pre-PXE figures,
  and it matters wherever a bounded fallback time does. `pxe get` walks **13** config names
  (MAC → 9 IP-hex prefixes → 3 `default-*` variants → `default`), and the in-`netboot`
  `tftpboot image.ub` adds a 14th attempt. Whether an attempt is cheap or expensive depends
  only on whether the server *answers*:

  | Host TFTP state | Per-attempt cost | 14 attempts |
  |-----------------|------------------|-------------|
  | up, file absent | immediate `TFTP error: 256` NAK | negligible |
  | down (port closed) | full request timeout, ~6 s | **≈ 85 s** |
  | silent DROP (black hole) | full request timeout | ≈ 110 s |

  **ICMP does not short-circuit a probe.** An earlier revision of this section claimed the
  port-closed case "stays fast (~6 s) because ICMP destination-unreachable aborts each probe
  immediately." That is wrong. Re-measured on the RFSoC 4x2 with dnsmasq stopped: the ICMP
  *does* arrive — 6 `ICMP destination unreachable (port unreachable)` lines per attempt, as
  Section 5 records — and each attempt **still** ends in `TFTP error: -1 (Request timeout)`.
  Boot-to-SSH took **134.5 s**, against **50.0 s** for the same 14 attempts with the server up
  and NAK'ing: a delta of 84.5 s over 14 attempts, i.e. ≈ 6.0 s each. Section 5's ≈ 5–6 s
  figures are per-`tftpboot` and remain correct; what changed is that PXE-first fires fourteen
  of them, so port-closed and black-hole failures now cost the same order of magnitude rather
  than differing by ~20×.

  Note this cost is paid by a `fallback` board on **every** boot on a net with no TFTP server,
  which is the usual idle-lab state. For a board that is *supposed* to netboot, staging
  `pxelinux.cfg/01-<mac-dashes>` collapses the 13-name search to a single hit (it is the first
  name probed) and is the cheapest mitigation; `provision_tftp_host.sh` currently writes only
  `pxelinux.cfg/default`. For a board that will **never** have a TFTP server, the cheaper
  answer is to stop netbooting: build it `-m sd-only` (§3), whose `bootcmd` contains no
  `run netboot` and therefore makes zero DHCP and zero TFTP attempts by construction.

  The full 14-attempt cost also requires a **reachable DHCP server**. `netboot` opens with
  `dhcp` in an `&&` chain, so on a net with no DHCP server at all it short-circuits at the DHCP
  timeout and never reaches the TFTP attempts (that DHCP-fail timeout is itself unmeasured).
  Do not quote the ~85-110 s figure for the no-DHCP case.

---

## 7. PL bitstream over TFTP (tftp-only / diskless)

Added for the diskless-QSPI use case: a `tftp-only` build has no SD card, so the runtime
`fpgautil -b /boot/system.bit` load in `startup-app-init` (which needs the SD FAT mounted at
`/boot`) never runs. To keep the PL updatable server-side without reflashing QSPI, U-Boot now
fetches the bitstream over TFTP and `fpga load`s it before booting the kernel — but **only in
`tftp-only` mode**. `fallback` builds are untouched (SD `fpgautil` still owns the PL).

### Where the PL bitstream comes from today (the gap this closes)
- **`BOOT.BIN` embeds the bitstream** — FSBL programs the PL from SD/QSPI at boot. Confirmed
  by size: RFSoC 4x2 `BOOT.BIN` is **36 383 360 B** vs `system.bit` **34 437 578 B**
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
| `fallback`           | `loadpl_skip` (`true`) | no-op; SD `fpgautil` owns the PL (no double-program) |
| `sd-only` (default)  | `loadpl_skip` (`true`) | same no-op, but moot on the boot path: `bootcmd` never runs `netboot`. Only reachable via a manual `run netboot` |

```
loadpl_net = setexpr macfn gsub : - ${ethaddr} && if tftpboot 0x10000000 system.bit.${macfn}; then true; elif tftpboot 0x10000000 system.bin.${macfn}; then true; elif tftpboot 0x10000000 system.bit; then true; else tftpboot 0x10000000 system.bin; fi && fpga load 0 0x10000000 ${filesize}
netboot    = if test -n "${ipaddr}"; then true; else if test -n "${serverip}"; then setenv _sip ${serverip}; dhcp && setenv serverip ${_sip} && setenv tftpserverip && setenv _sip; else dhcp; fi; fi && run @LOADPL_SEL@ && if pxe get; then pxe boot; else tftpboot 0x10000000 image.ub && bootm 0x10000000; fi
```

- **Halt-on-failure by `&&` chaining.** In `tftp-only`, a failed bitstream fetch **or** a
  failed `fpga load` makes `run loadpl_net` return nonzero, so `netboot` aborts *before* any
  kernel fetch/boot and `bootcmd`'s else-branch (`echo TFTP-only build…` → halt) runs. A net
  kernel never boots over an unprogrammed PL. PXE-first kernel boot is preserved in both
  netboot modes (in `sd-only` nothing on the boot path reaches `netboot` at all).
- **Address reuse.** `0x10000000` is reused sequentially: `fpga load` consumes the buffer
  (PCAP program) before the kernel FIT is fetched to the same address. `${filesize}` is set by
  whichever `tftpboot` (per-MAC or generic) succeeded, and is consumed by `fpga load` before
  the next `tftpboot` overwrites it.
- **Per-MAC naming is dash-form** (`system.bit.fc-c2-3d-5a-9a-08`), falling back through
  `system.bin.<mac>` and `system.bit` to generic `system.bin`. U-Boot's `tftpboot`
  parses the first `:` in a filename as a
  `hostIP:file` separator, so `system.bin.${ethaddr}` (colon form) is silently mis-parsed
  and never fetched (observed on HW — see below). `loadpl_net` therefore runs
  `setexpr macfn gsub : - ${ethaddr}` to rewrite colons to dashes, then fetches
  `system.bit.${macfn}`. `setexpr gsub` needs `CONFIG_CMD_SETEXPR` + `CONFIG_REGEX` (both
  on in the ZynqMP defconfig — verified in the built `.config`). A miss returns an immediate
  TFTP NAK rather than a timeout, so the extra probes cost ~nothing.
- **Either `.bit` or `.bin` loads.** `zynqmp_load()` (`drivers/fpga/zynqmppl.c`) calls
  `zynqmp_validate_bitstream()` **only** when `zynqmp_firmware_version() <= PMUFW_V1_0`; on
  current PMUFW it takes the `else { bstype = 0; }` branch, does no sync-word scan and no
  byte-swap, and hands the buffer verbatim to the PMU via `PM_FPGA_LOAD`. The PMU's PCAP
  loader skips the Vivado header, so an unconverted `.bit` programs fine — confirmed in the
  field before it was confirmed in the source. The size delta seen here is exactly the header:
  `system.bit` 34 437 578 B vs the staged `.bin` 34 437 356 B = **222 B**. On PMUFW ≤ v1.0 the
  validator does run and rejects a `.bit`: `check_data()` finds the sync word at a nonzero
  offset and the `diff` check fails with `Bitstream is not validated yet`. `-F bin` keeps
  converting for that reason.
- **…but the two files are not the same bytes, and the `.bit` is far slower.** The *size* delta
  is exactly the header, yet `bootgen` also byte-reverses every 32-bit word (sync
  `AA 99 55 66` → `66 55 99 AA`; bus-width `00 00 00 BB` → `BB 00 00 00`), so **2 363 968 of
  34 437 356 bytes differ** and stripping the header off a `.bit` does not yield the `.bin`. The
  `.bit` loads because the PCAP auto-detects bus width/endianness from the `000000BB/11220044`
  pattern ahead of the sync word — not because the payloads agree. Measured cost:
  **~16.6 s** in `fpga load` for a `.bit` versus **~0.2 s** for a `.bin`, with an identical
  ~17.6 MiB/s TFTP transfer either way (~60 s vs ~40 s total netboot). Since `.bit` is preferred
  at each specificity level, a `.bit`-served board pays that on every boot.
- **Kconfig pinned** in `bsp.cfg`: `CONFIG_FPGA`, `CONFIG_FPGA_XILINX`, `CONFIG_FPGA_ZYNQMPPL`,
  `CONFIG_CMD_FPGA` (normally already on via the ZynqMP defconfig; pinned since the feature
  hard-depends on `fpga load`). Verify present in the built `.config`.

### Host staging
`provision_tftp_host.sh -B` stages `linux/system.bit` unconverted as `/tftpboot/system.bit` —
no bootgen, no Vitis environment — which is the name `loadpl_net` probes first. `-F bin`
converts to a raw `.bin` instead (`bootgen -arch zynqmp -image <bif> -process_bitstream bin`;
needs Vitis/Vivado `bootgen` on `PATH`) and stages `/tftpboot/system.bin`. `-M <mac>`
(repeatable) adds per-MAC copies of whichever format is selected. Idempotent (`cmp`-guarded)
like the `image.ub`/PXE steps. Fallback-mode servers omit `-B`/`-F`/`-M`.

Staging one format removes the other's names — the generic one plus each `-M` MAC — so a stale
`system.bit` can no longer outrank a freshly staged `system.bin`. A run without `-M` cannot know
the board's MAC and so cannot clean a per-MAC leftover; it warns instead, since any per-MAC name
outranks the generic one.

### `startup-app-init` backstop
Before the DMA `insmod`s, it checks `/sys/class/fpga_manager/fpga0/state`; if not `operating`
it prints `ERROR: PL not programmed …` and skips the driver load (clear message instead of
cryptic DMA failures). NOTE: the **primary** guard is the U-Boot `&&`-chain halt; this state
check can read `operating` from the FSBL bitstream even if `fpga load` failed, so it is a
secondary backstop, not the mode guard.

### Hardware validation — Stage A (SD-simulated diskless), all PASS on the RFSoC 4x2
Method: reflash the SD `BOOT.BIN` in place with the tftp-only build (Linux VFAT `cp`), rename
`/boot/system.bit` → `system.old` so the runtime `fpgautil` step is skipped (isolating the PL
to the U-Boot `fpga load`), stage `image.ub` + `system.bin` (+ per-MAC) with
`provision_tftp_host.sh -B -M`, boot, and capture serial. `${filesize}` is set by `tftpboot`
under lwIP (confirmed — `fpga load` consumed it). Observed:

- **Per-MAC bitstream fetch + fpga load + net kernel boot.** `DHCP client bound` →
  `Filename 'system.bin.fc-c2-3d-5a-9a-08'` → `Bytes transferred = 34437356` → `fpga load`
  (silent, `FPGA_RC=0` confirmed at the prompt) → `pxe get` finds `pxelinux.cfg/default` → kernel
  FIT at `0x18000000` → `SimpleRfSoc4x2Example login:`. In Linux the DMA drivers bound
  (`axi_stream_dma … Found Version 2 Device`) and `axiversiondump` read `Root.AxiVersion`
  (`FwVersion 0x3030000`) — i.e. the PL is programmed and the app runs.
- **Per-MAC → generic fallback.** With the per-MAC file removed:
  `Filename 'system.bin.fc-c2-3d-5a-9a-08'` → `TFTP error: 256 (… not found)` →
  `Filename 'system.bin'` → `Bytes transferred = 34437356` → boots. The `if…else…fi` falls
  through exactly as intended.
- **Halt-on-failure (tftp-only).** With **both** bitstream files removed:
  per-MAC MISS → generic MISS → `TFTP-only build: not falling back to SD` → **halt at
  `ZynqMP>`**, and the kernel FIT is **never fetched** (the `&&` chain aborts inside `loadpl_net`,
  before the kernel) — never a net kernel over an unprogrammed PL. Restaging the bitstream and
  `reset` recovers it.

**LANDMINE (found on HW): a colon-form per-MAC name does not work.** The first cut used
`system.bin.${ethaddr}` (colon form). U-Boot's `tftpboot` parses the first `:` in a filename
as a `hostIP:file` separator, so it silently skipped the per-MAC attempt and went straight to the
generic name — no per-MAC line, no error. Fix: `setexpr macfn gsub : - ${ethaddr}` (validated
live: `ethaddr=fc:c2:3d:5a:9a:08` → `macfn=fc-c2-3d-5a-9a-08`) and request the per-MAC name in
dash form; `provision_tftp_host.sh -M` stores the dash form to match. The constraint is about
the colon, not the name — it applies equally to `system.bit.<mac>` and `system.bin.<mac>`.

Note: `fpga0/state` read `operating` here even in the disabled-SD case, because the tftp-only
`BOOT.BIN` still carries an FSBL bitstream that programs the PL before U-Boot runs — so the
state check cannot by itself prove the `fpga load` ran. The decisive proof is the `&&`-chain
halt (a failed `fpga load` would have halted before Linux) plus `FPGA_RC=0` at the prompt.

The literal no-SD QSPI diskless boot (Stage B) is deferred by decision — not run.

---

## 8. Hardware validation — Stage C (bitstream-format matrix), all PASS on the RFSoC 4x2

Run after `loadpl_net` was changed to probe four names. Method as Stage A (tftp-only `BOOT.BIN`
flashed by Linux VFAT `cp`, `/boot/system.bit` parked so the runtime `fpgautil` step cannot
override the fetch), staging exactly one name set per boot and reading the fetched `Filename`
and `Bytes transferred` off the serial console. `.bit` = 34 437 578 B, `.bin` = 34 437 356 B,
which makes every case unambiguous.

| # | staged | fetched | misses first | `fpga load` | outcome |
|---|--------|---------|--------------|-------------|---------|
| T1 | `system.bin` | `system.bin` | 3 | 0.00 s | boots |
| T2 | `system.bit` | `system.bit` | 2 | 16.60 s | boots |
| T3 | both generic | `system.bit` | 2 | 16.60 s | boots — `.bit` preferred |
| T4 | `system.bin.<mac>` | `system.bin.<mac>` | 1 | 0.20 s | boots |
| T5 | `system.bit.<mac>` | `system.bit.<mac>` | 0 | 16.60 s | boots |
| T6 | all four | `system.bit.<mac>` | 0 | 16.60 s | boots — full precedence |
| T7 | `.bit.<mac>` + `.bin` | `system.bit.<mac>` | 0 | 16.80 s | boots — per-MAC beats generic |
| T8 | nothing | — | 4 | — | **halts at `ZynqMP>`** |

- All four T8 misses landed inside **1 ms**, confirming that a miss costs an immediate NAK and
  not a timeout. No kernel was fetched in T8 **even though `image.ub` was staged and reachable**,
  proving the abort came from the bitstream step via the `&&` chain rather than a missing FIT.
- Recovery from the T8 halt (re-stage, `reset` over serial, per §5) worked first try.

### Conclusive format proof
Because `BOOT.BIN` embeds an FSBL bitstream, the cases above cannot by themselves show that the
*fetched* bitstream is what ends up running (same caveat as §7's `fpga0/state` note). Rebuilt
with `BIF_BITSTREAM_ATTR = ""` — 1 946 016 B, no `download-zynqmp-user.bit` partition — with
`/boot/system.bit` still parked, so the TFTP fetch became the only possible programmer. Both a
`.bit` and a `.bin` then bound `axi_memory_map` at `0x400000000` and `axi_stream_dma` at
`0xb0000000` ("Found Version 2 Device") and answered `axiversiondump`. Those are PL-side
registers, so the fetched bitstream configures the fabric in either format.

### `startup-app-init` guard
Exercised for the first time with `BIF_BITSTREAM_ATTR = ""`, `fallback` mode and no
`/boot/system.bit`: it printed `ERROR: PL not programmed …` and skipped the driver load, with
zero driver-bind lines — the diagnosis lands as intended.

Note that the guard wraps only the two `insmod`s. `axiversiondump` and the runtime app run
afterwards regardless and both die on `/dev/axi_memory_map`, so the service respawns roughly every
3 s, re-emitting the diagnosis plus two Python tracebacks (PIDs climbed 262 → 1233 in ~90 s).
Expect a noisy console rather than a single message. The board is non-functional in this state
either way, and the retry is self-healing: program the PL by hand and a later pass gets past the
guard and loads the drivers.

### Fallback regression
With TFTP up, `fallback` made **zero** bitstream probes even with `system.bin` staged and
reachable (`loadpl_skip` is a bare `true`), and netbooted via `pxe get` → `default` → `image.ub`.
With the FIT and PXE config unstaged, `pxe get` exhausted every `pxelinux.cfg` name, the
in-`netboot` direct `tftpboot image.ub` NAK'd, and `run sdboot` booted the on-SD `image.ub`
(14.4 MiB/s). This used an immediate NAK (files unstaged) rather than either §5 timeout
mechanism, both of which need root on the shared lab host. The port-closed mechanism has since
been re-measured with `pxe get` in front of it — see §6: the per-attempt figure holds at ≈ 6 s,
but 14 attempts make the end-to-end fallback ≈ 85 s, and ICMP does not cut an attempt short.

### The login banner is not a mode indicator
Confirmed: every case in both netboot modes printed the same `SimpleRfSoc4x2Example login:`.
`BuildYoctoProject.sh` sets `hostname:pn-base-files` from the target name with no
`UBOOT_NETBOOT_MODE` dependency, so no `-fallback`/`-tftponly` suffix exists in these builds.
Distinguish modes with the `strings -a BOOT.BIN | grep -a '^bootcmd='` check from §3, or by
runtime behavior. Not by md5: per §3 the sizes and checksums move with every rebuild, so a
committed digest goes stale immediately.

### Editing `UBOOT_NETBOOT_MODE` does not need a `cleansstate`
Changing it in `build/conf/local.conf` and running `bitbake xilinx-bootbin` re-ran 29 of 1972
tasks and produced the other mode's `BOOT.BIN` (`run loadpl_net` present, `run loadpl_skip`
absent). The variable is referenced by the u-boot bbappend's `do_configure`, so BitBake's
signature tracking picks it up. This is how Stage C switched modes without a `-c` reconfigure,
which would have re-run `repo sync` in the middle of the run.

That evidence (`loadpl_net` present / `loadpl_skip` absent) was a `fallback` ↔ `tftp-only`
observation. For `fallback` ↔ `sd-only` both select `loadpl_skip`, so the **only** confirmation
that the switch took is the `bootcmd=` line. The signature-tracking property still holds:
`${UBOOT_NETBOOT_MODE}` appears literally in the `case` inside `do_configure:append`, which is
what folds it into the task hash. Do not hoist that selection out of the task body.

`BuildYoctoProject.sh` also re-syncs `UBOOT_NETBOOT_MODE` into an existing project's
`local.conf` when `-m` is passed explicitly. Without that, a re-run without `-c` would silently
ignore `-m` (the original write is inside the fresh-configure branch only). The re-sync is gated
on an explicit `-m` precisely so the hand-edited-`local.conf` workflow above is not clobbered by
a plain rebuild.

### sd-only is not yet hardware-validated
`sd-only` and the `@BOOTCMD_SEL@` refactor were added after every measurement in §5 through §8.
Verified so far: the three substitution branches produce the expected `bootcmd` with no
placeholder left behind, `fallback` and `tftp-only` come out byte-identical to their
pre-refactor values, the macro still preprocesses, and the strict Sphinx docs build passes.
**Not yet observed on a board.** The minimum set to close this out:

1. `sd-only` on a net with no TFTP server: expect **zero** `DHCP client bound` lines, zero
   `TFTP error` lines, and the `SD-only build: skipping netboot` echo. This is the test that
   proves the feature.
2. Boot-to-login time versus the same board built `fallback`, to confirm the ~85 s recovered.
3. `strings -a BOOT.BIN | grep -a '^bootcmd='` yields exactly one line. Worth checking
   explicitly: U-Boot also emits `bootcmd=` from `CONFIG_BOOTCOMMAND` into
   `default_environment[]`, and `CFG_EXTRA_ENV_SETTINGS` is what wins today (proven by the
   `fallback` builds in §5), but that has never been checked for a `run sdboot`-shaped value.
4. `run netboot` by hand from `ZynqMP>` on an `sd-only` board still fetches and boots, which is
   the recovery guarantee the mode is documented to keep.
