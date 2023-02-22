# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

#########################################################################################
# You can install the board files from the Vivado Tcl prompt with the following commands:
#########################################################################################
# xhub::refresh_catalog [xhub::get_xstores xilinx_board_store]
# xhub::install [xhub::get_xitems xilinx.com:xilinx_board_store:rfsoc4x2:1.0]
#########################################################################################

# Check for version 2022.1 of Vivado (or later)
if { [VersionCheck 2022.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "xczu48dr-ffvg1517-2-e" } {
   puts "\n\nERROR: PRJ_PART must be either xczu48dr-ffvg1517-2-e in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# # Load the common source code
# loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Set the board part
set_property board_part realdigital.org:rfsoc4x2:part0:1.0 [current_project]

# Load the block design
loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/AxiSocUltraPlusCpuCore.tcl"
