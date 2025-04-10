# Load RUCKUS library
source $::env(RUCKUS_PROC_TCL)

# Load Source Code
loadRuckusTcl "$::DIR_PATH/AppRingBuffer"
loadRuckusTcl "$::DIR_PATH/SigGen"
loadRuckusTcl "$::DIR_PATH/SigToAxiStream"
loadRuckusTcl "$::DIR_PATH/SsrGearbox"
