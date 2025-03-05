#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter block
#-----------------------------------------------------------------------------
# Description: Complementary mapping to class RfdcApi(pyrogue.interfaces.OsCommandMemorySlave)
#              located in submodule/axi-soc-ultra-plus-core/petalinux-apps/roguetcpbridge/files/roguetcpbridge
#-----------------------------------------------------------------------------
# This file is part of the 'SLAC Firmware Standard Library'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'SLAC Firmware Standard Library', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------

import pyrogue as pr
import axi_soc_ultra_plus_core.rfsoc_utility as rfsoc_utility

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
                    name         = 'SamplingFreq',
                    description  = 'Sampling frequency',
                    offset       = 0x000,
                    bitSize      = 64,
                    mode         = 'RO',
                    base         = pr.Double,
                    hidden       = True,
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
                    enum         = rfsoc_utility.enumMixedMode if isAdc else None,
                ))

                if not isAdc:

                    self.add(pr.RemoteVariable(
                        name         = 'MixerMode',
                        offset       = 0x008,
                        bitSize      = 4,
                        bitOffset    = 20,
                        mode         = 'RO',
                        enum         = rfsoc_utility.enumMixedMode,
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
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetMixerSettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixerSettings
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'Freq',
                    description  = 'NCO frequency. Range: -Fs to Fs (MHz)',
                    offset       = 0x020,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'MHz',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PhaseOffset',
                    description  = 'NCO phase offset. Range: -180 to 180 (Exclusive)',
                    offset       = 0x028,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'degree',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'EventSource',
                    description  = 'Event source for mixer settings. XRFDC_EVNT_SRC_* represents valid values.',
                    offset       = 0x030,
                    bitSize      = 3,
                    mode         = 'RW',
                    enum         = rfsoc_utility.enumEventSource,
                ))

                self.add(pr.RemoteVariable(
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

                self.add(pr.RemoteVariable(
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
                    },
                ))

                self.add(pr.RemoteVariable(
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
                    },
                ))

                self.add(pr.RemoteVariable(
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
                    description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
                    offset       = 0x03C,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.post(1),
                ))

        self.add(pr.LinkVariable(
            name         = 'IsMixerEnabled',
            mode         = 'RO',
            linkedGet    = lambda read: (self.BlockStatus.MixerMode.get(read=read) != 0) and (self.BlockStatus.SamplingFreq.get(read=read) > 0.0),
            dependencies = [self.BlockStatus.MixerMode, self.BlockStatus.SamplingFreq],
        ))

        # Adding the Mixer device
        self.add(Mixer(enableDeps=[self.IsMixerEnabled]))

        class QMC(pr.Device):
            def __init__(self,**kwargs):
                super().__init__(**kwargs)
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetQMCSettings
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetQMCSettings
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'EnablePhase',
                    description  = 'Indicates if phase is enabled (1) or disabled (0)',
                    offset       = 0x040,
                    bitSize      = 1,
                    bitOffset    = 0,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'EnableGain',
                    description  = 'Indicates if gain is enabled(1) or disabled (0)',
                    offset       = 0x040,
                    bitSize      = 1,
                    bitOffset    = 1,
                    mode         = 'RW',
                    base         = pr.Bool,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'EventSource',
                    description  = 'Event source for QMC settings. XRFDC_EVNT_SRC_* represents valid values',
                    offset       = 0x044,
                    bitSize      = 3,
                    mode         = 'RW',
                    enum         = rfsoc_utility.enumEventSource,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'GainCorrectionFactor',
                    description  = 'Gain correction factor. Range: 0 to 2.0 (Exclusive).',
                    offset       = 0x048,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                ))

                self.add(pr.RemoteVariable(
                    name         = 'PhaseCorrectionFactor',
                    description  = 'Phase correction factor. Range: +/- 26.5 degrees (Exclusive)',
                    offset       = 0x050,
                    bitSize      = 64,
                    mode         = 'RW',
                    base         = pr.Double,
                    units        = 'degree',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'OffsetCorrectionFactor',
                    description  = 'Offset correction factor is adding a fixed LSB value to the sampled signal',
                    offset       = 0x058,
                    bitSize      = 32,
                    mode         = 'RW',
                    base         = pr.Int, # s32
                ))

                self.add(pr.RemoteCommand(
                    name         = 'UpdateEvent',
                    description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
                    offset       = 0x05C,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.post(1),
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
                self.add(pr.RemoteVariable(
                    name         = 'CoarseDelay',
                    description  = 'Coarse delay in the number of samples. Range: 0 to 7 for Gen 1/Gen 2 devices and 0 to 40 for Gen 3/DFE devices',
                    offset       = 0x060,
                    bitSize      = 8,
                    bitOffset    = 0,
                    maximum      = 40 if gen3 else 7,
                    mode         = 'RW',
                ))

                self.add(pr.RemoteVariable(
                    name         = 'EventSource',
                    description  = 'Event source for coarse delay settings. XRFDC_EVNT_SRC_* represents valid values',
                    offset       = 0x060,
                    bitSize      = 3,
                    bitOffset    = 8,
                    mode         = 'RW',
                    enum         = rfsoc_utility.enumEventSource,
                ))

                self.add(pr.RemoteCommand(
                    name         = 'UpdateEvent',
                    description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
                    offset       = 0x064,
                    bitSize      = 1,
                    function     = lambda cmd: cmd.post(1),
                ))

        # Adding the CoarseDelay device
        self.add(CoarseDelay())

        if not isAdc: # isAdc = false (DAC)
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetInterpolationFactor
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetInterpolationFactor
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'DecimationFactor',
                description  = 'This API function sets the interpolation factor for the requested RF-DAC and also updates the FIFO read width based on the interpolation factor',
                offset       = 0x068,
                bitSize      = 6,
                mode         = 'RW',
                enum         = rfsoc_utility.enumInterpDecim,
            ))

        else: # isAdc = true (ADC)
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactor
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactor
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'DecimationFactor',
                description  = 'This API function sets the decimation factor for the requested RF-ADC and also updates the FIFO write width based on the decimation factor',
                offset       = 0x070,
                bitSize      = 6,
                mode         = 'RW',
                enum         = rfsoc_utility.enumInterpDecim,
            ))

            if gen3:
                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDecimationFactorObs-Gen-3/DFE
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDecimationFactorObs-Gen-3/DFE
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'DecimationFactorObs',
                    description  = 'This API function sets the decimation factor for the observation channel of the requested RF-ADC and also updates the FIFO write width based on the decimation factor',
                    offset       = 0x074,
                    bitSize      = 6,
                    mode         = 'RW',
                    enum         = rfsoc_utility.enumInterpDecim,
                ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabWrVldWords
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWords
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'FabWrVldWords',
            description  = 'This API function sets the write fabric data rate for the requested RF-DAC by writing to the corresponding register',
            offset       = 0x078,
            bitSize      = 32,
            mode         = 'RO' if isAdc else 'RW',
            hidden       = True,
        ))

        if gen3:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabWrVldWordsObs-Gen-3/DFE
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'FabWrVldWordsObs',
                description  = 'Write PL data rate for the observation channel of the requested RF-ADCis returned back to the caller',
                offset       = 0x07C,
                bitSize      = 32,
                mode         = 'RO',
                hidden       = True,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetFabRdVldWords
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetFabRdVldWords
        #######################################################################################
        self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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
                    enum         = rfsoc_utility.enumUpdateThreshold,
                ))

                #######################################################################################
                # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdClrMode
                #######################################################################################
                self.add(pr.RemoteVariable(
                    name         = 'ThresholdClrMode_ThresholdToUpdate',
                    description  = 'This API function sets the threshold clear mode',
                    offset       = 0x08C,
                    bitSize      = 3,
                    bitOffset    = 0,
                    mode         = 'WO',
                    enum         = rfsoc_utility.enumUpdateThreshold,
                ))

                self.add(pr.RemoteVariable(
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
                    self.add(pr.RemoteVariable(
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
                    self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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
            function     = lambda cmd: cmd.post(1),
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
            self.add(pr.RemoteVariable(
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
        self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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
                    self.add(pr.RemoteVariable(
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
                        self.addRemoteVariables(
                            name         = 'CAL_BLOCK_OCB1_Coeff', # XRFDC_CAL_BLOCK_OCB1	Offset Calibration Block (Background) (Gen 3/DFE)
                            description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                            offset       = 0x0D0,
                            bitSize      = 32,
                            mode         = 'RW',
                            number       = 8,
                            stride       = 4,
                            hidden       = True,
                        )

                    self.addRemoteVariables(
                        name         = 'CAL_BLOCK_OCB2_Coeff', # XRFDC_CAL_BLOCK_OCB2	Offset Calibration Block (Foreground)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x0F0,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4,
                        hidden       = True,
                    )

                    self.addRemoteVariables(
                        name         = 'CAL_BLOCK_GCB_Coeff', # XRFDC_CAL_BLOCK_GCB	Gain Calibration Block (Background)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x110,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4,
                        hidden       = True,
                    )

                    self.addRemoteVariables(
                        name         = 'CAL_BLOCK_TSCB_Coeff', # XRFDC_CAL_BLOCK_TSCB	Time Skew Calibration Block (Background)
                        description  = 'This API function enables the coefficient override and programs the provided coefficients for the selected block.',
                        offset       = 0x130,
                        bitSize      = 32,
                        mode         = 'RW',
                        number       = 8,
                        stride       = 4,
                        hidden       = True,
                    )

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

                    self.add(pr.RemoteVariable(
                        name         = 'DisableFreezePin',
                        description  = 'Disables the calibration freeze pin',
                        offset       = 0x154,
                        bitSize      = 32,
                        mode         = 'RW',
                    ))

                    self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
                name         = 'DataScaler',
                description  = 'This API function enables/disables the data scaler. If the data scaler is enabled, the MSB of the datapath is reserved to prevent overflows at a cost of a slightly reduced SNR.',
                offset       = 0x160,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetLinkCoupling (XRFdc_GetLinkCoupling API Scheduled for deprication in 2024.1)
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

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_DSA_Settings-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDSA-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDSA-Gen-3/DFE
        #######################################################################################
        if isAdc and gen3:
            self.add(pr.RemoteVariable(
                name         = 'DSA_DisableRTS',
                description  = 'This disables the real time signals from setting the attenuation',
                offset       = 0x168,
                bitSize      = 32,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'DSA_Attenuation',
                description  = 'The attenuation 0 - 27 dB',
                offset       = 0x16C,
                bitSize      = 32,
                mode         = 'RW',
                base         = pr.Float,
                units        = 'dB',
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetDACCompMode-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetDACCompMode-Gen-3/DFE
        #######################################################################################
        if not isAdc and gen3:
            self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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
            self.add(pr.RemoteVariable(
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

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Signal_Detector_Settings-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetSignalDetector-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetSignalDetector-Gen-3/DFE
        #######################################################################################
        if isAdc and gen3:
            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_Mode',
                description  = 'Whether to use Average or Randomized mode.',
                offset       = 0x180,
                bitSize      = 1,
                mode         = 'RW',
                enum         = {
                    0x0 : "XRFDC_SIGDET_MODE_AVG",  #define XRFDC_SIGDET_MODE_AVG 0U
                    0x1 : "XRFDC_SIGDET_MODE_RNDM", #define XRFDC_SIGDET_MODE_RNDM 1U
                },
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_TimeConstant',
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

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_Flush',
                description  = 'Flush the leaky integrator.',
                offset       = 0x188,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_EnableIntegrator',
                description  = 'Enable the leaky integrator.',
                offset       = 0x18C,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_Threshold',
                description  = 'The threshold for signal detection.',
                offset       = 0x190,
                bitSize      = 16,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_ThresholdOnTriggerCnt',
                description  = 'The number of times value must exceed Threshold before turning on.',
                offset       = 0x194,
                bitSize      = 16,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_ThresholdOffTriggerCnt',
                description  = 'The number of times value must exceed Threshold before turning off.',
                offset       = 0x198,
                bitSize      = 16,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'SignalDetector_HysteresisEnable',
                description  = 'Enable hysteresis on signal on.',
                offset       = 0x19C,
                bitSize      = 1,
                mode         = 'RW',
                base         = pr.Bool,
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_ResetInternalFIFOWidth-Gen-3/DFE
        #######################################################################################
        if gen3:
            self.add(pr.RemoteCommand(
                name         = 'ResetInternalFIFOWidth',
                description  = 'This API function resets the internal FIFO width to conform with rate change and mixer settings for the RF-ADC/RF-DAC.',
                offset       = 0x1A0,
                bitSize      = 1,
                function     = lambda cmd: cmd.post(1),
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
                function     = lambda cmd: cmd.post(1),
            ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Pwr_Mode_Settings-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetPwrMode-Gen-3/DFE
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetPwrMode-Gen-3/DFE
        #######################################################################################
        if gen3:
            self.add(pr.RemoteVariable(
                name         = 'PwrModeSettings_DisableIPControl',
                description  = 'This disables the real time signals from setting the power mode: 0 to leave RTS control enabled, 1 to disable RTS control.',
                offset       = 0x1A8,
                bitSize      = 1,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'PwrModeSettings_PwrMode',
                description  = '0 to power down, 1 to power up.',
                offset       = 0x1AC,
                bitSize      = 1,
                mode         = 'RW',
            ))

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
