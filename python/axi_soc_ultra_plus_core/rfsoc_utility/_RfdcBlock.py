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

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_TileStatus
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetIPStatus
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_SamplingFreq',
            description  = 'Sampling frequency',
            offset       = 0x000,
            bitSize      = 64,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Double,
        ))

        if isAdc:
            self.add(pr.RemoteVariable(
                name         = 'BlockStatus_Enabled',
                description  = 'Converter enable/disable.',
                offset       = 0x008,
                bitSize      = 1,
                bitOffset    = 0,
                mode         = 'RO',
                pollInterval = 1,
                base         = pr.Bool,
            ))
        else:
            self.add(pr.RemoteVariable(
                name         = 'BlockStatus_InvSincEnabled',
                description  = 'Inverse sinc enable/disable',
                offset       = 0x008,
                bitSize      = 4,
                bitOffset    = 0,
                mode         = 'RO',
                pollInterval = 1,
            ))

            self.add(pr.RemoteVariable(
                name         = 'BlockStatus_DecoderMode',
                offset       = 0x008,
                bitSize      = 4,
                bitOffset    = 4,
                mode         = 'RO',
                pollInterval = 1,
            ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_FIFOStatus',
            offset       = 0x008,
            bitSize      = 4,
            bitOffset    = 8,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_DecimationFactor' if isAdc else 'BlockStatus_InterpolationFactor',
            offset       = 0x008,
            bitSize      = 4,
            bitOffset    = 12,
            mode         = 'RO',
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_MixerMode' if isAdc else 'BlockStatus_AdderStatus',
            offset       = 0x008,
            bitSize      = 4,
            bitOffset    = 16,
            mode         = 'RO',
            pollInterval = 1,
        ))

        if not isAdc:

            self.add(pr.RemoteVariable(
                name         = 'BlockStatus_MixerMode',
                offset       = 0x008,
                bitSize      = 4,
                bitOffset    = 20,
                mode         = 'RO',
                pollInterval = 1,
            ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_DataPathClocksStatus',
            description  = 'Indicates if all required datapath clocks are enabled; 1 if all clocks enabled, 0 otherwise',
            offset       = 0x008,
            bitSize      = 1,
            bitOffset    = 24,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_IsFIFOFlagsEnabled',
            description  = 'FIFO flags enabled mask; 1 is enabled, otherwise 0.',
            offset       = 0x008,
            bitSize      = 1,
            bitOffset    = 25,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'BlockStatus_IsFIFOFlagsAsserted',
            description  = 'FIFO flags asserted mask; 1 is enabled, otherwise 0',
            offset       = 0x008,
            bitSize      = 1,
            bitOffset    = 26,
            mode         = 'RO',
            pollInterval = 1,
            base         = pr.Bool,
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Mixer_Settings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetMixerSettings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetMixerSettings
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'Mixer_Freq',
            description  = 'NCO frequency. Range: -Fs to Fs (MHz)',
            offset       = 0x020,
            bitSize      = 64,
            mode         = 'RW',
            pollInterval = 1,
            base         = pr.Double,
            units        = 'MHz',
        ))

        self.add(pr.RemoteVariable(
            name         = 'Mixer_PhaseOffset',
            description  = 'NCO phase offset. Range: -180 to 180 (Exclusive)',
            offset       = 0x028,
            bitSize      = 64,
            mode         = 'RW',
            pollInterval = 1,
            base         = pr.Double,
            units        = 'degree',
        ))

        self.add(pr.RemoteVariable(
            name         = 'Mixer_EventSource',
            description  = 'Event source for mixer settings. XRFDC_EVNT_SRC_* represents valid values.',
            offset       = 0x030,
            bitSize      = 3,
            mode         = 'RW',
            enum         = rfsoc_utility.enumEventSource,
        ))

        self.add(pr.RemoteVariable(
            name         = 'Mixer_CoarseMixFreq',
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
            name         = 'Mixer_MixerMode',
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
            name         = 'Mixer_FineMixerScale',
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
            name         = 'Mixer_MixerType',
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
            name         = 'Mixer_UpdateEvent',
            description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
            offset       = 0x03C,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_QMC_Settings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetQMCSettings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetQMCSettings
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_EnablePhase',
            description  = 'Indicates if phase is enabled (1) or disabled (0)',
            offset       = 0x040,
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RW',
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_EnableGain',
            description  = 'Indicates if gain is enabled(1) or disabled (0)',
            offset       = 0x040,
            bitSize      = 1,
            bitOffset    = 1,
            mode         = 'RW',
            base         = pr.Bool,
        ))

        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_EventSource',
            description  = 'Event source for QMC settings. XRFDC_EVNT_SRC_* represents valid values',
            offset       = 0x044,
            bitSize      = 3,
            mode         = 'RW',
            enum         = rfsoc_utility.enumEventSource,
        ))

        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_GainCorrectionFactor',
            description  = 'Gain correction factor. Range: 0 to 2.0 (Exclusive).',
            offset       = 0x048,
            bitSize      = 64,
            mode         = 'RW',
            pollInterval = 1,
            base         = pr.Double,
        ))

        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_PhaseCorrectionFactor',
            description  = 'Phase correction factor. Range: +/- 26.5 degrees (Exclusive)',
            offset       = 0x050,
            bitSize      = 64,
            mode         = 'RW',
            pollInterval = 1,
            base         = pr.Double,
            units        = 'degree',
        ))

        self.add(pr.RemoteVariable(
            name         = 'QMCSettings_OffsetCorrectionFactor',
            description  = 'Offset correction factor is adding a fixed LSB value to the sampled signal',
            offset       = 0x058,
            bitSize      = 32,
            mode         = 'RW',
            base         = pr.Int, # s32
        ))

        self.add(pr.RemoteCommand(
            name         = 'QMCSettings_UpdateEvent',
            description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
            offset       = 0x05C,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_CoarseDelay_Settings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetCoarseDelaySettings
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetCoarseDelaySettings
        #######################################################################################
        self.add(pr.RemoteVariable(
            name         = 'CoarseDelaySettings_CoarseDelay',
            description  = 'Coarse delay in the number of samples. Range: 0 to 7 for Gen 1/Gen 2 devices and 0 to 40 for Gen 3/DFE devices',
            offset       = 0x060,
            bitSize      = 8,
            bitOffset    = 0,
            maximum      = 40 if gen3 else 7,
            mode         = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name         = 'CoarseDelaySettings_EventSource',
            description  = 'Event source for coarse delay settings. XRFDC_EVNT_SRC_* represents valid values',
            offset       = 0x060,
            bitSize      = 3,
            bitOffset    = 8,
            mode         = 'RW',
            enum         = rfsoc_utility.enumEventSource,
        ))

        self.add(pr.RemoteCommand(
            name         = 'CoarseDelaySettings_UpdateEvent',
            description  = 'Use this function to trigger the update event for an event if the event source is Slice or Tile',
            offset       = 0x064,
            bitSize      = 1,
            function     = lambda cmd: cmd.post(1),
        ))

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
            ))

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
        if isAdc:

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

        #######################################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetThresholdClrMode
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

        if not isAdc:
            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetOutputCurr
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'OutputCurr',
                offset       = 0x0B8,
                bitSize      = 32,
                mode         = 'RO',
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
                mode         = 'RW',
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
        if isAdc:

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
        if isAdc:
            self.add(pr.RemoteVariable(
                name         = 'CalFreeze_CalFrozen',
                description  = 'Status that indicates that the calibration has been frozen',
                offset       = 0x150,
                bitSize      = 32,
                mode         = 'RO',
                pollInterval = 1,
            ))

            self.add(pr.RemoteVariable(
                name         = 'CalFreeze_DisableFreezePin',
                description  = 'Disables the calibration freeze pin',
                offset       = 0x154,
                bitSize      = 32,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'CalFreeze_FreezeCalibration',
                description  = 'Freezes the calibration using the freeze port',
                offset       = 0x158,
                bitSize      = 32,
                mode         = 'RW',
            ))

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
