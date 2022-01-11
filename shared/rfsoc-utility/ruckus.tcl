# Load RUCKUS library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Load Source Code
loadRuckusTcl "$::DIR_PATH/AppRingBuffer"
loadRuckusTcl "$::DIR_PATH/DacSigGen"
