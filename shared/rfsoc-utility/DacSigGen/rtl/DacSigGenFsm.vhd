-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: DacSigGen FSM
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

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.DacSigGenPkg.all;

entity DacSigGenFsm is
   generic (
      TPD_G              : time     := 1 ns;
      NUM_CH_G           : positive := 1;
      RAM_ADDR_WIDTH_G   : positive := 10;
      SAMPLE_PER_CYCLE_G : positive := 16);
   port (
      -- Clock and Reset
      dspClk    : in  sl;
      dspRst    : in  sl;
      -- Control/Status Interface
      config    : in  DacSigGenConfigType;
      status    : out DacSigGenStatusType;
      -- Memory Interface
      ramAddr   : out slv(RAM_ADDR_WIDTH_G-1 downto 0);
      ramData   : in  Slv256Array(NUM_CH_G-1 downto 0);
      -- DAC Interface
      dspDacIn  : in  Slv256Array(NUM_CH_G-1 downto 0);
      dspDacOut : out Slv256Array(NUM_CH_G-1 downto 0));
end DacSigGenFsm;

architecture rtl of DacSigGenFsm is

   type StateType is (
      IDLE_S,
      INIT_S,
      MOVE_S);

   type RegType is record
      dspDacOut : Slv256Array(NUM_CH_G-1 downto 0);
      ramAddr   : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      cnt       : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      cntSize   : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      status    : DacSigGenStatusType;
      state     : StateType;
   end record;

   constant REG_INIT_C : RegType := (
      dspDacOut => (others => (others => '0')),
      ramAddr   => (others => '0'),
      cnt       => (others => '0'),
      cntSize   => (others => '0'),
      status    => DAC_SIG_GEN_STATUS_INIT_C,
      state     => IDLE_S);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

begin

   comb : process (config, dspDacIn, dspRst, r, ramData) is
      variable v : RegType;
      variable i : natural;
      variable j : natural;
   begin
      -- Latch the current value
      v := r;

      -- Reset flags
      v.status.dacGenValid := '0';

      -- Increment the counters
      v.ramAddr := r.ramAddr + 1;
      v.cnt     := r.cnt + 1;

      case r.state is
         ----------------------------------------------------------------------
         when IDLE_S =>
            -- Reset the counters
            v.ramAddr         := (others => '0');
            v.cnt             := (others => '0');
            v.status.burstCnt := (others => '0');

            -- Load IDLE value
            for i in 0 to SAMPLE_PER_CYCLE_G-1 loop
               for j in 0 to NUM_CH_G-1 loop
                  v.dspDacOut(j)(DAC_BIT_WIDTH_C*i+DAC_BIT_WIDTH_C-1 downto DAC_BIT_WIDTH_C*i) := config.idleValue;
               end loop;
            end loop;

            -- Check for start or continuous flags
            if (config.burst = '1') or (config.continuous = '1') then

               -- Latch the counter size
               v.cntSize := config.bufferLength(RAM_ADDR_WIDTH_G-1 downto 0);

               -- Check not continuous flags
               if (config.continuous = '0') then
                  -- Latch the counter size
                  v.status.burstCnt := config.burstCnt;
               end if;

               -- Next state
               v.state := INIT_S;

            end if;
         ----------------------------------------------------------------------
         when INIT_S =>
            -- Check the counter w.r.t. read latency (READ_LATENCY_G=2 minus one)
            if (r.cnt = 1) then

               -- Reset the counter
               v.cnt := (others => '0');

               -- Next state
               v.state := MOVE_S;

            end if;
         ----------------------------------------------------------------------
         when MOVE_S =>
            -- Move the RAM data
            v.dspDacOut := ramData;

            -- Set the flag
            v.status.dacGenValid := '1';

            -- Check the counter
            if (r.cnt = r.cntSize) then

               -- Reset the counter
               v.cnt := (others => '0');

               -- Check for not continuous flags
               if (config.continuous = '0') then

                  -- Check if done
                  if (r.status.burstCnt = 0) then
                     -- Next state
                     v.state := IDLE_S;
                  else
                     -- Decrement the counter
                     v.status.burstCnt := r.status.burstCnt - 1;
                  end if;

               else

                  -- Latch the counter size
                  v.cntSize := config.bufferLength(RAM_ADDR_WIDTH_G-1 downto 0);

               end if;

            end if;
      -------------------------------------------------------
      end case;

      -- Check the address
      if (r.ramAddr = r.cntSize) then
         -- Reset the counter
         v.ramAddr := (others => '0');
      end if;

      -- DSP reset or User Reset or not enabled mode
      if (dspRst = '1') or (config.reset = '1') or (config.enabled = '0') then
         -- Reset the registers
         v           := REG_INIT_C;
         -- Pass through the data
         v.dspDacOut := dspDacIn;
      end if;

      -- Outputs
      dspDacOut <= r.dspDacOut;
      ramAddr   <= r.ramAddr;
      status    <= r.status;

      -- Register the variable for next clock cycle
      rin <= v;

   end process;

   seq : process (dspClk) is
   begin
      if rising_edge(dspClk) then
         r <= rin after TPD_G;
      end if;
   end process;

end rtl;
