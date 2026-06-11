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

library surf;
use surf.StdRtlPkg.all;

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.AxiSocUltraPlusPkg.all;

package HardwareTypePkg is

   constant HW_TYPE_C : slv(31 downto 0) := HW_TYPE_SLAC_RFMC_CARRIER_C;

   ---------------------------------------------
   -- Register Mapping: 1st Layer base addresses
   ---------------------------------------------
   constant VERSION_ADDR_C : slv(31 downto 0) := x"00000000";
   constant SYSMON_ADDR_C  : slv(31 downto 0) := x"01000000";
   constant LMK_ADDR_C     : slv(31 downto 0) := x"02000000";
   constant XBAR_ADDR_C    : slv(31 downto 0) := x"03000000";
   constant LMX0_ADDR_C    : slv(31 downto 0) := x"04000000";
   constant LMX1_ADDR_C    : slv(31 downto 0) := x"05000000";
   constant MUX_I2C_ADDR_C : slv(31 downto 0) := x"06000000";
   constant IPMC_ADDR_C    : slv(31 downto 0) := x"07000000";
   constant TIMING_ADDR_C  : slv(31 downto 0) := x"08000000";
   constant BSA_ADDR_C     : slv(31 downto 0) := X"09000000";
   constant ETH_ADDR_C     : slv(31 downto 0) := x"0A000000";
   constant DDR_ADDR_C     : slv(31 downto 0) := x"0B000000";
   constant MPS_ADDR_C     : slv(31 downto 0) := x"0C000000";
   constant APP_ADDR_C     : slv(31 downto 0) := x"80000000";

   constant XBAR_TIME_GEN_C : Slv2Array(3 downto 0) := (
      3 => "01",                        -- OUT[3] = IN[1], DIST1 = FPGA
      2 => "01",                        -- OUT[2] = IN[1], DIST0 = FPGA
      1 => "01",   -- OUT[1] = IN[1], FPGA  = FPGA (loopback)
      0 => "01");                       -- OUT[0] = IN[1], RTM0  = FPGA

   constant XBAR_MPS_II_LINK_C : Slv2Array(3 downto 0) := (
      3 => "11",   -- OUT[3] = IN[3], DIST1 = RTM1 (LCLS-II)
      2 => "11",   -- OUT[2] = IN[3], DIST0 = RTM1 (LCLS-II)
      1 => "11",   -- OUT[1] = IN[3], FPGA  = RTM1 (LCLS-II)
      0 => "00");  -- OUT[0] = IN[0], RTM0  = RTM0 (loopback)

   constant XBAR_MPS_I_LINK_C : Slv2Array(3 downto 0) := (
      3 => "00",   -- OUT[3] = IN[0], DIST1 = RTM0 (LCLS-I)
      2 => "00",   -- OUT[2] = IN[0], DIST0 = RTM0 (LCLS-I)
      1 => "00",   -- OUT[1] = IN[0], FPGA  = RTM0 (LCLS-I)
      0 => "00");  -- OUT[0] = IN[0], RTM0  = RTM0 (loopback)

   constant XBAR_APP_NODE_C : Slv2Array(3 downto 0) := (
      3 => "00",                        -- OUT[3] = IN[0], DIST1 = RTM0
      2 => "00",                        -- OUT[2] = IN[0], DIST0 = RTM0
      1 => "10",                        -- OUT[1] = IN[2], FPGA  = backplane
      0 => "00");  -- OUT[0] = IN[0], RTM0  = RTM0 (loopback)

   function xbarDefault(app : AppType; mpsLinkNode : boolean) return Slv2Array;

   constant I2C_SCL_FREQ_C : real := 400.0E+3;

   constant XBAR_I2C_CONFIG_C : AxiLiteCrossbarMasterConfigArray(7 downto 0) := genAxiLiteConfig(8, MUX_I2C_ADDR_C, 20, 16);

   constant DDR_DEVICE_MAP_C : I2cAxiLiteDevArray(0 to 0) := (
      0             => MakeI2cAxiLiteDevType(
         i2cAddress => "1010000",  -- SRD Memory (1010) (Lookup tool at www.micron.com/spd)
         dataSize   => 8,               -- in units of bits
         addrSize   => 8,               -- in units of bits
         endianness => '1'));           -- Big endian

   constant GPIO_DEVICE_MAP_C : I2cAxiLiteDevArray(0 to 0) := (
      0              => MakeI2cAxiLiteDevType(
         i2cAddress  => "0100000",      -- PCA9555
         dataSize    => 8,              -- in units of bits
         addrSize    => 8,              -- in units of bits
         endianness  => '0',            -- Little endian
         repeatStart => '1'));          -- Repeat Start

end package HardwareTypePkg;

package body HardwareTypePkg is

   function xbarDefault (app : AppType; mpsLinkNode : boolean) return Slv2Array is
      variable retVar : Slv2Array(3 downto 0);
   begin
      if (app = APP_TIME_GEN_TYPE_C) then
         retVar := XBAR_TIME_GEN_C;
      elsif (mpsLinkNode = true) then
         retVar := XBAR_MPS_II_LINK_C;
      else
         retVar := XBAR_APP_NODE_C;
      end if;
      return retVar;
   end function;

end package body HardwareTypePkg;
