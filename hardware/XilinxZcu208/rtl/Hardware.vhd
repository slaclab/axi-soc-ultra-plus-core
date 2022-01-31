-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: PL Hardware Module
-------------------------------------------------------------------------------
-- This file is part of 'axi-soc-ultra-plus-core'.
-- It is subject to the license terms in the LICENSE.txt file found in the
-- top-level directory of this distribution and at:
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
-- No part of 'axi-soc-ultra-plus-core', including this file,
-- may be copied, modified, propagated, or distributed except according to
-- the terms contained in the LICENSE.txt file.
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

library surf;
use surf.StdRtlPkg.all;
use surf.AxiStreamPkg.all;
use surf.AxiLitePkg.all;
use surf.I2cPkg.all;
use surf.I2cMuxPkg.all;

library unisim;
use unisim.vcomponents.all;

entity Hardware is
   generic (
      TPD_G            : time := 1 ns;
      AXIL_BASE_ADDR_G : slv(31 downto 0));
   port (
      --------------------------
      --       Ports
      --------------------------
      lmkSync         : out   sl;
      clkMuxSel       : out   slv(1 downto 0);
      i2c1Scl         : inout sl;
      i2c1Sda         : inout sl;
      --------------------------
      --       Interfaces
      --------------------------
      -- Slave AXI-Lite Interface
      axilClk         : in    sl;
      axilRst         : in    sl;
      axilWriteMaster : in    AxiLiteWriteMasterType;
      axilWriteSlave  : out   AxiLiteWriteSlaveType;
      axilReadMaster  : in    AxiLiteReadMasterType;
      axilReadSlave   : out   AxiLiteReadSlaveType);
end Hardware;

architecture top_level of Hardware is

   constant XBAR_I2C_CONFIG_C : AxiLiteCrossbarMasterConfigArray(7 downto 0) := genAxiLiteConfig(8, AXIL_BASE_ADDR_G, 28, 24);

   constant I2C_CONFIG_C : I2cAxiLiteDevArray(1 downto 0) := (
      0              => MakeI2cAxiLiteDevType(
         i2cAddress  => "1010000",      -- 2 wire address 1010000X (A0h)
         dataSize    => 8,              -- in units of bits
         addrSize    => 8,              -- in units of bits
         endianness  => '0',            -- Little endian
         repeatStart => '1'),           -- No repeat start
      1              => MakeI2cAxiLiteDevType(
         i2cAddress  => "1010001",      -- 2 wire address 1010001X (A2h)
         dataSize    => 8,              -- in units of bits
         addrSize    => 8,              -- in units of bits
         endianness  => '0',            -- Little endian
         repeatStart => '1'));          -- Repeat Start

   signal i2cReadMasters  : AxiLiteReadMasterArray(7 downto 0);
   signal i2cReadSlaves   : AxiLiteReadSlaveArray(7 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal i2cWriteMasters : AxiLiteWriteMasterArray(7 downto 0);
   signal i2cWriteSlaves  : AxiLiteWriteSlaveArray(7 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal i2ci : i2c_in_type;
   signal i2coVec : i2c_out_array(8 downto 0) := (
      others    => (
         scl    => '1',
         scloen => '1',
         sda    => '1',
         sdaoen => '1',
         enable => '0'));
   signal i2co : i2c_out_type;

begin

   lmkSync   <= '0';
   clkMuxSel <= "10";                   -- b10: SDO = LMK_MUXOUT

   -----------------------------------------
   -- TCA9548APWR I2C MUX + AxiLite Crossbar
   -----------------------------------------
   U_XbarI2cMux : entity surf.AxiLiteCrossbarI2cMux
      generic map (
         TPD_G              => TPD_G,
         -- I2C MUX Generics
         MUX_DECODE_MAP_G   => I2C_MUX_DECODE_MAP_TCA9548_C,
         I2C_MUX_ADDR_G     => b"1110_100",
         I2C_SCL_FREQ_G     => 400.0E+3,  -- units of Hz
         AXIL_CLK_FREQ_G    => 156.25E+6,
         -- AXI-Lite Crossbar Generics
         NUM_MASTER_SLOTS_G => 8,
         MASTERS_CONFIG_G   => XBAR_I2C_CONFIG_C)
      port map (
         -- Clocks and Resets
         axilClk           => axilClk,
         axilRst           => axilRst,
         -- Slave AXI-Lite Interface
         sAxilWriteMaster  => axilWriteMaster,
         sAxilWriteSlave   => axilWriteSlave,
         sAxilReadMaster   => axilReadMaster,
         sAxilReadSlave    => axilReadSlave,
         -- Master AXI-Lite Interfaces
         mAxilWriteMasters => i2cWriteMasters,
         mAxilWriteSlaves  => i2cWriteSlaves,
         mAxilReadMasters  => i2cReadMasters,
         mAxilReadSlaves   => i2cReadSlaves,
         -- I2C MUX Ports
         i2ci              => i2ci,
         i2co              => i2coVec(8));

   U_I2C_CLK104 : entity surf.AxiI2cRegMasterCore
      generic map (
         TPD_G          => TPD_G,
         I2C_SCL_FREQ_G => 400.0E+3,    -- units of Hz
         DEVICE_MAP_G   => I2C_CONFIG_C,
         AXI_CLK_FREQ_G => AXIL_CLK_FREQ_C)
      port map (
         -- I2C Ports
         i2ci           => i2ci,
         i2co           => i2coVec(5),
         -- AXI-Lite Register Interface
         axiReadMaster  => i2cReadMasters(5),
         axiReadSlave   => i2cReadSlaves(5),
         axiWriteMaster => i2cWriteMasters(5),
         axiWriteSlave  => i2cWriteSlaves(5),
         -- Clocks and Resets
         axiClk         => axilClk,
         axiRst         => axilRst);

   process(i2cReadMasters, i2cWriteMasters, i2coVec)
      variable tmp : i2c_out_type;
   begin
      -- Init (Default to I2C MUX endpoint)
      tmp := i2coVec(8);
      -- Check for TXN after XBAR/I2C_MUX
      for i in 0 to 7 loop
         if (i2cWriteMasters(i).awvalid = '1') or (i2cReadMasters(i).arvalid = '1') then
            tmp := i2coVec(i);
         end if;
      end loop;
      -- Return result
      i2co <= tmp;
   end process;

   IOBUF_SCL : IOBUF
      port map (
         O  => i2ci.scl,
         IO => i2c1Scl,
         I  => i2co.scl,
         T  => i2co.scloen);

   IOBUF_SDA : IOBUF
      port map (
         O  => i2ci.sda,
         IO => i2c1Sda,
         I  => i2co.sda,
         T  => i2co.sdaoen);

end top_level;
