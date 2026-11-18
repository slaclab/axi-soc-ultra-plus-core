#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue as pr

class RfdcCalFreeze(pr.Device):
    """RF-ADC background calibration freeze control (RfdcCalFreeze.vhd).

    The RF-ADC gain (GCB) and time skew (TSCB) background calibration drifts
    when the input drops below -40 dBFS, so the product guide asks for it to
    be frozen while the input is absent for more than 100 us. Each channel is
    frozen by ManualFreeze, or by the signal detector while AutoEnable is set.

    Channel: one RF-ADC converter (a block of the RF Data Converter IP core,
    not a tile), the unit with its own sample stream and its own
    int_cal_freeze, cal_frozen and sig_detect pins. Every register with one
    bit per channel uses bit c for channel c, the channel being the index of
    the adcI<c>/adcQ<c> port of the firmware. Which tile and block that is
    depends on how the application wired the firmware.

    Signal detector: the mean ADC magnitude (|I|+|Q| per complex sample in
    I/Q mode, |x| per sample in real mode) is averaged over back to back
    windows of 2^Window DSP clock cycles, the clock the ADC samples arrive
    on. At the end of every window the mean is published in Magnitude[c] and
    compared with Threshold. HoldOff consecutive windows below Threshold (no
    signal) assert the freeze, so it lands HoldOff to HoldOff+1 windows after
    the signal disappears. A single window at or above Threshold (signal
    back) releases it. The polarity is "freeze while the signal is low",
    since the calibration drifts when it has no signal to train on. With the
    defaults (Window 15, HoldOff 1) and a 307.2 MHz DSP clock a window is
    106.7 us, so a channel freezes 107 to 213 us after its input drops below
    -40 dBFS.

    The RF-ADC honours the freeze pin only while the PS driver's
    Calibration.DisableFreezePin of the block is 0 (its default).

    numCh must match the NUM_CH_G generic of the firmware instance.
    """
    def __init__(self, numCh=1, **kwargs):
        super().__init__(**kwargs)

        self.add(pr.RemoteVariable(
            name         = 'NUM_CH_G',
            description  = 'Number of ADC channels VHDL Generic configuration',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 0,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'SAMPLE_PER_CYCLE_G',
            description  = 'Samples per channel per DSP clock VHDL Generic configuration (complex samples in I/Q mode)',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 8,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'WINDOW_MAX_C',
            description  = 'Largest Window value the signal detector accumulator supports',
            offset       = 0x00,
            bitSize      = 8,
            bitOffset    = 16,
            mode         = 'RO',
            disp         = '{:d}',
        ))

        self.add(pr.RemoteVariable(
            name         = 'IQ_MODE_G',
            description  = 'Sample format VHDL Generic configuration: I/Q samples on adcI/adcQ (Magnitude is mean |I|+|Q| per complex sample) or real samples on adcI (Magnitude is mean |x| per sample)',
            offset       = 0x00,
            bitSize      = 1,
            bitOffset    = 24,
            mode         = 'RO',
            enum         = {
                0: 'Real',
                1: 'IQ',
            },
        ))

        self.add(pr.RemoteVariable(
            name         = 'COMMON_CLK_G',
            description  = 'dspClk and axilClk are the same clock VHDL Generic configuration',
            offset       = 0x00,
            bitSize      = 1,
            bitOffset    = 25,
            mode         = 'RO',
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'ManualFreeze',
            description  = 'Freeze the GCB and TSCB background calibration of the channel (one bit per channel)',
            offset       = 0x10,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'AutoEnable',
            description  = 'Let the signal detector freeze the calibration of the channel while its input is absent: Magnitude below Threshold for HoldOff consecutive windows asserts the freeze, one window at or above Threshold releases it (one bit per channel)',
            offset       = 0x14,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'Threshold',
            description  = 'Signal detector threshold on Magnitude (16-bit full scale): a window with Magnitude below it counts as no signal. A -40 dBFS tone, the level below which the product guide asks for the freeze, gives 417 in I/Q mode and 209 in real mode',
            offset       = 0x18,
            bitSize      = 16,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'Window',
            description  = 'Signal detector window of 2^Window DSP clock cycles (the clock the ADC samples arrive on, SAMPLE_PER_CYCLE_G samples per cycle), WINDOW_MAX_C at most. Magnitude is updated and compared with Threshold at the end of every window. 15 is 106.7 us at 307.2 MHz, the product guide asks for the freeze after 100 us without signal',
            offset       = 0x1C,
            bitSize      = 5,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'HoldOff',
            description  = 'Consecutive windows with Magnitude below Threshold (no signal) before the signal detector asserts the freeze, so the freeze lands HoldOff to HoldOff+1 windows after the signal disappears. One window at or above Threshold releases it. 0 behaves as 1',
            offset       = 0x20,
            bitSize      = 8,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'CalFrozen',
            description  = 'cal_frozen status of the RF-ADC of the channel (one bit per channel)',
            offset       = 0x30,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'AutoFreeze',
            description  = 'Signal detector state of the channel, 1 while it sees no signal and asks for the freeze (one bit per channel)',
            offset       = 0x34,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'SigDetect',
            description  = 'sig_detect output of the RF-ADC of the channel, the IP core signal magnitude detector (one bit per channel)',
            offset       = 0x38,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'CalFreeze',
            description  = 'int_cal_freeze pin driven to the RF-ADC of the channel: ManualFreeze or (AutoEnable and AutoFreeze) (one bit per channel)',
            offset       = 0x3C,
            bitSize      = numCh,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.addRemoteVariables(
            name         = 'Magnitude',
            description  = 'Mean ADC magnitude of the channel over the last signal detector window, the value compared with Threshold: |I|+|Q| per complex sample in I/Q mode, |x| per sample in real mode',
            offset       = 0x40,
            bitSize      = 16,
            bitOffset    = 0,
            base         = pr.UInt,
            mode         = 'RO',
            number       = numCh,
            stride       = 4,
            pollInterval = 1,
        )
