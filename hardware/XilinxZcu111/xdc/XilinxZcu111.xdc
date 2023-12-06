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
set_property -dict { PACKAGE_PIN AU3 IOSTANDARD LVCMOS18 } [get_ports { adcio[12] }]
set_property -dict { PACKAGE_PIN AU4 IOSTANDARD LVCMOS18 } [get_ports { adcio[13] }]
set_property -dict { PACKAGE_PIN AV5 IOSTANDARD LVCMOS18 } [get_ports { adcio[14] }]
set_property -dict { PACKAGE_PIN AV6 IOSTANDARD LVCMOS18 } [get_ports { adcio[15] }]
set_property -dict { PACKAGE_PIN AU1 IOSTANDARD LVCMOS18 } [get_ports { adcio[16] }]
set_property -dict { PACKAGE_PIN AU2 IOSTANDARD LVCMOS18 } [get_ports { adcio[17] }]
set_property -dict { PACKAGE_PIN AV2 IOSTANDARD LVCMOS18 } [get_ports { adcio[18] }]
set_property -dict { PACKAGE_PIN AV3 IOSTANDARD LVCMOS18 } [get_ports { adcio[19] }]

set_property -dict { PACKAGE_PIN A9  IOSTANDARD LVCMOS18 } [get_ports { dacio[0] }]
set_property -dict { PACKAGE_PIN A10 IOSTANDARD LVCMOS18 } [get_ports { dacio[1] }]
set_property -dict { PACKAGE_PIN A6  IOSTANDARD LVCMOS18 } [get_ports { dacio[2] }]
set_property -dict { PACKAGE_PIN A7  IOSTANDARD LVCMOS18 } [get_ports { dacio[3] }]
set_property -dict { PACKAGE_PIN A5  IOSTANDARD LVCMOS18 } [get_ports { dacio[4] }]
set_property -dict { PACKAGE_PIN B5  IOSTANDARD LVCMOS18 } [get_ports { dacio[5] }]
set_property -dict { PACKAGE_PIN C5  IOSTANDARD LVCMOS18 } [get_ports { dacio[6] }]
set_property -dict { PACKAGE_PIN C6  IOSTANDARD LVCMOS18 } [get_ports { dacio[7] }]
set_property -dict { PACKAGE_PIN B9  IOSTANDARD LVCMOS18 } [get_ports { dacio[8] }]
set_property -dict { PACKAGE_PIN B10 IOSTANDARD LVCMOS18 } [get_ports { dacio[9] }]
set_property -dict { PACKAGE_PIN B7  IOSTANDARD LVCMOS18 } [get_ports { dacio[10] }]
set_property -dict { PACKAGE_PIN B8  IOSTANDARD LVCMOS18 } [get_ports { dacio[11] }]
set_property -dict { PACKAGE_PIN D8  IOSTANDARD LVCMOS18 } [get_ports { dacio[12] }]
set_property -dict { PACKAGE_PIN D9  IOSTANDARD LVCMOS18 } [get_ports { dacio[13] }]
set_property -dict { PACKAGE_PIN C7  IOSTANDARD LVCMOS18 } [get_ports { dacio[14] }]
set_property -dict { PACKAGE_PIN C8  IOSTANDARD LVCMOS18 } [get_ports { dacio[15] }]
set_property -dict { PACKAGE_PIN C10 IOSTANDARD LVCMOS18 } [get_ports { dacio[16] }]
set_property -dict { PACKAGE_PIN D10 IOSTANDARD LVCMOS18 } [get_ports { dacio[17] }]
set_property -dict { PACKAGE_PIN D6  IOSTANDARD LVCMOS18 } [get_ports { dacio[18] }]
set_property -dict { PACKAGE_PIN E7  IOSTANDARD LVCMOS18 } [get_ports { dacio[19] }]

##############################################################################

set_property -dict { PACKAGE_PIN C17 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][0] }]
set_property -dict { PACKAGE_PIN M18 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][1] }]
set_property -dict { PACKAGE_PIN H16 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][2] }]
set_property -dict { PACKAGE_PIN H17 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][3] }]
set_property -dict { PACKAGE_PIN J16 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][4] }]
set_property -dict { PACKAGE_PIN K16 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][5] }]
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][6] }]
set_property -dict { PACKAGE_PIN J15 IOSTANDARD LVCMOS12 } [get_ports { pmod[0][7] }]

set_property -dict { PACKAGE_PIN L14 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][0] }]
set_property -dict { PACKAGE_PIN L15 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][1] }]
set_property -dict { PACKAGE_PIN M13 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][2] }]
set_property -dict { PACKAGE_PIN N13 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][3] }]
set_property -dict { PACKAGE_PIN M15 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][4] }]
set_property -dict { PACKAGE_PIN N15 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][5] }]
set_property -dict { PACKAGE_PIN M14 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][6] }]
set_property -dict { PACKAGE_PIN N14 IOSTANDARD LVCMOS12 } [get_ports { pmod[1][7] }]

##############################################################################

set_property -dict { PACKAGE_PIN AL16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { plClkP }]
set_property -dict { PACKAGE_PIN AL15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { plClkN }]

set_property -dict { PACKAGE_PIN AK17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { plSysRefP }]
set_property -dict { PACKAGE_PIN AK16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { plSysRefN }]

set_property -dict { PACKAGE_PIN AT16 IOSTANDARD LVCMOS18 } [get_ports { i2cScl[0] }]
set_property -dict { PACKAGE_PIN AW16 IOSTANDARD LVCMOS18 } [get_ports { i2cSda[0] }]

set_property -dict { PACKAGE_PIN AV16 IOSTANDARD LVCMOS18 } [get_ports { i2cScl[1] }]
set_property -dict { PACKAGE_PIN AV13 IOSTANDARD LVCMOS18 } [get_ports { i2cSda[1] }]

##############################################################################

set_property PACKAGE_PIN AF5 [get_ports { adcClkP[0] }]
set_property PACKAGE_PIN AF4 [get_ports { adcClkN[0] }]
set_property PACKAGE_PIN AD5 [get_ports { adcClkP[1] }]
set_property PACKAGE_PIN AD4 [get_ports { adcClkN[1] }]
set_property PACKAGE_PIN AB5 [get_ports { adcClkP[2] }]
set_property PACKAGE_PIN AB4 [get_ports { adcClkN[2] }]
set_property PACKAGE_PIN Y5  [get_ports { adcClkP[3] }]
set_property PACKAGE_PIN Y4  [get_ports { adcClkN[3] }]

set_property PACKAGE_PIN R5  [get_ports { dacClkP[0] }]
set_property PACKAGE_PIN R4  [get_ports { dacClkN[0] }]
set_property PACKAGE_PIN N5  [get_ports { dacClkP[1] }]
set_property PACKAGE_PIN N4  [get_ports { dacClkN[1] }]

set_property PACKAGE_PIN U5 [get_ports { sysRefP }]
set_property PACKAGE_PIN U4 [get_ports { sysRefN }]

# ADC0 TILE 224
set_property PACKAGE_PIN AP2 [get_ports { adcP[0] }]
set_property PACKAGE_PIN AP1 [get_ports { adcN[0] }]
set_property PACKAGE_PIN AM2 [get_ports { adcP[1] }]
set_property PACKAGE_PIN AM1 [get_ports { adcN[1] }]

# ADC0 TILE 225
set_property PACKAGE_PIN AK2 [get_ports { adcP[2] }]
set_property PACKAGE_PIN AK1 [get_ports { adcN[2] }]
set_property PACKAGE_PIN AH2 [get_ports { adcP[3] }]
set_property PACKAGE_PIN AH1 [get_ports { adcN[3] }]

# ADC0 TILE 226
set_property PACKAGE_PIN AF2 [get_ports { adcP[4] }]
set_property PACKAGE_PIN AF1 [get_ports { adcN[4] }]
set_property PACKAGE_PIN AD2 [get_ports { adcP[5] }]
set_property PACKAGE_PIN AD1 [get_ports { adcN[5] }]

# ADC0 TILE 227
set_property PACKAGE_PIN AB2 [get_ports { adcP[6] }]
set_property PACKAGE_PIN AB1 [get_ports { adcN[6] }]
set_property PACKAGE_PIN Y2  [get_ports { adcP[7] }]
set_property PACKAGE_PIN Y1  [get_ports { adcN[7] }]

# DAC2 TILE 228
set_property PACKAGE_PIN U2 [get_ports { dacP[0] }]
set_property PACKAGE_PIN U1 [get_ports { dacN[0] }]
set_property PACKAGE_PIN R2 [get_ports { dacP[1] }]
set_property PACKAGE_PIN R1 [get_ports { dacN[1] }]

set_property PACKAGE_PIN N2 [get_ports { dacP[2] }]
set_property PACKAGE_PIN N1 [get_ports { dacN[2] }]
set_property PACKAGE_PIN L2 [get_ports { dacP[3] }]
set_property PACKAGE_PIN L1 [get_ports { dacN[3] }]

# DAC2 TILE 229
set_property PACKAGE_PIN J2 [get_ports { dacP[4] }]
set_property PACKAGE_PIN J1 [get_ports { dacN[4] }]
set_property PACKAGE_PIN G2 [get_ports { dacP[5] }]
set_property PACKAGE_PIN G1 [get_ports { dacN[5] }]

set_property PACKAGE_PIN E2 [get_ports { dacP[6] }]
set_property PACKAGE_PIN E1 [get_ports { dacN[6] }]
set_property PACKAGE_PIN C2 [get_ports { dacP[7] }]
set_property PACKAGE_PIN C1 [get_ports { dacN[7] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
