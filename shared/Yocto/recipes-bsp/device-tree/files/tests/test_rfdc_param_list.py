#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RF data converter param-list tool tests
#-----------------------------------------------------------------------------
# Description: Board-free unittest suite for rfdc_param_list.py
#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

"""Board-free proof of rfdc_param_list.py: encoder layout, decode and every check verdict.

Every expected offset and value below is a literal number, never recomputed
with the code under test, so a layout error in the tool cannot be mirrored
by the same error in its test. The ZCU111 fixture is a vendor-encoded
XRFdc_Config from another board, which checks the packed field order
independently of this tool's own encoder.

The check is run through its command line with sys.executable, so exit
codes and output lines are asserted exactly as the build log sees them.
DTBs are compiled with dtc from sources written into a temporary
directory; a host without dtc fails these tests rather than skipping them.

Run from anywhere:

    python3 -B -m unittest discover -s shared/Yocto/recipes-bsp/device-tree/files/tests -v
"""

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TESTS_DIR      = os.path.dirname(os.path.abspath(__file__))
TOOL_DIR       = os.path.normpath(os.path.join(TESTS_DIR, '..'))
TOOL_PATH      = os.path.join(TOOL_DIR, 'rfdc_param_list.py')
ZCU111_FIXTURE = os.path.join(TESTS_DIR, 'zcu111-param-list.hex')

if TOOL_DIR not in sys.path:
    sys.path.insert(0, TOOL_DIR)

import rfdc_param_list  # noqa: E402

RFDC_REFERENCE_C  = 'xilinx.com:ip:usp_rf_data_converter:2.6'
RFDC_COMPATIBLE_C = 'xlnx,usp-rf-data-converter-2.6'
BASE_ADDR_C       = 0x490000000

# A minimal .xci: enough to place known values at known offsets and to give
# the one enabled tile (DAC0) a plausible sampling rate, so the encoded
# configuration passes the check.
MINIMAL_PARAMS_C = {
    'C_IP_Type'            : '2',
    'C_DAC0_Fs_Max'        : '10.000',
    'C_ADC0_Fs_Max'        : '5.000',
    'C_DAC0_Enable'        : '1',
    'C_DAC0_PLL_Enable'    : 'true',
    'C_DAC0_Sampling_Rate' : '5.0',
    'C_High_Speed_ADC'     : '1',
}


def write_xci(path, params, reference=RFDC_REFERENCE_C):
    """Write an .xci-shaped JSON file carrying params as model parameters."""
    model = {name: [{'value': value}] for name, value in params.items()}
    xci = {
        'ip_inst': {
            'component_reference': reference,
            'parameters': {
                'component_parameters': {},
                'model_parameters': model,
            },
        },
    }
    with open(path, 'w') as f:
        json.dump(xci, f, indent=2)


def read_hex_fixture(path):
    """Read a whitespace-separated hex fixture, ignoring # comment lines."""
    tokens = []
    with open(path, 'r') as f:
        for line in f:
            if not line.lstrip().startswith('#'):
                tokens += line.split()
    return bytes(int(token, 16) for token in tokens)


def patched(data, offset, fmt, value):
    """Return data with one little-endian field overwritten at a literal offset."""
    out = bytearray(data)
    struct.pack_into(fmt, out, offset, value)
    return bytes(out)


class RfdcParamListTest(unittest.TestCase):

    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = tmp.name

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def xci(self, name='minimal.xci', params=None, reference=RFDC_REFERENCE_C):
        path = os.path.join(self.dir, name)
        write_xci(path, MINIMAL_PARAMS_C if params is None else params, reference)
        return path

    def good_param_list(self):
        return rfdc_param_list.encode(self.xci(), BASE_ADDR_C)

    def dtb(self, name, nodes):
        """Compile a DTB whose root holds nodes, a list of (name, compatible, param-list or None)."""
        self.assertIsNotNone(
            shutil.which('dtc'),
            msg='dtc must be on PATH, because the check is proven on DTBs compiled by it')

        lines = ['/dts-v1/;', '', '/ {']
        for node, compatible, param in nodes:
            lines.append(f'\t{node} {{')
            lines.append(f'\t\tcompatible = "{compatible}";')
            if param is not None:
                lines.append('\t\tparam-list = [' + ' '.join(f'{b:02x}' for b in param) + '];')
            lines.append('\t};')
        lines.append('};')

        dts = os.path.join(self.dir, name + '.dts')
        out = os.path.join(self.dir, name + '.dtb')
        with open(dts, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        res = subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', out, dts],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(res.returncode, 0,
                         msg=f'dtc compiles the test source {name}.dts (stderr: {res.stderr!r})')
        return out

    def rfdc_dtb(self, name, param):
        return self.dtb(name, [('usp_rf_data_converter', RFDC_COMPATIBLE_C, param)])

    def run_tool(self, *args):
        return subprocess.run([sys.executable, '-B', TOOL_PATH] + list(args),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    def assert_no_traceback(self, res):
        self.assertNotIn('Traceback', res.stderr,
                         msg='the tool reports a bad input as a problem line, never as a python traceback')

    def assert_fails_and_warns(self, dtb, needle, fail_extra=(), warn_extra=None):
        """Assert the fail policy fails naming needle and the warn policy warns naming it with exit 0."""
        if warn_extra is None:
            warn_extra = fail_extra

        res   = self.run_tool('check', '--dtb', dtb, '--board', 'SlacRfmcCarrier', '--policy', 'fail', *fail_extra)
        lines = res.stdout.splitlines()
        self.assert_no_traceback(res)
        self.assertEqual(res.returncode, 1,
                         msg=f'the fail policy exits 1 on a param-list with this problem: {needle} (stdout: {res.stdout!r})')
        hits = [line for line in lines if line.startswith('FAIL: SlacRfmcCarrier: ') and needle in line]
        self.assertTrue(hits,
                        msg=f'the fail policy prints a FAIL line naming the board and {needle!r} (stdout: {res.stdout!r})')
        self.assertEqual(lines[-1], 'RESULT FAIL',
                         msg='the fail policy ends its output with RESULT FAIL')

        res   = self.run_tool('check', '--dtb', dtb, '--board', 'XilinxZcu111', '--policy', 'warn', *warn_extra)
        lines = res.stdout.splitlines()
        self.assert_no_traceback(res)
        self.assertEqual(res.returncode, 0,
                         msg=f'the warn policy exits 0 on a param-list with this problem: {needle} (stdout: {res.stdout!r})')
        hits = [line for line in lines if line.startswith('WARNING: XilinxZcu111: ') and needle in line]
        self.assertTrue(hits,
                        msg=f'the warn policy prints a WARNING line naming the board and {needle!r} (stdout: {res.stdout!r})')
        self.assertEqual(lines[-1], 'RESULT WARN',
                         msg='the warn policy ends its output with RESULT WARN')

    # ------------------------------------------------------------------
    # Encoder
    # ------------------------------------------------------------------

    def test_param_names_count(self):
        names = rfdc_param_list.PARAM_NAMES_C
        self.assertEqual(len(names), 405,
                         msg='the encoder walks exactly the 405 parameter names that rfdc.tcl lists')
        self.assertEqual(names[0], 'DEVICE_ID',
                         msg='the first encoded field is the device id')
        self.assertEqual(names[1], 'C_BASEADDR',
                         msg='the second encoded field is the base address')

    def test_encode_layout(self):
        data = self.good_param_list()
        self.assertEqual(len(data), 1880,
                         msg='the encoded param-list is exactly sizeof(XRFdc_Config), 1880 bytes')
        self.assertEqual(data[32:36], bytes([0x02, 0x00, 0x00, 0x00]),
                         msg='the ip type 2 sits little-endian at packed offset 32')
        self.assertEqual(struct.unpack_from('<d', data, 88)[0], 10.0,
                         msg='the dac tile 0 maximum sample rate is the double at offset 88')
        self.assertEqual(struct.unpack_from('<d', data, 1112)[0], 5.0,
                         msg='the adc tile 0 maximum sample rate is the double at offset 1112')
        self.assertEqual(struct.unpack_from('<I', data, 44)[0], 1,
                         msg='a pll enable of true encodes as 1 at the dac tile 0 pll enable offset 44')
        self.assertEqual(struct.unpack_from('<I', data, 12)[0], 1,
                         msg='the high speed adc parameter fills the adc type at offset 12')
        self.assertEqual(data[4:12], bytes([0x00, 0x00, 0x00, 0x90, 0x04, 0x00, 0x00, 0x00]),
                         msg='the base address fills offset 4 as two little-endian 32-bit words, low word first')
        self.assertEqual(struct.unpack_from('<d', data, 1684)[0], 0.0,
                         msg='a parameter the xci omits, adc tile 3 sampling rate at offset 1684, encodes as 0')

    def test_encode_rejects_wrong_component(self):
        path = self.xci('ps.xci', reference='xilinx.com:ip:zynq_ultra_ps_e:3.5')
        with self.assertRaises(RuntimeError) as cm:
            rfdc_param_list.encode(path, BASE_ADDR_C)
        self.assertIn(path, str(cm.exception),
                      msg='an xci of another ip is refused with an error naming the file')

    def test_encode_rejects_non_numeric(self):
        params = dict(MINIMAL_PARAMS_C, C_DAC0_Slices='two')
        path   = self.xci('bad.xci', params=params)
        with self.assertRaises(RuntimeError) as cm:
            rfdc_param_list.encode(path, BASE_ADDR_C)
        self.assertIn('C_DAC0_Slices', str(cm.exception),
                      msg='a non-numeric integer parameter is refused with an error naming the parameter')

    def test_fragment_text_shape(self):
        path = self.xci()
        text = rfdc_param_list.fragment_text(rfdc_param_list.encode(path, BASE_ADDR_C), path)
        with open(path, 'rb') as f:
            digest = hashlib.sha256(f.read()).hexdigest()

        lines = text.splitlines()
        self.assertEqual(text.count('param-list = ['), 1,
                         msg='the fragment opens the param-list property exactly once')
        self.assertTrue(lines[0].startswith('/*') and digest in lines[0],
                        msg='the fragment starts with a comment carrying the sha256 of the xci it was encoded from')
        self.assertEqual(lines[1], 'param-list = [',
                         msg='the property opens on the line after the comment')
        self.assertEqual(lines[-1], '];',
                         msg='the fragment closes the property on its last line')

        body  = lines[2:-1]
        pairs = [token for line in body for token in line.split()]
        self.assertEqual(len(pairs), 1880,
                         msg='the fragment carries 1880 byte values')
        self.assertTrue(all(re.fullmatch('[0-9a-f]{2}', token) for token in pairs),
                        msg='every byte value is a two-digit lowercase hex pair')
        self.assertLessEqual(max(len(line.split()) for line in body), 16,
                             msg='no line of the fragment carries more than 16 byte values')

    # ------------------------------------------------------------------
    # Decode
    # ------------------------------------------------------------------

    def test_decode_zcu111_fixture(self):
        data = read_hex_fixture(ZCU111_FIXTURE)
        self.assertEqual(len(data), 1880,
                         msg='the zcu111 fixture holds one whole 1880-byte XRFdc_Config')

        fields   = dict(rfdc_param_list.decode(data))
        expected = {
            'DeviceId'           : 0,
            'BaseAddr'           : 0xb0080000,
            'ADCType'            : 1,
            'MasterADCTile'      : 0,
            'MasterDACTile'      : 0,
            'ADCSysRefSource'    : 1,
            'DACSysRefSource'    : 1,
            'IPType'             : 0,
            'SiRevision'         : 1,
            'DAC0.Enable'        : 1,
            'DAC0.PLLEnable'     : 0,
            'DAC0.SamplingRate'  : 6.4,
            'DAC0.RefClkFreq'    : 6400.0,
            'DAC0.FabClkFreq'    : 400.0,
            'DAC0.FeedbackDiv'   : 10,
            'DAC0.OutputDiv'     : 2,
            'DAC0.RefClkDiv'     : 1,
            'DAC0.MaxSampleRate' : 6.554,
            'DAC0.NumSlices'     : 4,
            'ADC0.Enable'        : 1,
            'ADC0.SamplingRate'  : 2.0,
            'ADC0.RefClkFreq'    : 2000.0,
            'ADC0.FabClkFreq'    : 250.0,
            'ADC0.FeedbackDiv'   : 10,
            'ADC0.OutputDiv'     : 6,
            'ADC0.MaxSampleRate' : 4.096,
            'ADC0.NumSlices'     : 2,
        }
        for name, value in expected.items():
            self.assertEqual(fields.get(name), value,
                             msg=f'the zcu111 fixture decodes {name} to its known gen1 value {value}')

    # ------------------------------------------------------------------
    # Check verdicts
    # ------------------------------------------------------------------

    def test_guard_good_passes(self):
        dtb = self.rfdc_dtb('good', self.good_param_list())
        for extra in ([], ['--xci', self.xci(), '--base', '0x490000000']):
            res   = self.run_tool('check', '--dtb', dtb, '--board', 'SlacRfmcCarrier', '--policy', 'fail',
                                  '--expect-iptype', '2', *extra)
            lines = res.stdout.splitlines()
            self.assert_no_traceback(res)
            self.assertEqual(res.returncode, 0,
                             msg=f'a full plausible param-list passes the fail policy (stdout: {res.stdout!r})')
            self.assertEqual(lines[-1], 'RESULT PASS',
                             msg='a passing check ends its output with RESULT PASS')
            self.assertFalse([line for line in lines if line.startswith('FAIL:')],
                             msg='a passing check prints no FAIL line')

    def test_guard_empty_fails_on_carrier(self):
        dtb   = self.rfdc_dtb('empty', b'')
        res   = self.run_tool('check', '--dtb', dtb, '--board', 'SlacRfmcCarrier', '--policy', 'fail',
                              '--expect-iptype', '2')
        lines = res.stdout.splitlines()
        self.assertEqual(res.returncode, 1,
                         msg='an empty param-list fails the carrier build')
        self.assertIn('FAIL: SlacRfmcCarrier: param-list length 0, expected 1880', lines,
                      msg='the failure names the carrier and the empty length')
        self.assertEqual(lines[-1], 'RESULT FAIL',
                         msg='the failing check ends its output with RESULT FAIL')

    def test_guard_empty_warns_elsewhere(self):
        dtb   = self.rfdc_dtb('empty', b'')
        res   = self.run_tool('check', '--dtb', dtb, '--board', 'XilinxZcu111', '--policy', 'warn')
        lines = res.stdout.splitlines()
        self.assertEqual(res.returncode, 0,
                         msg='an empty param-list on a board that has not opted in does not fail its build')
        self.assertIn('WARNING: XilinxZcu111: param-list length 0, expected 1880', lines,
                      msg='the warning names the board and the empty length')
        self.assertEqual(lines[-1], 'RESULT WARN',
                         msg='the warning check ends its output with RESULT WARN')

    def test_guard_absent(self):
        dtb = self.rfdc_dtb('absent', None)
        self.assert_fails_and_warns(dtb, 'has no param-list')

    def test_guard_short(self):
        dtb = self.rfdc_dtb('short', self.good_param_list()[:1876])
        self.assert_fails_and_warns(dtb, 'param-list length 1876, expected 1880')

    def test_guard_two_nodes(self):
        good = self.good_param_list()
        dtb  = self.dtb('two', [('usp_rf_data_converter', RFDC_COMPATIBLE_C, good),
                                ('usp_rf_data_converter_second', RFDC_COMPATIBLE_C, good)])
        self.assert_fails_and_warns(dtb, '2 RFDC nodes')

    def test_guard_iptype_255(self):
        # Under warn no expected IPType is given, as on the boards that set
        # none: the bound on known generations must catch 255 on its own.
        dtb = self.rfdc_dtb('iptype255', patched(self.good_param_list(), 32, '<I', 255))
        self.assert_fails_and_warns(dtb, 'IPType 255 is above the highest known',
                                    fail_extra=('--expect-iptype', '2'), warn_extra=())

    def test_guard_iptype_mismatch(self):
        dtb = self.rfdc_dtb('iptype1', patched(self.good_param_list(), 32, '<I', 1))
        self.assert_fails_and_warns(dtb, 'IPType 1, expected 2', fail_extra=('--expect-iptype', '2'))

    def test_guard_enable_out_of_range(self):
        dtb = self.rfdc_dtb('enable7', patched(self.good_param_list(), 40, '<I', 7))
        self.assert_fails_and_warns(dtb, 'DAC0.Enable 7')

    def test_guard_sampling_rate_implausible(self):
        for rate in (0.0, float('nan'), 12.0):
            with self.subTest(rate=rate):
                dtb = self.rfdc_dtb('rate', patched(self.good_param_list(), 48, '<d', rate))
                self.assert_fails_and_warns(dtb, 'DAC0.SamplingRate')

    def test_guard_xci_mismatch(self):
        dtb   = self.rfdc_dtb('xci', self.good_param_list())
        other = self.xci('other.xci', params=dict(MINIMAL_PARAMS_C, C_DAC0_Fs_Max='6.000'))
        self.assert_fails_and_warns(dtb, 'DAC0.MaxSampleRate', fail_extra=('--xci', other, '--base', '0x490000000'))

    def test_guard_malformed_dtb(self):
        good = self.rfdc_dtb('good', self.good_param_list())
        with open(good, 'rb') as f:
            blob = f.read()

        bad_magic = os.path.join(self.dir, 'bad-magic.dtb')
        with open(bad_magic, 'wb') as f:
            f.write(b'\x00\x00\x00\x00' + blob[4:])
        truncated = os.path.join(self.dir, 'truncated.dtb')
        with open(truncated, 'wb') as f:
            f.write(blob[:40])

        for path in (bad_magic, truncated):
            with self.subTest(dtb=os.path.basename(path)):
                self.assert_fails_and_warns(path, 'malformed DTB')

    def test_guard_device_id_nonzero(self):
        dtb = self.rfdc_dtb('devid', patched(self.good_param_list(), 0, '<I', 1))
        self.assert_fails_and_warns(dtb, 'DeviceId 1, expected 0')

    def test_guard_no_node_silent_under_warn(self):
        dtb = self.dtb('nonode', [('ethernet', 'cdns,zynqmp-gem', None)])
        res = self.run_tool('check', '--dtb', dtb, '--board', 'XilinxZcu102', '--policy', 'warn')
        self.assertEqual(res.returncode, 0,
                         msg='a machine with no RF data converter passes the warn policy')
        self.assertEqual(res.stdout, 'RESULT PASS\n',
                         msg='a machine with no RF data converter prints nothing but RESULT PASS under warn')

    def test_guard_no_node_fails_under_fail(self):
        dtb = self.dtb('nonode', [('ethernet', 'cdns,zynqmp-gem', None)])
        res = self.run_tool('check', '--dtb', dtb, '--board', 'SlacRfmcCarrier', '--policy', 'fail')
        self.assertEqual(res.returncode, 1,
                         msg='a board that opted in fails when its DTB has no RF data converter node')
        hits = [line for line in res.stdout.splitlines()
                if line.startswith('FAIL: SlacRfmcCarrier: ') and 'usp_rf_data_converter' in line]
        self.assertTrue(hits,
                        msg=f'the failure names the missing usp_rf_data_converter node (stdout: {res.stdout!r})')

    def test_guard_missing_dtb(self):
        path = os.path.join(self.dir, 'no-such.dtb')

        res = self.run_tool('check', '--dtb', path, '--board', 'XilinxZcu111', '--policy', 'warn')
        self.assertEqual(res.returncode, 0,
                         msg='a machine that deploys no such DTB passes the warn policy')
        self.assertEqual(res.stdout, 'RESULT PASS\n',
                         msg='a machine that deploys no such DTB prints nothing but RESULT PASS under warn')

        res = self.run_tool('check', '--dtb', path, '--board', 'SlacRfmcCarrier', '--policy', 'fail')
        self.assertEqual(res.returncode, 1,
                         msg='a board that opted in fails when its DTB is missing')
        hits = [line for line in res.stdout.splitlines()
                if line.startswith('FAIL: SlacRfmcCarrier: ') and 'no DTB at' in line]
        self.assertTrue(hits,
                        msg=f'the failure says there is no DTB at the path (stdout: {res.stdout!r})')

    def test_board_name_from_symlink(self):
        target = os.path.join(self.dir, 'hardware', 'XilinxZcu111', 'Yocto', 'recipes-bsp')
        os.makedirs(target)
        link = os.path.join(self.dir, 'meta-user-recipes-bsp')
        os.symlink(target, link)

        dtb = self.rfdc_dtb('empty', b'')
        res = self.run_tool('check', '--dtb', dtb, '--meta-user-bsp', link, '--policy', 'warn')
        hits = [line for line in res.stdout.splitlines() if line.startswith('WARNING: XilinxZcu111: ')]
        self.assertTrue(hits,
                        msg=f'the warning names the board the recipes-bsp link points into (stdout: {res.stdout!r})')

    # ------------------------------------------------------------------
    # Command line
    # ------------------------------------------------------------------

    def test_decode_cli(self):
        res   = self.run_tool('decode', '--param-list', ZCU111_FIXTURE)
        lines = res.stdout.splitlines()
        self.assertEqual(res.returncode, 0,
                         msg='decode of the zcu111 fixture exits 0')
        self.assertEqual(len(lines), 101,
                         msg='decode prints the 9 header fields, 12 per dac tile and 11 per adc tile')
        for line in ('BaseAddr = 0xb0080000', 'IPType = 0', 'DAC0.SamplingRate = 6.4', 'ADC0.MaxSampleRate = 4.096'):
            self.assertIn(line, lines,
                          msg=f'decode of the zcu111 fixture prints the line {line!r}')

        res = self.run_tool('decode', '--dtb', self.rfdc_dtb('good', self.good_param_list()))
        self.assertIn('IPType = 2', res.stdout.splitlines(),
                      msg='decode reads the param-list out of a DTB')

        res = self.run_tool('decode', '--dtb', self.rfdc_dtb('empty', b''))
        self.assert_no_traceback(res)
        self.assertEqual(res.returncode, 1,
                         msg='decode of an empty param-list exits 1')
        self.assertTrue(res.stdout.startswith('ERROR: '),
                        msg=f'decode of an empty param-list prints an ERROR line (stdout: {res.stdout!r})')

    def test_check_requires_xci_and_base_together(self):
        dtb = self.rfdc_dtb('good', self.good_param_list())
        res = self.run_tool('check', '--dtb', dtb, '--board', 'SlacRfmcCarrier', '--policy', 'fail',
                            '--xci', self.xci())
        self.assertNotEqual(res.returncode, 0,
                            msg='check refuses --xci without --base rather than skipping the comparison')
        self.assertIn('--base', res.stderr,
                      msg='the refusal names the missing --base option')


if __name__ == '__main__':
    unittest.main()
