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


/* Test case PARAMETERS */

#define MEM_ALLIGN 32
#define MEM_SIZE 1024


/*
 * Function prototypes
 */


/*
 * Test Case RegisterMemRegion
 */

ib_api_status_t
al_test_create_mem_window(
	void
	)
{
	ib_api_status_t ib_status = IB_SUCCESS;
	ib_al_handle_t h_al = NULL;
	ib_ca_handle_t h_ca = NULL;
	ib_pd_handle_t h_pd = NULL;

	ib_mr_create_t virt_mem;
	char *ptr = NULL, *ptr_align;
	size_t		mask; 
	uint32_t	lkey;
	uint32_t	rkey;
	ib_mr_handle_t	h_mr = NULL;
	ib_mr_attr_t	alts_mr_attr;

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
				("ib_alloc_pd failed status = %d", ib_status) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Allocate the virtual memory which needs to be registered
		 */

		mask = MEM_ALLIGN - 1;

		ptr = cl_malloc(MEM_SIZE + MEM_ALLIGN - 1);

		CL_ASSERT(ptr);

		ptr_align = ptr;

		if(((size_t)ptr & mask) != 0)
			ptr_align = (char *)(((size_t)ptr+mask)& ~mask);

		virt_mem.vaddr = ptr_align;
		virt_mem.length = MEM_SIZE;
		virt_mem.access_ctrl = (IB_AC_LOCAL_WRITE | IB_AC_MW_BIND);

		/*
		 * Register the memory region
		 */

		ib_status = ib_reg_mem(h_pd, &virt_mem, &lkey, &rkey, &h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_reg_mem failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Query the memory region
		 */
		ib_status = ib_query_mr(h_mr, &alts_mr_attr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Re-register the memeory region
		 */
		virt_mem.access_ctrl |= (IB_AC_RDMA_WRITE );

		ib_status = ib_rereg_mem(h_mr,IB_MR_MOD_ACCESS,
			&virt_mem,&lkey,&rkey,NULL);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_rereg_mem failed status = %s\n", ib_get_err_str(ib_status)) );
		}
		/*
		 * Create, Query and Destroy the memory window
		 */

		{

			uint32_t rkey_mw;
			ib_mw_handle_t h_mw;

			ib_pd_handle_t h_pd_query;
			uint32_t rkey_mw_query;

			ib_status = ib_create_mw(h_pd,&rkey_mw,&h_mw);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_create_mw failed status = %s\n",ib_get_err_str(ib_status)) );
				alts_close_ca(h_ca);
				break;
			}

			ib_status = ib_query_mw(h_mw,&h_pd_query,&rkey_mw_query);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_query_mw failed status = %s\n", ib_get_err_str(ib_status)) );
				alts_close_ca(h_ca);
				break;
			}

			ib_status = ib_destroy_mw(h_mw);
			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_destroy_mw failed status = %s\n", ib_get_err_str(ib_status)) );
				alts_close_ca(h_ca);
				break;
			}

		}

		/*
		 * De-register the memory region
		 */
		ib_status = ib_dereg_mr(h_mr);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dereg_mr failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		/*
		 * Deallocate the PD
		 */

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dealloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}

		break; //End of while
	}

	if( ptr )
		cl_free( ptr );

	/* Close AL */
	if(h_al)
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
