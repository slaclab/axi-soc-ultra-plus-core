Launch the Rogue GUI
====================

Start the PyDM-based Rogue control GUI to interact with a running
axi-soc-ultra-plus-core board over TCP.

Prerequisites
-------------

- Rogue installed in a conda environment (see :doc:`rogue_install`).
- The board is powered on and reachable at its IP address
  (``<board-ip>`` in the examples below).
- The application repository cloned locally (e.g.
  ``Simple-rfsoc-4x2-Example``).

Steps
-----

1. Activate the Rogue environment.  On the SLAC network use the
   provided setup script:

   .. code-block:: bash

      source Simple-rfsoc-4x2-Example/software/setup_env_slac.sh

   On a standalone machine, activate your conda Rogue environment
   directly (see :doc:`rogue_install`).

2. Launch the GUI, passing the board IP address:

   .. code-block:: bash

      cd Simple-rfsoc-4x2-Example/software
      python scripts/devGui.py --ip <board-ip>

   Replace ``<board-ip>`` with the DHCP-assigned (or static) address of
   your board.

What the GUI provides
---------------------

``devGui.py`` constructs the application ``Root`` defined in
`firmware/python/simple_rfsoc_4x2_example/_Root.py <https://github.com/slaclab/Simple-rfsoc-4x2-Example/blob/main/firmware/python/simple_rfsoc_4x2_example/_Root.py>`__
of the Simple-rfsoc-4x2-Example repository, which:

- Opens a TCP memory-map connection to the board (default port 9000).
- Opens TCP stream connections for ADC/DAC ring-buffer data.
- Starts a ZMQ server (default port 9099, bound to ``127.0.0.1``) so
  that ``launch_gui.py`` can reconnect to an existing session.

``devGui.py`` then launches the PyDM GUI via ``pyrogue.pydm.runPyDM()``
(the default ``--guiType PyDM``).

Troubleshooting
---------------

- **Connection refused:** verify the board is booted and the IP is
  correct.  Run ``ping <board-ip>`` before launching.
- **Rogue version error at startup:** ``rfsoc_utility`` requires Rogue
  6.15.0 or later; importing it with an older Rogue raises
  ``rogue.GeneralError`` with
  ``Installed rogue is less than minimum version``.  Update your conda
  environment to Rogue 6.15.0 or later.
- **Existing ZMQ server:** if a ``Root`` is already running on the
  same host, use ``scripts/launch_gui.py --serverList localhost:9099``
  (the default) to attach to it instead of constructing a new one.
