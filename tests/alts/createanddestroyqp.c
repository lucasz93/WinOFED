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
alts_qp(
	boolean_t modify_cq_attr
	);



/*
 * Test Case CrateDestroyQP
 */


ib_api_status_t
al_test_create_destroy_qp(void)
{
	boolean_t modify_qp_attr = FALSE;

	return alts_qp(modify_qp_attr);
}

ib_api_status_t
al_test_query_modify_qp(void)
{
	boolean_t modify_qp_attr = TRUE;

	return alts_qp(modify_qp_attr);
}


/* Internal Functions */

ib_api_status_t
alts_qp(
	boolean_t modify_cq_attr
	)
{
	ib_api_status_t	ib_status = IB_SUCCESS;
	ib_al_handle_t		h_al = NULL;
	ib_ca_handle_t	h_ca;
	ib_cq_handle_t	h_cq = NULL;
	ib_qp_handle_t	h_qp = NULL;	
	ib_pd_handle_t	h_pd;	
	cl_status_t		cl_status;
	ib_cq_create_t	cq_create;
	ib_qp_create_t	qp_create;	
	uint32_t			bsize; 
	ib_ca_attr_t		*p_ca_attr = NULL;
	ib_qp_attr_t		p_qp_attr;


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

		/*
		 * Get the CA Attributest
		 * Check for two active ports
		 */

		/*
		 * Query the CA
		 */
		bsize = 0;
		ib_status = ib_query_ca(h_ca, NULL, &bsize);
		if(ib_status != IB_INSUFFICIENT_MEMORY)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}
		CL_ASSERT(bsize);



		p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
		if (!p_ca_attr)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("zalloc() failed for p_ca_attr!\n") );
			break;
		}

		ib_status = ib_query_ca(h_ca, p_ca_attr, &bsize);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("ib_query_ca failed with status = %d\n", ib_status) );
			break;
		}




		/*
		 * Create CQ Attributes
		 */
		cq_create.size = ALTS_CQ_SIZE;
		cq_create.pfn_comp_cb = alts_cq_comp_cb;
		cq_create.h_wait_obj = NULL;

		ib_status = ib_create_cq(
								h_ca,
								&cq_create,
								NULL,
								alts_cq_err_cb,
								&h_cq );
		CL_ASSERT(ib_status == IB_SUCCESS);


	
		/*
		 * Create QP Attributes
		 */
		cl_memclr(&qp_create, sizeof(ib_qp_create_t));
		qp_create.sq_depth= 1;
		qp_create.rq_depth= 1;
		qp_create.sq_sge	= 1;
		qp_create.rq_sge	= 1;
		qp_create.h_sq_cq	= h_cq; //NULL
		qp_create.h_rq_cq	= h_cq;

		qp_create.sq_signaled = TRUE;

		qp_create.qp_type = IB_QPT_RELIABLE_CONN;

		
		ib_status = ib_create_qp(
			h_pd,
			&qp_create,
			NULL,
			alts_qp_err_cb,
			&h_qp);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("Error in ib_create_qp()! %s\n",
				ib_get_err_str(ib_status)));
			ALTS_EXIT(ALTS_DBG_VERBOSE);
			return (ib_status);
		}

		ib_status = ib_query_qp(h_qp,
							&p_qp_attr);

		if (ib_status != IB_SUCCESS)
		{
			ALTS_PRINT(ALTS_DBG_ERROR,
				("Error in query_qp()! %s\n",
				ib_get_err_str(ib_status)));

			ib_destroy_qp(h_qp,alts_qp_destroy_cb);
			ALTS_EXIT(ALTS_DBG_VERBOSE);
			return (ib_status);
		}

		ib_status = ib_destroy_qp(h_qp, alts_qp_destroy_cb);

		if (h_cq)
			ib_status = ib_destroy_cq(h_cq, alts_qp_destroy_cb);


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
alts_qp_err_cb(
	ib_async_event_rec_t				*p_err_rec )
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}

void
alts_qp_destroy_cb(
	void	*context
	)
{
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( context );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}




