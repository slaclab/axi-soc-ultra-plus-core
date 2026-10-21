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
