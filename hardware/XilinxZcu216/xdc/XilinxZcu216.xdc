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

set_property -dict { PACKAGE_PIN AA19 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN AB18 IOSTANDARD ANALOG } [get_ports { vNIn }]

##############################################################################

set_property -dict { PACKAGE_PIN AP10 IOSTANDARD LVCMOS18 } [get_ports { adcio[0] }]
set_property -dict { PACKAGE_PIN AP11 IOSTANDARD LVCMOS18 } [get_ports { adcio[1] }]
set_property -dict { PACKAGE_PIN AR11 IOSTANDARD LVCMOS18 } [get_ports { adcio[2] }]
set_property -dict { PACKAGE_PIN AP12 IOSTANDARD LVCMOS18 } [get_ports { adcio[3] }]
set_property -dict { PACKAGE_PIN AT10 IOSTANDARD LVCMOS18 } [get_ports { adcio[4] }]
set_property -dict { PACKAGE_PIN AR10 IOSTANDARD LVCMOS18 } [get_ports { adcio[5] }]
set_property -dict { PACKAGE_PIN AT12 IOSTANDARD LVCMOS18 } [get_ports { adcio[6] }]
set_property -dict { PACKAGE_PIN AR12 IOSTANDARD LVCMOS18 } [get_ports { adcio[7] }]
set_property -dict { PACKAGE_PIN AU11 IOSTANDARD LVCMOS18 } [get_ports { adcio[8] }]
set_property -dict { PACKAGE_PIN AU12 IOSTANDARD LVCMOS18 } [get_ports { adcio[9] }]
set_property -dict { PACKAGE_PIN AV10 IOSTANDARD LVCMOS18 } [get_ports { adcio[10] }]
set_property -dict { PACKAGE_PIN AU10 IOSTANDARD LVCMOS18 } [get_ports { adcio[11] }]
set_property -dict { PACKAGE_PIN AW9  IOSTANDARD LVCMOS18 } [get_ports { adcio[12] }]
set_property -dict { PACKAGE_PIN AV9  IOSTANDARD LVCMOS18 } [get_ports { adcio[13] }]
set_property -dict { PACKAGE_PIN AW11 IOSTANDARD LVCMOS18 } [get_ports { adcio[14] }]
set_property -dict { PACKAGE_PIN AV11 IOSTANDARD LVCMOS18 } [get_ports { adcio[15] }]

set_property -dict { PACKAGE_PIN F13 IOSTANDARD LVCMOS18 } [get_ports { dacio[0] }]
set_property -dict { PACKAGE_PIN F14 IOSTANDARD LVCMOS18 } [get_ports { dacio[1] }]
set_property -dict { PACKAGE_PIN A14 IOSTANDARD LVCMOS18 } [get_ports { dacio[2] }]
set_property -dict { PACKAGE_PIN A15 IOSTANDARD LVCMOS18 } [get_ports { dacio[3] }]
set_property -dict { PACKAGE_PIN C16 IOSTANDARD LVCMOS18 } [get_ports { dacio[4] }]
set_property -dict { PACKAGE_PIN D16 IOSTANDARD LVCMOS18 } [get_ports { dacio[5] }]
set_property -dict { PACKAGE_PIN E15 IOSTANDARD LVCMOS18 } [get_ports { dacio[6] }]
set_property -dict { PACKAGE_PIN E16 IOSTANDARD LVCMOS18 } [get_ports { dacio[7] }]
set_property -dict { PACKAGE_PIN B15 IOSTANDARD LVCMOS18 } [get_ports { dacio[8] }]
set_property -dict { PACKAGE_PIN B16 IOSTANDARD LVCMOS18 } [get_ports { dacio[9] }]
set_property -dict { PACKAGE_PIN C14 IOSTANDARD LVCMOS18 } [get_ports { dacio[10] }]
set_property -dict { PACKAGE_PIN C15 IOSTANDARD LVCMOS18 } [get_ports { dacio[11] }]
set_property -dict { PACKAGE_PIN E14 IOSTANDARD LVCMOS18 } [get_ports { dacio[12] }]
set_property -dict { PACKAGE_PIN F15 IOSTANDARD LVCMOS18 } [get_ports { dacio[13] }]
set_property -dict { PACKAGE_PIN B12 IOSTANDARD LVCMOS18 } [get_ports { dacio[14] }]
set_property -dict { PACKAGE_PIN B13 IOSTANDARD LVCMOS18 } [get_ports { dacio[15] }]

##############################################################################

set_property -dict { PACKAGE_PIN G15 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][0] }]
set_property -dict { PACKAGE_PIN G16 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][1] }]
set_property -dict { PACKAGE_PIN H14 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][2] }]
set_property -dict { PACKAGE_PIN H15 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][3] }]
set_property -dict { PACKAGE_PIN G13 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][4] }]
set_property -dict { PACKAGE_PIN H13 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][5] }]
set_property -dict { PACKAGE_PIN J13 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][6] }]
set_property -dict { PACKAGE_PIN J14 IOSTANDARD LVCMOS18 } [get_ports { pmod[0][7] }]

set_property -dict { PACKAGE_PIN L17 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][0] }]
set_property -dict { PACKAGE_PIN M17 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][1] }]
set_property -dict { PACKAGE_PIN M14 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][2] }]
set_property -dict { PACKAGE_PIN N14 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][3] }]
set_property -dict { PACKAGE_PIN M15 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][4] }]
set_property -dict { PACKAGE_PIN N15 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][5] }]
set_property -dict { PACKAGE_PIN M16 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][6] }]
set_property -dict { PACKAGE_PIN N16 IOSTANDARD LVCMOS18 } [get_ports { pmod[1][7] }]

##############################################################################

set_property -dict { PACKAGE_PIN E10 IOSTANDARD SUB_LVDS } [get_ports { plClkP }]
set_property -dict { PACKAGE_PIN E9  IOSTANDARD SUB_LVDS } [get_ports { plClkN }]

set_property -dict { PACKAGE_PIN E11 IOSTANDARD SUB_LVDS } [get_ports { plSysRefP }]
set_property -dict { PACKAGE_PIN D11 IOSTANDARD SUB_LVDS } [get_ports { plSysRefN }]

set_property -dict { PACKAGE_PIN L15 IOSTANDARD LVCMOS18 } [get_ports { lmkSync   }]

set_property -dict { PACKAGE_PIN G10 IOSTANDARD LVCMOS18 } [get_ports { clkMuxSel[0] }]
set_property -dict { PACKAGE_PIN H11 IOSTANDARD LVCMOS18 } [get_ports { clkMuxSel[1] }]

set_property -dict { PACKAGE_PIN C9 IOSTANDARD LVCMOS18 DRIVE 8 } [get_ports { i2c1Scl }]
set_property -dict { PACKAGE_PIN D9 IOSTANDARD LVCMOS18 DRIVE 8 } [get_ports { i2c1Sda }]

##############################################################################

# ADC_CLK_P/N_226
set_property PACKAGE_PIN BA5 [get_ports { adcClkP }]
set_property PACKAGE_PIN BB5 [get_ports { adcClkN }]

# DAC_CLK_P/N_230
set_property PACKAGE_PIN B5 [get_ports { dacClkP }]
set_property PACKAGE_PIN A5 [get_ports { dacClkN }]

# SYSREF_P/N_228
set_property PACKAGE_PIN D2 [get_ports { sysRefP }]
set_property PACKAGE_PIN D1 [get_ports { sysRefN }]

# ADC TILE 224
set_property PACKAGE_PIN AU5 [get_ports { adcP[0] }]
set_property PACKAGE_PIN AU4 [get_ports { adcN[0] }]
set_property PACKAGE_PIN AU2 [get_ports { adcP[1] }]
set_property PACKAGE_PIN AU1 [get_ports { adcN[1] }]
set_property PACKAGE_PIN AR5 [get_ports { adcP[2] }]
set_property PACKAGE_PIN AR4 [get_ports { adcN[2] }]
set_property PACKAGE_PIN AR2 [get_ports { adcP[3] }]
set_property PACKAGE_PIN AR1 [get_ports { adcN[3] }]

# ADC TILE 225
set_property PACKAGE_PIN AN5 [get_ports { adcP[4] }]
set_property PACKAGE_PIN AN4 [get_ports { adcN[4] }]
set_property PACKAGE_PIN AN2 [get_ports { adcP[5] }]
set_property PACKAGE_PIN AN1 [get_ports { adcN[5] }]
set_property PACKAGE_PIN AL5 [get_ports { adcP[6] }]
set_property PACKAGE_PIN AL4 [get_ports { adcN[6] }]
set_property PACKAGE_PIN AL2 [get_ports { adcP[7] }]
set_property PACKAGE_PIN AL1 [get_ports { adcN[7] }]

# ADC TILE 226
set_property PACKAGE_PIN AJ5 [get_ports { adcP[8] }]
set_property PACKAGE_PIN AJ4 [get_ports { adcN[8] }]
set_property PACKAGE_PIN AJ2 [get_ports { adcP[9] }]
set_property PACKAGE_PIN AJ1 [get_ports { adcN[9] }]
set_property PACKAGE_PIN AG5 [get_ports { adcP[10] }]
set_property PACKAGE_PIN AG4 [get_ports { adcN[10] }]
set_property PACKAGE_PIN AG2 [get_ports { adcP[11] }]
set_property PACKAGE_PIN AG1 [get_ports { adcN[11] }]

# ADC TILE 227
set_property PACKAGE_PIN AE5 [get_ports { adcP[12] }]
set_property PACKAGE_PIN AE4 [get_ports { adcN[12] }]
set_property PACKAGE_PIN AE2 [get_ports { adcP[13] }]
set_property PACKAGE_PIN AE1 [get_ports { adcN[13] }]
set_property PACKAGE_PIN AC5 [get_ports { adcP[14] }]
set_property PACKAGE_PIN AC4 [get_ports { adcN[14] }]
set_property PACKAGE_PIN AC2 [get_ports { adcP[15] }]
set_property PACKAGE_PIN AC1 [get_ports { adcN[15] }]

# DAC TILE 228
set_property PACKAGE_PIN Y5 [get_ports { dacP[0] }]
set_property PACKAGE_PIN Y4 [get_ports { dacN[0] }]
set_property PACKAGE_PIN Y2 [get_ports { dacP[1] }]
set_property PACKAGE_PIN Y1 [get_ports { dacN[1] }]
set_property PACKAGE_PIN V5 [get_ports { dacP[2] }]
set_property PACKAGE_PIN V4 [get_ports { dacN[2] }]
set_property PACKAGE_PIN V2 [get_ports { dacP[3] }]
set_property PACKAGE_PIN V1 [get_ports { dacN[3] }]

# DAC TILE 229
set_property PACKAGE_PIN T5 [get_ports { dacP[4] }]
set_property PACKAGE_PIN T4 [get_ports { dacN[4] }]
set_property PACKAGE_PIN T2 [get_ports { dacP[5] }]
set_property PACKAGE_PIN T1 [get_ports { dacN[5] }]
set_property PACKAGE_PIN P5 [get_ports { dacP[6] }]
set_property PACKAGE_PIN P4 [get_ports { dacN[6] }]
set_property PACKAGE_PIN P2 [get_ports { dacP[7] }]
set_property PACKAGE_PIN P1 [get_ports { dacN[7] }]

# DAC TILE 230
set_property PACKAGE_PIN M5 [get_ports { dacP[8] }]
set_property PACKAGE_PIN M4 [get_ports { dacN[8] }]
set_property PACKAGE_PIN M2 [get_ports { dacP[9] }]
set_property PACKAGE_PIN M1 [get_ports { dacN[9] }]
set_property PACKAGE_PIN K5 [get_ports { dacP[10] }]
set_property PACKAGE_PIN K4 [get_ports { dacN[10] }]
set_property PACKAGE_PIN K2 [get_ports { dacP[11] }]
set_property PACKAGE_PIN K1 [get_ports { dacN[11] }]

# DAC TILE 231
set_property PACKAGE_PIN H5 [get_ports { dacP[12] }]
set_property PACKAGE_PIN H4 [get_ports { dacN[12] }]
set_property PACKAGE_PIN H2 [get_ports { dacP[13] }]
set_property PACKAGE_PIN H1 [get_ports { dacN[13] }]
set_property PACKAGE_PIN F5 [get_ports { dacP[14] }]
set_property PACKAGE_PIN F4 [get_ports { dacN[14] }]
set_property PACKAGE_PIN F2 [get_ports { dacP[15] }]
set_property PACKAGE_PIN F1 [get_ports { dacN[15] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
