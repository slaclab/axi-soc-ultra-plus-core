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

set_property -dict { PACKAGE_PIN T15 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN U14 IOSTANDARD ANALOG } [get_ports { vNIn }]

##############################################################################

set_property -dict { PACKAGE_PIN A13  IOSTANDARD LVCMOS18 } [get_ports { adcio[0] }]
set_property -dict { PACKAGE_PIN A14  IOSTANDARD LVCMOS18 } [get_ports { adcio[1] }]
set_property -dict { PACKAGE_PIN B13  IOSTANDARD LVCMOS18 } [get_ports { adcio[2] }]
set_property -dict { PACKAGE_PIN C14  IOSTANDARD LVCMOS18 } [get_ports { adcio[3] }]
set_property -dict { PACKAGE_PIN C13  IOSTANDARD LVCMOS18 } [get_ports { adcio[4] }]
set_property -dict { PACKAGE_PIN D13  IOSTANDARD LVCMOS18 } [get_ports { adcio[5] }]
set_property -dict { PACKAGE_PIN F13  IOSTANDARD LVCMOS18 } [get_ports { adcio[6] }]
set_property -dict { PACKAGE_PIN F14  IOSTANDARD LVCMOS18 } [get_ports { adcio[7] }]
set_property -dict { PACKAGE_PIN AN9  IOSTANDARD LVCMOS12 } [get_ports { adcio[8] }]
set_property -dict { PACKAGE_PIN AN7  IOSTANDARD LVCMOS12 } [get_ports { adcio[9] }]
set_property -dict { PACKAGE_PIN AN8  IOSTANDARD LVCMOS12 } [get_ports { adcio[10] }]
set_property -dict { PACKAGE_PIN AP12 IOSTANDARD LVCMOS12 } [get_ports { adcio[11] }]
set_property -dict { PACKAGE_PIN AP13 IOSTANDARD LVCMOS12 } [get_ports { adcio[12] }]
set_property -dict { PACKAGE_PIN AN1  IOSTANDARD LVCMOS12 } [get_ports { adcio[13] }]
set_property -dict { PACKAGE_PIN AN2  IOSTANDARD LVCMOS12 } [get_ports { adcio[14] }]
set_property -dict { PACKAGE_PIN AP2  IOSTANDARD LVCMOS12 } [get_ports { adcio[15] }]

set_property -dict { PACKAGE_PIN G13 IOSTANDARD LVCMOS18 } [get_ports { dacio[0] }]
set_property -dict { PACKAGE_PIN H13 IOSTANDARD LVCMOS18 } [get_ports { dacio[1] }]
set_property -dict { PACKAGE_PIN J13 IOSTANDARD LVCMOS18 } [get_ports { dacio[2] }]
set_property -dict { PACKAGE_PIN J14 IOSTANDARD LVCMOS18 } [get_ports { dacio[3] }]
set_property -dict { PACKAGE_PIN H14 IOSTANDARD LVCMOS18 } [get_ports { dacio[4] }]
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS18 } [get_ports { dacio[5] }]
set_property -dict { PACKAGE_PIN K14 IOSTANDARD LVCMOS18 } [get_ports { dacio[6] }]
set_property -dict { PACKAGE_PIN K15 IOSTANDARD LVCMOS18 } [get_ports { dacio[7] }]
set_property -dict { PACKAGE_PIN AP3 IOSTANDARD LVCMOS12 } [get_ports { dacio[8] }]
set_property -dict { PACKAGE_PIN AN4 IOSTANDARD LVCMOS12 } [get_ports { dacio[9] }]
set_property -dict { PACKAGE_PIN AN5 IOSTANDARD LVCMOS12 } [get_ports { dacio[10] }]
set_property -dict { PACKAGE_PIN AM5 IOSTANDARD LVCMOS12 } [get_ports { dacio[11] }]
set_property -dict { PACKAGE_PIN AP5 IOSTANDARD LVCMOS12 } [get_ports { dacio[12] }]
set_property -dict { PACKAGE_PIN AP6 IOSTANDARD LVCMOS12 } [get_ports { dacio[13] }]
set_property -dict { PACKAGE_PIN AP7 IOSTANDARD LVCMOS12 } [get_ports { dacio[14] }]
set_property -dict { PACKAGE_PIN AP8 IOSTANDARD LVCMOS12 } [get_ports { dacio[15] }]

##############################################################################

set_property -dict { PACKAGE_PIN AP11 IOSTANDARD DIFF_SSTL12 } [get_ports { plClkP[0] }]
set_property -dict { PACKAGE_PIN AP10 IOSTANDARD DIFF_SSTL12 } [get_ports { plClkN[0] }]

set_property -dict { PACKAGE_PIN G11 IOSTANDARD SUB_LVDS } [get_ports { plClkP[1] }]
set_property -dict { PACKAGE_PIN G10 IOSTANDARD SUB_LVDS } [get_ports { plClkN[1] }]

set_property -dict { PACKAGE_PIN E10 IOSTANDARD SUB_LVDS } [get_ports { plSysRefP }]
set_property -dict { PACKAGE_PIN E9  IOSTANDARD SUB_LVDS } [get_ports { plSysRefN }]

set_property -dict { PACKAGE_PIN K10 IOSTANDARD LVCMOS18 } [get_ports { sysRefSel }]; # 0=Si5381A, 1=CLK104

set_property -dict { PACKAGE_PIN F12 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[0] }]
set_property -dict { PACKAGE_PIN G12 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[1] }]

##############################################################################

# set_property PACKAGE_PIN AD5 [get_ports { adcClkP[0] }]; # TILE 225 (Si5381A.OUT3)
# set_property PACKAGE_PIN AD4 [get_ports { adcClkN[0] }]; # TILE 225 (Si5381A.OUT3B)

# set_property PACKAGE_PIN AB5 [get_ports { adcClkP[1] }]; # TILE 226 (J8  - SSMP Connector)
# set_property PACKAGE_PIN AB4 [get_ports { adcClkN[1] }]; # TILE 226 (J98 - SSMP Connector)

# set_property PACKAGE_PIN L5 [get_ports { dacClkP[0] }]; # TILE 227 (Si5381A.OUT0)
# set_property PACKAGE_PIN L4 [get_ports { dacClkN[0] }]; # TILE 227 (Si5381A.OUT0B)

# set_property PACKAGE_PIN J5 [get_ports { dacClkP[1] }]; # TILE 228 (J99  - SSMP Connector)
# set_property PACKAGE_PIN J4 [get_ports { dacClkN[1] }]; # TILE 228 (J100 - SSMP Connector)

# set_property PACKAGE_PIN N5 [get_ports { sysRefP }]
# set_property PACKAGE_PIN N4 [get_ports { sysRefN }]

# # ADC TILE 224
# set_property PACKAGE_PIN AK2 [get_ports { adcP[0] }]
# set_property PACKAGE_PIN AK1 [get_ports { adcN[0] }]
# set_property PACKAGE_PIN AH2 [get_ports { adcP[1] }]
# set_property PACKAGE_PIN AH1 [get_ports { adcN[1] }]

# set_property PACKAGE_PIN AF2 [get_ports { adcP[2] }]
# set_property PACKAGE_PIN AF1 [get_ports { adcN[2] }]
# set_property PACKAGE_PIN AD2 [get_ports { adcP[3] }]
# set_property PACKAGE_PIN AD1 [get_ports { adcN[3] }]

# # ADC TILE 225
# set_property PACKAGE_PIN AB2 [get_ports { adcP[4] }]
# set_property PACKAGE_PIN AB1 [get_ports { adcN[4] }]
# set_property PACKAGE_PIN Y2  [get_ports { adcP[5] }]
# set_property PACKAGE_PIN Y1  [get_ports { adcN[5] }]

# set_property PACKAGE_PIN V2 [get_ports { adcP[6] }]
# set_property PACKAGE_PIN V1 [get_ports { adcN[6] }]
# set_property PACKAGE_PIN T2 [get_ports { adcP[7] }]
# set_property PACKAGE_PIN T1 [get_ports { adcN[7] }]

# # ADC TILE 226
# set_property PACKAGE_PIN Y4 [get_ports { adcP[8] }]
# set_property PACKAGE_PIN Y5 [get_ports { adcN[8] }]
# set_property PACKAGE_PIN V4 [get_ports { adcP[9] }]
# set_property PACKAGE_PIN V5 [get_ports { adcN[9] }]

# # DAC TILE 227
# set_property PACKAGE_PIN N2 [get_ports { dacP[0] }]
# set_property PACKAGE_PIN N1 [get_ports { dacN[0] }]
# set_property PACKAGE_PIN L2 [get_ports { dacP[1] }]
# set_property PACKAGE_PIN L1 [get_ports { dacN[1] }]

# set_property PACKAGE_PIN J2 [get_ports { dacP[2] }]
# set_property PACKAGE_PIN J1 [get_ports { dacN[2] }]
# set_property PACKAGE_PIN G2 [get_ports { dacP[3] }]
# set_property PACKAGE_PIN G1 [get_ports { dacN[3] }]

# # DAC TILE 228
# set_property PACKAGE_PIN E2 [get_ports { dacP[4] }]
# set_property PACKAGE_PIN E1 [get_ports { dacN[4] }]
# set_property PACKAGE_PIN C2 [get_ports { dacP[5] }]
# set_property PACKAGE_PIN C1 [get_ports { dacN[5] }]

# set_property PACKAGE_PIN B4 [get_ports { dacP[6] }]
# set_property PACKAGE_PIN A4 [get_ports { dacN[6] }]
# set_property PACKAGE_PIN B6 [get_ports { dacP[7] }]
# set_property PACKAGE_PIN A6 [get_ports { dacN[7] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
