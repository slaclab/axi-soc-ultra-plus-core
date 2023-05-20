# Load RUCKUS environment and library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for valid FPGA
if { $::env(PRJ_PART) != "XCZU49DR-FFVF1760-2-E" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU49DR-FFVF1760-2-E in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"

# Load the common source code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Load the common source code used by both ZCU208 and ZCU216
loadSource -lib axi_soc_ultra_plus_core -path "$::DIR_PATH/../XilinxZcu208/rtl/Hardware.vhd"

# Set the board part
set_property board_part xilinx.com:zcu216:part0:2.0 [current_project]

# Load the block design
if  { $::env(VIVADO_VERSION) >= 2023.1 } {
   set bdVer "2023.1"
} elseif  { $::env(VIVADO_VERSION) >= 2022.1 } {
   set bdVer "2022.1"
} else {
   set bdVer "2021.2"
}
loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.tcl"
