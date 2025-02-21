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
#ifdef __BAREMETAL__
#include "xparameters.h"
#endif
#include "xrfdc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>

/************************** Constant Definitions ****************************/
#ifdef __BAREMETAL__
#define RFDC_DEVICE_ID 	XPAR_XRFDC_0_DEVICE_ID
#else
#define RFDC_DEVICE_ID 	0
#endif

/**************************** Type Definitions ******************************/

/***************** Macros (Inline Functions) Definitions ********************/
#ifdef __BAREMETAL__
#define printf xil_printf
#endif

/************************** Function Prototypes *****************************/
int RFdcMTSAdc(XRFdc *RFdcInstPtr, u8 tiles);
int RFdcMTSDac(XRFdc *RFdcInstPtr, u8 tiles);

/************************** Variable Definitions ****************************/
static XRFdc RFdcInst;      /* RFdc driver instance */

/****************************************************************************/
int RFdcMTSAdc(XRFdc *RFdcInstPtr, u8 tiles) {
   int status, status_adc, i;
   u32 factor;

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
int RFdcMTSDac(XRFdc *RFdcInstPtr, u8 tiles) {
   int status, status_dac, i;
   u32 factor;

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

/* Program documentation */
const char *argp_program_version = "RFDC API 1.0";
const char *argp_program_bug_address = "https://github.com/slaclab/axi-soc-ultra-plus-core";
static char doc[] = "RFDC API for Linux CLI";
static char args_doc[] = "<mts-adc|mts-adc> --tiles=0xF";

/* Available options */
static struct argp_option options[] = {
   {"tiles", 't', "VALUE", 0, "Set tiles (decimal or hex, e.g., 0xF or 15)"},
   { 0 } // Indicates end of options
};

/* Structure to hold parsed arguments */
struct arguments {
   char mode[10];
   u8 tiles;
};

/* Argument parser function */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
   struct arguments *arguments = state->input;

   switch (key) {
      case 't': // Handle --tiles option
         if (strncmp(arg, "0x", 2) == 0) {
            arguments->tiles = strtol(arg, NULL, 16); // Hex input
         } else {
            arguments->tiles = atoi(arg); // Decimal input
         }
         break;

      case ARGP_KEY_ARG: // Handle positional arguments (adc or dac)
         if (state->arg_num == 0) {
            strncpy(arguments->mode, arg, sizeof(arguments->mode) - 1);
            arguments->mode[sizeof(arguments->mode) - 1] = '\0';
         } else {
            argp_usage(state); // Too many arguments
         }
         break;

      case ARGP_KEY_END: // Ensure required argument (mode) is provided
         if (state->arg_num < 1) {
            argp_usage(state); // Too few arguments
         }
         break;

      default:
         return ARGP_ERR_UNKNOWN;
   }
   return 0;
}

/* Define the argument parser */
static struct argp argp = { options, parse_opt, args_doc, doc };

/* Main function */
int main(int argc, char *argv[]) {

   /****************************************************************************/

   struct arguments arguments;
   memset(&arguments, 0, sizeof(arguments)); // Initialize struct

   /* Default argument values */
   arguments.tiles = 0x0;

   /* Parse command-line arguments */
   argp_parse(&argp, argc, argv, 0, 0, &arguments);

   /****************************************************************************/

   int status;
   XRFdc_Config *ConfigPtr;
   XRFdc *RFdcInstPtr = &RFdcInst;
#ifndef __BAREMETAL__
   struct metal_device *deviceptr;
#endif
   struct metal_init_params init_param = METAL_INIT_DEFAULTS;

   if (metal_init(&init_param)) {
      printf("ERROR: Failed to run metal initialization\n");
      return XRFDC_FAILURE;
   }

   metal_set_log_level(METAL_LOG_DEBUG);
   ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
   if (ConfigPtr == NULL) {
      printf("RFdc Config Failure\n\r");
      return XRFDC_FAILURE;
   }

#ifndef __BAREMETAL__
	status = XRFdc_RegisterMetal(RFdcInstPtr, RFDC_DEVICE_ID, &deviceptr);
	if (status != XRFDC_SUCCESS) {
		return XRFDC_FAILURE;
	}
#endif

   status = XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);
   if (status != XRFDC_SUCCESS) {
      printf("RFdc Init Failure\n\r");
      // Note: No return on status=error in example script
      // https://github.com/Xilinx/embeddedsw/blob/xilinx_v2024.2/XilinxProcessorIPLib/drivers/rfdc/examples/xrfdc_mts_example.c#L163
   }

   /****************************************************************************/

    if (strcmp(arguments.mode, "mts-adc") == 0) {
        status = RFdcMTSAdc(RFdcInstPtr,arguments.tiles);
    } else if (strcmp(arguments.mode, "mts-dac") == 0) {
        status = RFdcMTSDac(RFdcInstPtr,arguments.tiles);
    } else {
        printf("Invalid mode! Use 'mts-adc' or 'mts-dac'.\n");
        return XRFDC_FAILURE;
    }

    if (status != XRFDC_SUCCESS) {
        printf("MTS Example Test failed\n");
        return XRFDC_FAILURE;
    }

    return XRFDC_SUCCESS;
}
