/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


/*
 * Abstract:
 *	This is the main c file for the AL test suite application
 *
 * Environment:
 *	User Mode
 */


#include "stdio.h"
#include "string.h"
#include "stdlib.h"


#include <iba/ib_types.h>
#include "alts_debug.h"
#include "alts_common.h"

//#include <complib/cl_syshelper.h>
//#include <complib/cl_memory.h>


//#define COMPILE_USER_MODE
#define strcasecmp	lstrcmpi
#define strncasecmp( s1, s2, l )	CompareString( LOCALE_USER_DEFAULT, NORM_IGNORECASE, s1, strlen(s1), s2, l )

#if !defined( FALSE )
#define FALSE 0
#endif /* !defined( FALSE ) */

#if !defined( TRUE )
#define TRUE 1
#endif /* !defined( TRUE ) */

/*
 * Global Varables
 */

//Global Debug level
uint32_t	alts_dbg_lvl = ALTS_DBG_FULL;

/*
 * Data structure
 */


/*
 * Function Prototype
 */
boolean_t
parse_cmd_line(
	cmd_line_arg_t *input_arg,
	int argc,
	char **argv
	);

void
usage(
	void);


#ifndef CL_KERNEL
void
run_ual_test(
	cmd_line_arg_t *cmd_line_arg
	);
#endif

void
run_kal_test(
	cmd_line_arg_t *cmd_line_arg
	);



/*******************************************************************
*******************************************************************/


int32_t __cdecl
main(
	int32_t argc,
	char* argv[])
{
	boolean_t isvalid = FALSE;
	cmd_line_arg_t cmd_line_arg={0};

	CL_ENTER( ALTS_DBG_VERBOSE, alts_dbg_lvl );

	cl_memclr(&cmd_line_arg,sizeof(cmd_line_arg));

	isvalid = parse_cmd_line(&cmd_line_arg,
		argc, argv);

	if(cmd_line_arg.pgm_to_run == 0)
	{
		CL_PRINT( ALTS_DBG_ERROR, alts_dbg_lvl,
			("Command line parse failed\n") );
		usage();
		return 0;
	}

	if(cmd_line_arg.um == TRUE)
	{
		{
		#ifndef CL_KERNEL
		run_ual_test(&cmd_line_arg);
		#else
		CL_PRINT( ALTS_DBG_ERROR, alts_dbg_lvl,
			("User Mode test not COMPILED.\n #define COMPILE_USER_MODE to build for usr mode\n") );
		#endif
		}
	}
	else
	{
		CL_PRINT( ALTS_DBG_ERROR, alts_dbg_lvl,
			("Kernel mode test not supported\n") );
		//run_kal_test(&cmd_line_arg);
	}
	CL_EXIT( ALTS_DBG_VERBOSE, alts_dbg_lvl );

	return 0;
}

#ifndef CL_KERNEL
void
run_ual_test(cmd_line_arg_t *cmd_line_arg)
{
ib_api_status_t ib_status = IB_ERROR;

	switch(cmd_line_arg->pgm_to_run)
	{
	case OpenClose:
		ib_status = al_test_openclose();
		break;
	case QueryCAAttribute:
		ib_status = al_test_querycaattr();
		break;
	case ModifyCAAttribute:
		ib_status = al_test_modifycaattr();
		break;
	case AllocDeallocPD:
		ib_status = al_test_alloc_dealloc_pd();
		break;
	case CreateDestroyAV:
		ib_status = al_test_create_destroy_av();
		break;
	case QueryAndModifyAV:
		ib_status = al_test_query_modify_av();
		break;
	case CreateDestroyQP:
		ib_status = al_test_create_destroy_qp();
		break;
	case QueryAndModifyQP:
		CL_PRINT( ALTS_DBG_VERBOSE, alts_dbg_lvl,
			("altsapp: QueryAndModifyQP not implemented.\n") );
		ib_status = IB_SUCCESS;
		break;
	case CreateAndDestroyCQ:
		ib_status = al_test_create_destroy_cq();
		break;
	case QueryAndModifyCQ:
		ib_status = al_test_query_modify_cq();
		break;
	case AttachMultiCast:
		CL_PRINT( ALTS_DBG_VERBOSE, alts_dbg_lvl,
			("altsapp: AttachMultiCast not implemented.\n") );
		ib_status = IB_SUCCESS;
		break;
	case RegisterMemRegion:
		ib_status = al_test_register_mem();
		break;
	case RegisterVarMemRegions:
		ib_status = al_test_register_var_mem();
		break;
	case RegisterPhyMemRegion:
		CL_PRINT( ALTS_DBG_VERBOSE, alts_dbg_lvl,
			("altsapp: RegisterPhyMemRegion not implemented.\n") );
		ib_status = IB_SUCCESS;
		break;
	case CreateMemWindow:
		ib_status = al_test_create_mem_window();
		break;
	case RegisterSharedMemRegion:
		ib_status = al_test_register_shared_mem();
		break;
	case MultiSend:
		ib_status = al_test_multi_send_recv();
		break;
	case RegisterPnP:
		ib_status = al_test_register_pnp();
		break;
	case MadTests:
		ib_status = al_test_mad();
		break;
	case MadQuery:
		ib_status = al_test_query();
		break;
	case CmTests:
		ib_status = al_test_cm();
		break;

	case MaxTestCase:
		break;
	default:
		break;
	}
	if(ib_status != IB_SUCCESS)
	{
		printf("********************************\n");
		printf("altsapp:AL test failed\n");
		printf("********************************\n");
	}
	else
	{
		printf("********************************\n");
		printf("altsapp:AL test passed\n");
		printf("********************************\n");
	}

}
#endif

//void
//run_kal_test(cmd_line_arg_t *cmd_line_arg)
//{
//
//	cl_dev_handle_t h_al_test;
//	cl_status_t cl_status;
//	uint32_t command;
//	uintn_t inbufsz = 0;
//	uintn_t outbufsz = 0;
//
//	CL_ENTER( ALTS_DBG_VERBOSE, alts_dbg_lvl );
//
//	cl_status = cl_open_device(ALTS_DEVICE_NAME, &h_al_test);
//
//	if(cl_status != CL_SUCCESS)
//	{
//		printf("altsapp:cl_open_device failed\n");
//		CL_EXIT( ALTS_DBG_VERBOSE, alts_dbg_lvl );
//		return;
//	}
//
//	command = IOCTL_CMD(ALTS_DEV_KEY, cmd_line_arg->pgm_to_run);
//	inbufsz = sizeof(cmd_line_arg_t);
//
//
//	cl_status = cl_ioctl_device(
//		h_al_test,
//		command,
//		cmd_line_arg,
//		inbufsz,
//		&outbufsz);
//
//
//	if(cl_status != CL_SUCCESS)
//	{
//		printf("********************************\n");
//		printf("altsapp:AL test failed\n");
//		printf("********************************\n");
//
//		CL_EXIT( ALTS_DBG_VERBOSE, alts_dbg_lvl );
//		return;
//	}
//
//	if(cmd_line_arg->status == IB_SUCCESS)
//	{
//		printf("********************************\n");
//		printf("altsapp:AL test passed\n");
//		printf("********************************\n");
//	}
//	else
//	{
//		printf("********************************\n");
//		printf("altsapp:AL test failed\n");
//		printf("********************************\n");
//	}
//
//	cl_close_device(h_al_test);
//	CL_EXIT( ALTS_DBG_VERBOSE, alts_dbg_lvl );
//
//}

/*
 * Command Line Parser Routine
 */

boolean_t parse_cmd_line(
	cmd_line_arg_t *input_arg,
	int argc,
	char **argv
	)
{
	size_t	i,n,k,j;
	char temp[256];
	int Value;

	if (argc <= 1 || (NULL==argv))
		return FALSE;

	input_arg->pgm_to_run = 0; //Set to Zero

	i = argc;
	while (--i != 0)
	{
	/*
	 * Check for all the test case name
	 */
		++argv;
		if (strcasecmp(*argv, "--tc=OpenClose") == 0)
		{
			input_arg->pgm_to_run = OpenClose;
			continue;
		}
		if (strcasecmp(*argv, "--tc=QueryCAAttribute") == 0)
		{
			input_arg->pgm_to_run = QueryCAAttribute;
			continue;
		}
		if (strcasecmp(*argv, "--tc=ModifyCAAttribute") == 0)
		{
			input_arg->pgm_to_run = ModifyCAAttribute;
			continue;
		}
		if (strcasecmp(*argv, "--tc=AllocDeallocPD") == 0)
		{
			input_arg->pgm_to_run = AllocDeallocPD;
			continue;
		}
		if (strcasecmp(*argv, "--tc=CreateDestroyAV") == 0)
		{
			input_arg->pgm_to_run = CreateDestroyAV;
			continue;
		}
		if (strcasecmp(*argv, "--tc=QueryAndModifyAV") == 0)
		{
			input_arg->pgm_to_run = QueryAndModifyAV;
			continue;
		}
		if (strcasecmp(*argv, "--tc=CreateDestroyQP") == 0)
		{
			input_arg->pgm_to_run = CreateDestroyQP;
			continue;
		}
		if (strcasecmp(*argv, "--tc=QueryAndModifyQP") == 0)
		{
			input_arg->pgm_to_run = QueryAndModifyQP;
			continue;
		}
		if (strcasecmp(*argv, "--tc=CreateAndDestroyCQ") == 0)
		{
			input_arg->pgm_to_run = CreateAndDestroyCQ;
			continue;
		}
		if (strcasecmp(*argv, "--tc=QueryAndModifyCQ") == 0)
		{
			input_arg->pgm_to_run = QueryAndModifyCQ;
			continue;
		}
		if (strcasecmp(*argv, "--tc=AttachMultiCast") == 0)
		{
			input_arg->pgm_to_run = AttachMultiCast;
			continue;
		}
		if (strcasecmp(*argv, "--tc=RegisterMemRegion") == 0)
		{
			input_arg->pgm_to_run = RegisterMemRegion;
			continue;
		}
		if (strcasecmp(*argv, "--tc=RegisterVarMemRegions") == 0)
		{
			input_arg->pgm_to_run = RegisterVarMemRegions;
			continue;
		}
		if (strcasecmp(*argv, "--tc=ReregisterHca") == 0)
		{
			input_arg->pgm_to_run = ReregisterHca;
			continue;
		}
		if (strcasecmp(*argv, "--tc=RegisterPhyMemRegion") == 0)
		{
			input_arg->pgm_to_run = RegisterPhyMemRegion;
			continue;
		}
		if (strcasecmp(*argv, "--tc=CreateMemWindow") == 0)
		{
			input_arg->pgm_to_run = CreateMemWindow;
			continue;
		}
		if (strcasecmp(*argv, "--tc=RegisterSharedMemRegion") == 0)
		{
			input_arg->pgm_to_run = RegisterSharedMemRegion;
			continue;
		}
		if (strcasecmp(*argv, "--tc=MultiSend") == 0)
		{
			input_arg->pgm_to_run = MultiSend;
			continue;
		}
		if (strcasecmp(*argv, "--tc=RegisterPnP") == 0)
		{
			input_arg->pgm_to_run = RegisterPnP;
			continue;
		}
		if (strcasecmp(*argv, "--tc=MadTests") == 0)
		{
			input_arg->pgm_to_run = MadTests;
			continue;
		}
		if (strcasecmp(*argv, "--tc=MadQuery") == 0)
		{
			input_arg->pgm_to_run = MadQuery;
			continue;
		}
		if (strcasecmp(*argv, "--tc=CmTests") == 0)
		{
			input_arg->pgm_to_run = CmTests;
			continue;
		}


		/*
		 * Read Other parameter
		 */
		if (strcasecmp(*argv, "--um") == 0)
		{
			input_arg->um = TRUE;
			printf("altst:Running user mode test case\n");
			continue;
		}

		if (strcasecmp(*argv, "--km") == 0)
		{
			input_arg->um = FALSE;
			printf("altst:Running kernel mode test case\n");
			continue;
		}

		n = strlen(*argv);

		if (strncasecmp(*argv, "--Iteration=", j=strlen("--Iteration=")) == 0 )
		{
			k = 0;
			while (j < n){
				temp[k] = (*argv)[j++]; k++;}
			temp[k] = '\0';

			Value = atoi(temp);
			printf("/Iteration= %d", Value);

			if (Value < 0 || Value > 50)
				printf("Invalid Iteration specified\n");
			else
				printf("Valid Iteration specified\n");
			continue;
		}

	}

	return TRUE;
}

void
usage(void)
{
	printf("Usage: ./alts --tc=XXXXX [--um|--km]\n");
	printf("XXXX -> see the alts_readme.txt\n");
	printf("--um  ->  Usermode\n");
	printf("--km  ->  Kernelmode\n");
}
