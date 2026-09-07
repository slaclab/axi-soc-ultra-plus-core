# Load RUCKUS environment and library
source $::env(RUCKUS_PROC_TCL)

# Check for version 2026.1 of Vivado (or later)
if { [VersionCheck 2026.1] < 0 } {exit -1}

# Check for valid FPGA
if { $::env(PRJ_PART) != "XCZU48DR-FFVG1517-2-E" } {
   puts "\n\nERROR: PRJ_PART must be either XCZU48DR-FFVG1517-2-E in the Makefile\n\n"; exit -1
}

# Load shared source code
loadRuckusTcl "$::DIR_PATH/../../shared"
loadConstraints -dir "$::DIR_PATH/xdc"
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# Load the block design
if  { $::env(VIVADO_VERSION) >= 2026.1 } {
   set bdVer "2026.1"
}
loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.bd"
# loadBlockDesign -path "$::DIR_PATH/bd/${bdVer}/AxiSocUltraPlusCpuCore.tcl"

# Load IP cores
loadIpCore -dir "$::DIR_PATH/ip"

# The DDR4 IP records its custom parts database as an absolute path, so re-point it at this
# checkout. Without this the IP silently falls back to a 512MB AXI address space instead of 8GB.
set_property CONFIG.C0.DDR4_CustomParts "$::DIR_PATH/ip/MigCoreCustomParts.csv" [get_ips MigCore]
if { [get_property CONFIG.C0.DDR4_AxiAddressWidth [get_ips MigCore]] != 33 } {
   puts "\n\nERROR: MigCore custom part MTA4ATF1G64HZ-3G2 not applied (expected a 33-bit AXI address space for 8GB)\n\n"; exit -1
}
