-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: AppRingBuffer Module
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
use surf.SsiPkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

entity AppRingBuffer is
   generic (
      TPD_G                  : time     := 1 ns;
      EN_ADC_BUFF_G          : boolean  := true;
      EN_DAC_BUFF_G          : boolean  := true;
      NUM_ADC_CH_G           : positive := 1;
      NUM_DAC_CH_G           : positive := 1;
      ADC_SAMPLE_PER_CYCLE_G : positive := 16;
      DAC_SAMPLE_PER_CYCLE_G : positive := 16;
      RAM_ADDR_WIDTH_G       : positive := 9;
      AXIL_BASE_ADDR_G       : slv(31 downto 0));
   port (
      -- DMA Interface (dmaClk domain)
      dmaClk          : in  sl;
      dmaRst          : in  sl;
      dmaIbMaster     : out AxiStreamMasterType;
      dmaIbSlave      : in  AxiStreamSlaveType;
      -- ADC/DAC Interface (dspClk domain)
      dspClk          : in  sl;
      dspRst          : in  sl;
      dspAdc0         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc1         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc2         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc3         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc4         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc5         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc6         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc7         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc8         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc9         : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc10        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc11        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc12        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc13        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc14        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspAdc15        : in  slv(16*ADC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac0         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac1         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac2         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac3         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac4         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac5         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac6         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac7         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac8         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac9         : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac10        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac11        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac12        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac13        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac14        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDac15        : in  slv(16*DAC_SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      extTrigIn       : in  sl                                        := '0';
      -- AXI-Lite Interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType);
end AppRingBuffer;

architecture mapping of AppRingBuffer is

   constant ADC_RING_INDEX_C   : natural := 0;
   constant DAC_RING_INDEX_C   : natural := 1;
   constant RATE_LIMIT_INDEX_C : natural := 2;

   constant NUM_AXIL_MASTERS_C : natural := 3;

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_AXIL_MASTERS_C-1 downto 0) := genAxiLiteConfig(NUM_AXIL_MASTERS_C, AXIL_BASE_ADDR_G, 20, 16);

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_AXIL_MASTERS_C-1 downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_AXIL_MASTERS_C-1 downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal axisMasters : AxiStreamMasterArray(1 downto 0) := (others => AXI_STREAM_MASTER_INIT_C);
   signal axisSlaves  : AxiStreamSlaveArray(1 downto 0)  := (others => AXI_STREAM_SLAVE_FORCE_C);

   signal axisMaster : AxiStreamMasterType := AXI_STREAM_MASTER_INIT_C;
   signal axisSlave  : AxiStreamSlaveType  := AXI_STREAM_SLAVE_FORCE_C;

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

   GEN_ADC_BUFF : if (EN_ADC_BUFF_G) generate
      U_AdcRingBuffer : entity axi_soc_ultra_plus_core.AppRingBufferEngine
         generic map (
            TPD_G              => TPD_G,
            TDEST_ROUTES_G     => (
               0               => x"00",
               1               => x"01",
               2               => x"02",
               3               => x"03",
               4               => x"04",
               5               => x"05",
               6               => x"06",
               7               => x"07",
               8               => x"08",
               9               => x"09",
               10              => x"0A",
               11              => x"0B",
               12              => x"0C",
               13              => x"0D",
               14              => x"0E",
               15              => x"0F"),
            NUM_CH_G           => NUM_ADC_CH_G,
            SAMPLE_PER_CYCLE_G => ADC_SAMPLE_PER_CYCLE_G,
            RAM_ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
            AXIL_BASE_ADDR_G   => AXIL_CONFIG_C(ADC_RING_INDEX_C).baseAddr)
         port map (
            -- AXI-Stream Interface (axisClk domain)
            axisClk         => dmaClk,
            axisRst         => dmaRst,
            axisMaster      => axisMasters(0),
            axisSlave       => axisSlaves(0),
            -- DATA Interface (dataClk domain)
            dataClk         => dspClk,
            dataRst         => dspRst,
            data0           => dspAdc0,
            data1           => dspAdc1,
            data2           => dspAdc2,
            data3           => dspAdc3,
            data4           => dspAdc4,
            data5           => dspAdc5,
            data6           => dspAdc6,
            data7           => dspAdc7,
            data8           => dspAdc8,
            data9           => dspAdc9,
            data10          => dspAdc10,
            data11          => dspAdc11,
            data12          => dspAdc12,
            data13          => dspAdc13,
            data14          => dspAdc14,
            data15          => dspAdc15,
            extTrigIn       => extTrigIn,
            -- AXI-Lite Interface (axilClk domain)
            axilClk         => axilClk,
            axilRst         => axilRst,
            axilReadMaster  => axilReadMasters(ADC_RING_INDEX_C),
            axilReadSlave   => axilReadSlaves(ADC_RING_INDEX_C),
            axilWriteMaster => axilWriteMasters(ADC_RING_INDEX_C),
            axilWriteSlave  => axilWriteSlaves(ADC_RING_INDEX_C));
   end generate;

   GEN_DAC_BUFF : if (EN_DAC_BUFF_G) generate
      U_DacRingBuffer : entity axi_soc_ultra_plus_core.AppRingBufferEngine
         generic map (
            TPD_G              => TPD_G,
            TDEST_ROUTES_G     => (
               0               => x"10",
               1               => x"11",
               2               => x"12",
               3               => x"13",
               4               => x"14",
               5               => x"15",
               6               => x"16",
               7               => x"17",
               8               => x"18",
               9               => x"19",
               10              => x"1A",
               11              => x"1B",
               12              => x"1C",
               13              => x"1D",
               14              => x"1E",
               15              => x"1F"),
            NUM_CH_G           => NUM_DAC_CH_G,
            SAMPLE_PER_CYCLE_G => DAC_SAMPLE_PER_CYCLE_G,
            RAM_ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
            AXIL_BASE_ADDR_G   => AXIL_CONFIG_C(DAC_RING_INDEX_C).baseAddr)
         port map (
            -- AXI-Stream Interface (axisClk domain)
            axisClk         => dmaClk,
            axisRst         => dmaRst,
            axisMaster      => axisMasters(1),
            axisSlave       => axisSlaves(1),
            -- DATA Interface (dataClk domain)
            dataClk         => dspClk,
            dataRst         => dspRst,
            data0           => dspDac0,
            data1           => dspDac1,
            data2           => dspDac2,
            data3           => dspDac3,
            data4           => dspDac4,
            data5           => dspDac5,
            data6           => dspDac6,
            data7           => dspDac7,
            data8           => dspDac8,
            data9           => dspDac9,
            data10          => dspDac10,
            data11          => dspDac11,
            data12          => dspDac12,
            data13          => dspDac13,
            data14          => dspDac14,
            data15          => dspDac15,
            extTrigIn       => extTrigIn,
            -- AXI-Lite Interface (axilClk domain)
            axilClk         => axilClk,
            axilRst         => axilRst,
            axilReadMaster  => axilReadMasters(DAC_RING_INDEX_C),
            axilReadSlave   => axilReadSlaves(DAC_RING_INDEX_C),
            axilWriteMaster => axilWriteMasters(DAC_RING_INDEX_C),
            axilWriteSlave  => axilWriteSlaves(DAC_RING_INDEX_C));
   end generate;

   U_Mux : entity surf.AxiStreamMux
      generic map (
         TPD_G         => TPD_G,
         NUM_SLAVES_G  => 2,
         MODE_G        => "PASSTHROUGH",
         PIPE_STAGES_G => 1)
      port map (
         -- Clock and reset
         axisClk      => dmaClk,
         axisRst      => dmaRst,
         -- Slaves
         sAxisMasters => axisMasters,
         sAxisSlaves  => axisSlaves,
         -- Master
         mAxisMaster  => axisMaster,
         mAxisSlave   => axisSlave);

   U_RateLimiter : entity surf.AxiStreamFrameRateLimiter
      generic map (
         TPD_G              => TPD_G,
         AXIS_CLK_FREQ_G    => DMA_CLK_FREQ_C,
         DEFAULT_MAX_RATE_G => 2*(NUM_ADC_CH_G+NUM_DAC_CH_G))  -- 2 Hz per channel
      port map (
         -- AXI Stream Interface (axisClk domain)
         axisClk         => dmaClk,
         axisRst         => dmaRst,
         sAxisMaster     => axisMaster,
         sAxisSlave      => axisSlave,
         mAxisMaster     => dmaIbMaster,
         mAxisSlave      => dmaIbSlave,
         -- AXI-Lite Interface (axilClk domain)
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilReadMaster  => axilReadMasters(RATE_LIMIT_INDEX_C),
         axilReadSlave   => axilReadSlaves(RATE_LIMIT_INDEX_C),
         axilWriteMaster => axilWriteMasters(RATE_LIMIT_INDEX_C),
         axilWriteSlave  => axilWriteSlaves(RATE_LIMIT_INDEX_C));

end mapping;
