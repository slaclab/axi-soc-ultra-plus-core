# Load RUCKUS library
source $::env(RUCKUS_PROC_TCL)

# Check for submodule tagging
if { [info exists ::env(OVERRIDE_SUBMODULE_LOCKS)] != 1 || $::env(OVERRIDE_SUBMODULE_LOCKS) == 0 } {
   if { [SubmoduleCheck {aes-stream-drivers}  {6.7.0} ] < 0 } {exit -1}
   if { [SubmoduleCheck {ruckus}             {4.17.9} ] < 0 } {exit -1}
   if { [SubmoduleCheck {surf}               {2.60.0} ] < 0 } {exit -1}
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

# Directory of axi-soc-ultra-plus-core (captured now: $::DIR_PATH is restored by loadRuckusTcl before any later call)
set ::axiSocUltraPlusDir [file normalize "$::DIR_PATH/.."]

## Generate PYRFDC_CONFIG.mem next to the application's RFDC IP core and add it to the project.
## Call right after the application's loadIpCore of the RFDC .xci (RFSoC parts only).
## This is a no-op on non-RFSoC architectures.
proc AddPyRfdcMem { } {
   # Only the zynquplusRFSOC architecture carries the RFDC
   if { [getFpgaArch] ne {zynquplusRFSOC} } {
      return
   }
   set ipList [get_ips -quiet -filter {IPDEF =~ "xilinx.com:ip:usp_rf_data_converter:*"}]
   if { [llength ${ipList}] != 1 } {
      puts "\n\n*********************************************************"
      puts "AddPyRfdcMem: exactly one usp_rf_data_converter IP core is required, found [llength ${ipList}]: ${ipList}"
      puts "*********************************************************\n\n"
      exit -1
   }
   set ip       [lindex ${ipList} 0]
   set xci_file [get_property IP_FILE ${ip}]
   set xci_dir  [file dirname ${xci_file}]
   set mem_file [file join ${xci_dir} PYRFDC_CONFIG.mem]
   set rc [catch {exec python [file join $::axiSocUltraPlusDir scripts pyrfdc_mem.py] --xci ${xci_file} --out ${mem_file}.tmp 2>@1} msg opts]
   if { ${rc} && [string match {CHILDSTATUS *} [dict get ${opts} -errorcode]] } {
      puts "\n\n*********************************************************"
      puts "AddPyRfdcMem: pyrfdc_mem.py failed for ${xci_file}\n${msg}"
      puts "*********************************************************\n\n"
      exit -1
   }
   set fh [open ${mem_file}.tmp r]
   set newData [read ${fh}]
   close ${fh}
   set oldData ""
   if { [file exists ${mem_file}] } {
      set fh [open ${mem_file} r]
      set oldData [read ${fh}]
      close ${fh}
   }
   set isNew [expr { [get_files -quiet ${mem_file}] eq "" }]
   if { ${newData} ne ${oldData} } {
      file rename -force ${mem_file}.tmp ${mem_file}
      set changed 1
      puts "AddPyRfdcMem: wrote ${mem_file}"
   } else {
      file delete ${mem_file}.tmp
      set changed 0
      puts "AddPyRfdcMem: ${mem_file} unchanged"
   }
   if { ${isNew} } {
      add_files -norecurse ${mem_file}
   }
   if { ${changed} || ${isNew} } {
      reset_run synth_1
      puts "AddPyRfdcMem: reset_run synth_1"
   }
}

# A missing XPM init file (for example an RFSoC application that forgot AddPyRfdcMem) must fail synthesis
set_msg_config -id {Synth 8-4445} -new_severity ERROR
