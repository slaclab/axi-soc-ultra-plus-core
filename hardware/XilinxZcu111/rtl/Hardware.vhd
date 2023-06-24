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
use surf.AxiLitePkg.all;

library axi_soc_ultra_plus_core;

entity Hardware is
   generic (
      TPD_G            : time := 1 ns;
      AXIL_CLK_FREQ_G  : real;
      AXIL_BASE_ADDR_G : slv(31 downto 0));
   port (
      --------------------------
      --       Ports
      --------------------------
      i2cScl         : inout slv(1 downto 0);
      i2cSda         : inout slv(1 downto 0);
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

architecture mapping of Hardware is

   constant NUM_AXIL_MASTERS_C : positive := 2;

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_AXIL_MASTERS_C-1 downto 0) := genAxiLiteConfig(NUM_AXIL_MASTERS_C, AXIL_BASE_ADDR_G, 28, 24);

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_AXIL_MASTERS_C-1 downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_AXIL_MASTERS_C-1 downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

begin

   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => NUM_AXIL_MASTERS_C,
         MASTERS_CONFIG_G   => AXIL_CONFIG_C)
      port map (
         axiClk              => axilClk,
         axiClkRst           => axilRst,
         sAxiWriteMasters(0) => axilWriteMaster,
         sAxiWriteSlaves(0)  => axilWriteSlave,
         sAxiReadMasters(0)  => axilReadMaster,
         sAxiReadSlaves(0)   => axilReadSlave,
         mAxiWriteMasters    => axilWriteMasters,
         mAxiWriteSlaves     => axilWriteSlaves,
         mAxiReadMasters     => axilReadMasters,
         mAxiReadSlaves      => axilReadSlaves);

   U_HardwareI2c0 : entity axi_soc_ultra_plus_core.HardwareI2c0
      generic map (
         TPD_G            => TPD_G,
         AXIL_CLK_FREQ_G  => AXIL_CLK_FREQ_G,
         AXIL_BASE_ADDR_G => AXIL_CONFIG_C(0).baseAddr)
      port map (
         --------------------------
         --       Ports
         --------------------------
         -- LMK Ports
         i2c0Scl          => i2cScl(0),
         i2c0Sda          => i2cSda(0),
         --------------------------
         --       Interfaces
         --------------------------
         -- Slave AXI-Lite Interface
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilWriteMaster => axilWriteMasters(0),
         axilWriteSlave  => axilWriteSlaves(0),
         axilReadMaster  => axilReadMasters(0),
         axilReadSlave   => axilReadSlaves(0));

   U_HardwareI2c1 : entity axi_soc_ultra_plus_core.HardwareI2c1
      generic map (
         TPD_G            => TPD_G,
         AXIL_CLK_FREQ_G  => AXIL_CLK_FREQ_G,
         AXIL_BASE_ADDR_G => AXIL_CONFIG_C(1).baseAddr)
      port map (
         --------------------------
         --       Ports
         --------------------------
         -- LMK Ports
         i2c1Scl          => i2cScl(1),
         i2c1Sda          => i2cSda(1),
         --------------------------
         --       Interfaces
         --------------------------
         -- Slave AXI-Lite Interface
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilWriteMaster => axilWriteMasters(1),
         axilWriteSlave  => axilWriteSlaves(1),
         axilReadMaster  => axilReadMasters(1),
         axilReadSlave   => axilReadSlaves(1));

end mapping;
