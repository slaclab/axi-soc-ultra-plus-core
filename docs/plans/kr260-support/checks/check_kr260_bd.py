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
