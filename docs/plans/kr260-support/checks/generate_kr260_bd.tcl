##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################
## Generate hardware/XilinxKriaKr260/bd/2026.1/AxiSocUltraPlusCpuCore.{tcl,bd}
## from the KV260 2026.1 design: same cells, ports and addresses; PS settings
## from the KR260 SOM and carrier board presets plus the AXI SoC platform
## settings. Run from the repository root with Vivado 2026.1:
##   vivado -mode batch -source docs/plans/kr260-support/checks/generate_kr260_bd.tcl \
##          -tclargs <scratch directory outside the source tree>
##############################################################################

if {[llength $argv] != 1} {
   error "usage: -tclargs <scratch directory outside the source tree>"
}
set scratch [file normalize [lindex $argv 0]]
set repo    [pwd]
set kvTcl   $repo/hardware/XilinxKriaKv260/bd/2026.1/AxiSocUltraPlusCpuCore.tcl
set outDir  $repo/hardware/XilinxKriaKr260/bd/2026.1
set somPart xilinx.com:kr260_som:part0:1.1
set carVer  1.1

if {[llength [get_board_parts -quiet $somPart]] == 0} {
   puts "Installed KR260 board parts: [get_board_parts -quiet *kr260*]"
   error "$somPart is not installed in this Vivado"
}

create_project -force kr260_bd $scratch -part xck26-sfvc784-2LV-c
set_property board_part $somPart [current_project]
set_property board_connections [list \
   som240_1_connector xilinx.com:kr260_carrier:som240_1_connector:$carVer \
   som240_2_connector xilinx.com:kr260_carrier:som240_2_connector:$carVer] [current_project]

# Replace the KV260 PS property list with the board presets plus overrides
set fh  [open $kvTcl r]
set src [read $fh]
close $fh
set cellLine {set zynq_ultra_ps_e_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.5 zynq_ultra_ps_e_0 ]}
set endTok   {] $zynq_ultra_ps_e_0}
set first [string first $cellLine $src]
set last  [string first $endTok $src $first]
if {$first < 0 || $last < 0} { error "PS cell block not found in $kvTcl" }
set last [expr {$last + [string length $endTok] - 1}]

set psBlock {set zynq_ultra_ps_e_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.5 zynq_ultra_ps_e_0 ]
  apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $zynq_ultra_ps_e_0
  set_property -dict [list \
    CONFIG.PSU__SD0__PERIPHERAL__ENABLE {0} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__SRCSEL {IOPLL} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {250} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__FPGA_PL1_ENABLE {0} \
    CONFIG.PSU__NUM_FABRIC_RESETS {1} \
    CONFIG.PSU__PL_CLK0_BUF {TRUE} \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {1} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__MAXIGP0__DATA_WIDTH {32} \
    CONFIG.PSU__MAXIGP1__DATA_WIDTH {32} \
    CONFIG.PSU__USE__S_AXI_GP0 {0} \
    CONFIG.PSU__USE__S_AXI_GP1 {0} \
    CONFIG.PSU__USE__S_AXI_GP2 {1} \
    CONFIG.PSU__USE__S_AXI_GP3 {0} \
    CONFIG.PSU__USE__S_AXI_GP4 {0} \
    CONFIG.PSU__USE__S_AXI_GP5 {0} \
    CONFIG.PSU__USE__S_AXI_GP6 {0} \
    CONFIG.PSU__USE__S_AXI_ACE {0} \
    CONFIG.PSU__USE__S_AXI_ACP {0} \
    CONFIG.PSU__SAXIGP2__DATA_WIDTH {128} \
    CONFIG.PSU__USE__IRQ0 {1} \
    CONFIG.PSU__TTC0__WAVEOUT__ENABLE {1} \
    CONFIG.PSU__TTC0__WAVEOUT__IO {EMIO} \
    CONFIG.PSU__PMU__PLERROR__ENABLE {1} \
    CONFIG.PSU__DDR_HIGH_ADDRESS_GUI_ENABLE {1} \
  ] $zynq_ultra_ps_e_0}

set src [string replace $src $first $last $psBlock]
set src [string map {kv260_som:part0:1.3 kr260_som:part0:1.1} $src]
set derived $scratch/kr260_from_kv260.tcl
set fh [open $derived w]
puts -nonewline $fh $src
close $fh

# Builds, validates and saves the design (create_root_design in the script)
source $derived

file mkdir $outDir
write_bd_tcl -force $outDir/AxiSocUltraPlusCpuCore.tcl
file copy -force [get_files AxiSocUltraPlusCpuCore.bd] $outDir/AxiSocUltraPlusCpuCore.bd
close_project
puts "Wrote $outDir/AxiSocUltraPlusCpuCore.{tcl,bd}"
