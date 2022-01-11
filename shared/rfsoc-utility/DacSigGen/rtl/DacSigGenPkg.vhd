-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: DacSigGen VHDL Package
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

package DacSigGenPkg is

   constant DAC_BIT_WIDTH_C : positive := 16;

   type DacSigGenConfigType is record
      enabled      : sl;
      reset        : sl;
      idleValue    : slv(DAC_BIT_WIDTH_C-1 downto 0);
      burst        : sl;
      continuous   : sl;
      bufferLength : slv(15 downto 0);
      burstCnt     : slv(31 downto 0);
   end record;
   constant DAC_SIG_GEN_CONFIG_INIT_C : DacSigGenConfigType := (
      enabled      => '0',
      reset        => '1',
      idleValue    => (others => '0'),
      burst        => '0',
      continuous   => '0',
      bufferLength => (others => '0'),
      burstCnt     => (others => '0'));

   type DacSigGenStatusType is record
      dacGenValid : sl;
      burstCnt    : slv(31 downto 0);
   end record;
   constant DAC_SIG_GEN_STATUS_INIT_C : DacSigGenStatusType := (
      dacGenValid => '0',
      burstCnt    => (others => '0'));

end DacSigGenPkg;
