#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RF data converter param-list encoder and device tree check
#-----------------------------------------------------------------------------
# Description: Encodes the RFDC param-list from the IP .xci and checks it in a built DTB
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

"""Build-time encoder and deploy-time check for the RFDC device tree param-list.

Why it exists. On Linux, librfdc's XRFdc_LookupConfig copies the whole
XRFdc_Config structure (1880 bytes, packed) out of the RFDC node's
param-list property, with no check on how many bytes it actually read. The
device tree generator (device-tree-xlnx rfdc.tcl) writes that property only
for an RF data converter IP it can see in the XSA. An RFDC instantiated from
HDL outside the block design is invisible to it, so such a board has to
supply the node itself, and an empty param-list there hands the driver
uninitialized memory in place of its configuration, silently.

This tool closes that gap with the generator's own rules. The encode
subcommand reads the IP's .xci (the configuration the bitstream was built
from) and applies rfdc.tcl's parameter list and encoding, name for name and
in the same order, to produce the property as a DTS fragment. The check
subcommand walks a built DTB and reports an RFDC node whose param-list is
missing, of the wrong length, of an unexpected or unknown IPType, or with an
implausible tile enable or sampling rate, and, given the .xci, any byte that
differs from a fresh encode of it. The decode subcommand prints the header
and tile fields of a param-list by their packed offsets.

What it cannot show. It says nothing about an RFDC node the generator
emits from a block design (that path is rfdc.tcl's), and nothing about how
the driver behaves at runtime with the configuration it is given. It checks
the bytes that will boot, not what the converter does with them.

Invocation:

    python3 rfdc_param_list.py encode --xci RfDataConverterIpCore.xci --base 0x490000000 -o rfdc-param-list.dtsi
    python3 rfdc_param_list.py check --dtb system-top.dtb --board SlacRfmcCarrier --policy fail --expect-iptype 2
    python3 rfdc_param_list.py check --dtb system-top.dtb --meta-user-bsp sources/meta-user/recipes-bsp --policy warn
    python3 rfdc_param_list.py check --dtb system-top.dtb --board SlacRfmcCarrier --policy fail --expect-iptype 2 --xci RfDataConverterIpCore.xci --base 0x490000000
    python3 rfdc_param_list.py decode --dtb system-top.dtb
    python3 rfdc_param_list.py decode --param-list zcu111-param-list.hex
"""

import argparse
import hashlib
import json
import math
import os
import struct
import sys

# The parameter names below are copied verbatim, and in order, from the
# add_param_list_property call in github.com/Xilinx/device-tree-xlnx,
# path rfdc/data/rfdc.tcl, commit a40943b0f4d30d656d035ae6338f81034820ca98
# (the device tree generator revision the rel-v2026.1 build pins). Their
# order is the byte order of XRFdc_Config; do not edit, sort or dedupe them
# (C_Sysref_Master and C_Sysref_Source each appear twice on purpose, once
# per converter type).
PARAM_NAMES_C = (
    'DEVICE_ID', 'C_BASEADDR', 'C_High_Speed_ADC', 'C_Sysref_Master', 'C_Sysref_Master', 'C_Sysref_Source', 'C_Sysref_Source', 'C_IP_Type', 'C_Silicon_Revision',
    'C_DAC0_Enable', 'C_DAC0_PLL_Enable', 'C_DAC0_Sampling_Rate', 'C_DAC0_Refclk_Freq', 'C_DAC0_Fabric_Freq', 'C_DAC0_FBDIV', 'C_DAC0_OutDiv', 'C_DAC0_Refclk_Div', 'C_DAC0_Band', 'C_DAC0_Fs_Max', 'C_DAC0_Slices', 'DAC0_Link_Coupling',
    'C_DAC_Slice00_Enable', 'C_DAC_Invsinc_Ctrl00', 'C_DAC_Mixer_Mode00', 'C_DAC_Decoder_Mode00',
    'C_DAC_Slice01_Enable', 'C_DAC_Invsinc_Ctrl01', 'C_DAC_Mixer_Mode01', 'C_DAC_Decoder_Mode01',
    'C_DAC_Slice02_Enable', 'C_DAC_Invsinc_Ctrl02', 'C_DAC_Mixer_Mode02', 'C_DAC_Decoder_Mode02',
    'C_DAC_Slice03_Enable', 'C_DAC_Invsinc_Ctrl03', 'C_DAC_Mixer_Mode03', 'C_DAC_Decoder_Mode03',
    'C_DAC_Data_Type00', 'C_DAC_Data_Width00', 'C_DAC_Interpolation_Mode00', 'C_DAC_Fifo00_Enable', 'C_DAC_Adder00_Enable', 'C_DAC_Mixer_Type00', 'C_DAC_NCO_Freq00',
    'C_DAC_Data_Type01', 'C_DAC_Data_Width01', 'C_DAC_Interpolation_Mode01', 'C_DAC_Fifo01_Enable', 'C_DAC_Adder01_Enable', 'C_DAC_Mixer_Type01', 'C_DAC_NCO_Freq01',
    'C_DAC_Data_Type02', 'C_DAC_Data_Width02', 'C_DAC_Interpolation_Mode02', 'C_DAC_Fifo02_Enable', 'C_DAC_Adder02_Enable', 'C_DAC_Mixer_Type02', 'C_DAC_NCO_Freq02',
    'C_DAC_Data_Type03', 'C_DAC_Data_Width03', 'C_DAC_Interpolation_Mode03', 'C_DAC_Fifo03_Enable', 'C_DAC_Adder03_Enable', 'C_DAC_Mixer_Type03', 'C_DAC_NCO_Freq03',
    'C_DAC1_Enable', 'C_DAC1_PLL_Enable', 'C_DAC1_Sampling_Rate', 'C_DAC1_Refclk_Freq', 'C_DAC1_Fabric_Freq', 'C_DAC1_FBDIV', 'C_DAC1_OutDiv', 'C_DAC1_Refclk_Div', 'C_DAC1_Band', 'C_DAC1_Fs_Max', 'C_DAC1_Slices', 'DAC1_Link_Coupling',
    'C_DAC_Slice10_Enable', 'C_DAC_Invsinc_Ctrl10', 'C_DAC_Mixer_Mode10', 'C_DAC_Decoder_Mode10',
    'C_DAC_Slice11_Enable', 'C_DAC_Invsinc_Ctrl11', 'C_DAC_Mixer_Mode11', 'C_DAC_Decoder_Mode11',
    'C_DAC_Slice12_Enable', 'C_DAC_Invsinc_Ctrl12', 'C_DAC_Mixer_Mode12', 'C_DAC_Decoder_Mode12',
    'C_DAC_Slice13_Enable', 'C_DAC_Invsinc_Ctrl13', 'C_DAC_Mixer_Mode13', 'C_DAC_Decoder_Mode13',
    'C_DAC_Data_Type10', 'C_DAC_Data_Width10', 'C_DAC_Interpolation_Mode10', 'C_DAC_Fifo10_Enable', 'C_DAC_Adder10_Enable', 'C_DAC_Mixer_Type10', 'C_DAC_NCO_Freq10',
    'C_DAC_Data_Type11', 'C_DAC_Data_Width11', 'C_DAC_Interpolation_Mode11', 'C_DAC_Fifo11_Enable', 'C_DAC_Adder11_Enable', 'C_DAC_Mixer_Type11', 'C_DAC_NCO_Freq11',
    'C_DAC_Data_Type12', 'C_DAC_Data_Width12', 'C_DAC_Interpolation_Mode12', 'C_DAC_Fifo12_Enable', 'C_DAC_Adder12_Enable', 'C_DAC_Mixer_Type12', 'C_DAC_NCO_Freq12',
    'C_DAC_Data_Type13', 'C_DAC_Data_Width13', 'C_DAC_Interpolation_Mode13', 'C_DAC_Fifo13_Enable', 'C_DAC_Adder13_Enable', 'C_DAC_Mixer_Type13', 'C_DAC_NCO_Freq13',
    'C_DAC2_Enable', 'C_DAC2_PLL_Enable', 'C_DAC2_Sampling_Rate', 'C_DAC2_Refclk_Freq', 'C_DAC2_Fabric_Freq', 'C_DAC2_FBDIV', 'C_DAC2_OutDiv', 'C_DAC2_Refclk_Div', 'C_DAC2_Band', 'C_DAC2_Fs_Max', 'C_DAC2_Slices', 'DAC2_Link_Coupling',
    'C_DAC_Slice20_Enable', 'C_DAC_Invsinc_Ctrl20', 'C_DAC_Mixer_Mode20', 'C_DAC_Decoder_Mode20',
    'C_DAC_Slice21_Enable', 'C_DAC_Invsinc_Ctrl21', 'C_DAC_Mixer_Mode21', 'C_DAC_Decoder_Mode21',
    'C_DAC_Slice22_Enable', 'C_DAC_Invsinc_Ctrl22', 'C_DAC_Mixer_Mode22', 'C_DAC_Decoder_Mode22',
    'C_DAC_Slice23_Enable', 'C_DAC_Invsinc_Ctrl23', 'C_DAC_Mixer_Mode23', 'C_DAC_Decoder_Mode23',
    'C_DAC_Data_Type20', 'C_DAC_Data_Width20', 'C_DAC_Interpolation_Mode20', 'C_DAC_Fifo20_Enable', 'C_DAC_Adder20_Enable', 'C_DAC_Mixer_Type20', 'C_DAC_NCO_Freq20',
    'C_DAC_Data_Type21', 'C_DAC_Data_Width21', 'C_DAC_Interpolation_Mode21', 'C_DAC_Fifo21_Enable', 'C_DAC_Adder21_Enable', 'C_DAC_Mixer_Type21', 'C_DAC_NCO_Freq21',
    'C_DAC_Data_Type22', 'C_DAC_Data_Width22', 'C_DAC_Interpolation_Mode22', 'C_DAC_Fifo22_Enable', 'C_DAC_Adder22_Enable', 'C_DAC_Mixer_Type22', 'C_DAC_NCO_Freq22',
    'C_DAC_Data_Type23', 'C_DAC_Data_Width23', 'C_DAC_Interpolation_Mode23', 'C_DAC_Fifo23_Enable', 'C_DAC_Adder23_Enable', 'C_DAC_Mixer_Type23', 'C_DAC_NCO_Freq23',
    'C_DAC3_Enable', 'C_DAC3_PLL_Enable', 'C_DAC3_Sampling_Rate', 'C_DAC3_Refclk_Freq', 'C_DAC3_Fabric_Freq', 'C_DAC3_FBDIV', 'C_DAC3_OutDiv', 'C_DAC3_Refclk_Div', 'C_DAC3_Band', 'C_DAC3_Fs_Max', 'C_DAC3_Slices', 'DAC3_Link_Coupling',
    'C_DAC_Slice30_Enable', 'C_DAC_Invsinc_Ctrl30', 'C_DAC_Mixer_Mode30', 'C_DAC_Decoder_Mode30',
    'C_DAC_Slice31_Enable', 'C_DAC_Invsinc_Ctrl31', 'C_DAC_Mixer_Mode31', 'C_DAC_Decoder_Mode31',
    'C_DAC_Slice32_Enable', 'C_DAC_Invsinc_Ctrl32', 'C_DAC_Mixer_Mode32', 'C_DAC_Decoder_Mode32',
    'C_DAC_Slice33_Enable', 'C_DAC_Invsinc_Ctrl33', 'C_DAC_Mixer_Mode33', 'C_DAC_Decoder_Mode33',
    'C_DAC_Data_Type30', 'C_DAC_Data_Width30', 'C_DAC_Interpolation_Mode30', 'C_DAC_Fifo30_Enable', 'C_DAC_Adder30_Enable', 'C_DAC_Mixer_Type30', 'C_DAC_NCO_Freq30',
    'C_DAC_Data_Type31', 'C_DAC_Data_Width31', 'C_DAC_Interpolation_Mode31', 'C_DAC_Fifo31_Enable', 'C_DAC_Adder31_Enable', 'C_DAC_Mixer_Type31', 'C_DAC_NCO_Freq31',
    'C_DAC_Data_Type32', 'C_DAC_Data_Width32', 'C_DAC_Interpolation_Mode32', 'C_DAC_Fifo32_Enable', 'C_DAC_Adder32_Enable', 'C_DAC_Mixer_Type32', 'C_DAC_NCO_Freq32',
    'C_DAC_Data_Type33', 'C_DAC_Data_Width33', 'C_DAC_Interpolation_Mode33', 'C_DAC_Fifo33_Enable', 'C_DAC_Adder33_Enable', 'C_DAC_Mixer_Type33', 'C_DAC_NCO_Freq33',
    'C_ADC0_Enable', 'C_ADC0_PLL_Enable', 'C_ADC0_Sampling_Rate', 'C_ADC0_Refclk_Freq', 'C_ADC0_Fabric_Freq', 'C_ADC0_FBDIV', 'C_ADC0_OutDiv', 'C_ADC0_Refclk_Div', 'C_ADC0_Band', 'C_ADC0_Fs_Max', 'C_ADC0_Slices',
    'C_ADC_Slice00_Enable', 'C_ADC_Mixer_Mode00',
    'C_ADC_Slice01_Enable', 'C_ADC_Mixer_Mode01',
    'C_ADC_Slice02_Enable', 'C_ADC_Mixer_Mode02',
    'C_ADC_Slice03_Enable', 'C_ADC_Mixer_Mode03',
    'C_ADC_Data_Type00', 'C_ADC_Data_Width00', 'C_ADC_Decimation_Mode00', 'C_ADC_Fifo00_Enable', 'C_ADC_Mixer_Type00', 'C_ADC_NCO_Freq00',
    'C_ADC_Data_Type01', 'C_ADC_Data_Width01', 'C_ADC_Decimation_Mode01', 'C_ADC_Fifo01_Enable', 'C_ADC_Mixer_Type01', 'C_ADC_NCO_Freq01',
    'C_ADC_Data_Type02', 'C_ADC_Data_Width02', 'C_ADC_Decimation_Mode02', 'C_ADC_Fifo02_Enable', 'C_ADC_Mixer_Type02', 'C_ADC_NCO_Freq02',
    'C_ADC_Data_Type03', 'C_ADC_Data_Width03', 'C_ADC_Decimation_Mode03', 'C_ADC_Fifo03_Enable', 'C_ADC_Mixer_Type03', 'C_ADC_NCO_Freq03',
    'C_ADC1_Enable', 'C_ADC1_PLL_Enable', 'C_ADC1_Sampling_Rate', 'C_ADC1_Refclk_Freq', 'C_ADC1_Fabric_Freq', 'C_ADC1_FBDIV', 'C_ADC1_OutDiv', 'C_ADC1_Refclk_Div', 'C_ADC1_Band', 'C_ADC1_Fs_Max', 'C_ADC1_Slices',
    'C_ADC_Slice10_Enable', 'C_ADC_Mixer_Mode10',
    'C_ADC_Slice11_Enable', 'C_ADC_Mixer_Mode11',
    'C_ADC_Slice12_Enable', 'C_ADC_Mixer_Mode12',
    'C_ADC_Slice13_Enable', 'C_ADC_Mixer_Mode13',
    'C_ADC_Data_Type10', 'C_ADC_Data_Width10', 'C_ADC_Decimation_Mode10', 'C_ADC_Fifo10_Enable', 'C_ADC_Mixer_Type10', 'C_ADC_NCO_Freq10',
    'C_ADC_Data_Type11', 'C_ADC_Data_Width11', 'C_ADC_Decimation_Mode11', 'C_ADC_Fifo11_Enable', 'C_ADC_Mixer_Type11', 'C_ADC_NCO_Freq11',
    'C_ADC_Data_Type12', 'C_ADC_Data_Width12', 'C_ADC_Decimation_Mode12', 'C_ADC_Fifo12_Enable', 'C_ADC_Mixer_Type12', 'C_ADC_NCO_Freq12',
    'C_ADC_Data_Type13', 'C_ADC_Data_Width13', 'C_ADC_Decimation_Mode13', 'C_ADC_Fifo13_Enable', 'C_ADC_Mixer_Type13', 'C_ADC_NCO_Freq13',
    'C_ADC2_Enable', 'C_ADC2_PLL_Enable', 'C_ADC2_Sampling_Rate', 'C_ADC2_Refclk_Freq', 'C_ADC2_Fabric_Freq', 'C_ADC2_FBDIV', 'C_ADC2_OutDiv', 'C_ADC2_Refclk_Div', 'C_ADC2_Band', 'C_ADC2_Fs_Max', 'C_ADC2_Slices',
    'C_ADC_Slice20_Enable', 'C_ADC_Mixer_Mode20',
    'C_ADC_Slice21_Enable', 'C_ADC_Mixer_Mode21',
    'C_ADC_Slice22_Enable', 'C_ADC_Mixer_Mode22',
    'C_ADC_Slice23_Enable', 'C_ADC_Mixer_Mode23',
    'C_ADC_Data_Type20', 'C_ADC_Data_Width20', 'C_ADC_Decimation_Mode20', 'C_ADC_Fifo20_Enable', 'C_ADC_Mixer_Type20', 'C_ADC_NCO_Freq20',
    'C_ADC_Data_Type21', 'C_ADC_Data_Width21', 'C_ADC_Decimation_Mode21', 'C_ADC_Fifo21_Enable', 'C_ADC_Mixer_Type21', 'C_ADC_NCO_Freq21',
    'C_ADC_Data_Type22', 'C_ADC_Data_Width22', 'C_ADC_Decimation_Mode22', 'C_ADC_Fifo22_Enable', 'C_ADC_Mixer_Type22', 'C_ADC_NCO_Freq22',
    'C_ADC_Data_Type23', 'C_ADC_Data_Width23', 'C_ADC_Decimation_Mode23', 'C_ADC_Fifo23_Enable', 'C_ADC_Mixer_Type23', 'C_ADC_NCO_Freq23',
    'C_ADC3_Enable', 'C_ADC3_PLL_Enable', 'C_ADC3_Sampling_Rate', 'C_ADC3_Refclk_Freq', 'C_ADC3_Fabric_Freq', 'C_ADC3_FBDIV', 'C_ADC3_OutDiv', 'C_ADC3_Refclk_Div', 'C_ADC3_Band', 'C_ADC3_Fs_Max', 'C_ADC3_Slices',
    'C_ADC_Slice30_Enable', 'C_ADC_Mixer_Mode30',
    'C_ADC_Slice31_Enable', 'C_ADC_Mixer_Mode31',
    'C_ADC_Slice32_Enable', 'C_ADC_Mixer_Mode32',
    'C_ADC_Slice33_Enable', 'C_ADC_Mixer_Mode33',
    'C_ADC_Data_Type30', 'C_ADC_Data_Width30', 'C_ADC_Decimation_Mode30', 'C_ADC_Fifo30_Enable', 'C_ADC_Mixer_Type30', 'C_ADC_NCO_Freq30',
    'C_ADC_Data_Type31', 'C_ADC_Data_Width31', 'C_ADC_Decimation_Mode31', 'C_ADC_Fifo31_Enable', 'C_ADC_Mixer_Type31', 'C_ADC_NCO_Freq31',
    'C_ADC_Data_Type32', 'C_ADC_Data_Width32', 'C_ADC_Decimation_Mode32', 'C_ADC_Fifo32_Enable', 'C_ADC_Mixer_Type32', 'C_ADC_NCO_Freq32',
    'C_ADC_Data_Type33', 'C_ADC_Data_Width33', 'C_ADC_Decimation_Mode33', 'C_ADC_Fifo33_Enable', 'C_ADC_Mixer_Type33', 'C_ADC_NCO_Freq33',
)

# sizeof(XRFdc_Config) and offsetof(XRFdc_Config, IPType) for the Linux
# build of librfdc, whose xrfdc.h wraps the structures in #pragma pack(1).
CONFIG_SIZE_C   = 1880
IPTYPE_OFFSET_C = 32

# The highest IPType the check accepts as a known generation, kept equal to
# PyRFdc's PYRFDC_IPTYPE_MAX_KNOWN so the build and the driver's generation
# gate agree on what is out of range. A board that sets no expected IPType
# still has a value above this caught, such as the 255 of an erased byte.
IPTYPE_MAX_KNOWN_C = 3

# Packed XRFdc_Config tile layout: four DAC tiles follow the 40 header bytes,
# then four ADC tiles. The tile-relative offsets below are shared by both
# tile types up to NumSlices; only the DAC tile carries LinkCoupling.
DAC_TILE_BASE_C   = 40
DAC_TILE_STRIDE_C = 256
ADC_TILE_BASE_C   = 1064
ADC_TILE_STRIDE_C = 204

TILE_ENABLE_C           = 0
TILE_PLL_ENABLE_C       = 4
TILE_SAMPLING_RATE_C    = 8
TILE_REFCLK_FREQ_C      = 16
TILE_FABCLK_FREQ_C      = 24
TILE_FEEDBACK_DIV_C     = 32
TILE_OUTPUT_DIV_C       = 36
TILE_REFCLK_DIV_C       = 40
TILE_MULTIBAND_CONFIG_C = 44
TILE_MAX_SAMPLE_RATE_C  = 48
TILE_NUM_SLICES_C       = 56
DAC_LINK_COUPLING_C     = 60

# An enabled tile's SamplingRate (GS/s) must lie strictly between 0 and this.
# The fastest RFSoC converter (a Gen3 DAC) tops out at 10 GS/s, so a value
# outside the range was never encoded from a real IP; it is not a design choice.
SAMPLING_RATE_MAX_C = 11.0

COMPONENT_REFERENCE_C = 'xilinx.com:ip:usp_rf_data_converter:2.6'

# librfdc's XRFDC_COMPATIBLE_STRING: the driver accepts a node whose
# compatible starts with this prefix.
COMPATIBLE_PREFIX_C = 'xlnx,usp-rf-data-converter-'

# rfdc.tcl encodes a parameter whose name contains one of these as a
# little-endian 8-byte double; every other parameter is a 4-byte int.
DOUBLE_MARKERS_C = ('_Sampling_Rate', '_Refclk_Freq', '_Fabric_Freq', '_Fs_Max', '_NCO_Freq')

FDT_MAGIC_C       = 0xd00dfeed
FDT_HEADER_SIZE_C = 40
FDT_BEGIN_NODE_C  = 1
FDT_END_NODE_C    = 2
FDT_PROP_C        = 3
FDT_NOP_C         = 4
FDT_END_C         = 9


def load_xci_params(path):
    """Return the .xci's parameters as one dict keyed by lower-cased name.

    component_parameters are merged first and model_parameters second, so
    where both carry a name the model (C_) value wins, which is the value
    the IP's CONFIG.C_* properties would expose to rfdc.tcl.
    """
    try:
        with open(path, 'r') as f:
            xci = json.load(f)
    except (OSError, ValueError) as e:
        raise RuntimeError(f'Cannot read {path} as an .xci JSON file: {e}')

    inst = xci.get('ip_inst') if isinstance(xci, dict) else None
    ref  = inst.get('component_reference') if isinstance(inst, dict) else None
    if ref != COMPONENT_REFERENCE_C:
        raise RuntimeError(
            f'{path} is not a {COMPONENT_REFERENCE_C} .xci (ip_inst.component_reference is {ref!r})')

    params = inst.get('parameters')
    if not isinstance(params, dict):
        raise RuntimeError(f'{path} has no ip_inst.parameters')

    merged = {}
    for group in ('component_parameters', 'model_parameters'):
        entries = params.get(group, {})
        if not isinstance(entries, dict):
            raise RuntimeError(f'{path} ip_inst.parameters.{group} is not an object')
        for name, value in entries.items():
            first = value[0] if isinstance(value, list) and value else {}
            raw   = first.get('value', '') if isinstance(first, dict) else ''
            merged[name.lower()] = '' if raw is None else str(raw)
    return merged


def encode(xci_path, base_addr):
    """Encode the .xci into the param-list bytes, applying rfdc.tcl's rules.

    base_addr fills BaseAddr. It is passed in rather than read from the
    .xci because rfdc.tcl's C_BASEADDR handling takes the first ten
    characters of the value, which truncates a nine-digit hex address.
    """
    if not 0 <= base_addr < (1 << 64):
        raise RuntimeError(f'Base address {base_addr:#x} does not fit in 64 bits')

    params = load_xci_params(xci_path)
    data   = bytearray()
    for name in PARAM_NAMES_C:
        if name == 'DEVICE_ID':
            # The index of this instance among the RFDC IPs, as rfdc.tcl
            # computes it; the build instantiates one.
            data += struct.pack('<i', 0)
            continue

        value = params.get(name.lower(), '').strip()
        if value == '':
            value = '0'

        if any(marker in name for marker in DOUBLE_MARKERS_C):
            try:
                data += struct.pack('<d', float(value))
            except ValueError:
                raise RuntimeError(f'Parameter {name} value {value!r} in {xci_path} is not a number')
        elif 'C_BASEADDR' in name:
            data += struct.pack('<II', base_addr & 0xffffffff, base_addr >> 32)
        else:
            if value.lower() == 'true':
                number = 1
            elif value.lower() == 'false':
                number = 0
            else:
                try:
                    number = int(value, 0)
                except ValueError:
                    raise RuntimeError(f'Parameter {name} value {value!r} in {xci_path} is not an integer')
            try:
                data += struct.pack('<i', number)
            except struct.error:
                raise RuntimeError(f'Parameter {name} value {value!r} in {xci_path} does not fit in 32 bits')

    if len(data) != CONFIG_SIZE_C:
        raise RuntimeError(
            f'Encoded {len(data)} bytes from {xci_path}, expected {CONFIG_SIZE_C}')
    return bytes(data)


def fragment_text(data, xci_path):
    """Return the DTS fragment that carries data as the param-list property."""
    with open(xci_path, 'rb') as f:
        digest = hashlib.sha256(f.read()).hexdigest()

    lines = [
        f'/* Generated by rfdc_param_list.py from {os.path.basename(xci_path)} '
        f'(sha256 {digest}); do not edit */',
        'param-list = [',
    ]
    for i in range(0, len(data), 16):
        lines.append('\t' + ' '.join(f'{b:02x}' for b in data[i:i + 16]))
    lines.append('];')
    return '\n'.join(lines) + '\n'


def _field_layout():
    """Return (name, offset, struct format) for every decoded field, in byte order.

    Built from the literal packed offsets, deliberately not from the
    encoder's walk over PARAM_NAMES_C, so a disagreement between the name
    list and the structure shows up as a wrong decode instead of agreeing
    with itself.
    """
    layout = [
        ('DeviceId',        0,  '<I'),
        ('BaseAddr',        4,  '<Q'),
        ('ADCType',         12, '<I'),
        ('MasterADCTile',   16, '<I'),
        ('MasterDACTile',   20, '<I'),
        ('ADCSysRefSource', 24, '<I'),
        ('DACSysRefSource', 28, '<I'),
        ('IPType',          32, '<I'),
        ('SiRevision',      36, '<I'),
    ]
    tile_fields = [
        ('Enable',          TILE_ENABLE_C,           '<I'),
        ('PLLEnable',       TILE_PLL_ENABLE_C,       '<I'),
        ('SamplingRate',    TILE_SAMPLING_RATE_C,    '<d'),
        ('RefClkFreq',      TILE_REFCLK_FREQ_C,      '<d'),
        ('FabClkFreq',      TILE_FABCLK_FREQ_C,      '<d'),
        ('FeedbackDiv',     TILE_FEEDBACK_DIV_C,     '<I'),
        ('OutputDiv',       TILE_OUTPUT_DIV_C,       '<I'),
        ('RefClkDiv',       TILE_REFCLK_DIV_C,       '<I'),
        ('MultibandConfig', TILE_MULTIBAND_CONFIG_C, '<I'),
        ('MaxSampleRate',   TILE_MAX_SAMPLE_RATE_C,  '<d'),
        ('NumSlices',       TILE_NUM_SLICES_C,       '<I'),
    ]
    for kind, base, stride in (('DAC', DAC_TILE_BASE_C, DAC_TILE_STRIDE_C),
                               ('ADC', ADC_TILE_BASE_C, ADC_TILE_STRIDE_C)):
        extra = [('LinkCoupling', DAC_LINK_COUPLING_C, '<I')] if kind == 'DAC' else []
        for tile in range(4):
            for name, offset, fmt in tile_fields + extra:
                layout.append((f'{kind}{tile}.{name}', base + tile * stride + offset, fmt))
    return layout


def decode(data):
    """Return [(name, value)] for the header and tile fields of a param-list."""
    if len(data) != CONFIG_SIZE_C:
        raise RuntimeError(f'param-list length {len(data)}, expected {CONFIG_SIZE_C}')
    return [(name, struct.unpack_from(fmt, data, offset)[0]) for name, offset, fmt in _field_layout()]


def field_at(offset):
    """Name the decoded field that holds byte offset, or None if no decoded field does."""
    for name, start, fmt in _field_layout():
        if start <= offset < start + struct.calcsize(fmt):
            return name
    return None


def read_hex_param_list(path):
    """Read a param-list stored as whitespace-separated hex pairs; # starts a comment line."""
    data = bytearray()
    with open(path, 'r') as f:
        for number, line in enumerate(f, 1):
            if line.lstrip().startswith('#'):
                continue
            for token in line.split():
                if len(token) != 2 or token.strip('0123456789abcdefABCDEF'):
                    raise RuntimeError(f'{path}:{number}: {token!r} is not a two-digit hex byte')
                data.append(int(token, 16))
    return bytes(data)


def _node_path(names):
    return '/' + '/'.join(names[1:])


def walk_dtb(blob):
    """Return {node path: {property name: bytes}} for a flattened device tree.

    Every offset and length is checked against the blob before it is used,
    so a truncated or corrupt DTB raises one RuntimeError naming the fault
    instead of an IndexError or struct.error from deep in the walk.
    """
    if len(blob) < FDT_HEADER_SIZE_C:
        raise RuntimeError(f'malformed DTB: {len(blob)} bytes is shorter than the header')

    magic, total, off_struct, off_strings = struct.unpack_from('>4I', blob, 0)
    if magic != FDT_MAGIC_C:
        raise RuntimeError(f'malformed DTB: magic {magic:#010x}, expected {FDT_MAGIC_C:#010x}')
    if total > len(blob):
        raise RuntimeError(f'malformed DTB: totalsize {total} exceeds the {len(blob)} byte file')
    if off_struct >= total or off_strings >= total:
        raise RuntimeError('malformed DTB: structure or strings block starts past totalsize')

    nodes = {}
    names = []
    p     = off_struct
    while True:
        if p + 4 > total:
            raise RuntimeError('malformed DTB: structure block runs past totalsize')
        token, = struct.unpack_from('>I', blob, p)
        p += 4

        if token == FDT_BEGIN_NODE_C:
            end = blob.find(b'\0', p, total)
            if end < 0:
                raise RuntimeError(f'malformed DTB: unterminated node name at offset {p}')
            names.append(blob[p:end].decode('ascii', 'replace'))
            nodes[_node_path(names)] = {}
            p = (end + 4) & ~3

        elif token == FDT_END_NODE_C:
            if not names:
                raise RuntimeError(f'malformed DTB: unbalanced end of node at offset {p - 4}')
            names.pop()

        elif token == FDT_PROP_C:
            if not names:
                raise RuntimeError(f'malformed DTB: property outside any node at offset {p - 4}')
            if p + 8 > total:
                raise RuntimeError('malformed DTB: property header runs past totalsize')
            length, name_off = struct.unpack_from('>2I', blob, p)
            p += 8
            if p + length > total:
                raise RuntimeError(f'malformed DTB: property of {length} bytes at offset {p} runs past totalsize')
            start = off_strings + name_off
            end   = blob.find(b'\0', start, total) if start < total else -1
            if end < 0:
                raise RuntimeError(f'malformed DTB: property name offset {name_off} is outside the strings block')
            name = blob[start:end].decode('ascii', 'replace')
            nodes[_node_path(names)][name] = bytes(blob[p:p + length])
            p = (p + length + 3) & ~3

        elif token == FDT_NOP_C:
            continue

        elif token == FDT_END_C:
            if names:
                raise RuntimeError('malformed DTB: structure block ends inside a node')
            return nodes

        else:
            raise RuntimeError(f'malformed DTB: unknown token {token:#x} at offset {p - 4}')


def find_rfdc_nodes(nodes):
    """Return the paths of nodes whose compatible list names an RFDC."""
    prefix = COMPATIBLE_PREFIX_C.encode('ascii')
    found  = []
    for path, props in nodes.items():
        entries = props.get('compatible', b'').split(b'\0')
        if any(entry.startswith(prefix) for entry in entries):
            found.append(path)
    return found


def board_name(meta_user_bsp):
    """Name the board from the meta-user recipes-bsp link.

    The build scripts link sources/meta-user/recipes-bsp to
    hardware/<Board>/Yocto/recipes-bsp, so resolving it names the board
    without any per-board setting.
    """
    parts = os.path.realpath(meta_user_bsp).split(os.sep)
    if (len(parts) >= 4 and parts[-4] == 'hardware' and parts[-3]
            and parts[-2] == 'Yocto' and parts[-1] == 'recipes-bsp'):
        return parts[-3]
    return 'unknown board'


def check_dtb(dtb_path, board, policy, expect_iptype=None, xci_path=None, base_addr=None):
    """Return the problems found with the RFDC param-list in dtb_path.

    board is the name the caller reports problems under; the checks do not
    depend on it. Under policy 'warn' a missing DTB or a DTB with no RFDC
    node is not a problem, so a machine that deploys no such DTB, or has
    no RF data converter at all, stays silent. Under 'fail' both are.

    The length and IPType checks catch an empty or foreign param-list; the
    plausibility checks catch a full-length one that was never encoded from
    a real IP (erased flash, a hand-edited array). Neither can catch a
    plausible configuration of the wrong design, so when xci_path and
    base_addr are given the param-list must also equal a fresh encode of
    that .xci, byte for byte.
    """
    strict = (policy == 'fail')

    if not os.path.isfile(dtb_path):
        return [f'no DTB at {dtb_path}'] if strict else []

    try:
        with open(dtb_path, 'rb') as f:
            blob = f.read()
        nodes = walk_dtb(blob)
    except OSError as e:
        return [f'cannot read {dtb_path}: {e}']
    except RuntimeError as e:
        return [str(e)]

    rfdc = find_rfdc_nodes(nodes)
    if not rfdc:
        return [f'no usp_rf_data_converter node (compatible {COMPATIBLE_PREFIX_C}*) in {dtb_path}'] if strict else []
    if len(rfdc) > 1:
        return [f'{len(rfdc)} RFDC nodes ({", ".join(rfdc)}), expected exactly one']

    node  = rfdc[0]
    param = nodes[node].get('param-list')
    if param is None:
        return [f'{node} has no param-list']

    problems = []
    if len(param) != CONFIG_SIZE_C:
        problems.append(f'param-list length {len(param)}, expected {CONFIG_SIZE_C}')
    if len(param) >= IPTYPE_OFFSET_C + 4:
        iptype, = struct.unpack_from('<I', param, IPTYPE_OFFSET_C)
        if expect_iptype is not None and iptype != expect_iptype:
            problems.append(f'IPType {iptype}, expected {expect_iptype}')
        if iptype > IPTYPE_MAX_KNOWN_C:
            problems.append(f'IPType {iptype} is above the highest known generation {IPTYPE_MAX_KNOWN_C}')
    if len(param) != CONFIG_SIZE_C:
        return problems

    fields = dict(decode(param))
    if fields['DeviceId'] != 0:
        problems.append(f'DeviceId {fields["DeviceId"]}, expected 0')
    for kind in ('DAC', 'ADC'):
        for tile in range(4):
            prefix = f'{kind}{tile}'
            enable = fields[f'{prefix}.Enable']
            if enable not in (0, 1):
                problems.append(f'{prefix}.Enable {enable}, expected 0 or 1')
            elif enable == 1:
                rate = fields[f'{prefix}.SamplingRate']
                if not (math.isfinite(rate) and 0.0 < rate < SAMPLING_RATE_MAX_C):
                    problems.append(
                        f'{prefix}.SamplingRate {rate} on an enabled tile, '
                        f'expected strictly between 0 and {SAMPLING_RATE_MAX_C:g} GS/s')

    if xci_path is not None and base_addr is not None:
        try:
            expected = encode(xci_path, base_addr)
        except RuntimeError as e:
            problems.append(f'cannot encode {xci_path} to compare against: {e}')
        else:
            if param != expected:
                first = next(i for i in range(CONFIG_SIZE_C) if param[i] != expected[i])
                field = field_at(first)
                where = f' ({field})' if field else ''
                problems.append(f'param-list differs from a fresh encode of {xci_path} from byte {first}{where}')
    return problems


def _int_auto(text):
    return int(text, 0)


def _run_encode(args):
    try:
        data = encode(args.xci, args.base)
        text = fragment_text(data, args.xci)
        with open(args.output, 'w') as f:
            f.write(text)
    except (RuntimeError, OSError) as e:
        print(f'ERROR: {e}')
        return 1
    print(f'Wrote a {len(data)} byte param-list from {args.xci} to {args.output}')
    return 0


def _run_check(args):
    board    = args.board if args.board else board_name(args.meta_user_bsp)
    problems = check_dtb(args.dtb, board, args.policy, args.expect_iptype, args.xci, args.base)
    tag      = 'FAIL' if args.policy == 'fail' else 'WARNING'

    for problem in problems:
        print(f'{tag}: {board}: {problem}')

    if not problems:
        print('RESULT PASS')
        return 0
    if args.policy == 'warn':
        print('RESULT WARN')
        return 0
    print('RESULT FAIL')
    return 1


def _dtb_param_list(dtb_path):
    """Return the param-list of the one RFDC node in dtb_path, or raise RuntimeError."""
    try:
        with open(dtb_path, 'rb') as f:
            nodes = walk_dtb(f.read())
    except OSError as e:
        raise RuntimeError(f'cannot read {dtb_path}: {e}')
    rfdc = find_rfdc_nodes(nodes)
    if len(rfdc) != 1:
        raise RuntimeError(f'{len(rfdc)} RFDC nodes in {dtb_path}, expected exactly one')
    param = nodes[rfdc[0]].get('param-list')
    if param is None:
        raise RuntimeError(f'{rfdc[0]} in {dtb_path} has no param-list')
    return param


def _run_decode(args):
    try:
        if args.dtb is not None:
            data = _dtb_param_list(args.dtb)
        else:
            data = read_hex_param_list(args.param_list)
        fields = decode(data)
    except (RuntimeError, OSError) as e:
        print(f'ERROR: {e}')
        return 1
    for name, value in fields:
        print(f'{name} = {value:#x}' if name == 'BaseAddr' else f'{name} = {value}')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            'Encode the RF data converter param-list from the IP .xci with the '
            'device tree generator\'s rules, or check the param-list in a built DTB.'))

    subparsers = parser.add_subparsers(dest='command', metavar='command')
    subparsers.required = True

    enc = subparsers.add_parser(
        'encode',
        help = 'Write the param-list DTS fragment encoded from an .xci')

    enc.add_argument(
        '--xci',
        type     = str,
        required = True,
        help     = f'Path to the {COMPONENT_REFERENCE_C} .xci')

    enc.add_argument(
        '--base',
        type     = _int_auto,
        required = True,
        help     = 'RFDC base address for the BaseAddr field (for example 0x490000000)')

    enc.add_argument(
        '-o', '--output',
        type     = str,
        required = True,
        help     = 'Path of the DTS fragment to write')

    chk = subparsers.add_parser(
        'check',
        help = 'Check the RFDC param-list in a built DTB')

    chk.add_argument(
        '--dtb',
        type     = str,
        required = True,
        help     = 'Path to the DTB to check')

    who = chk.add_mutually_exclusive_group(required=True)

    who.add_argument(
        '--board',
        type = str,
        help = 'Board name to report problems under')

    who.add_argument(
        '--meta-user-bsp',
        type = str,
        help = 'meta-user recipes-bsp link to name the board from')

    chk.add_argument(
        '--policy',
        type     = str,
        choices  = ('fail', 'warn'),
        required = True,
        help     = 'fail: problems fail the check; warn: problems are reported and the check passes')

    chk.add_argument(
        '--expect-iptype',
        type    = int,
        default = None,
        help    = 'IPType the param-list must carry (librfdc XRFDC_GEN3 is 2)')

    chk.add_argument(
        '--xci',
        type    = str,
        default = None,
        help    = 'The .xci the param-list was encoded from; it must equal a fresh encode (needs --base)')

    chk.add_argument(
        '--base',
        type    = _int_auto,
        default = None,
        help    = 'RFDC base address the fresh encode uses (needs --xci)')

    dec = subparsers.add_parser(
        'decode',
        help = 'Print the header and tile fields of a param-list')

    src = dec.add_mutually_exclusive_group(required=True)

    src.add_argument(
        '--dtb',
        type = str,
        help = 'DTB whose one RFDC node carries the param-list')

    src.add_argument(
        '--param-list',
        type = str,
        help = 'Text file of whitespace-separated hex bytes; lines starting with # are comments')

    args = parser.parse_args(argv)

    if args.command == 'encode':
        return _run_encode(args)
    if args.command == 'decode':
        return _run_decode(args)
    # A lone --xci or --base would silently skip the comparison the caller
    # asked for, so refuse it instead.
    if (args.xci is None) != (args.base is None):
        chk.error('--xci and --base must be given together')
    return _run_check(args)


if __name__ == '__main__':
    sys.exit(main())
