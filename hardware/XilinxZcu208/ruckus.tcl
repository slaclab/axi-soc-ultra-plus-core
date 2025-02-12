# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for version 2023.1 of Vivado (or later)
if { [VersionCheck 2023.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "XCZU48DR-FSVG1517-2-E" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU48DR-FSVG1517-2-E in the Makefile\n\n"; exit -1
}

# Set the board part
set_property board_part xilinx.com:zcu208:part0:2.0 [current_project]

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Load the block design
loadBlockDesign -path "$::DIR_PATH/bd/2023.1/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/2023.1/AxiSocUltraPlusCpuCore.tcl"

# Load IP cores
loadIpCore -dir "$::DIR_PATH/ip"
