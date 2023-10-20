-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: AppRingBufferEngine Module
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

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

entity AppRingBufferEngine is
   generic (
      TPD_G              : time                   := 1 ns;
      NUM_CH_G           : positive               := 8;
      SAMPLE_PER_CYCLE_G : positive               := 16;
      RAM_ADDR_WIDTH_G   : positive               := 10;
      MEMORY_TYPE_G      : string                 := "block";
      COMMON_CLK_G       : boolean                := false;  -- true if dataClk=axilClk
      FIFO_MEMORY_TYPE_G : string                 := "block";
      FIFO_ADDR_WIDTH_G  : positive               := 9;
      TDEST_ROUTES_G     : Slv8Array(15 downto 0) := (others => x"00");
      AXIL_BASE_ADDR_G   : slv(31 downto 0));
   port (
      -- AXI-Stream Interface (axisClk domain)
      axisClk         : in  sl;
      axisRst         : in  sl;
      axisMaster      : out AxiStreamMasterType;
      axisSlave       : in  AxiStreamSlaveType;
      -- DATA Interface (dataClk domain)
      dataClk         : in  sl;
      dataRst         : in  sl;
      data0           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data1           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data2           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data3           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data4           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data5           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data6           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data7           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data8           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data9           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data10          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data11          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data12          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data13          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data14          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      data15          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      extTrigIn       : in  sl                                    := '0';
      -- AXI-Lite Interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType);
end AppRingBufferEngine;

architecture mapping of AppRingBufferEngine is

   type RingBuffArray is array (natural range <>) of slv(16*SAMPLE_PER_CYCLE_G-1 downto 0);

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_CH_G-1 downto 0) := genAxiLiteConfig(NUM_CH_G, AXIL_BASE_ADDR_G, 16, 12);

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_CH_G-1 downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_CH_G-1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_CH_G-1 downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_CH_G-1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal axisMasters : AxiStreamMasterArray(NUM_CH_G-1 downto 0);
   signal axisSlaves  : AxiStreamSlaveArray(NUM_CH_G-1 downto 0);

   signal dataValues : RingBuffArray(15 downto 0);

begin

   dataValues(0)  <= data0;
   dataValues(1)  <= data1;
   dataValues(2)  <= data2;
   dataValues(3)  <= data3;
   dataValues(4)  <= data4;
   dataValues(5)  <= data5;
   dataValues(6)  <= data6;
   dataValues(7)  <= data7;
   dataValues(8)  <= data8;
   dataValues(9)  <= data9;
   dataValues(10) <= data10;
   dataValues(11) <= data11;
   dataValues(12) <= data12;
   dataValues(13) <= data13;
   dataValues(14) <= data14;
   dataValues(15) <= data15;

   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => NUM_CH_G,
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

   GEN_VEC :
   for i in NUM_CH_G-1 downto 0 generate
      U_AxiStreamRingBuffer : entity surf.AxiStreamRingBuffer
         generic map (
            TPD_G               => TPD_G,
            SYNTH_MODE_G        => "xpm",
            MEMORY_TYPE_G       => MEMORY_TYPE_G,
            COMMON_CLK_G        => COMMON_CLK_G,
            DATA_BYTES_G        => ((16*SAMPLE_PER_CYCLE_G)/8),
            RAM_ADDR_WIDTH_G    => RAM_ADDR_WIDTH_G,
            -- AXI Stream Configurations
            FIFO_MEMORY_TYPE_G  => FIFO_MEMORY_TYPE_G,
            FIFO_ADDR_WIDTH_G   => FIFO_ADDR_WIDTH_G,
            GEN_SYNC_FIFO_G     => false,
            AXI_STREAM_CONFIG_G => DMA_AXIS_CONFIG_C)
         port map (
            -- Data to store in ring buffer (dataClk domain)
            dataClk         => dataClk,
            dataValue       => dataValues(i),
            extTrig         => extTrigIn,
            -- AXI-Lite interface (axilClk domain)
            axilClk         => axilClk,
            axilRst         => axilRst,
            axilReadMaster  => axilReadMasters(i),
            axilReadSlave   => axilReadSlaves(i),
            axilWriteMaster => axilWriteMasters(i),
            axilWriteSlave  => axilWriteSlaves(i),
            -- AXI-Stream Interface (axilClk domain)
            axisClk         => axisClk,
            axisRst         => axisRst,
            axisMaster      => axisMasters(i),
            axisSlave       => axisSlaves(i));
   end generate GEN_VEC;

   U_Mux : entity surf.AxiStreamMux
      generic map (
         TPD_G          => TPD_G,
         NUM_SLAVES_G   => NUM_CH_G,
         MODE_G         => "ROUTED",
         TDEST_ROUTES_G => TDEST_ROUTES_G(NUM_CH_G-1 downto 0),
         PIPE_STAGES_G  => 1)
      port map (
         -- Clock and reset
         axisClk      => axisClk,
         axisRst      => axisRst,
         -- Slaves
         sAxisMasters => axisMasters,
         sAxisSlaves  => axisSlaves,
         -- Master
         mAxisMaster  => axisMaster,
         mAxisSlave   => axisSlave);

end mapping;
