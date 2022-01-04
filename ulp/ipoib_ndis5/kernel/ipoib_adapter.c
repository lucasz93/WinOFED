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



#include "ipoib_adapter.h"
#include "ipoib_port.h"
#include "ipoib_driver.h"
#include "ipoib_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_adapter.tmh"
#endif


#define ITEM_POOL_START		16
#define ITEM_POOL_GROW		16


/* IB Link speeds in 100bps */
#define ONE_X_IN_100BPS		25000000
#define FOUR_X_IN_100BPS	100000000
#define TWELVE_X_IN_100BPS	300000000


/* Declarations */
static void
adapter_construct(
	IN				ipoib_adapter_t* const		p_adapter );


static ib_api_status_t
adapter_init(
	IN				ipoib_adapter_t* const		p_adapter );


static void
__adapter_destroying(
	IN				cl_obj_t* const				p_obj );


static void
__adapter_free(
	IN				cl_obj_t* const				p_obj );


static ib_api_status_t
__ipoib_pnp_reg(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_class_t				flags );


static void
__ipoib_pnp_dereg(
	IN				void*						context );


static void
__ipoib_adapter_reset(
	IN				void*	context);


static ib_api_status_t
__ipoib_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec );


void
ipoib_join_mcast(
	IN				ipoib_adapter_t* const		p_adapter );


/* Leaves all mcast groups when port goes down. */
static void
ipoib_clear_mcast(
	IN				ipoib_port_t* const			p_port );

NDIS_STATUS
ipoib_get_adapter_guids(
	IN				NDIS_HANDLE* const			h_adapter,
	IN	OUT			ipoib_adapter_t				*p_adapter );

NDIS_STATUS
ipoib_get_adapter_params(
	IN				NDIS_HANDLE* const			wrapper_config_context,
	IN	OUT			ipoib_adapter_t				*p_adapter,
	OUT				PUCHAR						*p_mac,
	OUT				UINT						*p_len);


/* Implementation */
ib_api_status_t
ipoib_create_adapter(
	IN				NDIS_HANDLE					wrapper_config_context,
	IN				void* const					h_adapter,
		OUT			ipoib_adapter_t** const		pp_adapter )
{
	ipoib_adapter_t		*p_adapter;
	ib_api_status_t		status;
	cl_status_t			cl_status;
	PUCHAR				mac;
	UINT				len;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_adapter = cl_zalloc( sizeof(ipoib_adapter_t) );
	if( !p_adapter )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate ipoib_adapter_t (%d bytes)",
			sizeof(ipoib_adapter_t)) );
		return IB_INSUFFICIENT_MEMORY;
	}

	adapter_construct( p_adapter );

	p_adapter->h_adapter = h_adapter;

	p_adapter->p_ifc = cl_zalloc( sizeof(ib_al_ifc_t) );
	if( !p_adapter->p_ifc )
	{
		__adapter_free( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_create_adapter failed to alloc ipoib_ifc_t %d bytes\n",
			sizeof(ib_al_ifc_t)) );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Get the CA and port GUID from the bus driver. */
	status = ipoib_get_adapter_guids( h_adapter,  p_adapter );
	if( status != NDIS_STATUS_SUCCESS )
	{
		__adapter_free( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_get_adapter_guids returned 0x%.8X.\n", status) );
		return status;
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port %016I64x (CA %016I64x port %d) initializing\n",
			p_adapter->guids.port_guid.guid, p_adapter->guids.ca_guid,
			p_adapter->guids.port_num) );

	cl_status = cl_obj_init( &p_adapter->obj, CL_DESTROY_SYNC,
		__adapter_destroying, NULL, __adapter_free );
	if( cl_status != CL_SUCCESS )
	{
		__adapter_free( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_obj_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	/* Read configuration parameters. */
	status = ipoib_get_adapter_params( wrapper_config_context,
		p_adapter , &mac, &len);
	if( status != NDIS_STATUS_SUCCESS )
	{
		cl_obj_destroy( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_get_adapter_params returned 0x%.8x.\n", status) );
		return status;
	}
		
	status = adapter_init( p_adapter );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_adapter->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("adapter_init returned %s.\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	ETH_COPY_NETWORK_ADDRESS( p_adapter->params.conf_mac.addr, p_adapter->mac.addr );
	/* If there is a NetworkAddress override in registry, use it */
	if( (len == HW_ADDR_LEN) && (mac != NULL) )
	{
		if( ETH_IS_MULTICAST(mac) || ETH_IS_BROADCAST(mac) ||
			!ETH_IS_LOCALLY_ADMINISTERED(mac) )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_INIT,
				("Overriding NetworkAddress is invalid - "
				"%02x-%02x-%02x-%02x-%02x-%02x\n",
				mac[0], mac[1], mac[2],
				mac[3], mac[4], mac[5]) );
		}
		else
	{
			ETH_COPY_NETWORK_ADDRESS( p_adapter->params.conf_mac.addr, mac );
		}
	}

	NdisMGetDeviceProperty(h_adapter, &p_adapter->pdo, NULL, NULL, NULL, NULL);
	ASSERT(p_adapter->pdo != NULL);

	p_adapter->p_stat = ipoib_st_dev_add();
	if ( p_adapter->p_stat ) 
		p_adapter->p_stat->p_adapter = p_adapter;

	*pp_adapter = p_adapter;

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


ib_api_status_t
ipoib_start_adapter(
	IN				ipoib_adapter_t* const		p_adapter )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	status = __ipoib_pnp_reg( p_adapter,
		IB_PNP_FLAG_REG_SYNC | IB_PNP_FLAG_REG_COMPLETE );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


void
ipoib_destroy_adapter(
	IN				ipoib_adapter_t* const		p_adapter )
{
	PIPOIB_ST_DEVICE p_stat;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_adapter );

	// statistics
	p_stat = p_adapter->p_stat;
	if ( p_stat ) {
		p_stat->p_halt_thread = KeGetCurrentThread();
		p_stat->p_prev_port = p_adapter->p_port;
	}

	/*
	 * Flag the adapter as being removed.  We use the IB_PNP_PORT_REMOVE state
	 * for this purpose.  Note that we protect this state change with both the
	 * mutex and the lock.  The mutex provides synchronization as a whole
	 * between destruction and AL callbacks (PnP, Query, Destruction).
	 * The lock provides protection
	 */
	KeWaitForMutexObject(
		&p_adapter->mutex, Executive, KernelMode, FALSE, NULL );
	cl_obj_lock( &p_adapter->obj );
	p_adapter->state = IB_PNP_PORT_REMOVE;

	/*
	 * Clear the pointer to the port object since the object destruction
	 * will cascade to child objects.  This prevents potential duplicate
	 * destruction (or worse, stale pointer usage).
	 */
	p_adapter->p_port = NULL;

	cl_obj_unlock( &p_adapter->obj );

	KeReleaseMutex( &p_adapter->mutex, FALSE );

	cl_obj_destroy( &p_adapter->obj );
	ipoib_st_dev_rmv( p_stat );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
adapter_construct(
	IN				ipoib_adapter_t* const		p_adapter )
{
	cl_obj_construct( &p_adapter->obj, IPOIB_OBJ_INSTANCE );
	cl_spinlock_construct( &p_adapter->send_stat_lock );
	cl_spinlock_construct( &p_adapter->recv_stat_lock );
	cl_qpool_construct( &p_adapter->item_pool );
	KeInitializeMutex( &p_adapter->mutex, 0 );

	cl_thread_construct(&p_adapter->destroy_thread);
	
	cl_vector_construct( &p_adapter->ip_vector );

	cl_perf_construct( &p_adapter->perf );

	p_adapter->state = IB_PNP_PORT_ADD;
	p_adapter->port_rate = FOUR_X_IN_100BPS;
}


static ib_api_status_t
adapter_init(
	IN				ipoib_adapter_t* const		p_adapter )
{
	cl_status_t			cl_status;
	ib_api_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_status = cl_perf_init( &p_adapter->perf, MaxPerf );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_perf_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_spinlock_init( &p_adapter->send_stat_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_spinlock_init( &p_adapter->recv_stat_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_qpool_init( &p_adapter->item_pool, ITEM_POOL_START, 0,
		ITEM_POOL_GROW, sizeof(cl_pool_obj_t), NULL, NULL, NULL );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}


	/* We manually manage the size and capacity of the vector. */
	cl_status = cl_vector_init( &p_adapter->ip_vector, 0,
		0, sizeof(net_address_item_t), NULL, NULL, p_adapter );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_vector_init for ip_vector returned %#x\n",
			cl_status) );
		return IB_ERROR;
	}

	/* Validate the port GUID and generate the MAC address. */
	status =
		ipoib_mac_from_guid( p_adapter->guids.port_guid.guid, p_adapter->params.guid_mask, &p_adapter->mac);
	if( status != IB_SUCCESS )
	{
		if( status == IB_INVALID_GUID_MASK )
		{
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ERROR,
				("Invalid GUID mask received, rejecting it") );
			ipoib_create_log(p_adapter->h_adapter, GUID_MASK_LOG_INDEX, EVENT_IPOIB_WRONG_PARAMETER_WRN);
		}

		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_mac_from_guid returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Open AL. */
	status = p_adapter->p_ifc->open_al( &p_adapter->h_al );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_open_al returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static ib_api_status_t
__ipoib_pnp_reg(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_class_t				flags )
{
	ib_api_status_t		status;
	ib_pnp_req_t		pnp_req;
	
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( !p_adapter->h_pnp );
	CL_ASSERT( !p_adapter->registering );

	p_adapter->registering = TRUE;
	
	/* Register for PNP events. */
	cl_memclr( &pnp_req, sizeof(pnp_req) );
	pnp_req.pnp_class = IB_PNP_PORT | flags;
	/*
	 * Context is the cl_obj of the adapter to allow passing cl_obj_deref
	 * to ib_dereg_pnp.
	 */
	pnp_req.pnp_context = &p_adapter->obj;
	pnp_req.pfn_pnp_cb = __ipoib_pnp_cb;
	status = p_adapter->p_ifc->reg_pnp( p_adapter->h_al, &pnp_req, &p_adapter->h_pnp );
	if( status != IB_SUCCESS )
	{
		p_adapter->registering = FALSE;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_reg_pnp returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	/*
	 * Reference the adapter on behalf of the PNP registration.
	 * This allows the destruction to block until the PNP deregistration
	 * completes.
	 */
	cl_obj_ref( &p_adapter->obj );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static void
__adapter_destroying(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_adapter_t		*p_adapter;
	KLOCK_QUEUE_HANDLE	hdl;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_adapter = PARENT_STRUCT( p_obj, ipoib_adapter_t, obj );

	/*
	 * The adapter's object will be dereferenced when the deregistration
	 * completes.  No need to lock here since all PnP related API calls
	 * are driven by NDIS (via the Init/Reset/Destroy paths).
	 */
	if( p_adapter->h_pnp )
	{
		p_adapter->p_ifc->dereg_pnp( p_adapter->h_pnp, cl_obj_deref );
		p_adapter->h_pnp = NULL;
	}

	if( p_adapter->packet_filter )
	{
		KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
		cl_obj_lock( &p_adapter->obj );

		ASSERT( cl_qlist_count( &g_ipoib.adapter_list ) );
		cl_qlist_remove_item( &g_ipoib.adapter_list, &p_adapter->entry );

		p_adapter->packet_filter = 0;

		cl_obj_unlock( &p_adapter->obj );
		KeReleaseInStackQueuedSpinLock( &hdl );
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__adapter_free(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_adapter_t	*p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_adapter = PARENT_STRUCT( p_obj, ipoib_adapter_t, obj );

	if( p_adapter->p_ifc )
	{
		if( p_adapter->h_al )
			p_adapter->p_ifc->close_al( p_adapter->h_al );

		cl_free( p_adapter->p_ifc );
		p_adapter->p_ifc = NULL;
	}

	cl_vector_destroy( &p_adapter->ip_vector );
	cl_qpool_destroy( &p_adapter->item_pool );
	cl_spinlock_destroy( &p_adapter->recv_stat_lock );
	cl_spinlock_destroy( &p_adapter->send_stat_lock );
	cl_obj_deinit( p_obj );

	cl_perf_destroy( &p_adapter->perf, TRUE );

	cl_free( p_adapter );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
ipoib_query_pkey_index(ipoib_adapter_t	*p_adapter)
{
	ib_api_status_t			status;
	ib_ca_attr_t		    *ca_attr;
	uint32_t			    ca_size;
	uint16_t index = 0;

	/* Query the CA for Pkey table */
	status = p_adapter->p_ifc->query_ca(p_adapter->p_port->ib_mgr.h_ca, NULL, &ca_size);
	if(status != IB_INSUFFICIENT_MEMORY)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("ib_query_ca failed\n"));
		return status;
	}

	ca_attr = (ib_ca_attr_t*)cl_zalloc(ca_size);
	if	(!ca_attr)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("cl_zalloc can't allocate %d\n",ca_size));
		return IB_INSUFFICIENT_MEMORY;
	}

	status = p_adapter->p_ifc->query_ca(p_adapter->p_port->ib_mgr.h_ca, ca_attr,&ca_size);	
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("ib_query_ca returned %s\n", 
						 p_adapter->p_ifc->get_err_str( status )) );
		goto pkey_end;
	}
	CL_ASSERT(ca_attr->p_port_attr[p_adapter->p_port->port_num -1].p_pkey_table[0] == IB_DEFAULT_PKEY);
	for(index = 0; index < ca_attr->p_port_attr[p_adapter->p_port->port_num -1].num_pkeys; index++)
	{
		if(cl_hton16(p_adapter->guids.port_guid.pkey) == ca_attr->p_port_attr[p_adapter->p_port->port_num -1].p_pkey_table[index])
			break;
	}
	if(index >= ca_attr->p_port_attr[p_adapter->p_port->port_num -1].num_pkeys)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Pkey table is invalid, index not found\n"));
		NdisWriteErrorLogEntry( p_adapter->h_adapter,
			EVENT_IPOIB_PARTITION_ERR, 1, p_adapter->guids.port_guid.pkey );
		status = IB_NOT_FOUND;
		p_adapter->p_port->pkey_index = PKEY_INVALID_INDEX;
		goto pkey_end;
	}

	p_adapter->p_port->pkey_index = index;
	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_IB,
					("for PKEY = 0x%04X got index = %d\n",p_adapter->guids.port_guid.pkey,index));

pkey_end:
	if(ca_attr)
		cl_free(ca_attr);
	return status;
}

static ib_api_status_t
__ipoib_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec )
{
	ipoib_adapter_t		*p_adapter;
	ipoib_port_t		*p_port;
	ib_pnp_event_t		old_state;
	ib_pnp_port_rec_t	*p_port_rec;
	ib_api_status_t		status = IB_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_PNP );

	CL_ASSERT( p_pnp_rec );

	p_adapter =
		PARENT_STRUCT( p_pnp_rec->pnp_context, ipoib_adapter_t, obj );

	CL_ASSERT( p_adapter );

	/* Synchronize with destruction */
	KeWaitForMutexObject(
		&p_adapter->mutex, Executive, KernelMode, FALSE, NULL );
	cl_obj_lock( &p_adapter->obj );
	old_state = p_adapter->state;
	cl_obj_unlock( &p_adapter->obj );
	if( old_state == IB_PNP_PORT_REMOVE )
	{
		KeReleaseMutex( &p_adapter->mutex, FALSE );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_PNP,
			("Aborting - Adapter destroying.\n") );
		return IB_NOT_DONE;
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	p_port_rec = (ib_pnp_port_rec_t*)p_pnp_rec;

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		CL_ASSERT( !p_pnp_rec->context );
		/* Only process our port GUID. */
		if( p_pnp_rec->guid != p_adapter->guids.port_guid.guid )
		{
			status = IB_NOT_DONE;
			break;
		}

		/* Don't process if we're destroying. */
		if( p_adapter->obj.state == CL_DESTROYING )
		{
			status = IB_NOT_DONE;
			break;
		}

		CL_ASSERT( !p_adapter->p_port );
		/* Allocate all IB resources. */
		cl_obj_lock( &p_adapter->obj );
		p_adapter->state = IB_PNP_PORT_ADD;
		cl_obj_unlock( &p_adapter->obj );
		status = ipoib_create_port( p_adapter, p_port_rec, &p_port );
		cl_obj_lock( &p_adapter->obj );
		if( status != IB_SUCCESS )
		{
			p_adapter->state = old_state;
			cl_obj_unlock( &p_adapter->obj );
			p_adapter->hung = TRUE;
			break;
		}

		p_pnp_rec->context = p_port;

		if ( p_adapter->p_stat ) 
			p_adapter->p_stat->p_prev_port = p_adapter->p_port;
		p_adapter->p_port = p_port;
		cl_obj_unlock( &p_adapter->obj );
		break;

	case IB_PNP_PORT_REMOVE:
		/* Release all IB resources. */
		CL_ASSERT( p_pnp_rec->context );

		cl_obj_lock( &p_adapter->obj );
		p_adapter->state = IB_PNP_PORT_REMOVE;
		p_port = p_adapter->p_port;
		p_adapter->p_port = NULL;
		if ( p_adapter->p_stat ) 
			p_adapter->p_stat->p_prev_port = p_port;
		cl_obj_unlock( &p_adapter->obj );
		ipoib_port_destroy( p_port );
		p_pnp_rec->context = NULL;
		status = IB_SUCCESS;
		break;

	case IB_PNP_PORT_ACTIVE:
		/* Join multicast groups and put QP in RTS. */
		CL_ASSERT( p_pnp_rec->context );

		cl_obj_lock( &p_adapter->obj );
		p_adapter->state = IB_PNP_PORT_INIT;
		cl_obj_unlock( &p_adapter->obj );
		ipoib_port_up( p_adapter->p_port, p_port_rec );

		status = IB_SUCCESS;
		break;

	case IB_PNP_PORT_ARMED:
		status = IB_SUCCESS;
		break;

	case IB_PNP_PORT_INIT:
		/*
		 * Init could happen if the SM brings the port down
		 * without changing the physical link.
		 */
	case IB_PNP_PORT_DOWN:
		CL_ASSERT( p_pnp_rec->context );

		cl_obj_lock( &p_adapter->obj );
		old_state = p_adapter->state;
		p_adapter->state = IB_PNP_PORT_DOWN;
		cl_obj_unlock( &p_adapter->obj );
		status = IB_SUCCESS;

		if( !p_adapter->registering && old_state != IB_PNP_PORT_DOWN )
		{
			NdisMIndicateStatus( p_adapter->h_adapter,
				NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
			NdisMIndicateStatusComplete( p_adapter->h_adapter );

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
				("Link DOWN!\n") );

			ipoib_port_down( p_adapter->p_port );
		}
		break;

	case IB_PNP_REG_COMPLETE:
		if( p_adapter->registering )
		{
			p_adapter->registering = FALSE;
			cl_obj_lock( &p_adapter->obj );
			old_state = p_adapter->state;
			cl_obj_unlock( &p_adapter->obj );

			if( old_state == IB_PNP_PORT_DOWN )
			{
				/* If we were initializing, we might have pended some OIDs. */
				ipoib_resume_oids( p_adapter );
				NdisMIndicateStatus( p_adapter->h_adapter,
					NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
				NdisMIndicateStatusComplete( p_adapter->h_adapter );
			}
		}

		if( p_adapter->reset && p_adapter->state != IB_PNP_PORT_INIT )
		{
			p_adapter->reset = FALSE;
			NdisMResetComplete(
				p_adapter->h_adapter, NDIS_STATUS_SUCCESS, TRUE );
		}
		status = IB_SUCCESS;
		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("IPOIB: Received unhandled PnP event 0x%x (%s)\n",
			p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );
		/* Fall through. */

		status = IB_SUCCESS;

		/* We ignore events below if the link is not active. */
		if( p_port_rec->p_port_attr->link_state != IB_LINK_ACTIVE )
		   	break;

		case IB_PNP_PKEY_CHANGE:
			if(p_pnp_rec->pnp_event == IB_PNP_PKEY_CHANGE && 
			   p_adapter->guids.port_guid.pkey != IB_DEFAULT_PKEY)
			{
				status = ipoib_query_pkey_index(p_adapter);
				if(status != IB_SUCCESS)
				{
				   cl_obj_lock( &p_adapter->obj );
				   p_adapter->state = IB_PNP_PORT_INIT;
				   cl_obj_unlock( &p_adapter->obj );
				}
			}

		case IB_PNP_SM_CHANGE:
		case IB_PNP_GID_CHANGE:
		case IB_PNP_LID_CHANGE:

		cl_obj_lock( &p_adapter->obj );
		old_state = p_adapter->state;
		switch( old_state )
		{
		case IB_PNP_PORT_DOWN:
			p_adapter->state = IB_PNP_PORT_INIT;
			break;

		default:
			p_adapter->state = IB_PNP_PORT_DOWN;
		}
		cl_obj_unlock( &p_adapter->obj );
		
		if( p_adapter->registering )
			break;

		switch( old_state )
		{
		case IB_PNP_PORT_ACTIVE:
		case IB_PNP_PORT_INIT:
			NdisMIndicateStatus( p_adapter->h_adapter,
				NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
			NdisMIndicateStatusComplete( p_adapter->h_adapter );

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
				("Link DOWN!\n") );

			ipoib_port_down( p_adapter->p_port );
			/* Fall through. */

		case IB_PNP_PORT_DOWN:
			cl_obj_lock( &p_adapter->obj );
			p_adapter->state = IB_PNP_PORT_INIT;
			cl_obj_unlock( &p_adapter->obj );
			ipoib_port_up( p_adapter->p_port, (ib_pnp_port_rec_t*)p_pnp_rec );
		}
		break;
	}

	KeReleaseMutex( &p_adapter->mutex, FALSE );

	IPOIB_EXIT( IPOIB_DBG_PNP );
	return status;
}


/* Joins/leaves mcast groups based on currently programmed mcast MACs. */
void
ipoib_refresh_mcast(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				mac_addr_t* const			p_mac_array,
	IN		const	uint8_t						num_macs )
{
	uint8_t				i, j;
	ipoib_port_t		*p_port = NULL;

	IPOIB_ENTER( IPOIB_DBG_MCAST );
	cl_obj_lock( &p_adapter->obj );
	if( p_adapter->state == IB_PNP_PORT_ACTIVE )
	{
		p_port = p_adapter->p_port;
		ipoib_port_ref( p_port, ref_refresh_mcast );
	}
	cl_obj_unlock( &p_adapter->obj );

	if( p_port )
	{
		/* Purge old entries. */
		for( i = 0; i < p_adapter->mcast_array_size; i++ )
		{
			for( j = 0; j < num_macs; j++ )
			{
				if( !cl_memcmp( &p_adapter->mcast_array[i], &p_mac_array[j],
					sizeof(mac_addr_t) ) )
				{
					break;
				}
			}
			if( j != num_macs )
				continue;

			ipoib_port_remove_endpt( p_port, p_adapter->mcast_array[i] );
		}

		/* Add new entries */
		for( i = 0; i < num_macs; i++ )
		{
			for( j = 0; j < p_adapter->mcast_array_size; j++ )
			{
				if( !cl_memcmp( &p_adapter->mcast_array[j], &p_mac_array[i],
					sizeof(mac_addr_t) ) )
				{
					break;
				}
			}

			if( j != p_adapter->mcast_array_size )
				continue;
			if ( ( p_mac_array[i].addr[0] == 1 && p_mac_array[i].addr[1] == 0 && p_mac_array[i].addr[2] == 0x5e &&
				   p_mac_array[i].addr[3] == 0 && p_mac_array[i].addr[4] == 0 && p_mac_array[i].addr[5] == 1 ) ||
				  !( p_mac_array[i].addr[0] == 1 && p_mac_array[i].addr[1] == 0 && p_mac_array[i].addr[2] == 0x5e )
				)
			{
				ipoib_port_join_mcast( p_port, p_mac_array[i], IB_MC_REC_STATE_FULL_MEMBER );
			}
		}
	}

	/* Copy the MAC array. */
	NdisMoveMemory( p_adapter->mcast_array, p_mac_array,
		num_macs * sizeof(mac_addr_t) );
	p_adapter->mcast_array_size = num_macs;

	if( p_port )
		ipoib_port_deref( p_port, ref_refresh_mcast );

	IPOIB_EXIT( IPOIB_DBG_MCAST );
}


ib_api_status_t
ipoib_reset_adapter(
	IN				ipoib_adapter_t* const		p_adapter )
{
	ib_api_status_t		status;
	ib_pnp_handle_t		h_pnp;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	if( p_adapter->reset )
		return IB_INVALID_STATE;

	p_adapter->hung = FALSE;
	p_adapter->reset = TRUE;

	if( p_adapter->h_pnp )
	{
		h_pnp = p_adapter->h_pnp;
		p_adapter->h_pnp  = NULL;
		status = p_adapter->p_ifc->dereg_pnp( h_pnp, __ipoib_pnp_dereg );
		if( status == IB_SUCCESS )
			status = IB_NOT_DONE;
	}
	else
	{
		status = __ipoib_pnp_reg( p_adapter, IB_PNP_FLAG_REG_COMPLETE );
		if( status == IB_SUCCESS )
			p_adapter->hung = FALSE;
	}
	if (status == IB_NOT_DONE) {
		p_adapter->reset = TRUE;
	}
	else {
		p_adapter->reset = FALSE;
	}
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static void
__ipoib_pnp_dereg(
	IN				void*						context )
{
	ipoib_adapter_t*	p_adapter;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_adapter = PARENT_STRUCT( context, ipoib_adapter_t, obj );

	cl_thread_init(&p_adapter->destroy_thread, __ipoib_adapter_reset, (void*)p_adapter, "destroy_thread");
	
	IPOIB_ENTER( IPOIB_DBG_INIT );

}

static void
__ipoib_adapter_reset(
	IN				void*	context)
{

	ipoib_adapter_t	*p_adapter;
	ipoib_port_t		*p_port;
	ib_api_status_t		status;
	ib_pnp_event_t		state;
	
	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_adapter = (ipoib_adapter_t*)context;
	
	/* Synchronize with destruction */
	KeWaitForMutexObject(
		&p_adapter->mutex, Executive, KernelMode, FALSE, NULL );

	cl_obj_lock( &p_adapter->obj );

	CL_ASSERT( !p_adapter->h_pnp );

	if( p_adapter->state != IB_PNP_PORT_REMOVE )
		p_adapter->state = IB_PNP_PORT_ADD;

	state = p_adapter->state;

	/* Destroy the current port instance if it still exists. */
	p_port = p_adapter->p_port;
	p_adapter->p_port = NULL;
	if ( p_adapter->p_stat ) 
		p_adapter->p_stat->p_prev_port = p_port;
	cl_obj_unlock( &p_adapter->obj );

	if( p_port )
		ipoib_port_destroy( p_port );
	
	if( state != IB_PNP_PORT_REMOVE )
	{
		status = __ipoib_pnp_reg( p_adapter, IB_PNP_FLAG_REG_COMPLETE );
		if( status != IB_SUCCESS )
		{
			ASSERT( p_adapter->reset == TRUE );
			p_adapter->reset = FALSE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__ipoib_pnp_reg returned %s\n",
				p_adapter->p_ifc->get_err_str( status )) );
			NdisMResetComplete( 
				p_adapter->h_adapter, NDIS_STATUS_HARD_ERRORS, TRUE );
		}
	}
	else
	{
		ASSERT( p_adapter->reset == TRUE );
		p_adapter->reset = FALSE;
		NdisMResetComplete(
			p_adapter->h_adapter, NDIS_STATUS_SUCCESS, TRUE );
		status = IB_SUCCESS;
	}

	/* Dereference the adapter since the previous registration is now gone. */
	cl_obj_deref( &p_adapter->obj );

	KeReleaseMutex( &p_adapter->mutex, FALSE );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


void
ipoib_set_rate(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	uint8_t						link_width, 
	IN		const	uint8_t						link_speed )
{
	uint32_t	rate;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Set the link speed based on the IB link speed (1x vs 4x, etc). */
	switch( link_speed )
	{
	case IB_LINK_SPEED_ACTIVE_2_5:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link speed is 2.5Gs\n") );
		rate = IB_LINK_SPEED_ACTIVE_2_5;
		break;

	case IB_LINK_SPEED_ACTIVE_5:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link speed is 5G\n") );
		rate = IB_LINK_SPEED_ACTIVE_5;
		break;

	case IB_LINK_SPEED_ACTIVE_10:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link speed is 10G\n") );
		rate = IB_LINK_SPEED_ACTIVE_10;
		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid link speed %d.\n", link_speed) );
		rate = 0;
	}

	switch( link_width )
	{
	case IB_LINK_WIDTH_ACTIVE_1X:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link width is 1X\n") );
		rate *= ONE_X_IN_100BPS;
		break;

	case IB_LINK_WIDTH_ACTIVE_4X:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link width is 4X\n") );
		rate *= FOUR_X_IN_100BPS;
		break;

	case IB_LINK_WIDTH_ACTIVE_12X:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Link width is 12X\n") );
		rate *= TWELVE_X_IN_100BPS;
		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid link rate (%d).\n", link_width) );
		rate = 0;
	}

	p_adapter->port_rate = rate;
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


ib_api_status_t
ipoib_set_active(
	IN				ipoib_adapter_t* const		p_adapter )
{
	ib_pnp_event_t	old_state;
	uint8_t			i;
	ib_api_status_t	status = IB_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_obj_lock( &p_adapter->obj );
	old_state = p_adapter->state;

	/* Change the state to indicate that we are now connected and live. */
	if( old_state == IB_PNP_PORT_INIT )
		p_adapter->state = IB_PNP_PORT_ACTIVE;

	cl_obj_unlock( &p_adapter->obj );

	/*
	 * If we had a pending OID request for OID_GEN_LINK_SPEED,
	 * complete it now.
	 */
	switch( old_state )
	{
	case IB_PNP_PORT_ADD:
		ipoib_reg_addrs( p_adapter );
		/* Fall through. */

	case IB_PNP_PORT_REMOVE:
		ipoib_resume_oids( p_adapter );
		break;

	default:
		if (p_adapter->guids.port_guid.pkey != IB_DEFAULT_PKEY)
		{
			status = ipoib_query_pkey_index(p_adapter);
			if( IB_SUCCESS != status)
			{
				break;
			}
		}
		/* Join all programmed multicast groups. */
		for( i = 0; i < p_adapter->mcast_array_size; i++ )
		{
			ipoib_port_join_mcast(
				p_adapter->p_port, p_adapter->mcast_array[i] ,IB_MC_REC_STATE_FULL_MEMBER);
		}

		/* Register all existing addresses. */
		ipoib_reg_addrs( p_adapter );

		ipoib_resume_oids( p_adapter );

		/*
		 * Now that we're in the broadcast group, notify that
		 * we have a link.
		 */
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT, ("Link UP!\n") );
		NdisWriteErrorLogEntry( p_adapter->h_adapter,
			EVENT_IPOIB_PORT_UP + (p_adapter->port_rate/ONE_X_IN_100BPS),
			1, p_adapter->port_rate );

		if( !p_adapter->reset )
		{
			NdisMIndicateStatus( p_adapter->h_adapter, NDIS_STATUS_MEDIA_CONNECT,
				NULL, 0 );
			NdisMIndicateStatusComplete( p_adapter->h_adapter );
		}
	}

	if( p_adapter->reset )
	{
		p_adapter->reset = FALSE;
		NdisMResetComplete(
			p_adapter->h_adapter, NDIS_STATUS_SUCCESS, TRUE );
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return	status;
}


/*
 * If something goes wrong after the port goes active, e.g.
 *	- PortInfo query failure
 *	- MC Join timeout
 *	- etc
 * Mark the port state as down, resume any pended OIDS, etc.
 */
void
ipoib_set_inactive(
	IN				ipoib_adapter_t* const		p_adapter )
{
	ib_pnp_event_t	old_state;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	cl_obj_lock( &p_adapter->obj );
	old_state = p_adapter->state;
	if( old_state != IB_PNP_PORT_REMOVE )
		p_adapter->state = IB_PNP_PORT_DOWN;
	cl_obj_unlock( &p_adapter->obj );

	/*
	 * If we had a pending OID request for OID_GEN_LINK_SPEED,
	 * complete it now.
	 */
	if( old_state == IB_PNP_PORT_INIT )
	{
		NdisMIndicateStatus( p_adapter->h_adapter,
			NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
		NdisMIndicateStatusComplete( p_adapter->h_adapter );

		ipoib_resume_oids( p_adapter );
	}

	if( p_adapter->reset )
	{
		p_adapter->reset = FALSE;
		NdisMResetComplete(
			p_adapter->h_adapter, NDIS_STATUS_SUCCESS, TRUE );
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


NDIS_STATUS
ipoib_get_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN				pending_oid_t* const		p_oid_info )
{
	uint64_t	stat;

	IPOIB_ENTER( IPOIB_DBG_STAT );

	CL_ASSERT( p_adapter );

	cl_spinlock_acquire( &p_adapter->recv_stat_lock );
	switch( stat_sel )
	{
	case IP_STAT_SUCCESS:
		stat = p_adapter->recv_stats.comp.success;
		break;

	case IP_STAT_ERROR:
		stat = p_adapter->recv_stats.comp.error;
		break;

	case IP_STAT_DROPPED:
		stat = p_adapter->recv_stats.comp.dropped;
		break;

	case IP_STAT_UCAST_BYTES:
		stat = p_adapter->recv_stats.ucast.bytes;
		break;

	case IP_STAT_UCAST_FRAMES:
		stat = p_adapter->recv_stats.ucast.frames;
		break;

	case IP_STAT_BCAST_BYTES:
		stat = p_adapter->recv_stats.bcast.bytes;
		break;

	case IP_STAT_BCAST_FRAMES:
		stat = p_adapter->recv_stats.bcast.frames;
		break;

	case IP_STAT_MCAST_BYTES:
		stat = p_adapter->recv_stats.mcast.bytes;
		break;

	case IP_STAT_MCAST_FRAMES:
		stat = p_adapter->recv_stats.mcast.frames;
		break;

	default:
		stat = 0;
	}
	cl_spinlock_release( &p_adapter->recv_stat_lock );

	*p_oid_info->p_bytes_needed = sizeof(uint64_t);

	if( p_oid_info->buf_len >= sizeof(uint64_t) )
	{
		*((uint64_t*)p_oid_info->p_buf) = stat;
		*p_oid_info->p_bytes_used = sizeof(uint64_t);
	}
	else if( p_oid_info->buf_len >= sizeof(uint32_t) )
	{
		*((uint32_t*)p_oid_info->p_buf) = (uint32_t)stat;
		*p_oid_info->p_bytes_used = sizeof(uint32_t);
	}
	else
	{
		*p_oid_info->p_bytes_used = 0;
		IPOIB_EXIT( IPOIB_DBG_STAT );
		return NDIS_STATUS_INVALID_LENGTH;
	}

	IPOIB_EXIT( IPOIB_DBG_STAT );
	return NDIS_STATUS_SUCCESS;
}


void
ipoib_inc_recv_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN		const	size_t						bytes OPTIONAL )
{
	IPOIB_ENTER( IPOIB_DBG_STAT );

	cl_spinlock_acquire( &p_adapter->recv_stat_lock );
	switch( stat_sel )
	{
	case IP_STAT_ERROR:
		p_adapter->recv_stats.comp.error++;
		break;

	case IP_STAT_DROPPED:
		p_adapter->recv_stats.comp.dropped++;
		break;

	case IP_STAT_UCAST_BYTES:
	case IP_STAT_UCAST_FRAMES:
		p_adapter->recv_stats.comp.success++;
		p_adapter->recv_stats.ucast.frames++;
		p_adapter->recv_stats.ucast.bytes += bytes;
		break;

	case IP_STAT_BCAST_BYTES:
	case IP_STAT_BCAST_FRAMES:
		p_adapter->recv_stats.comp.success++;
		p_adapter->recv_stats.bcast.frames++;
		p_adapter->recv_stats.bcast.bytes += bytes;
		break;

	case IP_STAT_MCAST_BYTES:
	case IP_STAT_MCAST_FRAMES:
		p_adapter->recv_stats.comp.success++;
		p_adapter->recv_stats.mcast.frames++;
		p_adapter->recv_stats.mcast.bytes += bytes;
		break;

	default:
		break;
	}
	cl_spinlock_release( &p_adapter->recv_stat_lock );

	IPOIB_EXIT( IPOIB_DBG_STAT );
}

NDIS_STATUS
ipoib_get_send_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN				pending_oid_t* const		p_oid_info )
{
	uint64_t	stat;

	IPOIB_ENTER( IPOIB_DBG_STAT );

	CL_ASSERT( p_adapter );

	cl_spinlock_acquire( &p_adapter->send_stat_lock );
	switch( stat_sel )
	{
	case IP_STAT_SUCCESS:
		stat = p_adapter->send_stats.comp.success;
		break;

	case IP_STAT_ERROR:
		stat = p_adapter->send_stats.comp.error;
		break;

	case IP_STAT_DROPPED:
		stat = p_adapter->send_stats.comp.dropped;
		break;

	case IP_STAT_UCAST_BYTES:
		stat = p_adapter->send_stats.ucast.bytes;
		break;

	case IP_STAT_UCAST_FRAMES:
		stat = p_adapter->send_stats.ucast.frames;
		break;

	case IP_STAT_BCAST_BYTES:
		stat = p_adapter->send_stats.bcast.bytes;
		break;

	case IP_STAT_BCAST_FRAMES:
		stat = p_adapter->send_stats.bcast.frames;
		break;

	case IP_STAT_MCAST_BYTES:
		stat = p_adapter->send_stats.mcast.bytes;
		break;

	case IP_STAT_MCAST_FRAMES:
		stat = p_adapter->send_stats.mcast.frames;
		break;

	default:
		stat = 0;
	}
	cl_spinlock_release( &p_adapter->send_stat_lock );

	*p_oid_info->p_bytes_needed = sizeof(uint64_t);

	if( p_oid_info->buf_len >= sizeof(uint64_t) )
	{
		*((uint64_t*)p_oid_info->p_buf) = stat;
		*p_oid_info->p_bytes_used = sizeof(uint64_t);
	}
	else if( p_oid_info->buf_len >= sizeof(uint32_t) )
	{
		*((uint32_t*)p_oid_info->p_buf) = (uint32_t)stat;
		*p_oid_info->p_bytes_used = sizeof(uint32_t);
	}
	else
	{
		*p_oid_info->p_bytes_used = 0;
		IPOIB_EXIT( IPOIB_DBG_STAT );
		return NDIS_STATUS_INVALID_LENGTH;
	}

	IPOIB_EXIT( IPOIB_DBG_STAT );
	return NDIS_STATUS_SUCCESS;
}


void
ipoib_inc_send_stat(
	IN				ipoib_adapter_t* const		p_adapter,
	IN		const	ip_stat_sel_t				stat_sel,
	IN		const	size_t						bytes OPTIONAL )
{
	IPOIB_ENTER( IPOIB_DBG_STAT );

	cl_spinlock_acquire( &p_adapter->send_stat_lock );
	switch( stat_sel )
	{
	case IP_STAT_ERROR:
		p_adapter->send_stats.comp.error++;
		break;

	case IP_STAT_DROPPED:
		p_adapter->send_stats.comp.dropped++;
		break;

	case IP_STAT_UCAST_BYTES:
	case IP_STAT_UCAST_FRAMES:
		p_adapter->send_stats.comp.success++;
		p_adapter->send_stats.ucast.frames++;
		p_adapter->send_stats.ucast.bytes += bytes;
		break;

	case IP_STAT_BCAST_BYTES:
	case IP_STAT_BCAST_FRAMES:
		p_adapter->send_stats.comp.success++;
		p_adapter->send_stats.bcast.frames++;
		p_adapter->send_stats.bcast.bytes += bytes;
		break;

	case IP_STAT_MCAST_BYTES:
	case IP_STAT_MCAST_FRAMES:
		p_adapter->send_stats.comp.success++;
		p_adapter->send_stats.mcast.frames++;
		p_adapter->send_stats.mcast.bytes += bytes;
		break;

	default:
		break;
	}
	cl_spinlock_release( &p_adapter->send_stat_lock );

	IPOIB_EXIT( IPOIB_DBG_STAT );
}
