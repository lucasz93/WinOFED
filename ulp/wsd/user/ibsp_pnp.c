/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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

/* TODO: right now, hotplug is not supported. */

#include "ibspdebug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ibsp_pnp.tmh"
#endif

#include "ibspdll.h"


static void pnp_port_remove(
	IN				struct ibsp_port* const		port );


/* Find a HCA in the list based on its GUID */
static struct ibsp_hca *
lookup_hca(
					ib_net64_t					ca_guid )
{
	cl_list_item_t *item;

	cl_spinlock_acquire( &g_ibsp.hca_mutex );

	for( item = cl_qlist_head( &g_ibsp.hca_list );
		item != cl_qlist_end( &g_ibsp.hca_list );
		item = cl_qlist_next( item ) )
	{
		struct ibsp_hca *hca = PARENT_STRUCT(item, struct ibsp_hca, item);
		if( hca->guid == ca_guid )
		{
			/* Found */
			cl_spinlock_release( &g_ibsp.hca_mutex );
			return hca;
		}
	}

	cl_spinlock_release( &g_ibsp.hca_mutex );

	return NULL;
}


/* Add a new adapter */
ib_api_status_t
pnp_ca_add(
	IN				ib_pnp_ca_rec_t* const		p_ca_rec )
{
	struct ibsp_hca	*hca;
	ib_api_status_t	status;

	IBSP_ENTER( IBSP_DBG_HW );

	hca = HeapAlloc( g_ibsp.heap, HEAP_ZERO_MEMORY, sizeof(struct ibsp_hca) );
	if( hca == NULL )
	{
		IBSP_ERROR( ("can't get enough memory (%d)\n", sizeof(struct ibsp_hca)) );
		status = IB_INSUFFICIENT_MEMORY;
		goto pnp_ca_add_err1;
	}

	hca->guid = p_ca_rec->p_ca_attr->ca_guid;
	hca->dev_id = p_ca_rec->p_ca_attr->dev_id;
	cl_qlist_init( &hca->port_list );
	cl_spinlock_init( &hca->port_lock );
	cl_qlist_init( &hca->rdma_mem_list.list );
	cl_spinlock_init( &hca->rdma_mem_list.mutex );
	cl_spinlock_init( &hca->cq_lock );

	/* HCA handle */
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW,
			 ("handle is %p %016I64x\n", g_ibsp.al_handle, hca->guid) );
	status =
		ib_open_ca( g_ibsp.al_handle, hca->guid, NULL, hca, &hca->hca_handle );

	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("ib_open_ca failed (%d)\n", status) );
		goto pnp_ca_add_err2;
	}

	STAT_INC( ca_num );

	/* Protection domain for the HCA */
	status = ib_alloc_pd( hca->hca_handle, IB_PDT_NORMAL, hca, &hca->pd );
	if( status == IB_SUCCESS )
	{
		STAT_INC( pd_num );

		IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_EP, ("allocated PD %p for HCA\n", hca->pd) );

		/* Success */
		cl_spinlock_acquire( &g_ibsp.hca_mutex );
		cl_qlist_insert_tail( &g_ibsp.hca_list, &hca->item );
		cl_spinlock_release( &g_ibsp.hca_mutex );

		p_ca_rec->pnp_rec.context = hca;
	}
	else
	{
		IBSP_ERROR( ("ib_alloc_pd failed (%d)\n", status) );
		if( ib_close_ca( hca->hca_handle, NULL ) == IB_SUCCESS )
			STAT_DEC( ca_num );

pnp_ca_add_err2:
		HeapFree( g_ibsp.heap, 0, hca );

	}
pnp_ca_add_err1:

	IBSP_EXIT( IBSP_DBG_HW );
	return status;
}


/* Remove an adapter and its ports. */
void
pnp_ca_remove(
					struct ibsp_hca				*hca )
{
	ib_api_status_t			status;
	cl_list_item_t			*p_item;
	struct cq_thread_info	*p_cq_tinfo;

	IBSP_ENTER( IBSP_DBG_HW );

	/*
	 * Remove all the ports
	 */
	cl_spinlock_acquire( &hca->port_lock );
	while( cl_qlist_count( &hca->port_list ) )
	{
		p_item = cl_qlist_remove_head( &hca->port_list );

		HeapFree( g_ibsp.heap, 0,
			PARENT_STRUCT(p_item, struct ibsp_port, item) );
	}
	cl_spinlock_release( &hca->port_lock );

	cl_spinlock_acquire( &hca->cq_lock );
	while( hca->cq_tinfo )
	{
		p_cq_tinfo = hca->cq_tinfo;

		hca->cq_tinfo = PARENT_STRUCT(
			cl_qlist_next( &hca->cq_tinfo->list_item ),
		struct cq_thread_info, list_item );

		__cl_primitive_remove( &p_cq_tinfo->list_item );

		if( hca->cq_tinfo == p_cq_tinfo )
			break;

		cl_spinlock_release( &hca->cq_lock );
		ib_destroy_cq_tinfo( p_cq_tinfo );
		cl_spinlock_acquire( &hca->cq_lock );
	}
	cl_spinlock_release( &hca->cq_lock );

	if( hca->pd )
	{
		ibsp_dereg_hca( &hca->rdma_mem_list );

		/*
		 * No need to wait for PD destruction - CA destruction will block
		 * until all child resources are released.
		 */
		status = ib_dealloc_pd( hca->pd, NULL );
		if( status )
		{
			IBSP_ERROR( ("ib_dealloc_pd failed (%d)\n", status) );
		}
		else
		{
			STAT_DEC( pd_num );
		}
		hca->pd = NULL;
	}

	if( hca->hca_handle )
	{
		status = ib_close_ca( hca->hca_handle, ib_sync_destroy );
		if( status != IB_SUCCESS )
			IBSP_ERROR( ("ib_close_ca failed (%d)\n", status) );

		hca->hca_handle = NULL;
	}

	/* Remove the HCA from the HCA list and free it. */
	cl_spinlock_acquire( &g_ibsp.hca_mutex );
	cl_qlist_remove_item( &g_ibsp.hca_list, &hca->item );
	cl_spinlock_release( &g_ibsp.hca_mutex );

	cl_spinlock_destroy( &hca->port_lock );
	cl_spinlock_destroy( &hca->rdma_mem_list.mutex );

	cl_spinlock_destroy( &hca->cq_lock );

	HeapFree( g_ibsp.heap, 0, hca );

	IBSP_EXIT( IBSP_DBG_HW );
}


/* Add a new port to an adapter */
static ib_api_status_t
pnp_port_add(
	IN	OUT			ib_pnp_port_rec_t* const	p_port_rec )
{
	struct ibsp_hca *hca;
	struct ibsp_port *port;

	IBSP_ENTER( IBSP_DBG_HW );

	hca = lookup_hca( p_port_rec->p_ca_attr->ca_guid );
	if( !hca )
	{
		IBSP_ERROR( ("Failed to lookup HCA (%016I64x) for new port (%016I64x)\n",
			p_port_rec->p_ca_attr->ca_guid, p_port_rec->p_port_attr->port_guid) );
		IBSP_EXIT( IBSP_DBG_HW );
		return IB_INVALID_GUID;
	}

	port = HeapAlloc( g_ibsp.heap, HEAP_ZERO_MEMORY, sizeof(struct ibsp_port) );
	if( port == NULL )
	{
		IBSP_ERROR( ("HeapAlloc failed (%d)\n", sizeof(struct ibsp_port)) );
		IBSP_EXIT( IBSP_DBG_HW );
		return IB_INSUFFICIENT_MEMORY;
	}

	port->guid = p_port_rec->p_port_attr->port_guid;
	port->port_num = p_port_rec->p_port_attr->port_num;
	port->hca = hca;

	cl_spinlock_acquire( &hca->port_lock );
	cl_qlist_insert_tail( &hca->port_list, &port->item );
	cl_spinlock_release( &hca->port_lock );
	p_port_rec->pnp_rec.context = port;

	IBSP_EXIT( IBSP_DBG_HW );
	return IB_SUCCESS;
}


/* Remove a port. The IP addresses should have already been removed. */
static void
pnp_port_remove(
	IN				struct ibsp_port* const		port )
{
	IBSP_ENTER( IBSP_DBG_HW );

	if( !port )
		goto done;

	CL_ASSERT( port->hca );

	/* Remove the port from the HCA list */
	cl_spinlock_acquire( &port->hca->port_lock );
	cl_qlist_remove_item( &port->hca->port_list, &port->item );
	cl_spinlock_release( &port->hca->port_lock );

	HeapFree( g_ibsp.heap, 0, port );

done:
	IBSP_EXIT( IBSP_DBG_HW );
}


static ib_api_status_t AL_API
pnp_callback(
	IN				ib_pnp_rec_t				*pnp_rec )
{
	ib_api_status_t			status = IB_SUCCESS;
	ib_pnp_port_rec_t*		p_port_rec = (ib_pnp_port_rec_t*)pnp_rec;

	IBSP_ENTER( IBSP_DBG_HW );
	IBSP_PRINT(TRACE_LEVEL_INFORMATION, IBSP_DBG_HW, ("event is %x\n", pnp_rec->pnp_event) );

	switch( pnp_rec->pnp_event )
	{
		/* CA events */
	case IB_PNP_CA_ADD:
		status = pnp_ca_add( (ib_pnp_ca_rec_t*)pnp_rec );
		break;

	case IB_PNP_CA_REMOVE:
		pnp_ca_remove( (struct ibsp_hca*)pnp_rec->context );
		break;

		/* Port events */
	case IB_PNP_PORT_ADD:
		status = pnp_port_add( p_port_rec );
		break;

	case IB_PNP_PORT_INIT:
	case IB_PNP_PORT_ARMED:
	case IB_PNP_PORT_ACTIVE:
	case IB_PNP_PORT_DOWN:
		/* Nothing to do. */
		break;

	case IB_PNP_PORT_REMOVE:
		pnp_port_remove( (struct ibsp_port*)pnp_rec->context );
		break;

	case IB_PNP_PKEY_CHANGE:
	case IB_PNP_SM_CHANGE:
	case IB_PNP_GID_CHANGE:
	case IB_PNP_LID_CHANGE:
	case IB_PNP_SUBNET_TIMEOUT_CHANGE:
		IBSP_ERROR( ("pnp_callback: unsupported event %x\n", pnp_rec->pnp_event) );
		break;

		/* Discovery complete event */
	case IB_PNP_REG_COMPLETE:
		break;

	default:
		IBSP_ERROR( ("pnp_callback: unsupported event %x\n", pnp_rec->pnp_event) );
		break;
	}

	IBSP_EXIT( IBSP_DBG_HW );

	return status;
}




/* Registers for PNP events and starts the hardware discovery */
ib_api_status_t
register_pnp(void)
{
	ib_api_status_t status;
	ib_pnp_req_t pnp_req;

	IBSP_ENTER( IBSP_DBG_HW );

	pnp_req.pnp_class = IB_PNP_CA;
	pnp_req.pnp_context = NULL;
	pnp_req.pfn_pnp_cb = pnp_callback;
	status = ib_reg_pnp( g_ibsp.al_handle, &pnp_req, &g_ibsp.pnp_handle_port );
	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("register_pnp: ib_reg_pnp for PORT failed (%d)\n", status) );
		goto done;
	}

	pnp_req.pnp_class = IB_PNP_PORT | IB_PNP_FLAG_REG_SYNC;
	pnp_req.pnp_context = NULL;
	pnp_req.pfn_pnp_cb = pnp_callback;
	status = ib_reg_pnp( g_ibsp.al_handle, &pnp_req, &g_ibsp.pnp_handle_port );
	if( status != IB_SUCCESS )
	{
		IBSP_ERROR( ("register_pnp: ib_reg_pnp for PORT failed (%d)\n", status) );
		goto done;
	}

	STAT_INC( pnp_num );

done:
	if( status != IB_SUCCESS )
	{
		unregister_pnp();
	}

	IBSP_EXIT( IBSP_DBG_HW );

	return status;
}


/* Unregisters the PNP events */
void
unregister_pnp(void)
{
	ib_api_status_t status;

	IBSP_ENTER( IBSP_DBG_HW );

	if( g_ibsp.pnp_handle_port )
	{
		status = ib_dereg_pnp( g_ibsp.pnp_handle_port, ib_sync_destroy );
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( ("unregister_pnp: ib_dereg_pnp for PORT failed (%d)\n",
				status) );
		}

		g_ibsp.pnp_handle_port = NULL;
	}

	if( g_ibsp.pnp_handle_ca )
	{
		status = ib_dereg_pnp( g_ibsp.pnp_handle_ca, ib_sync_destroy );
		if( status != IB_SUCCESS )
		{
			IBSP_ERROR( ("unregister_pnp: ib_dereg_pnp for PORT failed (%d)\n",
				status) );
		}

		g_ibsp.pnp_handle_ca = NULL;
	}

	IBSP_EXIT( IBSP_DBG_HW );
}
