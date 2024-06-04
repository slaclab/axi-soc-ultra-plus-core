import axi_soc_ultra_plus_core.rfdc as rfdc

# https://github.com/Xilinx/embeddedsw/blob/xilinx_v2023.2/XilinxProcessorIPLib/drivers/rfdc/src/xrfdc_mts.c#L71-L101

XRFDC_MTS_NUM_DTC = 128
XRFDC_MTS_REF_TARGET = 64
XRFDC_MTS_MAX_CODE = 16
XRFDC_MTS_MIN_GAP_T1 = 10
XRFDC_MTS_MIN_GAP_GEN3 = 5
XRFDC_MTS_MIN_GAP_PLL = 5
XRFDC_MTS_SR_TIMEOUT = 4096
XRFDC_MTS_DTC_COUNT = 10
XRFDC_MTS_MARKER_COUNT = 4
XRFDC_MTS_SRCOUNT_TIMEOUT = 1000
XRFDC_MTS_DELAY_MAX = 31
XRFDC_MTS_CHECK_ALL_FIFOS = 0

XRFDC_MTS_SRCAP_T1_EN = 0x4000
XRFDC_MTS_SRCAP_T1_RST = 0x0800
XRFDC_MTS_SRFLAG_T1 = 0x4
XRFDC_MTS_SRFLAG_PLL = 0x2
XRFDC_MTS_FIFO_DEFAULT = 0x0000
XRFDC_MTS_FIFO_ENABLE = 0x0003
XRFDC_MTS_FIFO_DISABLE = 0x0002
XRFDC_MTS_AMARK_LOC_S = 0x10
XRFDC_MTS_AMARK_DONE_S = 0x14
XRFDC_MTS_DLY_ALIGNER0 = 0x28
XRFDC_MTS_DLY_ALIGNER1 = 0x2C
XRFDC_MTS_DLY_ALIGNER2 = 0x30
XRFDC_MTS_DLY_ALIGNER3 = 0x34
XRFDC_MTS_DIR_FIFO_PTR = 0x40

def XRFDC_MTS_DAC_MARKER_LOC_MASK(X):
    return 0x7 if X < XRFDC_GEN3 else 0xF
def XRFDC_MTS_RMW(read, mask, data):
    return (read & ~mask) | (data & mask)
def XRFDC_MTS_FIELD(data, mask, shift):
    return (data & mask) >> shift









# https://github.com/Xilinx/embeddedsw/blob/xilinx_v2023.2/XilinxProcessorIPLib/drivers/rfdc/examples/xrfdc_mts_example.c#L167-L226
def MultiTileSync(
        DAC_Tiles = 0x3, # Sync DAC tiles 0 and 1
        ADC_Tiles = 0xF, # Sync ADC tiles 0, 1, 2, 3
    )
    # ADC MTS Settings
    ADC_Sync_Config = rfdc.XRFdc_MultiConverter_Sync_Config()
    
    # DAC MTS Settings
    DAC_Sync_Config = rfdc.XRFdc_MultiConverter_Sync_Config()
    
    # Run MTS for the ADC & DAC
    print( "\n=== Run DAC Sync ===\n" )
    
    # Initialize DAC MTS Setting
    DAC_Sync_Config = rfdc.XRFdc_MultiConverter_Init(DAC_Tiles, 0, 0, XRFDC_TILE_ID0);
    DAC_Sync_Config.Tiles = DAC_Tiles;