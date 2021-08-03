-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Wrapper for AXI SOC Core
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
use surf.AxiPkg.all;
use surf.AxiLitePkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

entity AxiSocUltraPlusCpu is
   generic (
      TPD_G : time := 1 ns);
   port (
      -- Clock and Reset
      axiClk         : out sl;
      axiRst         : out sl;
      -- Slave AXI4 Interface
      dmaReadMaster  : in  AxiReadMasterType;
      dmaReadSlave   : out AxiReadSlaveType;
      dmaWriteMaster : in  AxiWriteMasterType;
      dmaWriteSlave  : out AxiWriteSlaveType;
      -- Master AXI-Lite Interface
      regReadMaster  : out AxiLiteReadMasterType;
      regReadSlave   : in  AxiLiteReadSlaveType;
      regWriteMaster : out AxiLiteWriteMasterType;
      regWriteSlave  : in  AxiLiteWriteSlaveType;
      -- Interrupt Interface
      dmaIrq         : in  sl);
end AxiSocUltraPlusCpu;

architecture mapping of AxiSocUltraPlusCpu is

   component AxiSocUltraPlusCpuCore is
      port (
         dmaClk          : in  std_logic;
         dmaIrq          : in  std_logic;
         dmaRstL         : in  std_logic;
         plClk           : out std_logic;
         plRstL          : out std_logic;
         axiLite_awaddr  : out std_logic_vector (39 downto 0);
         axiLite_awprot  : out std_logic_vector (2 downto 0);
         axiLite_awvalid : out std_logic;
         axiLite_awready : in  std_logic;
         axiLite_wdata   : out std_logic_vector (31 downto 0);
         axiLite_wstrb   : out std_logic_vector (3 downto 0);
         axiLite_wvalid  : out std_logic;
         axiLite_wready  : in  std_logic;
         axiLite_bresp   : in  std_logic_vector (1 downto 0);
         axiLite_bvalid  : in  std_logic;
         axiLite_bready  : out std_logic;
         axiLite_araddr  : out std_logic_vector (39 downto 0);
         axiLite_arprot  : out std_logic_vector (2 downto 0);
         axiLite_arvalid : out std_logic;
         axiLite_arready : in  std_logic;
         axiLite_rdata   : in  std_logic_vector (31 downto 0);
         axiLite_rresp   : in  std_logic_vector (1 downto 0);
         axiLite_rvalid  : in  std_logic;
         axiLite_rready  : out std_logic;
         dma_aruser      : in  std_logic;
         dma_awuser      : in  std_logic;
         dma_awid        : in  std_logic_vector (5 downto 0);
         dma_awaddr      : in  std_logic_vector (48 downto 0);
         dma_awlen       : in  std_logic_vector (7 downto 0);
         dma_awsize      : in  std_logic_vector (2 downto 0);
         dma_awburst     : in  std_logic_vector (1 downto 0);
         dma_awlock      : in  std_logic;
         dma_awcache     : in  std_logic_vector (3 downto 0);
         dma_awprot      : in  std_logic_vector (2 downto 0);
         dma_awvalid     : in  std_logic;
         dma_awready     : out std_logic;
         dma_wdata       : in  std_logic_vector (127 downto 0);
         dma_wstrb       : in  std_logic_vector (15 downto 0);
         dma_wlast       : in  std_logic;
         dma_wvalid      : in  std_logic;
         dma_wready      : out std_logic;
         dma_bid         : out std_logic_vector (5 downto 0);
         dma_bresp       : out std_logic_vector (1 downto 0);
         dma_bvalid      : out std_logic;
         dma_bready      : in  std_logic;
         dma_arid        : in  std_logic_vector (5 downto 0);
         dma_araddr      : in  std_logic_vector (48 downto 0);
         dma_arlen       : in  std_logic_vector (7 downto 0);
         dma_arsize      : in  std_logic_vector (2 downto 0);
         dma_arburst     : in  std_logic_vector (1 downto 0);
         dma_arlock      : in  std_logic;
         dma_arcache     : in  std_logic_vector (3 downto 0);
         dma_arprot      : in  std_logic_vector (2 downto 0);
         dma_arvalid     : in  std_logic;
         dma_arready     : out std_logic;
         dma_rid         : out std_logic_vector (5 downto 0);
         dma_rdata       : out std_logic_vector (127 downto 0);
         dma_rresp       : out std_logic_vector (1 downto 0);
         dma_rlast       : out std_logic;
         dma_rvalid      : out std_logic;
         dma_rready      : in  std_logic;
         dma_awqos       : in  std_logic_vector (3 downto 0);
         dma_arqos       : in  std_logic_vector (3 downto 0));
   end component AxiSocUltraPlusCpuCore;

   signal dummyByte : Slv8Array(1 downto 0);

   signal plClk  : sl;
   signal plRstL : sl;

   signal dmaClk  : sl;
   signal dmaRst  : sl;
   signal dmaRstL : sl;

begin

   axiClk <= dmaClk;
   U_Rst : entity surf.RstPipeline
      generic map (
         TPD_G     => TPD_G,
         INV_RST_G => true)
      port map (
         clk    => dmaClk,
         rstIn  => dmaRstL,
         rstOut => axiRst);

   -------------------
   -- AXI SOC IP Core
   -------------------
   U_CPU : AxiSocUltraPlusCpuCore
      port map (
         -- AXI-Lite Interface
         axiLite_araddr(31 downto 0)  => regReadMaster.araddr,
         axiLite_araddr(39 downto 32) => dummyByte(0),
         axiLite_arprot               => regReadMaster.arprot,
         axiLite_arvalid              => regReadMaster.arvalid,
         axiLite_rready               => regReadMaster.rready,
         axiLite_arready              => regReadSlave.arready,
         axiLite_rdata                => regReadSlave.rdata,
         axiLite_rresp                => AXI_RESP_OK_C,  -- Always respond OK
         axiLite_rvalid               => regReadSlave.rvalid,
         axiLite_awaddr(31 downto 0)  => regWriteMaster.awaddr,
         axiLite_awaddr(39 downto 32) => dummyByte(1),
         axiLite_awprot               => regWriteMaster.awprot,
         axiLite_awvalid              => regWriteMaster.awvalid,
         axiLite_wdata                => regWriteMaster.wdata,
         axiLite_wstrb                => regWriteMaster.wstrb,
         axiLite_wvalid               => regWriteMaster.wvalid,
         axiLite_bready               => regWriteMaster.bready,
         axiLite_awready              => regWriteSlave.awready,
         axiLite_wready               => regWriteSlave.wready,
         axiLite_bresp                => AXI_RESP_OK_C,  -- Always respond OK
         axiLite_bvalid               => regWriteSlave.bvalid,
         -- DMA Interface
         dmaClk                       => dmaClk,
         dmaIrq                       => dmaIrq,
         dmaRstL                      => dmaRstL,
         dma_araddr(48 downto 0)      => dmaReadMaster.araddr(48 downto 0),
         dma_arburst(1 downto 0)      => dmaReadMaster.arburst(1 downto 0),
         dma_arcache(3 downto 0)      => dmaReadMaster.arcache(3 downto 0),
         dma_arid(5 downto 0)         => dmaReadMaster.arid(5 downto 0),
         dma_arlen(7 downto 0)        => dmaReadMaster.arlen(AXI_SOC_CONFIG_C.LEN_BITS_C-1 downto 0),
         dma_arlock                   => '0',
         dma_arprot(2 downto 0)       => dmaReadMaster.arprot(2 downto 0),
         dma_arqos(3 downto 0)        => dmaReadMaster.arqos(3 downto 0),
         dma_arready                  => dmaReadSlave.arready,
         dma_arsize(2 downto 0)       => dmaReadMaster.arsize(2 downto 0),
         dma_aruser                   => '0',
         dma_arvalid                  => dmaReadMaster.arvalid,
         dma_awaddr(48 downto 0)      => dmaWriteMaster.awaddr(48 downto 0),
         dma_awburst(1 downto 0)      => dmaWriteMaster.awburst(1 downto 0),
         dma_awcache(3 downto 0)      => dmaWriteMaster.awcache(3 downto 0),
         dma_awid(5 downto 0)         => dmaWriteMaster.awid(5 downto 0),
         dma_awlen(7 downto 0)        => dmaWriteMaster.awlen(AXI_SOC_CONFIG_C.LEN_BITS_C-1 downto 0),
         dma_awlock                   => '0',
         dma_awprot(2 downto 0)       => dmaWriteMaster.awprot(2 downto 0),
         dma_awqos(3 downto 0)        => dmaWriteMaster.awqos(3 downto 0),
         dma_awready                  => dmaWriteSlave.awready,
         dma_awsize(2 downto 0)       => dmaWriteMaster.awsize(2 downto 0),
         dma_awuser                   => '0',
         dma_awvalid                  => dmaWriteMaster.awvalid,
         dma_bid(5 downto 0)          => dmaWriteSlave.bid(5 downto 0),
         dma_bready                   => dmaWriteMaster.bready,
         dma_bresp(1 downto 0)        => dmaWriteSlave.bresp(1 downto 0),
         dma_bvalid                   => dmaWriteSlave.bvalid,
         dma_rdata(127 downto 0)      => dmaReadSlave.rdata(8*AXI_SOC_CONFIG_C.DATA_BYTES_C-1 downto 0),
         dma_rid(5 downto 0)          => dmaReadSlave.rid(5 downto 0),
         dma_rlast                    => dmaReadSlave.rlast,
         dma_rready                   => dmaReadMaster.rready,
         dma_rresp(1 downto 0)        => dmaReadSlave.rresp(1 downto 0),
         dma_rvalid                   => dmaReadSlave.rvalid,
         dma_wdata(127 downto 0)      => dmaWriteMaster.wdata(8*AXI_SOC_CONFIG_C.DATA_BYTES_C-1 downto 0),
         dma_wlast                    => dmaWriteMaster.wlast,
         dma_wready                   => dmaWriteSlave.wready,
         dma_wstrb(15 downto 0)       => dmaWriteMaster.wstrb(AXI_SOC_CONFIG_C.DATA_BYTES_C-1 downto 0),
         dma_wvalid                   => dmaWriteMaster.wvalid,
         -- Reference Clock and reset
         plClk                        => plClk,
         plRstL                       => plRstL);

   U_Pll : entity surf.ClockManagerUltraScale
      generic map(
         TPD_G             => TPD_G,
         TYPE_G            => "PLL",
         INPUT_BUFG_G      => true,
         FB_BUFG_G         => true,
         RST_IN_POLARITY_G => '0',      -- Active LOW reset
         NUM_CLOCKS_G      => 1,
         -- MMCM attributes
         CLKIN_PERIOD_G    => 4.0,      -- 250 MHz
         CLKFBOUT_MULT_G   => 5,        -- 1.25 GHz = 5 x 250 MHz
         CLKOUT0_DIVIDE_G  => 5)        -- 250 MHz = 1.25GHz/5
      port map(
         -- Clock Input
         clkIn     => plClk,
         rstIn     => plRstL,
         -- Clock Outputs
         clkOut(0) => dmaClk,
         -- Reset Outputs
         rstOut(0) => dmaRst);

   dmaRstL <= not(dmaRst);

end mapping;
