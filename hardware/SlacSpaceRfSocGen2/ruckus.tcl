# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for valid FPGA 
if { $::env(PRJ_PART) != "xczu48dr-ffvg1517-1-e" && $::env(PRJ_PART) != "xqzu48dr-fsrg1517-1M-m" } {
   puts "\n\nERROR: PRJ_PART must be either xczu48dr-ffvg1517-1-e or xqzu48dr-fsrg1517-1M-m in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Load the block design
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
