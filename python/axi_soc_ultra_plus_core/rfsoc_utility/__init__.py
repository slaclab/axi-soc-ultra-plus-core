from axi_soc_ultra_plus_core.rfsoc_utility._AppRingBuffer       import *
from axi_soc_ultra_plus_core.rfsoc_utility._SigGen              import *
from axi_soc_ultra_plus_core.rfsoc_utility._SigGenLoader        import *
from axi_soc_ultra_plus_core.rfsoc_utility._SigToAxiStream      import *
from axi_soc_ultra_plus_core.rfsoc_utility._RingBufferProcessor import *

from axi_soc_ultra_plus_core.rfsoc_utility._RfdcBlock import *
from axi_soc_ultra_plus_core.rfsoc_utility._RfdcTile  import *
from axi_soc_ultra_plus_core.rfsoc_utility._Rfdc      import *

enumCustomStartUp = {
    0x0 : "XRFDC_STATE_OFF",
    0x1 : "XRFDC_STATE_SHUTDOWN",
    0x3 : "XRFDC_STATE_PWRUP",
    0x6 : "XRFDC_STATE_CLK_DET",
    0xB : "XRFDC_STATE_CAL",
    0xF : "XRFDC_STATE_FULL",
}

powerOnSequenceSteps = {
    0:  'Device_Power-up_and_Configuration[0]',
    1:  'Device_Power-up_and_Configuration[1]',
    2:  'Device_Power-up_and_Configuration[2]',
    3:  'Power_Supply_Adjustment[0]',
    4:  'Power_Supply_Adjustment[1]',
    5:  'Power_Supply_Adjustment[2]',
    6:  'Clock_Configuration[0]',
    7:  'Clock_Configuration[1]',
    8:  'Clock_Configuration[2]',
    9:  'Clock_Configuration[3]',
    10: 'Clock_Configuration[4]',
    11: 'Converter_Calibration[0]',
    12: 'Converter_Calibration[1]',
    13: 'Converter_Calibration[2]',
    14: 'Wait_for_deassertion_of_AXI4-Stream_reset',
    15: 'Done',
}

enumEventSource = {
    0x0 : "XRFDC_EVNT_SRC_IMMEDIATE",
    0x1 : "XRFDC_EVNT_SRC_SLICE",
    0x2 : "XRFDC_EVNT_SRC_TILE",
    0x3 : "XRFDC_EVNT_SRC_SYSREF",
    0x4 : "XRFDC_EVNT_SRC_MARKER",
    0x5 : "XRFDC_EVNT_SRC_PL",
}

enumInterpDecim         = {
    0x0  : "XRFDC_INTERP_DECIM_OFF",
    0x1  : "XRFDC_INTERP_DECIM_1X",
    0x2  : "XRFDC_INTERP_DECIM_2X",
    0x3  : "XRFDC_INTERP_DECIM_3X",
    0x4  : "XRFDC_INTERP_DECIM_4X",
    0x5  : "XRFDC_INTERP_DECIM_5X",
    0x6  : "XRFDC_INTERP_DECIM_6X",
    0x8  : "XRFDC_INTERP_DECIM_8X",
    0xA  : "XRFDC_INTERP_DECIM_10X",
    0xC  : "XRFDC_INTERP_DECIM_12X",
    0x10 : "XRFDC_INTERP_DECIM_16X",
    0x14 : "XRFDC_INTERP_DECIM_20X",
    0x18 : "XRFDC_INTERP_DECIM_24X",
    0x28 : "XRFDC_INTERP_DECIM_40X",
}

enumUpdateThreshold = {
    0x0 : "UNDEFINED_0x0",
    0x1 : "XRFDC_UPDATE_THRESHOLD_0",
    0x2 : "XRFDC_UPDATE_THRESHOLD_1",
    0x3 : "UNDEFINED_0x3",
    0x4 : "XRFDC_UPDATE_THRESHOLD_BOTH",
}

enumMixedMode = {
    0x0 : "XRFDC_MIXER_MODE_OFF", #define XRFDC_MIXER_MODE_OFF 0x0U
    0x1 : "XRFDC_MIXER_MODE_C2C", #define XRFDC_MIXER_MODE_C2C 0x1U
    0x2 : "XRFDC_MIXER_MODE_C2R", #define XRFDC_MIXER_MODE_C2R 0x2U
    0x3 : "XRFDC_MIXER_MODE_R2C", #define XRFDC_MIXER_MODE_R2C 0x3U
    0x4 : "XRFDC_MIXER_MODE_R2R", #define XRFDC_MIXER_MODE_R2R 0x4U
}
