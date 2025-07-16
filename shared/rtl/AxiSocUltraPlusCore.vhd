-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
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
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

library surf;
use surf.StdRtlPkg.all;
use surf.AxiLitePkg.all;
use surf.AxiStreamPkg.all;
use surf.AxiPkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

entity AxiSocUltraPlusCore is
   generic (
      TPD_G                    : time                        := 1 ns;
      ROGUE_SIM_EN_G           : boolean                     := false;
      ROGUE_SIM_PORT_NUM_G     : natural range 1024 to 49151 := 10000;
      ROGUE_SIM_CH_COUNT_G     : natural range 1 to 256      := 256;
      BUILD_INFO_G             : BuildInfoType;
      EXT_AXIL_MASTER_G        : boolean                     := false;
      EN_DEVICE_DNA_G          : boolean                     := true;
      SYSMON_ENABLE_G          : boolean                     := true;
      SYSMON_LVAUX_THRESHOLD_G : slv(15 downto 0)            := x"FFFF";
      DESC_MEMORY_TYPE_G       : string                      := "ultra";
      DMA_BURST_BYTES_G        : positive range 256 to 4096  := 256;
      DMA_SIZE_G               : positive range 1 to 8       := 1);
   port (
      ------------------------
      --  Top Level Interfaces
      ------------------------
      -- DSP Clock and Reset Monitoring
      dspClk          : in  sl;
      dspRst          : in  sl;
      -- AUX Clock and Reset
      auxClk          : out sl;         -- 100 MHz
      auxRst          : out sl;
      -- DMA Interfaces  (dmaClk domain)
      dmaClk          : out sl;         -- 250 MHz
      dmaRst          : out sl;
      dmaBuffGrpPause : out slv(7 downto 0);
      dmaObMasters    : out AxiStreamMasterArray(DMA_SIZE_G-1 downto 0);
      dmaObSlaves     : in  AxiStreamSlaveArray(DMA_SIZE_G-1 downto 0);
      dmaIbMasters    : in  AxiStreamMasterArray(DMA_SIZE_G-1 downto 0);
      dmaIbSlaves     : out AxiStreamSlaveArray(DMA_SIZE_G-1 downto 0);
      -- External AXI-Lite Interfaces  (dmaClk domain): EXT_AXIL_MASTER_G = true
      extReadMaster   : in  AxiLiteReadMasterType  := AXI_LITE_READ_MASTER_INIT_C;
      extReadSlave    : out AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_EMPTY_DECERR_C;
      extWriteMaster  : in  AxiLiteWriteMasterType := AXI_LITE_WRITE_MASTER_INIT_C;
      extWriteSlave   : out AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C;
      -- Application AXI-Lite Interfaces [0x80000000:0xFFFFFFFF] (appClk domain)
      appClk          : in  sl                     := '0';
      appRst          : in  sl                     := '1';
      appReadMaster   : out AxiLiteReadMasterType;
      appReadSlave    : in  AxiLiteReadSlaveType   := AXI_LITE_READ_SLAVE_EMPTY_DECERR_C;
      appWriteMaster  : out AxiLiteWriteMasterType;
      appWriteSlave   : in  AxiLiteWriteSlaveType  := AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C;
      -- User General Purpose AXI4 Interfaces (dmaClk domain)
      usrReadMaster   : in  AxiReadMasterType      := AXI_READ_MASTER_INIT_C;
      usrReadSlave    : out AxiReadSlaveType       := AXI_READ_SLAVE_FORCE_C;
      usrWriteMaster  : in  AxiWriteMasterType     := AXI_WRITE_MASTER_INIT_C;
      usrWriteSlave   : out AxiWriteSlaveType      := AXI_WRITE_SLAVE_FORCE_C;
      -- PMU Interface
      pmuErrorFromPl  : in  slv(3 downto 0)        := (others => '0');
      pmuErrorToPl    : out slv(46 downto 0);
      fanEnableL      : out sl;
      -- Over Temp or LVAUX Error Detect
      sysmonError     : out sl;
      -- SYSMON Ports
      vPIn            : in  sl;
      vNIn            : in  sl);
end AxiSocUltraPlusCore;

architecture mapping of AxiSocUltraPlusCore is

   signal dmaReadMaster  : AxiReadMasterType;
   signal dmaReadSlave   : AxiReadSlaveType;
   signal dmaWriteMaster : AxiWriteMasterType;
   signal dmaWriteSlave  : AxiWriteSlaveType;

   signal regReadMaster  : AxiLiteReadMasterType;
   signal regReadSlave   : AxiLiteReadSlaveType;
   signal regWriteMaster : AxiLiteWriteMasterType;
   signal regWriteSlave  : AxiLiteWriteSlaveType;

   signal dmaCtrlReadMasters  : AxiLiteReadMasterArray(2 downto 0);
   signal dmaCtrlReadSlaves   : AxiLiteReadSlaveArray(2 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal dmaCtrlWriteMasters : AxiLiteWriteMasterArray(2 downto 0);
   signal dmaCtrlWriteSlaves  : AxiLiteWriteSlaveArray(2 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal sysClock    : sl;
   signal sysReset    : sl;
   signal systemReset : slv(1 downto 0);
   signal auxClock    : sl;
   signal auxReset    : sl;
   signal cardReset   : sl;
   signal dmaIrq      : sl;

begin

   dmaClk <= sysClock;

   U_dmaRst : entity surf.RstPipeline
      generic map (
         TPD_G => TPD_G)
      port map (
         clk    => sysClock,
         rstIn  => systemReset(0),
         rstOut => dmaRst);

   systemReset(0) <= sysReset or cardReset;

   auxClk <= auxClock;

   U_auxRst : entity surf.RstSync
      generic map (
         TPD_G => TPD_G)
      port map (
         clk      => auxClock,
         asyncRst => systemReset(1),
         syncRst  => auxRst);

   systemReset(1) <= auxReset or cardReset;

   ----------
   -- AXI CPU
   ----------
   REAL_CPU : if (not ROGUE_SIM_EN_G) generate

      U_CPU : entity axi_soc_ultra_plus_core.AxiSocUltraPlusCpu
         generic map (
            TPD_G => TPD_G)
         port map (
            -- Clock and Reset
            axiClk             => sysClock,
            axiRst             => sysReset,
            auxClk             => auxClock,
            auxRst             => auxReset,
            -- Slave AXI4 Interface
            dmaReadMaster      => dmaReadMaster,
            dmaReadSlave       => dmaReadSlave,
            dmaWriteMaster     => dmaWriteMaster,
            dmaWriteSlave      => dmaWriteSlave,
            -- Master AXI-Lite Interface
            regReadMaster      => regReadMaster,
            regReadSlave       => regReadSlave,
            regWriteMaster     => regWriteMaster,
            regWriteSlave      => regWriteSlave,
            dmaCtrlReadMaster  => dmaCtrlReadMasters(0),
            dmaCtrlReadSlave   => dmaCtrlReadSlaves(0),
            dmaCtrlWriteMaster => dmaCtrlWriteMasters(0),
            dmaCtrlWriteSlave  => dmaCtrlWriteSlaves(0),
            -- PMU Interface
            pmuErrorFromPl     => pmuErrorFromPl,
            pmuErrorToPl       => pmuErrorToPl,
            fanEnableL         => fanEnableL,
            -- Interrupt Interface
            dmaIrq             => dmaIrq);

   end generate;

   SIM_CPU : if (ROGUE_SIM_EN_G) generate

      -- Generate local 250 MHz clock
      U_dmaClock : entity surf.ClkRst
         generic map (
            CLK_PERIOD_G      => 4 ns,  -- 250 MHz
            RST_START_DELAY_G => 0 ns,
            RST_HOLD_TIME_G   => 1000 ns)
         port map (
            clkP => sysClock,
            rst  => sysReset);

      -- Generate local 100 MHz clock
      U_auxClock : entity surf.ClkRst
         generic map (
            CLK_PERIOD_G      => 10 ns,  -- 100 MHz
            RST_START_DELAY_G => 0 ns,
            RST_HOLD_TIME_G   => 1000 ns)
         port map (
            clkP => auxClock,
            rst  => auxReset);

   end generate;

   ---------------
   -- AXI CPU REG
   ---------------
   U_REG : entity axi_soc_ultra_plus_core.AxiSocUltraPlusReg
      generic map (
         TPD_G                    => TPD_G,
         ROGUE_SIM_EN_G           => ROGUE_SIM_EN_G,
         ROGUE_SIM_PORT_NUM_G     => ROGUE_SIM_PORT_NUM_G,
         BUILD_INFO_G             => BUILD_INFO_G,
         EXT_AXIL_MASTER_G        => EXT_AXIL_MASTER_G,
         EN_DEVICE_DNA_G          => EN_DEVICE_DNA_G,
         SYSMON_ENABLE_G          => SYSMON_ENABLE_G,
         SYSMON_LVAUX_THRESHOLD_G => SYSMON_LVAUX_THRESHOLD_G,
         DMA_SIZE_G               => DMA_SIZE_G)
      port map (
         -- DSP Clock and Reset Monitoring
         dspClk              => dspClk,
         dspRst              => dspRst,
         -- AUX Clock and Reset
         auxClk              => auxClock,
         auxRst              => auxReset,
         -- Internal AXI4 Interfaces (axiClk domain)
         axiClk              => sysClock,
         axiRst              => sysReset,
         regReadMaster       => regReadMaster,
         regReadSlave        => regReadSlave,
         regWriteMaster      => regWriteMaster,
         regWriteSlave       => regWriteSlave,
         -- External AXI-Lite Interfaces  (axiClk domain): EXT_AXIL_MASTER_G = true
         extReadMaster       => extReadMaster,
         extReadSlave        => extReadSlave,
         extWriteMaster      => extWriteMaster,
         extWriteSlave       => extWriteSlave,
         -- DMA AXI-Lite Interfaces
         dmaCtrlReadMasters  => dmaCtrlReadMasters(2 downto 1),
         dmaCtrlReadSlaves   => dmaCtrlReadSlaves(2 downto 1),
         dmaCtrlWriteMasters => dmaCtrlWriteMasters(2 downto 1),
         dmaCtrlWriteSlaves  => dmaCtrlWriteSlaves(2 downto 1),
         -- (Optional) Application AXI-Lite Interfaces
         appClk              => appClk,
         appRst              => appRst,
         appReadMaster       => appReadMaster,
         appReadSlave        => appReadSlave,
         appWriteMaster      => appWriteMaster,
         appWriteSlave       => appWriteSlave,
         -- Application Force reset
         cardResetOut        => cardReset,
         cardResetIn         => systemReset(0),
         -- Over Temp or LVAUX Error Detect
         sysmonError         => sysmonError,
         -- SYSMON Ports
         vPIn                => vPIn,
         vNIn                => vNIn);

   --------------
   -- AXI SOC DMA
   --------------
   U_DMA : entity axi_soc_ultra_plus_core.AxiSocUltraPlusDma
      generic map (
         TPD_G                => TPD_G,
         ROGUE_SIM_EN_G       => ROGUE_SIM_EN_G,
         ROGUE_SIM_PORT_NUM_G => ROGUE_SIM_PORT_NUM_G,
         ROGUE_SIM_CH_COUNT_G => ROGUE_SIM_CH_COUNT_G,
         DESC_MEMORY_TYPE_G   => DESC_MEMORY_TYPE_G,
         DMA_SIZE_G           => DMA_SIZE_G,
         DMA_BURST_BYTES_G    => DMA_BURST_BYTES_G)
      port map (
         axiClk           => sysClock,
         axiRst           => sysReset,
         -- DMA AXI4 Interfaces (
         axiReadMaster    => dmaReadMaster,
         axiReadSlave     => dmaReadSlave,
         axiWriteMaster   => dmaWriteMaster,
         axiWriteSlave    => dmaWriteSlave,
         -- User General Purpose AXI4 Interfaces
         usrReadMaster    => usrReadMaster,
         usrReadSlave     => usrReadSlave,
         usrWriteMaster   => usrWriteMaster,
         usrWriteSlave    => usrWriteSlave,
         -- AXI4-Lite Interfaces
         axilReadMasters  => dmaCtrlReadMasters,
         axilReadSlaves   => dmaCtrlReadSlaves,
         axilWriteMasters => dmaCtrlWriteMasters,
         axilWriteSlaves  => dmaCtrlWriteSlaves,
         -- DMA Interfaces
         dmaIrq           => dmaIrq,
         dmaBuffGrpPause  => dmaBuffGrpPause,
         dmaObMasters     => dmaObMasters,
         dmaObSlaves      => dmaObSlaves,
         dmaIbMasters     => dmaIbMasters,
         dmaIbSlaves      => dmaIbSlaves);

end mapping;
