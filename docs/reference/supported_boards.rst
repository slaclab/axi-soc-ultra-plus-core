Supported Boards
================

The following application repos target different Xilinx Zynq UltraScale+ SoC boards using the
``axi-soc-ultra-plus-core`` platform. Each repo contains a board-specific FPGA target under
``firmware/targets/<TargetName>/`` with its own top-level VHDL entity, XDC constraints, and
``ruckus.tcl`` build script.

.. list-table:: Simple-\*-Example Application Repos
   :header-rows: 1
   :widths: 32 24 22 25 15

   * - Repo
     - Board model
     - Xilinx part number
     - Target dir name
     - Notes
   * - `Simple-rfsoc-4x2-Example <https://github.com/slaclab/Simple-rfsoc-4x2-Example>`_
     - RealDigital RFSoC 4x2
     - xczu48dr-ffvg1517-2-e
     - SimpleRfSoc4x2Example
     - Verified end-to-end
   * - `Simple-Kria-Kv260-Example <https://github.com/slaclab/Simple-Kria-Kv260-Example>`_
     - Xilinx KV260 Vision AI Starter Kit
     - xck26-sfvc784-2lv-c
     - SimpleKriaKv260Example
     - Pending verification
   * - `Simple-ZCU102-Example <https://github.com/slaclab/Simple-ZCU102-Example>`_
     - Xilinx ZCU102
     - xczu9eg-ffvb1156-2-e
     - SimpleZcu102Example
     - Pending verification
   * - `Simple-ZCU111-Example <https://github.com/slaclab/Simple-ZCU111-Example>`_
     - Xilinx ZCU111
     - xczu28dr-ffvg1517-2-e
     - SimpleZcu111Example
     - Pending verification
   * - `Simple-ZCU208-Example <https://github.com/slaclab/Simple-ZCU208-Example>`_
     - Xilinx ZCU208
     - xczu48dr-fsvg1517-2-e
     - SimpleZcu208Example
     - Pending verification
   * - `Simple-ZCU216-Example <https://github.com/slaclab/Simple-ZCU216-Example>`_
     - Xilinx ZCU216
     - xczu49dr-ffvf1760-2-e
     - SimpleZcu216Example
     - Pending verification
   * - `Simple-ZCU670-Example <https://github.com/slaclab/Simple-ZCU670-Example>`_
     - Xilinx ZCU670
     - xczu67dr-fsve1156-2-i
     - SimpleZcu670Example
     - Pending verification
   * - `Simple-SLAC-rfmc-carrier-example <https://github.com/slaclab/Simple-SLAC-rfmc-carrier-example>`_
     - SLAC RFMC carrier
     - xczu48dr-ffvg1517-2-e
     - SimpleSlacRfmcCarrierExample
     - Pending verification

The ``hardware/TrenzTe0835`` board directory has no public ``Simple-*-Example`` repo.

Finding the target directory name
---------------------------------

The target directory name maps directly to the Vivado project name and to the top-level VHDL
entity. To find the name for any repo, list ``firmware/targets/``. The target's ``Makefile``
carries the authoritative Xilinx device part string used during synthesis in its ``PRJ_PART``
variable:

.. code-block:: bash

   ls firmware/targets/
   grep PRJ_PART firmware/targets/<TargetName>/Makefile
