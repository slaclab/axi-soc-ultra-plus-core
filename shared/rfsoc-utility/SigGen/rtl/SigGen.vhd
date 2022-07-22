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
      TPD_G              : time     := 1 ns;
      NUM_CH_G           : positive := 1;
      RAM_ADDR_WIDTH_G   : positive := 10;
      SAMPLE_PER_CYCLE_G : positive := 16;
      AXIL_BASE_ADDR_G   : slv(31 downto 0));
   port (
      -- DAC Interface (dspClk domain)
      dspClk          : in  sl;
      dspRst          : in  sl;
      dspDacIn        : in  Slv256Array(NUM_CH_G-1 downto 0);
      dspDacOut       : out Slv256Array(NUM_CH_G-1 downto 0);
      sigGenActive    : out sl;         -- Use to sync with other modules
      -- AXI-Lite Interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType);
end SigGen;

architecture mapping of SigGen is

   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_CH_G downto 0) := genAxiLiteConfig(NUM_CH_G+1, AXIL_BASE_ADDR_G, 21, 16);

   signal axilReadMasters  : AxiLiteReadMasterArray(NUM_CH_G downto 0);
   signal axilReadSlaves   : AxiLiteReadSlaveArray(NUM_CH_G downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteMasters : AxiLiteWriteMasterArray(NUM_CH_G downto 0);
   signal axilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_CH_G downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal ramAddr : slv(RAM_ADDR_WIDTH_G-1 downto 0);
   signal ramData : Slv256Array(NUM_CH_G-1 downto 0) := (others => (others => '0'));

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

   GEN_VEC :
   for i in NUM_CH_G-1 downto 0 generate
      U_Mem : entity surf.AxiDualPortRam
         generic map (
            TPD_G          => TPD_G,
            SYNTH_MODE_G   => "xpm",
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
            axiClk         => axilClk,
            axiRst         => axilRst,
            axiReadMaster  => axilReadMasters(i+1),
            axiReadSlave   => axilReadSlaves(i+1),
            axiWriteMaster => axilWriteMasters(i+1),
            axiWriteSlave  => axilWriteSlaves(i+1));
   end generate GEN_VEC;

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

   U_Fsm : entity axi_soc_ultra_plus_core.SigGenFsm
      generic map (
         TPD_G              => TPD_G,
         NUM_CH_G           => NUM_CH_G,
         RAM_ADDR_WIDTH_G   => RAM_ADDR_WIDTH_G,
         SAMPLE_PER_CYCLE_G => SAMPLE_PER_CYCLE_G)
      port map (
         -- Clock and Reset
         dspClk    => dspClk,
         dspRst    => dspRst,
         -- Control/Status Interface
         config    => config,
         status    => status,
         -- Memory Interface
         ramAddr   => ramAddr,
         ramData   => ramData,
         -- DAC Interface
         dspDacIn  => dspDacIn,
         dspDacOut => dspDacOut);

end mapping;
