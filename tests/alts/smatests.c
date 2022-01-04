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



/*
 * Abstract:
 *	mad test does a data transfer between two queue pairs created one
 *	on each port of the hca. In order for this test to work, two ports of the hca
 *	should be connected in a loop back and must be configured to ACTIVE PORT STATE.
 *
 *
 * Environment:
 *	All
 */


#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_memory.h>
#include <complib/cl_thread.h>
#include <alts_debug.h>
#include <alts_common.h>

extern ib_cq_create_t		cq_create_attr;
extern ib_qp_create_t		qp_create_attr;
extern ib_av_attr_t			av_attr;
extern ib_mad_svc_t		mad_svc;


typedef struct _alts_sma_object
{
	ib_api_status_t		status;
	ib_al_handle_t		h_al;
	ib_ca_handle_t		h_ca;
	ib_ca_attr_t		*p_ca_attr;
	ib_port_attr_t		*p_send_port_attr;

	ib_pd_handle_t		h_pd;

	ib_cq_handle_t		h_cq;
	uint32_t			cq_size;

	ib_pool_key_t		h_mad_pool;
	ib_qp_handle_t		h_qp0;

	ib_mad_svc_handle_t h_sma_mad_svc;

} alts_sma_object_t;


/* Function Prototype */
ib_api_status_t
alts_get_al_resource(
	alts_sma_object_t *p_alts_sma_obj
);

ib_api_status_t
alts_init_tst_resource_sma(
	alts_sma_object_t *p_alts_sma_obj
);
ib_api_status_t
alts_get_send_port(
	alts_sma_object_t *p_alts_sma_obj
);

void
sma_mad_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec
);
void
alts_sma_mad_svc_send_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element
);

void
alts_sma_mad_svc_recv_cb(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*mad_svc_context,
	IN				ib_mad_element_t			*p_mad_element
);

ib_api_status_t
alts_discover_fabric(
	alts_sma_object_t *p_alts_sma_obj
);



/**********************************************************
***********************************************************/
ib_api_status_t
al_test_sma(void)
{
	alts_sma_object_t *p_alts_sma_obj;
	ib_api_status_t		ib_status = IB_ERROR;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	/* Allocate Memory for the alts_sma;*/
	p_alts_sma_obj = (alts_sma_object_t *)cl_zalloc(sizeof(alts_sma_object_t));

	if(p_alts_sma_obj == NULL)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("cl_zalloc failed\n") );
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return IB_ERROR;
	}

	p_alts_sma_obj->cq_size = 255*2;

	do
	{
		/* Initialize the AL resources */
		ib_status = alts_open_al(&p_alts_sma_obj->h_al);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_al failed status = %d\n", ib_status) );
			break;
		}

		/*
		 * Default opens the first CA
		 */

		ib_status = alts_open_ca(p_alts_sma_obj->h_al, &p_alts_sma_obj->h_ca);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_open_ca failed status = %d\n", ib_status) );
			break;
		}

		ib_status = alts_get_al_resource(p_alts_sma_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_get_al_resource failed status = %d\n", ib_status) );
			break;
		}


		ib_status = alts_init_tst_resource_sma(p_alts_sma_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_init_tst_resource_sma failed status = %d\n", ib_status) );
			break;
		}

		ib_status = alts_discover_fabric(p_alts_sma_obj);
		if(ib_status != IB_SUCCESS)
		{
			ALTS_PRINT( ALTS_DBG_ERROR,
				("alts_discover_fabric failed status = %d\n", ib_status) );
			break;
		}
	}while(0);

	if(p_alts_sma_obj->h_ca)
		alts_close_ca(p_alts_sma_obj->h_ca);
	if(p_alts_sma_obj->h_al)
		alts_close_al(p_alts_sma_obj->h_al);


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
/*********************************************************
**********************************************************/
ib_api_status_t
alts_discover_fabric(alts_sma_object_t *p_alts_sma_obj)
{
	UNUSED_PARAM( p_alts_sma_obj );
	return IB_SUCCESS;
}
/*********************************************************
**********************************************************/
ib_api_status_t
alts_get_al_resource(alts_sma_object_t *p_alts_sma_obj)
{
	uint32_t bsize;
	ib_api_status_t ib_status;
	ib_ca_attr_t *p_ca_attr;
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	bsize = 0;
	ib_status = ib_query_ca(p_alts_sma_obj->h_ca, NULL, &bsize);
	if(ib_status != IB_INSUFFICIENT_MEMORY)
	{
		ALTS_PRINT(ALTS_DBG_ERROR,
			("ib_query_ca failed with status = %d\n", ib_status) );
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return ib_status;
	}
	CL_ASSERT(bsize);

	p_ca_attr = (ib_ca_attr_t *)cl_zalloc(bsize);
	if (!p_ca_attr)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("zalloc() failed for p_ca_attr!\n") );
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return IB_ERROR;
	}

	ib_status = ib_query_ca(p_alts_sma_obj->h_ca, p_ca_attr, &bsize);
	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("ib_query_ca failed with status = %d\n", ib_status) );
		cl_free(p_ca_attr);
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return IB_ERROR;
	}
	p_alts_sma_obj->p_ca_attr = p_ca_attr;

	ib_status = alts_get_send_port(p_alts_sma_obj);

	if(ib_status != IB_SUCCESS)
	{
		ALTS_PRINT( ALTS_DBG_ERROR,
			("alts_get_send_port failed with status = %d\n", ib_status) );
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return IB_ERROR;
	}

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return ib_status;
}
/*********************************************************
**********************************************************/

ib_api_status_t
alts_get_send_port(alts_sma_object_t *p_alts_sma_obj)
{
	ib_ca_attr_t	*p_ca_attr;
	ib_port_attr_t	*p_send_port_attr = NULL;
	ib_port_attr_t	*p_port_attr;
	uint32_t		i;
	ALTS_ENTER( ALTS_DBG_VERBOSE );

	p_ca_attr = p_alts_sma_obj->p_ca_attr;

	for(i=0; i<p_ca_attr->num_ports; i++)
	{
		p_port_attr = &p_ca_attr->p_port_attr[i];
		if ((p_port_attr->link_state == IB_LINK_ACTIVE) ||
			(p_port_attr->link_state == IB_LINK_INIT))
		{
			if (p_send_port_attr == NULL)
			{
					p_send_port_attr = p_port_attr;
					break;
			}
		}
	} //end of for

	if(p_send_port_attr == NULL)
	{
	/* No port is connected */
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return IB_ERROR;
	}

	p_alts_sma_obj->p_send_port_attr =	p_send_port_attr;

	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return IB_SUCCESS;
}
/*********************************************************
**********************************************************/

ib_api_status_t
alts_init_tst_resource_sma(alts_sma_object_t *p_alts_sma_obj)
{
	ib_api_status_t ib_status;

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	/*
	 * Create the necessary resource PD/QP/QP
	 */

	/*
	 * Allocate a PD
	 */
	ib_status = ib_alloc_pd(
		p_alts_sma_obj->h_ca,
		IB_PDT_ALIAS,
		p_alts_sma_obj,	//pd_context
		&p_alts_sma_obj->h_pd);

	CL_ASSERT(ib_status == IB_SUCCESS);

	/*
	 * Create QP Attributes
	 */
	cl_memclr(&qp_create_attr, sizeof(ib_qp_create_t));

	qp_create_attr.sq_depth	= 10;
	qp_create_attr.rq_depth	= 10;
	qp_create_attr.sq_sge	= 1;
	qp_create_attr.rq_sge	= 1;
	qp_create_attr.h_sq_cq	= NULL;
	qp_create_attr.h_rq_cq	= NULL;

	qp_create_attr.sq_signaled = TRUE;

	qp_create_attr.qp_type = IB_QPT_QP0_ALIAS;

	ib_status = ib_get_spl_qp(
		p_alts_sma_obj->h_pd,
		p_alts_sma_obj->p_send_port_attr->port_guid,
		&qp_create_attr,
		p_alts_sma_obj,		// context
		sma_mad_qp_err_cb,
		&p_alts_sma_obj->h_mad_pool,
		&p_alts_sma_obj->h_qp0);

	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_get_spl_qp()! %s\n", ib_get_err_str(ib_status)));
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return (ib_status);
	}

		// create svc
	cl_memclr(&mad_svc, sizeof(ib_mad_svc_t));

	mad_svc.mad_svc_context = p_alts_sma_obj;
	mad_svc.pfn_mad_send_cb = alts_sma_mad_svc_send_cb;
	mad_svc.pfn_mad_recv_cb = alts_sma_mad_svc_recv_cb;

	mad_svc.support_unsol = TRUE;


	mad_svc.mgmt_class = IB_MCLASS_SUBN_DIR;
	mad_svc.mgmt_version = 0x01;

	// fill in methods supported
	mad_svc.method_array[IB_MAD_METHOD_GET] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_SET] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_TRAP] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_REPORT] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_TRAP_REPRESS] = TRUE;

	ib_status = ib_reg_mad_svc(
		p_alts_sma_obj->h_qp0,
		&mad_svc,
		&p_alts_sma_obj->h_sma_mad_svc );

	if (ib_status != IB_SUCCESS)
	{
		ALTS_TRACE_EXIT(ALTS_DBG_VERBOSE,
			("Error in ib_reg_mad_svc()! %s\n", ib_get_err_str(ib_status)));
		ALTS_EXIT( ALTS_DBG_VERBOSE);
		return (ib_status);
	}


	ALTS_EXIT( ALTS_DBG_VERBOSE);
	return IB_SUCCESS;
}
/*********************************************************
**********************************************************/

void
alts_sma_mad_svc_send_cb(
	IN	const ib_mad_svc_handle_t		h_mad_svc,
	IN	void					*mad_svc_context,
	IN	ib_mad_element_t		*p_mad_element )
{
	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );
	UNUSED_PARAM( p_mad_element );
}
/*********************************************************
**********************************************************/

void
alts_sma_mad_svc_recv_cb(
	IN	const ib_mad_svc_handle_t		h_mad_svc,
	IN	void					*mad_svc_context,
	IN	ib_mad_element_t		*p_mad_element )
{
	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );
	UNUSED_PARAM( p_mad_element );
}
/*********************************************************
**********************************************************/
void
sma_mad_qp_err_cb(
	ib_async_event_rec_t	*p_err_rec
	)
{

	ALTS_ENTER( ALTS_DBG_VERBOSE );

	UNUSED_PARAM( p_err_rec );

	ALTS_EXIT( ALTS_DBG_VERBOSE);
}
