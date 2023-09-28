-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: Module to convert a waveform data bus into AXI stream
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
use surf.AxiStreamPkg.all;
use surf.AxiLitePkg.all;
use surf.SsiPkg.all;

entity SigToAxiStream is
   generic (
      TPD_G               : time     := 1 ns;
      RST_ASYNC_G         : boolean  := false;
      COMMON_CLK_G        : boolean  := false;  -- true if dataClk=axilClk
      DATA_BYTES_G        : positive;
      WRD_CNT_WIDTH_G     : positive;
      -- AXI Stream Configurations
      VALID_THOLD_G       : natural  := 1;
      VALID_BURST_MODE_G  : boolean  := false;
      INT_PIPE_STAGES_G   : natural  := 1;
      PIPE_STAGES_G       : natural  := 1;
      GEN_SYNC_FIFO_G     : boolean  := false;  -- true if dataClk=axisClk
      SYNTH_MODE_G        : string   := "inferred";
      FIFO_MEMORY_TYPE_G  : string   := "block";
      FIFO_ADDR_WIDTH_G   : positive := 10;
      AXI_STREAM_CONFIG_G : AxiStreamConfigType);
   port (
      -- Data to store in ring buffer (dataClk domain)
      dataClk         : in  sl;
      dataRst         : in  sl := '0';
      dataValid       : in  sl := '1';
      dataValue       : in  slv(8*DATA_BYTES_G-1 downto 0);
      extTrig         : in  sl := '0';
      -- AXI-Lite interface (axilClk domain)
      axilClk         : in  sl;
      axilRst         : in  sl;
      axilReadMaster  : in  AxiLiteReadMasterType;
      axilReadSlave   : out AxiLiteReadSlaveType;
      axilWriteMaster : in  AxiLiteWriteMasterType;
      axilWriteSlave  : out AxiLiteWriteSlaveType;
      -- AXI-Stream Interface (axisClk domain)
      axisClk         : in  sl;
      axisRst         : in  sl;
      axisMaster      : out AxiStreamMasterType;
      axisSlave       : in  AxiStreamSlaveType);
end SigToAxiStream;

architecture mapping of SigToAxiStream is

   constant AXIS_CONFIG_C : AxiStreamConfigType := ssiAxiStreamConfig(
      dataBytes => DATA_BYTES_G,
      tKeepMode => TKEEP_FIXED_C,
      tUserMode => TUSER_FIRST_LAST_C,
      tDestBits => 0,
      tUserBits => 2,
      tIdBits   => 0);

   type StateType is (
      IDLE_S,
      MOVE_S);

   type RegType is record
      sof        : sl;
      eofe       : sl;
      continuous : sl;
      intTrig    : sl;
      extTrig    : sl;
      wordCnt    : slv(WRD_CNT_WIDTH_G-1 downto 0);
      wordSize   : slv(WRD_CNT_WIDTH_G-1 downto 0);
      readSlave  : AxiLiteReadSlaveType;
      writeSlave : AxiLiteWriteSlaveType;
      txMaster   : AxiStreamMasterType;
      state      : StateType;
   end record;

   constant REG_INIT_C : RegType := (
      sof        => '0',
      eofe       => '0',
      continuous => '0',
      intTrig    => '0',
      extTrig    => '0',
      wordCnt    => (others => '0'),
      wordSize   => (others => '1'),    -- Preset to max size
      readSlave  => AXI_LITE_READ_SLAVE_INIT_C,
      writeSlave => AXI_LITE_WRITE_SLAVE_INIT_C,
      txMaster   => axiStreamMasterInit(AXIS_CONFIG_C),
      state      => IDLE_S);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   signal readMaster  : AxiLiteReadMasterType;
   signal writeMaster : AxiLiteWriteMasterType;
   signal txSlave     : AxiStreamSlaveType;


begin

   U_AxiLiteAsync : entity surf.AxiLiteAsync
      generic map (
         TPD_G           => TPD_G,
         RST_ASYNC_G     => RST_ASYNC_G,
         COMMON_CLK_G    => COMMON_CLK_G,
         NUM_ADDR_BITS_G => 8)
      port map (
         -- Slave Interface
         sAxiClk         => axilClk,
         sAxiClkRst      => axilRst,
         sAxiReadMaster  => axilReadMaster,
         sAxiReadSlave   => axilReadSlave,
         sAxiWriteMaster => axilWriteMaster,
         sAxiWriteSlave  => axilWriteSlave,
         -- Master Interface
         mAxiClk         => dataClk,
         mAxiClkRst      => dataRst,
         mAxiReadMaster  => readMaster,
         mAxiReadSlave   => r.readSlave,
         mAxiWriteMaster => writeMaster,
         mAxiWriteSlave  => r.writeSlave);

   comb : process (dataRst, dataValid, dataValue, extTrig, r, readMaster,
                   txSlave, writeMaster) is
      variable v      : RegType;
      variable axilEp : AxiLiteEndpointType;
   begin
      -- Latch the current value
      v := r;

      -- Reset flags
      v.intTrig := '0';

      ------------------------
      -- AXI-Lite Transactions
      ------------------------

      -- Determine the transaction type
      axiSlaveWaitTxn(axilEp, writeMaster, readMaster, v.writeSlave, v.readSlave);

      axiSlaveRegisterR(axilEp, x"0", 0, toSlv(DATA_BYTES_G, 8));
      axiSlaveRegisterR(axilEp, x"0", 8, toSlv(WRD_CNT_WIDTH_G, 8));
      axiSlaveRegister (axilEp, x"4", 0, v.wordSize);
      axiSlaveRegister (axilEp, x"8", 0, v.intTrig);
      axiSlaveRegister (axilEp, x"C", 0, v.continuous);

      -- Close the transaction
      axiSlaveDefault(axilEp, v.writeSlave, v.readSlave, AXI_RESP_DECERR_C);

      ------------------------
      -- Local Trigger Logic
      ------------------------

      -- AXI Stream Flow Control
      if (txSlave.tReady = '1') then
         v.txMaster := axiStreamMasterInit(AXIS_CONFIG_C);
      end if;

      -- Sample the external trigger
      v.extTrig := extTrig;

      case r.state is
         ----------------------------------------------------------------------
         when IDLE_S =>
            -- Reset the counters and flags
            v.wordCnt := (others => '0');
            v.sof     := '1';
            v.eofe    := '0';

            -- Check for start condition
            if (r.extTrig = '0' and v.extTrig = '1') or (r.intTrig = '1') or (r.continuous = '1') then

               -- Next state
               v.state := MOVE_S;

            end if;
         ----------------------------------------------------------------------
         when MOVE_S =>
            -- Check for new data
            if (dataValid = '1') then

               -- Check if ready to move data
               if (v.txMaster.tValid = '0') then

                  -- Send the data
                  v.txMaster.tValid                           := '1';
                  v.txMaster.tData(8*DATA_BYTES_G-1 downto 0) := dataValue;

                  -- Set the SOF bit
                  ssiSetUserSof(AXIS_CONFIG_C, v.txMaster, r.sof);

                  -- Reset the flag
                  v.sof := '0';

                  -- Increment the counter
                  v.wordCnt := r.wordCnt + 1;

                  -- Check for End of Frame (EOF) or error bit
                  if (r.wordCnt = r.wordSize) or (r.eofe = '1') then

                     -- Set the EOF bit
                     v.txMaster.tLast := '1';

                     -- Set the EOFE bit
                     ssiSetUserEofe(AXIS_CONFIG_C, v.txMaster, r.eofe);

                     -- Next state
                     v.state := IDLE_S;

                  end if;

               else
                  -- Set the error flag
                  v.eofe := '1';
               end if;

            end if;
      -------------------------------------------------------
      end case;

      -- Check for change in wordSize
      if (r.wordSize /= v.wordSize) then
         -- Set the error flag
         v.eofe := '1';
      end if;

      -- Synchronous Reset
      if (RST_ASYNC_G = false and dataRst = '1') then
         v := REG_INIT_C;
      end if;

      -- Register the variable for next clock cycle
      rin <= v;

   end process;

   axiSeq : process (dataClk, dataRst) is
   begin
      if (RST_ASYNC_G) and (dataRst = '1') then
         r <= REG_INIT_C after TPD_G;
      elsif rising_edge(dataClk) then
         r <= rin after TPD_G;
      end if;
   end process;

   TX_FIFO : entity surf.AxiStreamFifoV2
      generic map (
         -- General Configurations
         TPD_G               => TPD_G,
         RST_ASYNC_G         => RST_ASYNC_G,
         INT_PIPE_STAGES_G   => INT_PIPE_STAGES_G,
         PIPE_STAGES_G       => PIPE_STAGES_G,
         SLAVE_READY_EN_G    => true,
         VALID_THOLD_G       => VALID_THOLD_G,
         VALID_BURST_MODE_G  => VALID_BURST_MODE_G,
         -- FIFO configurations
         SYNTH_MODE_G        => SYNTH_MODE_G,
         MEMORY_TYPE_G       => FIFO_MEMORY_TYPE_G,
         GEN_SYNC_FIFO_G     => GEN_SYNC_FIFO_G,
         FIFO_ADDR_WIDTH_G   => FIFO_ADDR_WIDTH_G,
         -- AXI Stream Port Configurations
         SLAVE_AXI_CONFIG_G  => AXIS_CONFIG_C,
         MASTER_AXI_CONFIG_G => AXI_STREAM_CONFIG_G)
      port map (
         -- Slave Port
         sAxisClk    => dataClk,
         sAxisRst    => dataRst,
         sAxisMaster => r.txMaster,
         sAxisSlave  => txSlave,
         -- Master Port
         mAxisClk    => axisClk,
         mAxisRst    => axisRst,
         mAxisMaster => axisMaster,
         mAxisSlave  => axisSlave);

end mapping;
