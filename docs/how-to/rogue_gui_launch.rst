Launch the Rogue GUI
====================

Start the PyDM-based Rogue control GUI to interact with a running
axi-soc-ultra-plus-core board over TCP.

Prerequisites
-------------

- Rogue installed in a conda environment (see **Install Rogue** in this
  how-to section).
- The board is powered on and reachable at its IP address (default
  ``10.0.0.10``).
- The application repository cloned locally (e.g.
  ``Simple-rfsoc-4x2-Example``).

Steps
-----

1. Activate the Rogue environment.  On the SLAC AFS network use the
   provided setup script:

   .. code-block:: bash

      source Simple-rfsoc-4x2-Example/software/setup_env_slac.sh

   On a standalone machine, activate your conda Rogue environment
   directly (see :doc:`rogue_install`).

2. Launch the GUI, passing the board IP address:

   .. code-block:: bash

      cd Simple-rfsoc-4x2-Example/software
      python scripts/devGui.py --ip 10.0.0.10

   Replace ``10.0.0.10`` with the actual DHCP-assigned address of your
   board if it differs.

What the GUI provides
---------------------

``devGui.py`` constructs a :repo:`firmware/python/simple_rfsoc_4x2_example/_Root.py`
instance that:

- Opens a TCP memory-map connection to the board (default port 9000).
- Opens TCP stream connections for ADC/DAC ring-buffer data.
- Starts a ZMQ server (default port 9099) so that ``launch_gui.py``
  can reconnect to an existing session.
- Launches the PyDM GUI via ``pyrogue.pydm.runPyDM()``.

Troubleshooting
---------------

- **Connection refused:** verify the board is booted and the IP is
  correct.  Run ``ping 10.0.0.10`` before launching.
- **Rogue version error at startup:** the ``_Root.py`` enforces a
  minimum Rogue version.  Update your conda environment to meet the
  requirement shown in the error message.
- **Existing ZMQ server:** if a ``Root`` is already running on the
  same host, use ``scripts/launch_gui.py --server 10.0.0.10`` to
  attach to it instead of constructing a new one.
