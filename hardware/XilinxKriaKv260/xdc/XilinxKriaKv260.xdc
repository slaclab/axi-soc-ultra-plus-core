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

##############################################################################

set_property -dict { PACKAGE_PIN R13 IOSTANDARD ANALOG } [get_ports { vPIn }]
set_property -dict { PACKAGE_PIN T12 IOSTANDARD ANALOG } [get_ports { vNIn }]

set_property -dict { PACKAGE_PIN A12 IOSTANDARD LVCMOS33 } [get_ports { fanEnableL }] ;# fanEnableL = HDA20 = som240_1_c24: Bank  45 VCCO - som240_1_b13 - IO_L11P_AD9P_45

set_property -dict { PACKAGE_PIN H12 IOSTANDARD LVCMOS33 } [get_ports pmod[0]] ;# PMOD pin 1 - som240_1_a17
set_property -dict { PACKAGE_PIN B10 IOSTANDARD LVCMOS33 } [get_ports pmod[1]] ;# PMOD pin 2 - som240_1_b20
set_property -dict { PACKAGE_PIN E10 IOSTANDARD LVCMOS33 } [get_ports pmod[2]] ;# PMOD pin 3 - som240_1_d20
set_property -dict { PACKAGE_PIN E12 IOSTANDARD LVCMOS33 } [get_ports pmod[3]] ;# PMOD pin 4 - som240_1_b21
set_property -dict { PACKAGE_PIN D10 IOSTANDARD LVCMOS33 } [get_ports pmod[4]] ;# PMOD pin 5 - som240_1_d21
set_property -dict { PACKAGE_PIN D11 IOSTANDARD LVCMOS33 } [get_ports pmod[5]] ;# PMOD pin 6 - som240_1_b22
set_property -dict { PACKAGE_PIN C11 IOSTANDARD LVCMOS33 } [get_ports pmod[6]] ;# PMOD pin 7 - som240_1_d22
set_property -dict { PACKAGE_PIN B11 IOSTANDARD LVCMOS33 } [get_ports pmod[7]] ;# PMOD pin 8 - som240_1_c22

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
