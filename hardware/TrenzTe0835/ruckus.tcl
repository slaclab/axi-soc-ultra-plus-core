# Load RUCKUS environment and library
source $::env(RUCKUS_PROC_TCL)

# Check for version 2023.1 of Vivado (or later)
if { [VersionCheck 2023.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "xczu47dr-ffve1156-1-e" } {
   puts "\n\nERROR: PRJ_PART must be either xczu47dr-ffve1156-1-e in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Load the common source code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# # Set the board part
# set_property BOARD_PART trenz.biz:te0835_47dr_1e:part0:1.0 [current_project]

# Load the block design
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
