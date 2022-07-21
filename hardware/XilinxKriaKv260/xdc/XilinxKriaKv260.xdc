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

set_property PACKAGE_PIN G11 [get_ports { hda[0]  }] ;# HDA00=som240_1_d16: Bank  45 VCCO - som240_1_b13 - IO_L5P_HDGC_45
set_property PACKAGE_PIN F10 [get_ports { hda[1]  }] ;# HDA01=som240_1_d17: Bank  45 VCCO - som240_1_b13 - IO_L5N_HDGC_45
set_property PACKAGE_PIN J11 [get_ports { hda[2]  }] ;# HDA02=som240_1_d18: Bank  45 VCCO - som240_1_b13 - IO_L1P_AD15P_45
set_property PACKAGE_PIN J10 [get_ports { hda[3]  }] ;# HDA03=som240_1_b16: Bank  45 VCCO - som240_1_b13 - IO_L1N_AD15N_45
set_property PACKAGE_PIN K13 [get_ports { hda[4]  }] ;# HDA04=som240_1_b17: Bank  45 VCCO - som240_1_b13 - IO_L2P_AD14P_45
set_property PACKAGE_PIN K12 [get_ports { hda[5]  }] ;# HDA05=som240_1_b18: Bank  45 VCCO - som240_1_b13 - IO_L2N_AD14N_45
set_property PACKAGE_PIN H11 [get_ports { hda[6]  }] ;# HDA06=som240_1_c18: Bank  45 VCCO - som240_1_b13 - IO_L3P_AD13P_45
set_property PACKAGE_PIN G10 [get_ports { hda[7]  }] ;# HDA07=som240_1_c19: Bank  45 VCCO - som240_1_b13 - IO_L3N_AD13N_45
set_property PACKAGE_PIN F12 [get_ports { hda[8]  }] ;# HDA08=som240_1_c20: Bank  45 VCCO - som240_1_b13 - IO_L6P_HDGC_45
set_property PACKAGE_PIN F11 [get_ports { hda[9]  }] ;# HDA09=som240_1_a15: Bank  45 VCCO - som240_1_b13 - IO_L6N_HDGC_45
set_property PACKAGE_PIN J12 [get_ports { hda[10] }] ;# HDA10=som240_1_a16: Bank  45 VCCO - som240_1_b13 - IO_L4P_AD12P_45
set_property PACKAGE_PIN H12 [get_ports { hda[11] }] ;# HDA11=som240_1_a17: Bank  45 VCCO - som240_1_b13 - IO_L4N_AD12N_45
set_property PACKAGE_PIN E10 [get_ports { hda[12] }] ;# HDA12=som240_1_d20: Bank  45 VCCO - som240_1_b13 - IO_L7P_HDGC_45
set_property PACKAGE_PIN D10 [get_ports { hda[13] }] ;# HDA13=som240_1_d21: Bank  45 VCCO - som240_1_b13 - IO_L7N_HDGC_45
set_property PACKAGE_PIN C11 [get_ports { hda[14] }] ;# HDA14=som240_1_d22: Bank  45 VCCO - som240_1_b13 - IO_L9P_AD11P_45
set_property PACKAGE_PIN B10 [get_ports { hda[15] }] ;# HDA15=som240_1_b20: Bank  45 VCCO - som240_1_b13 - IO_L9N_AD11N_45
set_property PACKAGE_PIN E12 [get_ports { hda[16] }] ;# HDA16=som240_1_b21: Bank  45 VCCO - som240_1_b13 - IO_L8P_HDGC_45
set_property PACKAGE_PIN D11 [get_ports { hda[17] }] ;# HDA17=som240_1_b22: Bank  45 VCCO - som240_1_b13 - IO_L8N_HDGC_45
set_property PACKAGE_PIN B11 [get_ports { hda[18] }] ;# HDA18=som240_1_c22: Bank  45 VCCO - som240_1_b13 - IO_L10P_AD10P_45
set_property PACKAGE_PIN A10 [get_ports { hda[19] }] ;# HDA19=som240_1_c23: Bank  45 VCCO - som240_1_b13 - IO_L10N_AD10N_45
set_property PACKAGE_PIN A12 [get_ports { hda[20] }] ;# HDA20=som240_1_c24: Bank  45 VCCO - som240_1_b13 - IO_L11P_AD9P_45


##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
