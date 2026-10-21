Install Rogue
=============

Rogue is the SLAC hardware control framework used to interact with
axi-soc-ultra-plus-core based designs.  Installation instructions are
maintained upstream in the Rogue documentation.

Follow the upstream `Miniforge-based install guide
<https://slaclab.github.io/rogue/installing/miniforge.html>`_.

The Miniforge method is the recommended approach for SLAC network
users and for standalone workstations alike.  After completing the
upstream steps you will have a conda environment containing ``pyrogue``
and all required dependencies.  axi-soc-ultra-plus-core requires Rogue
6.15.0 or later; ``rfsoc_utility`` raises a Rogue version error at import
on an older release.

Once Rogue is installed, see :doc:`rogue_gui_launch` to start the
control GUI for your board.
