#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter block
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class PyRFdc(rogue::interfaces::memory)
#-----------------------------------------------------------------------------
# This file is part of the 'SLAC Firmware Standard Library'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'SLAC Firmware Standard Library', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import itertools
import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

enumEventSource = {
    0x0 : "XRFDC_EVNT_SRC_IMMEDIATE",
    0x1 : "XRFDC_EVNT_SRC_SLICE",
    0x2 : "XRFDC_EVNT_SRC_TILE",
    0x3 : "XRFDC_EVNT_SRC_SYSREF",
    0x4 : "XRFDC_EVNT_SRC_MARKER",
    0x5 : "XRFDC_EVNT_SRC_PL",
    0x6 : "ERROR_6",
    0x7 : "ERROR_7",
}

enumInterpDecim         = {
    0x0  : "XRFDC_INTERP_DECIM_OFF",
    0x1  : "XRFDC_INTERP_DECIM_1X",
    0x2  : "XRFDC_INTERP_DECIM_2X",
    0x3  : "XRFDC_INTERP_DECIM_3X",
    0x4  : "XRFDC_INTERP_DECIM_4X",
    0x5  : "XRFDC_INTERP_DECIM_5X",
    0x6  : "XRFDC_INTERP_DECIM_6X",
    0x8  : "XRFDC_INTERP_DECIM_8X",
    0xA  : "XRFDC_INTERP_DECIM_10X",
    0xC  : "XRFDC_INTERP_DECIM_12X",
    0x10 : "XRFDC_INTERP_DECIM_16X",
    0x14 : "XRFDC_INTERP_DECIM_20X",
    0x18 : "XRFDC_INTERP_DECIM_24X",
    0x28 : "XRFDC_INTERP_DECIM_40X",
}

enumUpdateThreshold = {
    0x0 : "UNDEFINED_0x0",
    0x1 : "XRFDC_UPDATE_THRESHOLD_0",
    0x2 : "XRFDC_UPDATE_THRESHOLD_1",
    0x3 : "UNDEFINED_0x3",
    0x4 : "XRFDC_UPDATE_THRESHOLD_BOTH",
    0x5 : "ERROR_5",
    0x6 : "ERROR_6",
    0x7 : "ERROR_7",
}

enumMixedMode = {
    0x0 : "XRFDC_MIXER_MODE_OFF", #define XRFDC_MIXER_MODE_OFF 0x0U
    0x1 : "XRFDC_MIXER_MODE_C2C", #define XRFDC_MIXER_MODE_C2C 0x1U
    0x2 : "XRFDC_MIXER_MODE_C2R", #define XRFDC_MIXER_MODE_C2R 0x2U
    0x3 : "XRFDC_MIXER_MODE_R2C", #define XRFDC_MIXER_MODE_R2C 0x3U
    0x4 : "XRFDC_MIXER_MODE_R2R", #define XRFDC_MIXER_MODE_R2R 0x4U
    0x5 : "ERROR_5",
    0x6 : "ERROR_6",
    0x7 : "ERROR_7",
}

#######################################################################################
# ConfigVariable marks a setting the operator wrote in this process, so a later
# tile restart (which reloads the Vivado value into the hardware) can write it
# back. set() and post() record the value and a record sequence number: an
# operator set(), the GUI setDisp() and LoadConfig (setYaml) all reach set().
# A value is recorded only after its write returned without an error, only on an
# enabled device, and never while the Rfdc IgnoreMetalError debug setting is on (a
# driver error is then swallowed and cannot be told from success). A write=True set is
# recorded only when it waited for its transaction, which is the default: with
# wait=False, or the deprecated check=False, set() returns before a refusal can
# surface. A write=False set (the LoadConfig path, written in bulk afterwards) is
# recorded once pyrogue has accepted the value, even if the bulk write is later refused.
# An unconfirmed write on an enabled device (unwaited, or any write under
# IgnoreMetalError) records nothing and also drops the earlier record: the hardware may
# now hold the new value, so a later restart must treat the setting as never written
# and not replay an older value. A write under a disabled device (the hardware ignored
# it) and a write that raises leave the earlier record in place. get(), ReadAll,
# WriteAll and polling never record.
# The restart sequence in Rfdc replays the recorded value through
# pr.RemoteVariable.set, which bypasses the record, so the replay itself never marks
# anything as written.
#######################################################################################
_recordSeq = itertools.count(1)

# True when the device is enabled. A disabled device ignores a write without raising, so
# such a write neither records nor drops. Reads the shadow only, never the hardware.
def _enabled(dev):
    return dev.enable.value() is True

# True when the owning Rfdc runs with IgnoreMetalError set, so a driver error is swallowed
# and a write cannot be confirmed. Reads the shadow only, never the hardware.
def _ignoringErrors(dev):
    node = dev
    while node is not None and not isinstance(node, pr.Root):
        if isinstance(node, rfsoc_utility.Rfdc):
            return node.IgnoreMetalError.value() is True
        node = node.parent
    return False

class ConfigVariable(pr.RemoteVariable):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._recValue    = None
        self._recHasValue = False
        self._recSeq      = 0

    def _dropRecord(self):
        self._recSeq      = 0
        self._recHasValue = False
        self._recValue    = None

    def _recordWrite(self, value, index):
        self._recSeq = next(_recordSeq)
        if index == -1:
            self._recValue    = value
            self._recHasValue = True
        else:
            # An indexed write does not describe the whole variable: fall back to the shadow
            self._recHasValue = False

    # Same signatures and pr.expose as RemoteVariable, so a VirtualClient (the GUI)
    # still sees set() and post() on these variables
    @pr.expose
    def set(self, value, *, index=-1, write=True, verify=True, wait=None, check=None):
        ret = super().set(value, index=index, write=write, verify=verify, wait=wait, check=check)
        # Resolved the way rogue does: wait decides, else check, else the default of True.
        # An unwaited refusal surfaces only at a later waitBlocks, so it is never recorded
        # and the earlier record is dropped, as it may no longer match the hardware
        waited = wait if wait is not None else (check if check is not None else True)
        if not _enabled(self.parent):
            pass
        elif _ignoringErrors(self.parent) or (write and not waited):
            self._dropRecord()
        else:
            self._recordWrite(value, index)
        return ret

    @pr.expose
    def post(self, value, *, index=-1):
        ret = super().post(value, index=index)
        if not _enabled(self.parent):
            pass
        elif _ignoringErrors(self.parent):
            self._dropRecord()
        else:
            self._recordWrite(value, index)
        return ret

    def isRecorded(self):
        return self._recSeq != 0

    def recordSeq(self):
        return self._recSeq

    # The value recorded at set() time; the RW shadow only when none was recorded
    def replayValue(self):
        return self._recValue if self._recHasValue else self.value()

#######################################################################################
# recordCommit is the function of the Mixer, QMC and PllConfig commit commands. It
# records an operator commit so a restart can re-apply exactly what was committed:
# every ConfigVariable of the group is snapshotted (the value the operator wrote, or the
# staging word the commit is about to program for a field never written), the commit
# word is written, and the snapshot is stored only when that write returned without an
# error, the group is enabled and the Rfdc IgnoreMetalError debug setting is off (a
# driver error is then swallowed). A commit under IgnoreMetalError on an enabled group
# cannot be confirmed, so it drops the earlier snapshot and a restart treats the group
# as never committed. A commit under a disabled group and a commit that raises keep the
# earlier snapshot. The restart sequence in Rfdc writes the snapshot back and writes the
# commit word with RemoteCommand.set, which does not come back through here.
#######################################################################################
def recordCommit(group, cmd):
    snapshot = {}
    for name, var in group.variables.items():
        if isinstance(var, ConfigVariable):
            snapshot[name] = var.replayValue() if var.isRecorded() else var.get(read=True)
    cmd.set(1)
    if not _enabled(group):
        return
    if _ignoringErrors(group):
        group._commitSnapshot = None
        group._commitSeq      = 0
    else:
        group._commitSnapshot = snapshot
        group._commitSeq      = next(_recordSeq)

#######################################################################################
# MixerNcoVariable is the ConfigVariable of the Mixer Freq and PhaseOffset fields. An
# operator set() (or a GUI edit) commits the staged mixer through Mixer.UpdateEvent right
# after its own write, while the Mixer AutoUpdate setting is on, so the new NCO value
# takes effect without a separate commit. The commit runs only after a waited write that
# returned without an error on an enabled Mixer, and it runs synchronously, so a refused
# commit raises to the caller. A write=False set (the LoadConfig path), a set with
# wait=False (or the deprecated check=False) and post() never commit. The restart
# write-back in Rfdc writes these fields through pr.RemoteVariable.set, which bypasses
# this set(), so it never triggers a commit of its own.
#######################################################################################
class MixerNcoVariable(ConfigVariable):
    @pr.expose
    def set(self, value, *, index=-1, write=True, verify=True, wait=None, check=None):
        ret = super().set(value, index=index, write=write, verify=verify, wait=wait, check=check)
        waited = wait if wait is not None else (check if check is not None else True)
        mixer = self.parent
        if write and waited and _enabled(mixer) and mixer.AutoUpdate.value() is True:
            mixer.UpdateEvent()
        return ret

class RfdcBlock(pr.Device):
    def __init__(
            self,
            gen3        = True,  # True if using RFSoC GEN3 Hardware
            isAdc       = False, # True if this is an ADC tile
            description = 'RFSoC data converter block registers',
            **kwargs):
        super().__init__(description=description, **kwargs)
        self.gen3  = gen3
        self.isAdc = isAdc

        class BlockStatus(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_BlockStatus
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetBlockStatus
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'SampleRate',
                    description  = 'Sampling rate (GSPS).',
                    offset       = 0x000,
                    bitSize      = 64,
                    mode         = 'RO',
                    base         = pr.Double,
                    units        = 'GSPS',
                    disp         = '{:1.3f}',
                ))

                if isAdc:
                    self.add(pr.RemoteVariable(
                        name         = 'IsEnabled',
                        description  = 'Converter enable/disable.',
                        offset       = 0x008,
                        bitSize      = 1,
                        bitOffset    = 0,
                        mode         = 'RO',
                        base         = pr.Bool,
                    ))
                else:
                    self.add(pr.RemoteVariable(
                        name         = 'InvSincEnabled',
                        description  = 'Inverse sinc enable/disable',
                        offset       = 0x008,
                        bitSize      = 4,
                        bitOffset    = 0,
                        mode         = 'RO',
                    ))

                    self.add(pr.RemoteVariable(
                        name         = 'DecoderMode',
                        offset       = 0x008,
                        bitSize      = 4,
                        bitOffset    = 4,
                        mode         = 'RO',
                    ))

                self.add(pr.RemoteVariable(
                    name         = 'FIFOStatus',
                    offset       = 0x008,
                    bitSize      = 4,
                    bitOffset    = 8,
                    mode         = 'RO',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'DecimationFactor' if isAdc else 'InterpolationFactor',
                    offset       = 0x008,
                    bitSize      = 4,
                    bitOffset    = 12,
                    mode         = 'RO',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'MixerMode' if isAdc else 'AdderStatus',
                    offset       = 0x008,
                    bitSize      = 4,
                    bitOffset    = 16,
                    mode         = 'RO',
                    enum         = enumMixedMode if isAdc else None,
                ))

                if not isAdc:

                    self.add(pr.RemoteVariable(
                        name         = 'MixerMode',
                        offset       = 0x008,
                        bitSize      = 4,
                        bitOffset    = 20,
                        mode         = 'RO',
                        enum         = enumMixedMode,
                    ))

                self.add(pr.RemoteVariable(
                    name         = 'DataPathClocksStatus',
                    description  = 'Indicates if all required datapath clocks are enabled; 1 if all clocks enabled, 0 otherwise',
                    offset       = 0x008,
                    bitSize      = 1,
                    bitOffset    = 24,
                    mode         = 'RO',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'IsFIFOFlagsEnabled',
                    description  = 'FIFO flags enabled mask; 1 is enabled, otherwise 0.',
                    offset       = 0x008,
                    bitSize      = 1,
                    bitOffset    = 25,
                    mode         = 'RO',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'IsFIFOFlagsAsserted',
                    description  = 'FIFO flags asserted mask; 1 is enabled, otherwise 0',
                    offset       = 0x008,
                    bitSize      = 1,
                    bitOffset    = 26,
                    mode         = 'RO',
                    base         = pr.Bool,
                ))

        # Adding the BlockStatus device
        self.add(BlockStatus())

        class Mixer(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                self._commitSnapshot = None
                self._commitSeq      = 0
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetMixerSettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixerSettings
                #######################################################################################
                self.add(pr.LocalVariable(
                    name         = 'AutoUpdate',
                    description  = 'When True, a Freq or PhaseOffset set() commits the staged mixer settings (UpdateEvent) right after its write. Set False to stage several fields and commit them with one UpdateEvent',
                    mode         = 'RW',
                    value        = True,
                ))

                self.add(MixerNcoVariable(
                    name         = 'Freq',
                    description  = 'NCO frequency. Range: -Fs to Fs (MHz). A set() commits the mixer (UpdateEvent) while AutoUpdate is True',
                    offset       = 0x020,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'MHz',
                ))

                self.add(MixerNcoVariable(
                    name         = 'PhaseOffset',
                    description  = 'NCO phase offset. Range: -180 to 180 (Exclusive). A set() commits the mixer (UpdateEvent) while AutoUpdate is True',
                    offset       = 0x028,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'degree',
                ))

                self.add(ConfigVariable(
                    name         = 'EventSource',
                    description  = 'Event source for mixer settings. XRFDC_EVNT_SRC_* represents valid values.',
                    offset       = 0x030,
                    bitSize      = 3,
                    mode         = 'RW',
                    enum         = enumEventSource,
                ))

                self.add(ConfigVariable(
                    name         = 'CoarseMixFreq',
                    description  = 'Coarse mixer frequency. XRFDC_COARSE_MIX_* represents valid values',
                    offset       = 0x034,
                    bitSize      = 5,
                    mode         = 'RW',
                    enum         = {
                        0x00 : "XRFDC_COARSE_MIX_OFF",
                        0x02 : "XRFDC_COARSE_MIX_SAMPLE_FREQ_BY_TWO",
                        0x04 : "XRFDC_COARSE_MIX_SAMPLE_FREQ_BY_FOUR",
                        0x08 : "XRFDC_COARSE_MIX_MIN_SAMPLE_FREQ_BY_FOUR",
                        0x10 : "XRFDC_COARSE_MIX_BYPASS",
                    },
                ))

                self.add(ConfigVariable(
                    name         = 'MixerMode',
                    description  = 'Mixer mode for fine or coarse mixer. XRFDC_MIXER_MODE_* represents valid values',
                    offset       = 0x038,
                    bitSize      = 3,
                    bitOffset    = 0,
                    mode         = 'RW',
                    enum         = {
                        0x0 : "XRFDC_MIXER_MODE_OFF",
                        0x1 : "XRFDC_MIXER_MODE_C2C",
                        0x2 : "XRFDC_MIXER_MODE_C2R",
                        0x3 : "XRFDC_MIXER_MODE_R2C",
                        0x4 : "XRFDC_MIXER_MODE_R2R",
                        0x5 : "ERROR_5",
                        0x6 : "ERROR_6",
                        0x7 : "ERROR_7",
                    },
                ))

                self.add(ConfigVariable(
                    name         = 'FineMixerScale',
                    description  = 'NCO output scale. XRFDC_MIXER_SCALE_* represents valid values',
                    offset       = 0x038,
                    bitSize      = 2,
                    bitOffset    = 8,
                    mode         = 'RW',
                    enum         = {
                        0x0 : "XRFDC_MIXER_SCALE_AUTO",
                        0x1 : "XRFDC_MIXER_SCALE_1P0",
                        0x2 : "XRFDC_MIXER_SCALE_0P7",
                        0x3 : "ERROR",
                    },
                ))

                self.add(ConfigVariable(
                    name         = 'MixerType',
                    description  = 'Mixer Type indicates coarse or fine mixer. XRFDC_MIXER_TYPE_* represents valid values',
                    offset       = 0x038,
                    bitSize      = 2,
                    bitOffset    = 16,
                    mode         = 'RW',
                    enum         = {
                        0x0 : "XRFDC_MIXER_TYPE_OFF",
                        0x1 : "XRFDC_MIXER_TYPE_COARSE",
                        0x2 : "XRFDC_MIXER_TYPE_FINE",
                        0x3 : "XRFDC_MIXER_TYPE_DISABLED",
                    },
                ))

                self.add(pr.RemoteCommand(
                    name         = 'UpdateEvent',
                    description  = 'Commit the staged mixer settings: always programs them (XRFdc_SetMixerSettings) and issues the update event only for a Tile or Slice event source. Immediate applies at once; SYSREF, PL or MARKER apply at the next external event the application issues',
                    offset       = 0x03C,
                    bitSize      = 1,
                    function     = lambda cmd: recordCommit(self, cmd),
                ))

                #######################################################################################
                # Heavy-update path: UpdateEvent + tile RestartSM. Triggers the IP power-on state
                # machine which re-runs Converter_Calibration[0..2] when the parent tile's
                # RestartStateStart is set to a step <= Clock_Configuration[0]. Matches the surf
                # v1.7.0 legacy RfBlock NCO-update pattern. Use only when the converter trim is
                # operating-point-sensitive (e.g. cryo-detectors, qubit readout). Plain
                # UpdateEvent() is ~100x faster and sufficient for most consumers.
                # Pre-condition: set parent tile's RestartStateStart (e.g. to 'Clock_Configuration[0]')
                # before invoking. Post-condition: caller may want to poll the parent tile's
                # CurrentState until 'Done' (15) before issuing dependent commands.
                #######################################################################################
                self.add(pr.LocalCommand(
                    name        = 'UpdateEventWithRestart',
                    description = 'Commit pending Mixer settings (UpdateEvent) AND trigger the parent tile RestartSM. See class comment for pre/post conditions.',
                    function    = self._UpdateEventWithRestart,
                ))

            def _UpdateEventWithRestart(self):
                # Step 1: flush the cached mixer config to HW via the standard event mechanism.
                self.UpdateEvent()
                # Step 2: trigger the parent tile's RestartSM. With RestartStateStart at
                # Clock_Configuration[0], the IPSM window 6..15 includes Converter_Calibration.
                # parent chain: Mixer -> RfdcBlock -> RfdcTile.
                self.parent.parent.RestartSM()

        # The Mixer device follows the block sample rate only. XRFdc_GetBlockStatus maps the
        # ADC mixer mode register to OFF, C2C or R2C, so an R2R bypass mixer reads
        # BlockStatus.MixerMode 0 (a DAC block reads 0 as well), and gating on it would
        # disable the Mixer device, and drop its writes, on every running block
        self.add(pr.LinkVariable(
            name         = 'IsMixerEnabled',
            mode         = 'RO',
            linkedGet    = lambda read: self.BlockStatus.SampleRate.get(read=read) > 0.0,
            dependencies = [self.BlockStatus.SampleRate],
        ))

        # Adding the Mixer device
        self.add(Mixer(enableDeps=[self.IsMixerEnabled]))

        class QMC(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                self._commitSnapshot = None
                self._commitSeq      = 0
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetQMCSettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetQMCSettings
                #######################################################################################
                self.add(ConfigVariable(
                    name         = 'EnablePhase',
                    description  = 'Indicates if phase is enabled (1) or disabled (0)',
                    offset       = 0x040,
                    bitSize      = 1,
                    bitOffset    = 0,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(ConfigVariable(
                    name         = 'EnableGain',
                    description  = 'Indicates if gain is enabled(1) or disabled (0)',
                    offset       = 0x040,
                    bitSize      = 1,
                    bitOffset    = 1,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(ConfigVariable(
                    name         = 'EventSource',
                    description  = 'Event source for QMC settings. XRFDC_EVNT_SRC_* represents valid values',
                    offset       = 0x044,
                    bitSize      = 3,
                    mode         = 'RW',
                    enum         = enumEventSource,
                ))

                self.add(ConfigVariable(
                    name         = 'GainCorrectionFactor',
                    description  = 'Gain correction factor. Range: 0 to 2.0 (Exclusive).',
                    offset       = 0x048,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                ))

                self.add(ConfigVariable(
                    name         = 'PhaseCorrectionFactor',
                    description  = 'Phase correction factor. Range: +/- 26.5 degrees (Exclusive)',
                    offset       = 0x050,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'degree',
                ))

                self.add(ConfigVariable(
                    name         = 'OffsetCorrectionFactor',
                    description  = 'Offset correction factor is adding a fixed LSB value to the sampled signal',
                    offset       = 0x058,
                    bitSize      = 32,
                    mode         = 'RW',
                    base         = pr.Int, # s32
                ))

                self.add(pr.RemoteCommand(
                    name         = 'UpdateEvent',
                    description  = 'Commit the staged QMC settings: always programs them (XRFdc_SetQMCSettings) and issues the update event only for a Tile or Slice event source. Immediate applies at once; SYSREF, PL or MARKER apply at the next external event the application issues',
                    offset       = 0x05C,
                    bitSize      = 1,
                    function     = lambda cmd: recordCommit(self, cmd),
                ))

        # Adding the QMC device
        self.add(QMC())

        class CoarseDelay(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_CoarseDelay_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCoarseDelaySettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoarseDelaySettings
                #######################################################################################
                self.add(ConfigVariable(
                    name         = 'CoarseDelay',
                    description  = 'Coarse delay in the number of samples. Range: 0 to 7 for Gen 1/Gen 2 devices and 0 to 40 for Gen 3/DFE devices',
                    offset       = 0x060,
                    bitSize      = 8,
                    bitOffset    = 0,
                    maximum      = 40 if gen3 else 7,
                    mode         = 'RW',
                ))

                self.add(ConfigVariable(
                    name         = 'EventSource',
                    description  = 'Event source for coarse delay settings. XRFDC_EVNT_SRC_* represents valid values',
                    offset       = 0x060,
                    bitSize      = 3,
                    bitOffset    = 8,
                    mode         = 'RW',
                    enum         = enumEventSource,
                ))

                self.add(pr.RemoteCommand(
                    name         = 'UpdateEvent',
                    description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
                    offset       = 0x064,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.set(1),
                ))

        # Adding the CoarseDelay device
        self.add(CoarseDelay())

        if not isAdc: # isAdc = false (DAC)
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetInterpolationFactor
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
            #######################################################################################
            self.add(ConfigVariable(
                name         = 'InterpolationFactor',
                description  = 'This API function sets the interpolation factor for the requested RF-DAC and also updates the FIFO read width based on the interpolation factor',
                offset       = 0x068,
                bitSize      = 6,
                mode         = 'RW',
                enum         = enumInterpDecim,
            ))

        else: # isAdc = true (ADC)
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactor
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
            #######################################################################################
            self.add(ConfigVariable(
                name         = 'DecimationFactor',
                description  = 'This API function sets the decimation factor for the requested RF-ADC and also updates the FIFO write width based on the decimation factor',
                offset       = 0x070,
                bitSize      = 6,
                mode         = 'RW',
                enum         = enumInterpDecim,
            ))

            if gen3:
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactorObs-Gen-3/DFE
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactorObs-Gen-3/DFE
                #######################################################################################
                self.add(ConfigVariable(
                    name         = 'DecimationFactorObs',
                    description  = 'This API function sets the decimation factor for the observation channel of the requested RF-ADC and also updates the FIFO write width based on the decimation factor',
                    offset       = 0x074,
                    bitSize      = 6,
                    mode         = 'RW',
                    enum         = enumInterpDecim,
                ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabWrVldWords
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWords
        #######################################################################################
        # Read only on an ADC block, so only the DAC variable is tracked
        self.add((pr.RemoteVariable if isAdc else ConfigVariable)(
            name         = 'FabWrVldWords',
            description  = 'This API function sets the write fabric data rate for the requested RF-DAC by writing to the corresponding register',
            offset       = 0x078,
            bitSize      = 32,
            mode         = 'RO' if isAdc else 'RW',
            hidden       = True,
        ))

        #######################################################################################
        # XRFdc_GetFabWrVldWordsObs appears to always return XRFDC_FAILURE
        #######################################################################################
#        if gen3:
#            #######################################################################################
#            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWordsObs-Gen-3/DFE
#            #######################################################################################
#            self.add(pr.RemoteVariable(
#                name         = 'FabWrVldWordsObs',
#                description  = 'Write PL data rate for the observation channel of the requested RF-ADCis returned back to the caller',
#                offset       = 0x07C,
#                bitSize      = 32,
#                mode         = 'RO',
#                hidden       = True,
#            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabRdVldWords
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabRdVldWords
        #######################################################################################
        # Read only on a DAC block, so only the ADC variable is tracked
        self.add((ConfigVariable if isAdc else pr.RemoteVariable)(
            name         = 'FabRdVldWords',
            description  = 'This API function sets the read PL data rate for the requested RF-ADC by writing to the corresponding register',
            offset       = 0x080,
            bitSize      = 32,
            mode         = 'RW' if isAdc else 'RO',
            hidden       = True,
        ))

        if gen3 and isAdc:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabRdVldWordsObs-Gen-3/DFE
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWordsObs-Gen-3/DFE
            #######################################################################################
            self.add(ConfigVariable(
                name         = 'FabRdVldWordsObs',
                description  = 'Write PL data rate for the observation channel of the requested RF-ADCis returned back to the caller.',
                offset       = 0x084,
                bitSize      = 32,
                mode         = 'RW',
                hidden       = True,
            ))

        class Threshold(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ThresholdStickyClear
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'ThresholdStickyClear',
                    description  = 'This API function clears the sticky bit in threshold configuration registers based on the ThresholdToUpdate parameter',
                    offset       = 0x088,
                    bitSize      = 3,
                    mode         = 'WO',
                    enum         = enumUpdateThreshold,
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdClrMode
                #######################################################################################
                self.add(ConfigVariable(
                    name         = 'ThresholdClrMode_ThresholdToUpdate',
                    description  = 'This API function sets the threshold clear mode',
                    offset       = 0x08C,
                    bitSize      = 3,
                    bitOffset    = 0,
                    mode         = 'WO',
                    enum         = enumUpdateThreshold,
                ))

                self.add(ConfigVariable(
                    name         = 'ThresholdClrMode_ClrMode',
                    description  = 'This API function sets the threshold clear mode',
                    offset       = 0x08C,
                    bitSize      = 2,
                    bitOffset    = 8,
                    mode         = 'WO',
                    enum         = {
                        0x0 : "UNDEFINED",
                        0x1 : "XRFDC_THRESHOLD_CLRMD_MANUAL_CLR", #define XRFDC_THRESHOLD_CLRMD_MANUAL_CLR 0x1U
                        0x2 : "XRFDC_THRESHOLD_CLRMD_AUTO_CLR",   #define XRFDC_THRESHOLD_CLRMD_AUTO_CLR 0x2U
                    },
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Threshold_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdSettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetThresholdSettings
                #######################################################################################
                thresholdReg = {
                    "ThresholdMode[0]": 0x90,
                    "ThresholdMode[1]": 0x94,
                }

                for name, offset in thresholdReg.items():
                    self.add(ConfigVariable(
                        name    = name,
                        offset  = offset,
                        bitSize = 2,
                        mode    = 'RW',
                        enum    = {
                            0 : "OFF",
                            1 : "sticky-over",
                            2 : "sticky-under",
                            3 : "hysteresis",
                        },
                    ))

                thresholdReg = {
                    "ThresholdAvgVal[0]":   0x98,
                    "ThresholdAvgVal[1]":   0x9C,
                    "ThresholdUnderVal[0]": 0xA0,
                    "ThresholdUnderVal[1]": 0xA4,
                    "ThresholdOverVal[0]":  0xA8,
                    "ThresholdOverVal[1]":  0xAC,
                }

                for name, offset in thresholdReg.items():
                    self.add(ConfigVariable(
                        name    = name,
                        offset  = offset,
                        bitSize = 32,
                        mode    = 'RW',
                    ))

        # Adding the Threshold device
        if isAdc:
            self.add(Threshold())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecoderMode
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecoderMode
        #######################################################################################
        if not isAdc:
            self.add(ConfigVariable(
                name         = 'DecoderMode',
                description  = 'This API function writes the decoder mode to the relevant registers. The driver structure is updated with the new values.',
                offset       = 0x0B0,
                bitSize      = 2,
                mode         = 'RW',
                enum         = {
                    0x0 : "UNDEFINED",
                    0x1 : "XRFDC_DECODER_MAX_SNR_MODE",       #define XRFDC_DECODER_MAX_SNR_MODE 0x1U
                    0x2 : "XRFDC_DECODER_MAX_LINEARITY_MODE", #define XRFDC_DECODER_MAX_LINEARITY_MODE 0x2U
                },
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetNCOPhase
        #######################################################################################
        self.add(pr.RemoteCommand(
            name         = 'ResetNCOPhase',
            description  = 'This API function arms the NCO phase reset of the current block phase accumulator',
            offset       = 0x0B4,
            bitSize      = 1,
            function     = lambda cmd: cmd.set(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetOutputCurr
        #######################################################################################
        if not isAdc:
            self.add(pr.RemoteVariable(
                name         = 'OutputCurr',
                offset       = 0x0B8,
                bitSize      = 32,
                mode         = 'RO',
                disp         = '{:d}',
                units        = 'μA',
                pollInterval = 1,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACVOP-Gen-3/DFE
        #######################################################################################
        if not isAdc:
            self.add(ConfigVariable(
                name         = 'DACVOP',
                description  = 'VOP μA current is used to update the corresponding block level registers.',
                offset       = 0x170,
                bitSize      = 32,
                minimum      = 2250,
                maximum      = 40500,
                mode         = 'WO',
                units        = 'μA',
                disp         = '{:d}',
            ))


        ###########################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetNyquistZone
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNyquistZone
        ###########################################################################
        self.add(ConfigVariable(
            name         = 'NyquistZone',
            description  = 'This API function sets the Nyquist zone for the RF-ADC/RF-DACs',
            offset       = 0x0BC,
            bitSize      = 2,
            mode         = 'RW',
            enum         = {
                0 : "Undefined",
                1 : "Odd",
                2 : "Even",
            },
        ))

        if not isAdc:
            ###########################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetInvSincFIR
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInvSincFIR
            ###########################################################################
            self.add(ConfigVariable(
                name         = 'InvSincFIR',
                description  = 'This API function is used to enable or disable the inverse sinc filter.',
                offset       = 0x0C0,
                bitSize      = 2,
                mode         = 'RW',
                enum         = {
                    0 : "disable",
                    1 : "Odd",
                    2 : "Even",
                },
            ))

        if isAdc:
            class Calibration(pr.Device):
                def __init__(self,**kwargs):
                    super().__init__(**kwargs)
                    ###############################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalibrationMode
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalibrationMode
                    ###############################################################################
                    self.add(ConfigVariable(
                        name         = 'CalibrationMode',
                        description  = 'Method to execute the RFSoC PS rfdc-CalibrationMode executable remotely',
                        offset       = 0x0C4,
                        bitSize      = 2,
                        mode         = 'RW',
                        enum         = {
                            0 : "AutoCal",
                            1 : "Mode1",
                            2 : "Mode2",
                        },
                    ))

                    ###############################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_DisableCoefficientsOverride
                    ###############################################################################
                    self.add(pr.RemoteVariable(
                        name         = 'DisableCoefficientsOverride',
                        description  = 'This API function disables the coefficient override for the selected block.',
                        offset       = 0x0C8,
                        bitSize      = 2,
                        mode         = 'WO',
                        enum         = {
                            0 : "XRFDC_CAL_BLOCK_OCB1", #define XRFDC_CAL_BLOCK_OCB1 0
                            1 : "XRFDC_CAL_BLOCK_OCB2", #define XRFDC_CAL_BLOCK_OCB2 1
                            2 : "XRFDC_CAL_BLOCK_GCB",  #define XRFDC_CAL_BLOCK_GCB  2
                            3 : "XRFDC_CAL_BLOCK_TSCB", #define XRFDC_CAL_BLOCK_TSCB 3
                        },
                    ))

                    ###############################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Calibration_Coefficients
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalCoefficients
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalCoefficients
                    ###############################################################################
                    if gen3:
                        self.addNodes(ConfigVariable,
                            name         = 'CAL_BLOCK_OCB1_Coeff', # XRFDC_CAL_BLOCK_OCB1	Offset Calibration Block (Background) (Gen 3/DFE)
                            description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                            offset       = 0x0D0,
                            bitSize      = 32,
                            mode         = 'RW',
                            number       = 8,
                            stride       = 4)

                    self.addNodes(ConfigVariable,
                        name         = 'CAL_BLOCK_OCB2_Coeff', # XRFDC_CAL_BLOCK_OCB2	Offset Calibration Block (Foreground)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x0F0,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4)

                    self.addNodes(ConfigVariable,
                        name         = 'CAL_BLOCK_GCB_Coeff', # XRFDC_CAL_BLOCK_GCB	Gain Calibration Block (Background)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x110,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4)

                    self.addNodes(ConfigVariable,
                        name         = 'CAL_BLOCK_TSCB_Coeff', # XRFDC_CAL_BLOCK_TSCB	Time Skew Calibration Block (Background)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x130,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4)

                    #######################################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Cal_Freeze_Settings
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCalFreeze
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCalFreeze
                    #######################################################################################
                    self.add(pr.RemoteVariable(
                        name         = 'CalFrozen',
                        description  = 'Status that indicates that the calibration has been frozen',
                        offset       = 0x150,
                        bitSize      = 32,
                        mode         = 'RO',
                        pollInterval = 1,
                    ))

                    self.add(ConfigVariable(
                        name         = 'DisableFreezePin',
                        description  = 'Disables the calibration freeze pin',
                        offset       = 0x154,
                        bitSize      = 32,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'FreezeCalibration',
                        description  = 'Freezes the calibration using the freeze port',
                        offset       = 0x158,
                        bitSize      = 32,
                        mode         = 'RW',
                    ))

            # Adding the Calibration device
            self.add(Calibration())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDither
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDither
        #######################################################################################
        if isAdc:
            self.add(ConfigVariable(
                name         = 'Dither',
                description  = 'This API function enables/disables the dither.',
                offset       = 0x15C,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACDataScaler
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDACDataScaler
        #######################################################################################
        if not isAdc:
            self.add(ConfigVariable(
                name         = 'DataScaler',
                description  = 'This API function enables/disables the data scaler. If the data scaler is enabled, the MSB of the datapath is reserved to prevent overflows at a cost of a slightly reduced SNR.',
                offset       = 0x160,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoupling
        #######################################################################################
        if isAdc or gen3:
            self.add(pr.RemoteVariable(
                name         = 'LinkCoupling',
                description  = 'This API function gets the Link Coupling mode for the RF-ADC or RF-DAC block. DAC coupling for Gen 1/2 devices is not available.',
                offset       = 0x164,
                bitSize      = 1,
                mode         = 'RO',
                enum         = {
                    0x0 : "XRFDC_LINK_COUPLING_DC", #define XRFDC_LINK_COUPLING_DC 0x0U
                    0x1 : "XRFDC_LINK_COUPLING_AC", #define XRFDC_LINK_COUPLING_AC 0x1U
                },
            ))

        if isAdc and gen3:
            class DSA(pr.Device):
                def __init__(self,**kwargs):
                    super().__init__(**kwargs)
                    #######################################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_DSA_Settings-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDSA-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDSA-Gen-3/DFE
                    #######################################################################################
                    self.add(ConfigVariable(
                        name         = 'DisableRTS',
                        description  = 'This disables the real time signals from setting the attenuation',
                        offset       = 0x168,
                        bitSize      = 32,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'Attenuation',
                        description  = 'The attenuation 0 - 27 dB',
                        offset       = 0x16C,
                        bitSize      = 32,
                        mode         = 'RW',
                        base         = pr.Float,
                        units        = 'dB',
                    ))

            # Adding the DSA device
            self.add(DSA())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACCompMode-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDACCompMode-Gen-3/DFE
        #######################################################################################
        if not isAdc and gen3:
            self.add(ConfigVariable(
                name         = 'DACCompMode',
                description  = 'Enable the legacy DAC output mode. Valid values are 0 (Gen 3/DFE behavior) 1 (Gen 2 behavior).',
                offset       = 0x174,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))


        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDataPathMode-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataPathMode-Gen-3/DFE
        #######################################################################################
        if not isAdc and gen3:
            self.add(ConfigVariable(
                name         = 'DataPathMode',
                description  = 'The data path mode. Valid values are 1-4.',
                offset       = 0x178,
                bitSize      = 3,
                mode         = 'RW',
                enum         = {
                    0x0 : "UNDEFINED",
                    0x1 : "XRFDC_DATAPATH_MODE_DUC_0_FSDIVTWO",     #define XRFDC_DATAPATH_MODE_DUC_0_FSDIVTWO 1U     = Full Bandwidth FS 7GSPS (First Nyquist zone)
                    0x2 : "XRFDC_DATAPATH_MODE_DUC_0_FSDIVFOUR",    #define XRFDC_DATAPATH_MODE_DUC_0_FSDIVFOUR 2U    = Half Bandwidth, Low Pass IMR, FS 10GSPS (Second Nyquist zone)
                    0x3 : "XRFDC_DATAPATH_MODE_FSDIVFOUR_FSDIVTWO", #define XRFDC_DATAPATH_MODE_FSDIVFOUR_FSDIVTWO 3U = Half Bandwidth, High Pass IMR, FS 10GSPS (First Nyquist zone)
                    0x4 : "XRFDC_DATAPATH_MODE_NODUC_0_FSDIVTWO",   #define XRFDC_DATAPATH_MODE_NODUC_0_FSDIVTWO 4U   = Full Bandwidth, Bypass Datapath
                },
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDataPathMode-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataPathMode-Gen-3/DFE
        #######################################################################################
        if not isAdc and gen3:
            self.add(ConfigVariable(
                name         = 'IMRPassMode',
                description  = 'The IMR Filter mode. Valid values are 0 (for low pass) 1 (for high pass)',
                offset       = 0x17C,
                bitSize      = 1,
                mode         = 'RW',
                enum         = {
                    0x0 : "XRFDC_DAC_IMR_MODE_LOWPASS",  #define XRFDC_DAC_IMR_MODE_LOWPASS 0U
                    0x1 : "XRFDC_DAC_IMR_MODE_HIGHPASS", #define XRFDC_DAC_IMR_MODE_HIGHPASS 1U
                },
            ))

        if isAdc and gen3:
            class SignalDetector(pr.Device):
                def __init__(self,**kwargs):
                    super().__init__(**kwargs)
                    #######################################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Signal_Detector_Settings-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetSignalDetector-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetSignalDetector-Gen-3/DFE
                    #######################################################################################

                    self.add(ConfigVariable(
                        name         = 'Mode',
                        description  = 'Whether to use Average or Randomized mode.',
                        offset       = 0x180,
                        bitSize      = 1,
                        mode         = 'RW',
                        enum         = {
                            0x0 : "XRFDC_SIGDET_MODE_AVG",  #define XRFDC_SIGDET_MODE_AVG 0U
                            0x1 : "XRFDC_SIGDET_MODE_RNDM", #define XRFDC_SIGDET_MODE_RNDM 1U
                        },
                    ))

                    self.add(ConfigVariable(
                        name         = 'TimeConstant',
                        description  = 'Time constant of the leaky integrator.',
                        offset       = 0x184,
                        bitSize      = 3,
                        mode         = 'RW',
                        enum         = {
                            0 : "XRFDC_SIGDET_TC_2_0",  #define XRFDC_SIGDET_TC_2_0 0   = 2^0 Cycles
                            1 : "XRFDC_SIGDET_TC_2_2",  #define XRFDC_SIGDET_TC_2_2 1   = 2^2 Cycles
                            2 : "XRFDC_SIGDET_TC_2_4",  #define XRFDC_SIGDET_TC_2_4 2   = 2^4 Cycles
                            3 : "XRFDC_SIGDET_TC_2_8",  #define XRFDC_SIGDET_TC_2_8 3   = 2^8 Cycles
                            4 : "XRFDC_SIGDET_TC_2_12", #define XRFDC_SIGDET_TC_2_12 4  = 2^12 Cycles
                            5 : "XRFDC_SIGDET_TC_2_14", #define XRFDC_SIGDET_TC_2_14 5  = 2^14 Cycles
                            6 : "XRFDC_SIGDET_TC_2_16", #define XRFDC_SIGDET_TC_2_16 6  = 2^16 Cycles
                            7 : "XRFDC_SIGDET_TC_2_18", #define XRFDC_SIGDET_TC_2_18 7  = 2^18 Cycles
                        },
                    ))

                    self.add(ConfigVariable(
                        name         = 'Flush',
                        description  = 'Flush the leaky integrator.',
                        offset       = 0x188,
                        bitSize      = 1,
                        mode         = 'RW',
                        base         = pr.Bool,
                    ))

                    self.add(ConfigVariable(
                        name         = 'EnableIntegrator',
                        description  = 'Enable the leaky integrator.',
                        offset       = 0x18C,
                        bitSize      = 1,
                        mode         = 'RW',
                        base         = pr.Bool,
                    ))

                    self.add(ConfigVariable(
                        name         = 'Threshold',
                        description  = 'The threshold for signal detection.',
                        offset       = 0x190,
                        bitSize      = 16,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'ThresholdOnTriggerCnt',
                        description  = 'The number of times value must exceed Threshold before turning on.',
                        offset       = 0x194,
                        bitSize      = 16,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'ThresholdOffTriggerCnt',
                        description  = 'The number of times value must exceed Threshold before turning off.',
                        offset       = 0x198,
                        bitSize      = 16,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'HysteresisEnable',
                        description  = 'Enable hysteresis on signal on.',
                        offset       = 0x19C,
                        bitSize      = 1,
                        mode         = 'RW',
                        base         = pr.Bool,
                    ))

            # Adding the SignalDetector device
            self.add(SignalDetector())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetInternalFIFOWidth-Gen-3/DFE
        #######################################################################################
        if gen3:
            self.add(pr.RemoteCommand(
                name         = 'ResetInternalFIFOWidth',
                description  = 'This API function resets the internal FIFO width to conform with rate change and mixer settings for the RF-ADC/RF-DAC.',
                offset       = 0x1A0,
                bitSize      = 1,
                function     = lambda cmd: cmd.set(1),
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetInternalFIFOWidthObs-Gen-3/DFE
        #######################################################################################
        if isAdc and gen3:
            self.add(pr.RemoteCommand(
                name         = 'ResetInternalFIFOWidthObs',
                description  = 'This API function resets the internal observation FIFO width to conform with rate change and mixer settings for the RF-ADC.',
                offset       = 0x1A4,
                bitSize      = 1,
                function     = lambda cmd: cmd.set(1),
                hidden       = True,
            ))

        if gen3:
            class PwrModeSettings(pr.Device):
                def __init__(self,**kwargs):
                    super().__init__(**kwargs)
                    #######################################################################################
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Pwr_Mode_Settings-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetPwrMode-Gen-3/DFE
                    # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPwrMode-Gen-3/DFE
                    #######################################################################################
                    self.add(ConfigVariable(
                        name         = 'DisableIPControl',
                        description  = 'This disables the real time signals from setting the power mode: 0 to leave RTS control enabled, 1 to disable RTS control.',
                        offset       = 0x1A8,
                        bitSize      = 1,
                        mode         = 'RW',
                    ))

                    self.add(ConfigVariable(
                        name         = 'PwrMode',
                        description  = '0 to power down, 1 to power up.',
                        offset       = 0x1AC,
                        bitSize      = 1,
                        mode         = 'RW',
                    ))

            # Adding the DSA device
            self.add(PwrModeSettings())

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_Get_BlockBaseAddr
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'BlockBaseAddr',
            description  = 'base address of the block',
            offset       = 0x1B0,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataType
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'DataType',
            description  = 'If the data type is real, the function returns 0; otherwise, it returns 1.',
            offset       = 0x1B4,
            bitSize      = 1,
            mode         = 'RO',
            enum         = {
                0x0 : "XRFDC_DATA_TYPE_REAL", #define XRFDC_DATA_TYPE_REAL 0x00000000U
                0x1 : "XRFDC_DATA_TYPE_IQ",   #define XRFDC_DATA_TYPE_IQ 0x00000001U
            },
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDataWidth
        #######################################################################################
        #######################################################################################
        # The reason why FabClkFreq variable is commented out is because it always returns 0.0
        # It was returns zeros because ADCTile_Config[Tile_Id].ADCBlock_Digital_Config[Block_Id].DataWidth
        # and DACTile_Config[Tile_Id].DACBlock_Digital_Config[Block_Id].DataWidth is never set by the driver
        #######################################################################################
#        self.add(pr.RemoteVariable(
#            name         = 'DataWidth',
#            description  = 'Returns the data width for the RF-ADC or RF-DAC',
#            offset       = 0x1B8,
#            bitSize      = 32,
#            mode         = 'RO',
#        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInverseSincFilter
        #######################################################################################
        if not isAdc:
            self.add(pr.RemoteVariable(
                name         = 'InverseSincFilter',
                description  = 'the inverse sinc filter is enabled for the RF-DAC, the function returns 1; otherwise, it returns 0.',
                offset       = 0x1BC,
                bitSize      = 1,
                mode         = 'RO',
                base         = pr.Bool,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixedMode
        #######################################################################################
        if not isAdc:
            self.add(pr.RemoteVariable(
                name         = 'MixedMode',
                description  = 'the mixed mode setting for the RF-DAC',
                offset       = 0x1C0,
                bitSize      = 32,
                mode         = 'RO',
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsFifoEnabled
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IsFifoEnabled',
            description  = 'If the FIFO is enabled, the function returns 1; otherwise, it returns 0',
            offset       = 0x1C4,
            bitSize      = 1,
            mode         = 'RO',
            base         = pr.Bool,
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetConnectedIData
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ConnectedIData',
            description  = 'Get converter connected for I digital data path.',
            offset       = 0x1C8,
            bitSize      = 32,
            mode         = 'RO',
            base         = pr.Int, # s32
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetConnectedQData
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'ConnectedQData',
            description  = 'Get converter connected for Q digital data path.',
            offset       = 0x1CC,
            bitSize      = 32,
            mode         = 'RO',
            base         = pr.Int, # s32
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsADCDigitalPathEnabled
        #######################################################################################
        if isAdc:
            self.add(pr.RemoteVariable(
                name         = 'IsADCDigitalPathEnabled',
                description  = 'This API checks whether ADC Digital path is enabled or disabled.',
                offset       = 0x1D0,
                bitSize      = 1,
                mode         = 'RO',
                base         = pr.Bool,
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IsDACDigitalPathEnabled
        #######################################################################################
        if not isAdc:
            self.add(pr.RemoteVariable(
                name         = 'IsDACDigitalPathEnabled',
                description  = 'This API checks whether RF-DAC digital path is enabled or not.',
                offset       = 0x1D4,
                bitSize      = 1,
                mode         = 'RO',
                base         = pr.Bool,
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_CheckDigitalPathEnabled
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'CheckDigitalPathEnabled',
            description  = 'This API checks whether RF-ADC/RF-DAC digital path is enabled or not.',
            offset       = 0x1D8,
            bitSize      = 1,
            mode         = 'RO',
            base         = pr.Bool,
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrEnable
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Interrupt-Handling?tocId=oje7sI2HBnZDvjFdAqF5gg
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IntrEnable',
            description  = 'Enable interrupts according to mask',
            offset       = 0x1DC,
            bitSize      = 32,
            mode         = 'WO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrDisable
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Interrupt-Handling?tocId=oje7sI2HBnZDvjFdAqF5gg
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IntrDisable',
            description  = 'Disable interrupts according to mask',
            offset       = 0x1E0,
            bitSize      = 32,
            mode         = 'WO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_IntrClr
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Interrupt-Handling?tocId=oje7sI2HBnZDvjFdAqF5gg
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'IntrClr',
            description  = 'Clear interrupts according to mask',
            offset       = 0x1E4,
            bitSize      = 32,
            mode         = 'WO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIntrStatus
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Interrupt-Handling?tocId=oje7sI2HBnZDvjFdAqF5gg
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'GetIntrStatus',
            description  = 'Get interrupt status, some may be cleared on read',
            offset       = 0x1E8,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetEnabledInterrupts
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/Interrupt-Handling?tocId=oje7sI2HBnZDvjFdAqF5gg
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'GetEnabledInterrupts',
            description  = 'Get enabled interrupts',
            offset       = 0x1EC,
            bitSize      = 32,
            mode         = 'RO',
            hidden       = True,
        ))
