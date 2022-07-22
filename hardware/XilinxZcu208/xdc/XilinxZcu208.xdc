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

set_property -dict { PACKAGE_PIN AU2 IOSTANDARD LVCMOS18 } [get_ports { lmkSync   }]

set_property -dict { PACKAGE_PIN C11 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[0] }]
set_property -dict { PACKAGE_PIN B12 IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[1] }]

set_property -dict { PACKAGE_PIN G10 IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Scl }]
set_property -dict { PACKAGE_PIN K12 IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Sda }]

##############################################################################

set_property PACKAGE_PIN AB5 [get_ports { adcClkP }]
set_property PACKAGE_PIN AB4 [get_ports { adcClkN }]

set_property PACKAGE_PIN R5 [get_ports { dacClkP }]
set_property PACKAGE_PIN R4 [get_ports { dacClkN }]

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
