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
      -- AUX Clock and Reset
      auxClk          : in  sl;         -- 100 MHz
      auxRst          : in  sl;
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

   signal auxReadMasters  : AxiLiteReadMasterArray(1 downto 0);
   signal auxReadSlaves   : AxiLiteReadSlaveArray(1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal auxWriteMasters : AxiLiteWriteMasterArray(1 downto 0);
   signal auxWriteSlaves  : AxiLiteWriteSlaveArray(1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal auxReadMaster  : AxiLiteReadMasterType;
   signal auxReadSlave   : AxiLiteReadSlaveType  := AXI_LITE_READ_SLAVE_EMPTY_DECERR_C;
   signal auxWriteMaster : AxiLiteWriteMasterType;
   signal auxWriteSlave  : AxiLiteWriteSlaveType := AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C;

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

   signal auxRstL     : sl;
   signal overTemp    : sl;
   signal stuckBusDet : sl;
   signal adcData     : slv(15 downto 0);
   signal lvauxThresh : slv(15 downto 0);
   signal readReg     : slv(31 downto 0) := (others => '0');
   signal writeReg    : slv(31 downto 0) := (others => '0');

begin

   U_AxiLiteAsync : entity surf.AxiLiteAsync
      generic map (
         TPD_G           => TPD_G,
         COMMON_CLK_G    => false,
         NUM_ADDR_BITS_G => 32)
      port map (
         -- Slave Interface
         sAxiClk         => axilClk,
         sAxiClkRst      => axilRst,
         sAxiReadMaster  => axilReadMaster,
         sAxiReadSlave   => axilReadSlave,
         sAxiWriteMaster => axilWriteMaster,
         sAxiWriteSlave  => axilWriteSlave,
         -- Master Interface
         mAxiClk         => auxClk,
         mAxiClkRst      => auxRst,
         mAxiReadMaster  => auxReadMaster,
         mAxiReadSlave   => auxReadSlave,
         mAxiWriteMaster => auxWriteMaster,
         mAxiWriteSlave  => auxWriteSlave);

   auxRstL <= not(auxRst);

   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => 2,
         MASTERS_CONFIG_G   => AXIL_CONFIG_C)
      port map (
         axiClk              => auxClk,
         axiClkRst           => auxRst,
         sAxiWriteMasters(0) => auxWriteMaster,
         sAxiWriteSlaves(0)  => auxWriteSlave,
         sAxiReadMasters(0)  => auxReadMaster,
         sAxiReadSlaves(0)   => auxReadSlave,
         mAxiWriteMasters    => auxWriteMasters,
         mAxiWriteSlaves     => auxWriteSlaves,
         mAxiReadMasters     => auxReadMasters,
         mAxiReadSlaves      => auxReadSlaves);

   SysMonCore_Inst : AxiSocUltraPlusSysMonCore
      port map (
         s_axi_aclk      => auxClk,
         s_axi_aresetn   => auxRstL,
         s_axi_awaddr    => auxWriteMasters(0).awaddr(12 downto 0),
         s_axi_awvalid   => auxWriteMasters(0).awvalid,
         s_axi_awready   => auxWriteSlaves(0).awready,
         s_axi_wdata     => auxWriteMasters(0).wdata,
         s_axi_wstrb     => auxWriteMasters(0).wstrb,
         s_axi_wvalid    => auxWriteMasters(0).wvalid,
         s_axi_wready    => auxWriteSlaves(0).wready,
         s_axi_bresp     => auxWriteSlaves(0).bresp,
         s_axi_bvalid    => auxWriteSlaves(0).bvalid,
         s_axi_bready    => auxWriteMasters(0).bready,
         s_axi_araddr    => auxReadMasters(0).araddr(12 downto 0),
         s_axi_arvalid   => auxReadMasters(0).arvalid,
         s_axi_arready   => auxReadSlaves(0).arready,
         s_axi_rdata     => auxReadSlaves(0).rdata,
         s_axi_rresp     => auxReadSlaves(0).rresp,
         s_axi_rvalid    => auxReadSlaves(0).rvalid,
         s_axi_rready    => auxReadMasters(0).rready,
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
         axiClk           => auxClk,
         axiClkRst        => auxRst,
         axiReadMaster    => auxReadMasters(1),
         axiReadSlave     => auxReadSlaves(1),
         axiWriteMaster   => auxWriteMasters(1),
         axiWriteSlave    => auxWriteSlaves(1),
         -- User Read/Write registers
         writeRegister(0) => writeReg,
         readRegister(0)  => readReg);

   U_WTD : entity surf.WatchDogRst
      generic map(
         TPD_G      => TPD_G,
         DURATION_G => getTimeRatio(AUX_CLK_FREQ_C, 0.2))  -- 1 s timeout
      port map (
         clk    => auxClk,
         monIn  => r.busChangeDet,
         rstOut => stuckBusDet);

   comb : process (adcData, auxRst, lvauxThresh, overTemp, r, stuckBusDet,
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
      if (r.adcData /= v.adcData) and (writeReg(16) = '0') then
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
      if (auxRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process comb;

   seq : process (auxClk) is
   begin
      if rising_edge(auxClk) then
         r <= rin after TPD_G;
      end if;
   end process seq;

end architecture mapping;
