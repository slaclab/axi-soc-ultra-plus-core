Platform Architecture
=====================

This page is the platform-level architecture narrative for ``axi-soc-ultra-plus-core``.
The platform provides the core infrastructure — AXI-Lite register bridge, DMA engine,
RFDC wrapper, ring-buffer IP, and signal-generator IP — that every ``Simple-*-Example``
application repo builds upon. Readers looking for per-board details should consult
:doc:`../reference/supported_boards` and each application repo's own documentation.

System overview
---------------

The platform connects a remote host machine to a Zynq UltraScale+ RFSoC device over TCP.
The PS (Processing System) runs Linux; the PL (Programmable Logic) implements the FPGA
design. All register access and DMA data transfers cross the PS-to-PL boundary through the
``AxiSocUltraPlusCore`` block.

.. code-block:: text

   ┌─────────────────────────────────────────────────────────────────────────┐
   │                     Host Machine (Remote Client)                         │
   │  software/scripts/devGui.py  |  software/notebooks/rfsoc_example.ipynb  │
   └───────────────────────┬──────────────────────────────────────────────────┘
                           │  TCP (ports 9000/9001 AXI-Lite, 9002 RFDC,
                           │       10000+ AXI Stream ring buffers)
   ┌───────────────────────▼──────────────────────────────────────────────────┐
   │                   RFSoC Board (PS + PL, Zynq UltraScale+)                │
   │                                                                           │
   │   ┌────────────────────────────────────────────────────────────────────┐ │
   │   │                          PL (FPGA)                                 │ │
   │   │                                                                    │ │
   │   │  ┌─────────────────┐  ┌───────────────┐  ┌─────────────────────┐ │ │
   │   │  │  AxiSocUltra    │  │ RfDataConverter│  │    Application      │ │ │
   │   │  │  PlusCore (HW)  │  │ (RFDC wrapper) │  │  (app-specific)     │ │ │
   │   │  │  [submodule]    │  │                │  │                     │ │ │
   │   │  │                 │  │                │  │  ┌─────────────┐   │ │ │
   │   │  │  DMA engine     │  │  dspClk domain │  │  │AppRingBuffer│   │ │ │
   │   │  │  axilClk domain │  │  axilClk domain│  │  └─────────────┘   │ │ │
   │   │  └────────┬────────┘  └───────┬────────┘  │  ┌─────────────┐   │ │ │
   │   │           │ AXI-Lite XBAR     │            │  │  SigGen     │   │ │ │
   │   │           │ (3 masters)       │ dspAdc/Dac │  │ (DacSigGen) │   │ │ │
   │   │           └───────────────────┴───────────►│  └─────────────┘   │ │ │
   │   │                                            └─────────────────────┘ │ │
   │   └────────────────────────────────────────────────────────────────────┘ │
   └──────────────────────────────────────────────────────────────────────────┘

Three major PL blocks make up the platform:

- **AxiSocUltraPlusCore** — the platform core, provided as a git submodule. It implements
  the PS-to-PL AXI-Lite bridge, the DMA engine, and system monitor. Application repos do
  not modify this block.
- **RfDataConverter** — wraps the Xilinx RF Data Converter IP core. Generates ``dspClk``
  from the reference PLL, applies ADC P/N swap and polarity corrections, and gear-boxes the
  ``adcClock`` (416.667 MHz) RFDC output down to the 312.5 MHz DSP clock domain.
- **Application** — the board-specific application logic. Contains the second-level
  AXI-Lite crossbar, ``AppRingBuffer`` (ADC/DAC capture), and ``DacSigGen`` (waveform
  generator). Each ``Simple-*-Example`` repo provides its own ``Application.vhd``.

Clock domains
-------------

The design has three independent, asynchronous clock domains. The XDC constraints file in
each application repo declares all three as asynchronous clock groups (lines 12–17 of
``SimpleRfSoc4x2Example.xdc``).

``axilClk`` — 100 MHz
   The AXI-Lite control-plane clock. All register reads and writes cross the PS-to-PL
   boundary in this domain. The ``AxiSocUltraPlusCore`` block, the top-level crossbar, and
   the AXI-Lite clock-frequency constant ``AXIL_CLK_FREQ_C`` (100.0E+6, declared in
   ``AppPkg.vhd``) all reference this domain.

``dspClk`` — 312.5 MHz
   The DSP and DAC sample-rate clock. The ``Application`` block, ``AppRingBuffer``,
   ``DacSigGen``, and the 256-bit ``Slv256Array`` sample bus all run on ``dspClk``.
   ``RfDataConverter`` generates ``dspClk`` internally from a PLL driven by the RFDC
   reference clock input.

``adcClock`` — 416.667 MHz
   The RFDC ADC output clock. The Xilinx RF Data Converter IP delivers digitised samples
   on ``adcClock``. The ``Ssr12ToSsr16Gearbox`` primitive converts 12 samples per cycle at
   416.667 MHz into 16 samples per cycle at ``dspClk`` (312.5 MHz), crossing the domain
   boundary in the process.

All clock-domain crossings (CDC) must use a ``surf`` ``Synchronizer`` primitive or the
``Ssr12ToSsr16Gearbox``. No direct flip-flop paths are permitted between the three async
groups.

The ``Application`` PyRogue device must be enabled only after ``dspClk`` is stable.
The ``Root.start()`` startup sequence enforces this order: ``InitClock`` → ``DspRstWait``
→ ``Application.enable`` → ``Rfdc.Init`` → MTS sync → config load.

DMA model
---------

The ``AxiSocUltraPlusCore`` DMA engine moves ADC and DAC ring-buffer data between the PL
and PS DDR over AXI Stream. The lane count is set by the ``DMA_SIZE_C`` constant in each
application's ``AppPkg.vhd`` (conventional value: ``DMA_SIZE_C = 2``).

Lane assignment:

- **Lane 0** — ADC/DAC ring-buffer data. ``AppRingBuffer`` writes captured samples into
  inbound DMA frames; the PS forwards them over TCP stream ports (10000 and above, one
  port per channel).
- **Lane 1** — Hard-wired loopback for DMA path debug. The top-level VHDL wires
  ``dmaIbMasters(1) <= dmaObMasters(1)`` directly.

On the Linux side, the ``aes-stream-drivers`` kernel module provides the device nodes used
by the Rogue framework:

- ``/dev/axi_memory_map`` — memory-mapped AXI-Lite register access
- ``/dev/axi_stream_dma_0`` — AXI Stream DMA channel for ring buffer data

The application ``Root`` (PyRogue) connects to the board through Rogue's TCP bridge rather
than through these device nodes directly. The firmware runs a TCP server on the board;
the host connects as a TCP client. Host-side drop FIFOs (``adcDropFifo``, ``dacDropFifo``,
``maxDepth=1``) prevent host-side backpressure from stalling the firmware DMA path.

Adding DMA lanes requires updating ``DMA_SIZE_C`` in ``AppPkg.vhd`` and the corresponding
stream wiring in the application ``Root._Root.py``.

For the host-side Rogue stream pipeline and the ``RingBufferProcessor`` API, see
:doc:`../reference/pyrogue_api`.

Sample bus
----------

ADC and DAC sample data between the RFDC wrapper and the ``Application`` block flows on the
``Slv256Array`` bus — a 256-bit parallel bus clocked at ``dspClk`` (312.5 MHz). The type
is defined in ``surf.StdRtlPkg``.

Each 256-bit word carries ``SAMPLE_PER_CYCLE_C = 16`` samples of 16 bits each:

.. code-block:: vhdl

   -- 4 ADC input channels, one Slv256 per channel
   dspAdc : in  Slv256Array(3 downto 0);
   -- 2 DAC output channels, one Slv256 per channel
   dspDac : out Slv256Array(1 downto 0);

The constant ``SAMPLE_PER_CYCLE_C`` (declared in ``AppPkg.vhd``) links VHDL generics
(``ADC_SAMPLE_PER_CYCLE_G``, ``DAC_SAMPLE_PER_CYCLE_G``, ``SAMPLE_PER_CYCLE_G``) to the
Python ``Application`` device parameter ``smplPerCycle``. Changing this constant requires
synchronized updates in both the VHDL package and the Python device tree — the two values
are not cross-checked automatically at build time.

For the full ``AppPkg`` constants layout and how they feed the crossbar configuration, see
:doc:`../reference/register_map`.

AXI-Lite hierarchy summary
---------------------------

All register access flows from the PS ARM core through the ``AxiSocUltraPlusCore`` AXI-Lite
bridge into a two-level crossbar tree instantiated in the application's top-level VHDL
entity:

1. **Top-level crossbar** decodes the address space into three slaves:
   ``HW_INDEX_C=0`` (platform core registers), ``RFDC_INDEX_C=1`` (RFDC IP registers),
   and ``APP_INDEX_C=2`` (application logic).
2. **Application crossbar** (inside ``Application.vhd``) further decodes the application
   address space: ``RING_INDEX_C=0`` (``AppRingBuffer`` registers) and
   ``DAC_SIG_INDEX_C=1`` (``DacSigGen`` registers).

The Python ``pr.Device`` tree mirrors this hierarchy exactly. Each device's ``offset``
argument must equal the ``AXIL_CONFIG_C(INDEX).baseAddr`` value generated by the
``genAxiLiteConfig`` VHDL call. For the full addressing pattern, constant definitions, and
Python offset examples, see :doc:`../reference/register_map`.

Further reading
---------------

- :doc:`../reference/register_map` — AXI-Lite addressing model, ``genAxiLiteConfig``
  pattern, ``AppPkg`` constants, and the ``Slv256Array`` sample bus definition
- :doc:`../reference/pyrogue_api` — platform PyRogue classes (``AxiSocCore``,
  ``AppRingBuffer``, ``RingBufferProcessor``, ``Rfdc``, and the PyDM GUI launcher)
- :doc:`../reference/supported_boards` — list of ``Simple-*-Example`` application repos,
  board models, Xilinx part numbers, and target directory names
