-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: SigGen Module
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
use axi_soc_ultra_plus_core.SigGenPkg.all;

entity SigGen is
   generic (
      TPD_G              : time                   := 1 ns;
      NUM_CH_G           : positive               := 1;
      RAM_ADDR_WIDTH_G   : positive range 9 to 12 := 10;
      SAMPLE_PER_CYCLE_G : positive               := 16;
      AXIL_BASE_ADDR_G   : slv(31 downto 0));
   port (
      -- DAC Interface (dspClk domain)
      dspClk          : in  sl;
      dspRst          : in  sl;
      dspDacIn0       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn1       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn2       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn3       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn4       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn5       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn6       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn7       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn8       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn9       : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn10      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn11      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn12      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn13      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn14      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacIn15      : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      dspDacOut0      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut1      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut2      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut3      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut4      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut5      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut6      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut7      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut8      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut9      : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut10     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut11     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut12     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut13     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut14     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut15     : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      sigGenActive    : out sl;         -- Use to sync with other modules
      extTrigIn       : in  sl                                                     := '0';
      extTrigOut      : out sl;
      -- AXI-Lite Interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType);
end SigGen;

architecture mapping of SigGen is

   type SigGenArray is array (natural range <>) of slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);

   constant USE_URAM_C : boolean := (RAM_ADDR_WIDTH_G > 10);

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_CH_G downto 0) := genAxiLiteConfig(NUM_CH_G+1, AXIL_BASE_ADDR_G, 23, 18);

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_CH_G downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_CH_G downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_CH_G downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_CH_G downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal syncClk          : sl;
   signal syncRst          : sl;
   signal syncReadMasters  : AxiLiteReadMasterArray(NUM_CH_G downto 1);
   signal syncReadSlaves   : AxiLiteReadSlaveArray(NUM_CH_G downto 1)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal syncWriteMasters : AxiLiteWriteMasterArray(NUM_CH_G downto 1);
   signal syncWriteSlaves  : AxiLiteWriteSlaveArray(NUM_CH_G downto 1) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal ramAddr : slv(RAM_ADDR_WIDTH_G-1 downto 0);
   signal ramData : SigGenArray(15 downto 0) := (others => (others => '0'));

   signal config : SigGenConfigType := SIG_GEN_CONFIG_INIT_C;
   signal status : SigGenStatusType := SIG_GEN_STATUS_INIT_C;

begin

   sigGenActive <= status.dacGenValid;

   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => (NUM_CH_G+1),
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

   U_Reg : entity axi_soc_ultra_plus_core.SigGenReg
      generic map (
         TPD_G              => TPD_G,
         NUM_CH_G           => NUM_CH_G,
         RAM_ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
         SAMPLE_PER_CYCLE_G => SAMPLE_PER_CYCLE_G)
      port map (
         -- Control/Status Interface (dataClk domain)
         dspClk          => dspClk,
         dspRst          => dspRst,
         config          => config,
         status          => status,
         -- AXI-Lite Interface (axilClk domain)
         axilClk         => axilClk,
         axilRst         => axilRst,
         axilReadMaster  => axilReadMasters(0),
         axilReadSlave   => axilReadSlaves(0),
         axilWriteMaster => axilWriteMasters(0),
         axilWriteSlave  => axilWriteSlaves(0));

   GEN_VEC :
   for i in NUM_CH_G-1 downto 0 generate

      U_AxiLiteAsync : entity surf.AxiLiteAsync
         generic map (
            TPD_G           => TPD_G,
            COMMON_CLK_G    => not(USE_URAM_C),
            NUM_ADDR_BITS_G => 18)
         port map (
            -- Slave Interface
            sAxiClk         => axilClk,
            sAxiClkRst      => axilRst,
            sAxiReadMaster  => axilReadMasters(i+1),
            sAxiReadSlave   => axilReadSlaves(i+1),
            sAxiWriteMaster => axilWriteMasters(i+1),
            sAxiWriteSlave  => axilWriteSlaves(i+1),
            -- Master Interface
            mAxiClk         => syncClk,
            mAxiClkRst      => syncRst,
            mAxiReadMaster  => syncReadMasters(i+1),
            mAxiReadSlave   => syncReadSlaves(i+1),
            mAxiWriteMaster => syncWriteMasters(i+1),
            mAxiWriteSlave  => syncWriteSlaves(i+1));

      syncClk <= dspClk when(USE_URAM_C) else axilClk;
      syncRst <= dspRst when(USE_URAM_C) else axilRst;

      U_Mem : entity surf.AxiDualPortRam
         generic map (
            TPD_G          => TPD_G,
            SYNTH_MODE_G   => "xpm",
            MEMORY_TYPE_G  => ite(USE_URAM_C, "ultra", "block"),
            COMMON_CLK_G   => USE_URAM_C,
            READ_LATENCY_G => 2,
            ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
            DATA_WIDTH_G   => (SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G))  -- units of bits
         port map (
            -- RAM Interface (dataClk domain)
            clk            => dspClk,
            rst            => dspRst,
            addr           => ramAddr,
            dout           => ramData(i)(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0),
            -- AXI-Lite interface (axilClk domain)
            axiClk         => syncClk,
            axiRst         => syncRst,
            axiReadMaster  => syncReadMasters(i+1),
            axiReadSlave   => syncReadSlaves(i+1),
            axiWriteMaster => syncWriteMasters(i+1),
            axiWriteSlave  => syncWriteSlaves(i+1));

   end generate GEN_VEC;

   U_Fsm : entity axi_soc_ultra_plus_core.SigGenFsm
      generic map (
         TPD_G              => TPD_G,
         RAM_ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
         SAMPLE_PER_CYCLE_G => SAMPLE_PER_CYCLE_G)
      port map (
         -- Clock and Reset
         dspClk      => dspClk,
         dspRst      => dspRst,
         -- Control/Status Interface
         config      => config,
         status      => status,
         -- External Trigger Interface
         extTrigIn   => extTrigIn,
         extTrigOut  => extTrigOut,
         -- Memory Interface
         ramAddr     => ramAddr,
         ramData0    => ramData(0),
         ramData1    => ramData(1),
         ramData2    => ramData(2),
         ramData3    => ramData(3),
         ramData4    => ramData(4),
         ramData5    => ramData(5),
         ramData6    => ramData(6),
         ramData7    => ramData(7),
         ramData8    => ramData(8),
         ramData9    => ramData(9),
         ramData10   => ramData(10),
         ramData11   => ramData(11),
         ramData12   => ramData(12),
         ramData13   => ramData(13),
         ramData14   => ramData(14),
         ramData15   => ramData(15),
         -- DAC Interface
         dspDacIn0   => dspDacIn0,
         dspDacIn1   => dspDacIn1,
         dspDacIn2   => dspDacIn2,
         dspDacIn3   => dspDacIn3,
         dspDacIn4   => dspDacIn4,
         dspDacIn5   => dspDacIn5,
         dspDacIn6   => dspDacIn6,
         dspDacIn7   => dspDacIn7,
         dspDacIn8   => dspDacIn8,
         dspDacIn9   => dspDacIn9,
         dspDacIn10  => dspDacIn10,
         dspDacIn11  => dspDacIn11,
         dspDacIn12  => dspDacIn12,
         dspDacIn13  => dspDacIn13,
         dspDacIn14  => dspDacIn14,
         dspDacIn15  => dspDacIn15,
         dspDacOut0  => dspDacOut0,
         dspDacOut1  => dspDacOut1,
         dspDacOut2  => dspDacOut2,
         dspDacOut3  => dspDacOut3,
         dspDacOut4  => dspDacOut4,
         dspDacOut5  => dspDacOut5,
         dspDacOut6  => dspDacOut6,
         dspDacOut7  => dspDacOut7,
         dspDacOut8  => dspDacOut8,
         dspDacOut9  => dspDacOut9,
         dspDacOut10 => dspDacOut10,
         dspDacOut11 => dspDacOut11,
         dspDacOut12 => dspDacOut12,
         dspDacOut13 => dspDacOut13,
         dspDacOut14 => dspDacOut14,
         dspDacOut15 => dspDacOut15);

end mapping;
