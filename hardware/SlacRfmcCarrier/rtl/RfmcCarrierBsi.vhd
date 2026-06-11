-------------------------------------------------------------------------------
-- Company    : SLAC National Accelerator Laboratory
-------------------------------------------------------------------------------
-- Description: BootStrap Interface (BSI) to the IPMI's controller (IPMC)
-------------------------------------------------------------------------------
-- This file is part of 'LCLS2 Common Carrier Core'.
-- It is subject to the license terms in the LICENSE.txt file found in the
-- top-level directory of this distribution and at:
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
-- No part of 'LCLS2 Common Carrier Core', including this file,
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
use surf.i2cPkg.all;

library unisim;
use unisim.vcomponents.all;

entity RfmcCarrierBsi is
   generic (
      TPD_G        : time := 1 ns;
      BUILD_INFO_G : BuildInfoType);
   port (
      -- I2C Ports
      scl             : inout sl;
      sda             : inout sl;
      -- AXI-Lite Register Interface
      axilReadMaster  : in    AxiLiteReadMasterType;
      axilReadSlave   : out   AxiLiteReadSlaveType;
      axilWriteMaster : in    AxiLiteWriteMasterType;
      axilWriteSlave  : out   AxiLiteWriteSlaveType;
      -- Clocks and Resets
      axilClk         : in    sl;
      axilRst         : in    sl);
end RfmcCarrierBsi;

architecture rtl of RfmcCarrierBsi is

   constant BSI_MAC_SIZE_C : natural := 4;

   constant BUILD_INFO_C : BuildInfoRetType := toBuildInfo(BUILD_INFO_G);

   constant BSI_MAJOR_VERSION_C : slv(7 downto 0) := x"01";
   constant BSI_MINOR_VERSION_C : slv(7 downto 0) := x"03";

   constant TIMEOUT_1HZ_C : natural := (getTimeRatio(1.0, 6.4E-9) -1);

   type RomType is array (0 to 255) of slv(7 downto 0);

   function makeStringRom return RomType is
      variable ret : RomType := (others => (others => '0'));
   begin
      ret(0) := x"00";
      for i in 0 to 254 loop
         ret(i+1) := BUILD_INFO_C.buildString(i/4)(8*(i mod 4)+7 downto 8*(i mod 4));
      end loop;
      return ret;
   end function makeStringRom;

   signal stringRom : RomType := makeStringRom;

   function ConvertEndianness (word : slv(47 downto 0)) return slv is
      variable retVar : slv(47 downto 0);
   begin
      retVar(47 downto 40) := word(7 downto 0);
      retVar(39 downto 32) := word(15 downto 8);
      retVar(31 downto 24) := word(23 downto 16);
      retVar(23 downto 16) := word(31 downto 24);
      retVar(15 downto 8)  := word(39 downto 32);
      retVar(7 downto 0)   := word(47 downto 40);
      return retVar;
   end function;

   type RegType is record
      ethUpTimeCnt   : slv(31 downto 0);
      timer          : natural range 0 to TIMEOUT_1HZ_C;
      cnt            : slv(3 downto 0);
      addr           : slv(7 downto 0);
      we             : sl;
      ramData        : slv(7 downto 0);
      bootReq        : sl;
      bootAddr       : slv(31 downto 0);
      slotNumber     : slv(7 downto 0);
      crateId        : slv(15 downto 0);
      macAddress     : Slv48Array(BSI_MAC_SIZE_C-1 downto 0);
      localIp        : slv(31 downto 0);
      rst            : slv(2 downto 0);
      axilReadSlave  : AxiLiteReadSlaveType;
      axilWriteSlave : AxiLiteWriteSlaveType;
   end record;

   constant REG_INIT_C : RegType := (
      ethUpTimeCnt   => (others => '0'),
      timer          => 0,
      cnt            => x"0",
      addr           => x"00",
      we             => '0',
      ramData        => x"00",
      bootReq        => '0',
      bootAddr       => x"04000000",    -- Default to 2nd stage boot
      slotNumber     => x"00",
      crateId        => x"0000",
      macAddress     => (others => (others => '0')),
      localIp        => x"0000000A",
      rst            => (others => '0'),
      axilReadSlave  => AXI_LITE_READ_SLAVE_INIT_C,
      axilWriteSlave => AXI_LITE_WRITE_SLAVE_INIT_C);

   signal r   : RegType := REG_INIT_C;
   signal rin : RegType;

   signal i2cBramWr   : slv(1 downto 0);
   signal i2cBramAddr : Slv8Array(1 downto 0);
   signal i2cBramDout : Slv8Array(1 downto 0);
   signal i2cBramDin  : Slv8Array(1 downto 0);
   signal bramDout    : slv(7 downto 0);
   signal ramData     : slv(7 downto 0);
   signal slaveIn     : i2c_in_array(1 downto 0);
   signal slaveOut    : i2c_out_array(1 downto 0);
   signal i2cIn       : i2c_in_type;
   signal i2cOut      : i2c_out_type;

begin

   U_I2cScl : IOBUF
      port map (
         IO => scl,
         I  => i2cOut.scl,
         O  => i2cIn.scl,
         T  => i2cOut.scloen);

   U_I2cSda : IOBUF
      port map (
         IO => sda,
         I  => i2cOut.sda,
         O  => i2cIn.sda,
         T  => i2cOut.sdaoen);

   -------------------------
   -- MUX I2C Slave together
   -------------------------
   slaveIn(0).scl <= i2cIn.scl;
   slaveIn(1).scl <= i2cIn.scl;
   slaveIn(0).sda <= i2cIn.sda;
   slaveIn(1).sda <= i2cIn.sda;

   i2cOut.scl    <= '0';
   i2cOut.scloen <= slaveOut(0).scloen and slaveOut(1).scloen;
   i2cOut.sda    <= '0';
   i2cOut.sdaoen <= slaveOut(0).sdaoen and slaveOut(1).sdaoen;

   -------------------
   -- I2c Slave @ 0x49
   -------------------
   U_I2C_SLAVE_0x49 : entity surf.i2cRegSlave
      generic map (
         TPD_G                => TPD_G,
         TENBIT_G             => 0,
         I2C_ADDR_G           => 73,    -- "1001001";
         OUTPUT_EN_POLARITY_G => 0,
         FILTER_G             => 4,
         ADDR_SIZE_G          => 1,     -- in bytes
         DATA_SIZE_G          => 1,     -- in bytes
         ENDIANNESS_G         => 1)     -- 0=LE, 1=BE
      port map (
         clk    => axilClk,
         sRst   => r.rst(0),
         aRst   => '0',
         addr   => i2cBramAddr(0),
         wrEn   => i2cBramWr(0),
         wrData => i2cBramDin(0),
         rdEn   => open,
         rdData => i2cBramDout(0),
         i2ci   => slaveIn(0),
         i2co   => slaveOut(0));

   -------------------
   -- I2c Slave @ 0x51
   -------------------
   U_I2C_SLAVE_0x51 : entity surf.i2cRegSlave
      generic map (
         TPD_G                => TPD_G,
         TENBIT_G             => 0,
         I2C_ADDR_G           => 81,    -- "1010001";
         OUTPUT_EN_POLARITY_G => 0,
         FILTER_G             => 4,
         ADDR_SIZE_G          => 1,     -- in bytes
         DATA_SIZE_G          => 1,     -- in bytes
         ENDIANNESS_G         => 1)     -- 0=LE, 1=BE
      port map (
         clk    => axilClk,
         sRst   => r.rst(0),
         aRst   => '0',
         addr   => i2cBramAddr(1),
         wrEn   => i2cBramWr(1),
         wrData => i2cBramDin(1),
         rdEn   => open,
         rdData => i2cBramDout(1),
         i2ci   => slaveIn(1),
         i2co   => slaveOut(1));

   ----------------
   -- Dual port RAM
   ----------------
   U_RAM : entity surf.TrueDualPortRam
      generic map (
         TPD_G        => TPD_G,
         MODE_G       => "read-first",
         DATA_WIDTH_G => 8,
         ADDR_WIDTH_G => 8)
      port map (
         -- Port A
         clka  => axilClk,
         wea   => i2cBramWr(0),
         addra => i2cBramAddr(0),
         dina  => i2cBramDin(0),
         douta => i2cBramDout(0),
         -- Port B
         clkb  => axilClk,
         web   => r.we,
         addrb => r.addr,
         dinb  => r.ramData,
         doutb => ramData);

   ------------------
   -- Single port ROM
   ------------------
   process (axilClk) is
   begin
      if (rising_edge(axilClk)) then
         i2cBramDout(1) <= stringRom(conv_integer(i2cBramAddr(1))) after TPD_G;
      end if;
   end process;

   ---------------------
   -- AXI Lite Interface
   ---------------------
   comb : process (axilReadMaster, axilRst, axilWriteMaster, r, ramData) is
      variable v      : RegType;
      variable regCon : AxiLiteEndPointType;
      variable i      : natural;
      variable index  : natural;
   begin
      -- Latch the current value
      v := r;

      -- Reset the strobe
      v.we := '0';

      -- Increment the counter
      v.cnt := r.cnt + 1;

      -- Update the index
      index := conv_integer(r.addr(7 downto 4));

      -- Check counter phase
      if r.cnt = x"0" then
         -- Increment the address
         v.addr := r.addr + 1;
      elsif r.cnt = x"8" then
         -- Reset the data bus
         v.ramData := x"00";
         -- Check the address bus
         case (r.addr) is
            ---------------------------------------
            -- Check for ATCA slot number
            ---------------------------------------
            when x"FF" => v.slotNumber           := ramData;
            ---------------------------------------
            -- Check for ATCA Crate ID
            ---------------------------------------
            when x"FE" => v.crateId(15 downto 8) := ramData;
            when x"FD" => v.crateId(7 downto 0)  := ramData;
            ---------------------------------------
            -- Check for BSI Version
            ---------------------------------------
            when x"FC" =>
               v.we      := '1';
               v.ramData := BSI_MINOR_VERSION_C;
            when x"FB" =>
               v.we      := '1';
               v.ramData := BSI_MAJOR_VERSION_C;
            ---------------------------------------
            -- Check for start boot
            ---------------------------------------
            when x"FA" =>
               -- Sample the LSB of the memory byte
               v.bootReq := ramData(0);
               -- Reset memory
               v.we      := '1';
               v.ramData := x"00";
            ---------------------------------------
            -- Check for boot address
            ---------------------------------------
            when x"F9" => v.bootAddr(31 downto 24) := ramData;
            when x"F8" => v.bootAddr(23 downto 16) := ramData;
            when x"F7" => v.bootAddr(15 downto 8)  := ramData;
            when x"F6" => v.bootAddr(7 downto 0)   := ramData;
            ---------------------------------------
            -- Check for BUILD_INFO_C.fwVersion
            ---------------------------------------
            when x"F5" =>
               v.we      := '1';
               v.ramData := BUILD_INFO_C.fwVersion(31 downto 24);
            when x"F4" =>
               v.we      := '1';
               v.ramData := BUILD_INFO_C.fwVersion(23 downto 16);
            when x"F3" =>
               v.we      := '1';
               v.ramData := BUILD_INFO_C.fwVersion(15 downto 8);
            when x"F2" =>
               v.we      := '1';
               v.ramData := BUILD_INFO_C.fwVersion(7 downto 0);
            ---------------------------------------
            -- Check for DDR Memory Status
            ---------------------------------------
            when x"F1" =>
               v.we         := '1';
               v.ramData(0) := '0';           -- ddrMemError
            when x"F0" =>
               v.we         := '1';
               v.ramData(1) := '0';           -- ethLinkUp
               v.ramData(0) := '0';           -- ddrMemReady
            ----------------------------------------
            -- Check for AxiVersion's Uptime Counter
            ----------------------------------------
            when x"EF" =>
               v.we      := '1';
               v.ramData := (others => '1');  -- upTimeCnt(31 downto 24)
            when x"EE" =>
               v.we      := '1';
               v.ramData := (others => '1');  -- upTimeCnt(23 downto 16)
            when x"ED" =>
               v.we      := '1';
               v.ramData := (others => '1');  -- upTimeCnt(15 downto 8)
            when x"EC" =>
               v.we      := '1';
               v.ramData := (others => '1');  -- upTimeCnt(7 downto 0)
            --------------------------------------
            -- Check for Ethernet's Uptime Counter
            --------------------------------------
            when x"EB" =>
               v.we      := '1';
               v.ramData := r.ethUpTimeCnt(31 downto 24);
            when x"EA" =>
               v.we      := '1';
               v.ramData := r.ethUpTimeCnt(23 downto 16);
            when x"E9" =>
               v.we      := '1';
               v.ramData := r.ethUpTimeCnt(15 downto 8);
            when x"E8" =>
               v.we      := '1';
               v.ramData := r.ethUpTimeCnt(7 downto 0);
            -------------------
            -- Get the GIT HASH
            -------------------
            when x"D0" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(7 downto 0);
            when x"D1" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(15 downto 8);
            when x"D2" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(23 downto 16);
            when x"D3" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(31 downto 24);
            when x"D4" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(39 downto 32);
            when x"D5" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(47 downto 40);
            when x"D6" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(55 downto 48);
            when x"D7" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(63 downto 56);
            when x"D8" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(71 downto 64);
            when x"D9" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(79 downto 72);
            when x"DA" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(87 downto 80);
            when x"DB" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(95 downto 88);
            when x"DC" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(103 downto 96);
            when x"DD" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(111 downto 104);
            when x"DE" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(119 downto 112);
            when x"DF" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(127 downto 120);
            when x"E0" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(135 downto 128);
            when x"E1" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(143 downto 136);
            when x"E2" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(151 downto 144);
            when x"E3" => v.we := '1'; v.ramData := BUILD_INFO_C.gitHash(159 downto 152);
            ---------------------------------------
            when others =>
               if (index < BSI_MAC_SIZE_C) then
                  -- Check for available MAC addresses
                  case (r.addr(3 downto 0)) is
                     when x"0"   => v.macAddress(index)(7 downto 0)   := ramData;
                     when x"1"   => v.macAddress(index)(15 downto 8)  := ramData;
                     when x"2"   => v.macAddress(index)(23 downto 16) := ramData;
                     when x"3"   => v.macAddress(index)(31 downto 24) := ramData;
                     when x"4"   => v.macAddress(index)(39 downto 32) := ramData;
                     when x"5"   => v.macAddress(index)(47 downto 40) := ramData;
                     when others => null;
                  end case;
               end if;
         end case;
      end if;

      -- Update the local IP addresses
      v.localIp(15 downto 8)  := r.crateId(15 downto 8);
      v.localIp(23 downto 16) := r.crateId(7 downto 0);
      v.localIp(31 downto 24) := (100 + r.slotNumber);

      -- Determine the transaction type
      axiSlaveWaitTxn(regCon, axilWriteMaster, axilReadMaster, v.axilWriteSlave, v.axilReadSlave);

      -- Map the read registers
      for i in BSI_MAC_SIZE_C-1 downto 0 loop
         axiSlaveRegisterR(regCon, toSlv(8*i, 8), 0, r.macAddress(i));
      end loop;
      axiSlaveRegisterR(regCon, x"80", 0, r.crateId);
      axiSlaveRegisterR(regCon, x"84", 0, r.slotNumber);
      axiSlaveRegisterR(regCon, x"88", 0, r.bootAddr);
      axiSlaveRegisterR(regCon, x"8C", 0, BSI_MINOR_VERSION_C);
      axiSlaveRegisterR(regCon, x"90", 8, BSI_MAJOR_VERSION_C);
      axiSlaveRegisterR(regCon, x"94", 0, r.ethUpTimeCnt);

      -- Closeout the transaction
      axiSlaveDefault(regCon, v.axilWriteSlave, v.axilReadSlave, AXI_RESP_DECERR_C);

      -- Synchronous Reset
      if (axilRst = '1') then
         v := REG_INIT_C;
      end if;

      v.rst := axilRst & r.rst(r.rst'left downto 1);

      -- Register the variable for next clock cycle
      rin <= v;

      -- Outputs
      axilWriteSlave <= r.axilWriteSlave;
      axilReadSlave  <= r.axilReadSlave;

   end process comb;

   seq : process (axilClk) is
   begin
      if (rising_edge(axilClk)) then
         r <= rin after TPD_G;
      end if;
   end process seq;

end rtl;
