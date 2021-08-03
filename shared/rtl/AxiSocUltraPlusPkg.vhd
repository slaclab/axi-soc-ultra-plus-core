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
use surf.AxiLitePkg.all;
use surf.AxiStreamPkg.all;
use surf.AxiPkg.all;

package AxiSocUltraPlusPkg is

   -- System Clock Frequency
   constant DMA_CLK_FREQ_C  : real := 250.0E+6;  -- units of Hz
   
   -- Application Address Offset
   constant APP_ADDR_OFFSET_C  : slv(31 downto 0) := x"8000_0000";  -- units of Hz
      
   -- SOC AXI Configuration
   constant AXI_SOC_CONFIG_C : AxiConfigType := (
      ADDR_WIDTH_C => 40,               -- 40-bit address interface
      DATA_BYTES_C => 16,               -- 128-bit data interface
      ID_BITS_C    => 4,                -- Up to 16 DMA IDS
      LEN_BITS_C   => 8);               -- 8-bit awlen/arlen interface

   -- DMA AXI Stream Configuration
   constant DMA_AXIS_CONFIG_C : AxiStreamConfigType := (
      TSTRB_EN_C    => false,
      TDATA_BYTES_C => AXI_SOC_CONFIG_C.DATA_BYTES_C, -- Map the widths of the AXI interface
      TDEST_BITS_C  => 8,
      TID_BITS_C    => 3,
      TKEEP_MODE_C  => TKEEP_COMP_C,
      TUSER_BITS_C  => 4,
      TUSER_MODE_C  => TUSER_FIRST_LAST_C);

end package AxiSocUltraPlusPkg;
