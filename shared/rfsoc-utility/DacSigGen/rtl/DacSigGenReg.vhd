-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: DacSigGen Registers
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

library axi_soc_ultra_plus_core;
use axi_soc_ultra_plus_core.DacSigGenPkg.all;

entity DacSigGenReg is
   generic (
      TPD_G              : time     := 1 ns;
      NUM_CH_G           : positive := 1;
      RAM_ADDR_WIDTH_G   : positive := 10;
      SAMPLE_PER_CYCLE_G : positive := 16);
   port (
      -- Control/Status Interface (dataClk domain)
      dspClk          : in  sl;
      dspRst          : in  sl;
      config          : out DacSigGenConfigType;
      status          : in  DacSigGenStatusType;
      -- AXI-Lite interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType);
end DacSigGenReg;

architecture rtl of DacSigGenReg is

   type RegType is record
      fifoWr         : sl;
      config         : DacSigGenConfigType;
      axilReadSlave  : AxiLiteReadSlaveType;
      axilWriteSlave : AxiLiteWriteSlaveType;
   end record;

   constant REG_INIT_C : RegType := (
      fifoWr         => '0',
      config         => DAC_SIG_GEN_CONFIG_INIT_C,
      axilReadSlave  => AXI_LITE_READ_SLAVE_INIT_C,
      axilWriteSlave => AXI_LITE_WRITE_SLAVE_INIT_C);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   signal statusSync : DacSigGenStatusType;

begin

   comb : process (axilReadMaster, axilRst, axilWriteMaster, r, statusSync) is
      variable v      : RegType;
      variable axilEp : AxiLiteEndpointType;
   begin
      -- Latch the current value
      v := r;

      -- Reset flags
      v.config.burst := '0';
      v.config.reset := '0';

      ------------------------
      -- AXI-Lite Transactions
      ------------------------

      -- Determine the transaction type
      axiSlaveWaitTxn(axilEp, axilWriteMaster, axilReadMaster, v.axilWriteSlave, v.axilReadSlave);

      axiSlaveRegisterR(axilEp, x"00", 0, toSlv(NUM_CH_G, 8));
      axiSlaveRegisterR(axilEp, x"00", 8, toSlv(RAM_ADDR_WIDTH_G, 8));
      axiSlaveRegisterR(axilEp, x"00", 16, toSlv(SAMPLE_PER_CYCLE_G, 8));
      axiSlaveRegisterR(axilEp, x"00", 24, toSlv(DAC_BIT_WIDTH_C, 8));
      axiSlaveRegisterR(axilEp, x"04", 0, statusSync.burstCnt);

      axiSlaveRegister (axilEp, x"08", 0, v.config.burstCnt);
      axiSlaveRegister (axilEp, x"0C", 0, v.config.bufferLength(RAM_ADDR_WIDTH_G-1 downto 0));
      axiSlaveRegister (axilEp, x"10", 0, v.config.continuous);
      axiSlaveRegister (axilEp, x"14", 0, v.config.burst);
      axiSlaveRegister (axilEp, x"18", 0, v.config.idleValue);
      axiSlaveRegister (axilEp, x"1C", 0, v.config.reset);
      axiSlaveRegister (axilEp, x"20", 0, v.config.enabled);

      -- Close the transaction
      axiSlaveDefault(axilEp, v.axilWriteSlave, v.axilReadSlave, AXI_RESP_DECERR_C);

      -- Check for register changes that require reset of the FSM
      if (r.config.burstCnt /= v.config.burstCnt) or (r.config.bufferLength /= v.config.bufferLength) then
         v.config.reset := '1';
      end if;

      -- Update the SYNC FIFO write flag
      v.fifoWr := axilEp.axiStatus.writeEnable;

      -- Outputs
      axilReadSlave  <= r.axilReadSlave;
      axilWriteSlave <= r.axilWriteSlave;

      -- Synchronous Reset
      if (axilRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process;

   seq : process (axilClk) is
   begin
      if rising_edge(axilClk) then
         r <= rin after TPD_G;
      end if;
   end process;

   U_SyncOut : entity surf.SynchronizerFifo
      generic map (
         TPD_G        => TPD_G,
         DATA_WIDTH_G => 64)
      port map (
         rst                => dspRst,
         -- Write Interface
         wr_clk             => axilClk,
         wr_en              => r.fifoWr,
         din(31 downto 0)   => r.config.burstCnt,
         din(47 downto 32)  => r.config.bufferLength,
         din(63 downto 48)  => r.config.idleValue,
         -- Read interface
         rd_clk             => dspClk,
         dout(31 downto 0)  => config.burstCnt,
         dout(47 downto 32) => config.bufferLength,
         dout(63 downto 48) => config.idleValue);

   U_enabled : entity surf.Synchronizer
      generic map (
         TPD_G => TPD_G)
      port map (
         clk     => dspClk,
         rst     => dspRst,
         dataIn  => r.config.enabled,
         dataOut => config.enabled);

   U_continuous : entity surf.Synchronizer
      generic map (
         TPD_G => TPD_G)
      port map (
         clk     => dspClk,
         rst     => dspRst,
         dataIn  => r.config.continuous,
         dataOut => config.continuous);

   U_burst : entity surf.SynchronizerOneShot
      generic map (
         TPD_G => TPD_G)
      port map (
         clk     => dspClk,
         dataIn  => r.config.burst,
         dataOut => config.burst);

   U_reset : entity surf.SynchronizerOneShot
      generic map (
         TPD_G => TPD_G)
      port map (
         clk     => dspClk,
         dataIn  => r.config.reset,
         dataOut => config.reset);

   U_SyncIn : entity surf.SynchronizerFifo
      generic map (
         TPD_G        => TPD_G,
         DATA_WIDTH_G => 32)
      port map (
         rst    => axilRst,
         -- Write Interface
         wr_clk => dspClk,
         din    => status.burstCnt,
         -- Read interface
         rd_clk => axilClk,
         dout   => statusSync.burstCnt);

end rtl;
