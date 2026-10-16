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

class I2cGpio(pr.Device):
    """
    NXP PCA9506DGG 40-bit I2C-bus I/O expander (U58).

    Five 8-bit banks (IO0..IO4). As with the PCA955x QSFP GPIO this is modeled
    on, the firmware I2C bridge maps each native I2C register byte onto its own
    32-bit AXI-Lite word, so:

        AXI offset = (I2C command-byte register address) x 4

        Register      I2C addr      AXI offset (relative to this Device's base)
        IP0..IP4      0x00..0x04 -> 0x00, 0x04, 0x08, 0x0C, 0x10   (read only)
        OP0..OP4      0x08..0x0C -> 0x20, 0x24, 0x28, 0x2C, 0x30
        PI0..PI4      0x10..0x14 -> 0x40, 0x44, 0x48, 0x4C, 0x50
        IOC0..IOC4    0x18..0x1C -> 0x60, 0x64, 0x68, 0x6C, 0x70
        MSK0..MSK4    0x20..0x24 -> 0x80, 0x84, 0x88, 0x8C, 0x90

    IOCx bit:  1 = input (power-on default),  0 = output.
    OPx  bit:  value driven on the pin when that pin is configured as an output.
    MSKx bit:  1 = interrupt masked (power-on default).

    Per-bank pin assignment for THIS board (from the U58 schematic). Rename the
    class/signals if you reuse this on a board with a different net mapping:

        Bank 0:  IO0_1=LMK_SEL0  IO0_2=LMK_SEL1  IO0_3=LMK_LD1  IO0_4=LMK_LD2
        Bank 1:  IO1_0=LMX_ENABLE1  IO1_1=LMX_SDO0
        Bank 2:  IO2_0=LMX_ENABLE0  IO2_1=LMX_SDO1
        Bank 3:  IO3_0=PWR_SALERT
        Bank 4:  IO4_0..IO4_3=BSI_DBG0..BSI_DBG3
        (Every other pin is a no-connect and is left as an input.)

    LMX_SDOx is dual-purpose (SDO and PLL LOCK), per the schematic notes.

    NOTE on multi-signal variables:
        In Rogue, passing list-valued ``offset``/``bitOffset``/``bitSize`` does
        NOT create an array; it *concatenates* the selected bit-slices into a
        single scalar value (sum(bitSize) bits wide).  ``.get()`` then returns
        one packed int and ``.set()`` expects one packed int -- so calling
        ``.set([a, b])`` raises "Overflow error ... Variable length = 0".

        To get a true array (where ``.get()`` returns ``[v0, v1, ...]`` and
        ``.set([v0, v1, ...])`` works) you must declare a *list variable* with
        ``numValues`` / ``valueBits`` / ``valueStride``.  Element ``k`` lives at
        bit ``bitOffset + k*valueStride`` and is ``valueBits`` wide.  Because the
        I2C->AXI bridge puts each register byte in its own 32-bit word, the
        stride between consecutive bytes is 32 bits.
    """

    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        ##############################################################
        # Input Port (IP) registers - read-only pin state.
        # Declared as list variables (numValues>1), so e.g.
        # LMK_SEL.get() returns array([LMK_SEL0, LMK_SEL1]).
        ##############################################################

        self.add(pr.RemoteVariable(
            name        = 'LMK_SEL',
            description = 'LMK select inputs: [0]=LMK_SEL0 (IO0_1, bit1), [1]=LMK_SEL1 (IO0_2, bit2)',
            offset      = 0x00,
            bitOffset   = 1,    # first element at bit 1
            numValues   = 2,
            valueBits   = 1,
            valueStride = 1,    # bits 1,2 are adjacent within bank-0
            mode        = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name        = 'LMK_LD',
            description = 'LMK lock-detect inputs: [0]=LMK_LD1 (IO0_3, bit3), [1]=LMK_LD2 (IO0_4, bit4)',
            offset      = 0x00,
            bitOffset   = 3,    # first element at bit 3
            numValues   = 2,
            valueBits   = 1,
            valueStride = 1,    # bits 3,4 are adjacent within bank-0
            mode        = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name        = 'LMX_PLL_LOCK',
            description = 'LMX SDO / PLL-LOCK inputs: [0]=LMX_SDO0 (IO1_1 @0x04 bit1), [1]=LMX_SDO1 (IO2_1 @0x08 bit1)',
            offset      = 0x04,
            bitOffset   = 1,    # bit 1 within each bank word
            numValues   = 2,
            valueBits   = 1,
            valueStride = 32,   # IP1 (0x04) and IP2 (0x08) are 1 AXI word (32 bits) apart
            mode        = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name        = 'PWR_SALERT',
            description = 'Power-supply alert input (IO3_0); external 4.7k pull-up to 3V3',
            offset      = 0x0C,
            bitOffset   = 0,
            bitSize     = 1,
            mode        = 'RO',
        ))

        self.add(pr.RemoteVariable(
            name        = 'BSI_DBG',
            description = 'BSI debug inputs: [i]=BSI_DBG[i] (IO4_0..IO4_3)',
            offset      = 0x10,
            bitOffset   = 0,
            numValues   = 4,
            valueBits   = 1,
            valueStride = 1,    # BSI_DBG[0..3] are bits 0..3 within bank-4
            mode        = 'RO',
        ))

        ##############################################################
        # Output Port (OP) registers - LMX_ENABLE drive value.
        # The two enables sit in *descending* address order relative to
        # their index, so they're kept as two explicit scalars rather
        # than one array (a list variable can only stride upward).
        #   LMX_ENABLE0 -> IO2_0, OP2 @ 0x28
        #   LMX_ENABLE1 -> IO1_0, OP1 @ 0x24
        # Both are configured as outputs and driven LOW by _start().
        ##############################################################

        self.add(pr.RemoteVariable(
            name        = 'LMX_ENABLE[0]',
            description = 'LMX_ENABLE[0] output (IO2_0, OP2 @0x28); driven LOW at start',
            offset      = 0x28,
            bitOffset   = 0,
            bitSize     = 1,
            mode        = 'RW',
        ))

        self.add(pr.RemoteVariable(
            name        = 'LMX_ENABLE[1]',
            description = 'LMX_ENABLE[1] output (IO1_0, OP1 @0x24); driven LOW at start',
            offset      = 0x24,
            bitOffset   = 0,
            bitSize     = 1,
            mode        = 'RW',
        ))

        ##############################################################
        # Configuration registers (hidden) - written by _start().
        #   IOC : I/O direction,        1=input / 0=output   (IOC0..IOC4)
        #   PI  : polarity inversion,   expect 0 (no invert) (PI0..PI4)
        #   MSK : interrupt mask,       1=masked             (MSK0..MSK4)
        # Each is a 5-element list variable spanning the five banks; each
        # bank byte is in its own AXI word, hence valueStride = 32.
        ##############################################################

        self.add(pr.RemoteVariable(
            name        = 'IOC',
            description = 'I/O Configuration banks 0-4 (1=input, 0=output)',
            offset      = 0x60,
            bitOffset   = 0,
            numValues   = 5,
            valueBits   = 8,
            valueStride = 32,
            mode        = 'RW',
            hidden      = True,
        ))

        self.add(pr.RemoteVariable(
            name        = 'PI',
            description = 'Polarity Inversion banks 0-4 (0=not inverted)',
            offset      = 0x40,
            bitOffset   = 0,
            numValues   = 5,
            valueBits   = 8,
            valueStride = 32,
            mode        = 'RW',
            hidden      = True,
        ))

        self.add(pr.RemoteVariable(
            name        = 'MSK',
            description = 'Interrupt Mask banks 0-4 (1=masked; INT pin is a no-connect)',
            offset      = 0x80,
            bitOffset   = 0,
            numValues   = 5,
            valueBits   = 8,
            valueStride = 32,
            mode        = 'RW',
            hidden      = True,
        ))

    def _start(self):
        # _start() is the per-device hook that Root.start() invokes recursively
        # (there is no public Device.start()), so the port configuration is
        # applied to hardware whenever the Rogue tree is started.
        super()._start()

        # 1) Park the only outputs (LMX_ENABLE0/1) LOW *before* the pins are
        #    turned into outputs, so they drive 0 with no transient HIGH.
        self.LMX_ENABLE[0].set(0x0)
        self.LMX_ENABLE[1].set(0x0)

        # 2) Set pin directions per the schematic (1=input, 0=output):
        #      IOC0 = 0xFF  LMK_SEL[1:0] + LMK_LD[1:0] + NC      -> all inputs
        #      IOC1 = 0xFE  IO1_0=LMX_ENABLE1 output, IO1_1=LMX_SDO0 input
        #      IOC2 = 0xFE  IO2_0=LMX_ENABLE0 output, IO2_1=LMX_SDO1 input
        #      IOC3 = 0xFF  PWR_SALERT + NC                      -> all inputs
        #      IOC4 = 0xFF  BSI_DBG[3:0] + NC                    -> all inputs
        self.IOC.set([0xFF, 0xFE, 0xFE, 0xFF, 0xFF])

        # 3) No polarity inversion on any input.
        self.PI.set([0x00, 0x00, 0x00, 0x00, 0x00])

        # 4) Mask every interrupt source (INT is a no-connect on this board).
        self.MSK.set([0xFF, 0xFF, 0xFF, 0xFF, 0xFF])
