-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Converts the SSR=X interface to SSR=2x interface
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
use ieee.std_logic_unsigned.all;
use ieee.std_logic_arith.all;

library surf;
use surf.StdRtlPkg.all;

entity SsrToSsr2x is
   generic (
      TPD_G    : time := 1 ns;
      SSR_IN_G : positive);
   port (
      -- SSR_IN_G Write Interface
      wrClk   : in  sl;
      dataIn  : in  slv(16*SSR_IN_G-1 downto 0);
      -- 2xSSR_IN_G Read Interface
      rdClk   : in  sl;
      rdRst   : in  sl;
      dataOut : out slv(2*16*SSR_IN_G-1 downto 0));
end SsrToSsr2x;

architecture rtl of SsrToSsr2x is

   type RegType is record
      toggle    : sl;
      firstHalf : slv(16*SSR_IN_G-1 downto 0);
      data2x    : slv(2*16*SSR_IN_G-1 downto 0);
   end record;

   constant REG_INIT_C : RegType := (
      toggle    => '0',
      firstHalf => (others => '0'),
      data2x    => (others => '0'));

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   signal toggleRd   : sl := '0';
   signal firstCycle : sl := '0';

   attribute dont_touch      : string;
   attribute dont_touch of r : signal is "TRUE";

begin

   firstCycle <= not(r.toggle xor toggleRd);

   comb : process (dataIn, firstCycle, r, toggleRd) is
      variable v : RegType;
   begin
      -- Latch the current value
      v := r;

      v.toggle := toggleRd;

      if firstCycle = '0' then
         v.firstHalf := dataIn;
      end if;

      if firstCycle = '1' then
         v.data2x := dataIn & r.firstHalf;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process comb;

   seqWr : process (wrClk) is
   begin
      if (rising_edge(wrClk)) then
         r <= rin after TPD_G;
      end if;
   end process seqWr;

   seqRd : process (rdClk) is
   begin
      if (rising_edge(rdClk)) then
         dataOut <= r.data2x after TPD_G;
         if rdRst = '1' then
            toggleRd <= '0' after TPD_G;
         else
            toggleRd <= not(toggleRd) after TPD_G;
         end if;
      end if;
   end process seqRd;

end rtl;
