# KR260 board support — M1 implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `hardware/XilinxKriaKr260` so a KR260 Starter Kit builds a Vivado 2026.1 design with `AxiSocUltraPlusCore`, and boots our `BOOT.BIN` from a Kria QSPI slot and Linux from the USB-attached SD card.

**Architecture:** Mirror `hardware/XilinxKriaKv260` at `7.1.0`. The PS block design is the KV260 design with its PS settings replaced by AMD's KR260 SOM and carrier board presets plus the AXI SoC platform settings. The Yocto machine reuses meta-kria's `k26-smk.inc`, AMD's `zynqmp-sck-kr-g-revB` carrier overlay, and meta-kria's own U-Boot and boot script.

**Tech Stack:** Vivado 2026.1 block design Tcl, ruckus, VHDL-2008 (GHDL 6 for static analysis), Yocto `rel-v2026.1` (meta-xilinx, meta-kria), Python 3 check scripts.

## Global Constraints

- Branch `kr260-support` from `main` `7.1.0` (`909aa43`), worked inside the ptp-dev submodule checkout. Do not change ptp-dev's recorded submodule pin.
- Part `xck26-sfvc784-2lv-c`. Vivado 2026.1 or later (`VersionCheck 2026.1`). Board part `xilinx.com:kr260_som:part0:1.1`, carrier connectors `xilinx.com:kr260_carrier:som240_1_connector:1.1` and `som240_2_connector:1.1`.
- Yocto manifests `rel-v2026.1` (fixed by `BuildYoctoProject.sh`).
- A Kria QSPI A/B slot holds `0xD80000` bytes (slot A `0x200000`, slot B `0xF80000`; meta-kria `kria-qspi.bb`). `BOOT.BIN` must not contain the bitstream.
- The starter-kit SOM has no eMMC: PS SD0 and SD1 stay disabled.
- Vivado and Yocto run only on `rdsrv<NNN>` hosts. Use the `slac-fpga-remote-build` skill for every remote build. Source the tool environment in the same command as the tool: `source /sdf/group/faders/tools/xilinx/2026.1/Vivado/2026.1/settings64.sh`.
- Keep the SLAC license header on every new source file. Follow `surf/docs/vhdl-conventions.md` for VHDL.
- Commit only when the user authorizes it. Commit steps below list what belongs in each commit.
- Checks under `docs/plans/kr260-support/checks/` are execution aids for this plan; delete them when M1 closes, after folding the procedures into `hardware/XilinxKriaKr260/README.md`.

## Execution order with the ptp-dev plan

The Yocto build needs an XSA from ptp-dev's `PtpKr260` target, and hardware acceptance needs both repositories. Run in this order:

1. This plan, Tasks 1–5.
2. ptp-dev `docs/plans/kr260-ptp/m1-implementation.md`, Tasks 1–4 (target, XSA, Python).
3. This plan, Task 6 (Yocto build).
4. ptp-dev plan, Tasks 5–6 (SD card, QSPI, boot and host acceptance).
5. This plan, Task 7 (close-out).

## File structure

| File | Responsibility |
| --- | --- |
| `shared/rtl/AxiSocUltraPlusPkg.vhd` | Adds `HW_TYPE_XILINX_KR260_C = 0x0B`. |
| `python/axi_soc_ultra_plus_core/_AxiVersion.py` | `HW_TYPE_C` enum entry for `0x0B`. |
| `shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump` | Same enum entry for the on-target dump. |
| `hardware/XilinxKriaKr260/ruckus.tcl` | Version/part checks, board part, sources, constraints, block design. |
| `hardware/XilinxKriaKr260/rtl/HardwareTypePkg.vhd` | `HW_TYPE_C` and `IS_RFSOC_C` for this board. |
| `hardware/XilinxKriaKr260/xdc/XilinxKriaKr260.xdc` | Board-fixed pins only: SYSMON and fan. |
| `hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.{tcl,bd}` | PS and AXI wiring, generated in Task 3. |
| `hardware/XilinxKriaKr260/Yocto/zynqmp-user.conf` | Machine fragment appended by `BuildYoctoProject.sh`. |
| `hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/device-tree.bbappend` | Includes `system-user.dtsi` (same as KV260). |
| `hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/files/system-user.dtsi` | Boot arguments, CMA, EEPROM MAC, DMA node. |
| `hardware/XilinxKriaKr260/README.md` | Boot chain, SD card, QSPI update, validation and recovery. |
| `docs/plans/kr260-support/checks/check_hw_type.py` | Task 1 check. |
| `docs/plans/kr260-support/checks/generate_kr260_bd.tcl` | Task 3 generator. |
| `docs/plans/kr260-support/checks/check_kr260_bd.py` | Task 3 check. |
| `docs/plans/kr260-support/checks/check_yocto_outputs.sh` | Task 6 check. |

---

### Task 1: Register the KR260 hardware type

**Files:**
- Create: `docs/plans/kr260-support/checks/check_hw_type.py`
- Modify: `shared/rtl/AxiSocUltraPlusPkg.vhd:66`
- Modify: `python/axi_soc_ultra_plus_core/_AxiVersion.py:102`
- Modify: `shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump:245`

**Interfaces:**
- Produces: VHDL constant `HW_TYPE_XILINX_KR260_C : slv(31 downto 0) := x"00_00_00_0B"`; enum name `'XilinxKriaKr260'` for value `0x00_00_00_0B` in both Python maps. Task 2 and ptp-dev Task 4 use these names.

- [ ] **Step 1: Write the failing check**

`docs/plans/kr260-support/checks/check_hw_type.py`:

```python
#!/usr/bin/env python3
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
"""Check that the KR260 hardware type is registered in RTL, PyRogue and Yocto."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]

errors = []
pkg = (ROOT / 'shared/rtl/AxiSocUltraPlusPkg.vhd').read_text()
if not re.search(r'constant HW_TYPE_XILINX_KR260_C\s*:\s*slv\(31 downto 0\)\s*:=\s*x"00_00_00_0B";\s*-- XilinxKriaKr260', pkg):
    errors.append('AxiSocUltraPlusPkg.vhd: HW_TYPE_XILINX_KR260_C missing')
for rel in ('python/axi_soc_ultra_plus_core/_AxiVersion.py',
            'shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump'):
    if "0x00_00_00_0B: 'XilinxKriaKr260'" not in (ROOT / rel).read_text():
        errors.append(f'{rel}: enum entry 0x0B missing')

for e in errors:
    print('FAIL:', e)
if errors:
    sys.exit(1)
print('hardware type OK')
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python3 docs/plans/kr260-support/checks/check_hw_type.py`
Expected: three `FAIL:` lines, exit status 1.

- [ ] **Step 3: Add the constant and enum entries**

In `shared/rtl/AxiSocUltraPlusPkg.vhd`, after the `HW_TYPE_SLAC_RFMC_CARRIER_C` line:

```vhdl
   constant HW_TYPE_XILINX_KR260_C          : slv(31 downto 0) := x"00_00_00_0B"; -- XilinxKriaKr260
```

In `python/axi_soc_ultra_plus_core/_AxiVersion.py`, after `0x00_00_00_0A: 'SlacRfmcCarrier',`, at the same indentation:

```python
                0x00_00_00_0B: 'XilinxKriaKr260',
```

In `shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump`, after `0x00_00_00_0A: 'SlacRfmcCarrier',`, at that file's indentation:

```python
                    0x00_00_00_0B: 'XilinxKriaKr260',
```

- [ ] **Step 4: Run the check and confirm it passes**

Run: `python3 docs/plans/kr260-support/checks/check_hw_type.py`
Expected: `hardware type OK`.
Also run: `python3 -m py_compile python/axi_soc_ultra_plus_core/_AxiVersion.py shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump`
Expected: no output.

- [ ] **Step 5: Commit (when authorized)**

```bash
git add shared/rtl/AxiSocUltraPlusPkg.vhd python/axi_soc_ultra_plus_core/_AxiVersion.py \
        shared/Yocto/recipes-apps/axiversiondump/files/axiversiondump \
        docs/plans/kr260-support/checks/check_hw_type.py
git commit -m "Add the XilinxKriaKr260 hardware type"
```

---

### Task 2: Board directory, hardware package and board-fixed constraints

**Files:**
- Create: `hardware/XilinxKriaKr260/ruckus.tcl`
- Create: `hardware/XilinxKriaKr260/rtl/HardwareTypePkg.vhd`
- Create: `hardware/XilinxKriaKr260/xdc/XilinxKriaKr260.xdc`

**Interfaces:**
- Consumes: `HW_TYPE_XILINX_KR260_C` (Task 1).
- Produces: top-level port names a consuming target must declare: `fanEnableL : out sl`, `vPIn : in sl`, `vNIn : in sl`. Block design path `bd/2026.1/AxiSocUltraPlusCpuCore.bd` (created in Task 3).

Pin evidence (AMD XilinxBoardStore `2026.1`, `kr260_som/1.1/part0_pins.xml`, plus SCK-KR schematic sheet 3): the fan control is `HDA20` = `som240_1_c24` = package `A12`, driving Q20. High turns the fan off, so the signal is active-low, as on the KV260. SYSMON `R13`/`T12` are the device's dedicated analog pins.

- [ ] **Step 1: Write `rtl/HardwareTypePkg.vhd`**

```vhdl
-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- This file is part of 'axi-soc-ultra-plus-core'.
-- It is subject to the license terms in the LICENSE.txt file found in the
-- top-level directory of this distribution and at:
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
-- No part of 'axi-soc-ultra-plus-core', including this file,
-- may be copied, modified, propagated, or distributed except according to
-- the terms contained in the LICENSE.txt file.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library surf;
use surf.StdRtlPkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

package HardwareTypePkg is

   constant HW_TYPE_C : slv(31 downto 0) := HW_TYPE_XILINX_KR260_C;

   -- This board does not carry the RF data converter: no PYRFDC_CONFIG ROM
   constant IS_RFSOC_C : boolean := false;

end package HardwareTypePkg;
```

- [ ] **Step 2: Analyze it with GHDL**

Run in bash from the repository root:

```bash
W=$(mktemp -d)
O="--std=08 -frelaxed -fsynopsys -Wno-elaboration -Wno-hide -Wno-shared --workdir=$W -P$W"
S=../surf
ghdl -a $O --work=surf $S/base/general/rtl/StdRtlPkg.vhd $S/base/general/rtl/TextUtilPkg.vhd \
     $S/axi/axi-lite/rtl/AxiLitePkg.vhd $S/axi/axi4/rtl/AxiPkg.vhd $S/axi/axi-stream/rtl/AxiStreamPkg.vhd
ghdl -a $O --work=axi_soc_ultra_plus_core shared/rtl/AxiSocUltraPlusPkg.vhd \
     hardware/XilinxKriaKr260/rtl/HardwareTypePkg.vhd && echo ANALYZE_OK
```

Expected: `ANALYZE_OK`. (`../surf` is the sibling SURF submodule in ptp-dev. `TextUtilPkg.vhd` lives next to `StdRtlPkg.vhd`; if not, locate it with `find ../surf -name TextUtilPkg.vhd -not -path '*/build/*'`.)

- [ ] **Step 3: Write `xdc/XilinxKriaKr260.xdc`**

```tcl
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################

set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]

##############################################################################

set_property -dict { PACKAGE_PIN R13 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN T12 IOSTANDARD ANALOG } [get_ports { vNIn }]

# fanEnableL = HDA20 = som240_1_c24 (SCK-KR sheet 3, Q20): high turns the fan off
set_property -dict { PACKAGE_PIN A12 IOSTANDARD LVCMOS33 } [get_ports { fanEnableL }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
```

- [ ] **Step 4: Write `ruckus.tcl`**

```tcl
# Load RUCKUS environment and library
source $::env(RUCKUS_PROC_TCL)

# Check for version 2026.1 of Vivado (or later)
if { [VersionCheck 2026.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "xck26-sfvc784-2lv-c" } {
   puts "\n\nERROR: PRJ_PART must be xck26-sfvc784-2lv-c in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Load the common source code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Set the board part
set_property board_part xilinx.com:kr260_som:part0:1.1 [current_project]

# Load the block design
set bdVer "2026.1"
loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.bd"
```

- [ ] **Step 5: Static checks**

Run:

```bash
diff <(sed 's/XilinxKriaKr260/X/;s/kr260_som:part0:1.1/BP/' hardware/XilinxKriaKr260/ruckus.tcl) \
     <(sed 's/XilinxKriaKv260/X/;s/kv260_som:part0:1.3/BP/' hardware/XilinxKriaKv260/ruckus.tcl)
grep -c PACKAGE_PIN hardware/XilinxKriaKr260/xdc/XilinxKriaKr260.xdc
```

Expected: the diff shows only the KV260 `ERROR:` wording ("must be either") and the commented-out `.tcl` `loadBlockDesign` line, which the KR260 file omits. The pin count is `3`.

- [ ] **Step 6: Commit (when authorized)**

```bash
git add hardware/XilinxKriaKr260/ruckus.tcl hardware/XilinxKriaKr260/rtl hardware/XilinxKriaKr260/xdc
git commit -m "Add XilinxKriaKr260 board directory with board-fixed pins"
```

---

### Task 3: PS block design from the KR260 board presets

**Files:**
- Create: `docs/plans/kr260-support/checks/check_kr260_bd.py`
- Create: `docs/plans/kr260-support/checks/generate_kr260_bd.tcl`
- Create (generated): `hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.tcl`
- Create (generated): `hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.bd`

**Interfaces:**
- Consumes: KV260 `bd/2026.1/AxiSocUltraPlusCpuCore.tcl` (cells, ports, addresses).
- Produces: the same block-design ports and address map as the KV260: `axiLite` at `0x4_0000_0000` (range `0x1_0000_0000`), `dmaCtrl` at `0xB000_0000`, DMA HP0 to DDR low/high, `fanEnableL` from TTC0, `plClk` at 250 MHz. `AxiSocUltraPlusCpu.vhd` instantiates this design unchanged.

Expected PS settings: AMD's `kr260_carrier/1.1/preset.xml` (`mpsoc_preset_som240_1`) and `kr260_som/1.1/preset.xml`, plus the KV260 design's platform overrides. The SOM preset enables SD0 as eMMC, which the starter-kit SOM lacks, so it is turned off.

- [ ] **Step 1: Write the failing check**

`docs/plans/kr260-support/checks/check_kr260_bd.py`:

```python
#!/usr/bin/env python3
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
"""Check the generated KR260 block design against the board presets and the KV260 wiring."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
KV = ROOT / 'hardware/XilinxKriaKv260/bd/2026.1/AxiSocUltraPlusCpuCore.tcl'
KR = ROOT / 'hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.tcl'
KR_BD = KR.with_suffix('.bd')

EXPECTED = {
    # KR260 carrier preset (SCK-KR): PS Ethernet, USB, I2C, UART
    'PSU__ENET0__PERIPHERAL__ENABLE': '1',
    'PSU__ENET0__PERIPHERAL__IO': 'GT Lane0',
    'PSU__GEM0__REF_CLK_SEL': 'Ref Clk0',
    'PSU__ENET1__PERIPHERAL__ENABLE': '1',
    'PSU__ENET1__PERIPHERAL__IO': 'MIO 38 .. 49',
    'PSU__ENET1__GRP_MDIO__IO': 'MIO 50 .. 51',
    'PSU__CRL_APB__GEM_TSU_REF_CTRL__FREQMHZ': '250',
    'PSU__USB0__PERIPHERAL__IO': 'MIO 52 .. 63',
    'PSU__USB1__PERIPHERAL__IO': 'MIO 64 .. 75',
    'PSU__USB3_0__PERIPHERAL__IO': 'GT Lane2',
    'PSU__USB3_1__PERIPHERAL__IO': 'GT Lane3',
    'PSU__USB0__RESET__IO': 'MIO 76',
    'PSU__USB1__RESET__IO': 'MIO 77',
    'PSU__USB0__REF_CLK_SEL': 'Ref Clk2',
    'PSU__USB1__REF_CLK_SEL': 'Ref Clk3',
    'PSU__I2C1__PERIPHERAL__IO': 'MIO 24 .. 25',
    'PSU__UART1__PERIPHERAL__IO': 'MIO 36 .. 37',
    # SOM preset: QSPI boot device
    'PSU__QSPI__PERIPHERAL__ENABLE': '1',
    'PSU__QSPI__PERIPHERAL__IO': 'MIO 0 .. 5',
    # Starter-kit SOM has no eMMC
    'PSU__SD0__PERIPHERAL__ENABLE': '0',
    # AXI SoC platform settings (same as KV260)
    'PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ': '250',
    'PSU__FPGA_PL1_ENABLE': '0',
    'PSU__USE__M_AXI_GP0': '1',
    'PSU__USE__M_AXI_GP1': '1',
    'PSU__MAXIGP0__DATA_WIDTH': '32',
    'PSU__MAXIGP1__DATA_WIDTH': '32',
    'PSU__USE__S_AXI_GP2': '1',
    'PSU__SAXIGP2__DATA_WIDTH': '128',
    'PSU__USE__IRQ0': '1',
    'PSU__TTC0__WAVEOUT__ENABLE': '1',
    'PSU__TTC0__WAVEOUT__IO': 'EMIO',
    'PSU__PMU__PLERROR__ENABLE': '1',
}
ABSENT_OR_ZERO = ('PSU__SD1__PERIPHERAL__ENABLE', 'PSU__ENET2__PERIPHERAL__ENABLE',
                  'PSU__ENET3__PERIPHERAL__ENABLE')


def params(text):
    return dict(re.findall(r'CONFIG\.(PSU[A-Za-z0-9_]+) \{([^}]*)\}', text))


def lines(text, prefix):
    return sorted(l.strip() for l in text.splitlines() if l.strip().startswith(prefix))


def main():
    errors = []
    if not KR.exists() or not KR_BD.exists():
        print(f'FAIL: {KR} or {KR_BD} missing')
        return 1
    kr_text, kv_text = KR.read_text(), KV.read_text()
    kr = params(kr_text)
    for key, want in EXPECTED.items():
        if kr.get(key) != want:
            errors.append(f'{key} = {kr.get(key)!r}, expected {want!r}')
    for key in ABSENT_OR_ZERO:
        if kr.get(key, '0') != '0':
            errors.append(f'{key} = {kr[key]!r}, expected disabled')
    for prefix in ('assign_bd_address', 'set axiLite', 'set dma', 'set dmaCtrl', 'set dmaClk',
                   'set dmaIrq', 'set dmaRstL', 'set fanEnableL', 'set plClk', 'set plRstL',
                   'set pmuErrorFromPl', 'set pmuErrorToPl', 'connect_bd_'):
        if lines(kr_text, prefix) != lines(kv_text, prefix):
            errors.append(f'"{prefix}" lines differ from the KV260 design')
    if 'kr260_som:part0:1.1' not in kr_text:
        errors.append('board part kr260_som:part0:1.1 not recorded in the Tcl')
    for path in (KR, KR_BD):
        if 'kv260' in path.read_text().lower():
            errors.append(f'{path.name} still mentions kv260')
    for e in errors:
        print('FAIL:', e)
    if errors:
        return 1
    print(f'block design OK ({len(EXPECTED)} PS settings, wiring matches KV260)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `python3 docs/plans/kr260-support/checks/check_kr260_bd.py`
Expected: `FAIL: ... missing`, exit status 1.

- [ ] **Step 3: Write the generator**

`docs/plans/kr260-support/checks/generate_kr260_bd.tcl`:

```tcl
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
## Generate hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.{tcl,bd}
## from the KV260 2026.1 design: same cells, ports and addresses; PS settings
## from the KR260 SOM and carrier board presets plus the AXI SoC platform
## settings. Run from the repository root with Vivado 2026.1:
##   vivado -mode batch -source docs/plans/kr260-support/checks/generate_kr260_bd.tcl \
##          -tclargs <scratch directory outside the source tree>
##############################################################################

if {[llength $argv] != 1} {
   error "usage: -tclargs <scratch directory outside the source tree>"
}
set scratch [file normalize [lindex $argv 0]]
set repo    [pwd]
set kvTcl   $repo/hardware/XilinxKriaKv260/bd/2026.1/AxiSocUltraPlusCpuCore.tcl
set outDir  $repo/hardware/XilinxKriaKr260/bd/2026.1
set somPart xilinx.com:kr260_som:part0:1.1
set carVer  1.1

if {[llength [get_board_parts -quiet $somPart]] == 0} {
   puts "Installed KR260 board parts: [get_board_parts -quiet *kr260*]"
   error "$somPart is not installed in this Vivado"
}

create_project -force kr260_bd $scratch -part xck26-sfvc784-2LV-c
set_property board_part $somPart [current_project]
set_property board_connections [list \
   som240_1_connector xilinx.com:kr260_carrier:som240_1_connector:$carVer \
   som240_2_connector xilinx.com:kr260_carrier:som240_2_connector:$carVer] [current_project]

# Replace the KV260 PS property list with the board presets plus overrides
set fh  [open $kvTcl r]
set src [read $fh]
close $fh
set cellLine {set zynq_ultra_ps_e_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.5 zynq_ultra_ps_e_0 ]}
set endTok   {] $zynq_ultra_ps_e_0}
set first [string first $cellLine $src]
set last  [string first $endTok $src $first]
if {$first < 0 || $last < 0} { error "PS cell block not found in $kvTcl" }
set last [expr {$last + [string length $endTok] - 1}]

set psBlock {set zynq_ultra_ps_e_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.5 zynq_ultra_ps_e_0 ]
  apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $zynq_ultra_ps_e_0
  set_property -dict [list \
    CONFIG.PSU__SD0__PERIPHERAL__ENABLE {0} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__SRCSEL {IOPLL} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {250} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__FPGA_PL1_ENABLE {0} \
    CONFIG.PSU__NUM_FABRIC_RESETS {1} \
    CONFIG.PSU__PL_CLK0_BUF {TRUE} \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {1} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__MAXIGP0__DATA_WIDTH {32} \
    CONFIG.PSU__MAXIGP1__DATA_WIDTH {32} \
    CONFIG.PSU__USE__S_AXI_GP0 {0} \
    CONFIG.PSU__USE__S_AXI_GP1 {0} \
    CONFIG.PSU__USE__S_AXI_GP2 {1} \
    CONFIG.PSU__USE__S_AXI_GP3 {0} \
    CONFIG.PSU__USE__S_AXI_GP4 {0} \
    CONFIG.PSU__USE__S_AXI_GP5 {0} \
    CONFIG.PSU__USE__S_AXI_GP6 {0} \
    CONFIG.PSU__USE__S_AXI_ACE {0} \
    CONFIG.PSU__USE__S_AXI_ACP {0} \
    CONFIG.PSU__SAXIGP2__DATA_WIDTH {128} \
    CONFIG.PSU__USE__IRQ0 {1} \
    CONFIG.PSU__TTC0__WAVEOUT__ENABLE {1} \
    CONFIG.PSU__TTC0__WAVEOUT__IO {EMIO} \
    CONFIG.PSU__PMU__PLERROR__ENABLE {1} \
    CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE {1} \
  ] $zynq_ultra_ps_e_0}

set src [string replace $src $first $last $psBlock]
set src [string map {kv260_som:part0:1.3 kr260_som:part0:1.1} $src]
set derived $scratch/kr260_from_kv260.tcl
set fh [open $derived w]
puts -nonewline $fh $src
close $fh

# Builds, validates and saves the design (create_root_design in the script)
source $derived

file mkdir $outDir
write_bd_tcl -force $outDir/AxiSocUltraPlusCpuCore.tcl
file copy -force [get_files AxiSocUltraPlusCpuCore.bd] $outDir/AxiSocUltraPlusCpuCore.bd
close_project
puts "Wrote $outDir/AxiSocUltraPlusCpuCore.{tcl,bd}"
```

- [ ] **Step 4: Run the generator on an rdsrv host (slac-fpga-remote-build skill)**

On the remote copy of this branch, from the repository root:

```bash
hostname   # must be rdsrv<NNN>
source /sdf/group/faders/tools/xilinx/2026.1/Vivado/2026.1/settings64.sh && \
vivado -mode batch -nojournal -nolog \
       -source docs/plans/kr260-support/checks/generate_kr260_bd.tcl \
       -tclargs /tmp/$USER-kr260-bd
```

Expected: ends with `Wrote .../hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.{tcl,bd}`, and `validate_bd_design` reports no errors. Copy both generated files back into the local branch.

If `get_board_parts` fails: stop and report the printed list to the user. Installing board files changes the tool environment and needs the user's decision.
If `board_connections` errors with an unknown VLNV: run `get_board_components -quiet *kr260*`, use the reported connector version in `carVer`, and update the Global Constraints line.

- [ ] **Step 5: Run the check and confirm it passes**

Run: `python3 docs/plans/kr260-support/checks/check_kr260_bd.py`
Expected: `block design OK (32 PS settings, wiring matches KV260)`.
If a carrier setting differs from `EXPECTED`, compare with `kr260_carrier/1.1/preset.xml` and SCK-KR sheet 16 before changing anything. Report a real conflict instead of editing the expectation to match.

- [ ] **Step 6: Commit (when authorized)**

```bash
git add hardware/XilinxKriaKr260/bd docs/plans/kr260-support/checks/generate_kr260_bd.tcl \
        docs/plans/kr260-support/checks/check_kr260_bd.py
git commit -m "Add the KR260 PS block design from the SOM and carrier presets"
```

---

### Task 4: Yocto machine fragment and device tree

**Files:**
- Create: `hardware/XilinxKriaKr260/Yocto/zynqmp-user.conf`
- Create: `hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/device-tree.bbappend`
- Create: `hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/files/system-user.dtsi`

**Interfaces:**
- Consumes: `BuildYoctoProject.sh -h XilinxKriaKr260`, which appends `zynqmp-user.conf` to the shared machine file and symlinks `recipes-bsp`.
- Produces: `MACHINEOVERRIDES` `k26-kr`; `BIF_BITSTREAM_ATTR:k26-kr = ""`; `image_update` installed; the DMA node `axi_stream_dma_0@b0000000` (IRQ `<0 89 4>`), as the KV260.

- [ ] **Step 1: Write `zynqmp-user.conf`**

```
#### Preamble
MACHINEOVERRIDES =. "${@['', 'k26-smk-kr:']['k26-smk-kr' != '${MACHINE}']}"
MACHINEOVERRIDES =. "k26-kr:"
#### Regular settings follow

require conf/machine/include/k26-smk.inc
require conf/machine/include/kria-qemu-kr.inc

# rel-v2026.1: restore the SMK-K26 board DTSI (SOM compatible, FRU EEPROM
# nodes) in the U-Boot control DTB; see the KV260 fragment for the history.
YAML_DT_BOARD_FLAGS ?= "{BOARD zynqmp-smk-k26-reva}"

# kria-qspi is only compatible with the *-sdt-multidomain machines; this
# project installs its own boot.bin into an A/B slot instead.
EXTRA_IMAGEDEPENDS:remove = "kria-qspi"

# Break the wic/initramfs dependency loop (see the KV260 fragment).
IMAGE_FSTYPES:remove = "wic wic.xz wic.qemu-sd"

INITRAMFS_IMAGE ?= "petalinux-image-minimal"

# KR260 carrier overlays (U-Boot selects by carrier EEPROM); Linux uses revB
UBOOT_DT_FILES = "\
    zynqmp-sck-kr-g-revA.dtso \
    zynqmp-sck-kr-g-revB.dtso \
    "
DTB_FILE_NAME:task-install:pn-device-tree = "${UBOOT_DTFILE_PREFIX}-zynqmp-sck-kr-g-revB.dtb"
DTB_FILE_NAME:task-deploy:pn-device-tree = "${UBOOT_DTFILE_PREFIX}-zynqmp-sck-kr-g-revB.dtb"
# Workaround for bitbake not detecting the above change in the hash
PR:append:pn-device-tree = ".1"

# boot.cmd.kria ignores /chosen bootargs; this is the knob it honors.
KERNEL_COMMAND_APPEND = "net.ifnames=0 biosdevname=0 clk_ignore_unused cma=1024M root=LABEL=root"

# A Kria QSPI A/B slot holds 0xD80000 bytes (meta-kria kria-qspi.bb), so keep
# the bitstream out of BOOT.BIN. startup-app-init loads /boot/system.bit with
# fpgautil before inserting the DMA drivers. Overrides BuildYoctoProject.sh's
# local.conf value for this machine; its -m option has no effect on Kria.
BIF_BITSTREAM_ATTR:k26-kr = ""

# On-target QSPI A/B update: image_update -i BOOT.BIN, -v to validate, -p to print
IMAGE_INSTALL:append = " image-update"

#### No additional settings should be after the Postamble
#### Postamble
PACKAGE_EXTRA_ARCHS:append = "${@['', ' k26_smk_kr']['k26-smk-kr' != "${MACHINE}"]}"

# Additional configurations
```

- [ ] **Step 2: Write `device-tree.bbappend`** (identical to the KV260 file)

```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SYSTEM_USER_DTSI ?= "system-user.dtsi"

SRC_URI:append = " file://${SYSTEM_USER_DTSI}"

do_configure:append() {
   cp ${WORKDIR}/${SYSTEM_USER_DTSI} ${B}/device-tree
   echo "#include \"${SYSTEM_USER_DTSI}\"" >> ${B}/device-tree/system-top.dts
}
```

- [ ] **Step 3: Write `system-user.dtsi`**

Differences from the KV260 file: the EEPROM MAC goes to `gem0` (alias `ethernet0` in AMD's KR overlay), and there is no `sdhci1` node. AMD's overlay owns both GEMs, the PHYs, the USB hubs, the SD bridge and the reset chip.

```dts
/ {
	chosen {
		bootargs = " earlycon console=ttyPS1,115200 clk_ignore_unused root=/dev/ram0 rw net.ifnames=0 biosdevname=0 cma=1024M";
		stdout-path = "serial1:115200n8";
	};
	aliases {
		serial0 = &uart0;
		serial1 = &uart1;
	};

	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;

		cma_reserved: cma@0 {
			compatible = "shared-dma-pool";
			reusable;
			reg = <0x0 0x80000000 0x0 0x60000000>; /* 1.5 GB reserved at 2GB boundary (as KV260) */
			alignment = <0 0x1000>;
			no-map;
		};
	};
};
&i2c1 {
	eeprom: eeprom@50 {
		#address-cells = <1>;
		#size-cells = <1>;
		compatible = "atmel,24c64";
		reg = <0x50>;
		mac_address: mac-address@83 {
			reg = <0x83 6>;
		};
	};
};
&gem0 {
	nvmem-cells = <&mac_address>;
	nvmem-cell-names = "mac-address";
};
/ {
	axi_stream_dma_0@b0000000 {
		compatible = "axi_stream_dma";
		reg = <0x0 0xb0000000 0x0 0x10000>;
		interrupts = <0 89 4>;
		interrupt-parent = <&gic>;
	};
};
```

- [ ] **Step 4: Static checks**

Run:

```bash
diff -w hardware/XilinxKriaKv260/Yocto/recipes-bsp/device-tree/device-tree.bbappend \
        hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/device-tree.bbappend && echo bbappend-same
grep -c 'kv' hardware/XilinxKriaKr260/Yocto/zynqmp-user.conf
grep -n 'sdhci\|gem3' hardware/XilinxKriaKr260/Yocto/recipes-bsp/device-tree/files/system-user.dtsi
```

Expected: `bbappend-same` (the files differ only in the KV260's trailing whitespace). The `kv` count is `0`. The last grep prints nothing.

- [ ] **Step 5: Commit (when authorized)**

```bash
git add hardware/XilinxKriaKr260/Yocto
git commit -m "Add the KR260 Yocto machine fragment and device tree"
```

---

### Task 5: Board README

**Files:**
- Create: `hardware/XilinxKriaKr260/README.md`

**Interfaces:**
- Produces: the procedures ptp-dev Task 5 follows. Command names: `scripts/FormatSdCard.sh`, `image_update -i|-v|-p`, the JTAG boot-mode override in `docs/how-to/xsct_boot_mode.rst`.

- [ ] **Step 1: Write the README**

````markdown
# Kria KR260 Robotics Starter Kit

Board support for the KR260 carrier with the non-production K26 SOM
(`xck26-sfvc784-2lv-c`, board part `xilinx.com:kr260_som:part0:1.1`).
Status and outstanding checks: [docs/plans/kr260-support](../../docs/plans/kr260-support/README.md).

## Boot chain

The carrier straps QSPI32 boot (`MODE[3:0] = 0010`; SCK-KR sheet 6). There is
no PS SD card: the microSD sits behind a USB2244 bridge on a USB5744 hub, so the
boot ROM cannot read it, and the starter-kit SOM has no eMMC. The KV260
SD-boot rework does not apply.

1. The boot ROM runs AMD's ImageSelector from QSPI, which starts `BOOT.BIN`
   from slot A (`0x200000`) or B (`0xF80000`). Each slot holds `0xD80000` bytes.
2. Our `BOOT.BIN` (FSBL, PMU firmware, ATF, U-Boot from the design XSA)
   contains no bitstream.
3. meta-kria's U-Boot runs `boot.scr` (`boot.cmd.kria`), which scans `usb0`/`usb1`
   and boots `image.ub` (kernel, device tree, initramfs) from the SD card.
4. `startup-app-init` mounts `/dev/sda1` at `/boot` and `/dev/sda2` at `/app`,
   loads `/boot/system.bit` with `fpgautil`, then loads the DMA drivers.

The shared U-Boot netboot hooks are not used on Kria, so
`BuildYoctoProject.sh -m` has no effect on this board. Its "BOOT.BIN bitstream:
embedded" message is also wrong here: `BIF_BITSTREAM_ATTR:k26-kr` removes it.

## SD card

Build with the target's `BuildYoctoProject`, then on a Linux host:

```bash
scripts/FormatSdCard.sh /dev/sdX <target>.linux.tar.gz
```

The card holds `BOOT.BIN`, `boot.scr`, `image.ub` and `system.bit` on the
`boot` partition. `BOOT.BIN` on the card is ignored: the board boots from QSPI.

## Installing BOOT.BIN in QSPI

First install, while the board still runs AMD's firmware: use the Boot Image
Recovery Tool (hold FWUEN/SW1 while powering on, then open the web page on the
Ethernet port it reports on the serial console). Alternatively, run
`sudo xmutil bootfw_update -i BOOT.BIN` from AMD's stock SD image. Load our
`BOOT.BIN` into one slot and keep the other slot on AMD's image.

From our image, once Linux runs (`image-update` is installed):

```bash
cat /proc/mtd                  # expect AMD's partitions; image_update uses mtd3/mtd4
image_update -p                # persistent state: running and requested image
image_update -i /boot/BOOT.BIN # writes the inactive slot and requests it
reboot
image_update -v                # after a good boot: mark the running image bootable
```

An image that is not marked bootable falls back to the other slot on the next
boot.

## Recovery

- Hold FWUEN at power-on for the Boot Image Recovery Tool.
- JTAG boot without rework: write the boot-mode register through `xsdb`
  ([docs/how-to/xsct_boot_mode.rst](../../docs/how-to/xsct_boot_mode.rst)).

## Pins

The board XDC holds only SYSMON and the fan (`A12`, `HDA20`; high turns the
fan off). Targets own Pmod, SFP and PL Ethernet pins. Take them from AMD's
XilinxBoardStore `kr260_carrier` and `kr260_som` board files and the SCK-KR
schematic.
````

- [ ] **Step 2: Check the relative links**

Run:

```bash
cd hardware/XilinxKriaKr260 && for p in ../../docs/plans/kr260-support/README.md ../../docs/how-to/xsct_boot_mode.rst; do test -e $p && echo ok $p; done
```

Expected: two `ok` lines.

- [ ] **Step 3: Commit (when authorized)**

```bash
git add hardware/XilinxKriaKr260/README.md
git commit -m "Document the KR260 boot chain, QSPI update and recovery"
```

---

### Task 6: Yocto build and output checks

Depends on ptp-dev plan Task 3, which provides the `PtpKr260` XSA.

**Files:**
- Create: `docs/plans/kr260-support/checks/check_yocto_outputs.sh`

**Interfaces:**
- Consumes: `firmware/targets/PtpKr260/images/PtpKr260-*.xsa` and that target's `BuildYoctoProject` (ptp-dev).
- Produces: `firmware/targets/PtpKr260/images/PtpKr260-*.linux.tar.gz` for ptp-dev Task 5.

- [ ] **Step 1: Write the failing check**

`docs/plans/kr260-support/checks/check_yocto_outputs.sh`:

```bash
#!/bin/bash
##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
## Check a KR260 Yocto project: usage: check_yocto_outputs.sh <YoctoProjects/<Target>>
set -u
proj=${1:?usage: $0 <Yocto project dir>}
fail=0
f() { echo "FAIL: $*"; fail=1; }
for x in BOOT.BIN boot.scr image.ub system.bit; do
   [ -f "$proj/linux/$x" ] || f "missing linux/$x"
done
if [ -f "$proj/linux/BOOT.BIN" ]; then
   size=$(stat -c %s "$proj/linux/BOOT.BIN")
   [ "$size" -le $((0xD80000)) ] || f "BOOT.BIN is $size bytes, slot holds $((0xD80000))"
   [ "$size" -le $((4 * 1024 * 1024)) ] || f "BOOT.BIN is $size bytes: bitstream probably embedded"
fi
conf="$proj/build/conf/local.conf"
grep -q '^DMA_NUM_LANES = "2"' "$conf" || f "DMA_NUM_LANES is not 2"
dtb=$(ls "$proj"/build/tmp/deploy/images/zynqmp-user/*zynqmp-sck-kr-g-revB.dtb 2>/dev/null | head -1)
[ -n "$dtb" ] || f "no zynqmp-sck-kr-g-revB.dtb in deploy dir"
if [ -n "$dtb" ]; then
   strings "$dtb" | grep -q 'microchip,usb5744' || f "DTB lacks the USB5744 hub"
   strings "$dtb" | grep -q 'microchip,usb2244' || f "DTB lacks the USB2244 SD bridge"
   strings "$dtb" | grep -q 'axi_stream_dma'    || f "DTB lacks axi_stream_dma"
fi
cfg=$(find "$proj/build/tmp/work" -path '*linux-xlnx*' -name .config -not -path '*/sysroot*' 2>/dev/null | head -1)
[ -n "$cfg" ] || f "kernel .config not found"
if [ -n "$cfg" ]; then
   for o in USB_STORAGE GPIO_SLG7XL45106 DP83867_PHY MACB; do
      grep -q "^CONFIG_$o=[ym]" "$cfg" || f "kernel lacks CONFIG_$o"
   done
   grep -Eq '^CONFIG_USB_ONBOARD_(HUB|DEV)=[ym]' "$cfg" || f "kernel lacks the onboard USB hub driver"
fi
ls "$proj"/build/tmp/deploy/ipk/*/image-update_* >/dev/null 2>&1 || \
   ls "$proj"/build/tmp/deploy/rpm/*/image-update-* >/dev/null 2>&1 || f "image-update package not built"
[ $fail -eq 0 ] && echo "Yocto outputs OK"
exit $fail
```

Run it locally against an empty directory: `bash docs/plans/kr260-support/checks/check_yocto_outputs.sh /tmp/none`
Expected: `FAIL:` lines, exit status 1.

- [ ] **Step 2: Build on an rdsrv host (slac-fpga-remote-build skill)**

From ptp-dev `firmware/targets/PtpKr260` on the remote copy:

```bash
./BuildYoctoProject -f ../../targets/PtpKr260/images/<PtpKr260 XSA from ptp-dev Task 3>
```

Expected: ends with `linux.tar.gz image path: .../images/PtpKr260-...linux.tar.gz`.

- [ ] **Step 3: Run the check on the remote host**

Run: `bash <axi-soc>/docs/plans/kr260-support/checks/check_yocto_outputs.sh <ptp-dev>/firmware/build/YoctoProjects/PtpKr260`
Expected: `Yocto outputs OK`.

If a kernel option is missing: create `hardware/XilinxKriaKr260/Yocto/recipes-bsp/linux/linux-xlnx_%.bbappend`:

```
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://kr260.cfg"
```

and `hardware/XilinxKriaKr260/Yocto/recipes-bsp/linux/files/kr260.cfg` with only the missing lines from:

```
CONFIG_USB_STORAGE=y
CONFIG_USB_ONBOARD_DEV=y
CONFIG_GPIO_SLG7XL45106=y
CONFIG_DP83867_PHY=y
```

Then rebuild with `./BuildYoctoProject -f <xsa> -c` and rerun the check.
If `image-update` fails to build or install, remove its `IMAGE_INSTALL` line, record that QSPI updates use the Recovery Tool, and continue.

- [ ] **Step 4: Commit (when authorized)**

```bash
git add docs/plans/kr260-support/checks/check_yocto_outputs.sh hardware/XilinxKriaKr260/Yocto
git commit -m "Check KR260 Yocto outputs: BOOT.BIN size, overlays and kernel options"
```

---

### Task 7: Close-out after hardware acceptance

Runs after ptp-dev plan Task 6.

**Files:**
- Modify: `docs/plans/kr260-support/README.md`
- Modify: `hardware/XilinxKriaKr260/README.md`
- Modify: `docs/reference/supported_boards.rst` (only if the user wants KR260 listed)
- Delete: `docs/plans/kr260-support/checks/` and this file, once their content is folded in

- [ ] **Step 1: Record results in `docs/plans/kr260-support/README.md`**

Update "Current state" with: branch revision; Vivado and Yocto versions used; check outputs (Tasks 3 and 6); the ptp-dev acceptance results (QSPI slot and validation, fallback test, USB boot, `/dev/sda`, `eth0` jack and MAC, `fpgautil` reload); and anything not run. Remove open checks that are now answered.

- [ ] **Step 2: Fold procedures into the board README**

Add the regeneration command for the block design (the generator's steps) and any corrections found on hardware to `hardware/XilinxKriaKr260/README.md`. Then delete `docs/plans/kr260-support/checks/` and this plan file, leaving the README's "Next step" pointing at M2/M3 work (`roguetcpbridge` lane exclusion, driver recipe).

- [ ] **Step 3: Commit (when authorized)**

```bash
git add -A docs/plans/kr260-support hardware/XilinxKriaKr260/README.md
git commit -m "Record KR260 M1 bring-up and retire the execution checklist"
```
