SlacRfmcCarrier RF Data Converter Configuration
===============================================

This page documents how the SlacRfmcCarrier board builds the ``param-list`` property of the RF
Data Converter device tree node at build time, and the one variable every application on this
board must set for it: ``RFDC_XCI``. A reader bringing up an application on this board cares
because the Yocto build stops without it. The recipe that enforces the requirement is
:repo:`hardware/SlacRfmcCarrier/Yocto/recipes-bsp/device-tree/device-tree.bbappend`.

Why the param-list comes from the .xci
--------------------------------------

On this board the RF data converter IP is instantiated from HDL outside the block design, so the
device tree generator never sees it and cannot write the ``param-list`` the converter driver reads
its configuration from. The encoder
:repo:`shared/Yocto/recipes-bsp/device-tree/files/rfdc_param_list.py`, shipped by the shared
``device-tree`` bbappend, encodes the ``param-list`` from the IP's ``.xci`` instead. An empty
``param-list`` never fails at runtime: the driver copies its configuration out of the property
without checking how many bytes it read, so it runs on a configuration it did not read, whose IP
type then comes from uninitialized memory. That is why the requirement is enforced at build time.

What each application sets
--------------------------

Set ``RFDC_XCI`` in ``<project>/shared/Yocto/local.conf``, which the Yocto build script appends to
``build/conf/local.conf`` on a fresh or ``-c`` configure:

.. code-block:: none

   RFDC_XCI = "${PROJ_TOP}/shared/ip/<YourRfdcIp>.xci"

The ``.xci`` must be the one the design's bitstream is built from. The recipe tracks the content
of that file, so a change to it re-runs ``do_configure`` and sstate cannot hand back a device tree
encoded from an older ``.xci``.

When it is not set
------------------

The build stops in ``do_configure`` with this message:

.. code-block:: none

   SlacRfmcCarrier: RFDC_XCI is not set. Point RFDC_XCI at the usp_rf_data_converter .xci that the design's bitstream is built from; set it in <project>/shared/Yocto/local.conf, which the Yocto build script appends to build/conf/local.conf on a fresh or -c configure.

Policy the board recipe sets
----------------------------

- ``RFDC_PARAM_LIST_POLICY = "fail"``: the shared deploy check fails the build, rather than
  warning, when the device tree does not carry a full converter configuration.
- ``RFDC_EXPECTED_IPTYPE = "2"``: the check also requires the Gen3 IP type (``XRFDC_GEN3``).
- ``RFDC_BASEADDR = "0x490000000"``: the register base of the converter node in
  ``system-user.dtsi``. It fills only the configuration's base address, because the driver on
  Linux maps the registers from ``reg``. With ``RFDC_XCI`` also set, the check requires the
  deployed device tree to equal a fresh encode of that ``.xci``.
- ``RFDC_XCI ??= ""``: a weak empty default, so the application's ``local.conf`` value takes
  precedence and an unset value reaches the stop described above.

Other boards
------------

The other RFSoC boards in this repository keep ``param-list = [ ];`` in their ``system-user.dtsi``
and set none of these variables, so they are unaffected: the shared recipe's default policy is
``warn``, and their deploy check prints a warning naming the board while the build continues.
Boards with no converter node, such as the Kria KV260 and the ZCU102, get no check output at all.
