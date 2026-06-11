-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: PL Hardware Module
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
use surf.I2cPkg.all;
use surf.I2cMuxPkg.all;

library ruckus;
use ruckus.BuildInfoPkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.HardwareTypePkg.all;

library unisim;
use unisim.vcomponents.all;

library axi_soc_ultra_plus_core;

entity Hardware is
   generic (
      TPD_G           : time := 1 ns;
      AXIL_CLK_FREQ_G : real);
   port (
      --------------------------
      --       Ports
      --------------------------
      -- LMK Ports
      lmkCsL          : out   sl;
      lmkSck          : out   sl;
      lmkSdi          : out   sl;       -- lmkSdio
      lmkSdo          : in    sl;       -- lmkRst(GPO)
      -- LMX Ports
      lmxCsL          : out   slv(1 downto 0);
      lmxSck          : out   slv(1 downto 0);
      lmxSdi          : out   slv(1 downto 0);
      lmxSdo          : in    slv(1 downto 0);
      -- Crossbar Ports
      xBarSin         : out   slv(1 downto 0);
      xBarSout        : out   slv(1 downto 0);
      xBarConfig      : out   sl;
      xBarLoad        : out   sl;
      -- IPMC Ports
      ipmcScl         : inout sl;
      ipmcSda         : inout sl;
      -- Misc. Ports
      muxRstL         : out   sl;
      muxScl          : inout sl;
      muxSda          : inout sl;
      plDone          : out   sl;
      --------------------------
      --       Interfaces
      --------------------------
      -- Slave AXI-Lite Interface
      axilClk         : in    sl;
      axilRst         : in    sl;
      axilWriteMaster : in    AxiLiteWriteMasterType;
      axilWriteSlave  : out   AxiLiteWriteSlaveType;
      axilReadMaster  : in    AxiLiteReadMasterType;
      axilReadSlave   : out   AxiLiteReadSlaveType);
end Hardware;

architecture mapping of Hardware is

   constant AXIL_CLK_PERIOD_C : real := (1.0/AXIL_CLK_FREQ_G);

   constant LMK_INDEX_C     : natural := 0;
   constant XBAR_INDEX_C    : natural := 1;
   constant LMX0_INDEX_C    : natural := 2;
   constant LMX1_INDEX_C    : natural := 3;
   constant MUX_I2C_INDEX_C : natural := 4;
   constant IPMC_INDEX_C    : natural := 5;

   constant NUM_AXI_MASTERS_C : natural := 6;

   constant AXI_CROSSBAR_MASTERS_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_AXI_MASTERS_C-1 downto 0) := (
      LMK_INDEX_C     => (
         baseAddr     => LMK_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"),
      XBAR_INDEX_C    => (
         baseAddr     => XBAR_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"),
      LMX0_INDEX_C    => (
         baseAddr     => LMX0_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"),
      LMX1_INDEX_C    => (
         baseAddr     => LMX1_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"),
      MUX_I2C_INDEX_C => (
         baseAddr     => MUX_I2C_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"),
      IPMC_INDEX_C    => (
         baseAddr     => IPMC_ADDR_C,
         addrBits     => 24,
         connectivity => x"FFFF"));

   signal mAxilWriteMasters : AxiLiteWriteMasterArray(NUM_AXI_MASTERS_C-1 downto 0);
   signal mAxilWriteSlaves  : AxiLiteWriteSlaveArray(NUM_AXI_MASTERS_C-1 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);
   signal mAxilReadMasters  : AxiLiteReadMasterArray(NUM_AXI_MASTERS_C-1 downto 0);
   signal mAxilReadSlaves   : AxiLiteReadSlaveArray(NUM_AXI_MASTERS_C-1 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);

   signal i2cReadMasters  : AxiLiteReadMasterArray(7 downto 0);
   signal i2cReadSlaves   : AxiLiteReadSlaveArray(7 downto 0)  := (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal i2cWriteMasters : AxiLiteWriteMasterArray(7 downto 0);
   signal i2cWriteSlaves  : AxiLiteWriteSlaveArray(7 downto 0) := (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

   signal i2ci : i2c_in_type;
   signal i2coVec : i2c_out_array(8 downto 0) := (
      others    => (
         scl    => '1',
         scloen => '1',
         sda    => '1',
         sdaoen => '1',
         enable => '0'));
   signal i2co : i2c_out_type;

begin

   U_plDone : entity surf.PwrUpRst
      generic map (
         TPD_G          => TPD_G,
         IN_POLARITY_G  => '1',
         OUT_POLARITY_G => '0')
      port map (
         clk    => axilClk,
         arst   => axilRst,
         rstOut => plDone);  -- Emulate the IPMC/shelf-manager interaction when pizza box configuration

   --------------------------
   -- AXI-Lite: Crossbar Core
   --------------------------
   U_XBAR : entity surf.AxiLiteCrossbar
      generic map (
         TPD_G              => TPD_G,
         NUM_SLAVE_SLOTS_G  => 1,
         NUM_MASTER_SLOTS_G => NUM_AXI_MASTERS_C,
         MASTERS_CONFIG_G   => AXI_CROSSBAR_MASTERS_CONFIG_C)
      port map (
         axiClk              => axilClk,
         axiClkRst           => axilRst,
         sAxiWriteMasters(0) => axilWriteMaster,
         sAxiWriteSlaves(0)  => axilWriteSlave,
         sAxiReadMasters(0)  => axilReadMaster,
         sAxiReadSlaves(0)   => axilReadSlave,
         mAxiWriteMasters    => mAxilWriteMasters,
         mAxiWriteSlaves     => mAxilWriteSlaves,
         mAxiReadMasters     => mAxilReadMasters,
         mAxiReadSlaves      => mAxilReadSlaves);

   -----------------------
   -- AXI-Lite: LMK Module
   -----------------------
   U_LMK : entity surf.AxiSpiMaster
      generic map (
         TPD_G             => TPD_G,
         ADDRESS_SIZE_G    => 15,
         DATA_SIZE_G       => 8,
         CLK_PERIOD_G      => AXIL_CLK_PERIOD_C,
         SPI_SCLK_PERIOD_G => (1.0/10.0E+3))
      port map (
         axiClk         => axilClk,
         axiRst         => axilRst,
         axiReadMaster  => mAxilReadMasters(LMK_INDEX_C),
         axiReadSlave   => mAxilReadSlaves(LMK_INDEX_C),
         axiWriteMaster => mAxilWriteMasters(LMK_INDEX_C),
         axiWriteSlave  => mAxilWriteSlaves(LMK_INDEX_C),
         coreSclk       => lmkSck,
         coreSDin       => lmkSdo,
         coreSDout      => lmkSdi,
         coreCsb        => lmkCsL);

   ----------------------------------
   -- AXI-Lite: Clock Crossbar Module
   ----------------------------------
   U_Sy56040 : entity surf.AxiSy56040Reg
      generic map (
         TPD_G          => TPD_G,
         XBAR_DEFAULT_G => XBAR_APP_NODE_C,
         AXI_CLK_FREQ_G => AXIL_CLK_FREQ_G)
      port map (
         -- XBAR Ports
         xBarSin        => xBarSin,
         xBarSout       => xBarSout,
         xBarConfig     => xBarConfig,
         xBarLoad       => xBarLoad,
         -- AXI-Lite Register Interface
         axiReadMaster  => mAxilReadMasters(XBAR_INDEX_C),
         axiReadSlave   => mAxilReadSlaves(XBAR_INDEX_C),
         axiWriteMaster => mAxilWriteMasters(XBAR_INDEX_C),
         axiWriteSlave  => mAxilWriteSlaves(XBAR_INDEX_C),
         -- Clocks and Resets
         axiClk         => axilClk,
         axiRst         => axilRst);

   ------------------------
   -- AXI-Lite: LMX Modules
   ------------------------
   GEN_LMX : for i in 1 downto 0 generate
      U_SPI : entity surf.AxiSpiMaster
         generic map (
            TPD_G             => TPD_G,
            ADDRESS_SIZE_G    => 7,
            DATA_SIZE_G       => 16,
            CLK_PERIOD_G      => AXIL_CLK_PERIOD_C,
            SPI_SCLK_PERIOD_G => (1.0/10.0E+3))
         port map (
            -- AXI-Lite Interface
            axiClk         => axilClk,
            axiRst         => axilRst,
            axiReadMaster  => mAxilReadMasters(LMX0_INDEX_C+i),
            axiReadSlave   => mAxilReadSlaves(LMX0_INDEX_C+i),
            axiWriteMaster => mAxilWriteMasters(LMX0_INDEX_C+i),
            axiWriteSlave  => mAxilWriteSlaves(LMX0_INDEX_C+i),
            -- SPI Ports
            coreSclk       => lmxSck(i),
            coreSDin       => lmxSdo(i),
            coreSDout      => lmxSdi(i),
            coreCsb        => lmxCsL(i));
   end generate GEN_LMX;

   ------------------------
   -- AXI-Lite: I2C Modules
   ------------------------
   U_XbarI2cMux : entity surf.AxiLiteCrossbarI2cMux
      generic map (
         TPD_G              => TPD_G,
         -- I2C MUX Generics
         MUX_DECODE_MAP_G   => I2C_MUX_DECODE_MAP_PCA9547_C,
         I2C_MUX_ADDR_G     => b"1110_000",
         I2C_SCL_FREQ_G     => I2C_SCL_FREQ_C,
         AXIL_CLK_FREQ_G    => AXIL_CLK_FREQ_G,
         -- AXI-Lite Crossbar Generics
         NUM_MASTER_SLOTS_G => 8,
         MASTERS_CONFIG_G   => XBAR_I2C_CONFIG_C)
      port map (
         -- Clocks and Resets
         axilClk           => axilClk,
         axilRst           => axilRst,
         -- Slave AXI-Lite Interface
         sAxilWriteMaster  => mAxilWriteMasters(MUX_I2C_INDEX_C),
         sAxilWriteSlave   => mAxilWriteSlaves(MUX_I2C_INDEX_C),
         sAxilReadMaster   => mAxilReadMasters(MUX_I2C_INDEX_C),
         sAxilReadSlave    => mAxilReadSlaves(MUX_I2C_INDEX_C),
         -- Master AXI-Lite Interfaces
         mAxilWriteMasters => i2cWriteMasters,
         mAxilWriteSlaves  => i2cWriteSlaves,
         mAxilReadMasters  => i2cReadMasters,
         mAxilReadSlaves   => i2cReadSlaves,
         -- I2C MUX Ports
         i2cRstL           => muxRstL,
         i2ci              => i2ci,
         i2co              => i2coVec(8));

   GEN_DDR : for i in 1 downto 0 generate
      U_I2C : entity surf.AxiI2cRegMasterCore
         generic map (
            TPD_G          => TPD_G,
            I2C_SCL_FREQ_G => I2C_SCL_FREQ_C,
            DEVICE_MAP_G   => DDR_DEVICE_MAP_C,
            AXI_CLK_FREQ_G => AXIL_CLK_FREQ_G)
         port map (
            -- I2C Ports
            i2ci           => i2ci,
            i2co           => i2coVec(i),
            -- AXI-Lite Register Interface
            axiReadMaster  => i2cReadMasters(i),
            axiReadSlave   => i2cReadSlaves(i),
            axiWriteMaster => i2cWriteMasters(i),
            axiWriteSlave  => i2cWriteSlaves(i),
            -- Clocks and Resets
            axiClk         => axilClk,
            axiRst         => axilRst);
   end generate GEN_DDR;

   U_PWR_I2C : entity surf.AxiLitePMbusMasterCore
      generic map (
         TPD_G          => TPD_G,
         I2C_ADDR_G     => b"0101_000",
         I2C_SCL_FREQ_G => I2C_SCL_FREQ_C,
         AXI_CLK_FREQ_G => AXIL_CLK_FREQ_G)
      port map (
         -- I2C Ports
         i2ci            => i2ci,
         i2co            => i2coVec(4),
         -- AXI-Lite Register Interface
         axilReadMaster  => i2cReadMasters(4),
         axilReadSlave   => i2cReadSlaves(4),
         axilWriteMaster => i2cWriteMasters(4),
         axilWriteSlave  => i2cWriteSlaves(4),
         -- Clocks and Resets
         axilClk         => axilClk,
         axilRst         => axilRst);

   U_GPIO_I2C : entity surf.AxiI2cRegMasterCore
      generic map (
         TPD_G          => TPD_G,
         I2C_SCL_FREQ_G => I2C_SCL_FREQ_C,
         DEVICE_MAP_G   => GPIO_DEVICE_MAP_C,
         AXI_CLK_FREQ_G => AXIL_CLK_FREQ_G)
      port map (
         -- I2C Ports
         i2ci           => i2ci,
         i2co           => i2coVec(7),
         -- AXI-Lite Register Interface
         axiReadMaster  => i2cReadMasters(7),
         axiReadSlave   => i2cReadSlaves(7),
         axiWriteMaster => i2cWriteMasters(7),
         axiWriteSlave  => i2cWriteSlaves(7),
         -- Clocks and Resets
         axiClk         => axilClk,
         axiRst         => axilRst);

   process(i2cReadMasters, i2cWriteMasters, i2coVec)
      variable tmp : i2c_out_type;
   begin
      -- Init
      tmp := i2coVec(8);
      -- Check for TXN after XBAR/I2C_MUX
      for i in 0 to 7 loop
         if (i2cWriteMasters(i).awvalid = '1') or (i2cReadMasters(i).arvalid = '1') then
            tmp := i2coVec(i);
         end if;
      end loop;
      -- Return result
      i2co <= tmp;
   end process;

   IOBUF_SCL : IOBUF
      port map (
         O  => i2ci.scl,
         IO => muxScl,
         I  => i2co.scl,
         T  => i2co.scloen);

   IOBUF_SDA : IOBUF
      port map (
         O  => i2ci.sda,
         IO => muxSda,
         I  => i2co.sda,
         T  => i2co.sdaoen);

   -----------------------
   -- AXI-Lite: BSI Module
   -----------------------
   U_Bsi : entity axi_soc_ultra_plus_core.RfmcCarrierBsi
      generic map (
         TPD_G        => TPD_G,
         BUILD_INFO_G => BUILD_INFO_C)
      port map (
         -- I2C Ports
         scl             => ipmcScl,
         sda             => ipmcSda,
         -- AXI-Lite Register Interface
         axilReadMaster  => mAxilReadMasters(IPMC_INDEX_C),
         axilReadSlave   => mAxilReadSlaves(IPMC_INDEX_C),
         axilWriteMaster => mAxilWriteMasters(IPMC_INDEX_C),
         axilWriteSlave  => mAxilWriteSlaves(IPMC_INDEX_C),
         -- Clocks and Resets
         axilClk         => axilClk,
         axilRst         => axilRst);

end mapping;
