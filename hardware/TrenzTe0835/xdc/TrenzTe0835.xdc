##############################################################################
## This file is part of 'axi-soc-ultra-plus-core'.
## It is subject to the license terms in the LICENSE.txt file found in the
## top-level directory of this distribution and at:
##    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
## No part of 'axi-soc-ultra-plus-core', including this file,
## may be copied, modified, propagated, or distributed except according to
## the terms contained in the LICENSE.txt file.
##############################################################################

# https://wiki.trenz-electronic.de/display/PD/TE0835+TRM
# https://wiki.trenz-electronic.de/display/PD/TE0835+Test+Board

set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLNONE [current_design]

set_property -dict { PACKAGE_PIN T15 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN U14 IOSTANDARD ANALOG } [get_ports { vNIn }]

##############################################################################

# ADC_CLK_P/N_226
set_property PACKAGE_PIN Y5 [get_ports { adcClkP }]
set_property PACKAGE_PIN Y4 [get_ports { adcClkN }]

# DAC_CLK_P/N_230
set_property PACKAGE_PIN L5 [get_ports { dacClkP }]
set_property PACKAGE_PIN L4 [get_ports { dacClkN }]

# SYSREF_P/N_228
set_property PACKAGE_PIN N5 [get_ports { sysRefP }]
set_property PACKAGE_PIN N4 [get_ports { sysRefN }]

# ADC0 TILE 224
set_property PACKAGE_PIN AK2 [get_ports { adcP[0] }]
set_property PACKAGE_PIN AK1 [get_ports { adcN[0] }]
set_property PACKAGE_PIN AH2 [get_ports { adcP[1] }]
set_property PACKAGE_PIN AH1 [get_ports { adcN[1] }]

# ADC0 TILE 225
set_property PACKAGE_PIN AF2 [get_ports { adcP[2] }]
set_property PACKAGE_PIN AF1 [get_ports { adcN[2] }]
set_property PACKAGE_PIN AD2 [get_ports { adcP[3] }]
set_property PACKAGE_PIN AD1 [get_ports { adcN[3] }]

# ADC0 TILE 226
set_property PACKAGE_PIN AB2 [get_ports { adcP[4] }]
set_property PACKAGE_PIN AB1 [get_ports { adcN[4] }]
set_property PACKAGE_PIN Y2  [get_ports { adcP[5] }]
set_property PACKAGE_PIN Y1  [get_ports { adcN[5] }]

# ADC0 TILE 227
set_property PACKAGE_PIN V2 [get_ports { adcP[6] }]
set_property PACKAGE_PIN V1 [get_ports { adcN[6] }]
set_property PACKAGE_PIN T2 [get_ports { adcP[7] }]
set_property PACKAGE_PIN T1 [get_ports { adcN[7] }]

# DAC2 TILE 228
set_property PACKAGE_PIN N2 [get_ports { dacP[0] }]
set_property PACKAGE_PIN N1 [get_ports { dacN[0] }]
set_property PACKAGE_PIN L2 [get_ports { dacP[1] }]
set_property PACKAGE_PIN L1 [get_ports { dacN[1] }]

set_property PACKAGE_PIN J2 [get_ports { dacP[2] }]
set_property PACKAGE_PIN J1 [get_ports { dacN[2] }]
set_property PACKAGE_PIN G2 [get_ports { dacP[3] }]
set_property PACKAGE_PIN G1 [get_ports { dacN[3] }]

# DAC2 TILE 229
set_property PACKAGE_PIN E2 [get_ports { dacP[4] }]
set_property PACKAGE_PIN E1 [get_ports { dacN[4] }]
set_property PACKAGE_PIN C2 [get_ports { dacP[5] }]
set_property PACKAGE_PIN C1 [get_ports { dacN[5] }]

set_property PACKAGE_PIN B4 [get_ports { dacP[6] }]
set_property PACKAGE_PIN A4 [get_ports { dacN[6] }]
set_property PACKAGE_PIN B6 [get_ports { dacP[7] }]
set_property PACKAGE_PIN A6 [get_ports { dacN[7] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
