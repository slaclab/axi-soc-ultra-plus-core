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
const char *argp_program_version = "RFDC CalibrationMode 1.0";
const char *argp_program_bug_address = "https://github.com/slaclab/axi-soc-ultra-plus-core";
static char doc[] = "RFDC CalibrationMode for Linux CLI";
static char args_doc[] = "<set|get> --tile=0x1 --block=0x3 --setValue=0x0 --debugPrint=0";

/* Available options */
static struct argp_option options[] = {
   {"tile",       't', "VALUE", 0, "tile Index"},
   {"block",      'b', "VALUE", 0, "block Index"},
   {"setValue",   's', "VALUE", 0, "Set Value"},
   {"debugPrint", 'd', "VALUE", 0, "Enable debug prints (1: debug, 0: error)"},
   { 0 } // Indicates end of options
};

/* Structure to hold parsed arguments */
struct arguments {
   char mode[10];
   u32 tile;
   u32 block;
   u8 setValue;
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
      case 's':
         args->setValue = (u8) strtol(arg, NULL, 0);
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
      metal_log(METAL_LOG_ERROR, "rfdc-CalibrationMode: Failed to run metal initialization\n");
      return RFDC_FAILURE;
   }

   ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
   if (ConfigPtr == NULL) {
      metal_log(METAL_LOG_ERROR, "rfdc-CalibrationMode: RFdc Config Failure\n\r");
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

   struct arguments args = { .tile = 0, .block = 0, .setValue = 0, .debugPrint = 0 };

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

   u8 retVar = args.setValue&0x3; // 2-bit Mask
   if (strcmp(args.mode, "set") == 0) {
      metal_log(METAL_LOG_INFO, "XRFdc_SetCalibrationMode Value: 0x%X\n", retVar);
      status = XRFdc_SetCalibrationMode(RFdcInstPtr, args.tile, args.block, retVar);
   } else if (strcmp(args.mode, "get") == 0) {
      status = XRFdc_GetCalibrationMode(RFdcInstPtr, args.tile, args.block, &retVar);
      metal_log(METAL_LOG_INFO, "XRFdc_GetCalibrationMode Value: 0x%X\n", retVar);
   } else {
      metal_log(METAL_LOG_ERROR, "rfdc-CalibrationMode: Invalid mode! Use 'set' or 'get'.\n");
      return RFDC_FAILURE;
   }

   if (status != XRFDC_SUCCESS) {
      metal_log(METAL_LOG_ERROR, "rfdc-CalibrationMode: failed\n");
      return RFDC_FAILURE;
   }

   return retVar;
}
