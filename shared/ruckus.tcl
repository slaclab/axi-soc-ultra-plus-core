# Load RUCKUS library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Check for submodule tagging
if { [info exists ::env(OVERRIDE_SUBMODULE_LOCKS)] != 1 || $::env(OVERRIDE_SUBMODULE_LOCKS) == 0 } {
   if { [SubmoduleCheck {aes-stream-drivers}  {6.2.0} ] < 0 } {exit -1}
   if { [SubmoduleCheck {ruckus}             {4.14.0} ] < 0 } {exit -1}
   if { [SubmoduleCheck {surf}               {2.48.0} ] < 0 } {exit -1}
} else {
   puts "\n\n*********************************************************"
   puts "OVERRIDE_SUBMODULE_LOCKS != 0"
   puts "Ignoring the submodule locks in axi-soc-ultra-plus-core/ruckus.tcl"
   puts "*********************************************************\n\n"
}

# Check for .bit file copy to target's image dir
if { $::env(GEN_BIT_IMAGE) == 0 } {
   puts "\n\n*********************************************************"
   puts "GEN_BIT_IMAGE env var must be defined as 1 in Makefile"
   puts "*********************************************************\n\n"
   exit -1
}

# Check for .xsa file copy to target's image dir
if { $::env(GEN_XSA_IMAGE) == 0 } {
   puts "\n\n*********************************************************"
   puts "GEN_XSA_IMAGE env var must be defined as 1 in Makefile"
   puts "*********************************************************\n\n"
   exit -1
}

# Check for version 2021.2 of Vivado (or later)
if { [VersionCheck 2021.2] < 0 } {exit -1}

# Load Source Code
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/rtl"

# loadIpCore -dir "$::DIR_PATH/ip/SysMon"
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/ip/SysMon"

# loadIpCore -dir "$::DIR_PATH/ip/AxiPcie16BCrossbarIpCore"
loadSource -lib axi_soc_ultra_plus_core -dir "$::DIR_PATH/ip/AxiPcie16BCrossbarIpCore"

# Load External FW utilities
loadRuckusTcl "$::DIR_PATH/rfsoc-utility"
