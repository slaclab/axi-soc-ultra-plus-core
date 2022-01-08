-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Wrapper for Sysmon IP core
-------------------------------------------------------------------------------
-- This file is part of 'axi-pcie-core'.
-- It is subject to the license terms in the LICENSE.txt file found in the
-- top-level directory of this distribution and at:
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
-- No part of 'axi-pcie-core', including this file,
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
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

entity AxiSocUltraPlusSysMon is
   generic (
      TPD_G                    : time := 1 ns;
      SYSMON_LVAUX_THRESHOLD_G : slv(15 downto 0);
      AXIL_BASE_ADDR_G         : slv(31 downto 0));
   port (
      -- Over Temp or LVAUX Error Detect
      sysmonError     : out sl;
      -- SYSMON Ports
      vPIn            : in  sl;
      vNIn            : in  sl;
      -- AXI-Lite Register Interface
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      -- Clocks and Resets
      axilClk         : in  sl;
      axilRst         : in  sl);
end entity AxiSocUltraPlusSysMon;

architecture mapping of AxiSocUltraPlusSysMon is

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(1 downto 0) := genAxiLiteConfig(2, AXIL_BASE_ADDR_G, 16, 12);

   signal axilReadMasters  : AxiLiteReadMasterArray(1 downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(1 downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   component AxiSocUltraPlusSysMonCore
      port (
         s_axi_aclk      : in  std_logic;
         s_axi_aresetn   : in  std_logic;
         s_axi_awaddr    : in  std_logic_vector(12 downto 0);
         s_axi_awvalid   : in  std_logic;
         s_axi_awready   : out std_logic;
         s_axi_wdata     : in  std_logic_vector(31 downto 0);
         s_axi_wstrb     : in  std_logic_vector(3 downto 0);
         s_axi_wvalid    : in  std_logic;
         s_axi_wready    : out std_logic;
         s_axi_bresp     : out std_logic_vector(1 downto 0);
         s_axi_bvalid    : out std_logic;
         s_axi_bready    : in  std_logic;
         s_axi_araddr    : in  std_logic_vector(12 downto 0);
         s_axi_arvalid   : in  std_logic;
         s_axi_arready   : out std_logic;
         s_axi_rdata     : out std_logic_vector(31 downto 0);
         s_axi_rresp     : out std_logic_vector(1 downto 0);
         s_axi_rvalid    : out std_logic;
         s_axi_rready    : in  std_logic;
         ip2intc_irpt    : out std_logic;
         vp              : in  std_logic;
         vn              : in  std_logic;
         ot_out          : out std_logic;
         channel_out     : out std_logic_vector(5 downto 0);
         eoc_out         : out std_logic;
         alarm_out       : out std_logic;
         eos_out         : out std_logic;
         busy_out        : out std_logic;
         adc_data_master : out std_logic_vector(15 downto 0)
         );
   end component;

   type RegType is record
      adcData      : slv(15 downto 0);
      busChangeDet : sl;
      lvauxTrip    : sl;

   end record RegType;
   constant REG_INIT_C : RegType := (
      adcData      => (others => '0'),
      lvauxTrip    => '0',
      busChangeDet => '0');

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   signal axilRstL    : sl;
   signal overTemp    : sl;
   signal stuckBusDet : sl;
   signal adcData     : slv(15 downto 0);
   signal lvauxThresh : slv(15 downto 0);
   signal readReg     : slv(31 downto 0) := (others => '0');
   signal writeReg    : slv(31 downto 0) := (others => '0');

begin

   axilRstL <= not(axilRst);

   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => 2,
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

   SysMonCore_Inst : AxiSocUltraPlusSysMonCore
      port map (
         s_axi_aclk      => axilClk,
         s_axi_aresetn   => axilRstL,
         s_axi_awaddr    => axilWriteMasters(0).awaddr(12 downto 0),
         s_axi_awvalid   => axilWriteMasters(0).awvalid,
         s_axi_awready   => axilWriteSlaves(0).awready,
         s_axi_wdata     => axilWriteMasters(0).wdata,
         s_axi_wstrb     => axilWriteMasters(0).wstrb,
         s_axi_wvalid    => axilWriteMasters(0).wvalid,
         s_axi_wready    => axilWriteSlaves(0).wready,
         s_axi_bresp     => axilWriteSlaves(0).bresp,
         s_axi_bvalid    => axilWriteSlaves(0).bvalid,
         s_axi_bready    => axilWriteMasters(0).bready,
         s_axi_araddr    => axilReadMasters(0).araddr(12 downto 0),
         s_axi_arvalid   => axilReadMasters(0).arvalid,
         s_axi_arready   => axilReadSlaves(0).arready,
         s_axi_rdata     => axilReadSlaves(0).rdata,
         s_axi_rresp     => axilReadSlaves(0).rresp,
         s_axi_rvalid    => axilReadSlaves(0).rvalid,
         s_axi_rready    => axilReadMasters(0).rready,
         ip2intc_irpt    => open,
         vp              => vPIn,
         vn              => vNIn,
         ot_out          => overTemp,
         channel_out     => open,
         eoc_out         => open,
         alarm_out       => open,
         eos_out         => open,
         busy_out        => open,
         adc_data_master => adcData);

   readReg(15 downto 0)     <= adcData(15 downto 0);
   lvauxThresh(15 downto 0) <= writeReg(15 downto 0);

   U_AxiLiteRegs : entity surf.AxiLiteRegs
      generic map (
         TPD_G           => TPD_G,
         NUM_WRITE_REG_G => 1,
         INI_WRITE_REG_G => (0 => resize(SYSMON_LVAUX_THRESHOLD_G, 32)),
         NUM_READ_REG_G  => 1)
      port map (
         -- AXI-Lite Bus
         axiClk           => axilClk,
         axiClkRst        => axilRst,
         axiReadMaster    => axilReadMasters(1),
         axiReadSlave     => axilReadSlaves(1),
         axiWriteMaster   => axilWriteMasters(1),
         axiWriteSlave    => axilWriteSlaves(1),
         -- User Read/Write registers
         writeRegister(0) => writeReg,
         readRegister(0)  => readReg);

   U_WTD : entity surf.WatchDogRst
      generic map(
         TPD_G      => TPD_G,
         DURATION_G => getTimeRatio(DMA_CLK_FREQ_C, 0.2))  -- 1 s timeout
      port map (
         clk    => axilClk,
         monIn  => r.busChangeDet,
         rstOut => stuckBusDet);

   comb : process (adcData, axilRst, lvauxThresh, overTemp, r, stuckBusDet,
                   writeReg) is
      variable v      : RegType;
      variable i      : natural;
      variable sofDet : sl;
   begin
      -- Latch the current value
      v := r;

      -- Keep a delayed copy
      v.adcData := adcData;

      -- Check for change in bus and not forcing bus change detection from SW
      if (r.adcData /= v.adcData) and (writeReg(31 downto 16) = 0) then
         -- Set the flag
         v.busChangeDet := '1';
      else
         -- Reset flag
         v.busChangeDet := '0';
      end if;

      -- Check if there is an LVAUX trip (refer to UG584)
      if (r.adcData > lvauxThresh) then
         -- Set the flag
         v.lvauxTrip := '1';
      else
         -- Reset flag
         v.lvauxTrip := '0';
      end if;

      -- Outputs
      sysmonError <= overTemp or stuckBusDet or r.lvauxTrip;

      -- Reset
      if (axilRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process comb;

   seq : process (axilClk) is
   begin
      if rising_edge(axilClk) then
         r <= rin after TPD_G;
      end if;
   end process seq;

end architecture mapping;
