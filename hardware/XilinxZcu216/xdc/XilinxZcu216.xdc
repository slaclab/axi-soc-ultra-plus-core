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

set_property -dict { PACKAGE_PIN XXX IOSTANDARD LVCMOS18 } [get_ports { lmkSync   }]

set_property -dict { PACKAGE_PIN XXX IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[0] }]
set_property -dict { PACKAGE_PIN XXX IOSTANDARD LVCMOS12 } [get_ports { clkMuxSel[1] }]

set_property -dict { PACKAGE_PIN XXX IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Scl }]
set_property -dict { PACKAGE_PIN XXX IOSTANDARD LVCMOS12 DRIVE 8 } [get_ports { i2c1Sda }]

##############################################################################

set_property PACKAGE_PIN XXX [get_ports { adcClkP }]
set_property PACKAGE_PIN XXX [get_ports { adcClkN }]

set_property PACKAGE_PIN XXX [get_ports { dacClkP }]
set_property PACKAGE_PIN XXX [get_ports { dacClkN }]

set_property PACKAGE_PIN XXX [get_ports { sysRefP }]
set_property PACKAGE_PIN XXX [get_ports { sysRefN }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[0] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[0] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[1] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[1] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[2] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[2] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[3] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[3] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[4] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[4] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[5] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[5] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[6] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[6] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[7] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[7] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[8] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[8] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[9] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[9] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[10] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[10] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[11] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[11] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[12] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[12] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[13] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[13] }]

# ADC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { adcP[14] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[14] }]
set_property PACKAGE_PIN XXX [get_ports { adcP[15] }]
set_property PACKAGE_PIN XXX [get_ports { adcN[15] }]

# DAC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { dacP[0] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[0] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[1] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[1] }]

set_property PACKAGE_PIN XXX [get_ports { dacP[2] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[2] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[3] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[3] }]

# DAC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { dacP[4] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[4] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[5] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[5] }]

set_property PACKAGE_PIN XXX [get_ports { dacP[6] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[6] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[7] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[7] }]

# DAC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { dacP[8] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[8] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[9] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[9] }]

set_property PACKAGE_PIN XXX [get_ports { dacP[10] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[10] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[11] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[11] }]

# DAC TILE XXX
set_property PACKAGE_PIN XXX [get_ports { dacP[12] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[12] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[13] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[13] }]

set_property PACKAGE_PIN XXX [get_ports { dacP[14] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[14] }]
set_property PACKAGE_PIN XXX [get_ports { dacP[15] }]
set_property PACKAGE_PIN XXX [get_ports { dacN[15] }]

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/U_REG/U_Version/GEN_DEVICE_DNA.DeviceDna_1/GEN_ULTRA_SCALE.DeviceDnaUltraScale_Inst/BUFGCE_DIV_Inst/O]]

##############################################################################
