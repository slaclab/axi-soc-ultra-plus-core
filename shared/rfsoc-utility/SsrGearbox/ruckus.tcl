# Load RUCKUS library
source $::env(RUCKUS_PROC_TCL)

# Load Source Code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"
