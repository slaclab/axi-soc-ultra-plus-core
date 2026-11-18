-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: RF-ADC background calibration freeze control, one bit per channel.
--
--    The RF-ADC gain (GCB) and time skew (TSCB) background calibration blocks
--    train on the input signal and drift when it drops below -40 dBFS, so the
--    product guide asks for them to be frozen whenever the input is absent for
--    longer than 100 us. This module drives the IP core's int_cal_freeze pin
--    of every ADC channel from two sources, either of which freezes the block:
--
--    ManualFreeze : a register bit per channel, for a train-then-freeze policy
--                   run by software after the converters are up
--    AutoEnable   : a signal detector per channel on the ADC samples. The mean
--                   magnitude over a window of 2^Window clocks is compared
--                   with Threshold; HoldOff consecutive windows below it
--                   assert the freeze, one window above releases it
--
--    Both are off at reset. The IP core honours the pin only while the PS
--    driver's DisableFreezePin is 0 (its default).
--
--    Channel c takes its samples from adcI<c> (and adcQ<c> when IQ_MODE_G),
--    SAMPLE_PER_CYCLE_G 16-bit samples per clock in bits [16k+15:16k]:
--
--    IQ_MODE_G = true  : adcI/adcQ carry the I and Q of SAMPLE_PER_CYCLE_G
--                        complex samples (the RF-ADC I/Q output streams).
--                        The magnitude is |I|+|Q| per complex sample.
--    IQ_MODE_G = false : adcI carries SAMPLE_PER_CYCLE_G real samples and
--                        adcQ is unused. The magnitude is |x| per sample.
--
--    The signal detector pipeline is: |x| of every sample of the clock, an
--    adder tree of log2 levels down to one magnitude per clock, a window
--    accumulator, the mean (a shift), the threshold compare and the HoldOff
--    counter (see the comb process). A single window timer is shared by the
--    channels, so no stage holds more than one adder, one comparator or one
--    barrel shifter of the DSP clock. The few clocks of latency from sample
--    to freeze pin are irrelevant against the 100 us window.
--
--    The registers and the detectors share the DSP clock behind an AXI-Lite
--    async bridge (unless COMMON_CLK_G), so a register access needs that
--    clock to be running. The freeze pins belong to the IP core's s_axi_aclk
--    (axilClk) domain and cross as level signals.
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
use ieee.numeric_std.all;

library surf;
use surf.StdRtlPkg.all;
use surf.AxiLitePkg.all;

entity RfdcCalFreeze is
   generic (
      TPD_G              : time                     := 1 ns;
      COMMON_CLK_G       : boolean                  := false;  -- true if dspClk=axilClk
      NUM_CH_G           : positive range 1 to 16   := 1;
      IQ_MODE_G          : boolean                  := true;   -- true: I/Q samples on adcI/adcQ, false: real samples on adcI
      SAMPLE_PER_CYCLE_G : positive                 := 16;     -- (complex) samples per channel per dspClk, power of 2
      -- Reset values of the signal detector registers
      THRESHOLD_INIT_G   : natural range 0 to 65535 := 416;    -- a -40 dBFS tone gives a mean |I|+|Q| of 417 (|x| of 209)
      WINDOW_INIT_G      : natural range 0 to 24    := 15;     -- 2^15 clocks, 106.7 us at 307.2 MHz
      HOLD_OFF_INIT_G    : natural range 0 to 255   := 1);
   port (
      -- ADC samples (dspClk domain)
      dspClk          : in  sl;
      dspRst          : in  sl;
      adcI0           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI1           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI2           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI3           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI4           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI5           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI6           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI7           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI8           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI9           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI10          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI11          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI12          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI13          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI14          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcI15          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ0           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ1           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ2           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ3           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ4           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ5           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ6           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ7           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ8           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ9           : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ10          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ11          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ12          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ13          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ14          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      adcQ15          : in  slv(16*SAMPLE_PER_CYCLE_G-1 downto 0) := (others => '0');
      -- RF-ADC calibration freeze interface (axilClk domain), one bit per channel
      adcCalFreeze    : out slv(NUM_CH_G-1 downto 0);
      adcCalFrozen    : in  slv(NUM_CH_G-1 downto 0) := (others => '0');
      adcSigDetect    : in  slv(NUM_CH_G-1 downto 0) := (others => '0');
      -- AXI-Lite Interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType);
end RfdcCalFreeze;

architecture rtl of RfdcCalFreeze is

   -- Largest window exponent the accumulator supports
   constant WINDOW_MAX_C : natural := 24;

   -- 16-bit samples of a channel per clock: I and Q of every complex sample in I/Q mode
   constant SMP_PER_CLK_C : positive := ite(IQ_MODE_G, 2, 1) * SAMPLE_PER_CYCLE_G;
   -- Adder tree levels from SMP_PER_CLK_C magnitudes down to one
   constant LEVELS_C      : natural  := log2(SMP_PER_CLK_C);
   -- Tree entries: SMP_PER_CLK_C leaves plus the pairwise sums of every level
   constant TREE_SIZE_C   : positive := 2*SMP_PER_CLK_C - 1;
   -- Root of the tree, the magnitude of the clock
   constant MAG_INDEX_C   : natural  := TREE_SIZE_C - 1;
   -- Mean per (complex) sample: the window holds 2^(Window + SHIFT_C) of them
   constant SHIFT_C       : natural  := log2(SAMPLE_PER_CYCLE_G);

   -- |x| of a 16-bit sample is 32768 at most: 17 bits
   constant ABS_WIDTH_C : natural := 17;
   -- Sum of the SMP_PER_CLK_C magnitudes of a clock
   constant MAG_WIDTH_C : natural := ABS_WIDTH_C + LEVELS_C;
   -- Sum of 2^WINDOW_MAX_C of them
   constant ACC_WIDTH_C : natural := MAG_WIDTH_C + WINDOW_MAX_C;

   type DataArray is array (natural range <>) of slv(16*SAMPLE_PER_CYCLE_G-1 downto 0);
   type MagArray is array (natural range <>) of unsigned(MAG_WIDTH_C-1 downto 0);

   -- First tree index of a level: level 0 holds the SMP_PER_CLK_C magnitudes,
   -- every level above holds the pairwise sums of the level below
   function treeOffset (lvl : natural) return natural is
   begin
      return 2*SMP_PER_CLK_C - (2*SMP_PER_CLK_C)/(2**lvl);
   end function;

   type DetType is record
      tree       : MagArray(0 to TREE_SIZE_C-1);     -- stages 1 to 1+LEVELS_C
      acc        : unsigned(ACC_WIDTH_C-1 downto 0);  -- accumulate
      total      : unsigned(ACC_WIDTH_C-1 downto 0);  -- accumulate
      mean       : unsigned(ACC_WIDTH_C-1 downto 0);  -- mean
      magnitude  : slv(15 downto 0);                  -- compare
      below      : sl;                                -- compare
      lowCnt     : unsigned(7 downto 0);              -- hold off
      autoFreeze : sl;                                -- hold off
   end record;

   constant DET_INIT_C : DetType := (
      tree       => (others => (others => '0')),
      acc        => (others => '0'),
      total      => (others => '0'),
      mean       => (others => '0'),
      magnitude  => (others => '0'),
      below      => '0',
      lowCnt     => (others => '0'),
      autoFreeze => '0');

   type DetArray is array (natural range <>) of DetType;

   type RegType is record
      manualFreeze   : slv(NUM_CH_G-1 downto 0);
      autoEnable     : slv(NUM_CH_G-1 downto 0);
      threshold      : slv(15 downto 0);
      window         : slv(4 downto 0);
      holdOff        : slv(7 downto 0);
      -- Window timer shared by the channels
      cnt            : unsigned(WINDOW_MAX_C-1 downto 0);
      mask           : unsigned(WINDOW_MAX_C-1 downto 0);
      shift          : unsigned(4 downto 0);
      windowEnd      : sl;              -- accumulate strobe
      totalValid     : sl;              -- mean strobe
      meanValid      : sl;              -- compare strobe
      belowValid     : sl;              -- hold off strobe
      det            : DetArray(NUM_CH_G-1 downto 0);
      calFreeze      : slv(NUM_CH_G-1 downto 0);
      axilReadSlave  : AxiLiteReadSlaveType;
      axilWriteSlave : AxiLiteWriteSlaveType;
   end record;

   constant REG_INIT_C : RegType := (
      manualFreeze   => (others => '0'),
      autoEnable     => (others => '0'),  -- signal detector freeze disabled
      threshold      => toSlv(THRESHOLD_INIT_G, 16),
      window         => toSlv(WINDOW_INIT_G, 5),
      holdOff        => toSlv(HOLD_OFF_INIT_G, 8),
      cnt            => (others => '0'),
      mask           => (others => '0'),
      shift          => (others => '0'),
      windowEnd      => '0',
      totalValid     => '0',
      meanValid      => '0',
      belowValid     => '0',
      det            => (others => DET_INIT_C),
      calFreeze      => (others => '0'),
      axilReadSlave  => AXI_LITE_READ_SLAVE_INIT_C,
      axilWriteSlave => AXI_LITE_WRITE_SLAVE_INIT_C);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   -- Numbered ports as arrays
   signal adcI : DataArray(15 downto 0);
   signal adcQ : DataArray(15 downto 0);

   -- AXI-Lite in the dspClk domain
   signal readMaster  : AxiLiteReadMasterType;
   signal readSlave   : AxiLiteReadSlaveType;
   signal writeMaster : AxiLiteWriteMasterType;
   signal writeSlave  : AxiLiteWriteSlaveType;

   -- IP core status in the dspClk domain
   signal calFrozen : slv(NUM_CH_G-1 downto 0);
   signal sigDetect : slv(NUM_CH_G-1 downto 0);

   function absMag (x : slv(15 downto 0)) return unsigned is
      variable s : signed(ABS_WIDTH_C-1 downto 0);
   begin
      s := resize(signed(x), ABS_WIDTH_C);
      if (s(ABS_WIDTH_C-1) = '1') then
         s := -s;
      end if;
      return unsigned(s);
   end function;

begin

   assert (isPowerOf2(SAMPLE_PER_CYCLE_G))
      report "RfdcCalFreeze: SAMPLE_PER_CYCLE_G must be a power of 2"
      severity failure;

   adcI(0)  <= adcI0;
   adcI(1)  <= adcI1;
   adcI(2)  <= adcI2;
   adcI(3)  <= adcI3;
   adcI(4)  <= adcI4;
   adcI(5)  <= adcI5;
   adcI(6)  <= adcI6;
   adcI(7)  <= adcI7;
   adcI(8)  <= adcI8;
   adcI(9)  <= adcI9;
   adcI(10) <= adcI10;
   adcI(11) <= adcI11;
   adcI(12) <= adcI12;
   adcI(13) <= adcI13;
   adcI(14) <= adcI14;
   adcI(15) <= adcI15;

   adcQ(0)  <= adcQ0;
   adcQ(1)  <= adcQ1;
   adcQ(2)  <= adcQ2;
   adcQ(3)  <= adcQ3;
   adcQ(4)  <= adcQ4;
   adcQ(5)  <= adcQ5;
   adcQ(6)  <= adcQ6;
   adcQ(7)  <= adcQ7;
   adcQ(8)  <= adcQ8;
   adcQ(9)  <= adcQ9;
   adcQ(10) <= adcQ10;
   adcQ(11) <= adcQ11;
   adcQ(12) <= adcQ12;
   adcQ(13) <= adcQ13;
   adcQ(14) <= adcQ14;
   adcQ(15) <= adcQ15;

   U_AxiLiteAsync : entity surf.AxiLiteAsync
      generic map (
         TPD_G           => TPD_G,
         COMMON_CLK_G    => COMMON_CLK_G,
         NUM_ADDR_BITS_G => 8)
      port map (
         -- Slave Port
         sAxiClk         => axilClk,
         sAxiClkRst      => axilRst,
         sAxiReadMaster  => axilReadMaster,
         sAxiReadSlave   => axilReadSlave,
         sAxiWriteMaster => axilWriteMaster,
         sAxiWriteSlave  => axilWriteSlave,
         -- Master Port
         mAxiClk         => dspClk,
         mAxiClkRst      => dspRst,
         mAxiReadMaster  => readMaster,
         mAxiReadSlave   => readSlave,
         mAxiWriteMaster => writeMaster,
         mAxiWriteSlave  => writeSlave);

   -- Level signals, so a bit-wise synchronizer is enough
   U_CalFrozen : entity surf.SynchronizerVector
      generic map (
         TPD_G         => TPD_G,
         BYPASS_SYNC_G => COMMON_CLK_G,
         WIDTH_G       => NUM_CH_G)
      port map (
         clk     => dspClk,
         dataIn  => adcCalFrozen,
         dataOut => calFrozen);

   U_SigDetect : entity surf.SynchronizerVector
      generic map (
         TPD_G         => TPD_G,
         BYPASS_SYNC_G => COMMON_CLK_G,
         WIDTH_G       => NUM_CH_G)
      port map (
         clk     => dspClk,
         dataIn  => adcSigDetect,
         dataOut => sigDetect);

   U_CalFreeze : entity surf.SynchronizerVector
      generic map (
         TPD_G         => TPD_G,
         BYPASS_SYNC_G => COMMON_CLK_G,
         WIDTH_G       => NUM_CH_G)
      port map (
         clk     => axilClk,
         dataIn  => r.calFreeze,
         dataOut => adcCalFreeze);

   comb : process (adcI, adcQ, calFrozen, dspRst, r, readMaster, sigDetect,
                   writeMaster) is
      variable v         : RegType;
      variable regCon    : AxiLiteEndPointType;
      variable threshold : unsigned(15 downto 0);
      variable window    : natural range 0 to 31;
      variable holdOff   : unsigned(7 downto 0);
      variable total     : unsigned(ACC_WIDTH_C-1 downto 0);
   begin
      -- Latch the current value
      v := r;

      threshold := unsigned(r.threshold);
      holdOff   := unsigned(r.holdOff);

      -----------------------------------------------------------------------
      -- Window timer, shared by the channels
      -----------------------------------------------------------------------
      -- 2^Window clocks per window, 2^WINDOW_MAX_C at most
      window := to_integer(unsigned(r.window));
      if (window > WINDOW_MAX_C) then
         window := WINDOW_MAX_C;
      end if;
      for k in 0 to WINDOW_MAX_C-1 loop
         v.mask(k) := ite(k < window, '1', '0');
      end loop;
      -- Mean per (complex) sample: SAMPLE_PER_CYCLE_G of them per clock
      v.shift := to_unsigned(window + SHIFT_C, 5);

      -- Free running counter: the window ends on the clock where its Window
      -- low bits are all set, so a Window write never strands the counter.
      -- The window being accumulated when Window changes is cut short.
      v.cnt       := r.cnt + 1;
      v.windowEnd := ite((r.cnt and r.mask) = r.mask, '1', '0');

      -- The strobe follows the window total down the pipeline
      v.totalValid := r.windowEnd;
      v.meanValid  := r.totalValid;
      v.belowValid := r.meanValid;

      -----------------------------------------------------------------------
      -- Signal magnitude detectors
      -----------------------------------------------------------------------
      for ch in 0 to NUM_CH_G-1 loop

         -- Stage 1: |x| of every sample of this clock (I then Q in I/Q mode)
         for k in 0 to SAMPLE_PER_CYCLE_G-1 loop
            v.det(ch).tree(k) := resize(absMag(adcI(ch)(16*k+15 downto 16*k)), MAG_WIDTH_C);
            if (IQ_MODE_G) then
               v.det(ch).tree(SAMPLE_PER_CYCLE_G+k) := resize(absMag(adcQ(ch)(16*k+15 downto 16*k)), MAG_WIDTH_C);
            end if;
         end loop;

         -- Stages 2 to 1+LEVELS_C: adder tree, one adder per stage, down to
         -- the magnitude of the clock in tree(MAG_INDEX_C)
         for lvl in 1 to LEVELS_C loop
            for j in 0 to (SMP_PER_CLK_C/(2**lvl))-1 loop
               v.det(ch).tree(treeOffset(lvl)+j) := r.det(ch).tree(treeOffset(lvl-1)+2*j) + r.det(ch).tree(treeOffset(lvl-1)+2*j+1);
            end loop;
         end loop;

         -- Accumulate over the window and capture its total
         total := r.det(ch).acc + resize(r.det(ch).tree(MAG_INDEX_C), ACC_WIDTH_C);
         if (r.windowEnd = '1') then
            v.det(ch).total := total;
            v.det(ch).acc   := (others => '0');
         else
            v.det(ch).acc := total;
         end if;

         -- Mean per (complex) sample
         if (r.totalValid = '1') then
            v.det(ch).mean := shift_right(r.det(ch).total, to_integer(r.shift));
         end if;

         -- Saturate to 16 bits and compare with the threshold
         if (r.meanValid = '1') then
            if (r.det(ch).mean(ACC_WIDTH_C-1 downto 16) = 0) then
               v.det(ch).magnitude := slv(r.det(ch).mean(15 downto 0));
               v.det(ch).below     := ite(r.det(ch).mean(15 downto 0) < threshold, '1', '0');
            else
               -- 65535 is never below a 16-bit threshold
               v.det(ch).magnitude := (others => '1');
               v.det(ch).below     := '0';
            end if;
         end if;

         -- HoldOff consecutive low windows assert the freeze,
         -- one window with signal releases it
         if (r.belowValid = '1') then
            if (r.det(ch).below = '1') then
               if (r.det(ch).lowCnt < holdOff) then
                  v.det(ch).lowCnt := r.det(ch).lowCnt + 1;
               end if;
               -- The count after this window reaches HoldOff
               if (resize(r.det(ch).lowCnt, 9) + 1 >= resize(holdOff, 9)) then
                  v.det(ch).autoFreeze := '1';
               end if;
            else
               v.det(ch).lowCnt     := (others => '0');
               v.det(ch).autoFreeze := '0';
            end if;
         end if;

         -- Either source freezes the block
         v.calFreeze(ch) := r.manualFreeze(ch) or (r.autoEnable(ch) and r.det(ch).autoFreeze);

      end loop;

      -----------------------------------------------------------------------
      -- AXI-Lite registers
      -----------------------------------------------------------------------
      axiSlaveWaitTxn(regCon, writeMaster, readMaster, v.axilWriteSlave, v.axilReadSlave);

      -- Generic configuration (read only)
      axiSlaveRegisterR(regCon, x"00", 0, toSlv(NUM_CH_G, 8));
      axiSlaveRegisterR(regCon, x"00", 8, toSlv(SAMPLE_PER_CYCLE_G, 8));
      axiSlaveRegisterR(regCon, x"00", 16, toSlv(WINDOW_MAX_C, 8));
      axiSlaveRegisterR(regCon, x"00", 24, ite(IQ_MODE_G, '1', '0'));
      axiSlaveRegisterR(regCon, x"00", 25, ite(COMMON_CLK_G, '1', '0'));

      -- Control
      axiSlaveRegister(regCon, x"10", 0, v.manualFreeze);
      axiSlaveRegister(regCon, x"14", 0, v.autoEnable);
      axiSlaveRegister(regCon, x"18", 0, v.threshold);
      axiSlaveRegister(regCon, x"1C", 0, v.window);
      axiSlaveRegister(regCon, x"20", 0, v.holdOff);

      -- Status
      axiSlaveRegisterR(regCon, x"30", 0, calFrozen);
      for ch in 0 to NUM_CH_G-1 loop
         axiSlaveRegisterR(regCon, x"34", ch, r.det(ch).autoFreeze);
      end loop;
      axiSlaveRegisterR(regCon, x"38", 0, sigDetect);
      axiSlaveRegisterR(regCon, x"3C", 0, r.calFreeze);
      for ch in 0 to NUM_CH_G-1 loop
         axiSlaveRegisterR(regCon, toSlv(64 + 4*ch, 8), 0, r.det(ch).magnitude);
      end loop;

      axiSlaveDefault(regCon, v.axilWriteSlave, v.axilReadSlave, AXI_RESP_DECERR_C);

      -- Synchronous Reset
      if (dspRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

      -- Outputs
      writeSlave <= r.axilWriteSlave;
      readSlave  <= r.axilReadSlave;

   end process comb;

   seq : process (dspClk) is
   begin
      if (rising_edge(dspClk)) then
         r <= rin after TPD_G;
      end if;
   end process seq;

end rtl;
