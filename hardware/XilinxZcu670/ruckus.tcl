# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for version 2023.1 of Vivado (or later)
if { [VersionCheck 2023.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "XCZU67DR-FSVE1156-2-I" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU67DR-FSVE1156-2-I in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Set the board part
set_property BOARD_PART xilinx.com:zcu670:part0:2.0 [current_project]

# Load the block design
if  { $::env(VIVADO_VERSION) >= 2023.1 } {
   set bdVer "2023.1"
}
loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.tcl"
