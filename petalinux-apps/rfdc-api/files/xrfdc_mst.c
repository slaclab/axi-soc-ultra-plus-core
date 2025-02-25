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
#define RFDC_DEVICE_ID  XPAR_XRFDC_0_DEVICE_ID
#else
#define RFDC_DEVICE_ID  0
#endif

#define RFDC_FAILURE -1

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
   metal_log(METAL_LOG_INFO, "rfdc-mst: \n=== Run ADC Sync ===\n");

   /* Initialize ADC MTS Settings */
   XRFdc_MultiConverter_Init (&ADC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
   ADC_Sync_Config.Tiles = tiles;

   status_adc = XRFdc_MultiConverter_Sync(RFdcInstPtr, XRFDC_ADC_TILE, &ADC_Sync_Config);
   if(status_adc == XRFDC_MTS_OK){
      metal_log(METAL_LOG_INFO, "rfdc-mst: ADC Multi-Tile-Sync completed successfully\n");
   } else {
      metal_log(METAL_LOG_ERROR, "rfdc-mst: ADC Multi-Tile-Sync did not complete successfully. Error code is %u\n", status_adc);
      return status_adc;
   }

   /*
   * Report Overall Latency in T1 (Sample Clocks) and
   * Offsets (in terms of PL words) added to each FIFO
   */
   metal_log(METAL_LOG_INFO, "rfdc-mst: \n\n=== Multi-Tile Sync Report ===\n");
   for(i=0; i<4; i++) {
      if( (1<<i)&ADC_Sync_Config.Tiles ) {
         XRFdc_GetDecimationFactor(RFdcInstPtr, i, 0, &factor);
         metal_log(METAL_LOG_INFO, "rfdc-mst: ADC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, ADC_Sync_Config.Latency[i], factor, ADC_Sync_Config.Offset[i]);
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
   metal_log(METAL_LOG_INFO, "rfdc-mst: \n=== Run DAC Sync ===\n");

   /* Initialize DAC MTS Settings */
   XRFdc_MultiConverter_Init (&DAC_Sync_Config, 0, 0, XRFDC_TILE_ID0);
   DAC_Sync_Config.Tiles = tiles;

   status_dac = XRFdc_MultiConverter_Sync(RFdcInstPtr, XRFDC_DAC_TILE, &DAC_Sync_Config);
   if(status_dac == XRFDC_MTS_OK){
      metal_log(METAL_LOG_INFO, "rfdc-mst: DAC Multi-Tile-Sync completed successfully\n");
   }else{
      metal_log(METAL_LOG_ERROR, "rfdc-mst: DAC Multi-Tile-Sync did not complete successfully. Error code is %u \n", status_dac);
      return status_dac;
   }

   /*
   * Report Overall Latency in T1 (Sample Clocks) and
   * Offsets (in terms of PL words) added to each FIFO
   */
   metal_log(METAL_LOG_INFO, "rfdc-mst: \n\n=== Multi-Tile Sync Report ===\n");
   for(i=0; i<4; i++) {
      if((1<<i)&DAC_Sync_Config.Tiles) {
         XRFdc_GetInterpolationFactor(RFdcInstPtr, i, 0, &factor);
         metal_log(METAL_LOG_INFO, "rfdc-mst: DAC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d\n", i, DAC_Sync_Config.Latency[i], factor, DAC_Sync_Config.Offset[i]);
      }
   }

   /* Return completed successfully */
   return XRFDC_MTS_OK;
}

/****************************************************************************/

/* Program documentation */
const char *argp_program_version = "RFDC MST 1.0";
const char *argp_program_bug_address = "https://github.com/slaclab/axi-soc-ultra-plus-core";
static char doc[] = "RFDC MST for Linux CLI";
static char args_doc[] = "<adc|dac> --tiles=0xF --debugPrint=0";

/* Available options */
static struct argp_option options[] = {
   {"tiles",      't', "VALUE", 0, "Set tiles (decimal or hex, e.g., 0xF or 15)"},
   {"debugPrint", 'd', "VALUE", 0, "Enable debug prints (1: debug, 0: error)"},
   { 0 } // Indicates end of options
};

/* Structure to hold parsed arguments */
struct arguments {
   char mode[10];
   u8 tiles;
   u8 debugPrint;
};

/* Argument parsing function */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *args = state->input;

   switch (key) {
      case 't':
         args->tiles = (u32) strtol(arg, NULL, 0);
         break;
      case 'd':
         args->debugPrint = (u8) strtol(arg, NULL, 0);
         break;
       case ARGP_KEY_ARG:
         if (state->arg_num == 0)
            strncpy(args->mode, arg, sizeof(args->mode) - 1);
         else
            argp_usage(state); // Too many arguments
         break;
      case ARGP_KEY_END:
         if (state->arg_num < 1)
            argp_usage(state); // Too few arguments
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

   int status;
   XRFdc_Config *ConfigPtr;
   XRFdc *RFdcInstPtr = &RFdcInst;
#ifndef __BAREMETAL__
   struct metal_device *deviceptr;
#endif
   struct metal_init_params init_param = METAL_INIT_DEFAULTS;

   if (metal_init(&init_param)) {
      metal_log(METAL_LOG_ERROR, "rfdc-mst: Failed to run metal initialization\n");
      return RFDC_FAILURE;
   }

   ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
   if (ConfigPtr == NULL) {
      metal_log(METAL_LOG_ERROR, "rfdc-mst: RFdc Config Failure\n\r");
      return RFDC_FAILURE;
   }

#ifndef __BAREMETAL__
   status = XRFdc_RegisterMetal(RFdcInstPtr, RFDC_DEVICE_ID, &deviceptr);
   if (status != XRFDC_SUCCESS) {
      return RFDC_FAILURE;
   }
#endif

   XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);

   /****************************************************************************/

   struct arguments args = { .tiles = 0, .debugPrint = 0 };

   /* Default values */
   strncpy(args.mode, "unset", sizeof(args.mode) - 1);
   args.mode[sizeof(args.mode) - 1] = '\0';

   /* Parse arguments */
   argp_parse(&argp, argc, argv, 0, 0, &args);

   /* Set log level based on debugPrint flag */
   if (args.debugPrint) {
      metal_set_log_level(METAL_LOG_DEBUG);
   } else {
      metal_set_log_level(METAL_LOG_ERROR);
   }

   /****************************************************************************/

   u8 retVar = args.tiles&0xF; // 4-bit Mask
   if (strcmp(args.mode, "adc") == 0) {
      status = RFdcMTSAdc(RFdcInstPtr, retVar);
   } else if (strcmp(args.mode, "dac") == 0) {
      status = RFdcMTSDac(RFdcInstPtr, retVar);
   } else {
      metal_log(METAL_LOG_ERROR, "rfdc-mst: Invalid mode! Use 'adc' or 'dac'.\n");
      return RFDC_FAILURE;
   }

   if (status != XRFDC_SUCCESS) {
      metal_log(METAL_LOG_ERROR, "rfdc-mst: failed\n");
      return RFDC_FAILURE;
   }

   return retVar;
}
