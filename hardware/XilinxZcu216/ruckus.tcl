# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for valid FPGA
if { $::env(PRJ_PART) != "XCZU49DR-FFVF1760-2-E" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU49DR-FFVF1760-2-E in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"
loadSource -dir "$::DIR_PATH/rtl"

# Set the board part
set_property board_part xilinx.com:zcu216:part0:2.0 [current_project]

# Load the block design
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
