/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_memory.h>
#include <alts_debug.h>
#include <alts_common.h>

/*
 * Function prototypes
 */



/*
 * Test Case AllocDeallocPD
 */

ib_api_status_t
al_test_alloc_dealloc_pd(void)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca;
	ib_pd_handle_t h_pd;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	while(1)
	{
		/* Open AL */
		ib_status = alts_open_al(&h_al);

		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_al);

		/* Open CA */
		ib_status = alts_open_ca(h_al,&h_ca);
		if(ib_status != IB_SUCCESS)
			break;

		CL_ASSERT(h_ca);

		/*
		 * Allocate a PD here
		 */
		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}
		ALTS_PRINT( ALTS_DBG_INFO,
			("\tib_alloc_pd passed with handle = %p\n",h_pd));

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);

		if(ib_status == IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_INFO,
				("\tib_dealloc_pd passed\n"));
		}
		else
		{
			ALTS_PRINT( ALTS_DBG_INFO,
				("\tib_dealloc_pd failed with status = %s\n",ib_get_err_str(ib_status)));
		}

		break; //End of while
	}
	/* Close AL */
	if(h_al)
		alts_close_al(h_al);
	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}



