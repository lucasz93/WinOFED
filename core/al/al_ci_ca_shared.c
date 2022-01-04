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

#include "al_ci_ca.h"
#include "al_common.h"
#include "al_cq.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ci_ca_shared.tmh"
#endif

#include "al_mgr.h"
#include "al_pnp.h"
#include "al_qp.h"
#include "al_srq.h"
#include "ib_common.h"


void
ci_ca_process_event_cb(
	IN				cl_async_proc_item_t*		p_async_item );

void
ca_process_async_event_cb(
	IN		const	ib_async_event_rec_t* const	p_event_rec );

void
ca_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec );


void
free_ci_ca(
	IN				al_obj_t*					p_obj )
{
	al_ci_ca_t				*p_ci_ca;

	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	cl_spinlock_destroy( &p_ci_ca->attr_lock );
	cl_qpool_destroy( &p_ci_ca->event_pool );
	cl_event_destroy( &p_ci_ca->event );

	if( p_ci_ca->port_array )
		cl_free( p_ci_ca->port_array );

	/* Free the PnP attributes buffer. */
	if( p_ci_ca->p_pnp_attr )
		cl_free( p_ci_ca->p_pnp_attr );

	destroy_al_obj( p_obj );
	cl_free( p_ci_ca );
}

void
add_ca(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_ca_handle_t				h_ca )
{
	cl_spinlock_acquire( &p_ci_ca->obj.lock );
	cl_qlist_insert_tail( &p_ci_ca->ca_list, &h_ca->list_item );
	ref_ctx_al_obj( &p_ci_ca->obj, E_REF_CA_ADD_REMOVE );
	cl_spinlock_release( &p_ci_ca->obj.lock );
}



void
remove_ca(
	IN		const	ib_ca_handle_t				h_ca )
{
	al_ci_ca_t			*p_ci_ca;

	p_ci_ca = h_ca->obj.p_ci_ca;

	cl_spinlock_acquire( &p_ci_ca->obj.lock );
	cl_qlist_remove_item( &p_ci_ca->ca_list, &h_ca->list_item );
	cl_spinlock_release( &p_ci_ca->obj.lock );
	deref_ctx_al_obj( &p_ci_ca->obj, E_REF_CA_ADD_REMOVE );
}



ib_api_status_t
get_port_info(
	IN				al_ci_ca_t					*p_ci_ca )
{
	ib_api_status_t		status;
	ib_ca_attr_t		*p_ca_attr;
	uint32_t			attr_size;
	uint8_t				i;

	AL_ENTER( AL_DBG_CA );

	/* Get the size of the CA attribute structure. */
	status = ib_query_ca( p_ci_ca->h_ca, NULL, &attr_size );
	if( status != IB_INSUFFICIENT_MEMORY )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_query_ca failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Allocate enough space to store the attribute structure. */
	p_ca_attr = cl_malloc( attr_size );
	if( !p_ca_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_malloc failed to allocate p_ca_attr!\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Query the CA attributes. */
	status = ib_query_ca( p_ci_ca->h_ca, p_ca_attr, &attr_size );
	if( status != IB_SUCCESS )
	{
		cl_free( p_ca_attr );

		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_query_ca failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Allocate the port GUID array. */
	p_ci_ca->port_array = cl_malloc( sizeof( ib_net64_t ) *
		p_ca_attr->num_ports );
	if( !p_ci_ca->port_array )
	{
		cl_free( p_ca_attr );

		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_malloc failed to allocate port_array!\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}
	p_ci_ca->num_ports = p_ca_attr->num_ports;

	/* Copy the necessary port information. */
	for( i = 0; i < p_ca_attr->num_ports; i++ )
	{
		p_ci_ca->port_array[i] = p_ca_attr->p_port_attr[i].port_guid;

#ifdef CL_KERNEL
		/* Set the port's client reregister bit. */
		{
			ib_port_attr_mod_t attr;

			attr.cap.client_reregister = TRUE;
			ib_modify_ca( p_ci_ca->h_ca, i + 1,
				IB_CA_MOD_IS_CLIENT_REREGISTER_SUPPORTED, &attr );
		}
#endif
	}

	cl_free( p_ca_attr );

	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}



void
ci_ca_async_event(
	IN		const	ib_async_event_rec_t* const	p_event_rec )
{
	al_obj_t*				p_obj;
	cl_pool_item_t*			p_item;
	event_item_t*			p_event_item;

	AL_ENTER( AL_DBG_CA );

	CL_ASSERT( p_event_rec );

	p_obj = (al_obj_t*)p_event_rec->context;

	/* Block the destruction of the object until a reference is taken. */
	cl_spinlock_acquire( &p_obj->lock );
	if( p_obj->state == CL_DESTROYING )
	{
		/* Ignore events if the object is being destroyed. */
		cl_spinlock_release( &p_obj->lock );
		AL_EXIT( AL_DBG_CA );
		return;
	}

	/*
	 * Get an event item from the pool.  If an object is a child of
	 * a CA (e.g., CQ or QP) it will have valid p_ci_ca pointer.
	 * For CA's, the object is the actual p_ci_ca pointer itself.
	 */
	if( p_obj->p_ci_ca )
	{
		cl_spinlock_acquire( &p_obj->p_ci_ca->obj.lock );
		p_item = cl_qpool_get( &p_obj->p_ci_ca->event_pool );
		cl_spinlock_release( &p_obj->p_ci_ca->obj.lock );
	}
	else
	{
		p_item = cl_qpool_get( &((al_ci_ca_t*)p_obj)->event_pool );
	}
	if( !p_item )
	{
		/* Could not get an item.  This event will not be reported. */
		cl_spinlock_release( &p_obj->lock );
		AL_EXIT( AL_DBG_CA );
		return;
	}

	/* Hold a reference to prevent destruction until the async_item runs. */
	ref_ctx_al_obj( p_obj , E_REF_CI_CA_ASYNC_EVENT);

	cl_spinlock_release( &p_obj->lock );

	/* Initialize the item with the asynchronous event information. */
	p_event_item = PARENT_STRUCT( p_item, event_item_t, async_item.pool_item );
	p_event_item->event_rec.code = p_event_rec->code;
	p_event_item->event_rec.context = p_event_rec->context;
	p_event_item->event_rec.port_number = p_event_rec->port_number;

	/* Queue the item on the asynchronous callback thread for processing. */
	p_event_item->async_item.pfn_callback = ci_ca_process_event_cb;
	cl_async_proc_queue( gp_async_proc_mgr, &p_event_item->async_item );

	AL_EXIT( AL_DBG_CA );
}



void
ci_ca_process_event_cb(
	IN				cl_async_proc_item_t*		p_async_item )
{
	event_item_t*			p_event_item;
	al_obj_t*				p_obj;

	AL_ENTER( AL_DBG_CA );

	CL_ASSERT( p_async_item );

	p_event_item = PARENT_STRUCT( p_async_item, event_item_t,
		async_item.pool_item );

	p_obj = (al_obj_t*)p_event_item->event_rec.context;

	switch( p_event_item->event_rec.code )
	{
	case IB_AE_QP_COMM:
	case IB_AE_QP_APM:
	case IB_AE_QP_APM_ERROR:
	case IB_AE_QP_FATAL:
	case IB_AE_RQ_ERROR:
	case IB_AE_SQ_ERROR:
	case IB_AE_SQ_DRAINED:
	case IB_AE_WQ_REQ_ERROR:
	case IB_AE_WQ_ACCESS_ERROR:
	case IB_AE_SRQ_QP_LAST_WQE_REACHED:
		qp_async_event_cb( &p_event_item->event_rec );
		break;

	case IB_AE_SRQ_LIMIT_REACHED:
	case IB_AE_SRQ_CATAS_ERROR:
		srq_async_event_cb( &p_event_item->event_rec );
		break;

	case IB_AE_CQ_ERROR:
		cq_async_event_cb( &p_event_item->event_rec );
		break;

#ifdef CL_KERNEL

	case IB_AE_LID_CHANGE:
	case IB_AE_CLIENT_REREGISTER:
		// These AE events will be generated even in the case when
		// SM was restaretd but LID will not actually change.
		// It's important to propagate these event (via PnP mechanism)
		// up to subscribers. Otherwise, there will be no ping after
		// subnet manager restart
		//if (AL_OBJ_IS_TYPE(p_obj, AL_OBJ_TYPE_CI_CA)
		if (AL_BASE_TYPE( p_obj->type) == AL_OBJ_TYPE_CI_CA) {
				pnp_force_event( (struct _al_ci_ca *) p_obj, IB_PNP_LID_CHANGE,
					p_event_item->event_rec.port_number );
				force_smi_poll();
		}
		break;
#endif //CL_KERNEL

	case IB_AE_PORT_TRAP:
	case IB_AE_PORT_DOWN:
	case IB_AE_PORT_ACTIVE:
	
#ifdef CL_KERNEL
		/* The SMI polling routine may report a PnP event. */
		force_smi_poll();
#endif
		/* Fall through next case. */

	case IB_AE_LOCAL_FATAL:
		ca_process_async_event_cb( &p_event_item->event_rec );
		break;

	/* Unhandled events - optional per IBA spec. */
	case IB_AE_QKEY_TRAP:
	case IB_AE_PKEY_TRAP:
	case IB_AE_MKEY_TRAP:
	case IB_AE_BKEY_TRAP:
	case IB_AE_BUF_OVERRUN:
	case IB_AE_LINK_INTEGRITY:
	case IB_AE_FLOW_CTRL_ERROR:
	case IB_AE_SYSIMG_GUID_TRAP:
	default:
		break;
	}

	/*
	 * Return the event item to the pool.  If an object is a child of
	 * a CA (e.g., CQ or QP) it will have valid p_ci_ca pointer.
	 * For CA's, the object is the actual p_ci_ca pointer itself.
	 */
	if( p_obj->p_ci_ca )
	{
		cl_spinlock_acquire( &p_obj->p_ci_ca->obj.lock );
		cl_qpool_put( &p_obj->p_ci_ca->event_pool,
			&p_event_item->async_item.pool_item );
		cl_spinlock_release( &p_obj->p_ci_ca->obj.lock );
	}
	else
	{
		cl_spinlock_acquire( &p_obj->lock );
		cl_qpool_put( &((al_ci_ca_t*)p_obj)->event_pool,
			&p_event_item->async_item.pool_item );
		cl_spinlock_release( &p_obj->lock );
	}

	/* Dereference the object. */
	deref_ctx_al_obj( p_obj , E_REF_CI_CA_ASYNC_EVENT);

	AL_EXIT( AL_DBG_CA );
}



/*
 * Process an asynchronous event on a CA.  Notify all clients of the event.
 */
void
ca_process_async_event_cb(
	IN		const	ib_async_event_rec_t* const	p_event_rec )
{
	al_ci_ca_t*				p_ci_ca;
	cl_list_item_t*			p_list_item;
	ib_ca_handle_t			h_ca;
	ib_async_event_rec_t	event_rec;

	CL_ASSERT( p_event_rec );
	p_ci_ca = (al_ci_ca_t*)p_event_rec->context;

	/* Report the CA event to all clients. */
	cl_spinlock_acquire( &p_ci_ca->obj.lock );
	for( p_list_item = cl_qlist_head( &p_ci_ca->ca_list );
		 p_list_item != cl_qlist_end( &p_ci_ca->ca_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		cl_spinlock_release( &p_ci_ca->obj.lock );

		h_ca = PARENT_STRUCT( p_list_item, ib_ca_t, list_item );

		event_rec = *p_event_rec;
		event_rec.handle.h_ca = h_ca;
		ca_async_event_cb( &event_rec );

		cl_spinlock_acquire( &p_ci_ca->obj.lock );
	}
	cl_spinlock_release( &p_ci_ca->obj.lock );
}



/*
 * Process an asynchronous event on a CA.  Notify the user of the event.
 */
void
ca_async_event_cb(
	IN				ib_async_event_rec_t* const	p_event_rec )
{
	ib_ca_handle_t			h_ca;

	CL_ASSERT( p_event_rec );
	h_ca = p_event_rec->handle.h_ca;

	p_event_rec->context = (void*)h_ca->obj.context;
	if( h_ca->pfn_event_cb )
		h_ca->pfn_event_cb( p_event_rec );
}



void
ca_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
}



/*
 * Returns a port's index on its CA for the given port GUID.
 */
ib_api_status_t
get_port_num(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_net64_t					port_guid,
		OUT			uint8_t						*p_port_num OPTIONAL )
{
	uint8_t		i;

	/* Find a matching port GUID on this CI CA. */
	for( i = 0; i < p_ci_ca->num_ports; i++ )
	{
		if( p_ci_ca->port_array[i] == port_guid )
		{
			/* The port number is the index plus one. */
			if( p_port_num )
				*p_port_num = (uint8_t)(i + 1);
			return IB_SUCCESS;
		}
	}

	/* The port GUID was not found. */
	return IB_INVALID_GUID;
}



ib_port_attr_t*
get_port_attr(
	IN				ib_ca_attr_t * const		p_ca_attr,
	IN				ib_gid_t * const			p_gid )
{
	uintn_t			port_index, gid_index;
	ib_port_attr_t	*p_port_attr;

	AL_ENTER( AL_DBG_CA );

	if( !p_ca_attr || !p_gid )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("No p_ca_attr or p_gid.\n") );
		return NULL;
	}

	/* Check all ports on this HCA for matching GID. */
	for( port_index = 0; port_index < p_ca_attr->num_ports; port_index++ )
	{
		p_port_attr = &p_ca_attr->p_port_attr[port_index];

		for( gid_index = 0; gid_index < p_port_attr->num_gids; gid_index++ )
		{
			if( !cl_memcmp( &p_port_attr->p_gid_table[gid_index],
				p_gid, sizeof(ib_gid_t) ) )
			{
				AL_EXIT( AL_DBG_CA );
				return p_port_attr;
			}
		}
	}

	AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("No match found.\n") );
	return NULL;
}



uint16_t
get_pkey_index(
	IN				ib_port_attr_t * const		p_port_attr,
	IN		const	ib_net16_t					pkey )
{
	uint16_t			pkey_index;

	if( !p_port_attr )
		return BAD_PKEY_INDEX;

	for( pkey_index = 0; pkey_index < p_port_attr->num_pkeys; pkey_index++ )
	{
		if( p_port_attr->p_pkey_table[pkey_index] == pkey )
			return pkey_index;
	}
	return BAD_PKEY_INDEX;
}


/*
 * Reads the CA attributes from verbs.
 */
ib_api_status_t
ci_ca_update_attr(
	IN				al_ci_ca_t*					p_ci_ca,
		OUT			ib_ca_attr_t**				pp_old_pnp_attr )
{
	ib_ca_attr_t		*p_pnp_attr;
	uint32_t			attr_size;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CA );

	/* Query to get the CA attributes size. */
	attr_size = 0;
	status = ib_query_ca( p_ci_ca->h_ca, NULL, &attr_size );
	if( status != IB_INSUFFICIENT_MEMORY )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_CA,
			("Unable to query attributes size\n") );
		return status;
	}

	/*
	 * Allocate the new CA attributes buffer.
	 * Double the buffer size for PnP and user reporting halves.
	 */
	p_pnp_attr = (ib_ca_attr_t*)cl_zalloc( attr_size * 2 );
	if( !p_pnp_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_CA,
			("Unable to allocate buffer for PnP attributes\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Read the attributes. */
	status = ib_query_ca( p_ci_ca->h_ca, p_pnp_attr, &attr_size );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_CA,
			("Unable to query attributes\n") );
		cl_free( p_pnp_attr );
		return status;
	}

	ci_ca_excl_lock_attr( p_ci_ca );
	if( pp_old_pnp_attr )
		*pp_old_pnp_attr = p_ci_ca->p_pnp_attr;
	p_ci_ca->p_pnp_attr = p_pnp_attr;

	/*
	 * Initialize pointer to the user reporting half.
	 * This buffer is used to report this CAs attributes to users.
	 */
	p_ci_ca->p_user_attr = (ib_ca_attr_t*)(((uint8_t*)p_pnp_attr) + attr_size);
	ci_ca_unlock_attr( p_ci_ca );

	AL_EXIT( AL_DBG_CA );
	return IB_SUCCESS;
}

