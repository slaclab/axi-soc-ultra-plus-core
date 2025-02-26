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

/***************** Macros (Inline Functions) Definitions ********************/
#ifdef __BAREMETAL__
#define printf xil_printf
#endif

/************************** Variable Definitions ****************************/
static XRFdc RFdcInst;      /* RFdc driver instance */

/****************************************************************************/

/* Program documentation */
const char *argp_program_version = "RFDC ThresholdSettings 1.0";
const char *argp_program_bug_address = "https://github.com/slaclab/axi-soc-ultra-plus-core";
static char doc[] = "RFDC ThresholdSettings for Linux CLI";
static char args_doc[] = "<set|get> --tile=0x1 --block=0x2 --index=3 --setValue=0x4 --debugPrint=0";

/* Available options */
static struct argp_option options[] = {
   {"tile",     't', "VALUE", 0, "tile Index"},
   {"block",    'b', "VALUE", 0, "block Index"},
   {"index",    'i', "VALUE", 0, "threshold Index"},
   {"setValue", 's', "VALUE", 0, "Set Value"},
   {"debugPrint", 'd', "VALUE", 0, "Enable debug prints (1: debug, 0: error)"},
   { 0 } // Indicates end of options
};

/* Structure to hold parsed arguments */
struct arguments {
   char mode[10];
   u32 tile;
   u32 block;
   u32 index;
   u32 setValue;
   u8 debugPrint;
};

/* Argument parsing function */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *args = state->input;

   switch (key) {
      case 't':
         args->tile = (u32) strtol(arg, NULL, 0);
         break;
      case 'b':
         args->block = (u32) strtol(arg, NULL, 0);
         break;
      case 'i':
         args->index = (u32) strtol(arg, NULL, 0);
         break;
      case 's':
         args->setValue = (u32) strtol(arg, NULL, 0);
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
      metal_log(METAL_LOG_ERROR, "rfdc-ThresholdSettings: Failed to run metal initialization\n");
      return RFDC_FAILURE;
   }

   ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
   if (ConfigPtr == NULL) {
      metal_log(METAL_LOG_ERROR, "rfdc-ThresholdSettings: RFdc Config Failure\n\r");
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

   struct arguments args = { .tile = 0, .block = 0, .setValue = 0, .index = 0, .debugPrint = 0 };

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

   // Get the current values of ThresholdSettings settings
   XRFdc_Threshold_Settings  ThresholdSettingsPtr;
   if (XRFdc_GetThresholdSettings(RFdcInstPtr, args.tile, args.block, &ThresholdSettingsPtr) != XRFDC_SUCCESS) {
      metal_log(METAL_LOG_ERROR, "rfdc-ThresholdSettings: RFDC ThresholdSettings failed\n");
      return RFDC_FAILURE;
   }

   u32 retVar = args.setValue;
   if (strcmp(args.mode, "set") == 0) {

      ThresholdSettingsPtr.UpdateThreshold = XRFDC_UPDATE_THRESHOLD_BOTH;

      switch (args.index & 0x7) {
         case 0:
            ThresholdSettingsPtr.ThresholdMode[0] = args.setValue&0x3; // Range: 0 to 3 (0-OFF, 1-sticky-over, 2-sticky-under and 3-hysteresis)
            break;
         case 1:
            ThresholdSettingsPtr.ThresholdMode[1] = args.setValue&0x3; // Range: 0 to 3 (0-OFF, 1-sticky-over, 2-sticky-under and 3-hysteresis)
            break;
         case 2:
            ThresholdSettingsPtr.ThresholdAvgVal[0] = args.setValue;
            break;
         case 3:
            ThresholdSettingsPtr.ThresholdAvgVal[1] = args.setValue;
            break;
         case 4:
            ThresholdSettingsPtr.ThresholdUnderVal[0] = args.setValue;
            break;
         case 5:
            ThresholdSettingsPtr.ThresholdUnderVal[1] = args.setValue;
            break;
         case 6:
            ThresholdSettingsPtr.ThresholdOverVal[0] = args.setValue;
            break;
         case 7:
            ThresholdSettingsPtr.ThresholdOverVal[1] = args.setValue;
            break;
         default:
            break;
      }

      status = XRFdc_SetThresholdSettings(RFdcInstPtr, args.tile, args.block, &ThresholdSettingsPtr);

   } else if (strcmp(args.mode, "get") == 0) {

      switch (args.index & 0x7) {
         case 0:
            retVar = ThresholdSettingsPtr.ThresholdMode[0];
            break;
         case 1:
            retVar = ThresholdSettingsPtr.ThresholdMode[1];
            break;
         case 2:
            retVar = ThresholdSettingsPtr.ThresholdAvgVal[0];
            break;
         case 3:
            retVar = ThresholdSettingsPtr.ThresholdAvgVal[1];
            break;
         case 4:
            retVar = ThresholdSettingsPtr.ThresholdUnderVal[0];
            break;
         case 5:
            retVar = ThresholdSettingsPtr.ThresholdUnderVal[1];
            break;
         case 6:
            retVar = ThresholdSettingsPtr.ThresholdOverVal[0];
            break;
         case 7:
            retVar = ThresholdSettingsPtr.ThresholdOverVal[1];
            break;
         default:
            break;
      }

   } else {
      metal_log(METAL_LOG_ERROR, "rfdc-ThresholdSettings: Invalid mode! Use 'set' or 'get'.\n");
      return RFDC_FAILURE;
   }

   if (status != XRFDC_SUCCESS) {
      metal_log(METAL_LOG_ERROR, "rfdc-ThresholdSettings: failed\n");
      return RFDC_FAILURE;
   }

   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdMode[0]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdMode[0]);
   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdMode[1]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdMode[1]);

   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdAvgVal[0]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdAvgVal[0]);
   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdAvgVal[1]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdAvgVal[1]);

   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdUnderVal[0]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdUnderVal[0]);
   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdUnderVal[1]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdUnderVal[1]);

   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdOverVal[0]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdOverVal[0]);
   metal_log(METAL_LOG_INFO, "ThresholdSettings(tile=0x%X,block=0x%X).ThresholdOverVal[1]=0x%X\n", args.tile, args.block, ThresholdSettingsPtr.ThresholdOverVal[1]);

   return retVar;
}
