-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: AXI-Lite Crossbar and Register Access
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

library surf;
use surf.StdRtlPkg.all;
use surf.AxiPkg.all;
use surf.AxiStreamPkg.all;
use surf.AxiLitePkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

library unisim;
use unisim.vcomponents.all;

entity AxiSocUltraPlusReg is
   generic (
      TPD_G                : time                        := 1 ns;
      ROGUE_SIM_EN_G       : boolean                     := false;
      ROGUE_SIM_PORT_NUM_G : natural range 1024 to 49151 := 8000;
      BUILD_INFO_G         : BuildInfoType;
      EXT_AXIL_MASTER_G    : boolean                     := false;
      DMA_SIZE_G           : positive range 1 to 16      := 1);
   port (
      -- Internal AXI4 Interfaces (axiClk domain)
      axiClk              : in  sl;
      axiRst              : in  sl;
      regReadMaster       : in  AxiLiteReadMasterType;
      regReadSlave        : out AxiLiteReadSlaveType;
      regWriteMaster      : in  AxiLiteWriteMasterType;
      regWriteSlave       : out AxiLiteWriteSlaveType;
      -- External AXI-Lite Interfaces  (dmaClk domain): EXT_AXIL_MASTER_G = true
      extReadMaster       : in  AxiLiteReadMasterType  := AXI_LITE_READ_MASTER_INIT_C;
      extReadSlave        : out AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_EMPTY_DECERR_C;
      extWriteMaster      : in  AxiLiteWriteMasterType := AXI_LITE_WRITE_MASTER_INIT_C;
      extWriteSlave       : out AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C;
      -- DMA AXI-Lite Interfaces (axiClk domain)
      dmaCtrlReadMasters  : out AxiLiteReadMasterArray(2 downto 1);
      dmaCtrlReadSlaves   : in  AxiLiteReadSlaveArray(2 downto 1);
      dmaCtrlWriteMasters : out AxiLiteWriteMasterArray(2 downto 1);
      dmaCtrlWriteSlaves  : in  AxiLiteWriteSlaveArray(2 downto 1);
      -- Application AXI-Lite Interfaces [0x80000000:0xFFFFFFFF] (appClk domain)
      appClk              : in  sl;
      appRst              : in  sl;
      appReadMaster       : out AxiLiteReadMasterType;
      appReadSlave        : in  AxiLiteReadSlaveType;
      appWriteMaster      : out AxiLiteWriteMasterType;
      appWriteSlave       : in  AxiLiteWriteSlaveType;
      -- Application Force reset
      cardResetIn         : in  sl;
      cardResetOut        : out sl;
      -- SYSMON Ports
      vPIn                : in  sl;
      vNIn                : in  sl);
end AxiSocUltraPlusReg;

architecture mapping of AxiSocUltraPlusReg is

   constant VERSION_INDEX_C : natural := 0;
   constant SYSMON_INDEX_C  : natural := 1;
   constant AXIS_MON_IB_C   : natural := 2;
   constant AXIS_MON_OB_C   : natural := 3;
   constant APP_INDEX_C     : natural := 4;

   constant NUM_AXI_MASTERS_C : natural := 5;

   constant AXI_CROSSBAR_MASTERS_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_AXI_MASTERS_C-1 downto 0) := (
      VERSION_INDEX_C => (
         baseAddr     => x"0000_0000",
         addrBits     => 16,
         connectivity => x"FFFF"),
      SYSMON_INDEX_C  => (
         baseAddr     => x"0001_0000",
         addrBits     => 16,
         connectivity => x"FFFF"),
      AXIS_MON_IB_C   => (
         baseAddr     => x"0002_0000",
         addrBits     => 16,
         connectivity => x"FFFF"),
      AXIS_MON_OB_C   => (
         baseAddr     => x"0003_0000",
         addrBits     => 16,
         connectivity => x"FFFF"),
      APP_INDEX_C     => (
         baseAddr     => APP_ADDR_OFFSET_C,
         addrBits     => 31,
         connectivity => x"FFFF"));

   signal axilReadMaster  : AxiLiteReadMasterType;
   signal axilReadSlave   : AxiLiteReadSlaveType;
   signal axilWriteMaster : AxiLiteWriteMasterType;
   signal axilWriteSlave  : AxiLiteWriteSlaveType;

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_AXI_MASTERS_C-1 downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_AXI_MASTERS_C-1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_AXI_MASTERS_C-1 downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_AXI_MASTERS_C-1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal userValues   : Slv32Array(0 to 63) := (others => x"00000000");
   signal cardRst      : sl;
   signal appReset     : sl;
   signal appResetSync : sl;
   signal appClkFreq   : slv(31 downto 0);

begin

   ---------------------------------------------------------------------------------------------
   -- Driver Polls the userValues to determine the firmware's configurations and interrupt state
   ---------------------------------------------------------------------------------------------
   process(appClkFreq, appResetSync)
      variable i : natural;
   begin
      -- Number of DMA lanes (defined by user)
      userValues(0) <= toSlv(DMA_SIZE_G, 32);

      -- System Clock Frequency
      userValues(1) <= toSlv(getTimeRatio(DMA_CLK_FREQ_C, 1.0), 32);

      -- Application Reset
      userValues(2)(0) <= appResetSync;

      -- Application Clock Frequency
      userValues(3) <= appClkFreq;

      -- Set unused to zero
      for i in 63 downto 4 loop
         userValues(i) <= x"00000000";
      end loop;

   end process;

   -------------------------
   -- AXI-to-AXI-Lite Bridge
   -------------------------
   REAL_CPU : if (not ROGUE_SIM_EN_G) generate

      axilReadMaster  <= regReadMaster;
      regReadSlave    <= axilReadSlave;
      axilWriteMaster <= regWriteMaster;
      regWriteSlave   <= axilWriteSlave;

      U_SysMon : entity axi_soc_ultra_plus_core.AxiSocUltraPlusSysMon
         generic map (
            TPD_G => TPD_G)
         port map (
            -- SYSMON Ports
            vPIn            => vPIn,
            vNIn            => vNIn,
            -- AXI-Lite Register Interface
            axilReadMaster  => axilReadMasters(SYSMON_INDEX_C),
            axilReadSlave   => axilReadSlaves(SYSMON_INDEX_C),
            axilWriteMaster => axilWriteMasters(SYSMON_INDEX_C),
            axilWriteSlave  => axilWriteSlaves(SYSMON_INDEX_C),
            -- Clocks and Resets
            axilClk         => axiClk,
            axilRst         => axiRst);

   end generate;

   SIM_CPU : if (ROGUE_SIM_EN_G) generate

      U_TcpToAxiLite : entity surf.RogueTcpMemoryWrap
         generic map (
            TPD_G      => TPD_G,
            PORT_NUM_G => ROGUE_SIM_PORT_NUM_G+0)
         port map (
            axilClk         => axiClk,
            axilRst         => axiRst,
            axilReadMaster  => axilReadMaster,
            axilReadSlave   => axilReadSlave,
            axilWriteMaster => axilWriteMaster,
            axilWriteSlave  => axilWriteSlave);

   end generate;

   --------------------
   -- AXI-Lite Crossbar
   --------------------
   DUAL_MASTER : if (EXT_AXIL_MASTER_G) generate
      U_XBAR : entity surf.AxiLiteCrossbar
         generic map (
            TPD_G              => TPD_G,
            NUM_SLAVE_SLOTS_G  => 2,
            NUM_MASTER_SLOTS_G => NUM_AXI_MASTERS_C,
            MASTERS_CONFIG_G   => AXI_CROSSBAR_MASTERS_CONFIG_C)
         port map (
            axiClk              => axiClk,
            axiClkRst           => axiRst,
            sAxiWriteMasters(0) => axilWriteMaster,
            sAxiWriteMasters(1) => extWriteMaster,
            sAxiWriteSlaves(0)  => axilWriteSlave,
            sAxiWriteSlaves(1)  => extWriteSlave,
            sAxiReadMasters(0)  => axilReadMaster,
            sAxiReadMasters(1)  => extReadMaster,
            sAxiReadSlaves(0)   => axilReadSlave,
            sAxiReadSlaves(1)   => extReadSlave,
            mAxiWriteMasters    => axilWriteMasters,
            mAxiWriteSlaves     => axilWriteSlaves,
            mAxiReadMasters     => axilReadMasters,
            mAxiReadSlaves      => axilReadSlaves);
   end generate;

   SINGLE_MASTER : if (not EXT_AXIL_MASTER_G) generate
      U_XBAR : entity surf.AxiLiteCrossbar
         generic map (
            TPD_G              => TPD_G,
            NUM_SLAVE_SLOTS_G  => 1,
            NUM_MASTER_SLOTS_G => NUM_AXI_MASTERS_C,
            MASTERS_CONFIG_G   => AXI_CROSSBAR_MASTERS_CONFIG_C)
         port map (
            axiClk              => axiClk,
            axiClkRst           => axiRst,
            sAxiWriteMasters(0) => axilWriteMaster,
            sAxiWriteSlaves(0)  => axilWriteSlave,
            sAxiReadMasters(0)  => axilReadMaster,
            sAxiReadSlaves(0)   => axilReadSlave,
            mAxiWriteMasters    => axilWriteMasters,
            mAxiWriteSlaves     => axilWriteSlaves,
            mAxiReadMasters     => axilReadMasters,
            mAxiReadSlaves      => axilReadSlaves);
   end generate;

   --------------------------
   -- AXI-Lite Version Module
   --------------------------
   U_Version : entity surf.AxiVersion
      generic map (
         TPD_G           => TPD_G,
         BUILD_INFO_G    => BUILD_INFO_G,
         CLK_PERIOD_G    => DMA_CLK_PERIOD_C,
         EN_DEVICE_DNA_G => true,
         XIL_DEVICE_G    => "ULTRASCALE_PLUS",
         EN_ICAP_G       => false)
      port map (
         -- AXI-Lite Interface
         axiClk         => axiClk,
         axiRst         => axiRst,
         axiReadMaster  => axilReadMasters(VERSION_INDEX_C),
         axiReadSlave   => axilReadSlaves(VERSION_INDEX_C),
         axiWriteMaster => axilWriteMasters(VERSION_INDEX_C),
         axiWriteSlave  => axilWriteSlaves(VERSION_INDEX_C),
         -- Optional: User Reset
         userReset      => cardResetOut,
         -- Optional: user values
         userValues     => userValues);

   ---------------------------------
   -- Map the AXI-Lite to DMA Engine
   ---------------------------------
   dmaCtrlWriteMasters(1)         <= axilWriteMasters(AXIS_MON_IB_C);
   axilWriteSlaves(AXIS_MON_IB_C) <= dmaCtrlWriteSlaves(1);
   dmaCtrlReadMasters(1)          <= axilReadMasters(AXIS_MON_IB_C);
   axilReadSlaves(AXIS_MON_IB_C)  <= dmaCtrlReadSlaves(1);

   dmaCtrlWriteMasters(2)         <= axilWriteMasters(AXIS_MON_OB_C);
   axilWriteSlaves(AXIS_MON_OB_C) <= dmaCtrlWriteSlaves(2);
   dmaCtrlReadMasters(2)          <= axilReadMasters(AXIS_MON_OB_C);
   axilReadSlaves(AXIS_MON_OB_C)  <= dmaCtrlReadSlaves(2);

   ----------------------------------
   -- Map the AXI-Lite to Application
   ----------------------------------
   U_AxiLiteAsync : entity surf.AxiLiteAsync
      generic map (
         TPD_G           => TPD_G,
         COMMON_CLK_G    => false,
         NUM_ADDR_BITS_G => 32)
      port map (
         -- Slave Interface
         sAxiClk         => axiClk,
         sAxiClkRst      => axiRst,
         sAxiReadMaster  => axilReadMasters(APP_INDEX_C),
         sAxiReadSlave   => axilReadSlaves(APP_INDEX_C),
         sAxiWriteMaster => axilWriteMasters(APP_INDEX_C),
         sAxiWriteSlave  => axilWriteSlaves(APP_INDEX_C),
         -- Master Interface
         mAxiClk         => appClk,
         mAxiClkRst      => appReset,
         mAxiReadMaster  => appReadMaster,
         mAxiReadSlave   => appReadSlave,
         mAxiWriteMaster => appWriteMaster,
         mAxiWriteSlave  => appWriteSlave);

   appReset <= cardResetIn or appRst;

   U_AppResetSync : entity surf.Synchronizer
      port map (
         clk     => axiClk,
         dataIn  => appReset,
         dataOut => appResetSync);

   U_appClkFreq : entity surf.SyncClockFreq
      generic map (
         TPD_G          => TPD_G,
         REF_CLK_FREQ_G => DMA_CLK_FREQ_C,
         REFRESH_RATE_G => 1.0,
         CNT_WIDTH_G    => 32)
      port map (
         -- Frequency Measurement (locClk domain)
         freqOut => appClkFreq,
         -- Clocks
         clkIn   => appClk,
         locClk  => axiClk,
         refClk  => axiClk);

end mapping;
