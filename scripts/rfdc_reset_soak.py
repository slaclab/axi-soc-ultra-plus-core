#!/usr/bin/env python3
#-----------------------------------------------------------------------------
# Title      : RFDC reset soak
#-----------------------------------------------------------------------------
# Description: Drives the headless rfmc-carrier-init application devGui
#              through a pyrogue VirtualClient on the host, running repeated
#              ResetAllAdc/ResetAllDac/per-tile Reset/Rfdc.Init() cycles plus
#              real Linux reboots of the board, and records one JSON object
#              per step and a summary line in a JSONL file (SOAK-01).
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
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace

DEFAULT_BOARD = '10.0.0.151'
DEFAULT_SOFTWARE_DIR = os.path.expanduser('~/project/smurf/rfmc-carrier-init/software')
DEVGUI_ARGS = ['python', 'scripts/devGui.py', '--commType', 'eth-tcp-bridge', '--guiType', 'None']
PORT_LINE_RE = re.compile(r'Started zmqServer on ports (\d+)-')
READY_LINE = 'Running without GUI...'
CONFIG_STATUS_OK = 1

# D-13: one key=value DIAG-01 line, searched for (not anchored) since it is
# embedded inside a longer, possibly VirtualClient-wrapped, exception string.
DIAG_RE = re.compile(
    r'(?P<op>Reset\(first pass\)|Reset|StartUp|CustomStartUp)\s+'
    r'(?P<type>ADC|DAC)\s+tile\s+(?P<tile>\d+):\s+(?P<call>\S+)\s+failed;\s+'
    r'CurrentState=(?P<CurrentState>\d+)\s+'
    r'ClockPresent=(?P<ClockPresent>[01])\s+'
    r'SupplyUp=(?P<SupplyUp>[01])\s+'
    r'PowerUp=(?P<PowerUp>[01])\s+'
    r'PllLocked=(?P<PllLocked>[01])\s+'
    r'ClkDet=(?P<ClkDet>[01]|NA)\s+'
    r'ClkSrc=(?P<ClkSrc>External|InternalPLL|Unknown)'
)

# D-21: the DIAG-01 key names map one to one onto the RfdcTile/PllStatus
# PyRogue variable names (documented here, used by the audit page and this
# soak script; no PyRogue renames).
DIAG_KEY_TO_VARIABLE = {
    'ClockPresent': 'ClockPresent',
    'SupplyUp': 'SupplyStable',
    'PowerUp': 'PoweredUp',
    'PllLocked': 'PllLocked',
    'ClkDet': 'ClockDetector',
    'ClkSrc': 'ClockSource',
}


def parseDiag(text):
    """Parse every DIAG-01 line found in text (D-13), in order, into a list
    of dicts. Returns an empty list for falsy input."""
    if not text:
        return []
    out = []
    for m in DIAG_RE.finditer(text):
        d = m.groupdict()
        clkDet = d['ClkDet'] if d['ClkDet'] == 'NA' else int(d['ClkDet'])
        out.append({
            'op': d['op'],
            'type': d['type'],
            'tile': int(d['tile']),
            'call': d['call'],
            'CurrentState': int(d['CurrentState']),
            'ClockPresent': int(d['ClockPresent']),
            'SupplyUp': int(d['SupplyUp']),
            'PowerUp': int(d['PowerUp']),
            'PllLocked': int(d['PllLocked']),
            'ClkDet': clkDet,
            'ClkSrc': d['ClkSrc'],
        })
    return out


def classifyError(text):
    """D-17/F11: a rogue timeout is a distinct failureKind from a command
    failure, so a slow Reset is never misattributed to a device error."""
    if text and ('timeout' in text or 'Timeout' in text):
        return 'timeout'
    return 'command'


#-----------------------------------------------------------------------------
# Board-side helpers (ssh, boot_id, read-only snapshot, journal capture)
#-----------------------------------------------------------------------------

def ssh(board, command, timeout=30):
    """List-argument subprocess ssh: no shell, fixed options, BatchMode so a
    missing host key or a password prompt fails fast instead of hanging."""
    return subprocess.run(
        ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
         '-o', 'StrictHostKeyChecking=accept-new', 'root@' + board, command],
        capture_output=True, text=True, timeout=timeout)


def bootId(board, timeout=15):
    r = ssh(board, 'cat /proc/sys/kernel/random/boot_id', timeout=timeout)
    return r.stdout.strip() if r.returncode == 0 else None


def snapshot(board, path=None, timeout=30):
    """Run the sibling rfdc_snapshot.py on the board over ssh (python3 - on
    stdin) and return the parsed JSON. Optionally also writes it to path."""
    scriptPath = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'rfdc_snapshot.py')
    with open(scriptPath) as f:
        src = f.read()
    r = subprocess.run(
        ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
         '-o', 'StrictHostKeyChecking=accept-new', 'root@' + board, 'python3 -'],
        input=src, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError('snapshot failed: rc=%d stderr=%s' % (r.returncode, r.stderr[-400:]))
    data = json.loads(r.stdout)
    if path:
        with open(path, 'w') as f:
            json.dump(data, f, sort_keys=True)
    return data


def saveJournal(board, path, lines=300, timeout=30):
    """D-17/F6: the metal log lines embedded in an exception are usually
    truncated, so a failing step also pulls the full journal and saves it
    next to the JSONL."""
    r = ssh(board, 'journalctl -b -u startup-app-init -n %d --no-pager' % lines, timeout=timeout)
    with open(path, 'w') as f:
        f.write(r.stdout)
        if r.returncode != 0:
            f.write('\n[journalctl exit %d] %s\n' % (r.returncode, r.stderr))
    return path


def rawRead(board, port, address, nbytes=4, timeout=10):
    """A bare rogue memory read through a short-lived TcpClient/Master, used
    only for readiness checks (D-18/D-72): a not-initialized PyRFdc still
    serves this, unlike the driver-backed registers it refuses."""
    import rogue.interfaces.memory as rim
    client = rim.TcpClient(board, port)
    master = rim.Master()
    master._setSlave(client)
    time.sleep(0.2)
    try:
        buf = bytearray(nbytes)
        txId = master._reqTransaction(address, buf, nbytes, 0, rim.Read)
        master._waitTransaction(txId)
        err = master._getError()
        if err:
            master._clearError()
            raise RuntimeError(err)
        return bytes(buf)
    finally:
        del master
        del client


def waitReady(board, timeout=180):
    """D-18/D-72/F12: readiness is a successful raw register read, not an
    open port. AxiVersion.FpgaVersion over port 9000 first, then
    Rfdc.ConfigStatus over port 9002 (served even when PyRFdc is not
    initialized; never Rfdc.IpCoreRevision, which a not-initialized PyRFdc
    refuses). Returns (configStatus, elapsedS)."""
    t0 = time.monotonic()
    lastErr = None
    while time.monotonic() - t0 < timeout:
        try:
            rawRead(board, 9000, 0x400000000, 4)
            raw = rawRead(board, 9002, 0x14000, 4)
            status = int.from_bytes(raw, 'little')
            return status, time.monotonic() - t0
        except Exception as e:
            lastErr = e
            time.sleep(2)
    raise TimeoutError('board not ready after %ss: %r' % (timeout, lastErr))


#-----------------------------------------------------------------------------
# Headless devGui subprocess (R11, D-52)
#-----------------------------------------------------------------------------

class DevGui(object):
    """Owns one headless devGui subprocess: launches it, waits for its
    VirtualClient port and its ready line in the log, and stops it on exit
    through an escalating signal sequence."""

    def __init__(self, board, softwareDir):
        self.board = board
        self.softwareDir = softwareDir
        self.proc = None
        self.port = None
        self.logPath = None
        self._logFh = None

    def start(self, logPath):
        self.logPath = logPath
        self._logFh = open(logPath, 'a')
        self.proc = subprocess.Popen(
            list(DEVGUI_ARGS),
            cwd=self.softwareDir,
            stdout=self._logFh,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

    def _tailLog(self, n=40):
        try:
            with open(self.logPath) as f:
                lines = f.readlines()
            return ''.join(lines[-n:])
        except OSError:
            return ''

    def _readLog(self):
        try:
            with open(self.logPath) as f:
                return f.read()
        except OSError:
            return ''

    def waitPort(self, timeout=60):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if self.proc.poll() is not None:
                raise RuntimeError('devGui exited early (rc=%s):\n%s' % (self.proc.returncode, self._tailLog()))
            m = PORT_LINE_RE.search(self._readLog())
            if m:
                self.port = int(m.group(1))
                return self.port
            time.sleep(1)
        raise TimeoutError('devGui did not print the zmqServer port within %ss:\n%s' % (timeout, self._tailLog()))

    def waitReady(self, timeout=900):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if self.proc.poll() is not None:
                raise RuntimeError('devGui exited before start() finished (rc=%s):\n%s' % (self.proc.returncode, self._tailLog()))
            if READY_LINE in self._readLog():
                return True
            time.sleep(2)
        raise TimeoutError('devGui did not reach "%s" within %ss:\n%s' % (READY_LINE, timeout, self._tailLog()))

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            try:
                self.proc.send_signal(signal.SIGINT)
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                try:
                    self.proc.terminate()
                    self.proc.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=10)
        if self._logFh is not None:
            try:
                self._logFh.close()
            except OSError:
                pass
        self.proc = None


class AttachedGui(object):
    """--attach host:port mode: no devGui subprocess is launched or owned;
    stop() is a no-op so an operator-launched devGui is left running."""

    def __init__(self, port):
        self.port = port

    def start(self, logPath=None):
        pass

    def waitPort(self, timeout=60):
        return self.port

    def waitReady(self, timeout=900):
        return True

    def stop(self):
        pass


def attach(port, host='localhost'):
    import pyrogue.interfaces
    return pyrogue.interfaces.VirtualClient(addr=host, port=port)


#-----------------------------------------------------------------------------
# Tile tree helpers
#-----------------------------------------------------------------------------

def enabledTiles(root):
    rfdc = root.Rfdc
    adc = [i for i in range(4) if rfdc.CheckAdcTileEnabled[i].get() != 0]
    dac = [i for i in range(4) if rfdc.CheckDacTileEnabled[i].get() != 0]
    return adc, dac


def _readOneTile(tile):
    pll = tile.PllStatus
    clockDetector = pll.ClockDetector.get() if hasattr(pll, 'ClockDetector') else None
    if clockDetector is not None:
        clockDetector = int(clockDetector)
    return {
        'currentState': int(tile.CurrentState.get()),
        'pllLocked': int(pll.PllLocked.get()),
        'clockPresent': int(pll.ClockPresent.get()),
        'supplyUp': int(pll.SupplyStable.get()),
        'powerUp': int(pll.PoweredUp.get()),
        'clockDetector': clockDetector,
        'clockSource': pll.ClockSource.getDisp(),
        'resetCount': int(tile.ResetCount.get()),
        'failureCount': int(tile.FailureCount.get()),
    }


def readTiles(root):
    """Per enabled tile: currentState, pllLocked, clockPresent, supplyUp,
    powerUp, clockDetector, clockSource, resetCount, failureCount."""
    adcTiles, dacTiles = enabledTiles(root)
    out = {}
    for i in adcTiles:
        out['ADC%d' % i] = _readOneTile(root.Rfdc.AdcTile[i])
    for i in dacTiles:
        out['DAC%d' % i] = _readOneTile(root.Rfdc.DacTile[i])
    return out


def cycleSteps(ctx):
    """D-16: ResetAllAdc, ResetAllDac, per-tile Reset on every enabled tile
    (ADC before DAC), then Init."""
    rfdc = ctx.root.Rfdc
    steps = [
        ('ResetAllAdc', rfdc.ResetAllAdc),
        ('ResetAllDac', rfdc.ResetAllDac),
    ]
    for i in ctx.adcTiles:
        steps.append(('Reset AdcTile[%d]' % i, rfdc.AdcTile[i].Reset))
    for i in ctx.dacTiles:
        steps.append(('Reset DacTile[%d]' % i, rfdc.DacTile[i].Reset))
    steps.append(('Init', rfdc.Init))
    return steps


def stepNames(ctx):
    names = ['ResetAllAdc', 'ResetAllDac']
    names += ['Reset AdcTile[%d]' % i for i in ctx.adcTiles]
    names += ['Reset DacTile[%d]' % i for i in ctx.dacTiles]
    names.append('Init')
    return names


#-----------------------------------------------------------------------------
# Soak context and the per-step/per-cycle driver
#-----------------------------------------------------------------------------

class _StopSoak(Exception):
    """Internal control-flow signal: stop the cycle loop without --continue."""
    pass


class Ctx(object):
    """Carries the JSONL writer, run state, and every pluggable hook
    (devGui factory, attach, snapshot, journal, boot_id, reboot, readiness)
    so --selftest can inject stubs with no globals (D-17 action text)."""

    def __init__(self, args, outDir, fh):
        self.args = args
        self.outDir = outDir
        self.fh = fh

        self.root = None
        self.adcTiles = []
        self.dacTiles = []
        self.hostTreeAvailable = True
        self.bootCounter = 0
        self.attachHost = 'localhost'
        self.lastBootId = None

        self.prevResetCounts = {}
        self.resetCountIncreases = 0
        self.failureIndex = 0
        self.firstFailure = None
        self.stepsRun = 0
        self.stepsFailed = 0
        self.stepsSkipped = 0

        # Pluggable hooks; None means "use the real implementation",
        # filled in by runSoak() the first time it is called.
        self.devGuiFactory = None
        self.attachFn = None
        self.snapshotFn = None
        self.journalFn = None
        self.bootIdFn = None
        self.sshRebootFn = None
        self.waitReadyFn = None

    def write(self, obj):
        self.fh.write(json.dumps(obj, sort_keys=True) + '\n')
        self.fh.flush()


def makeGui(ctx):
    args = ctx.args
    if args.attach:
        host, portStr = args.attach.split(':')
        ctx.attachHost = host
        return AttachedGui(int(portStr))
    return DevGui(args.board, args.software_dir)


def _saveFailureEvidence(ctx, cycle, step, failureKind, diagFirst):
    ctx.failureIndex += 1
    n = ctx.failureIndex
    journalPath = os.path.join(ctx.outDir, 'fail-%d-journal.txt' % n)
    snapshotPath = os.path.join(ctx.outDir, 'fail-%d-snapshot.json' % n)
    metalLog = None
    try:
        ctx.journalFn(ctx.args.board, journalPath)
        with open(journalPath) as f:
            metalLog = ''.join(f.readlines()[-40:])
    except Exception as e:
        metalLog = 'journal capture failed: %s' % e
    try:
        ctx.snapshotFn(ctx.args.board, snapshotPath)
    except Exception:
        pass
    if ctx.firstFailure is None:
        ctx.firstFailure = {
            'cycle': cycle, 'step': step, 'failureKind': failureKind, 'diag': diagFirst,
        }
    return metalLog


def runStep(ctx, cycle, name, fn):
    """Run one reset step, verify every enabled tile reached state 15 with
    PLL locked, record the resulting JSONL line, and on failure save the
    journal and a snapshot next to it. Returns True on success."""
    t0 = time.monotonic()
    err = None
    ok = True
    try:
        fn()
    except Exception as e:
        ok = False
        err = str(e)
    durationS = time.monotonic() - t0

    tiles = {}
    try:
        tiles = readTiles(ctx.root)
    except Exception as e:
        if ok:
            ok = False
            err = str(e)

    stateFail = any((t['currentState'] != 15 or not t['pllLocked']) for t in tiles.values())
    if ok and stateFail:
        ok = False

    failureKind = None if ok else (classifyError(err) if err else 'state')

    for key, t in tiles.items():
        prev = ctx.prevResetCounts.get(key)
        delta = 0 if prev is None else (t['resetCount'] - prev)
        t['resetCountDelta'] = delta
        ctx.prevResetCounts[key] = t['resetCount']
        if delta > 0:
            ctx.resetCountIncreases += 1

    # D-16/D-17: the restart field (tile register 0x04) and the step's
    # bootId both come from one read-only snapshot taken after the step.
    bId = None
    try:
        snap = ctx.snapshotFn(ctx.args.board)
        bId = snap.get('bootId')
        snapTiles = snap.get('tiles', {})
        for key, t in tiles.items():
            kind = key[:3]
            idx = int(key[3:])
            snapTile = snapTiles.get('%s%d' % (kind, idx))
            if snapTile:
                t['restart'] = snapTile.get('restart')
    except Exception:
        pass

    configStatus = None
    try:
        configStatus = int(ctx.root.Rfdc.ConfigStatus.get())
    except Exception:
        pass

    diag = parseDiag(err) if err else []
    record = {
        'type': 'step',
        'cycle': cycle,
        'step': name,
        'durationS': durationS,
        'ok': ok,
        'failureKind': failureKind,
        'error': err,
        'diag': diag,
        'metalLog': None,
        'configStatus': configStatus,
        'bootId': bId,
        'tiles': tiles,
    }

    if not ok:
        record['metalLog'] = _saveFailureEvidence(ctx, cycle, name, failureKind, diag[0] if diag else None)

    ctx.write(record)
    return ok


def attachAndCheck(ctx, gui, cycle):
    """BootStart step: launch (or re-use, in --attach mode) the devGui, wait
    for it, attach a VirtualClient, cache the enabled tiles, and refuse to
    continue if IgnoreMetalError is set (D-17, checked after every attach).
    Returns True (usable), False (BootStart failed), or 'refused'."""
    ctx.bootCounter += 1
    logPath = os.path.join(ctx.outDir, 'devgui-%d.log' % ctx.bootCounter)

    t0 = time.monotonic()
    err = None
    ok = True
    try:
        gui.start(logPath)
        gui.waitPort(timeout=ctx.args.ready_timeout)
        gui.waitReady(timeout=ctx.args.start_timeout)
        ctx.root = ctx.attachFn(gui.port)
        ctx.adcTiles, ctx.dacTiles = enabledTiles(ctx.root)
    except Exception as e:
        ok = False
        err = str(e)
    durationS = time.monotonic() - t0

    bId = None
    if ok:
        try:
            snap = ctx.snapshotFn(ctx.args.board)
            bId = snap.get('bootId')
            ctx.lastBootId = bId
        except Exception:
            pass

    failureKind = None if ok else classifyError(err)
    diag = parseDiag(err) if err else []
    record = {
        'type': 'step',
        'cycle': cycle,
        'step': 'BootStart',
        'durationS': durationS,
        'ok': ok,
        'failureKind': failureKind,
        'error': err,
        'diag': diag,
        'metalLog': None,
        'configStatus': None,
        'bootId': bId,
        'tiles': {},
    }
    if not ok:
        record['metalLog'] = _saveFailureEvidence(ctx, cycle, 'BootStart', failureKind, diag[0] if diag else None)
    ctx.write(record)

    if not ok:
        return False

    # D-17: refuse to run (and attempt no reset step) while the bare-metal
    # driver's error swallow is active, checked after every attach.
    if bool(ctx.root.Rfdc.IgnoreMetalError.get()):
        ctx.write({'type': 'refused', 'cycle': cycle, 'reason': 'IgnoreMetalError is set'})
        return 'refused'

    return True


def doReboot(ctx, gui, cycle):
    """Reboot step: stop the devGui, ssh reboot, wait for a new boot_id,
    then for readiness (ConfigStatus must read Ok)."""
    gui.stop()
    t0 = time.monotonic()
    err = None
    ok = True
    newBootId = None
    try:
        before = ctx.bootIdFn(ctx.args.board)
        ctx.sshRebootFn(ctx.args.board)
        deadline = ctx.args.boot_timeout
        t1 = time.monotonic()
        while True:
            if time.monotonic() - t1 >= deadline:
                raise TimeoutError('board did not come back with a new boot_id within %ss' % deadline)
            now = ctx.bootIdFn(ctx.args.board)
            if now and now != before:
                newBootId = now
                break
            time.sleep(3)
        status, _elapsed = ctx.waitReadyFn(ctx.args.board, ctx.args.ready_timeout)
        if status != CONFIG_STATUS_OK:
            raise RuntimeError('ConfigStatus not Ok after reboot: %s' % status)
        ctx.snapshotFn(ctx.args.board, os.path.join(ctx.outDir, 'reboot-%d-snapshot.json' % cycle))
    except Exception as e:
        ok = False
        err = str(e)
    durationS = time.monotonic() - t0

    failureKind = None if ok else classifyError(err)
    record = {
        'type': 'step',
        'cycle': cycle,
        'step': 'Reboot',
        'durationS': durationS,
        'ok': ok,
        'failureKind': failureKind,
        'error': err,
        'diag': [],
        'metalLog': None,
        'configStatus': None,
        'bootId': newBootId,
        'tiles': {},
    }
    if not ok:
        record['metalLog'] = _saveFailureEvidence(ctx, cycle, 'Reboot', failureKind, None)
    ctx.write(record)
    return ok


def buildSummary(ctx, cyclesRun, reboots, bootFailures, elapsedS, result):
    return {
        'type': 'summary',
        'cyclesRequested': ctx.args.cycles,
        'cyclesRun': cyclesRun,
        'stepsRun': ctx.stepsRun,
        'stepsFailed': ctx.stepsFailed,
        'stepsSkipped': ctx.stepsSkipped,
        'reboots': reboots,
        'bootFailures': bootFailures,
        'resetCountIncreases': ctx.resetCountIncreases,
        'firstFailure': ctx.firstFailure,
        'result': result,
        'elapsedS': elapsedS,
    }


def runSoak(ctx):
    """Startup, the N-cycle loop with its periodic reboots, and the final
    summary. Returns the process exit code (0 pass, 1 fail, 2 refused or
    bad config)."""
    args = ctx.args
    if ctx.devGuiFactory is None:
        ctx.devGuiFactory = lambda: makeGui(ctx)
    if ctx.attachFn is None:
        ctx.attachFn = lambda port: attach(port, host=ctx.attachHost)
    if ctx.snapshotFn is None:
        ctx.snapshotFn = lambda board, path=None: snapshot(board, path)
    if ctx.journalFn is None:
        ctx.journalFn = lambda board, path: saveJournal(board, path)
    if ctx.bootIdFn is None:
        ctx.bootIdFn = lambda board: bootId(board)
    if ctx.sshRebootFn is None:
        ctx.sshRebootFn = lambda board: ssh(board, '/bin/sync; /sbin/reboot', timeout=15)
    if ctx.waitReadyFn is None:
        ctx.waitReadyFn = lambda board, timeout: waitReady(board, timeout)

    t0 = time.monotonic()
    gui = ctx.devGuiFactory()
    result = attachAndCheck(ctx, gui, cycle=0)
    if result == 'refused':
        gui.stop()
        return 2
    if result is not True:
        gui.stop()
        ctx.write(buildSummary(ctx, 0, 0, 0, time.monotonic() - t0, 'fail'))
        return 1

    status = int(ctx.root.Rfdc.ConfigStatus.get())
    if status != CONFIG_STATUS_OK:
        message = ctx.root.Rfdc.ConfigMessage.get()
        ctx.write({'type': 'config', 'cycle': 0, 'configStatus': status, 'message': message})
        gui.stop()
        return 2

    ctx.write({
        'type': 'start',
        'args': vars(args),
        'board': args.board,
        'bootId': ctx.lastBootId,
        'configStatus': status,
        'configPayloadSha256': ctx.root.Rfdc.ConfigPayloadSha256.get(),
        'ipCoreVersion': int(ctx.root.Rfdc.IpCoreVersion.get()),
    })

    exitCode = 0
    reboots = 0
    bootFailures = 0
    cyclesRun = 0
    try:
        for cycle in range(1, args.cycles + 1):
            cyclesRun = cycle
            if ctx.hostTreeAvailable:
                for name, fn in cycleSteps(ctx):
                    ok = runStep(ctx, cycle, name, fn)
                    ctx.stepsRun += 1
                    if not ok:
                        ctx.stepsFailed += 1
                        exitCode = 1
                        if not args.continue_:
                            raise _StopSoak()
            else:
                # D-17/F6: no host tree since the last BootStart failure;
                # this cycle's reset steps are skipped until the next reboot.
                for name in stepNames(ctx):
                    ctx.write({
                        'type': 'step', 'cycle': cycle, 'step': name,
                        'durationS': 0.0, 'ok': None, 'failureKind': None,
                        'error': None, 'skipped': 'no host tree', 'diag': [],
                        'metalLog': None, 'configStatus': None,
                        'bootId': None, 'tiles': {},
                    })
                    ctx.stepsSkipped += 1

            if args.reboot_every and (cycle % args.reboot_every == 0):
                reboots += 1
                ok = doReboot(ctx, gui, cycle)
                if not ok:
                    exitCode = 1
                    if not args.continue_:
                        raise _StopSoak()

                gui = ctx.devGuiFactory()
                result = attachAndCheck(ctx, gui, cycle)
                if result == 'refused':
                    gui.stop()
                    return 2
                if result is not True:
                    bootFailures += 1
                    ctx.hostTreeAvailable = False
                    exitCode = 1
                    if not args.continue_:
                        raise _StopSoak()
                else:
                    ctx.hostTreeAvailable = True
    except _StopSoak:
        pass
    finally:
        gui.stop()

    summary = buildSummary(
        ctx, cyclesRun, reboots, bootFailures, time.monotonic() - t0,
        'pass' if exitCode == 0 else 'fail')
    ctx.write(summary)
    return exitCode


#-----------------------------------------------------------------------------
# CLI
#-----------------------------------------------------------------------------

def buildArgParser():
    ap = argparse.ArgumentParser(
        description='RFDC reset soak: drives the headless devGui through repeated reset '
                    'cycles and real reboots, recording JSONL evidence (SOAK-01)')
    ap.add_argument('--board', default=DEFAULT_BOARD)
    ap.add_argument('--software-dir', default=DEFAULT_SOFTWARE_DIR)
    ap.add_argument('--cycles', type=int, default=100)
    ap.add_argument('--reboot-every', type=int, default=5)
    ap.add_argument('--continue', dest='continue_', action='store_true')
    ap.add_argument('--out', default=None)
    ap.add_argument(
        '--attach', default=None,
        help='host:port of an already-running devGui; incompatible with '
             '--reboot-every other than 0')
    ap.add_argument('--ready-timeout', type=float, default=180)
    ap.add_argument('--boot-timeout', type=float, default=300)
    ap.add_argument('--start-timeout', type=float, default=900)
    ap.add_argument('--selftest', action='store_true')
    return ap


def main(argv=None):
    ap = buildArgParser()
    args = ap.parse_args(argv)

    if args.selftest:
        return runSelfTest()

    if args.attach and args.reboot_every not in (0, None):
        sys.stderr.write('--attach is incompatible with --reboot-every other than 0\n')
        return 2

    if args.out is None:
        args.out = './rfdc-soak-%s' % time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
    os.makedirs(args.out, exist_ok=True)

    jsonlPath = os.path.join(args.out, 'soak.jsonl')
    with open(jsonlPath, 'w') as fh:
        ctx = Ctx(args, args.out, fh)
        exitCode = runSoak(ctx)
    return exitCode


#-----------------------------------------------------------------------------
# --selftest: stub tree, stub hooks, no hardware, no pyrogue/rogue import.
#-----------------------------------------------------------------------------

class Val(object):
    """Duck-types the .get()/.getDisp() surface of a PyRogue RemoteVariable
    over a plain Python value, so the real readTiles()/enabledTiles() code
    runs unmodified against a stub tree."""
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v

    def getDisp(self):
        return str(self.v)


class StubTile(object):
    def __init__(self):
        self.CurrentState = Val(15)
        self.ResetCount = Val(0)
        self.FailureCount = Val(0)
        self.PllStatus = SimpleNamespace(
            PllLocked=Val(1), ClockPresent=Val(1), SupplyStable=Val(1),
            PoweredUp=Val(1), ClockDetector=Val(1), ClockSource=Val('InternalPLL'))
        self._resetFn = lambda: None

    def Reset(self):
        self._resetFn()


class StubRfdc(object):
    def __init__(self, nAdc=4, nDac=4):
        self.AdcTile = {i: StubTile() for i in range(nAdc)}
        self.DacTile = {i: StubTile() for i in range(nDac)}
        self.CheckAdcTileEnabled = {i: Val(1) for i in range(nAdc)}
        self.CheckDacTileEnabled = {i: Val(1) for i in range(nDac)}
        self.IgnoreMetalError = Val(False)
        self.ConfigStatus = Val(1)
        self.ConfigMessage = Val('Ok')
        self.ConfigPayloadSha256 = Val('deadbeef')
        self.IpCoreVersion = Val(0x02060B00)
        self._initFn = lambda: None

    def ResetAllAdc(self):
        pass

    def ResetAllDac(self):
        pass

    def Init(self):
        self._initFn()


class StubRoot(object):
    def __init__(self, rfdc):
        self.Rfdc = rfdc


class StubGui(object):
    def __init__(self, failBoot=False):
        self.port = 1234
        self.failBoot = failBoot
        self.stopped = False

    def start(self, logPath=None):
        pass

    def waitPort(self, timeout=60):
        return self.port

    def waitReady(self, timeout=900):
        if self.failBoot:
            raise RuntimeError('stub boot failure')
        return True

    def stop(self):
        self.stopped = True


def _newCtxForTest(**argOverrides):
    outDir = tempfile.mkdtemp(prefix='rfdc-soak-selftest-')
    kwargs = dict(board='stub', software_dir='.', cycles=1, reboot_every=0,
                  continue_=False, out=outDir, attach=None,
                  ready_timeout=5, boot_timeout=5, start_timeout=5)
    kwargs.update(argOverrides)
    args = argparse.Namespace(**kwargs)
    fh = open(os.path.join(outDir, 'soak.jsonl'), 'w')
    return Ctx(args, outDir, fh), fh, outDir


def _readRecords(outDir):
    with open(os.path.join(outDir, 'soak.jsonl')) as f:
        return [json.loads(line) for line in f]


def _test_diag_basic():
    line = ('Reset ADC tile 1: XRFdc_Reset failed; CurrentState=6 ClockPresent=0 '
            'SupplyUp=1 PowerUp=0 PllLocked=0 ClkDet=0 ClkSrc=External')
    entries = parseDiag(line)
    assert len(entries) == 1, entries
    assert entries[0] == {
        'op': 'Reset', 'type': 'ADC', 'tile': 1, 'call': 'XRFdc_Reset',
        'CurrentState': 6, 'ClockPresent': 0, 'SupplyUp': 1, 'PowerUp': 0,
        'PllLocked': 0, 'ClkDet': 0, 'ClkSrc': 'External',
    }, entries[0]


def _test_diag_wrapped():
    # A VirtualClient-wrapped rogue GeneralError: the DIAG-01 line appears
    # after "Error " inside a longer block-transaction message.
    wrapped = (
        'root.Rfdc.AdcTile[3].Reset: Block::checkTransaction: Transaction error '
        'for block AdcTile[3] with address 0x00000008. '
        'Error Reset ADC tile 3: XRFdc_Reset failed; CurrentState=6 '
        'ClockPresent=1 SupplyUp=1 PowerUp=0 PllLocked=0 ClkDet=0 ClkSrc=InternalPLL\n'
        'metal: error: ADC 3 timed out at state 6'
    )
    entries = parseDiag(wrapped)
    assert len(entries) == 1, entries
    assert entries[0]['tile'] == 3
    assert classifyError(wrapped) == 'command'
    assert classifyError('rogue.GeneralError: Timeout waiting for Ack') == 'timeout'


def _test_multitile_diag():
    text = (
        'Reset ADC tile 1: XRFdc_Reset failed; CurrentState=6 ClockPresent=0 '
        'SupplyUp=1 PowerUp=0 PllLocked=0 ClkDet=0 ClkSrc=External\n'
        'Reset ADC tile 2: XRFdc_Reset failed; CurrentState=6 ClockPresent=1 '
        'SupplyUp=1 PowerUp=1 PllLocked=0 ClkDet=NA ClkSrc=Unknown\n'
        'StartUp DAC tile 0: XRFdc_StartUp failed; CurrentState=3 ClockPresent=1 '
        'SupplyUp=1 PowerUp=1 PllLocked=1 ClkDet=1 ClkSrc=InternalPLL'
    )
    entries = parseDiag(text)
    assert len(entries) == 3, entries
    assert entries[0]['tile'] == 1 and entries[0]['type'] == 'ADC'
    assert entries[1]['tile'] == 2 and entries[1]['ClkDet'] == 'NA', entries[1]
    assert entries[2]['tile'] == 0 and entries[2]['type'] == 'DAC' and entries[2]['op'] == 'StartUp'


def _test_jsonl_roundtrip():
    tmpDir = tempfile.mkdtemp(prefix='rfdc-soak-selftest-')
    try:
        path = os.path.join(tmpDir, 'r.jsonl')
        obj1 = {'type': 'start', 'board': '10.0.0.151', 'nested': {'a': 1}}
        obj2 = {'type': 'summary', 'result': 'pass', 'elapsedS': 1.5}
        with open(path, 'w') as fh:
            fh.write(json.dumps(obj1, sort_keys=True) + '\n')
            fh.write(json.dumps(obj2, sort_keys=True) + '\n')
        with open(path) as f:
            lines = [json.loads(line) for line in f]
        assert lines == [obj1, obj2], lines
    finally:
        shutil.rmtree(tmpDir, ignore_errors=True)


def _test_reboot_schedule():
    for cycles, every, expected in ((100, 5, 20), (100, 0, 0), (1, 1, 1), (5, 7, 0)):
        count = sum(1 for c in range(1, cycles + 1) if every and c % every == 0)
        assert count == expected, (cycles, every, count, expected)


def _test_stop_and_continue():
    diagText = ('Reset ADC tile 3: XRFdc_Reset failed; CurrentState=6 '
                'ClockPresent=1 SupplyUp=1 PowerUp=0 PllLocked=0 ClkDet=0 ClkSrc=InternalPLL')

    def failingReset():
        raise RuntimeError(diagText)

    def stubSnapshot(board, path=None):
        data = {'bootId': 'boot-stub', 'tiles': {}}
        data['tiles'].update({'ADC%d' % i: {'restart': 0} for i in range(4)})
        data['tiles'].update({'DAC%d' % i: {'restart': 0} for i in range(4)})
        if path:
            with open(path, 'w') as f:
                json.dump(data, f)
        return data

    calls = {'journal': 0}

    def stubJournal(board, path):
        calls['journal'] += 1
        with open(path, 'w') as f:
            f.write('stub journal\n')

    rfdc = StubRfdc()
    rfdc.AdcTile[3]._resetFn = failingReset

    ctx, fh, outDir = _newCtxForTest(continue_=False)
    try:
        ctx.devGuiFactory = StubGui
        ctx.attachFn = lambda port: StubRoot(rfdc)
        ctx.snapshotFn = stubSnapshot
        ctx.journalFn = stubJournal
        rc = runSoak(ctx)
        fh.close()
        assert rc == 1, rc
        records = _readRecords(outDir)
        adc3 = [r for r in records if r.get('type') == 'step' and r.get('step') == 'Reset AdcTile[3]']
        assert len(adc3) == 1, adc3
        rec = adc3[0]
        assert rec['ok'] is False
        assert rec['failureKind'] == 'command', rec
        assert rec['diag'] and rec['diag'][0]['tile'] == 3, rec['diag']
        assert rec['diag'][0]['CurrentState'] == 6, rec['diag']
        assert calls['journal'] == 1, calls
        names = [r['step'] for r in records if r.get('type') == 'step']
        assert 'Init' not in names, names
    finally:
        shutil.rmtree(outDir, ignore_errors=True)

    rfdc2 = StubRfdc()
    rfdc2.AdcTile[3]._resetFn = failingReset
    ctx2, fh2, outDir2 = _newCtxForTest(continue_=True)
    try:
        ctx2.devGuiFactory = StubGui
        ctx2.attachFn = lambda port: StubRoot(rfdc2)
        ctx2.snapshotFn = stubSnapshot
        ctx2.journalFn = stubJournal
        rc2 = runSoak(ctx2)
        fh2.close()
        assert rc2 == 1, rc2
        names2 = [r['step'] for r in _readRecords(outDir2) if r.get('type') == 'step']
        assert 'Init' in names2, names2
    finally:
        shutil.rmtree(outDir2, ignore_errors=True)


def _test_timeout_classification():
    def failingInit():
        raise RuntimeError('Timeout waiting for Reset(-1) to complete')

    def stubSnapshot(board, path=None):
        data = {'bootId': 'b', 'tiles': {}}
        if path:
            with open(path, 'w') as f:
                json.dump(data, f)
        return data

    calls = {'journal': 0}

    def stubJournal(board, path):
        calls['journal'] += 1
        with open(path, 'w') as f:
            f.write('stub\n')

    rfdc = StubRfdc()
    rfdc._initFn = failingInit

    ctx, fh, outDir = _newCtxForTest(continue_=False)
    try:
        ctx.devGuiFactory = StubGui
        ctx.attachFn = lambda port: StubRoot(rfdc)
        ctx.snapshotFn = stubSnapshot
        ctx.journalFn = stubJournal
        rc = runSoak(ctx)
        fh.close()
        assert rc == 1, rc
        initRec = [r for r in _readRecords(outDir) if r.get('step') == 'Init'][0]
        assert initRec['failureKind'] == 'timeout', initRec
        assert calls['journal'] == 1, calls
    finally:
        shutil.rmtree(outDir, ignore_errors=True)


def _test_refusal():
    rfdc = StubRfdc()
    rfdc.IgnoreMetalError = Val(True)

    ctx, fh, outDir = _newCtxForTest()
    try:
        ctx.devGuiFactory = StubGui
        ctx.attachFn = lambda port: StubRoot(rfdc)
        ctx.snapshotFn = lambda board, path=None: {'bootId': 'b', 'tiles': {}}
        ctx.journalFn = lambda board, path: open(path, 'w').close()
        rc = runSoak(ctx)
        fh.close()
        assert rc == 2, rc
        records = _readRecords(outDir)
        assert any(r.get('type') == 'refused' for r in records), records
        assert not any(r.get('type') == 'step' and r.get('step') != 'BootStart' for r in records), \
            'a reset step ran while IgnoreMetalError was set'
    finally:
        shutil.rmtree(outDir, ignore_errors=True)


def _test_bootstart_failure_continue():
    rfdc = StubRfdc()
    guiState = {'index': 0}

    def guiFactory():
        guiState['index'] += 1
        # Boot 1 (startup) succeeds; boot 2 (after cycle 1's reboot) fails;
        # boot 3 (after cycle 2's reboot) succeeds again.
        return StubGui(failBoot=(guiState['index'] == 2))

    def stubSnapshot(board, path=None):
        data = {'bootId': 'boot-%d' % guiState['index'], 'tiles': {}}
        if path:
            with open(path, 'w') as f:
                json.dump(data, f)
        return data

    bootIdCounter = {'n': 0}

    def stubBootId(board):
        bootIdCounter['n'] += 1
        return 'boot-%d' % bootIdCounter['n']

    ctx, fh, outDir = _newCtxForTest(cycles=2, reboot_every=1, continue_=True)
    try:
        ctx.devGuiFactory = guiFactory
        ctx.attachFn = lambda port: StubRoot(rfdc)
        ctx.snapshotFn = stubSnapshot
        ctx.journalFn = lambda board, path: open(path, 'w').close()
        ctx.bootIdFn = stubBootId
        ctx.sshRebootFn = lambda board: None
        ctx.waitReadyFn = lambda board, timeout: (CONFIG_STATUS_OK, 0.1)
        rc = runSoak(ctx)
        fh.close()
        assert rc == 1, rc
        records = _readRecords(outDir)
        cycle2Steps = [r for r in records if r.get('type') == 'step' and r.get('cycle') == 2]
        assert any(r.get('skipped') == 'no host tree' for r in cycle2Steps), cycle2Steps
        rebootSteps = [r for r in records if r.get('step') == 'Reboot']
        assert len(rebootSteps) == 2, rebootSteps
    finally:
        shutil.rmtree(outDir, ignore_errors=True)


def runSelfTest():
    _test_diag_basic()
    _test_diag_wrapped()
    _test_multitile_diag()
    _test_jsonl_roundtrip()
    _test_reboot_schedule()
    _test_stop_and_continue()
    _test_timeout_classification()
    _test_refusal()
    _test_bootstart_failure_continue()
    print('SELFTEST_OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
