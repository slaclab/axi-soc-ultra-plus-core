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
const char *argp_program_version = "RFDC NyquistZone 1.0";
const char *argp_program_bug_address = "https://github.com/slaclab/axi-soc-ultra-plus-core";
static char doc[] = "RFDC NyquistZone for Linux CLI";
static char args_doc[] = "<set|get> <adc|dac> --tile=0x1 --block=0x3 --setValue=0x0" ;

/* Available options */
static struct argp_option options[] = {
   {"tile",     't', "VALUE", 0, "tile Index"},
   {"block",    'b', "VALUE", 0, "block Index"},
   {"setValue", 's', "VALUE", 0, "Set Value"},
   { 0 } // Indicates end of options
};

/* Structure to hold parsed arguments */
struct arguments {
   char mode[10];
   char type[10];
   u32 tile;
   u32 block;
   u32 setValue;
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
         args->setValue = (u32) strtol(arg, NULL, 0);
         break;
       case ARGP_KEY_ARG:
         if (state->arg_num == 0)
            strncpy(args->mode, arg, sizeof(args->mode) - 1);
         else if (state->arg_num == 1)
            strncpy(args->type, arg, sizeof(args->type) - 1);
         else
            argp_usage(state); // Too many arguments
         break;
      case ARGP_KEY_END:
         if (state->arg_num < 2)
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
      return RFDC_FAILURE;
   }

   metal_set_log_level(METAL_LOG_ERROR);
   ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
   if (ConfigPtr == NULL) {
      printf("RFdc Config Failure\n\r");
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

   struct arguments args = { .tile = 0, .block = 0, .setValue = 0 };

   /* Default values */
   strncpy(args.mode, "unset", sizeof(args.mode) - 1);
   strncpy(args.type, "unset", sizeof(args.type) - 1);

   args.mode[sizeof(args.mode) - 1] = '\0';
   args.type[sizeof(args.type) - 1] = '\0';

   /* Parse arguments */
   argp_parse(&argp, argc, argv, 0, 0, &args);

   /****************************************************************************/

   u32 Type = 0;
   if (strcmp(args.type, "adc") == 0) {
      Type = XRFDC_ADC_TILE;
   } else if (strcmp(args.type, "dac") == 0) {
      Type = XRFDC_DAC_TILE;
   } else {
      printf("Invalid type! Use 'adc' or 'dac'.\n");
      return RFDC_FAILURE;
   }

   u8 retVar = args.setValue&0x3; // 2-bit Mask
   if (strcmp(args.mode, "set") == 0) {
      printf("XRFdc_SetNyquistZone Value: 0x%X\n", retVar);
      status = XRFdc_SetNyquistZone(RFdcInstPtr, Type, args.tile, args.block, retVar);
   } else if (strcmp(args.mode, "get") == 0) {
      status = XRFdc_GetNyquistZone(RFdcInstPtr, Type, args.tile, args.block, &retVar);
      printf("XRFdc_GetNyquistZone Value: 0x%X\n", retVar);
   } else {
      printf("Invalid mode! Use 'set' or 'get'.\n");
      return RFDC_FAILURE;
   }

   if (status != XRFDC_SUCCESS) {
      printf("RFDC NyquistZone failed\n");
      return RFDC_FAILURE;
   }

   return retVar;
}
