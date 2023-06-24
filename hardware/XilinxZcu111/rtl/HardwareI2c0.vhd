-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: PL Hardware I2c0 Module
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
use surf.I2cPkg.all;
use surf.I2cMuxPkg.all;

library unisim;
use unisim.vcomponents.all;

entity HardwareI2c0 is
   generic (
      TPD_G            : time := 1 ns;
      AXIL_CLK_FREQ_G  : real;
      AXIL_BASE_ADDR_G : slv(31 downto 0));
   port (
      --------------------------
      --       Ports
      --------------------------
      i2c0Scl         : inout sl;
      i2c0Sda         : inout sl;
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
end HardwareI2c0;

architecture mapping of HardwareI2c0 is

   constant TCA6416A_CONFIG_C : I2cAxiLiteDevArray(0 downto 0) := (
      0              => MakeI2cAxiLiteDevType(
         i2cAddress  => "0100000",      -- TCA6416A
         dataSize    => 8,              -- in units of bits
         addrSize    => 8,              -- in units of bits
         endianness  => '0',            -- Little endian
         repeatStart => '1'));          -- Repeat Start

begin

      U_TCA6416A : entity surf.AxiI2cRegMaster
         generic map (
            TPD_G          => TPD_G,
            I2C_SCL_FREQ_G => 100.0E+3,  -- units of Hz
            DEVICE_MAP_G   => TCA6416A_CONFIG_C,
            AXI_CLK_FREQ_G => AXIL_CLK_FREQ_G)
         port map (
            -- I2C Ports
            scl            => i2c0Scl,
            sda            => i2c0Sda,
            -- AXI-Lite Register Interface
            axiReadMaster  => axilReadMaster,
            axiReadSlave   => axilReadSlave,
            axiWriteMaster => axilWriteMaster,
            axiWriteSlave  => axilWriteSlave,
            -- Clocks and Resets
            axiClk         => axilClk,
            axiRst         => axilRst);

end mapping;
