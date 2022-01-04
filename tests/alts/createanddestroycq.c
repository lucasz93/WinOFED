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
ib_api_status_t
alts_cq(
	boolean_t modify_cq_attr
	);

/*
 * Test Case CrateDestroyCQ
 */


ib_api_status_t
al_test_create_destroy_cq(void)
{
	boolean_t modify_cq_attr = FALSE;

	return alts_cq(modify_cq_attr);
}

ib_api_status_t
al_test_query_modify_cq(void)
{
	boolean_t modify_cq_attr = TRUE;

	return alts_cq(modify_cq_attr);
}


/* Internal Functions */

ib_api_status_t
alts_cq(
	boolean_t modify_cq_attr
	)
{
	ib_api_status_t	ib_status = IB_SUCCESS;
	ib_al_handle_t	h_al = NULL;
	ib_ca_handle_t	h_ca;
	ib_cq_handle_t	h_cq = NULL;
	cl_status_t		cl_status;
	ib_cq_create_t	cq_create;
	ib_pd_handle_t	h_pd;
	int				iteration = 0;
#ifdef CL_KERNEL
	cl_event_t		cq_event;

	cl_event_construct( &cq_event );
#endif

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
		 * Allocate a PD required for CQ
		 */
		ib_status = ib_alloc_pd(h_ca, IB_PDT_NORMAL, NULL, &h_pd); //passing null context

		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("\tib_alloc_pd failed status = %s\n", ib_get_err_str(ib_status)) );
			alts_close_ca(h_ca);
			break;
		}
		ALTS_PRINT( ALTS_DBG_ERROR,
			("\tib_pd handle = %p\n",h_pd));

		/* 1st pass: event callback, 2nd pass: wait object. */
		do
		{
			iteration++;

			/*
			 * Initialize the CQ creation attributes.
			 */
			cl_memclr( &cq_create, sizeof( ib_cq_create_t ) );
			if( iteration == 1 )
			{
				/* Event callback */
				cq_create.pfn_comp_cb = alts_cq_comp_cb;
			}
			else if( iteration == 2 )
#ifdef CL_KERNEL
			{
				cl_status = cl_event_init( &cq_event, FALSE );
				if( cl_status != CL_SUCCESS )
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
						("cl_event_init returned status %#x\n",
						cl_status) );
					break;
				}
				cq_create.h_wait_obj = &cq_event;
			}
#else
			{
				/* Wait Object */
				cl_status =
					cl_waitobj_create( TRUE, &cq_create.h_wait_obj );
				if( cl_status != CL_SUCCESS )
				{
					CL_PRINT( ALTS_DBG_ERROR, alts_dbg_lvl,
						("cl_create_wait_object failed status = 0x%x\n",
						cl_status) );
					break;
				}
			}
			else
			{
				/* Bogus wait object. */
				cq_create.h_wait_obj = (void*)(uintn_t)0xCDEF0000;
			}
#endif

			cq_create.size = ALTS_CQ_SIZE; //Size of the CQ // NOTENOTE

			/* Create CQ here */
			ib_status = ib_create_cq(h_ca, &cq_create,
				NULL,alts_cq_err_cb, &h_cq);

			/* Trap expected failure. */
			if( iteration == 3 && ib_status != IB_SUCCESS )
				break;

			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_create_cq failed status = %s\n",
					ib_get_err_str(ib_status)) );
				break;
			}
			CL_ASSERT(h_cq);
			ALTS_PRINT( ALTS_DBG_INFO,\
				("ib_create_cq successful size = 0x%x status = %s\n",
				cq_create.size, ib_get_err_str(ib_status)) );

			while( modify_cq_attr == TRUE )
			{
				/*
				 * Query and Modify CQ ATTR
				 */
				uint32_t cq_size;

				ib_status = ib_query_cq(h_cq,&cq_size);

				if(ib_status != IB_SUCCESS)
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
						("ib_query_cq failed status = %s\n",
						ib_get_err_str(ib_status)) );
					break;
				}

				if(cq_size != cq_create.size)
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
						("ib_query_cq failed cq_size=0x%x cq_create.cq_size=0x%x\n",
						cq_size,cq_create.size));
					ib_status = IB_INVALID_CQ_SIZE;
					break;
				}
				
				ALTS_PRINT( ALTS_DBG_INFO,
					("ib_query_cq cq_size = 0x%x\n", cq_size) );

				cq_size = 0x90;

				ib_status = ib_modify_cq(h_cq,&cq_size);
				if(ib_status != IB_SUCCESS)
				{
					ALTS_PRINT( ALTS_DBG_ERROR,
						("ib_modify_cq failed status = %s\n",
						ib_get_err_str(ib_status)) );
					break;
				}

				ALTS_PRINT( ALTS_DBG_INFO,
					("ib_modify_cq passed for cq_size = 0x%x\n", cq_size) );

				break; //Break for the while
			}

			ib_status = ib_destroy_cq(h_cq, alts_cq_destroy_cb);

			if(ib_status != IB_SUCCESS)
			{
				ALTS_PRINT( ALTS_DBG_ERROR,
					("ib_destroy_cq failed status = %s\n",
					ib_get_err_str(ib_status)) );
				break;
			}
			ALTS_PRINT( ALTS_DBG_INFO,\
				("ib_destroy_cq successful status = %s\n",
				ib_get_err_str(ib_status)));


#ifdef CL_KERNEL
		} while( iteration < 2 );

		cl_event_destroy( &cq_event );
#else
			if( cq_create.h_wait_obj )
			{
				cl_status = cl_waitobj_destroy( cq_create.h_wait_obj );
				if( cl_status != CL_SUCCESS )
				{
					CL_PRINT( ALTS_DBG_ERROR, alts_dbg_lvl,
						("cl_destroy_wait_object failed status = 0x%x",
						cl_status) );
				}
			}

		} while( iteration < 3 );
#endif

		ib_status = ib_dealloc_pd(h_pd,alts_pd_destroy_cb);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_dealloc_pd failed status = %s\n",
				ib_get_err_str(ib_status)) );
		}

		alts_close_ca(h_ca);

		break; //End of while
	}

	/* Close AL */
	if(h_al)
		alts_close_al(h_al);

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}

void
alts_cq_comp_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( h_cq );
	UNUSED_PARAM( cq_context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}


void
alts_cq_err_cb(
	ib_async_event_rec_t				*p_err_rec )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_cq_destroy_cb(
	void	*context
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}
