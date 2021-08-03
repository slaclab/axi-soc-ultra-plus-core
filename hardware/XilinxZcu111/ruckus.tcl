# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for valid FPGA 
if { $::env(PRJ_PART) != "XCZU28DR-FFVG1517-2-E" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU28DR-FFVG1517-2-E in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Set the board part
set_property board_part xilinx.com:zcu111:part0:1.4 [current_project]

# Load the block design
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
