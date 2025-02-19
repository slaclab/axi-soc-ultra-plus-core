//-----------------------------------------------------------------------------
// This file is part of the 'axi-soc-ultra-plus-core'. It is subject to
// the license terms in the LICENSE.txt file found in the top-level directory
// of this distribution and at:
//    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
// No part of the 'axi-soc-ultra-plus-core', including this file, may be
// copied, modified, propagated, or distributed except according to the terms
// contained in the LICENSE.txt file.
//-----------------------------------------------------------------------------

/***************************** Include Files ********************************/
#include "xrfdc.h"

/************************** Constant Definitions ****************************/

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/

/************************** Function Prototypes *****************************/
int RFdcMTSAdc(tiles);
int RFdcMTSDac(tiles);

/************************** Variable Definitions ****************************/
static XRFdc RFdcInst;      /* RFdc driver instance */

/****************************************************************************/
int RFdcMTSAdc(tiles) {
   int status, status_adc, i;
   u32 factor;
   XRFdc_Config *ConfigPtr;
   XRFdc *RFdcInstPtr = &RFdcInst;
   struct metal_init_params init_param = METAL_INIT_DEFAULTS;

   if (metal_init(&init_param)) {
      printf("ERROR: Failed to run metal initialization\n");
      return XRFDC_FAILURE;
   }
   metal_set_log_level(METAL_LOG_DEBUG);

   ConfigPtr = XRFdc_LookupConfig(0);
   if (ConfigPtr == NULL) {
      printf("RFdc Config Failure\n\r");
      return XRFDC_FAILURE;
   }

   status = XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);
   // Note: No return on status=error in example script
   // https://github.com/Xilinx/embeddedsw/blob/xilinx_v2024.2/XilinxProcessorIPLib/drivers/rfdc/examples/xrfdc_mts_example.c#L163

   /* ADC MTS Settings */
   XRFdc_MultiConverter_Sync_Config ADC_Sync_Config;

   /* Run MTS for the ADC */
   printf("\n=== Run ADC Sync ===\n");

   /* Initialize ADC MTS Settings */
   XRFdc_MultiConverter_Init (&ADC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
   ADC_Sync_Config.Tiles = tiles;

   status_adc = XRFdc_MultiConverter_Sync(RFdcInstPtr, XRFDC_ADC_TILE, &ADC_Sync_Config);
   if(status_adc == XRFDC_MTS_OK){
      printf("INFO : ADC Multi-Tile-Sync completed successfully\n");
   } else {
      printf("ERROR : ADC Multi-Tile-Sync did not complete successfully. Error code is %u\n", status_adc);
      return status_adc;
   }

   /*
   * Report Overall Latency in T1 (Sample Clocks) and
   * Offsets (in terms of PL words) added to each FIFO
   */
   printf("\n\n=== Multi-Tile Sync Report ===\n");
   for(i=0; i<4; i++) {
      if( (1<<i)&ADC_Sync_Config.Tiles ) {
         XRFdc_GetDecimationFactor(RFdcInstPtr, i, 0, &factor);
         printf("ADC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, ADC_Sync_Config.Latency[i], factor, ADC_Sync_Config.Offset[i]);
      }
   }
   
   /* Return completed successfully */
   return XRFDC_MTS_OK;
}

/****************************************************************************/
int RFdcMTSDac(tiles) {
   int status, status_dac, i;
   u32 factor;
   XRFdc_Config *ConfigPtr;
   XRFdc *RFdcInstPtr = &RFdcInst;
   struct metal_init_params init_param = METAL_INIT_DEFAULTS;

   if (metal_init(&init_param)) {
      printf("ERROR: Failed to run metal initialization\n");
      return XRFDC_FAILURE;
   }
   metal_set_log_level(METAL_LOG_DEBUG);

   ConfigPtr = XRFdc_LookupConfig(0);
   if (ConfigPtr == NULL) {
      printf("RFdc Config Failure\n\r");
      return XRFDC_FAILURE;
   }

   status = XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);
   status = XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);
   // Note: No return on status=error in example script
   // https://github.com/Xilinx/embeddedsw/blob/xilinx_v2024.2/XilinxProcessorIPLib/drivers/rfdc/examples/xrfdc_mts_example.c#L163

   /* DAC MTS Settings */
   XRFdc_MultiConverter_Sync_Config DAC_Sync_Config;

   /* Run MTS for the DAC */
   printf("\n=== Run DAC Sync ===\n");

   /* Initialize DAC MTS Settings */
   XRFdc_MultiConverter_Init (&DAC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
   DAC_Sync_Config.Tiles = tiles;

   status_dac = XRFdc_MultiConverter_Sync(RFdcInstPtr, XRFDC_DAC_TILE, &DAC_Sync_Config);
   if(status_dac == XRFDC_MTS_OK){
      printf("INFO : DAC Multi-Tile-Sync completed successfully\n");
   }else{
      printf("ERROR : DAC Multi-Tile-Sync did not complete successfully. Error code is %u \n", status_dac);
      return status_dac;
   }

   /*
   * Report Overall Latency in T1 (Sample Clocks) and
   * Offsets (in terms of PL words) added to each FIFO
   */
   printf("\n\n=== Multi-Tile Sync Report ===\n");
   for(i=0; i<4; i++) {
      if((1<<i)&DAC_Sync_Config.Tiles) {
         XRFdc_GetInterpolationFactor(RFdcInstPtr, i, 0, &factor);
         printf("DAC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, DAC_Sync_Config.Latency[i], factor, DAC_Sync_Config.Offset[i]);
      }
   }
   
   /* Return completed successfully */
   return XRFDC_MTS_OK;
}

/****************************************************************************/
int main(void) {

	int Status;

	printf("RFdc MTS Example Test\r\n");

	Status = RFdcMTSAdc(0xF);
	if (Status != XRFDC_SUCCESS) {
		printf("MTS Example Test failed\r\n");
		return XRFDC_FAILURE;
	}

	printf("Successfully ran MTS Example\r\n");
	return XRFDC_SUCCESS;
}
/****************************************************************************/
