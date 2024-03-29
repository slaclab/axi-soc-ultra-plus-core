-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: PL Hardware I2c1 Module
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
use surf.AxiLitePkg.all;
use surf.I2cPkg.all;
use surf.I2cMuxPkg.all;

library axi_soc_ultra_plus_core;

library unisim;
use unisim.vcomponents.all;

entity HardwareI2c1 is
   generic (
      TPD_G            : time := 1 ns;
      AXIL_CLK_FREQ_G  : real;
      AXIL_BASE_ADDR_G : slv(31 downto 0));
   port (
      --------------------------
      --       Ports
      --------------------------
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
end HardwareI2c1;

architecture mapping of HardwareI2c1 is

   constant XBAR_I2C_CONFIG_C : AxiLiteCrossbarMasterConfigArray(7 downto 0) := genAxiLiteConfig(8, AXIL_BASE_ADDR_G, 24, 20);

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

   type RegType is record
      i2co : i2c_out_type;
   end record;

   constant REG_INIT_C : RegType := (
      i2co      => (
         scl    => '1',
         scloen => '1',
         sda    => '1',
         sdaoen => '1',
         enable => '0'));

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   -- attribute dont_touch                    : string;
   -- attribute dont_touch of r               : signal is "TRUE";
   -- attribute dont_touch of i2coVec         : signal is "TRUE";
   -- attribute dont_touch of i2cReadMasters  : signal is "TRUE";
   -- attribute dont_touch of i2cReadSlaves   : signal is "TRUE";
   -- attribute dont_touch of i2cWriteMasters : signal is "TRUE";
   -- attribute dont_touch of i2cWriteSlaves  : signal is "TRUE";

begin

   -----------------------------------------
   -- TCA9548APWR I2C MUX + AxiLite Crossbar
   -----------------------------------------
   U_XbarI2cMux : entity surf.AxiLiteCrossbarI2cMux
      generic map (
         TPD_G              => TPD_G,
         -- I2C MUX Generics
         MUX_DECODE_MAP_G   => I2C_MUX_DECODE_MAP_TCA9548_C,
         I2C_MUX_ADDR_G     => b"1110_100",
         I2C_SCL_FREQ_G     => 100.0E+3,  -- units of Hz
         AXIL_CLK_FREQ_G    => AXIL_CLK_FREQ_G,
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

   U_I2C_SPI : entity axi_soc_ultra_plus_core.Zcu111Sc18Is602
      generic map (
         TPD_G             => TPD_G,
         AXIL_CLK_FREQ_G   => AXIL_CLK_FREQ_G)
      port map (
         -- I2C Ports
         i2ci            => i2ci,
         i2co            => i2coVec(5),
         -- AXI-Lite Register Interface
         axilReadMaster  => i2cReadMasters(5),
         axilReadSlave   => i2cReadSlaves(5),
         axilWriteMaster => i2cWriteMasters(5),
         axilWriteSlave  => i2cWriteSlaves(5),
         -- Clocks and Resets
         axilClk         => axilClk,
         axilRst         => axilRst);

   comb : process (axilRst, i2cReadMasters, i2cWriteMasters, i2coVec, r) is
      variable v : RegType;
   begin
      -- Latch the current value
      v := r;

      -- Init (Default to I2C MUX endpoint)
      v.i2co := i2coVec(8);

      -- Check for TXN after XBAR/I2C_MUX
      for i in 0 to 7 loop
         if (i2cWriteMasters(i).awvalid = '1') or (i2cReadMasters(i).arvalid = '1') then
            v.i2co := i2coVec(i);
         end if;
      end loop;

      -- Synchronous Reset
      if (axilRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process;

   seq : process (axilClk) is
   begin
      if rising_edge(axilClk) then
         r <= rin after TPD_G;
      end if;
   end process;

   IOBUF_SCL : IOBUF
      port map (
         O  => i2ci.scl,
         IO => i2c1Scl,
         I  => r.i2co.scl,
         T  => r.i2co.scloen);

   IOBUF_SDA : IOBUF
      port map (
         O  => i2ci.sda,
         IO => i2c1Sda,
         I  => r.i2co.sda,
         T  => r.i2co.sdaoen);

end mapping;
