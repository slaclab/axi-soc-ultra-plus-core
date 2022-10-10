-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: SigGen FSM
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
use axi_soc_ultra_plus_core.SigGenPkg.all;

entity SigGenFsm is
   generic (
      TPD_G              : time     := 1 ns;
      RAM_ADDR_WIDTH_G   : positive := 10;
      SAMPLE_PER_CYCLE_G : positive := 16);
   port (
      -- Clock and Reset
      dspClk      : in  sl;
      dspRst      : in  sl;
      -- Control/Status Interface
      config      : in  SigGenConfigType;
      status      : out SigGenStatusType;
      -- External Trigger Interface
      extTrigIn   : in  sl;
      extTrigout  : out sl;
      -- Memory Interface
      ramAddr     : out slv(RAM_ADDR_WIDTH_G-1 downto 0);
      ramData0    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData1    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData2    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData3    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData4    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData5    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData6    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData7    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData8    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData9    : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData10   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData11   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData12   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData13   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData14   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramData15   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      -- DAC Interface
      dspDacIn0   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn1   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn2   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn3   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn4   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn5   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn6   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn7   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn8   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn9   : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn10  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn11  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn12  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn13  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn14  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacIn15  : in  slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut0  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut1  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut2  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut3  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut4  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut5  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut6  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut7  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut8  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut9  : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut10 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut11 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut12 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut13 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut14 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut15 : out slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0));
end SigGenFsm;

architecture rtl of SigGenFsm is

   type StateType is (
      IDLE_S,
      INIT_S,
      MOVE_S);

   type RegType is record
      extTrigout  : sl;
      dspDacOut0  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut1  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut2  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut3  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut4  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut5  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut6  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut7  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut8  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut9  : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut10 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut11 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut12 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut13 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut14 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      dspDacOut15 : slv(SIG_GEN_BIT_WIDTH_C*SAMPLE_PER_CYCLE_G-1 downto 0);
      ramAddr     : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      cnt         : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      cntSize     : slv(RAM_ADDR_WIDTH_G-1 downto 0);
      status      : SigGenStatusType;
      state       : StateType;
   end record;

   constant REG_INIT_C : RegType := (
      extTrigout  => '0',
      dspDacOut0  => (others => '0'),
      dspDacOut1  => (others => '0'),
      dspDacOut2  => (others => '0'),
      dspDacOut3  => (others => '0'),
      dspDacOut4  => (others => '0'),
      dspDacOut5  => (others => '0'),
      dspDacOut6  => (others => '0'),
      dspDacOut7  => (others => '0'),
      dspDacOut8  => (others => '0'),
      dspDacOut9  => (others => '0'),
      dspDacOut10 => (others => '0'),
      dspDacOut11 => (others => '0'),
      dspDacOut12 => (others => '0'),
      dspDacOut13 => (others => '0'),
      dspDacOut14 => (others => '0'),
      dspDacOut15 => (others => '0'),
      ramAddr     => (others => '0'),
      cnt         => (others => '0'),
      cntSize     => (others => '0'),
      status      => SIG_GEN_STATUS_INIT_C,
      state       => IDLE_S);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

begin

   comb : process (config, dspDacIn0, dspDacIn1, dspDacIn10, dspDacIn11,
                   dspDacIn12, dspDacIn13, dspDacIn14, dspDacIn15, dspDacIn2,
                   dspDacIn3, dspDacIn4, dspDacIn5, dspDacIn6, dspDacIn7,
                   dspDacIn8, dspDacIn9, dspRst, extTrigIn, r, ramData0,
                   ramData1, ramData10, ramData11, ramData12, ramData13,
                   ramData14, ramData15, ramData2, ramData3, ramData4,
                   ramData5, ramData6, ramData7, ramData8, ramData9) is
      variable v : RegType;
      variable i : natural;
      variable j : natural;
   begin
      -- Latch the current value
      v := r;

      -- Reset flags
      v.status.dacGenValid := '0';
      v.extTrigout         := '0';

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
               v.dspDacOut0(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut1(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut2(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut3(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut4(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut5(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut6(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut7(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut8(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut9(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i)  := config.idleValue;
               v.dspDacOut10(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
               v.dspDacOut11(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
               v.dspDacOut12(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
               v.dspDacOut13(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
               v.dspDacOut14(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
               v.dspDacOut15(SIG_GEN_BIT_WIDTH_C*i+SIG_GEN_BIT_WIDTH_C-1 downto SIG_GEN_BIT_WIDTH_C*i) := config.idleValue;
            end loop;

            -- Check for start or continuous flags
            if (config.burst = '1') or (config.continuous = '1') or (extTrigIn = '1') then

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

               -- Set the flag
               v.extTrigout := '1';

               -- Next state
               v.state := MOVE_S;

            end if;
         ----------------------------------------------------------------------
         when MOVE_S =>
            -- Move the RAM data
            v.dspDacOut0  := ramData0;
            v.dspDacOut1  := ramData1;
            v.dspDacOut2  := ramData2;
            v.dspDacOut3  := ramData3;
            v.dspDacOut4  := ramData4;
            v.dspDacOut5  := ramData5;
            v.dspDacOut6  := ramData6;
            v.dspDacOut7  := ramData7;
            v.dspDacOut8  := ramData8;
            v.dspDacOut9  := ramData9;
            v.dspDacOut10 := ramData10;
            v.dspDacOut11 := ramData11;
            v.dspDacOut12 := ramData12;
            v.dspDacOut13 := ramData13;
            v.dspDacOut14 := ramData14;
            v.dspDacOut15 := ramData15;

            -- Set the flag
            v.status.dacGenValid := '1';

            -- Check the counter
            if (r.cnt = r.cntSize) then

               -- Reset the counter
               v.cnt := (others => '0');

               -- Set the flag
               v.extTrigout := '1';

               -- Check for not continuous flags
               if (config.continuous = '0') then

                  -- Check if done
                  if (r.status.burstCnt = 0) then

                     -- Reset the flag
                     v.extTrigout := '0';

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
         v             := REG_INIT_C;
         -- Pass through the data
         v.dspDacOut0  := dspDacIn0;
         v.dspDacOut1  := dspDacIn1;
         v.dspDacOut2  := dspDacIn2;
         v.dspDacOut3  := dspDacIn3;
         v.dspDacOut4  := dspDacIn4;
         v.dspDacOut5  := dspDacIn5;
         v.dspDacOut6  := dspDacIn6;
         v.dspDacOut7  := dspDacIn7;
         v.dspDacOut8  := dspDacIn8;
         v.dspDacOut9  := dspDacIn9;
         v.dspDacOut10 := dspDacIn10;
         v.dspDacOut11 := dspDacIn11;
         v.dspDacOut12 := dspDacIn12;
         v.dspDacOut13 := dspDacIn13;
         v.dspDacOut14 := dspDacIn14;
         v.dspDacOut15 := dspDacIn15;
      end if;

      -- Outputs
      extTrigout  <= r.extTrigout;
      dspDacOut0  <= r.dspDacOut0;
      dspDacOut1  <= r.dspDacOut1;
      dspDacOut2  <= r.dspDacOut2;
      dspDacOut3  <= r.dspDacOut3;
      dspDacOut4  <= r.dspDacOut4;
      dspDacOut5  <= r.dspDacOut5;
      dspDacOut6  <= r.dspDacOut6;
      dspDacOut7  <= r.dspDacOut7;
      dspDacOut8  <= r.dspDacOut8;
      dspDacOut9  <= r.dspDacOut9;
      dspDacOut10 <= r.dspDacOut10;
      dspDacOut11 <= r.dspDacOut11;
      dspDacOut12 <= r.dspDacOut12;
      dspDacOut13 <= r.dspDacOut13;
      dspDacOut14 <= r.dspDacOut14;
      dspDacOut15 <= r.dspDacOut15;
      ramAddr     <= r.ramAddr;
      status      <= r.status;

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
