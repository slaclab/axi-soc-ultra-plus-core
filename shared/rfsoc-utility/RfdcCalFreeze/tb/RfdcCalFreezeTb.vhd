-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Simulation Testbed for testing the RfdcCalFreeze module
--
--    U_Real: 2 real channels of 4 samples per clock behind the async bridge.
--            Channel 0 holds a steady 1000 LSB signal, channel 1 drops to
--            100 LSB and comes back, so the detector freezes and releases it.
--    U_Iq  : 1 I/Q channel of 2 complex samples per clock on a common clock,
--            |I|+|Q| of 400 LSB (below Threshold) then 600 LSB (above).
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
use ieee.numeric_std.all;

library surf;
use surf.StdRtlPkg.all;
use surf.AxiLitePkg.all;
use surf.TextUtilPkg.all;

library axi_soc_ultra_plus_core;

entity RfdcCalFreezeTb is end RfdcCalFreezeTb;

architecture testbed of RfdcCalFreezeTb is

   constant TPD_G : time := 1 ns;

   -- Register offsets
   constant GENERICS_C      : slv(31 downto 0) := x"0000_0000";
   constant MANUAL_FREEZE_C : slv(31 downto 0) := x"0000_0010";
   constant AUTO_ENABLE_C   : slv(31 downto 0) := x"0000_0014";
   constant CAL_FROZEN_C    : slv(31 downto 0) := x"0000_0030";
   constant AUTO_FREEZE_C   : slv(31 downto 0) := x"0000_0034";
   constant SIG_DETECT_C    : slv(31 downto 0) := x"0000_0038";
   constant CAL_FREEZE_C    : slv(31 downto 0) := x"0000_003C";
   constant MAGNITUDE0_C    : slv(31 downto 0) := x"0000_0040";
   constant MAGNITUDE1_C    : slv(31 downto 0) := x"0000_0044";

   -- Window of 2^6 clocks: 205 ns of dspClk, 640 ns of axilClk
   constant WINDOW_C : natural := 6;

   function sample (value : integer) return slv is
   begin
      return slv(to_signed(value, 16));
   end function;

   -- The same value in every 16-bit sample of the clock
   function fill (value : integer; n : positive) return slv is
      variable ret : slv(16*n-1 downto 0);
   begin
      for k in 0 to n-1 loop
         ret(16*k+15 downto 16*k) := sample(value);
      end loop;
      return ret;
   end function;

   signal dspClk : sl := '0';
   signal dspRst : sl := '1';

   signal axilClk : sl := '0';
   signal axilRst : sl := '1';

   -- U_Real
   signal realAdc0        : slv(63 downto 0)       := fill(1000, 4);
   signal realAdc1        : slv(63 downto 0)       := fill(-100, 4);
   signal realCalFreeze   : slv(1 downto 0)        := (others => '0');
   signal realWriteMaster : AxiLiteWriteMasterType := AXI_LITE_WRITE_MASTER_INIT_C;
   signal realWriteSlave  : AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_INIT_C;
   signal realReadMaster  : AxiLiteReadMasterType  := AXI_LITE_READ_MASTER_INIT_C;
   signal realReadSlave   : AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_INIT_C;

   -- U_Iq
   signal iqAdcI        : slv(31 downto 0)       := fill(200, 2);
   signal iqAdcQ        : slv(31 downto 0)       := fill(-200, 2);
   signal iqCalFreeze   : slv(0 downto 0)        := (others => '0');
   signal iqWriteMaster : AxiLiteWriteMasterType := AXI_LITE_WRITE_MASTER_INIT_C;
   signal iqWriteSlave  : AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_INIT_C;
   signal iqReadMaster  : AxiLiteReadMasterType  := AXI_LITE_READ_MASTER_INIT_C;
   signal iqReadSlave   : AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_INIT_C;

   signal passed : sl := '0';

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
   -- Modules to be tested
   -----------------------
   U_Real : entity axi_soc_ultra_plus_core.RfdcCalFreeze
      generic map (
         TPD_G              => TPD_G,
         COMMON_CLK_G       => false,
         NUM_CH_G           => 2,
         IQ_MODE_G          => false,
         SAMPLE_PER_CYCLE_G => 4,
         WINDOW_INIT_G      => WINDOW_C,
         HOLD_OFF_INIT_G    => 2)
      port map (
         -- ADC samples (dspClk domain)
         dspClk          => dspClk,
         dspRst          => dspRst,
         adcI0           => realAdc0,
         adcI1           => realAdc1,
         -- RF-ADC calibration freeze interface (axilClk domain)
         adcCalFreeze    => realCalFreeze,
         adcCalFrozen    => "10",
         adcSigDetect    => "01",
         -- AXI-Lite Interface (axilClk domain)
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilReadMaster  => realReadMaster,
         axilReadSlave   => realReadSlave,
         axilWriteMaster => realWriteMaster,
         axilWriteSlave  => realWriteSlave);

   U_Iq : entity axi_soc_ultra_plus_core.RfdcCalFreeze
      generic map (
         TPD_G              => TPD_G,
         COMMON_CLK_G       => true,
         NUM_CH_G           => 1,
         IQ_MODE_G          => true,
         SAMPLE_PER_CYCLE_G => 2,
         WINDOW_INIT_G      => WINDOW_C,
         HOLD_OFF_INIT_G    => 1)
      port map (
         -- ADC samples (dspClk domain)
         dspClk          => axilClk,
         dspRst          => axilRst,
         adcI0           => iqAdcI,
         adcQ0           => iqAdcQ,
         -- RF-ADC calibration freeze interface (axilClk domain)
         adcCalFreeze    => iqCalFreeze,
         -- AXI-Lite Interface (axilClk domain)
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilReadMaster  => iqReadMaster,
         axilReadSlave   => iqReadSlave,
         axilWriteMaster => iqWriteMaster,
         axilWriteSlave  => iqWriteSlave);

   ---------------------------------
   -- AXI-Lite Register Transactions
   ---------------------------------
   test : process is
      variable rdData : slv(31 downto 0);

      procedure checkReal (addr : slv(31 downto 0); expected : slv(31 downto 0); name : string) is
      begin
         axiLiteBusSimRead(axilClk, realReadMaster, realReadSlave, addr, rdData);
         assert (rdData = expected)
            report "U_Real." & name & " = 0x" & hstr(rdData) & ", expected 0x" & hstr(expected)
            severity failure;
      end procedure;

      procedure checkIq (addr : slv(31 downto 0); expected : slv(31 downto 0); name : string) is
      begin
         axiLiteBusSimRead(axilClk, iqReadMaster, iqReadSlave, addr, rdData);
         assert (rdData = expected)
            report "U_Iq." & name & " = 0x" & hstr(rdData) & ", expected 0x" & hstr(expected)
            severity failure;
      end procedure;

   begin
      ------------------------------------------
      -- Wait for the resets to complete and the
      -- detectors to run a few windows
      ------------------------------------------
      wait until axilRst = '1';
      wait until axilRst = '0';
      wait for 4 us;

      ------------------------------------------
      -- U_Real: 2 real channels, 4 samples/clk
      ------------------------------------------
      -- NUM_CH_G=2, SAMPLE_PER_CYCLE_G=4, WINDOW_MAX_C=24, real, async
      checkReal(GENERICS_C, x"0018_0402", "Generics");
      checkReal(MAGNITUDE0_C, x"0000_03E8", "Magnitude[0]");  -- 1000
      checkReal(MAGNITUDE1_C, x"0000_0064", "Magnitude[1]");  -- 100
      -- The IP core status pins pass through
      checkReal(CAL_FROZEN_C, x"0000_0002", "CalFrozen");
      checkReal(SIG_DETECT_C, x"0000_0001", "SigDetect");
      -- Channel 1 is below Threshold but AutoEnable is off
      checkReal(AUTO_FREEZE_C, x"0000_0002", "AutoFreeze");
      checkReal(CAL_FREEZE_C, x"0000_0000", "CalFreeze");
      assert (realCalFreeze = "00") report "U_Real.adcCalFreeze asserted while disabled" severity failure;

      -- Enable the detector on both channels: only channel 1 freezes
      axiLiteBusSimWrite(axilClk, realWriteMaster, realWriteSlave, AUTO_ENABLE_C, x"0000_0003");
      wait for 1 us;
      checkReal(CAL_FREEZE_C, x"0000_0002", "CalFreeze");
      assert (realCalFreeze = "10") report "U_Real.adcCalFreeze /= 10 with the detector enabled" severity failure;

      -- Manual freeze of channel 0 adds to it
      axiLiteBusSimWrite(axilClk, realWriteMaster, realWriteSlave, MANUAL_FREEZE_C, x"0000_0001");
      wait for 1 us;
      checkReal(CAL_FREEZE_C, x"0000_0003", "CalFreeze");
      assert (realCalFreeze = "11") report "U_Real.adcCalFreeze /= 11 with the manual freeze" severity failure;

      -- Signal returns on channel 1: one window releases it
      realAdc1 <= fill(1000, 4);
      wait for 1 us;
      checkReal(MAGNITUDE1_C, x"0000_03E8", "Magnitude[1]");  -- 1000
      checkReal(AUTO_FREEZE_C, x"0000_0000", "AutoFreeze");
      checkReal(CAL_FREEZE_C, x"0000_0001", "CalFreeze");
      assert (realCalFreeze = "01") report "U_Real.adcCalFreeze /= 01 after the signal returned" severity failure;

      ------------------------------------------
      -- U_Iq: 1 I/Q channel, 2 complex samples/clk
      ------------------------------------------
      -- NUM_CH_G=1, SAMPLE_PER_CYCLE_G=2, WINDOW_MAX_C=24, I/Q, common clock
      checkIq(GENERICS_C, x"0318_0201", "Generics");
      checkIq(MAGNITUDE0_C, x"0000_0190", "Magnitude[0]");  -- |200|+|-200| = 400
      checkIq(AUTO_FREEZE_C, x"0000_0001", "AutoFreeze");
      checkIq(CAL_FREEZE_C, x"0000_0000", "CalFreeze");

      axiLiteBusSimWrite(axilClk, iqWriteMaster, iqWriteSlave, AUTO_ENABLE_C, x"0000_0001");
      wait for 2 us;
      checkIq(CAL_FREEZE_C, x"0000_0001", "CalFreeze");
      assert (iqCalFreeze = "1") report "U_Iq.adcCalFreeze /= 1 with the detector enabled" severity failure;

      -- Signal returns: |300|+|-300| = 600 is above Threshold
      iqAdcI <= fill(300, 2);
      iqAdcQ <= fill(-300, 2);
      wait for 2 us;
      checkIq(MAGNITUDE0_C, x"0000_0258", "Magnitude[0]");
      checkIq(CAL_FREEZE_C, x"0000_0000", "CalFreeze");
      assert (iqCalFreeze = "0") report "U_Iq.adcCalFreeze /= 0 after the signal returned" severity failure;

      passed <= '1';
      report "RfdcCalFreezeTb PASSED" severity note;
      wait;

   end process test;

end testbed;
