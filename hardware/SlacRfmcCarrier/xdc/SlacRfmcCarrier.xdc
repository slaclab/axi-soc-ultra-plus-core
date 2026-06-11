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

create_pblock RFDC_ADC_GRP
resize_pblock [get_pblocks RFDC_ADC_GRP] -add {CLOCKREGION_X4Y0:CLOCKREGION_X5Y3}

create_pblock RFDC_DAC_GRP
resize_pblock [get_pblocks RFDC_DAC_GRP] -add {CLOCKREGION_X4Y4:CLOCKREGION_X5Y7}

##############################################################################
# DDR4 SO-DIMM Ports
##############################################################################

set_property PACKAGE_PIN A21 [get_ports {ddrDq[63]}]
set_property PACKAGE_PIN A20 [get_ports {ddrDq[62]}]
set_property PACKAGE_PIN B20 [get_ports {ddrDq[61]}]
set_property PACKAGE_PIN C20 [get_ports {ddrDq[60]}]
set_property PACKAGE_PIN A22 [get_ports {ddrDqsN[7]}]
set_property PACKAGE_PIN B22 [get_ports {ddrDqsP[7]}]
set_property PACKAGE_PIN C22 [get_ports {ddrDq[59]}]
set_property PACKAGE_PIN C21 [get_ports {ddrDq[58]}]
set_property PACKAGE_PIN A24 [get_ports {ddrDq[57]}]
set_property PACKAGE_PIN B24 [get_ports {ddrDq[56]}]
set_property PACKAGE_PIN C23 [get_ports {ddrDm[7]}]

set_property PACKAGE_PIN D21 [get_ports {ddrDq[55]}]
set_property PACKAGE_PIN E21 [get_ports {ddrDq[54]}]
set_property PACKAGE_PIN E23 [get_ports {ddrDq[53]}]
set_property PACKAGE_PIN E22 [get_ports {ddrDq[52]}]
set_property PACKAGE_PIN D24 [get_ports {ddrDqsN[6]}]
set_property PACKAGE_PIN D23 [get_ports {ddrDqsP[6]}]
set_property PACKAGE_PIN E24 [get_ports {ddrDq[51]}]
set_property PACKAGE_PIN F24 [get_ports {ddrDq[50]}]
set_property PACKAGE_PIN F20 [get_ports {ddrDq[49]}]
set_property PACKAGE_PIN G20 [get_ports {ddrDq[48]}]
set_property PACKAGE_PIN F21 [get_ports {ddrDm[6]}]

set_property PACKAGE_PIN G23 [get_ports {ddrDq[47]}]
set_property PACKAGE_PIN H23 [get_ports {ddrDq[46]}]
set_property PACKAGE_PIN G22 [get_ports {ddrDq[45]}]
set_property PACKAGE_PIN H22 [get_ports {ddrDq[44]}]
set_property PACKAGE_PIN H20 [get_ports {ddrDqsN[5]}]
set_property PACKAGE_PIN J20 [get_ports {ddrDqsP[5]}]
set_property PACKAGE_PIN H21 [get_ports {ddrDq[43]}]
set_property PACKAGE_PIN J21 [get_ports {ddrDq[42]}]
set_property PACKAGE_PIN K24 [get_ports {ddrDq[41]}]
set_property PACKAGE_PIN L24 [get_ports {ddrDq[40]}]
set_property PACKAGE_PIN J23 [get_ports {ddrDm[5]}]

set_property PACKAGE_PIN L20 [get_ports {ddrDq[39]}]
set_property PACKAGE_PIN L19 [get_ports {ddrDq[38]}]
set_property PACKAGE_PIN L23 [get_ports {ddrDq[37]}]
set_property PACKAGE_PIN L22 [get_ports {ddrDq[36]}]
set_property PACKAGE_PIN K22 [get_ports {ddrDqsN[4]}]
set_property PACKAGE_PIN K21 [get_ports {ddrDqsP[4]}]
set_property PACKAGE_PIN L21 [get_ports {ddrDq[35]}]
set_property PACKAGE_PIN M20 [get_ports {ddrDq[34]}]
set_property PACKAGE_PIN M19 [get_ports {ddrDq[33]}]
set_property PACKAGE_PIN N19 [get_ports {ddrDq[32]}]
set_property PACKAGE_PIN N20 [get_ports {ddrDm[4]}]

set_property PACKAGE_PIN A14 [get_ports {ddrDq[31]}]
set_property PACKAGE_PIN A15 [get_ports {ddrDq[30]}]
set_property PACKAGE_PIN A11 [get_ports {ddrDq[29]}]
set_property PACKAGE_PIN A12 [get_ports {ddrDq[28]}]
set_property PACKAGE_PIN B15 [get_ports {ddrDqsN[3]}]
set_property PACKAGE_PIN C15 [get_ports {ddrDqsP[3]}]
set_property PACKAGE_PIN B13 [get_ports {ddrDq[27]}]
set_property PACKAGE_PIN B14 [get_ports {ddrDq[26]}]
set_property PACKAGE_PIN C13 [get_ports {ddrDq[25]}]
set_property PACKAGE_PIN D13 [get_ports {ddrDq[24]}]
set_property PACKAGE_PIN C12 [get_ports {ddrDm[3]}]

set_property PACKAGE_PIN D14 [get_ports {ddrDq[23]}]
set_property PACKAGE_PIN E14 [get_ports {ddrDq[22]}]
set_property PACKAGE_PIN E11 [get_ports {ddrDq[21]}]
set_property PACKAGE_PIN F12 [get_ports {ddrDq[20]}]
set_property PACKAGE_PIN E12 [get_ports {ddrDqsN[2]}]
set_property PACKAGE_PIN E13 [get_ports {ddrDqsP[2]}]
set_property PACKAGE_PIN F14 [get_ports {ddrDq[19]}]
set_property PACKAGE_PIN G14 [get_ports {ddrDq[18]}]
set_property PACKAGE_PIN H12 [get_ports {ddrDq[17]}]
set_property PACKAGE_PIN H13 [get_ports {ddrDq[16]}]
set_property PACKAGE_PIN G13 [get_ports {ddrDm[2]}]

set_property PACKAGE_PIN H10 [get_ports {ddrDq[15]}]
set_property PACKAGE_PIN H11 [get_ports {ddrDq[14]}]
set_property PACKAGE_PIN J10 [get_ports {ddrDq[13]}]
set_property PACKAGE_PIN J11 [get_ports {ddrDq[12]}]
set_property PACKAGE_PIN J13 [get_ports {ddrDqsN[1]}]
set_property PACKAGE_PIN J14 [get_ports {ddrDqsP[1]}]
set_property PACKAGE_PIN F10 [get_ports {ddrDq[11]}]
set_property PACKAGE_PIN F11 [get_ports {ddrDq[10]}]
set_property PACKAGE_PIN K10 [get_ports {ddrDq[9]}]
set_property PACKAGE_PIN K11 [get_ports {ddrDq[8]}]
set_property PACKAGE_PIN K13 [get_ports {ddrDm[1]}]

set_property PACKAGE_PIN F9 [get_ports {ddrDq[7]}]
set_property PACKAGE_PIN G9 [get_ports {ddrDq[6]}]
set_property PACKAGE_PIN G6 [get_ports {ddrDq[5]}]
set_property PACKAGE_PIN G7 [get_ports {ddrDq[4]}]
set_property PACKAGE_PIN G8 [get_ports {ddrDqsN[0]}]
set_property PACKAGE_PIN H8 [get_ports {ddrDqsP[0]}]
set_property PACKAGE_PIN H6 [get_ports {ddrDq[3]}]
set_property PACKAGE_PIN H7 [get_ports {ddrDq[2]}]
set_property PACKAGE_PIN J9 [get_ports {ddrDq[1]}]
set_property PACKAGE_PIN K9 [get_ports {ddrDq[0]}]
set_property PACKAGE_PIN J8 [get_ports {ddrDm[0]}]

# set_property PACKAGE_PIN A16 [get_ports {ddrDq[71]}]
# set_property PACKAGE_PIN A17 [get_ports {ddrDq[70]}]
# set_property PACKAGE_PIN D15 [get_ports {ddrDq[69]}]
# set_property PACKAGE_PIN D16 [get_ports {ddrDq[68]}]
# set_property PACKAGE_PIN B17 [get_ports {ddrDqsN[8]}]
# set_property PACKAGE_PIN B18 [get_ports {ddrDqsP[8]}]
# set_property PACKAGE_PIN C16 [get_ports {ddrDq[67]}]
# set_property PACKAGE_PIN C17 [get_ports {ddrDq[66]}]
# set_property PACKAGE_PIN A19 [get_ports {ddrDq[65]}]
# set_property PACKAGE_PIN B19 [get_ports {ddrDq[64]}]
# set_property PACKAGE_PIN D18 [get_ports {ddrDm[8]}]

# set_property PACKAGE_PIN E17 [get_ports {ddrParity}]
# set_property PACKAGE_PIN E18 [get_ports {ddrAlertL}]
set_property PACKAGE_PIN E16 [get_ports {ddrRstL}]
set_property PACKAGE_PIN F16 [get_ports {ddrActL}]

# set_property PACKAGE_PIN F19 [get_ports {ddrOdt[1]}]
set_property PACKAGE_PIN G19 [get_ports {ddrOdt[0]}]

# set_property PACKAGE_PIN F15 [get_ports {ddrCke[1]}]
set_property PACKAGE_PIN G15 [get_ports {ddrCke[0]}]

# set_property PACKAGE_PIN G18 [get_ports {ddrCsL[1]}]
set_property PACKAGE_PIN H18 [get_ports {ddrCsL[0]}]

set_property PACKAGE_PIN F17 [get_ports {ddrBg[1]}]
set_property PACKAGE_PIN G17 [get_ports {ddrBg[0]}]

set_property PACKAGE_PIN M18 [get_ports {ddrBa[1]}]
set_property PACKAGE_PIN J18 [get_ports {ddrBa[0]}]

set_property PACKAGE_PIN J19 [get_ports {ddrA[16]}]; # ddrRasL

set_property PACKAGE_PIN  H16         [get_ports {ddrClkN}]
set_property PACKAGE_PIN  H17         [get_ports {ddrClkP}]
set_property IOSTANDARD   DIFF_SSTL12 [get_ports {ddrClkP ddrClkN}]
set_property IBUF_LOW_PWR FALSE       [get_ports {ddrClkP ddrClkN}]
set_property PULLTYPE     KEEPER      [get_ports {ddrClkP ddrClkN}]
create_clock -name ddrClkP  -period  6.4 [get_ports {ddrClkP}]

set_property PACKAGE_PIN K18 [get_ports {ddrA[15]}]; # ddrCasL
set_property PACKAGE_PIN K19 [get_ports {ddrA[14]}]; # ddrWeL

set_property PACKAGE_PIN J16 [get_ports {ddrA[13]}]
set_property PACKAGE_PIN K16 [get_ports {ddrA[12]}]
set_property PACKAGE_PIN K17 [get_ports {ddrA[11]}]
set_property PACKAGE_PIN L17 [get_ports {ddrA[10]}]
set_property PACKAGE_PIN H15 [get_ports {ddrA[9]}]
set_property PACKAGE_PIN J15 [get_ports {ddrA[8]}]
set_property PACKAGE_PIN M17 [get_ports {ddrA[7]}]
set_property PACKAGE_PIN N17 [get_ports {ddrA[6]}]
set_property PACKAGE_PIN L12 [get_ports {ddrA[5]}]
set_property PACKAGE_PIN M12 [get_ports {ddrA[4]}]
set_property PACKAGE_PIN L14 [get_ports {ddrA[3]}]
set_property PACKAGE_PIN L15 [get_ports {ddrA[2]}]
set_property PACKAGE_PIN M13 [get_ports {ddrA[1]}]
set_property PACKAGE_PIN N13 [get_ports {ddrA[0]}]

# set_property PACKAGE_PIN M15 [get_ports {ddrCkN[1]}]
# set_property PACKAGE_PIN N15 [get_ports {ddrCkP[1]}]

set_property PACKAGE_PIN M14 [get_ports {ddrCkN[0]}]
set_property PACKAGE_PIN N14 [get_ports {ddrCkP[0]}]

##############################################################################

set_property -dict { PACKAGE_PIN AF16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[14]}]
set_property -dict { PACKAGE_PIN AF17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[14]}]
set_property -dict { PACKAGE_PIN AH15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[13]}]
set_property -dict { PACKAGE_PIN AH16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[13]}]
set_property -dict { PACKAGE_PIN AH17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[12]}]
set_property -dict { PACKAGE_PIN AG17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[12]}]
set_property -dict { PACKAGE_PIN AJ15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[11]}]
set_property -dict { PACKAGE_PIN AJ16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[11]}]
set_property -dict { PACKAGE_PIN AK16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[10]}]
set_property -dict { PACKAGE_PIN AK17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[10]}]

set_property -dict { PACKAGE_PIN AL15 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoN[2]}]
set_property -dict { PACKAGE_PIN AL16 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoP[2]}]
set_property -dict { PACKAGE_PIN AM17 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoN[1]}]
set_property -dict { PACKAGE_PIN AL17 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoP[1]}]
set_property -dict { PACKAGE_PIN AN15 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoN[0]}]
set_property -dict { PACKAGE_PIN AM15 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoP[0]}]

set_property -dict { PACKAGE_PIN AN16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[9]}]
set_property -dict { PACKAGE_PIN AN17 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[9]}]
set_property -dict { PACKAGE_PIN AR14 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[8]}]
set_property -dict { PACKAGE_PIN AP14 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[8]}]
set_property -dict { PACKAGE_PIN AR16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[7]}]
set_property -dict { PACKAGE_PIN AP16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[7]}]
set_property -dict { PACKAGE_PIN AR13 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[6]}]
set_property -dict { PACKAGE_PIN AP13 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[6]}]
set_property -dict { PACKAGE_PIN AU14 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[5]}]
set_property -dict { PACKAGE_PIN AU15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[5]}]
set_property -dict { PACKAGE_PIN AT15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[4]}]
set_property -dict { PACKAGE_PIN AT16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[4]}]
set_property -dict { PACKAGE_PIN AW16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[3]}]
set_property -dict { PACKAGE_PIN AV16 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[3]}]
set_property -dict { PACKAGE_PIN AV13 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[2]}]
set_property -dict { PACKAGE_PIN AU13 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[2]}]
set_property -dict { PACKAGE_PIN AW15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxN[1]}]
set_property -dict { PACKAGE_PIN AV15 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {mpsBusRxP[1]}]

set_property -dict { PACKAGE_PIN AW13 IOSTANDARD LVDS } [get_ports {mpsTxN}]
set_property -dict { PACKAGE_PIN AW14 IOSTANDARD LVDS } [get_ports {mpsTxP}]

set_property -dict { PACKAGE_PIN AJ13 IOSTANDARD LVCMOS18 } [get_ports {adcIo[0]}]
set_property -dict { PACKAGE_PIN AH13 IOSTANDARD LVCMOS18 } [get_ports {adcIo[1]}]
set_property -dict { PACKAGE_PIN AH12 IOSTANDARD LVCMOS18 } [get_ports {adcIo[2]}]
set_property -dict { PACKAGE_PIN AG12 IOSTANDARD LVCMOS18 } [get_ports {adcIo[3]}]
set_property -dict { PACKAGE_PIN AK14 IOSTANDARD LVCMOS18 } [get_ports {adcIo[4]}]
set_property -dict { PACKAGE_PIN AJ14 IOSTANDARD LVCMOS18 } [get_ports {adcIo[5]}]
set_property -dict { PACKAGE_PIN AK12 IOSTANDARD LVCMOS18 } [get_ports {adcIo[6]}]
set_property -dict { PACKAGE_PIN AJ12 IOSTANDARD LVCMOS18 } [get_ports {adcIo[7]}]

set_property -dict { PACKAGE_PIN AL7 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoN[4]}]
set_property -dict { PACKAGE_PIN AL8 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoP[4]}]
set_property -dict { PACKAGE_PIN AM9 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoN[3]}]
set_property -dict { PACKAGE_PIN AL9 IOSTANDARD LVCMOS18 } [get_ports {rfmcIoP[3]}]

set_property -dict { PACKAGE_PIN AN7 IOSTANDARD LVDS } [get_ports {lmkClkInN}]
set_property -dict { PACKAGE_PIN AN8 IOSTANDARD LVDS } [get_ports {lmkClkInP}]

set_property -dict { PACKAGE_PIN AM7 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkSysRefN}]
set_property -dict { PACKAGE_PIN AM8 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkSysRefP}]

set_property -dict { PACKAGE_PIN AR9 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkClkOutN[1]}]
set_property -dict { PACKAGE_PIN AP9 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkClkOutP[1]}]

set_property -dict { PACKAGE_PIN AR8 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkClkOutN[0]}]
set_property -dict { PACKAGE_PIN AP8 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports {lmkClkOutP[0]}]

set_property -dict { PACKAGE_PIN AP10 IOSTANDARD LVCMOS18 } [get_ports {adcIo[8]}]
set_property -dict { PACKAGE_PIN AN10 IOSTANDARD LVCMOS18 } [get_ports {adcIo[9]}]
set_property -dict { PACKAGE_PIN AP11 IOSTANDARD LVCMOS18 } [get_ports {adcIo[10]}]
set_property -dict { PACKAGE_PIN AN11 IOSTANDARD LVCMOS18 } [get_ports {adcIo[11]}]
set_property -dict { PACKAGE_PIN AN13 IOSTANDARD LVCMOS18 } [get_ports {adcIo[12]}]
set_property -dict { PACKAGE_PIN AM13 IOSTANDARD LVCMOS18 } [get_ports {adcIo[13]}]
set_property -dict { PACKAGE_PIN AM10 IOSTANDARD LVCMOS18 } [get_ports {adcIo[14]}]
set_property -dict { PACKAGE_PIN AL10 IOSTANDARD LVCMOS18 } [get_ports {adcIo[15]}]

set_property -dict { PACKAGE_PIN AR11 IOSTANDARD LVCMOS18 } [get_ports {dacIo[0]}]
set_property -dict { PACKAGE_PIN AR12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[1]}]
set_property -dict { PACKAGE_PIN AN12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[2]}]
set_property -dict { PACKAGE_PIN AM12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[3]}]
set_property -dict { PACKAGE_PIN AU10 IOSTANDARD LVCMOS18 } [get_ports {dacIo[4]}]
set_property -dict { PACKAGE_PIN AT10 IOSTANDARD LVCMOS18 } [get_ports {dacIo[5]}]
set_property -dict { PACKAGE_PIN AW8  IOSTANDARD LVCMOS18 } [get_ports {dacIo[6]}]
set_property -dict { PACKAGE_PIN AW9  IOSTANDARD LVCMOS18 } [get_ports {dacIo[7]}]
set_property -dict { PACKAGE_PIN AT11 IOSTANDARD LVCMOS18 } [get_ports {dacIo[8]}]
set_property -dict { PACKAGE_PIN AT12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[9]}]
set_property -dict { PACKAGE_PIN AW11 IOSTANDARD LVCMOS18 } [get_ports {dacIo[10]}]
set_property -dict { PACKAGE_PIN AV11 IOSTANDARD LVCMOS18 } [get_ports {dacIo[11]}]
set_property -dict { PACKAGE_PIN AV12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[12]}]
set_property -dict { PACKAGE_PIN AU12 IOSTANDARD LVCMOS18 } [get_ports {dacIo[13]}]
set_property -dict { PACKAGE_PIN AW10 IOSTANDARD LVCMOS18 } [get_ports {dacIo[14]}]
set_property -dict { PACKAGE_PIN AV10 IOSTANDARD LVCMOS18 } [get_ports {dacIo[15]}]

set_property -dict { PACKAGE_PIN AF19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[4]}]
set_property -dict { PACKAGE_PIN AF20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[4]}]
set_property -dict { PACKAGE_PIN AH18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[5]}]
set_property -dict { PACKAGE_PIN AG18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[5]}]
set_property -dict { PACKAGE_PIN AH20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[6]}]
set_property -dict { PACKAGE_PIN AG20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[6]}]
set_property -dict { PACKAGE_PIN AJ19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[7]}]
set_property -dict { PACKAGE_PIN AJ20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[7]}]
set_property -dict { PACKAGE_PIN AK21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[8]}]
set_property -dict { PACKAGE_PIN AK22 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[8]}]
set_property -dict { PACKAGE_PIN AK18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[9]}]
set_property -dict { PACKAGE_PIN AJ18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[9]}]
set_property -dict { PACKAGE_PIN AL20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[10]}]
set_property -dict { PACKAGE_PIN AL21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[10]}]
set_property -dict { PACKAGE_PIN AM19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[11]}]
set_property -dict { PACKAGE_PIN AL19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[11]}]
set_property -dict { PACKAGE_PIN AM22 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[12]}]
set_property -dict { PACKAGE_PIN AL22 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[12]}]
set_property -dict { PACKAGE_PIN AN18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[13]}]
set_property -dict { PACKAGE_PIN AM18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[13]}]
set_property -dict { PACKAGE_PIN AP21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[0]}]
set_property -dict { PACKAGE_PIN AN21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[0]}]
set_property -dict { PACKAGE_PIN AN20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[1]}]
set_property -dict { PACKAGE_PIN AM20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[1]}]
set_property -dict { PACKAGE_PIN AP19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[2]}]
set_property -dict { PACKAGE_PIN AP20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[2]}]
set_property -dict { PACKAGE_PIN AR18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[3]}]
set_property -dict { PACKAGE_PIN AP18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[3]}]
set_property -dict { PACKAGE_PIN AT22 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[14]}]
set_property -dict { PACKAGE_PIN AR22 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[14]}]
set_property -dict { PACKAGE_PIN AT19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[15]}]
set_property -dict { PACKAGE_PIN AR19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[15]}]
set_property -dict { PACKAGE_PIN AT21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[16]}]
set_property -dict { PACKAGE_PIN AR21 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[16]}]
set_property -dict { PACKAGE_PIN AT17 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[17]}]
set_property -dict { PACKAGE_PIN AR17 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[17]}]
set_property -dict { PACKAGE_PIN AU19 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[18]}]
set_property -dict { PACKAGE_PIN AU20 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[18]}]
set_property -dict { PACKAGE_PIN AV18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsN[19]}]
set_property -dict { PACKAGE_PIN AU18 IOSTANDARD LVCMOS18 } [get_ports {rtmLsP[19]}]

set_property -dict { PACKAGE_PIN AV17 IOSTANDARD LVCMOS18 } [get_ports {xBarSin[0]}];  # TIME_MUX_CFG0
set_property -dict { PACKAGE_PIN AU17 IOSTANDARD LVCMOS18 } [get_ports {xBarSin[1]}];  # TIME_MUX_CFG1
set_property -dict { PACKAGE_PIN AW20 IOSTANDARD LVCMOS18 } [get_ports {xBarSout[0]}]; # TIME_MUX_CFG2
set_property -dict { PACKAGE_PIN AV20 IOSTANDARD LVCMOS18 } [get_ports {xBarSout[1]}]; # TIME_MUX_CFG3
set_property -dict { PACKAGE_PIN AW18 IOSTANDARD LVCMOS18 } [get_ports {xBarConfig}];  # TIME_MUX_CFG4
set_property -dict { PACKAGE_PIN AW19 IOSTANDARD LVCMOS18 } [get_ports {xBarLoad}];    # TIME_MUX_CFG5

##############################################################################

set_property -dict { PACKAGE_PIN AT7 IOSTANDARD LVCMOS33 }           [get_ports {mpsClkIn}]  ; # BP_CLK1_IN
set_property -dict { PACKAGE_PIN AP6 IOSTANDARD LVCMOS33 SLEW FAST } [get_ports {mpsClkOut}] ; # BP_CLK1_OUT

set_property -dict { PACKAGE_PIN AV8 IOSTANDARD LVCMOS33 } [get_ports {ledGreen}]
set_property -dict { PACKAGE_PIN AU8 IOSTANDARD LVCMOS33 } [get_ports {ledRed}]

set_property -dict { PACKAGE_PIN AV5 IOSTANDARD LVCMOS33 } [get_ports {ipmcScl}]
set_property -dict { PACKAGE_PIN AV6 IOSTANDARD LVCMOS33 } [get_ports {ipmcSda}]

set_property -dict { PACKAGE_PIN AU1 IOSTANDARD LVCMOS33 } [get_ports {muxRstL}]
set_property -dict { PACKAGE_PIN AU2 IOSTANDARD LVCMOS33 } [get_ports {muxScl}]
set_property -dict { PACKAGE_PIN AV2 IOSTANDARD LVCMOS33 } [get_ports {muxSda}]

set_property -dict { PACKAGE_PIN AV3 IOSTANDARD LVCMOS33 } [get_ports {lmkSync}]
set_property -dict { PACKAGE_PIN AW3 IOSTANDARD LVCMOS33 } [get_ports {lmkCsL}]
set_property -dict { PACKAGE_PIN AW4 IOSTANDARD LVCMOS33 } [get_ports {lmkSck}]
set_property -dict { PACKAGE_PIN AW5 IOSTANDARD LVCMOS33 } [get_ports {lmkSdi}]; # lmkSdio
set_property -dict { PACKAGE_PIN AW6 IOSTANDARD LVCMOS33 } [get_ports {lmkSdo}]; # lmkRst(GPO)

set_property -dict { PACKAGE_PIN A9  IOSTANDARD LVCMOS33 } [get_ports {timingClkSel}]

set_property -dict { PACKAGE_PIN A10 IOSTANDARD LVCMOS33 } [get_ports {lmxCsL[0]}]
set_property -dict { PACKAGE_PIN A6  IOSTANDARD LVCMOS33 } [get_ports {lmxSck[0]}]
set_property -dict { PACKAGE_PIN A7  IOSTANDARD LVCMOS33 } [get_ports {lmxSdi[0]}]
set_property -dict { PACKAGE_PIN A5  IOSTANDARD LVCMOS33 } [get_ports {lmxSdo[0]}]

set_property -dict { PACKAGE_PIN B5 IOSTANDARD LVCMOS33 } [get_ports {lmxCsL[1]}]
set_property -dict { PACKAGE_PIN C5 IOSTANDARD LVCMOS33 } [get_ports {lmxSck[1]}]
set_property -dict { PACKAGE_PIN C6 IOSTANDARD LVCMOS33 } [get_ports {lmxSdi[1]}]
set_property -dict { PACKAGE_PIN B9 IOSTANDARD LVCMOS33 } [get_ports {lmxSdo[1]}]

set_property -dict { PACKAGE_PIN D8 IOSTANDARD LVCMOS33 } [get_ports {rtmSpare[0]}]
set_property -dict { PACKAGE_PIN D9 IOSTANDARD LVCMOS33 } [get_ports {rtmSpare[1]}]
set_property -dict { PACKAGE_PIN C7 IOSTANDARD LVCMOS33 } [get_ports {rtmSpare[2]}]
set_property -dict { PACKAGE_PIN C8 IOSTANDARD LVCMOS33 } [get_ports {rtmSpare[3]}]

set_property -dict { PACKAGE_PIN C10 IOSTANDARD LVCMOS33 } [get_ports {plDone}]

set_property -dict { PACKAGE_PIN D10 IOSTANDARD LVCMOS33 } [get_ports {pwrSync[0]}]
set_property -dict { PACKAGE_PIN D6  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[1]}]
set_property -dict { PACKAGE_PIN E7  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[2]}]
set_property -dict { PACKAGE_PIN E8  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[3]}]
set_property -dict { PACKAGE_PIN E9  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[4]}]
set_property -dict { PACKAGE_PIN E6  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[5]}]
set_property -dict { PACKAGE_PIN F6  IOSTANDARD LVCMOS33 } [get_ports {pwrSync[6]}]

##############################################################################

set_property PACKAGE_PIN P35  [get_ports {timingTxP}]
set_property PACKAGE_PIN P36  [get_ports {timingTxN}]
set_property PACKAGE_PIN N38  [get_ports {timingRxP}]
set_property PACKAGE_PIN N39  [get_ports {timingRxN}]

set_property PACKAGE_PIN W33  [get_ports {timingRefClkInP}]
set_property PACKAGE_PIN W34  [get_ports {timingRefClkInN}]

######################################################################
# Commented out because it could be defined in RfmcCarrierCoreZone3Eth
######################################################################

# set_property PACKAGE_PIN H31 [get_ports {ethTxP[0]}]
# set_property PACKAGE_PIN H32 [get_ports {ethTxN[0]}]
# set_property PACKAGE_PIN J38 [get_ports {ethRxP[0]}]
# set_property PACKAGE_PIN J39 [get_ports {ethRxN[0]}]
# set_property PACKAGE_PIN G33 [get_ports {ethTxP[1]}]
# set_property PACKAGE_PIN G34 [get_ports {ethTxN[1]}]
# set_property PACKAGE_PIN H36 [get_ports {ethRxP[1]}]
# set_property PACKAGE_PIN H37 [get_ports {ethRxN[1]}]
# set_property PACKAGE_PIN F31 [get_ports {ethTxP[2]}]
# set_property PACKAGE_PIN F32 [get_ports {ethTxN[2]}]
# set_property PACKAGE_PIN G38 [get_ports {ethRxP[2]}]
# set_property PACKAGE_PIN G39 [get_ports {ethRxN[2]}]
# set_property PACKAGE_PIN E33 [get_ports {ethTxP[3]}]
# set_property PACKAGE_PIN E34 [get_ports {ethTxN[3]}]
# set_property PACKAGE_PIN F36 [get_ports {ethRxP[3]}]
# set_property PACKAGE_PIN F37 [get_ports {ethRxN[3]}]

set_property PACKAGE_PIN U33  [get_ports {ethClkP}]
set_property PACKAGE_PIN U34  [get_ports {ethClkN}]
create_clock -name ethClkP  -period  6.4 [get_ports {ethClkP}]

######################################################################
# Commented out because it could be defined in RfmcCarrierCoreZone3Eth
######################################################################

# set_property PACKAGE_PIN D31 [get_ports {rtmHsTxP[0]}]
# set_property PACKAGE_PIN D32 [get_ports {rtmHsTxN[0]}]
# set_property PACKAGE_PIN E38 [get_ports {rtmHsRxP[0]}]
# set_property PACKAGE_PIN E39 [get_ports {rtmHsRxN[0]}]
# set_property PACKAGE_PIN C33 [get_ports {rtmHsTxP[1]}]
# set_property PACKAGE_PIN C34 [get_ports {rtmHsTxN[1]}]
# set_property PACKAGE_PIN D36 [get_ports {rtmHsRxP[1]}]
# set_property PACKAGE_PIN D37 [get_ports {rtmHsRxN[1]}]
# set_property PACKAGE_PIN B31 [get_ports {rtmHsTxP[2]}]
# set_property PACKAGE_PIN B32 [get_ports {rtmHsTxN[2]}]
# set_property PACKAGE_PIN C38 [get_ports {rtmHsRxP[2]}]
# set_property PACKAGE_PIN C39 [get_ports {rtmHsRxN[2]}]
# set_property PACKAGE_PIN A33 [get_ports {rtmHsTxP[3]}]
# set_property PACKAGE_PIN A34 [get_ports {rtmHsTxN[3]}]
# set_property PACKAGE_PIN B36 [get_ports {rtmHsRxP[3]}]
# set_property PACKAGE_PIN B37 [get_ports {rtmHsRxN[3]}]

#################################################################################
# Commented out because these RFDC pins constraints get set in the IP core (.XCI)
#################################################################################

# set_property PACKAGE_PIN AF5 [get_ports { adcClkP[0] }]; # ADC224_CLK_P
# set_property PACKAGE_PIN AF4 [get_ports { adcClkN[0] }]; # ADC224_CLK_N

# set_property PACKAGE_PIN AD5 [get_ports { adcClkP[1] }]; # ADC225_CLK_P
# set_property PACKAGE_PIN AD4 [get_ports { adcClkN[1] }]; # ADC225_CLK_N

# set_property PACKAGE_PIN AB5 [get_ports { adcClkP[2] }]; # ADC226_CLK_P
# set_property PACKAGE_PIN AB4 [get_ports { adcClkN[2] }]; # ADC226_CLK_N

# set_property PACKAGE_PIN Y5 [get_ports { adcClkP[3] }]; # ADC227_CLK_P
# set_property PACKAGE_PIN Y4 [get_ports { adcClkN[3] }]; # ADC227_CLK_N

# set_property PACKAGE_PIN R5 [get_ports { dacClkP[0] }]; # DAC228_CLK_P
# set_property PACKAGE_PIN R4 [get_ports { dacClkN[0] }]; # DAC228_CLK_N

# set_property PACKAGE_PIN N5 [get_ports { dacClkP[1] }]; # DAC230_CLK_P
# set_property PACKAGE_PIN N4 [get_ports { dacClkN[1] }]; # DAC230_CLK_N

# set_property PACKAGE_PIN U5 [get_ports { rfdcSysRefP }]
# set_property PACKAGE_PIN U4 [get_ports { rfdcSysRefN }]

# # ADC TILE 224
# set_property PACKAGE_PIN AP2 [get_ports { adcP[0] }]; # ADC224_T0_CH0_P
# set_property PACKAGE_PIN AP1 [get_ports { adcN[0] }]; # ADC224_T0_CH0_N
# set_property PACKAGE_PIN AM2 [get_ports { adcP[1] }]; # ADC224_T2_CH0_P
# set_property PACKAGE_PIN AM1 [get_ports { adcN[1] }]; # ADC224_T2_CH0_N

# # ADC TILE 225
# set_property PACKAGE_PIN AK2 [get_ports { adcP[2] }]; # ADC225_T1_CH0_P
# set_property PACKAGE_PIN AK1 [get_ports { adcN[2] }]; # ADC225_T1_CH0_N
# set_property PACKAGE_PIN AH2 [get_ports { adcP[3] }]; # ADC225_T1_CH2_P
# set_property PACKAGE_PIN AH1 [get_ports { adcN[3] }]; # ADC225_T1_CH2_N

# # ADC TILE 226
# set_property PACKAGE_PIN AF2 [get_ports { adcP[4] }]; # ADC226_T2_CH0_P
# set_property PACKAGE_PIN AF1 [get_ports { adcN[4] }]; # ADC226_T2_CH0_N
# set_property PACKAGE_PIN AD2 [get_ports { adcP[5] }]; # ADC226_T2_CH2_P
# set_property PACKAGE_PIN AD1 [get_ports { adcN[5] }]; # ADC226_T2_CH2_N

# # ADC TILE 227
# set_property PACKAGE_PIN AB2 [get_ports { adcP[6] }]; # ADC227_T3_CH0_P
# set_property PACKAGE_PIN AB1 [get_ports { adcN[6] }]; # ADC227_T3_CH0_N
# set_property PACKAGE_PIN Y2  [get_ports { adcP[7] }]; # ADC227_T3_CH2_P
# set_property PACKAGE_PIN Y1  [get_ports { adcN[7] }]; # ADC227_T3_CH2_N

# # DAC TILE 228
# set_property PACKAGE_PIN U2 [get_ports { dacP[0] }]; # DAC228_T0_CH0_P
# set_property PACKAGE_PIN U1 [get_ports { dacN[0] }]; # DAC228_T0_CH0_N
# set_property PACKAGE_PIN R2 [get_ports { dacP[1] }]; # DAC228_T0_CH2_P
# set_property PACKAGE_PIN R1 [get_ports { dacN[1] }]; # DAC228_T0_CH2_N

# # DAC TILE 229
# set_property PACKAGE_PIN N2 [get_ports { dacP[2] }]; # DAC229_T1_CH0_P
# set_property PACKAGE_PIN N1 [get_ports { dacN[2] }]; # DAC229_T1_CH0_N
# set_property PACKAGE_PIN L2 [get_ports { dacP[3] }]; # DAC229_T1_CH2_P
# set_property PACKAGE_PIN L1 [get_ports { dacN[3] }]; # DAC229_T1_CH2_N

# # DAC TILE 230
# set_property PACKAGE_PIN J2 [get_ports { dacP[4] }]; # DAC230_T2_CH0_P
# set_property PACKAGE_PIN J1 [get_ports { dacN[4] }]; # DAC230_T2_CH0_N
# set_property PACKAGE_PIN G2 [get_ports { dacP[5] }]; # DAC230_T2_CH2_P
# set_property PACKAGE_PIN G1 [get_ports { dacN[5] }]; # DAC230_T2_CH2_N

# # DAC TILE 231
# set_property PACKAGE_PIN E2 [get_ports { dacP[6] }]; # DAC231_T3_CH0_P
# set_property PACKAGE_PIN E1 [get_ports { dacN[6] }]; # DAC231_T3_CH0_N
# set_property PACKAGE_PIN C2 [get_ports { dacP[7] }]; # DAC231_T3_CH2_P
# set_property PACKAGE_PIN C1 [get_ports { dacN[7] }]; # DAC231_T3_CH2_N

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]]
set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]
set_clock_groups -asynchronous -group [get_clocks -include_generated_clocks {ddrClkP}] -group [get_clocks -include_generated_clocks {plClkP}]

##############################################################################
