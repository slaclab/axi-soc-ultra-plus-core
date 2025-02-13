-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Wrapper for the DDR4 MIG IP core
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

entity MigCoreWrapper is
   generic (
      TPD_G : time := 1 ns);
   port (
      extRst         : in    sl;
      -- AXI4 Interface
      ddrClk         : out   sl;
      ddrRst         : out   sl;
      ddrReady       : out   sl;
      ddrWriteMaster : in    AxiWriteMasterType;
      ddrWriteSlave  : out   AxiWriteSlaveType;
      ddrReadMaster  : in    AxiReadMasterType;
      ddrReadSlave   : out   AxiReadSlaveType;
      ----------------
      -- Core Ports --
      ----------------
      -- DDR4 Ports
      ddrClkP        : in    sl;
      ddrClkN        : in    sl;
      ddrDm          : inout slv(7 downto 0);
      ddrDqsP        : inout slv(7 downto 0);
      ddrDqsN        : inout slv(7 downto 0);
      ddrDq          : inout slv(63 downto 0);
      ddrA           : out   slv(16 downto 0);
      ddrBa          : out   slv(1 downto 0);
      ddrCsL         : out   slv(0 downto 0);
      ddrOdt         : out   slv(0 downto 0);
      ddrCke         : out   slv(0 downto 0);
      ddrCkP         : out   slv(0 downto 0);
      ddrCkN         : out   slv(0 downto 0);
      ddrBg          : out   slv(0 downto 0);
      ddrActL        : out   sl;
      ddrRstL        : out   sl);
end MigCoreWrapper;

architecture mapping of MigCoreWrapper is

   component MigCore
      port (
         c0_init_calib_complete  : out   std_logic;
         dbg_clk                 : out   std_logic;
         c0_sys_clk_p            : in    std_logic;
         c0_sys_clk_n            : in    std_logic;
         dbg_bus                 : out   std_logic_vector(511 downto 0);
         c0_ddr4_adr             : out   std_logic_vector(16 downto 0);
         c0_ddr4_ba              : out   std_logic_vector(1 downto 0);
         c0_ddr4_cke             : out   std_logic_vector(0 downto 0);
         c0_ddr4_cs_n            : out   std_logic_vector(0 downto 0);
         c0_ddr4_dm_dbi_n        : inout std_logic_vector(7 downto 0);
         c0_ddr4_dq              : inout std_logic_vector(63 downto 0);
         c0_ddr4_dqs_c           : inout std_logic_vector(7 downto 0);
         c0_ddr4_dqs_t           : inout std_logic_vector(7 downto 0);
         c0_ddr4_odt             : out   std_logic_vector(0 downto 0);
         c0_ddr4_bg              : out   std_logic_vector(0 downto 0);
         c0_ddr4_reset_n         : out   std_logic;
         c0_ddr4_act_n           : out   std_logic;
         c0_ddr4_ck_c            : out   std_logic_vector(0 downto 0);
         c0_ddr4_ck_t            : out   std_logic_vector(0 downto 0);
         c0_ddr4_ui_clk          : out   std_logic;
         c0_ddr4_ui_clk_sync_rst : out   std_logic;
         c0_ddr4_aresetn         : in    std_logic;
         c0_ddr4_s_axi_awid      : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_awaddr    : in    std_logic_vector(31 downto 0);
         c0_ddr4_s_axi_awlen     : in    std_logic_vector(7 downto 0);
         c0_ddr4_s_axi_awsize    : in    std_logic_vector(2 downto 0);
         c0_ddr4_s_axi_awburst   : in    std_logic_vector(1 downto 0);
         c0_ddr4_s_axi_awlock    : in    std_logic_vector(0 downto 0);
         c0_ddr4_s_axi_awcache   : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_awprot    : in    std_logic_vector(2 downto 0);
         c0_ddr4_s_axi_awqos     : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_awvalid   : in    std_logic;
         c0_ddr4_s_axi_awready   : out   std_logic;
         c0_ddr4_s_axi_wdata     : in    std_logic_vector(511 downto 0);
         c0_ddr4_s_axi_wstrb     : in    std_logic_vector(63 downto 0);
         c0_ddr4_s_axi_wlast     : in    std_logic;
         c0_ddr4_s_axi_wvalid    : in    std_logic;
         c0_ddr4_s_axi_wready    : out   std_logic;
         c0_ddr4_s_axi_bready    : in    std_logic;
         c0_ddr4_s_axi_bid       : out   std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_bresp     : out   std_logic_vector(1 downto 0);
         c0_ddr4_s_axi_bvalid    : out   std_logic;
         c0_ddr4_s_axi_arid      : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_araddr    : in    std_logic_vector(31 downto 0);
         c0_ddr4_s_axi_arlen     : in    std_logic_vector(7 downto 0);
         c0_ddr4_s_axi_arsize    : in    std_logic_vector(2 downto 0);
         c0_ddr4_s_axi_arburst   : in    std_logic_vector(1 downto 0);
         c0_ddr4_s_axi_arlock    : in    std_logic_vector(0 downto 0);
         c0_ddr4_s_axi_arcache   : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_arprot    : in    std_logic_vector(2 downto 0);
         c0_ddr4_s_axi_arqos     : in    std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_arvalid   : in    std_logic;
         c0_ddr4_s_axi_arready   : out   std_logic;
         c0_ddr4_s_axi_rready    : in    std_logic;
         c0_ddr4_s_axi_rlast     : out   std_logic;
         c0_ddr4_s_axi_rvalid    : out   std_logic;
         c0_ddr4_s_axi_rresp     : out   std_logic_vector(1 downto 0);
         c0_ddr4_s_axi_rid       : out   std_logic_vector(3 downto 0);
         c0_ddr4_s_axi_rdata     : out   std_logic_vector(511 downto 0);
         addn_ui_clkout1         : out   std_logic;
         sys_rst                 : in    std_logic
         );
   end component;

   signal ddrClock : sl;
   signal ddrReset : sl;
   signal extRstL  : sl;

begin

   ddrClk <= ddrClock;
   U_axiRst : entity surf.RstPipeline
      generic map (
         TPD_G => TPD_G)
      port map (
         clk    => ddrClock,
         rstIn  => ddrReset,
         rstOut => ddrRst);

   extRstL <= not(extRst);

   U_MigCore : MigCore
      port map (
         dbg_clk                 => open,
         dbg_bus                 => open,
         c0_init_calib_complete  => ddrReady,
         c0_sys_clk_p            => ddrClkP,
         c0_sys_clk_n            => ddrClkN,
         c0_ddr4_adr             => ddrA,
         c0_ddr4_ba              => ddrBa,
         c0_ddr4_cke             => ddrCke,
         c0_ddr4_cs_n            => ddrCsL,
         c0_ddr4_dm_dbi_n        => ddrDm,
         c0_ddr4_dq              => ddrDq,
         c0_ddr4_dqs_c           => ddrDqsN,
         c0_ddr4_dqs_t           => ddrDqsP,
         c0_ddr4_odt             => ddrOdt,
         c0_ddr4_bg              => ddrBg,
         c0_ddr4_reset_n         => ddrRstL,
         c0_ddr4_act_n           => ddrActL,
         c0_ddr4_ck_c            => ddrCkN,
         c0_ddr4_ck_t            => ddrCkP,
         c0_ddr4_ui_clk          => ddrClock,
         c0_ddr4_ui_clk_sync_rst => ddrReset,
         c0_ddr4_aresetn         => extRstL,
         c0_ddr4_s_axi_awid      => ddrWriteMaster.awid(3 downto 0),
         c0_ddr4_s_axi_awaddr    => ddrWriteMaster.awaddr(31 downto 0),
         c0_ddr4_s_axi_awlen     => ddrWriteMaster.awlen(7 downto 0),
         c0_ddr4_s_axi_awsize    => ddrWriteMaster.awsize(2 downto 0),
         c0_ddr4_s_axi_awburst   => ddrWriteMaster.awburst(1 downto 0),
         c0_ddr4_s_axi_awlock    => ddrWriteMaster.awlock(0 downto 0),
         c0_ddr4_s_axi_awcache   => ddrWriteMaster.awcache(3 downto 0),
         c0_ddr4_s_axi_awprot    => ddrWriteMaster.awprot(2 downto 0),
         c0_ddr4_s_axi_awqos     => ddrWriteMaster.awqos(3 downto 0),
         c0_ddr4_s_axi_awvalid   => ddrWriteMaster.awvalid,
         c0_ddr4_s_axi_awready   => ddrWriteSlave.awready,
         c0_ddr4_s_axi_wdata     => ddrWriteMaster.wdata(511 downto 0),
         c0_ddr4_s_axi_wstrb     => ddrWriteMaster.wstrb(63 downto 0),
         c0_ddr4_s_axi_wlast     => ddrWriteMaster.wlast,
         c0_ddr4_s_axi_wvalid    => ddrWriteMaster.wvalid,
         c0_ddr4_s_axi_wready    => ddrWriteSlave.wready,
         c0_ddr4_s_axi_bready    => ddrWriteMaster.bready,
         c0_ddr4_s_axi_bid       => ddrWriteSlave.bid(3 downto 0),
         c0_ddr4_s_axi_bresp     => ddrWriteSlave.bresp(1 downto 0),
         c0_ddr4_s_axi_bvalid    => ddrWriteSlave.bvalid,
         c0_ddr4_s_axi_arid      => ddrReadMaster.arid(3 downto 0),
         c0_ddr4_s_axi_araddr    => ddrReadMaster.araddr(31 downto 0),
         c0_ddr4_s_axi_arlen     => ddrReadMaster.arlen(7 downto 0),
         c0_ddr4_s_axi_arsize    => ddrReadMaster.arsize(2 downto 0),
         c0_ddr4_s_axi_arburst   => ddrReadMaster.arburst(1 downto 0),
         c0_ddr4_s_axi_arlock    => ddrReadMaster.arlock(0 downto 0),
         c0_ddr4_s_axi_arcache   => ddrReadMaster.arcache(3 downto 0),
         c0_ddr4_s_axi_arprot    => ddrReadMaster.arprot(2 downto 0),
         c0_ddr4_s_axi_arqos     => ddrReadMaster.arqos(3 downto 0),
         c0_ddr4_s_axi_arvalid   => ddrReadMaster.arvalid,
         c0_ddr4_s_axi_arready   => ddrReadSlave.arready,
         c0_ddr4_s_axi_rready    => ddrReadMaster.rready,
         c0_ddr4_s_axi_rlast     => ddrReadSlave.rlast,
         c0_ddr4_s_axi_rvalid    => ddrReadSlave.rvalid,
         c0_ddr4_s_axi_rresp     => ddrReadSlave.rresp(1 downto 0),
         c0_ddr4_s_axi_rid       => ddrReadSlave.rid(3 downto 0),
         c0_ddr4_s_axi_rdata     => ddrReadSlave.rdata(511 downto 0),
         sys_rst                 => extRst);

end mapping;
