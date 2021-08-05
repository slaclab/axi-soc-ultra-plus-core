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

library surf;
use surf.StdRtlPkg.all;
use surf.AxiLitePkg.all;

entity AxiSocUltraPlusSysMon is
   generic (
      TPD_G : time := 1 ns);
   port (
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

   component AxiSocUltraPlusSysMonCore
      port (
         s_axi_aclk    : in  std_logic;
         s_axi_aresetn : in  std_logic;
         s_axi_awaddr  : in  std_logic_vector(12 downto 0);
         s_axi_awvalid : in  std_logic;
         s_axi_awready : out std_logic;
         s_axi_wdata   : in  std_logic_vector(31 downto 0);
         s_axi_wstrb   : in  std_logic_vector(3 downto 0);
         s_axi_wvalid  : in  std_logic;
         s_axi_wready  : out std_logic;
         s_axi_bresp   : out std_logic_vector(1 downto 0);
         s_axi_bvalid  : out std_logic;
         s_axi_bready  : in  std_logic;
         s_axi_araddr  : in  std_logic_vector(12 downto 0);
         s_axi_arvalid : in  std_logic;
         s_axi_arready : out std_logic;
         s_axi_rdata   : out std_logic_vector(31 downto 0);
         s_axi_rresp   : out std_logic_vector(1 downto 0);
         s_axi_rvalid  : out std_logic;
         s_axi_rready  : in  std_logic;
         ip2intc_irpt  : out std_logic;
         vp            : in  std_logic;
         vn            : in  std_logic;
         ot_out        : out std_logic;
         channel_out   : out std_logic_vector(5 downto 0);
         eoc_out       : out std_logic;
         alarm_out     : out std_logic;
         eos_out       : out std_logic;
         busy_out      : out std_logic);
   end component;

   signal axilRstL : sl;

begin

   axilRstL <= not axilRst;

   SysMonCore_Inst : AxiSocUltraPlusSysMonCore
      port map (
         s_axi_aclk    => axilClk,
         s_axi_aresetn => axilRstL,
         s_axi_awaddr  => axilWriteMaster.awaddr(12 downto 0),
         s_axi_awvalid => axilWriteMaster.awvalid,
         s_axi_awready => axilWriteSlave.awready,
         s_axi_wdata   => axilWriteMaster.wdata,
         s_axi_wstrb   => axilWriteMaster.wstrb,
         s_axi_wvalid  => axilWriteMaster.wvalid,
         s_axi_wready  => axilWriteSlave.wready,
         s_axi_bresp   => axilWriteSlave.bresp,
         s_axi_bvalid  => axilWriteSlave.bvalid,
         s_axi_bready  => axilWriteMaster.bready,
         s_axi_araddr  => axilReadMaster.araddr(12 downto 0),
         s_axi_arvalid => axilReadMaster.arvalid,
         s_axi_arready => axilReadSlave.arready,
         s_axi_rdata   => axilReadSlave.rdata,
         s_axi_rresp   => axilReadSlave.rresp,
         s_axi_rvalid  => axilReadSlave.rvalid,
         s_axi_rready  => axilReadMaster.rready,
         ip2intc_irpt  => open,
         vp            => vPIn,
         vn            => vNIn,
         ot_out        => open,
         channel_out   => open,
         eoc_out       => open,
         alarm_out     => open,
         eos_out       => open,
         busy_out      => open);

end architecture mapping;
