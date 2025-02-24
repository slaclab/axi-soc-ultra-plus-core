#-----------------------------------------------------------------------------
# Title      : Xilinx RFSoC RF data converter block
#-----------------------------------------------------------------------------
# Description:
# Xilinx RFSoC RF data converter block
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

class RfBlock(pr.Device):
    def __init__(
            self,
            gen3        = True,  # True if using RFSoC GEN3 Hardware
            isAdc       = False, # True if this is an ADC tile
            description = 'RFSoC data converter block registers',
            **kwargs):
        super().__init__(description=description, **kwargs)

        ###########################################################################
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_SetNyquistZone
        # https://docs.amd.com/r/en-US/pg269-rf-data-converter/XRFdc_GetNyquistZone
        ###########################################################################
        self.add(pr.RemoteVariable(
            name         = 'NyquistZone',
            description  = 'Method to execute the RFSoC PS rfdc-NyquistZone executable remotely',
            offset       = 0x00,
            bitSize      = 2,
            mode         = 'RW',
            enum         = {
                0 : "Undefined",
                1 : "Odd",
                2 : "Even",
                3 : "ERROR",
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
                offset       = 0x04,
                bitSize      = 2,
                # mode         = 'RW', "RFDC CalibrationMode failed" when we try to change configurations
                mode         = 'RO',
                enum         = {
                    0 : "AutoCal",
                    1 : "Mode1",
                    2 : "Mode2",
                    3 : "ERROR",
                },
            ))

            #######################################################################################
            # https://docs.amd.com/r/en-US/pg269-rf-data-converter/struct-XRFdc_Cal_Freeze_Settings
            #######################################################################################
            self.add(pr.RemoteVariable(
                name         = 'CalFrozen',
                description  = 'Status that indicates that the calibration has been frozen',
                offset       = 0x08,
                bitSize      = 32,
                mode         = 'RO',
            ))

            self.add(pr.RemoteVariable(
                name         = 'DisableFreezePin',
                description  = 'Disables the calibration freeze pin',
                offset       = 0x0C,
                bitSize      = 32,
                mode         = 'RW',
            ))

            self.add(pr.RemoteVariable(
                name         = 'FreezeCalibration',
                description  = 'Freezes the calibration using the freeze port',
                offset       = 0x10,
                bitSize      = 32,
                mode         = 'RW',
            ))
