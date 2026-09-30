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

import surf.protocols.i2c

class PMbus(pr.Device):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        literalDataFormat = surf.protocols.i2c.getPMbusLiteralDataFormat

        # ---------------------------------------------------------------------
        # Raw PMBus registers (hidden)
        # ---------------------------------------------------------------------
        self.add(pr.RemoteVariable(
            name         = 'READ_IOUT',
            description  = 'Raw total output current measurement (LINEAR11)',
            offset       = (4*0x8C),
            bitSize      = 16,
            mode         = 'RO',
            hidden       = True,
            pollInterval = 5,
        ))

        self.add(pr.RemoteVariable(
            name         = 'READ_TEMPERATURE_1',
            description  = 'Raw controller temperature measurement (LINEAR11)',
            offset       = (4*0x8D),
            bitSize      = 16,
            mode         = 'RO',
            hidden       = True,
            pollInterval = 5,
        ))

        # ---------------------------------------------------------------------
        # Linked variables (real-world converted measurements)
        # ---------------------------------------------------------------------
        self.add(pr.LinkVariable(
            name         = 'IOUT',
            description  = 'Total output current measurement',
            mode         = 'RO',
            units        = 'A',
            disp         = '{:1.3f}',
            linkedGet    = literalDataFormat,
            dependencies = [self.READ_IOUT],
        ))

        self.add(pr.LinkVariable(
            name         = 'TEMPERATURE[1]',
            description  = 'Controller temperature sensor measurement',
            mode         = 'RO',
            units        = 'degC',
            disp         = '{:1.3f}',
            linkedGet    = literalDataFormat,
            dependencies = [self.READ_TEMPERATURE_1],
        ))
