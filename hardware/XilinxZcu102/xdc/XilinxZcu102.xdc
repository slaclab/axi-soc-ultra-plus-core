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

# SYSMON Ports

set_property -dict { PACKAGE_PIN U18 IOSTANDARD ANALOG } [get_ports { vPIn }] ;# Other net   PACKAGE_PIN U18      - SYSMON_VP_R               Bank   0 - VP
set_property -dict { PACKAGE_PIN V17 IOSTANDARD ANALOG } [get_ports { vNIn }] ;# Other net   PACKAGE_PIN V17      - SYSMON_VN_R               Bank   0 - VN

set_property -dict { PACKAGE_PIN B14 IOSTANDARD LVCMOS33 } [get_ports { sysMonSda }] ;# [get_ports "SYSMON_SDA"] ;# Bank  49 VCCO - VCC3V3   - IO_L7N_HDGC_49
set_property -dict { PACKAGE_PIN C14 IOSTANDARD LVCMOS33 } [get_ports { sysMonScl }] ;# [get_ports "SYSMON_SCL"] ;# Bank  49 VCCO - VCC3V3   - IO_L7P_HDGC_49

##############################################################################

# PL I2C Ports

set_property -dict { PACKAGE_PIN J10 IOSTANDARD LVCMOS33 } [get_ports { plScl[0] }] ;# [get_ports "PL_I2C0_SCL_LS"] ;# Bank  50 VCCO - VCC3V3   - IO_L1N_AD15N_50
set_property -dict { PACKAGE_PIN J11 IOSTANDARD LVCMOS33 } [get_ports { plSda[0] }] ;# [get_ports "PL_I2C0_SDA_LS"] ;# Bank  50 VCCO - VCC3V3   - IO_L1P_AD15P_50

set_property -dict { PACKAGE_PIN K20 IOSTANDARD LVCMOS33 } [get_ports { plScl[1] }] ;# [get_ports "PL_I2C1_SCL_LS"] ;# Bank  47 VCCO - VCC3V3   - IO_L1N_AD11N_47
set_property -dict { PACKAGE_PIN L20 IOSTANDARD LVCMOS33 } [get_ports { plSda[1] }] ;# [get_ports "PL_I2C1_SDA_LS"] ;# Bank  47 VCCO - VCC3V3   - IO_L1P_AD11P_47

##############################################################################

# Prototype Header Ports

set_property PACKAGE_PIN H14 [get_ports { dbgHdrN[4] }] ;# [get_ports "L10N_AD10N_50_N"] ;# Bank  50 VCCO - VCC3V3   - IO_L10N_AD10N_50
set_property PACKAGE_PIN J14 [get_ports { dbgHdrP[4] }] ;# [get_ports "L10P_AD10P_50_P"] ;# Bank  50 VCCO - VCC3V3   - IO_L10P_AD10P_50
set_property PACKAGE_PIN G14 [get_ports { dbgHdrN[3] }] ;# [get_ports "L9N_AD11N_50_N"]  ;# Bank  50 VCCO - VCC3V3   - IO_L9N_AD11N_50
set_property PACKAGE_PIN G15 [get_ports { dbgHdrP[3] }] ;# [get_ports "L9P_AD11P_50_P"]  ;# Bank  50 VCCO - VCC3V3   - IO_L9P_AD11P_50
set_property PACKAGE_PIN J15 [get_ports { dbgHdrN[2] }] ;# [get_ports "L12N_AD8N_50_N"]  ;# Bank  50 VCCO - VCC3V3   - IO_L12N_AD8N_50
set_property PACKAGE_PIN J16 [get_ports { dbgHdrP[2] }] ;# [get_ports "L12P_AD8P_50_P"]  ;# Bank  50 VCCO - VCC3V3   - IO_L12P_AD8P_50
set_property PACKAGE_PIN G16 [get_ports { dbgHdrN[1] }] ;# [get_ports "L11N_AD9N_50_N"]  ;# Bank  50 VCCO - VCC3V3   - IO_L11N_AD9N_50
set_property PACKAGE_PIN H16 [get_ports { dbgHdrP[1] }] ;# [get_ports "L11P_AD9P_50_P"]  ;# Bank  50 VCCO - VCC3V3   - IO_L11P_AD9P_50
set_property PACKAGE_PIN G13 [get_ports { dbgHdrN[0] }] ;# [get_ports "L8N_HDGC_50_N"]   ;# Bank  50 VCCO - VCC3V3   - IO_L8N_HDGC_50
set_property PACKAGE_PIN H13 [get_ports { dbgHdrP[0] }] ;# [get_ports "L8P_HDGC_50_P"]   ;# Bank  50 VCCO - VCC3V3   - IO_L8P_HDGC_50

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { dbgHdrP[*] }]
set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { dbgHdrN[*] }]

##############################################################################

# PMOD[0]

set_property PACKAGE_PIN A20 [get_ports { pmod[0][0] }] ;# [get_ports "PMOD0_0"] ;# Bank  47 VCCO - VCC3V3   - IO_L12N_AD0N_47
set_property PACKAGE_PIN B20 [get_ports { pmod[0][1] }] ;# [get_ports "PMOD0_1"] ;# Bank  47 VCCO - VCC3V3   - IO_L12P_AD0P_47
set_property PACKAGE_PIN A22 [get_ports { pmod[0][2] }] ;# [get_ports "PMOD0_2"] ;# Bank  47 VCCO - VCC3V3   - IO_L11N_AD1N_47
set_property PACKAGE_PIN A21 [get_ports { pmod[0][3] }] ;# [get_ports "PMOD0_3"] ;# Bank  47 VCCO - VCC3V3   - IO_L11P_AD1P_47
set_property PACKAGE_PIN B21 [get_ports { pmod[0][4] }] ;# [get_ports "PMOD0_4"] ;# Bank  47 VCCO - VCC3V3   - IO_L10N_AD2N_47
set_property PACKAGE_PIN C21 [get_ports { pmod[0][5] }] ;# [get_ports "PMOD0_5"] ;# Bank  47 VCCO - VCC3V3   - IO_L10P_AD2P_47
set_property PACKAGE_PIN C22 [get_ports { pmod[0][6] }] ;# [get_ports "PMOD0_6"] ;# Bank  47 VCCO - VCC3V3   - IO_L9N_AD3N_47
set_property PACKAGE_PIN D21 [get_ports { pmod[0][7] }] ;# [get_ports "PMOD0_7"] ;# Bank  47 VCCO - VCC3V3   - IO_L9P_AD3P_47

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { pmod[0][*] }]

##############################################################################

# PMOD[1]

set_property PACKAGE_PIN D20 [get_ports { pmod[1][0] }] ;# [get_ports "PMOD1_0"] ;# Bank  47 VCCO - VCC3V3   - IO_L8N_HDGC_AD4N_47
set_property PACKAGE_PIN E20 [get_ports { pmod[1][1] }] ;# [get_ports "PMOD1_1"] ;# Bank  47 VCCO - VCC3V3   - IO_L8P_HDGC_AD4P_47
set_property PACKAGE_PIN D22 [get_ports { pmod[1][2] }] ;# [get_ports "PMOD1_2"] ;# Bank  47 VCCO - VCC3V3   - IO_L7N_HDGC_AD5N_47
set_property PACKAGE_PIN E22 [get_ports { pmod[1][3] }] ;# [get_ports "PMOD1_3"] ;# Bank  47 VCCO - VCC3V3   - IO_L7P_HDGC_AD5P_47
set_property PACKAGE_PIN F20 [get_ports { pmod[1][4] }] ;# [get_ports "PMOD1_4"] ;# Bank  47 VCCO - VCC3V3   - IO_L6N_HDGC_AD6N_47
set_property PACKAGE_PIN G20 [get_ports { pmod[1][5] }] ;# [get_ports "PMOD1_5"] ;# Bank  47 VCCO - VCC3V3   - IO_L6P_HDGC_AD6P_47
set_property PACKAGE_PIN J20 [get_ports { pmod[1][6] }] ;# [get_ports "PMOD1_6"] ;# Bank  47 VCCO - VCC3V3   - IO_L4N_AD8N_47
set_property PACKAGE_PIN J19 [get_ports { pmod[1][7] }] ;# [get_ports "PMOD1_7"] ;# Bank  47 VCCO - VCC3V3   - IO_L4P_AD8P_47

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { pmod[1][*] }]

##############################################################################

# GPIO Switches/Buttons and LEDS

set_property -dict { PACKAGE_PIN AM13 IOSTANDARD LVCMOS33 } [get_ports { extRst }] ;# [get_ports "CPU_RESET"] ;# Bank  44 VCCO - VCC3V3   - IO_L4N_AD8N_44

set_property -dict { PACKAGE_PIN AE14 IOSTANDARD LVCMOS33 } [get_ports { gpioSwE }] ;# [get_ports "GPIO_SW_E"] ;# Bank  44 VCCO - VCC3V3   - IO_L12N_AD0N_44
set_property -dict { PACKAGE_PIN AE15 IOSTANDARD LVCMOS33 } [get_ports { gpioSwS }] ;# [get_ports "GPIO_SW_S"] ;# Bank  44 VCCO - VCC3V3   - IO_L12P_AD0P_44
set_property -dict { PACKAGE_PIN AG15 IOSTANDARD LVCMOS33 } [get_ports { gpioSwN }] ;# [get_ports "GPIO_SW_N"] ;# Bank  44 VCCO - VCC3V3   - IO_L11N_AD1N_44
set_property -dict { PACKAGE_PIN AF15 IOSTANDARD LVCMOS33 } [get_ports { gpioSwW }] ;# [get_ports "GPIO_SW_W"] ;# Bank  44 VCCO - VCC3V3   - IO_L11P_AD1P_44
set_property -dict { PACKAGE_PIN AG13 IOSTANDARD LVCMOS33 } [get_ports { gpioSwC }] ;# [get_ports "GPIO_SW_C"] ;# Bank  44 VCCO - VCC3V3   - IO_L10N_AD2N_44

set_property PACKAGE_PIN AG14 [get_ports { gpioLed[0] }] ;# [get_ports "GPIO_LED_0"] ;# Bank  44 VCCO - VCC3V3   - IO_L10P_AD2P_44
set_property PACKAGE_PIN AF13 [get_ports { gpioLed[1] }] ;# [get_ports "GPIO_LED_1"] ;# Bank  44 VCCO - VCC3V3   - IO_L9N_AD3N_44
set_property PACKAGE_PIN AE13 [get_ports { gpioLed[2] }] ;# [get_ports "GPIO_LED_2"] ;# Bank  44 VCCO - VCC3V3   - IO_L9P_AD3P_44
set_property PACKAGE_PIN AJ14 [get_ports { gpioLed[3] }] ;# [get_ports "GPIO_LED_3"] ;# Bank  44 VCCO - VCC3V3   - IO_L8N_HDGC_AD4N_44
set_property PACKAGE_PIN AJ15 [get_ports { gpioLed[4] }] ;# [get_ports "GPIO_LED_4"] ;# Bank  44 VCCO - VCC3V3   - IO_L8P_HDGC_AD4P_44
set_property PACKAGE_PIN AH13 [get_ports { gpioLed[5] }] ;# [get_ports "GPIO_LED_5"] ;# Bank  44 VCCO - VCC3V3   - IO_L7N_HDGC_AD5N_44
set_property PACKAGE_PIN AH14 [get_ports { gpioLed[6] }] ;# [get_ports "GPIO_LED_6"] ;# Bank  44 VCCO - VCC3V3   - IO_L7P_HDGC_AD5P_44
set_property PACKAGE_PIN AL12 [get_ports { gpioLed[7] }] ;# [get_ports "GPIO_LED_7"] ;# Bank  44 VCCO - VCC3V3   - IO_L6N_HDGC_AD6N_44

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { gpioLed[*] }]

set_property PACKAGE_PIN AK13 [get_ports { gpioSw[7] }] ;# [get_ports "GPIO_DIP_SW7"] ;# Bank  44 VCCO - VCC3V3   - IO_L6P_HDGC_AD6P_44
set_property PACKAGE_PIN AL13 [get_ports { gpioSw[6] }] ;# [get_ports "GPIO_DIP_SW6"] ;# Bank  44 VCCO - VCC3V3   - IO_L4P_AD8P_44
set_property PACKAGE_PIN AP12 [get_ports { gpioSw[5] }] ;# [get_ports "GPIO_DIP_SW5"] ;# Bank  44 VCCO - VCC3V3   - IO_L3N_AD9N_44
set_property PACKAGE_PIN AN12 [get_ports { gpioSw[4] }] ;# [get_ports "GPIO_DIP_SW4"] ;# Bank  44 VCCO - VCC3V3   - IO_L3P_AD9P_44
set_property PACKAGE_PIN AN13 [get_ports { gpioSw[3] }] ;# [get_ports "GPIO_DIP_SW3"] ;# Bank  44 VCCO - VCC3V3   - IO_L2N_AD10N_44
set_property PACKAGE_PIN AM14 [get_ports { gpioSw[2] }] ;# [get_ports "GPIO_DIP_SW2"] ;# Bank  44 VCCO - VCC3V3   - IO_L2P_AD10P_44
set_property PACKAGE_PIN AP14 [get_ports { gpioSw[1] }] ;# [get_ports "GPIO_DIP_SW1"] ;# Bank  44 VCCO - VCC3V3   - IO_L1N_AD11N_44
set_property PACKAGE_PIN AN14 [get_ports { gpioSw[0] }] ;# [get_ports "GPIO_DIP_SW0"] ;# Bank  44 VCCO - VCC3V3   - IO_L1P_AD11P_44

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { gpioSw[*] }]

##############################################################################

# FMC[0] - Low Speeds

set_property PACKAGE_PIN Y3 [get_ports { fmcHpc0N[0] }] ;# [get_ports "FMC_HPC0_LA00_CC_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L13N_T2L_N1_GC_QBC_66
set_property PACKAGE_PIN Y4 [get_ports { fmcHpc0P[0] }] ;# [get_ports "FMC_HPC0_LA00_CC_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L13P_T2L_N0_GC_QBC_66

set_property PACKAGE_PIN AC4 [get_ports { fmcHpc0N[1] }] ;# [get_ports "FMC_HPC0_LA01_CC_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L16N_T2U_N7_QBC_AD3N_66
set_property PACKAGE_PIN AB4 [get_ports { fmcHpc0P[1] }] ;# [get_ports "FMC_HPC0_LA01_CC_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L16P_T2U_N6_QBC_AD3P_66

set_property PACKAGE_PIN V1 [get_ports { fmcHpc0N[2] }] ;# [get_ports "FMC_HPC0_LA02_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L23N_T3U_N9_66
set_property PACKAGE_PIN V2 [get_ports { fmcHpc0P[2] }] ;# [get_ports "FMC_HPC0_LA02_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L23P_T3U_N8_66

set_property PACKAGE_PIN Y1 [get_ports { fmcHpc0N[3] }] ;# [get_ports "FMC_HPC0_LA03_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L22N_T3U_N7_DBC_AD0N_66
set_property PACKAGE_PIN Y2 [get_ports { fmcHpc0P[3] }] ;# [get_ports "FMC_HPC0_LA03_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L22P_T3U_N6_DBC_AD0P_66

set_property PACKAGE_PIN AA1 [get_ports { fmcHpc0N[4] }] ;# [get_ports "FMC_HPC0_LA04_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L21N_T3L_N5_AD8N_66
set_property PACKAGE_PIN AA2 [get_ports { fmcHpc0P[4] }] ;# [get_ports "FMC_HPC0_LA04_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L21P_T3L_N4_AD8P_66

set_property PACKAGE_PIN AC3 [get_ports { fmcHpc0N[5] }] ;# [get_ports "FMC_HPC0_LA05_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L20N_T3L_N3_AD1N_66
set_property PACKAGE_PIN AB3 [get_ports { fmcHpc0P[5] }] ;# [get_ports "FMC_HPC0_LA05_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L20P_T3L_N2_AD1P_66

set_property PACKAGE_PIN AC1 [get_ports { fmcHpc0N[6] }] ;# [get_ports "FMC_HPC0_LA06_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L19N_T3L_N1_DBC_AD9N_66
set_property PACKAGE_PIN AC2 [get_ports { fmcHpc0P[6] }] ;# [get_ports "FMC_HPC0_LA06_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L19P_T3L_N0_DBC_AD9P_66

set_property PACKAGE_PIN U4 [get_ports { fmcHpc0N[7] }] ;# [get_ports "FMC_HPC0_LA07_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L18N_T2U_N11_AD2N_66
set_property PACKAGE_PIN U5 [get_ports { fmcHpc0P[7] }] ;# [get_ports "FMC_HPC0_LA07_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L18P_T2U_N10_AD2P_66

set_property PACKAGE_PIN V3 [get_ports { fmcHpc0N[8] }] ;# [get_ports "FMC_HPC0_LA08_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L17N_T2U_N9_AD10N_66
set_property PACKAGE_PIN V4 [get_ports { fmcHpc0P[8] }] ;# [get_ports "FMC_HPC0_LA08_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L17P_T2U_N8_AD10P_66

set_property PACKAGE_PIN W1 [get_ports { fmcHpc0N[9] }] ;# [get_ports "FMC_HPC0_LA09_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L24N_T3U_N11_66
set_property PACKAGE_PIN W2 [get_ports { fmcHpc0P[9] }] ;# [get_ports "FMC_HPC0_LA09_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L24P_T3U_N10_66

set_property PACKAGE_PIN W4 [get_ports { fmcHpc0N[10] }] ;# [get_ports "FMC_HPC0_LA10_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L15N_T2L_N5_AD11N_66
set_property PACKAGE_PIN W5 [get_ports { fmcHpc0P[10] }] ;# [get_ports "FMC_HPC0_LA10_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L15P_T2L_N4_AD11P_66

set_property PACKAGE_PIN AB5 [get_ports { fmcHpc0N[11] }] ;# [get_ports "FMC_HPC0_LA11_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L10N_T1U_N7_QBC_AD4N_66
set_property PACKAGE_PIN AB6 [get_ports { fmcHpc0P[11] }] ;# [get_ports "FMC_HPC0_LA11_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L10P_T1U_N6_QBC_AD4P_66

set_property PACKAGE_PIN W6 [get_ports { fmcHpc0N[12] }] ;# [get_ports "FMC_HPC0_LA12_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L9N_T1L_N5_AD12N_66
set_property PACKAGE_PIN W7 [get_ports { fmcHpc0P[12] }] ;# [get_ports "FMC_HPC0_LA12_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L9P_T1L_N4_AD12P_66

set_property PACKAGE_PIN AC8 [get_ports { fmcHpc0N[13] }] ;# [get_ports "FMC_HPC0_LA13_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L8N_T1L_N3_AD5N_66
set_property PACKAGE_PIN AB8 [get_ports { fmcHpc0P[13] }] ;# [get_ports "FMC_HPC0_LA13_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L8P_T1L_N2_AD5P_66

set_property PACKAGE_PIN AC6 [get_ports { fmcHpc0N[14] }] ;# [get_ports "FMC_HPC0_LA14_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L7N_T1L_N1_QBC_AD13N_66
set_property PACKAGE_PIN AC7 [get_ports { fmcHpc0P[14] }] ;# [get_ports "FMC_HPC0_LA14_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L7P_T1L_N0_QBC_AD13P_66

set_property PACKAGE_PIN Y9  [get_ports { fmcHpc0N[15] }] ;# [get_ports "FMC_HPC0_LA15_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L6N_T0U_N11_AD6N_66
set_property PACKAGE_PIN Y10 [get_ports { fmcHpc0P[15] }] ;# [get_ports "FMC_HPC0_LA15_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L6P_T0U_N10_AD6P_66

set_property PACKAGE_PIN AA12 [get_ports { fmcHpc0N[16] }] ;# [get_ports "FMC_HPC0_LA16_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L5N_T0U_N9_AD14N_66
set_property PACKAGE_PIN Y12  [get_ports { fmcHpc0P[16] }] ;# [get_ports "FMC_HPC0_LA16_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L5P_T0U_N8_AD14P_66

set_property PACKAGE_PIN N11 [get_ports { fmcHpc0N[17] }] ;# [get_ports "FMC_HPC0_LA17_CC_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L13N_T2L_N1_GC_QBC_67
set_property PACKAGE_PIN P11 [get_ports { fmcHpc0P[17] }] ;# [get_ports "FMC_HPC0_LA17_CC_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L13P_T2L_N0_GC_QBC_67

set_property PACKAGE_PIN N8 [get_ports { fmcHpc0N[18] }] ;# [get_ports "FMC_HPC0_LA18_CC_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L16N_T2U_N7_QBC_AD3N_67
set_property PACKAGE_PIN N9 [get_ports { fmcHpc0P[18] }] ;# [get_ports "FMC_HPC0_LA18_CC_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L16P_T2U_N6_QBC_AD3P_67

set_property PACKAGE_PIN K13 [get_ports { fmcHpc0N[19] }] ;# [get_ports "FMC_HPC0_LA19_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L23N_T3U_N9_67
set_property PACKAGE_PIN L13 [get_ports { fmcHpc0P[19] }] ;# [get_ports "FMC_HPC0_LA19_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L23P_T3U_N8_67

set_property PACKAGE_PIN M13 [get_ports { fmcHpc0N[20] }] ;# [get_ports "FMC_HPC0_LA20_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L22N_T3U_N7_DBC_AD0N_67
set_property PACKAGE_PIN N13 [get_ports { fmcHpc0P[20] }] ;# [get_ports "FMC_HPC0_LA20_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L22P_T3U_N6_DBC_AD0P_67

set_property PACKAGE_PIN N12 [get_ports { fmcHpc0N[21] }] ;# [get_ports "FMC_HPC0_LA21_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L21N_T3L_N5_AD8N_67
set_property PACKAGE_PIN P12 [get_ports { fmcHpc0P[21] }] ;# [get_ports "FMC_HPC0_LA21_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L21P_T3L_N4_AD8P_67

set_property PACKAGE_PIN M14 [get_ports { fmcHpc0N[22] }] ;# [get_ports "FMC_HPC0_LA22_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L20N_T3L_N3_AD1N_67
set_property PACKAGE_PIN M15 [get_ports { fmcHpc0P[22] }] ;# [get_ports "FMC_HPC0_LA22_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L20P_T3L_N2_AD1P_67

set_property PACKAGE_PIN K16 [get_ports { fmcHpc0N[23] }] ;# [get_ports "FMC_HPC0_LA23_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L19N_T3L_N1_DBC_AD9N_67
set_property PACKAGE_PIN L16 [get_ports { fmcHpc0P[23] }] ;# [get_ports "FMC_HPC0_LA23_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L19P_T3L_N0_DBC_AD9P_67

set_property PACKAGE_PIN K12 [get_ports { fmcHpc0N[24] }] ;# [get_ports "FMC_HPC0_LA24_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L18N_T2U_N11_AD2N_67
set_property PACKAGE_PIN L12 [get_ports { fmcHpc0P[24] }] ;# [get_ports "FMC_HPC0_LA24_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L18P_T2U_N10_AD2P_67

set_property PACKAGE_PIN L11 [get_ports { fmcHpc0N[25] }] ;# [get_ports "FMC_HPC0_LA25_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L17N_T2U_N9_AD10N_67
set_property PACKAGE_PIN M11 [get_ports { fmcHpc0P[25] }] ;# [get_ports "FMC_HPC0_LA25_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L17P_T2U_N8_AD10P_67

set_property PACKAGE_PIN K15 [get_ports { fmcHpc0N[26] }] ;# [get_ports "FMC_HPC0_LA26_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L24N_T3U_N11_67
set_property PACKAGE_PIN L15 [get_ports { fmcHpc0P[26] }] ;# [get_ports "FMC_HPC0_LA26_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L24P_T3U_N10_67

set_property PACKAGE_PIN L10 [get_ports { fmcHpc0N[27] }] ;# [get_ports "FMC_HPC0_LA27_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L15N_T2L_N5_AD11N_67
set_property PACKAGE_PIN M10 [get_ports { fmcHpc0P[27] }] ;# [get_ports "FMC_HPC0_LA27_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L15P_T2L_N4_AD11P_67

set_property PACKAGE_PIN T6 [get_ports { fmcHpc0N[28] }] ;# [get_ports "FMC_HPC0_LA28_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L10N_T1U_N7_QBC_AD4N_67
set_property PACKAGE_PIN T7 [get_ports { fmcHpc0P[28] }] ;# [get_ports "FMC_HPC0_LA28_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L10P_T1U_N6_QBC_AD4P_67

set_property PACKAGE_PIN U8 [get_ports { fmcHpc0N[29] }] ;# [get_ports "FMC_HPC0_LA29_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L9N_T1L_N5_AD12N_67
set_property PACKAGE_PIN U9 [get_ports { fmcHpc0P[29] }] ;# [get_ports "FMC_HPC0_LA29_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L9P_T1L_N4_AD12P_67

set_property PACKAGE_PIN U6 [get_ports { fmcHpc0N[30] }] ;# [get_ports "FMC_HPC0_LA30_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L8N_T1L_N3_AD5N_67
set_property PACKAGE_PIN V6 [get_ports { fmcHpc0P[30] }] ;# [get_ports "FMC_HPC0_LA30_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L8P_T1L_N2_AD5P_67

set_property PACKAGE_PIN V7 [get_ports { fmcHpc0N[31] }] ;# [get_ports "FMC_HPC0_LA31_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L7N_T1L_N1_QBC_AD13N_67
set_property PACKAGE_PIN V8 [get_ports { fmcHpc0P[31] }] ;# [get_ports "FMC_HPC0_LA31_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L7P_T1L_N0_QBC_AD13P_67

set_property PACKAGE_PIN T11 [get_ports { fmcHpc0N[32] }] ;# [get_ports "FMC_HPC0_LA32_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L6N_T0U_N11_AD6N_67
set_property PACKAGE_PIN U11 [get_ports { fmcHpc0P[32] }] ;# [get_ports "FMC_HPC0_LA32_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L6P_T0U_N10_AD6P_67

set_property PACKAGE_PIN V11 [get_ports { fmcHpc0N[33] }] ;# [get_ports "FMC_HPC0_LA33_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L5N_T0U_N9_AD14N_67
set_property PACKAGE_PIN V12 [get_ports { fmcHpc0P[33] }] ;# [get_ports "FMC_HPC0_LA33_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L5P_T0U_N8_AD14P_67

set_property -dict { IOSTANDARD LVCMOS18 } [get_ports { fmcHpc0N[*] }]
set_property -dict { IOSTANDARD LVCMOS18 } [get_ports { fmcHpc0P[*] }]

set_property -dict { PACKAGE_PIN AA6 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc0ClkN[0] }] ;# [get_ports "FMC_HPC0_CLK0_M2C_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L12N_T1U_N11_GC_66
set_property -dict { PACKAGE_PIN AA7 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc0ClkP[0] }] ;# [get_ports "FMC_HPC0_CLK0_M2C_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L12P_T1U_N10_GC_66

set_property -dict { PACKAGE_PIN R8 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc0ClkN[1] }] ;# [get_ports "FMC_HPC0_CLK1_M2C_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L12N_T1U_N11_GC_67
set_property -dict { PACKAGE_PIN T8 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc0ClkP[1] }] ;# [get_ports "FMC_HPC0_CLK1_M2C_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L12P_T1U_N10_GC_67

##############################################################################

# FMC[1] - Low Speeds

set_property PACKAGE_PIN AF5 [get_ports { fmcHpc1N[0] }] ;# [get_ports "FMC_HPC1_LA00_CC_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L13N_T2L_N1_GC_QBC_65
set_property PACKAGE_PIN AE5 [get_ports { fmcHpc1P[0] }] ;# [get_ports "FMC_HPC1_LA00_CC_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L13P_T2L_N0_GC_QBC_65

set_property PACKAGE_PIN AJ5 [get_ports { fmcHpc1N[1] }] ;# [get_ports "FMC_HPC1_LA01_CC_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L16N_T2U_N7_QBC_AD3N_65
set_property PACKAGE_PIN AJ6 [get_ports { fmcHpc1P[1] }] ;# [get_ports "FMC_HPC1_LA01_CC_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L16P_T2U_N6_QBC_AD3P_65

set_property PACKAGE_PIN AD1 [get_ports { fmcHpc1N[2] }] ;# [get_ports "FMC_HPC1_LA02_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L23N_T3U_N9_65
set_property PACKAGE_PIN AD2 [get_ports { fmcHpc1P[2] }] ;# [get_ports "FMC_HPC1_LA02_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L23P_T3U_N8_I2C_SCLK_65

set_property PACKAGE_PIN AJ1 [get_ports { fmcHpc1N[3] }] ;# [get_ports "FMC_HPC1_LA03_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L22N_T3U_N7_DBC_AD0N_65
set_property PACKAGE_PIN AH1 [get_ports { fmcHpc1P[3] }] ;# [get_ports "FMC_HPC1_LA03_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L22P_T3U_N6_DBC_AD0P_65

set_property PACKAGE_PIN AF1 [get_ports { fmcHpc1N[4] }] ;# [get_ports "FMC_HPC1_LA04_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L21N_T3L_N5_AD8N_65
set_property PACKAGE_PIN AF2 [get_ports { fmcHpc1P[4] }] ;# [get_ports "FMC_HPC1_LA04_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L21P_T3L_N4_AD8P_65

set_property PACKAGE_PIN AH3 [get_ports { fmcHpc1N[5] }] ;# [get_ports "FMC_HPC1_LA05_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L20N_T3L_N3_AD1N_65
set_property PACKAGE_PIN AG3 [get_ports { fmcHpc1P[5] }] ;# [get_ports "FMC_HPC1_LA05_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L20P_T3L_N2_AD1P_65

set_property PACKAGE_PIN AJ2 [get_ports { fmcHpc1N[6] }] ;# [get_ports "FMC_HPC1_LA06_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L19N_T3L_N1_DBC_AD9N_65
set_property PACKAGE_PIN AH2 [get_ports { fmcHpc1P[6] }] ;# [get_ports "FMC_HPC1_LA06_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L19P_T3L_N0_DBC_AD9P_65

set_property PACKAGE_PIN AE4 [get_ports { fmcHpc1N[7] }] ;# [get_ports "FMC_HPC1_LA07_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L18N_T2U_N11_AD2N_65
set_property PACKAGE_PIN AD4 [get_ports { fmcHpc1P[7] }] ;# [get_ports "FMC_HPC1_LA07_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L18P_T2U_N10_AD2P_65

set_property PACKAGE_PIN AF3 [get_ports { fmcHpc1N[8] }] ;# [get_ports "FMC_HPC1_LA08_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L17N_T2U_N9_AD10N_65
set_property PACKAGE_PIN AE3 [get_ports { fmcHpc1P[8] }] ;# [get_ports "FMC_HPC1_LA08_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L17P_T2U_N8_AD10P_65

set_property PACKAGE_PIN AE1 [get_ports { fmcHpc1N[9] }] ;# [get_ports "FMC_HPC1_LA09_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L24N_T3U_N11_PERSTN0_65
set_property PACKAGE_PIN AE2 [get_ports { fmcHpc1P[9] }] ;# [get_ports "FMC_HPC1_LA09_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L24P_T3U_N10_PERSTN1_I2C_SDA_65

set_property PACKAGE_PIN AJ4 [get_ports { fmcHpc1N[10] }] ;# [get_ports "FMC_HPC1_LA10_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L15N_T2L_N5_AD11N_65
set_property PACKAGE_PIN AH4 [get_ports { fmcHpc1P[10] }] ;# [get_ports "FMC_HPC1_LA10_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L15P_T2L_N4_AD11P_65

set_property PACKAGE_PIN AF8 [get_ports { fmcHpc1N[11] }] ;# [get_ports "FMC_HPC1_LA11_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L10N_T1U_N7_QBC_AD4N_65
set_property PACKAGE_PIN AE8 [get_ports { fmcHpc1P[11] }] ;# [get_ports "FMC_HPC1_LA11_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L10P_T1U_N6_QBC_AD4P_65

set_property PACKAGE_PIN AD6 [get_ports { fmcHpc1N[12] }] ;# [get_ports "FMC_HPC1_LA12_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L9N_T1L_N5_AD12N_65
set_property PACKAGE_PIN AD7 [get_ports { fmcHpc1P[12] }] ;# [get_ports "FMC_HPC1_LA12_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L9P_T1L_N4_AD12P_65

set_property PACKAGE_PIN AH8 [get_ports { fmcHpc1N[13] }] ;# [get_ports "FMC_HPC1_LA13_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L8N_T1L_N3_AD5N_65
set_property PACKAGE_PIN AG8 [get_ports { fmcHpc1P[13] }] ;# [get_ports "FMC_HPC1_LA13_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L8P_T1L_N2_AD5P_65

set_property PACKAGE_PIN AH6 [get_ports { fmcHpc1N[14] }] ;# [get_ports "FMC_HPC1_LA14_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L7N_T1L_N1_QBC_AD13N_65
set_property PACKAGE_PIN AH7 [get_ports { fmcHpc1P[14] }] ;# [get_ports "FMC_HPC1_LA14_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L7P_T1L_N0_QBC_AD13P_65

set_property PACKAGE_PIN AE9  [get_ports { fmcHpc1N[15] }] ;# [get_ports "FMC_HPC1_LA15_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L6N_T0U_N11_AD6N_65
set_property PACKAGE_PIN AD10 [get_ports { fmcHpc1P[15] }] ;# [get_ports "FMC_HPC1_LA15_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L6P_T0U_N10_AD6P_65

set_property PACKAGE_PIN AG9  [get_ports { fmcHpc1N[16] }] ;# [get_ports "FMC_HPC1_LA16_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L5N_T0U_N9_AD14N_65
set_property PACKAGE_PIN AG10 [get_ports { fmcHpc1P[16] }] ;# [get_ports "FMC_HPC1_LA16_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L5P_T0U_N8_AD14P_65

set_property PACKAGE_PIN AA5 [get_ports { fmcHpc1N[17] }] ;# [get_ports "FMC_HPC1_LA17_CC_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L14N_T2L_N3_GC_66
set_property PACKAGE_PIN Y5  [get_ports { fmcHpc1P[17] }] ;# [get_ports "FMC_HPC1_LA17_CC_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L14P_T2L_N2_GC_66

set_property PACKAGE_PIN Y7 [get_ports { fmcHpc1N[18] }] ;# [get_ports "FMC_HPC1_LA18_CC_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L11N_T1U_N9_GC_66
set_property PACKAGE_PIN Y8 [get_ports { fmcHpc1P[18] }] ;# [get_ports "FMC_HPC1_LA18_CC_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L11P_T1U_N8_GC_66

set_property PACKAGE_PIN AA10 [get_ports { fmcHpc1N[19] }] ;# [get_ports "FMC_HPC1_LA19_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L3N_T0L_N5_AD15N_66
set_property PACKAGE_PIN AA11 [get_ports { fmcHpc1P[19] }] ;# [get_ports "FMC_HPC1_LA19_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L3P_T0L_N4_AD15P_66

set_property PACKAGE_PIN AB10 [get_ports { fmcHpc1N[20] }] ;# [get_ports "FMC_HPC1_LA20_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L2N_T0L_N3_66
set_property PACKAGE_PIN AB11 [get_ports { fmcHpc1P[20] }] ;# [get_ports "FMC_HPC1_LA20_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L2P_T0L_N2_66

set_property PACKAGE_PIN AC11 [get_ports { fmcHpc1N[21] }] ;# [get_ports "FMC_HPC1_LA21_N"] ;# Bank  66 VCCO - VADJ_FMC - IO_L1N_T0L_N1_DBC_66
set_property PACKAGE_PIN AC12 [get_ports { fmcHpc1P[21] }] ;# [get_ports "FMC_HPC1_LA21_P"] ;# Bank  66 VCCO - VADJ_FMC - IO_L1P_T0L_N0_DBC_66

set_property PACKAGE_PIN AG11 [get_ports { fmcHpc1N[22] }] ;# [get_ports "FMC_HPC1_LA22_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L4N_T0U_N7_DBC_AD7N_65
set_property PACKAGE_PIN AF11 [get_ports { fmcHpc1P[22] }] ;# [get_ports "FMC_HPC1_LA22_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L4P_T0U_N6_DBC_AD7P_SMBALERT_65

set_property PACKAGE_PIN AF12 [get_ports { fmcHpc1N[23] }] ;# [get_ports "FMC_HPC1_LA23_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L3N_T0L_N5_AD15N_65
set_property PACKAGE_PIN AE12 [get_ports { fmcHpc1P[23] }] ;# [get_ports "FMC_HPC1_LA23_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L3P_T0L_N4_AD15P_65

set_property PACKAGE_PIN AH11 [get_ports { fmcHpc1N[24] }] ;# [get_ports "FMC_HPC1_LA24_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L2N_T0L_N3_65
set_property PACKAGE_PIN AH12 [get_ports { fmcHpc1P[24] }] ;# [get_ports "FMC_HPC1_LA24_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L2P_T0L_N2_65

set_property PACKAGE_PIN AF10 [get_ports { fmcHpc1N[25] }] ;# [get_ports "FMC_HPC1_LA25_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L1N_T0L_N1_DBC_65
set_property PACKAGE_PIN AE10 [get_ports { fmcHpc1P[25] }] ;# [get_ports "FMC_HPC1_LA25_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L1P_T0L_N0_DBC_65

set_property PACKAGE_PIN R12 [get_ports { fmcHpc1N[26] }] ;# [get_ports "FMC_HPC1_LA26_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L4N_T0U_N7_DBC_AD7N_67
set_property PACKAGE_PIN T12 [get_ports { fmcHpc1P[26] }] ;# [get_ports "FMC_HPC1_LA26_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L4P_T0U_N6_DBC_AD7P_67

set_property PACKAGE_PIN T10 [get_ports { fmcHpc1N[27] }] ;# [get_ports "FMC_HPC1_LA27_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L3N_T0L_N5_AD15N_67
set_property PACKAGE_PIN U10 [get_ports { fmcHpc1P[27] }] ;# [get_ports "FMC_HPC1_LA27_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L3P_T0L_N4_AD15P_67

set_property PACKAGE_PIN R13 [get_ports { fmcHpc1N[28] }] ;# [get_ports "FMC_HPC1_LA28_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L2N_T0L_N3_67
set_property PACKAGE_PIN T13 [get_ports { fmcHpc1P[28] }] ;# [get_ports "FMC_HPC1_LA28_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L2P_T0L_N2_67

set_property PACKAGE_PIN W11 [get_ports { fmcHpc1N[29] }] ;# [get_ports "FMC_HPC1_LA29_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L1N_T0L_N1_DBC_67
set_property PACKAGE_PIN W12 [get_ports { fmcHpc1P[29] }] ;# [get_ports "FMC_HPC1_LA29_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L1P_T0L_N0_DBC_67

set_property -dict { IOSTANDARD LVCMOS18 } [get_ports { fmcHpc1N[*] }]
set_property -dict { IOSTANDARD LVCMOS18 } [get_ports { fmcHpc1P[*] }]

set_property -dict { PACKAGE_PIN AF7 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc1ClkN[0] }] ;# [get_ports "FMC_HPC1_CLK0_M2C_N"] ;# Bank  65 VCCO - VADJ_FMC - IO_L12N_T1U_N11_GC_65
set_property -dict { PACKAGE_PIN AE7 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc1ClkP[0] }] ;# [get_ports "FMC_HPC1_CLK0_M2C_P"] ;# Bank  65 VCCO - VADJ_FMC - IO_L12P_T1U_N10_GC_65

set_property -dict { PACKAGE_PIN P9  IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc1ClkN[1] }] ;# [get_ports "FMC_HPC1_CLK1_M2C_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L14N_T2L_N3_GC_67
set_property -dict { PACKAGE_PIN P10 IOSTANDARD LVDS DIFF_TERM_ADV TERM_100 } [get_ports { fmcHpc1ClkP[1] }] ;# [get_ports "FMC_HPC1_CLK1_M2C_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L14P_T2L_N2_GC_67

##############################################################################

# FMC[0] - High Speeds

set_property PACKAGE_PIN G3 [get_ports { fmcHpc0GtTxN[0] }] ;# [get_ports "FMC_HPC0_DP0_C2M_N"] ;# Bank 229 - MGTHTXN2_229
set_property PACKAGE_PIN G4 [get_ports { fmcHpc0GtTxP[0] }] ;# [get_ports "FMC_HPC0_DP0_C2M_P"] ;# Bank 229 - MGTHTXP2_229
set_property PACKAGE_PIN H1 [get_ports { fmcHpc0GtRxN[0] }] ;# [get_ports "FMC_HPC0_DP0_M2C_N"] ;# Bank 229 - MGTHRXN2_229
set_property PACKAGE_PIN H2 [get_ports { fmcHpc0GtRxP[0] }] ;# [get_ports "FMC_HPC0_DP0_M2C_P"] ;# Bank 229 - MGTHRXP2_229

set_property PACKAGE_PIN H5 [get_ports { fmcHpc0GtTxN[1] }] ;# [get_ports "FMC_HPC0_DP1_C2M_N"] ;# Bank 229 - MGTHTXN1_229
set_property PACKAGE_PIN H6 [get_ports { fmcHpc0GtTxP[1] }] ;# [get_ports "FMC_HPC0_DP1_C2M_P"] ;# Bank 229 - MGTHTXP1_229
set_property PACKAGE_PIN J3 [get_ports { fmcHpc0GtRxN[1] }] ;# [get_ports "FMC_HPC0_DP1_M2C_N"] ;# Bank 229 - MGTHRXN1_229
set_property PACKAGE_PIN J4 [get_ports { fmcHpc0GtRxP[1] }] ;# [get_ports "FMC_HPC0_DP1_M2C_P"] ;# Bank 229 - MGTHRXP1_229

set_property PACKAGE_PIN F5 [get_ports { fmcHpc0GtTxN[2] }] ;# [get_ports "FMC_HPC0_DP2_C2M_N"] ;# Bank 229 - MGTHTXN3_229
set_property PACKAGE_PIN F6 [get_ports { fmcHpc0GtTxP[2] }] ;# [get_ports "FMC_HPC0_DP2_C2M_P"] ;# Bank 229 - MGTHTXP3_229
set_property PACKAGE_PIN F1 [get_ports { fmcHpc0GtRxN[2] }] ;# [get_ports "FMC_HPC0_DP2_M2C_N"] ;# Bank 229 - MGTHRXN3_229
set_property PACKAGE_PIN F2 [get_ports { fmcHpc0GtRxP[2] }] ;# [get_ports "FMC_HPC0_DP2_M2C_P"] ;# Bank 229 - MGTHRXP3_229

set_property PACKAGE_PIN K5 [get_ports { fmcHpc0GtTxN[3] }] ;# [get_ports "FMC_HPC0_DP3_C2M_N"] ;# Bank 229 - MGTHTXN0_229
set_property PACKAGE_PIN K6 [get_ports { fmcHpc0GtTxP[3] }] ;# [get_ports "FMC_HPC0_DP3_C2M_P"] ;# Bank 229 - MGTHTXP0_229
set_property PACKAGE_PIN K1 [get_ports { fmcHpc0GtRxN[3] }] ;# [get_ports "FMC_HPC0_DP3_M2C_N"] ;# Bank 229 - MGTHRXN0_229
set_property PACKAGE_PIN K2 [get_ports { fmcHpc0GtRxP[3] }] ;# [get_ports "FMC_HPC0_DP3_M2C_P"] ;# Bank 229 - MGTHRXP0_229

set_property PACKAGE_PIN M5 [get_ports { fmcHpc0GtTxN[4] }] ;# [get_ports "FMC_HPC0_DP4_C2M_N"] ;# Bank 228 - MGTHTXN3_228
set_property PACKAGE_PIN M6 [get_ports { fmcHpc0GtTxP[4] }] ;# [get_ports "FMC_HPC0_DP4_C2M_P"] ;# Bank 228 - MGTHTXP3_228
set_property PACKAGE_PIN L3 [get_ports { fmcHpc0GtRxN[4] }] ;# [get_ports "FMC_HPC0_DP4_M2C_N"] ;# Bank 228 - MGTHRXN3_228
set_property PACKAGE_PIN L4 [get_ports { fmcHpc0GtRxP[4] }] ;# [get_ports "FMC_HPC0_DP4_M2C_P"] ;# Bank 228 - MGTHRXP3_228

set_property PACKAGE_PIN P5 [get_ports { fmcHpc0GtTxN[5] }] ;# [get_ports "FMC_HPC0_DP5_C2M_N"] ;# Bank 228 - MGTHTXN1_228
set_property PACKAGE_PIN P6 [get_ports { fmcHpc0GtTxP[5] }] ;# [get_ports "FMC_HPC0_DP5_C2M_P"] ;# Bank 228 - MGTHTXP1_228
set_property PACKAGE_PIN P1 [get_ports { fmcHpc0GtRxN[5] }] ;# [get_ports "FMC_HPC0_DP5_M2C_N"] ;# Bank 228 - MGTHRXN1_228
set_property PACKAGE_PIN P2 [get_ports { fmcHpc0GtRxP[5] }] ;# [get_ports "FMC_HPC0_DP5_M2C_P"] ;# Bank 228 - MGTHRXP1_228

set_property PACKAGE_PIN R3 [get_ports { fmcHpc0GtTxN[6] }] ;# [get_ports "FMC_HPC0_DP6_C2M_N"] ;# Bank 228 - MGTHTXN0_228
set_property PACKAGE_PIN R4 [get_ports { fmcHpc0GtTxP[6] }] ;# [get_ports "FMC_HPC0_DP6_C2M_P"] ;# Bank 228 - MGTHTXP0_228
set_property PACKAGE_PIN T1 [get_ports { fmcHpc0GtRxN[6] }] ;# [get_ports "FMC_HPC0_DP6_M2C_N"] ;# Bank 228 - MGTHRXN0_228
set_property PACKAGE_PIN T2 [get_ports { fmcHpc0GtRxP[6] }] ;# [get_ports "FMC_HPC0_DP6_M2C_P"] ;# Bank 228 - MGTHRXP0_228

set_property PACKAGE_PIN N3 [get_ports { fmcHpc0GtTxN[7] }] ;# [get_ports "FMC_HPC0_DP7_C2M_N"] ;# Bank 228 - MGTHTXN2_228
set_property PACKAGE_PIN N4 [get_ports { fmcHpc0GtTxP[7] }] ;# [get_ports "FMC_HPC0_DP7_C2M_P"] ;# Bank 228 - MGTHTXP2_228
set_property PACKAGE_PIN M1 [get_ports { fmcHpc0GtRxN[7] }] ;# [get_ports "FMC_HPC0_DP7_M2C_N"] ;# Bank 228 - MGTHRXN2_228
set_property PACKAGE_PIN M2 [get_ports { fmcHpc0GtRxP[7] }] ;# [get_ports "FMC_HPC0_DP7_M2C_P"] ;# Bank 228 - MGTHRXP2_228

set_property PACKAGE_PIN G7 [get_ports { fmcHpc0GtClkN[0] }] ;# [get_ports "FMC_HPC0_GBTCLK0_M2C_C_N"] ;# Bank 229 - MGTREFCLK0N_229
set_property PACKAGE_PIN G8 [get_ports { fmcHpc0GtClkP[0] }] ;# [get_ports "FMC_HPC0_GBTCLK0_M2C_C_P"] ;# Bank 229 - MGTREFCLK0P_229

set_property PACKAGE_PIN L7 [get_ports { fmcHpc0GtClkN[1] }] ;# [get_ports "FMC_HPC0_GBTCLK1_M2C_C_N"] ;# Bank 228 - MGTREFCLK0N_228
set_property PACKAGE_PIN L8 [get_ports { fmcHpc0GtClkP[1] }] ;# [get_ports "FMC_HPC0_GBTCLK1_M2C_C_P"] ;# Bank 228 - MGTREFCLK0P_228

##############################################################################

# FMC[1] - High Speeds

set_property PACKAGE_PIN F30 [get_ports { fmcHpc1GtTxN[0] }] ;# [get_ports "FMC_HPC1_DP0_C2M_N"] ;# Bank 130 - MGTHTXN0_130
set_property PACKAGE_PIN F29 [get_ports { fmcHpc1GtTxP[0] }] ;# [get_ports "FMC_HPC1_DP0_C2M_P"] ;# Bank 130 - MGTHTXP0_130
set_property PACKAGE_PIN E32 [get_ports { fmcHpc1GtRxN[0] }] ;# [get_ports "FMC_HPC1_DP0_M2C_N"] ;# Bank 130 - MGTHRXN0_130
set_property PACKAGE_PIN E31 [get_ports { fmcHpc1GtRxP[0] }] ;# [get_ports "FMC_HPC1_DP0_M2C_P"] ;# Bank 130 - MGTHRXP0_130

set_property PACKAGE_PIN D30 [get_ports { fmcHpc1GtTxN[1] }] ;# [get_ports "FMC_HPC1_DP1_C2M_N"] ;# Bank 130 - MGTHTXN1_130
set_property PACKAGE_PIN D29 [get_ports { fmcHpc1GtTxP[1] }] ;# [get_ports "FMC_HPC1_DP1_C2M_P"] ;# Bank 130 - MGTHTXP1_130
set_property PACKAGE_PIN D34 [get_ports { fmcHpc1GtRxN[1] }] ;# [get_ports "FMC_HPC1_DP1_M2C_N"] ;# Bank 130 - MGTHRXN1_130
set_property PACKAGE_PIN D33 [get_ports { fmcHpc1GtRxP[1] }] ;# [get_ports "FMC_HPC1_DP1_M2C_P"] ;# Bank 130 - MGTHRXP1_130

set_property PACKAGE_PIN B30 [get_ports { fmcHpc1GtTxN[2] }] ;# [get_ports "FMC_HPC1_DP2_C2M_N"] ;# Bank 130 - MGTHTXN2_130
set_property PACKAGE_PIN B29 [get_ports { fmcHpc1GtTxP[2] }] ;# [get_ports "FMC_HPC1_DP2_C2M_P"] ;# Bank 130 - MGTHTXP2_130
set_property PACKAGE_PIN C32 [get_ports { fmcHpc1GtRxN[2] }] ;# [get_ports "FMC_HPC1_DP2_M2C_N"] ;# Bank 130 - MGTHRXN2_130
set_property PACKAGE_PIN C31 [get_ports { fmcHpc1GtRxP[2] }] ;# [get_ports "FMC_HPC1_DP2_M2C_P"] ;# Bank 130 - MGTHRXP2_130

set_property PACKAGE_PIN A32 [get_ports { fmcHpc1GtTxN[3] }] ;# [get_ports "FMC_HPC1_DP3_C2M_N"] ;# Bank 130 - MGTHTXN3_130
set_property PACKAGE_PIN A31 [get_ports { fmcHpc1GtTxP[3] }] ;# [get_ports "FMC_HPC1_DP3_C2M_P"] ;# Bank 130 - MGTHTXP3_130
set_property PACKAGE_PIN B34 [get_ports { fmcHpc1GtRxN[3] }] ;# [get_ports "FMC_HPC1_DP3_M2C_N"] ;# Bank 130 - MGTHRXN3_130
set_property PACKAGE_PIN B33 [get_ports { fmcHpc1GtRxP[3] }] ;# [get_ports "FMC_HPC1_DP3_M2C_P"] ;# Bank 130 - MGTHRXP3_130

set_property PACKAGE_PIN K30 [get_ports { fmcHpc1GtTxN[4] }] ;# [get_ports "FMC_HPC1_DP4_C2M_N"] ;# Bank 129 - MGTHTXN0_129
set_property PACKAGE_PIN K29 [get_ports { fmcHpc1GtTxP[4] }] ;# [get_ports "FMC_HPC1_DP4_C2M_P"] ;# Bank 129 - MGTHTXP0_129
set_property PACKAGE_PIN L32 [get_ports { fmcHpc1GtRxN[4] }] ;# [get_ports "FMC_HPC1_DP4_M2C_N"] ;# Bank 129 - MGTHRXN0_129
set_property PACKAGE_PIN L31 [get_ports { fmcHpc1GtRxP[4] }] ;# [get_ports "FMC_HPC1_DP4_M2C_P"] ;# Bank 129 - MGTHRXP0_129

set_property PACKAGE_PIN J32 [get_ports { fmcHpc1GtTxN[5] }] ;# [get_ports "FMC_HPC1_DP5_C2M_N"] ;# Bank 129 - MGTHTXN1_129
set_property PACKAGE_PIN J31 [get_ports { fmcHpc1GtTxP[5] }] ;# [get_ports "FMC_HPC1_DP5_C2M_P"] ;# Bank 129 - MGTHTXP1_129
set_property PACKAGE_PIN K34 [get_ports { fmcHpc1GtRxN[5] }] ;# [get_ports "FMC_HPC1_DP5_M2C_N"] ;# Bank 129 - MGTHRXN1_129
set_property PACKAGE_PIN K33 [get_ports { fmcHpc1GtRxP[5] }] ;# [get_ports "FMC_HPC1_DP5_M2C_P"] ;# Bank 129 - MGTHRXP1_129

set_property PACKAGE_PIN H30 [get_ports { fmcHpc1GtTxN[6] }] ;# [get_ports "FMC_HPC1_DP6_C2M_N"] ;# Bank 129 - MGTHTXN2_129
set_property PACKAGE_PIN H29 [get_ports { fmcHpc1GtTxP[6] }] ;# [get_ports "FMC_HPC1_DP6_C2M_P"] ;# Bank 129 - MGTHTXP2_129
set_property PACKAGE_PIN H34 [get_ports { fmcHpc1GtRxN[6] }] ;# [get_ports "FMC_HPC1_DP6_M2C_N"] ;# Bank 129 - MGTHRXN2_129
set_property PACKAGE_PIN H33 [get_ports { fmcHpc1GtRxP[6] }] ;# [get_ports "FMC_HPC1_DP6_M2C_P"] ;# Bank 129 - MGTHRXP2_129

set_property PACKAGE_PIN G32 [get_ports { fmcHpc1GtTxN[7] }] ;# [get_ports "FMC_HPC1_DP7_C2M_N"] ;# Bank 129 - MGTHTXN3_129
set_property PACKAGE_PIN G31 [get_ports { fmcHpc1GtTxP[7] }] ;# [get_ports "FMC_HPC1_DP7_C2M_P"] ;# Bank 129 - MGTHTXP3_129
set_property PACKAGE_PIN F34 [get_ports { fmcHpc1GtRxN[7] }] ;# [get_ports "FMC_HPC1_DP7_M2C_N"] ;# Bank 129 - MGTHRXN3_129
set_property PACKAGE_PIN F33 [get_ports { fmcHpc1GtRxP[7] }] ;# [get_ports "FMC_HPC1_DP7_M2C_P"] ;# Bank 129 - MGTHRXP3_129

set_property PACKAGE_PIN G28 [get_ports { fmcHpc1GtClkN[0] }] ;# [get_ports "FMC_HPC1_GBTCLK0_M2C_C_N"] ;# Bank 130 - MGTREFCLK0N_130
set_property PACKAGE_PIN G27 [get_ports { fmcHpc1GtClkP[0] }] ;# [get_ports "FMC_HPC1_GBTCLK0_M2C_C_P"] ;# Bank 130 - MGTREFCLK0P_130

set_property PACKAGE_PIN E28 [get_ports { fmcHpc1GtClkN[1] }] ;# [get_ports "FMC_HPC1_GBTCLK1_M2C_C_N"] ;# Bank 130 - MGTREFCLK1N_130
set_property PACKAGE_PIN E27 [get_ports { fmcHpc1GtClkP[1] }] ;# [get_ports "FMC_HPC1_GBTCLK1_M2C_C_P"] ;# Bank 130 - MGTREFCLK1P_130

##############################################################################

# SFP Ports

set_property PACKAGE_PIN E3 [get_ports { sfpGtTxN[0] }] ;# [get_ports "SFP0_TX_N"] ;# Bank 230 - MGTHTXN0_230
set_property PACKAGE_PIN E4 [get_ports { sfpGtTxP[0] }] ;# [get_ports "SFP0_TX_P"] ;# Bank 230 - MGTHTXP0_230
set_property PACKAGE_PIN D1 [get_ports { sfpGtRxN[0] }] ;# [get_ports "SFP0_RX_N"] ;# Bank 230 - MGTHRXN0_230
set_property PACKAGE_PIN D2 [get_ports { sfpGtRxP[0] }] ;# [get_ports "SFP0_RX_P"] ;# Bank 230 - MGTHRXP0_230

set_property PACKAGE_PIN D5 [get_ports { sfpGtTxN[1] }] ;# [get_ports "SFP1_TX_N"] ;# Bank 230 - MGTHTXN1_230
set_property PACKAGE_PIN D6 [get_ports { sfpGtTxP[1] }] ;# [get_ports "SFP1_TX_P"] ;# Bank 230 - MGTHTXP1_230
set_property PACKAGE_PIN C3 [get_ports { sfpGtRxN[1] }] ;# [get_ports "SFP1_RX_N"] ;# Bank 230 - MGTHRXN1_230
set_property PACKAGE_PIN C4 [get_ports { sfpGtRxP[1] }] ;# [get_ports "SFP1_RX_P"] ;# Bank 230 - MGTHRXP1_230

set_property PACKAGE_PIN B5 [get_ports { sfpGtTxN[2] }] ;# [get_ports "SFP2_TX_N"] ;# Bank 230 - MGTHTXN2_230
set_property PACKAGE_PIN B6 [get_ports { sfpGtTxP[2] }] ;# [get_ports "SFP2_TX_P"] ;# Bank 230 - MGTHTXP2_230
set_property PACKAGE_PIN B1 [get_ports { sfpGtRxN[2] }] ;# [get_ports "SFP2_RX_N"] ;# Bank 230 - MGTHRXN2_230
set_property PACKAGE_PIN B2 [get_ports { sfpGtRxP[2] }] ;# [get_ports "SFP2_RX_P"] ;# Bank 230 - MGTHRXP2_230

set_property PACKAGE_PIN A7 [get_ports { sfpGtTxN[3] }] ;# [get_ports "SFP3_TX_N"] ;# Bank 230 - MGTHTXN3_230
set_property PACKAGE_PIN A8 [get_ports { sfpGtTxP[3] }] ;# [get_ports "SFP3_TX_P"] ;# Bank 230 - MGTHTXP3_230
set_property PACKAGE_PIN A3 [get_ports { sfpGtRxN[3] }] ;# [get_ports "SFP3_RX_N"] ;# Bank 230 - MGTHRXN3_230
set_property PACKAGE_PIN A4 [get_ports { sfpGtRxP[3] }] ;# [get_ports "SFP3_RX_P"] ;# Bank 230 - MGTHRXP3_230

set_property PACKAGE_PIN A12 [get_ports { sfpTxDisable[0] }] ;# [get_ports "SFP0_TX_DISABLE"] ;# Bank  49 VCCO - VCC3V3   - IO_L9N_AD11N_49
set_property PACKAGE_PIN A13 [get_ports { sfpTxDisable[1] }] ;# [get_ports "SFP1_TX_DISABLE"] ;# Bank  49 VCCO - VCC3V3   - IO_L9P_AD11P_49
set_property PACKAGE_PIN B13 [get_ports { sfpTxDisable[2] }] ;# [get_ports "SFP2_TX_DISABLE"] ;# Bank  49 VCCO - VCC3V3   - IO_L8N_HDGC_49
set_property PACKAGE_PIN C13 [get_ports { sfpTxDisable[3] }] ;# [get_ports "SFP3_TX_DISABLE"] ;# Bank  49 VCCO - VCC3V3   - IO_L8P_HDGC_49

set_property -dict { IOSTANDARD LVCMOS33 } [get_ports { sfpTxDisable[*] }]

set_property PACKAGE_PIN B9  [get_ports { sfpSi5328OutN }] ;# [get_ports "SFP_SI5328_OUT_C_N"] ;# Bank 230 - MGTREFCLK1N_230
set_property PACKAGE_PIN B10 [get_ports { sfpSi5328OutP }] ;# [get_ports "SFP_SI5328_OUT_C_P"] ;# Bank 230 - MGTREFCLK1P_230

set_property -dict { PACKAGE_PIN H10 IOSTANDARD LVCMOS33 } [get_ports { sfpSi5328IntAlm }] ;# [get_ports "SFP_SI5328_INT_ALM"] ;# Bank  50 VCCO - VCC3V3   - IO_L2P_AD14P_50

set_property -dict { PACKAGE_PIN R9  IOSTANDARD LVDS } [get_ports { sfpRecClkN }] ;# [get_ports "SFP_REC_CLOCK_C_N"] ;# Bank  67 VCCO - VADJ_FMC - IO_L11N_T1U_N9_GC_67
set_property -dict { PACKAGE_PIN R10 IOSTANDARD LVDS } [get_ports { sfpRecClkP }] ;# [get_ports "SFP_REC_CLOCK_C_P"] ;# Bank  67 VCCO - VADJ_FMC - IO_L11P_T1U_N8_GC_67

##############################################################################

# SMA Ports

set_property PACKAGE_PIN M34 [get_ports { smaGtRxN }] ;# [get_ports "SMA_MGT_RX_C_N"] ;# Bank 128 - MGTHRXN3_128
set_property PACKAGE_PIN M33 [get_ports { smaGtRxP }] ;# [get_ports "SMA_MGT_RX_C_P"] ;# Bank 128 - MGTHRXP3_128

set_property PACKAGE_PIN M30 [get_ports { smaGtTxN }] ;# [get_ports "SMA_MGT_TX_N"] ;# Bank 128 - MGTHTXN3_128
set_property PACKAGE_PIN M29 [get_ports { smaGtTxP }] ;# [get_ports "SMA_MGT_TX_P"] ;# Bank 128 - MGTHTXP3_128

set_property PACKAGE_PIN J28 [get_ports { smaGtClkN }] ;# [get_ports "USER_SMA_MGT_CLOCK_C_N"] ;# Bank 129 - MGTREFCLK1N_129
set_property PACKAGE_PIN J27 [get_ports { smaGtClkP }] ;# [get_ports "USER_SMA_MGT_CLOCK_C_P"] ;# Bank 129 - MGTREFCLK1P_129

##############################################################################

# Misc. Clock Ports

set_property PACKAGE_PIN C7 [get_ports { userGtClkN[1] }] ;# [get_ports "USER_MGT_SI570_CLOCK2_C_N"] ;# Bank 230 - MGTREFCLK0N_230
set_property PACKAGE_PIN C8 [get_ports { userGtClkP[1] }] ;# [get_ports "USER_MGT_SI570_CLOCK2_C_P"] ;# Bank 230 - MGTREFCLK0P_230

set_property PACKAGE_PIN L28 [get_ports { userGtClkN[0] }] ;# [get_ports "USER_MGT_SI570_CLOCK1_C_N"] ;# Bank 129 - MGTREFCLK0N_129
set_property PACKAGE_PIN L27 [get_ports { userGtClkP[0] }] ;# [get_ports "USER_MGT_SI570_CLOCK1_C_P"] ;# Bank 129 - MGTREFCLK0P_129

set_property -dict { PACKAGE_PIN AK14 IOSTANDARD LVDS_25 } [get_ports { clk74p25N }] ;# [get_ports "CLK_74_25_N"] ;# Bank  44 VCCO - VCC3V3   - IO_L5N_HDGC_AD7N_44
set_property -dict { PACKAGE_PIN AK15 IOSTANDARD LVDS_25 } [get_ports { clk74p25P }] ;# [get_ports "CLK_74_25_P"] ;# Bank  44 VCCO - VCC3V3   - IO_L5P_HDGC_AD7P_44

set_property -dict { PACKAGE_PIN F21 IOSTANDARD LVDS_25 } [get_ports { clk125N }] ;# [get_ports "CLK_125_N"] ;# Bank  47 VCCO - VCC3V3   - IO_L5N_HDGC_AD7N_47
set_property -dict { PACKAGE_PIN G21 IOSTANDARD LVDS_25 } [get_ports { clk125P }] ;# [get_ports "CLK_125_P"] ;# Bank  47 VCCO - VCC3V3   - IO_L5P_HDGC_AD7P_47

##############################################################################

set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT0]] -group [get_clocks -of_objects [get_pins U_Core/REAL_CPU.U_CPU/U_Pll/PllGen.U_Pll/CLKOUT1]]

##############################################################################
