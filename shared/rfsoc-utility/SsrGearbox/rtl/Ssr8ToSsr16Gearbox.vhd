-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Converts the SSR=8 interface to SSR=16 interface
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

entity Ssr8ToSsr16Gearbox is
   generic (
      TPD_G                : time     := 1 ns;
      -- Pipelining generics
      INPUT_PIPE_STAGES_G  : natural  := 0;
      OUTPUT_PIPE_STAGES_G : natural  := 0;
      -- Async FIFO generics
      FIFO_MEMORY_TYPE_G   : string   := "distributed";
      FIFO_ADDR_WIDTH_G    : positive := 4;
      -- Number of channels
      NUM_CH_G             : positive);
   port (
      -- SSR_IN_G Write Interface
      wrClk   : in  sl;
      wrRst   : in  sl := '0';
      wrValid : in  sl := '1';
      wrData  : in  Slv128Array(NUM_CH_G-1 downto 0);
      -- 2xSSR_IN_G Read Interface
      rdClk   : in  sl;
      rdRst   : in  sl;
      rdData  : out Slv256Array(NUM_CH_G-1 downto 0));
end Ssr8ToSsr16Gearbox;

architecture mapping of Ssr8ToSsr16Gearbox is

   signal slaveData  : slv(128*NUM_CH_G-1 downto 0);
   signal masterData : slv(256*NUM_CH_G-1 downto 0);

begin

   ib_map : process (wrData) is
   begin
      for idx in 0 to (128/16)-1 loop
         for ch in 0 to NUM_CH_G-1 loop
            slaveData(idx*NUM_CH_G*16+(ch*16+15) downto idx*NUM_CH_G*16+(ch*16)) <= wrData(ch)(idx*16+15 downto idx*16);
         end loop;
      end loop;
   end process ib_map;

   U_Gearbox : entity surf.AsyncGearbox
      generic map (
         TPD_G                => TPD_G,
         SLAVE_WIDTH_G        => 128*NUM_CH_G,
         MASTER_WIDTH_G       => 256*NUM_CH_G,
         EN_EXT_CTRL_G        => false,  -- Set to false if slaveBitOrder, masterBitOrder and slip ports are unused for better performance
         -- Pipelining generics
         INPUT_PIPE_STAGES_G  => INPUT_PIPE_STAGES_G,
         OUTPUT_PIPE_STAGES_G => OUTPUT_PIPE_STAGES_G,
         -- Async FIFO generics
         FIFO_MEMORY_TYPE_G   => FIFO_MEMORY_TYPE_G,
         FIFO_ADDR_WIDTH_G    => FIFO_ADDR_WIDTH_G)
      port map (
         -- Slave Interface
         slaveClk    => wrClk,
         slaveRst    => wrRst,
         slaveData   => slaveData,
         slaveValid  => '1',
         slaveReady  => open,
         -- Master Interface
         masterClk   => rdClk,
         masterRst   => rdRst,
         masterData  => masterData,
         masterValid => open,
         masterReady => '1');

   ob_map : process (masterData) is
   begin
      for idx in 0 to (256/16)-1 loop
         for ch in 0 to NUM_CH_G-1 loop
            rdData(ch)(idx*16+15 downto idx*16) <= masterData(idx*NUM_CH_G*16+(ch*16+15) downto idx*NUM_CH_G*16+(ch*16));
         end loop;
      end loop;
   end process ob_map;

end mapping;
