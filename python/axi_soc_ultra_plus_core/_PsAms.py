#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import math
import os
import select
import subprocess
import threading
import time

import pyrogue as pr

__all__ = ['PsAms']

# (name, units, kind, sysfs channel index, description)
# The channels are selected by sysfs index because the labels repeat on this
# IP. The PL-side duplicates (in_voltage21, in_voltage26, in_voltage27 and
# in_temp20) read 0 on this board.
CHANNELS = (
    ('Temp_LPD',    'degC', 'temp', 7,  'PS low-power domain temperature'),
    ('Temp_FPD',    'degC', 'temp', 8,  'PS full-power domain temperature'),
    ('VCC_PSINTLP', 'V',    'volt', 9,  'PS low-power domain core supply'),
    ('VCC_PSINTFP', 'V',    'volt', 10, 'PS full-power domain core supply'),
    ('VCCINT',      'V',    'volt', 2,  'PL core supply measured by the PS AMS'),
)

# Marks the end of every reply from the remote shell
SENTINEL = b'__END__\n'

# The PS IP is set by the ATCA crate slot, so a board swap or a reflash puts a
# new host key behind the same IP. The host key is neither checked nor stored,
# while the login itself still requires the public key.
SSH_OPTS = (
    '-o', 'BatchMode=yes',
    '-o', 'StrictHostKeyChecking=no',
    '-o', 'UserKnownHostsFile=/dev/null',
    '-o', 'LogLevel=ERROR',
)

class PsAms(pr.Device):
    def __init__(self, ipPS, minAge=0.5, readTimeout=2.0, retryPeriod=10.0, **kwargs):
        super().__init__(**kwargs)

        self._ipPS        = ipPS
        self._minAge      = minAge
        self._readTimeout = readTimeout
        self._retryPeriod = retryPeriod

        self._lock        = threading.Lock()
        self._proc        = None
        self._lastRefresh = -math.inf
        self._lastConnect = -math.inf
        self._healthy     = None
        self._values      = {name: math.nan for name, _, _, _, _ in CHANNELS}

        # One round trip reads every channel, each scale file included
        files = []
        for _, _, kind, index, _ in CHANNELS:
            if kind == 'temp':
                files.append(f'in_temp{index}_input')
            else:
                files.append(f'in_voltage{index}_raw')
                files.append(f'in_voltage{index}_scale')
        self._refreshCmd = ('cd "$D" && grep -H "" ' + ' '.join(files) + '; echo __END__\n').encode()

        for name, units, kind, index, text in CHANNELS:
            if kind == 'temp':
                fileName = f'in_temp{index}_input'
            else:
                fileName = f'in_voltage{index}_raw'
            self.add(pr.LocalVariable(
                name         = name,
                description  = f'{text} (xilinx-ams {fileName})',
                mode         = 'RO',
                units        = units,
                disp         = '{:1.3f}',
                value        = math.nan,
                localGet     = lambda name=name: self._get(name),
                pollInterval = 5,
            ))

    def _get(self, name):
        # pyrogue applies the enable to a LocalBlock only when the enable
        # changes, so a device built disabled still reaches localGet. The
        # check here keeps the ssh session closed until the device is enabled
        if self.enable.value() is not True:
            return self._values[name]

        # pyrogue calls localGet from the caller, update and poll threads on
        # every get, so the pipe access is serialized and cached for minAge
        # seconds
        with self._lock:
            if time.monotonic() - self._lastRefresh >= self._minAge:
                self._refresh()
            return self._values[name]

    def _setAllNan(self):
        for name in self._values:
            self._values[name] = math.nan

    def _refresh(self):
        try:
            # An ssh child that exited on its own is reaped before any reconnect
            if self._proc is not None and self._proc.poll() is not None:
                self._close()

            # Throttled reconnect: a dead link is retried once per retryPeriod
            if self._proc is None and time.monotonic() - self._lastConnect < self._retryPeriod:
                self._setAllNan()
                return

            try:
                if self._proc is None:
                    self._connect()
                self._proc.stdin.write(self._refreshCmd)
                lines = self._readReply(self._readTimeout)

                reply = {}
                for line in lines:
                    fileName, sep, text = line.partition(':')
                    if sep:
                        reply[fileName] = text.strip()

                for name, _, kind, index, _ in CHANNELS:
                    try:
                        if kind == 'temp':
                            # The _input file is in millidegC with the channel
                            # offset already applied by the kernel, so the
                            # temperatures do not use raw times scale
                            value = float(reply[f'in_temp{index}_input']) / 1000.0
                        else:
                            value = float(reply[f'in_voltage{index}_raw']) * float(reply[f'in_voltage{index}_scale']) / 1000.0
                    except (KeyError, ValueError):
                        value = math.nan
                    self._values[name] = value

                if self._healthy is False:
                    self._log.info(f'PS AMS read from root@{self._ipPS} recovered')
                self._healthy = True

            except (OSError, EOFError, TimeoutError, ConnectionError):
                # Log once on the state change, not on every failed sample
                if self._healthy is not False:
                    self._log.warning(
                        f'PS AMS read from root@{self._ipPS} failed, check '
                        f'"ssh {" ".join(SSH_OPTS)} root@{self._ipPS} true"')
                self._healthy = False
                self._close()
                self._setAllNan()
        finally:
            self._lastRefresh = time.monotonic()

    def _connect(self):
        # Counted before the attempt, so a failed Popen also starts the retry throttle
        self._lastConnect = time.monotonic()

        # A new session keeps the terminal Ctrl-C, which goes to the whole
        # foreground process group, away from the ssh child, so the session
        # ends only through _close or when this process exits and the stdin
        # pipe closes
        self._proc = subprocess.Popen(
            [
                'ssh',
                *SSH_OPTS,
                '-o', 'ConnectTimeout=5',
                '-o', 'ServerAliveInterval=5',
                '-o', 'ServerAliveCountMax=2',
                f'root@{self._ipPS}',
                'sh',
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
            start_new_session=True,
        )

        # Find the xilinx-ams IIO device by name instead of assuming iio:device0
        self._proc.stdin.write(
            b'for d in /sys/bus/iio/devices/iio:device*; do '
            b'[ "$(cat $d/name)" = xilinx-ams ] && D=$d; done; '
            b'echo "D=$D"; echo __END__\n')
        lines = self._readReply(8.0)
        if not any(line.startswith('D=') and len(line) > 2 for line in lines):
            raise ConnectionError(f'no xilinx-ams device found on root@{self._ipPS}')

    def _readReply(self, timeout):
        # Never blocks past the deadline, so a hung PS cannot stall the
        # single pyrogue update thread
        deadline = time.monotonic() + timeout
        fd = self._proc.stdout.fileno()
        buf = b''
        while not buf.endswith(SENTINEL):
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([fd], [], [], left)[0]:
                raise TimeoutError('PS AMS read timed out')
            chunk = os.read(fd, 4096)
            if not chunk:
                raise EOFError('ssh session closed')
            buf += chunk
        return buf.decode(errors='replace').splitlines()[:-1]

    def _close(self):
        if self._proc is not None:
            try:
                self._proc.stdin.close()
            except OSError:
                pass
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()
            self._proc.stdout.close()
            self._proc = None

    def _stop(self):
        super()._stop()
        with self._lock:
            self._close()
