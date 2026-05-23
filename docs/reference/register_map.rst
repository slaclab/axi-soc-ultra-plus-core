Register Map
============

This page documents the AXI-Lite addressing model used across all ``axi-soc-ultra-plus-core``
platform designs. The same hierarchical crossbar pattern appears in every ``Simple-*-Example``
application repo; only the per-application sub-indices (documented in each application repo's
own reference pages) differ.

Hierarchical AXI-Lite crossbar
-------------------------------

The Zynq UltraScale+ PS initiates all AXI-Lite register transactions. These are forwarded
across the PS-to-PL boundary through the ``AxiSocUltraPlusCore`` block and dispatched by a
two-level crossbar tree:

**Top-level crossbar** (inside the application's top-level VHDL entity):

.. code-block:: text

   PS AXI-Lite master
        │
        ▼
   AxiSocUltraPlusCore (AXI-Lite bridge)
        │
        ▼
   Top-level AxiLiteCrossbar (3 slaves, 28-bit decode)
        ├─ [0] HW_INDEX_C   → AxiSocUltraPlusCore registers
        ├─ [1] RFDC_INDEX_C → RfDataConverter / RFDC IP registers
        └─ [2] APP_INDEX_C  → Application crossbar (second level)

**Application-level crossbar** (inside ``Application.vhd``):

.. code-block:: text

   APP_INDEX_C slave
        │
        ▼
   Application AxiLiteCrossbar (N slaves, 24-bit decode)
        ├─ [0] → AppRingBuffer registers
        └─ [1] → DacSigGen registers
        └─ ... (per-application; documented in each application repo)

The full address of any register is the sum of offsets along the path from the PS base address
to the register's leaf node. Python-side offsets in ``pr.Device`` subclasses must match the
VHDL crossbar configuration exactly.

``genAxiLiteConfig`` pattern
-----------------------------

The ``surf`` library function ``genAxiLiteConfig`` generates the ``AXIL_CONFIG_C`` constant
array that parameterizes every ``AxiLiteCrossbar`` instance:

.. code-block:: vhdl

   -- Declare the number of slaves and index constants
   constant NUM_AXIL_MASTERS_C : natural := 3;
   constant HW_INDEX_C         : natural := 0;
   constant RFDC_INDEX_C       : natural := 1;
   constant APP_INDEX_C        : natural := 2;

   -- Generate the crossbar configuration
   constant AXIL_CONFIG_C : AxiLiteCrossbarMasterConfigArray(NUM_AXIL_MASTERS_C-1 downto 0) :=
       genAxiLiteConfig(NUM_AXIL_MASTERS_C, AXIL_BASE_ADDR_G, 28, 26);

Arguments to ``genAxiLiteConfig``:

- ``NUM_MASTERS`` — number of slave slots
- ``BASE_ADDR`` — base address of the crossbar (passed in as a generic)
- ``ADDR_BITS`` — total decode width in bits
- ``DECODE_BITS`` — bits used to select among slaves (upper bits of the decode window)

Each slave's base address is then ``AXIL_CONFIG_C(INDEX).baseAddr``.

The Python device tree mirrors these addresses:

.. code-block:: python

   # Top-level offsets from the PS base address 0x04_0000_0000
   # HW_INDEX_C=0  → AxiSocCore at offset 0x0000_0000
   # RFDC_INDEX_C=1 → Rfdc device at offset determined by AXIL_CONFIG_C(1).baseAddr
   # APP_INDEX_C=2  → Application device at offset 0xA000_0000

   self.add(soc_core.AxiSocCore(
       offset = 0x0000_0000,
   ))
   self.add(Application(
       offset = 0xA000_0000,
   ))

Signal initialization for undriven slaves uses the DECERR constant:

.. code-block:: vhdl

   signal axilReadSlaves : AxiLiteReadSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0) :=
       (others => AXI_LITE_READ_SLAVE_EMPTY_DECERR_C);
   signal axilWriteSlaves : AxiLiteWriteSlaveArray(NUM_AXIL_MASTERS_C-1 downto 0) :=
       (others => AXI_LITE_WRITE_SLAVE_EMPTY_DECERR_C);

Any unmapped address returns DECERR rather than hanging the bus.

``Slv256Array`` sample bus
---------------------------

ADC and DAC sample data between the RFDC wrapper and the application logic flows on a
256-bit parallel bus at ``dspClk`` (312.5 MHz). The type is defined in ``surf.StdRtlPkg``:

.. code-block:: vhdl

   -- 4 ADC channels, each 256 bits wide
   dspAdc : in  Slv256Array(3 downto 0);
   -- 2 DAC channels, each 256 bits wide
   dspDac : out Slv256Array(1 downto 0);

Each 256-bit word carries ``SAMPLE_PER_CYCLE_C = 16`` samples of 16 bits each:

- 16 samples × 16 bits = 256 bits per channel per ``dspClk`` cycle
- At 312.5 MHz, this yields an effective sample rate of 312.5 MHz × 16 = 5 GS/s throughput
  (the actual ADC/DAC sample rate is set by the RFDC tile configuration)

The ``Slv256Array`` type is generic across all channel counts. Each application's top-level
entity declares the port widths to match its board's RFDC configuration.

``AppPkg`` constants pattern
-----------------------------

Each application repo contains a VHDL package ``AppPkg.vhd`` that declares the
design-wide constants tying RTL generics to Python parameters:

.. code-block:: vhdl

   package AppPkg is

      -- DMA lane count: lane 0 = ring buffer data, lane 1 = loopback debug
      constant DMA_SIZE_C : positive := 2;

      -- Samples per dspClk cycle — must match smplPerCycle in Python Application device
      constant SAMPLE_PER_CYCLE_C : positive := 16;

      -- AXI-Lite clock frequency (used by surf timing primitives)
      constant AXIL_CLK_FREQ_C : real := 100.0E+6;

   end package AppPkg;

These constants are consumed by RTL generics throughout the design:

.. code-block:: vhdl

   U_AppRingBuffer : entity axi_soc_ultra_plus_core.AppRingBuffer
       generic map (
           ADC_SAMPLE_PER_CYCLE_G => SAMPLE_PER_CYCLE_C,
           DAC_SAMPLE_PER_CYCLE_G => SAMPLE_PER_CYCLE_C,
           DMA_SIZE_G             => DMA_SIZE_C)
       port map (...);

The Python ``Application`` device must pass a matching value:

.. code-block:: python

   self.add(hardware.AppRingBuffer(
       numAdcCh     = NUM_ADC_CH_C,
       numDacCh     = NUM_DAC_CH_C,
       smplPerCycle = SAMPLE_PER_CYCLE_C,   # must equal AppPkg.SAMPLE_PER_CYCLE_C
   ))

Changing ``SAMPLE_PER_CYCLE_C`` requires synchronized updates in both the VHDL package and
the Python device tree. The two values are not automatically cross-checked at build time.

External references
-------------------

- `Zynq UltraScale+ Devices Register Reference (UG1087)
  <https://www.xilinx.com/html_docs/registers/ug1087/ug1087-zynq-ultrascale-registers.html>`_
  — authoritative PS register map for boot-mode, PMU, and diagnostic register addresses
  referenced in platform debug recipes.
