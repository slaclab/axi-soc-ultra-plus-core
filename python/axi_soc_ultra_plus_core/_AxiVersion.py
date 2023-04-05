#-----------------------------------------------------------------------------
# This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
# the license terms in the LICENSE.txt file found in the top-level directory
# of this distribution and at:
#    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
# No part of the 'axi-soc-ultra-plus-core', including this file, may be
# copied, modified, propagated, or distributed except according to the terms
# contained in the LICENSE.txt file.
#-----------------------------------------------------------------------------
import pyrogue       as pr
import surf.axi      as axi

class AxiVersion(axi.AxiVersion):
    def __init__(self,
            name             = 'AxiVersion',
            description      = 'AXI-Lite Version Module',
            numUserConstants = 0,
            **kwargs):
        super().__init__(
            name        = name,
            description = description,
            **kwargs
        )

        self.add(pr.RemoteVariable(
            name         = 'DMA_SIZE_G',
            description  = 'DMA_SIZE_G VHDL generic value',
            offset       = 0x400+(4*0),
            bitSize      = 32,
            mode         = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name         = 'DMA_CLK_FREQ_C',
            description  = 'DMA_CLK_FREQ_C VHDL generic value',
            offset       = 0x400+(4*1),
            bitSize      = 32,
            mode         = 'RO',
            disp         = '{:d}',
            units        = 'Hz',
        ))

        self.add(pr.RemoteVariable(
            name         = 'AppReset',
            description  = 'Application Reset Status',
            offset       = 0x400+(4*2),
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RO',
            base         = pr.Bool,
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'AppClkFreq',
            description  = 'Application Clock Frequency',
            offset       = 0x400+(4*3),
            units        = 'Hz',
            disp         = '{:d}',
            mode         = 'RO',
            pollInterval = 1
        ))

        self.add(pr.RemoteVariable(
            name         = 'DspClkFreq',
            description  = 'DSP Clock Frequency',
            offset       = 0x400+(4*4),
            units        = 'Hz',
            disp         = '{:d}',
            mode         = 'RO',
            pollInterval = 1
        ))

        self.add(pr.RemoteVariable(
            name         = 'DspReset',
            description  = 'DSP Reset Status',
            offset       = 0x400+(4*5),
            bitSize      = 1,
            bitOffset    = 0,
            mode         = 'RO',
            base         = pr.Bool,
            pollInterval = 1,
        ))

        self.add(pr.RemoteVariable(
            name         = 'HW_TYPE_C',
            offset       = 0x400+(4*6),
            bitSize      = 32,
            bitOffset    = 0,
            mode         = 'RO',
            enum        = {
                0x00_00_00_00: 'Undefined',
                0x00_00_00_01: 'XilinxZcu208',
                0x00_00_00_02: 'XilinxZcu216',
                0x00_00_00_03: 'XilinxKriaKv260',
                0x00_00_00_04: 'TrenzTe0835',
                0x00_00_00_05: 'SlacSpaceRfSocGen2',
                0x00_00_00_06: 'RealDigitalRfSoC4x2',
            },
        ))
