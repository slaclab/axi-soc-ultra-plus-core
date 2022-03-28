-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Simulation Testbed for testing the SigGen module
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
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library surf;
use surf.StdRtlPkg.all;
use surf.AxiLitePkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.SigGenPkg.all;

entity SigGenTb is end SigGenTb;

architecture testbed of SigGenTb is

   constant TPD_G : time := 1 ns;

   signal dspClk    : sl                      := '0';
   signal dspRst    : sl                      := '1';
   signal dspDacIn  : Slv256Array(0 downto 0) := (others => (others => '1'));
   signal dspDacOut : Slv256Array(0 downto 0) := (others => (others => '0'));

   signal axilClk         : sl                     := '0';
   signal axilRst         : sl                     := '1';
   signal axilWriteMaster : AxiLiteWriteMasterType := AXI_LITE_WRITE_MASTER_INIT_C;
   signal axilWriteSlave  : AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_INIT_C;
   signal axilReadMaster  : AxiLiteReadMasterType  := AXI_LITE_READ_MASTER_INIT_C;
   signal axilReadSlave   : AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_INIT_C;

begin

   --------------------
   -- Clocks and Resets
   --------------------
   U_dspClk : entity surf.ClkRst
      generic map (
         CLK_PERIOD_G      => 3.2 ns,
         RST_START_DELAY_G => 0 ns,
         RST_HOLD_TIME_G   => 1000 ns)
      port map (
         clkP => dspClk,
         rst  => dspRst);

   U_axilClk : entity surf.ClkRst
      generic map (
         CLK_PERIOD_G      => 10 ns,
         RST_START_DELAY_G => 0 ns,
         RST_HOLD_TIME_G   => 1000 ns)
      port map (
         clkP => axilClk,
         rst  => axilRst);

   -----------------------
   -- Module to be tested
   -----------------------
   U_DUT : entity axi_soc_ultra_plus_core.SigGen
      generic map (
         TPD_G              => TPD_G,
         NUM_CH_G           => 1,
         RAM_ADDR_WIDTH_G   => 4,
         SAMPLE_PER_CYCLE_G => 16,
         AXIL_BASE_ADDR_G   => (others => '0'))
      port map (
         -- DAC Interface (dspClk domain)
         dspClk          => dspClk,
         dspRst          => dspRst,
         dspDacIn        => dspDacIn,
         dspDacOut       => dspDacOut,
         -- AXI-Lite Interface (axilClk domain)
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilReadMaster  => axilReadMaster,
         axilReadSlave   => axilReadSlave,
         axilWriteMaster => axilWriteMaster,
         axilWriteSlave  => axilWriteSlave);

   ---------------------------------
   -- AXI-Lite Register Transactions
   ---------------------------------
   test : process is
      variable i : natural := 0;
   begin
      ------------------------------------------
      -- Wait for the AXI-Lite reset to complete
      ------------------------------------------
      wait until axilRst = '1';
      wait until axilRst = '0';

      ------------------------------------------
      -- Load BRAM with counter data
      ------------------------------------------
      for i in 0 to 127 loop
         axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0001_0000"+toSlv(4*i, 32), toSlv((2**16)*(2*i+1)+(2*i), 32), true);
      end loop;

      -- config.burstCnt = 3
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_0008", x"0000_0003", true);

      -- config.bufferLength = 7
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_000C", x"0000_0007", true);

      -- config.continuous = 0
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_0010", x"0000_0000", true);

      -- config.idleValue = 0x8000
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_0018", x"0000_8000", true);

      -- config.enabled = 1
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_0020", x"0000_0001", true);

      -- config.burst = 1
      axiLiteBusSimWrite (axilClk, axilWriteMaster, axilWriteSlave, x"0000_0014", x"0000_0001", true);

   end process test;

end testbed;
