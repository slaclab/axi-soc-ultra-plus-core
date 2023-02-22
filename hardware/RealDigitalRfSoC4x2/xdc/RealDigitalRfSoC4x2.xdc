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

# ADC TILE 224
set_property PACKAGE_PIN AP1 [get_ports { adcP[0] }]
set_property PACKAGE_PIN AP2 [get_ports { adcN[0] }]
set_property PACKAGE_PIN AM1 [get_ports { adcP[1] }]
set_property PACKAGE_PIN AM2 [get_ports { adcN[1] }]
set_property PACKAGE_PIN AF5 [get_ports { adcClkP[0] }]
set_property PACKAGE_PIN AF4 [get_ports { adcClkN[0] }]

# ADC TILE 226
set_property PACKAGE_PIN AF2 [get_ports { adcP[2] }]
set_property PACKAGE_PIN AF1 [get_ports { adcN[2] }]
set_property PACKAGE_PIN AD2 [get_ports { adcP[3] }]
set_property PACKAGE_PIN AD1 [get_ports { adcN[3] }]
set_property PACKAGE_PIN AB5 [get_ports { adcClkP[1] }]
set_property PACKAGE_PIN AB4 [get_ports { adcClkN[1] }]

# DAC TILE 228
set_property PACKAGE_PIN U2 [get_ports { dacP[0] }]
set_property PACKAGE_PIN U1 [get_ports { dacN[0] }]
set_property PACKAGE_PIN R5 [get_ports { dacClkP[0] }]
set_property PACKAGE_PIN R4 [get_ports { dacClkN[0] }]
set_property PACKAGE_PIN U5 [get_ports { sysRefP }]
set_property PACKAGE_PIN U4 [get_ports { sysRefN }]

# DAC TILE 230
set_property PACKAGE_PIN J2 [get_ports { dacP[1] }]
set_property PACKAGE_PIN J1 [get_ports { dacN[1] }]
set_property PACKAGE_PIN N5 [get_ports { dacClkP[1] }]
set_property PACKAGE_PIN N4 [get_ports { dacClkN[1] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
