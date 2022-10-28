# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for version 2022.1 of Vivado (or later)
if { [VersionCheck 2022.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "xck26-sfvc784-2lv-c" } {
   puts "\n\nERROR: PRJ_PART must be either xck26-sfvc784-2lv-c in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# # Load the common source code
# loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Set the board part
set_property board_part xilinx.com:kv260_som:part0:1.3 [current_project]

# Load the block design
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
