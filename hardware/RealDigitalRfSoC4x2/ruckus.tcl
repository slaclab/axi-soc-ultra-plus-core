# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

#########################################################################################
# You can install the board files from the Vivado Tcl prompt with the following commands:
#########################################################################################
# xhub::refresh_catalog [xhub::get_xstores xilinx_board_store]
# xhub::install [xhub::get_xitems xilinx.com:xilinx_board_store:rfsoc4x2:1.0]
#########################################################################################

#########################################################################################
# Usefule Development Notes and URLs:
#########################################################################################
# hhttp://dev.realdigital.org/hardware/rfsoc-4x2
# https://casper-toolflow.readthedocs.io/projects/tutorials/en/latest/tutorials/rfsoc/tut_getting_started.html
# https://github.com/Xilinx/RFSoC-PYNQ/tree/master/boards/RFSoC4x2/petalinux_bsp/meta-user
#########################################################################################

# Check for version 2023.1 of Vivado (or later)
if { [VersionCheck 2023.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "xczu48dr-ffvg1517-1-e" } {
   puts "\n\nERROR: PRJ_PART must be either xczu48dr-ffvg1517-1-e in the Makefile\n\n"; exit -1
}

# Set the board part
set_property board_part realdigital.org:rfsoc4x2:part0:1.0 [current_project]

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Load the common source code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Load the block design
if  { $::env(VIVADO_VERSION) >= 2023.1 } {
   set bdVer "2023.1"
}
loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.tcl"

# Load IP cores
loadIpCore -dir "$::DIR_PATH/ip"
