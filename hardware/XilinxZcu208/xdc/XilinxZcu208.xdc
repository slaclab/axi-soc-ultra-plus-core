##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################

set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]

set_property -dict { PACKAGE_PIN W17 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN Y16 IOSTANDARD ANALOG } [get_ports { vNIn }]

##############################################################################

set_property -dict { PACKAGE_PIN AP5 IOSTANDARD LVCMOS18 } [get_ports { adcio[0] }]
set_property -dict { PACKAGE_PIN AP6 IOSTANDARD LVCMOS18 } [get_ports { adcio[1] }]
set_property -dict { PACKAGE_PIN AR6 IOSTANDARD LVCMOS18 } [get_ports { adcio[2] }]
set_property -dict { PACKAGE_PIN AR7 IOSTANDARD LVCMOS18 } [get_ports { adcio[3] }]
set_property -dict { PACKAGE_PIN AV7 IOSTANDARD LVCMOS18 } [get_ports { adcio[4] }]
set_property -dict { PACKAGE_PIN AU7 IOSTANDARD LVCMOS18 } [get_ports { adcio[5] }]
set_property -dict { PACKAGE_PIN AV8 IOSTANDARD LVCMOS18 } [get_ports { adcio[6] }]
set_property -dict { PACKAGE_PIN AU8 IOSTANDARD LVCMOS18 } [get_ports { adcio[7] }]
set_property -dict { PACKAGE_PIN AT6 IOSTANDARD LVCMOS18 } [get_ports { adcio[8] }]
set_property -dict { PACKAGE_PIN AT7 IOSTANDARD LVCMOS18 } [get_ports { adcio[9] }]
set_property -dict { PACKAGE_PIN AU5 IOSTANDARD LVCMOS18 } [get_ports { adcio[10] }]
set_property -dict { PACKAGE_PIN AT5 IOSTANDARD LVCMOS18 } [get_ports { adcio[11] }]
set_property -dict { PACKAGE_PIN AW3 IOSTANDARD LVCMOS18 } [get_ports { adcio[12] }]
set_property -dict { PACKAGE_PIN AW4 IOSTANDARD LVCMOS18 } [get_ports { adcio[13] }]
set_property -dict { PACKAGE_PIN AV2 IOSTANDARD LVCMOS18 } [get_ports { adcio[14] }]
set_property -dict { PACKAGE_PIN AV3 IOSTANDARD LVCMOS18 } [get_ports { adcio[15] }]

set_property -dict { PACKAGE_PIN A9  IOSTANDARD LVCMOS18 } [get_ports { dacio[0] }]
set_property -dict { PACKAGE_PIN A10 IOSTANDARD LVCMOS18 } [get_ports { dacio[1] }]
set_property -dict { PACKAGE_PIN A6  IOSTANDARD LVCMOS18 } [get_ports { dacio[2] }]
set_property -dict { PACKAGE_PIN A7  IOSTANDARD LVCMOS18 } [get_ports { dacio[3] }]
set_property -dict { PACKAGE_PIN A5  IOSTANDARD LVCMOS18 } [get_ports { dacio[4] }]
set_property -dict { PACKAGE_PIN B5  IOSTANDARD LVCMOS18 } [get_ports { dacio[5] }]
set_property -dict { PACKAGE_PIN C5  IOSTANDARD LVCMOS18 } [get_ports { dacio[6] }]
set_property -dict { PACKAGE_PIN C6  IOSTANDARD LVCMOS18 } [get_ports { dacio[7] }]
set_property -dict { PACKAGE_PIN C10 IOSTANDARD LVCMOS18 } [get_ports { dacio[8] }]
set_property -dict { PACKAGE_PIN D10 IOSTANDARD LVCMOS18 } [get_ports { dacio[9] }]
set_property -dict { PACKAGE_PIN D6  IOSTANDARD LVCMOS18 } [get_ports { dacio[10] }]
set_property -dict { PACKAGE_PIN E7  IOSTANDARD LVCMOS18 } [get_ports { dacio[11] }]
set_property -dict { PACKAGE_PIN E8  IOSTANDARD LVCMOS18 } [get_ports { dacio[12] }]
set_property -dict { PACKAGE_PIN E9  IOSTANDARD LVCMOS18 } [get_ports { dacio[13] }]
set_property -dict { PACKAGE_PIN E6  IOSTANDARD LVCMOS18 } [get_ports { dacio[14] }]
set_property -dict { PACKAGE_PIN F6  IOSTANDARD LVCMOS18 } [get_ports { dacio[15] }]

##############################################################################

set_property -dict { PACKAGE_PIN B8 IOSTANDARD SUB_LVDS } [get_ports { plClkP }]
set_property -dict { PACKAGE_PIN B7 IOSTANDARD SUB_LVDS } [get_ports { plClkN }]

set_property -dict { PACKAGE_PIN B10 IOSTANDARD SUB_LVDS } [get_ports { plSysRefP }]
set_property -dict { PACKAGE_PIN B9  IOSTANDARD SUB_LVDS } [get_ports { plSysRefN }]

set_property -dict { PACKAGE_PIN AU2 IOSTANDARD LVCMOS18 } [get_ports { lmkSync   }]

set_property -dict { PACKAGE_PIN C11 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[0] }]
set_property -dict { PACKAGE_PIN B12 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[1] }]

set_property -dict { PACKAGE_PIN G10 IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Scl }]
set_property -dict { PACKAGE_PIN K12 IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Sda }]

set_property -dict { PACKAGE_PIN M20 IOSTANDARD LVDS } [get_ports { sfpRecClkP }]
set_property -dict { PACKAGE_PIN L21 IOSTANDARD LVDS } [get_ports { sfpRecClkN }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]]
set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]
set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -include_generated_clocks {plClkP}]

##############################################################################
