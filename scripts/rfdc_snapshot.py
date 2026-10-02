#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RFDC tile register snapshot
#-----------------------------------------------------------------------------
# Description: Read-only out-of-band reader of the RF data converter tile
#              registers. Run on the board as: python3 - < rfdc_snapshot.py
#              (for example over ssh). It opens the UIO node read-only and
#              never writes to the RFDC or touches the tile interrupt
#              registers, so it can be used to preserve a failure state.
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
import ctypes
import glob
import json
import mmap
import os
import socket
import sys

UIO_NAME   = 'usp_rf_data_converter@490000000'
PARAM_LIST = '/sys/bus/platform/devices/490000000.usp_rf_data_converter/of_node/param-list'

# Tile control/status bases (xrfdc_hw.h:2225-2227): DAC tile t is
# DAC_BASE + TILE_STRIDE * t, ADC tile t is ADC_BASE + TILE_STRIDE * t.
DAC_BASE    = 0x4000
ADC_BASE    = 0x14000
TILE_STRIDE = 0x4000

# (field, offset, mask) per tile. PG269 v2.6 Tables 31, 35, 36 and 48 and
# xrfdc_hw.h:2225-2227. 0x04 Restart Power-On State Machine, 0x08 Restart
# State, 0x0C Current State, 0x38 Reset Count (8 bit, saturates at 255),
# 0x84 Clock Detector (Gen3 only), 0x228 Common Status.
TILE_REGS = (
    ('restart',       0x04,  0xFFFF),
    ('restartState',  0x08,  0xFFFF),
    ('currentState',  0x0C,  0xFF),
    ('resetCount',    0x38,  0xFF),
    ('clockDetector', 0x84,  0x1),
    ('commonStatus',  0x228, 0xF),
)

WATCHED_PORTS = (9000, 9002)


def readText(path):
    with open(path) as f:
        return f.read().strip()


def findUio(name):
    for p in sorted(glob.glob('/sys/class/uio/uio*/name')):
        if readText(p) == name:
            return os.path.basename(os.path.dirname(p))
    sys.stderr.write('rfdc_snapshot: no UIO device named %s\n' % name)
    sys.exit(2)


def hexAddr(h):
    # /proc/net/tcp* store each 32-bit word in host (little endian) order
    raw = bytes.fromhex(h)
    if len(raw) == 4:
        return socket.inet_ntop(socket.AF_INET, raw[::-1])
    words = [raw[i:i + 4][::-1] for i in range(0, 16, 4)]
    return socket.inet_ntop(socket.AF_INET6, b''.join(words))


def connections():
    ret = []
    for path in ('/proc/net/tcp', '/proc/net/tcp6'):
        try:
            with open(path) as f:
                lines = f.read().splitlines()[1:]
        except OSError:
            continue
        for line in lines:
            fields = line.split()
            if len(fields) < 4 or fields[3] != '01':
                continue
            localPort = int(fields[1].split(':')[1], 16)
            if localPort not in WATCHED_PORTS:
                continue
            remIp, remPort = fields[2].split(':')
            ret.append('%d %s:%d' % (localPort, hexAddr(remIp), int(remPort, 16)))
    return sorted(ret)


def paramListLen():
    # Devicetree property length in bytes, same unit as sizeof(XRFdc_Config)
    try:
        return os.stat(PARAM_LIST).st_size
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser(description='Read-only RFDC tile register snapshot')
    parser.add_argument('--gen1', action='store_true', help='Gen1 device: report clockDetector as null')
    parser.add_argument('--uio-name', default=UIO_NAME, help='UIO device name (default %s)' % UIO_NAME)
    args = parser.parse_args()

    uio = findUio(args.uio_name)
    size = int(readText('/sys/class/uio/%s/maps/map0/size' % uio), 16)
    fd = os.open('/dev/' + uio, os.O_RDONLY)
    mm = mmap.mmap(fd, size, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ, offset=0)

    def rd(off):
        # one aligned 32-bit access
        return ctypes.c_uint32.from_buffer_copy(mm[off:off + 4]).value

    tiles = {}
    for kind, base in (('DAC', DAC_BASE), ('ADC', ADC_BASE)):
        for t in range(4):
            b = base + TILE_STRIDE * t
            tile = {}
            for field, off, mask in TILE_REGS:
                tile[field] = rd(b + off) & mask
            if args.gen1:
                tile['clockDetector'] = None
            status = tile['commonStatus']
            tile['clockPresent'] = status & 0x1
            tile['supplyUp']     = (status >> 1) & 0x1
            tile['powerUp']      = (status >> 2) & 0x1
            tile['pllLocked']    = (status >> 3) & 0x1
            tiles['%s%d' % (kind, t)] = tile

    try:
        fpgaState = readText('/sys/class/fpga_manager/fpga0/state')
    except OSError:
        fpgaState = None

    out = {
        'schema':        'rfdc-snapshot/1',
        'bootId':        readText('/proc/sys/kernel/random/boot_id'),
        'uptimeS':       float(readText('/proc/uptime').split()[0]),
        'kernel':        os.uname().release,
        'fpgaState':     fpgaState,
        'uio':           uio,
        'ipVersion':     rd(0x0),
        'gen3':          not args.gen1,
        'paramListLen':  paramListLen(),
        'connections':   connections(),
        'tiles':         tiles,
    }
    print(json.dumps(out, sort_keys=True))


if __name__ == '__main__':
    main()
