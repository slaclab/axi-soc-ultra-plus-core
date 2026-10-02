#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RFDC param-list generator
#-----------------------------------------------------------------------------
# Description: Converts the RFDC IP core .xci (JSON) into the XRFdc_Config
#              byte blob that XRFdc_LookupConfig() reads from the devicetree
#              "param-list" property, and emits it as a devicetree fragment.
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import argparse
import json
import os
import re
import struct
import sys
import tempfile

# Exit codes
EXIT_BAD_INPUT = 2
EXIT_BAD_SIZE  = 3

# Keys holding an IEEE-754 double in XRFdc_Config; every other key is an int32
DOUBLE_MARKERS = ('_Sampling_Rate', '_Refclk_Freq', '_Fabric_Freq', '_Fs_Max', 'NCO_Freq')

# sizeof(XRFdc_Config) for the non-SDT layout of the Xilinx rfdc driver.
# The structs are packed on Linux (#pragma pack(1), xrfdc.h:359-360):
#    header         : DeviceId u32 + BaseAddr u64 + 7 x u32     =  40 bytes
#    DAC tile config: xrfdc.h:574-687                           = 256 bytes (x4)
#    ADC tile config: xrfdc.h:574-687                           = 204 bytes (x4)
LAYOUT_SIZE = 40 + 4 * 256 + 4 * 204

# Component this layout and key template were derived from
COMPONENT = 'usp_rf_data_converter:2.6'

# Number of keys the Xilinx rfdc.tcl walks (some names are used twice)
NUM_KEYS = 405

# Keys that no .xci carries (HSI-only values); the Xilinx DTG writes 0 for them
ABSENT_OK = {'C_Sysref_Master'}
for _t in range(4):
    for _b in range(4):
        ABSENT_OK.add('C_DAC_Fifo%d%d_Enable'  % (_t, _b))
        ABSENT_OK.add('C_DAC_Adder%d%d_Enable' % (_t, _b))
        ABSENT_OK.add('C_ADC_Fifo%d%d_Enable'  % (_t, _b))
ABSENT_OK = frozenset(ABSENT_OK)


class ParamListError(Exception):
    pass


def xrfdcKeys():
    # Key order of the Xilinx rfdc.tcl, which is the field order of XRFdc_Config
    k = ['DEVICE_ID', 'C_BASEADDR', 'C_High_Speed_ADC', 'C_Sysref_Master', 'C_Sysref_Master',
         'C_Sysref_Source', 'C_Sysref_Source', 'C_IP_Type', 'C_Silicon_Revision']
    for t in range(4):
        k += ['C_DAC%d_%s' % (t, s) for s in ('Enable', 'PLL_Enable', 'Sampling_Rate', 'Refclk_Freq',
              'Fabric_Freq', 'FBDIV', 'OutDiv', 'Refclk_Div', 'Band', 'Fs_Max', 'Slices', 'Link_Coupling')]
        for b in range(4):
            k += ['C_DAC_Slice%d%d_Enable' % (t, b), 'C_DAC_Invsinc_Ctrl%d%d' % (t, b),
                  'C_DAC_Mixer_Mode%d%d' % (t, b), 'C_DAC_Decoder_Mode%d%d' % (t, b)]
        for b in range(4):
            k += ['C_DAC_Data_Type%d%d' % (t, b), 'C_DAC_Data_Width%d%d' % (t, b),
                  'C_DAC_Interpolation_Mode%d%d' % (t, b), 'C_DAC_Fifo%d%d_Enable' % (t, b),
                  'C_DAC_Adder%d%d_Enable' % (t, b), 'C_DAC_Mixer_Type%d%d' % (t, b),
                  'C_DAC_NCO_Freq%d%d' % (t, b)]
    for t in range(4):
        k += ['C_ADC%d_%s' % (t, s) for s in ('Enable', 'PLL_Enable', 'Sampling_Rate', 'Refclk_Freq',
              'Fabric_Freq', 'FBDIV', 'OutDiv', 'Refclk_Div', 'Band', 'Fs_Max', 'Slices')]
        for b in range(4):
            k += ['C_ADC_Slice%d%d_Enable' % (t, b), 'C_ADC_Mixer_Mode%d%d' % (t, b)]
        for b in range(4):
            k += ['C_ADC_Data_Type%d%d' % (t, b), 'C_ADC_Data_Width%d%d' % (t, b),
                  'C_ADC_Decimation_Mode%d%d' % (t, b), 'C_ADC_Fifo%d%d_Enable' % (t, b),
                  'C_ADC_Mixer_Type%d%d' % (t, b), 'C_ADC_NCO_Freq%d%d' % (t, b)]
    assert len(k) == NUM_KEYS
    return k


def _parseInt(key, val):
    s = str(val).strip()
    low = s.lower()
    if low in ('true', 'false'):
        return 1 if low == 'true' else 0
    if s == '':
        return 0
    try:
        v = int(s, 10)
    except ValueError:
        try:
            v = int(s, 0)
        except ValueError:
            raise ParamListError("key %s: value %r is not an integer" % (key, s))
    if v < 0 or v > 0xFFFFFFFF:
        raise ParamListError("key %s: value %r does not fit an unsigned 32-bit field" % (key, s))
    return v


def _parseDouble(key, val):
    s = str(val).strip()
    low = s.lower()
    if low in ('true', 'false'):
        return float(1 if low == 'true' else 0)
    if s == '':
        return 0.0
    try:
        return float(s)
    except ValueError:
        raise ParamListError("key %s: value %r is not a number" % (key, s))


def _getValue(mp, key):
    # Returns (present, value) for a model parameter
    if key not in mp:
        return False, None
    try:
        return True, mp[key][0]['value']
    except (KeyError, IndexError, TypeError):
        raise ParamListError("key %s: unexpected model_parameters entry layout" % key)


def buildBlob(mp, baseAddr):
    out = bytearray()
    absent = set()
    for key in xrfdcKeys():
        if key == 'DEVICE_ID':
            out += struct.pack('<I', 0)
            continue
        if key == 'C_BASEADDR':
            out += struct.pack('<Q', baseAddr)
            continue
        present, val = _getValue(mp, key)
        if not present:
            if key not in ABSENT_OK:
                raise ParamListError("template key %s is missing from the .xci (newer or different IP?)" % key)
            absent.add(key)
            val = '0'
        if any(m in key for m in DOUBLE_MARKERS):
            out += struct.pack('<d', _parseDouble(key, val))
        else:
            out += struct.pack('<I', _parseInt(key, val))
    return bytes(out), absent


def loadModelParameters(xciPath):
    if not xciPath:
        raise ParamListError("empty --xci path")
    try:
        with open(xciPath, 'r') as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        raise ParamListError("cannot read %s as JSON: %s" % (xciPath, e))
    try:
        inst = doc['ip_inst']
        comp = inst['component_reference']
        mp = inst['parameters']['model_parameters']
    except (KeyError, TypeError):
        raise ParamListError("%s has no ip_inst/component_reference/parameters/model_parameters" % xciPath)
    if not str(comp).endswith(COMPONENT):
        raise ParamListError("component_reference %r is not supported (need ...%s)" % (comp, COMPONENT))
    if not isinstance(mp, dict):
        raise ParamListError("model_parameters in %s is not an object" % xciPath)
    present, ipType = _getValue(mp, 'C_IP_Type')
    if not present or _parseInt('C_IP_Type', ipType) not in (0, 1, 2):
        raise ParamListError("C_IP_Type %r is not in 0..2" % (ipType,))
    return mp


def formatFragment(blob, node, xciName):
    lines = []
    lines.append('/* Generated from %s by rfdc_param_list.py; do not edit. */' % xciName)
    lines.append('/ {')
    lines.append('\t%s {' % node)
    lines.append('\t\tparam-list = [')
    for i in range(0, len(blob), 16):
        lines.append('\t\t\t' + ' '.join('%02x' % b for b in blob[i:i + 16]))
    lines.append('\t\t];')
    lines.append('\t};')
    lines.append('};')
    return '\n'.join(lines) + '\n'


def writeAtomic(path, text):
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(prefix='.rfdc_param_list.', dir=d)
    try:
        with os.fdopen(fd, 'w') as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def main():
    ap = argparse.ArgumentParser(description='Generate the RFDC devicetree param-list from the IP .xci')
    ap.add_argument('--xci',         required=True, help='RFDC IP core .xci (JSON)')
    ap.add_argument('--out',         default=None,  help='write the devicetree fragment to this path')
    ap.add_argument('--base-addr',   default='0x490000000', help='RFDC AXI base address (default 0x490000000)')
    ap.add_argument('--node',        default='usp_rf_data_converter@490000000', help='devicetree node name')
    ap.add_argument('--expect-size', type=int, default=None, help='fail with exit 3 unless the blob has this length')
    ap.add_argument('--print-size',  action='store_true', help='print only the blob length on stdout')
    args = ap.parse_args()

    try:
        try:
            baseAddr = int(args.base_addr, 0)
        except ValueError:
            raise ParamListError("--base-addr %r is not an integer" % args.base_addr)
        if baseAddr < 0 or baseAddr > 0xFFFFFFFFFFFFFFFF:
            raise ParamListError("--base-addr %r does not fit 64 bits" % args.base_addr)
        if not re.match(r'^[A-Za-z0-9,._+@-]+$', args.node):
            raise ParamListError("--node %r is not a valid devicetree node name" % args.node)
        mp = loadModelParameters(args.xci)
        blob, _ = buildBlob(mp, baseAddr)
        if len(blob) != LAYOUT_SIZE:
            raise ParamListError("internal layout error: blob is %d bytes, layout says %d" % (len(blob), LAYOUT_SIZE))
    except ParamListError as e:
        sys.stderr.write('rfdc_param_list: %s\n' % e)
        return EXIT_BAD_INPUT

    if args.expect_size is not None and args.expect_size != len(blob):
        sys.stderr.write('rfdc_param_list: generated size %d differs from expected size %d\n' % (len(blob), args.expect_size))
        return EXIT_BAD_SIZE

    if args.out:
        writeAtomic(args.out, formatFragment(blob, args.node, os.path.basename(args.xci)))
    if args.print_size:
        sys.stdout.write('%d\n' % len(blob))
    return 0


if __name__ == '__main__':
    sys.exit(main())
