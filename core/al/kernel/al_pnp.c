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

#include <iba/ib_al.h>

#include "al.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_pnp.tmh"
#endif
#include "al_mgr.h"
#include "al_pnp.h"
#include "ib_common.h"
#include "al_ioc_pnp.h"


#define PNP_CA_VECTOR_MIN		0
#define PNP_CA_VECTOR_GROW		10


/*
 * Declarations.
 */
static void
__pnp_free(
	IN				al_obj_t					*p_obj );


/*
 * Compares two context for inserts/lookups in a flexi map.  Keys are the
 * address of the reg guid1, which is adjacent to the context guid2 (if exist).
 * This allows for a single call to cl_memcmp.
 */
static int
__context_cmp128(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return cl_memcmp( p_key1, p_key2, sizeof(uint64_t) * 2 );
}

/*
 * Compares two context for inserts/lookups in a flexi map.  Keys are the
 * address of the reg guid1, which is adjacent to the context guid2 (if exist).
 * This allows for a single call to cl_memcmp.
 */
static int
__context_cmp64(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return cl_memcmp( p_key1, p_key2, sizeof(uint64_t) );
}


/*
 * Event structures for queuing to the async proc manager.
 */
typedef struct _al_pnp_ca_change
{
	cl_async_proc_item_t	async_item;
	al_ci_ca_t				*p_ci_ca;
	ib_ca_attr_t			*p_new_ca_attr;

}	al_pnp_ca_change_t;


typedef struct _al_pnp_ca_event
{
	cl_async_proc_item_t	async_item;
	ib_pnp_event_t			pnp_event;
	al_ci_ca_t				*p_ci_ca;
	uint8_t					port_index;

}	al_pnp_ca_event_t;


typedef struct _al_pnp_reg_event
{
	cl_async_proc_item_t	async_item;
	al_pnp_t				*p_reg;

}	al_pnp_reg_event_t;


/* PnP Manager structure. */
typedef struct _al_pnp_mgr
{
	al_obj_t				obj;

	cl_qlist_t				ca_reg_list;
	cl_qlist_t				port_reg_list;

	cl_ptr_vector_t			ca_vector;

	cl_async_proc_item_t	async_item;
	boolean_t				async_item_is_busy;

}	al_pnp_mgr_t;


/*
 * PnP Manager instance, creation, destruction.
 */

/* Global instance of the PnP manager. */
al_pnp_mgr_t	*gp_pnp = NULL;


static void
__pnp_check_events(
	IN				cl_async_proc_item_t*	p_item );

static void
__al_pnp_process_dereg(
	IN				cl_async_proc_item_t*		p_item );


ib_api_status_t
create_pnp(
	IN				al_obj_t* const			p_parent_obj )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( gp_pnp == NULL );

	gp_pnp = (al_pnp_mgr_t*)cl_zalloc( sizeof(al_pnp_mgr_t) );
	if( !gp_pnp )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to allocate PnP manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	cl_qlist_init( &gp_pnp->ca_reg_list );
	cl_qlist_init( &gp_pnp->port_reg_list );
	construct_al_obj( &gp_pnp->obj, AL_OBJ_TYPE_PNP_MGR );
	cl_ptr_vector_construct( &gp_pnp->ca_vector );

	cl_status = cl_ptr_vector_init( &gp_pnp->ca_vector, PNP_CA_VECTOR_MIN,
		PNP_CA_VECTOR_GROW );
	if( cl_status != CL_SUCCESS )
	{
		__pnp_free( &gp_pnp->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_ptr_vector_init failed with status %#x.\n",
			cl_status) );
		return IB_ERROR;
	}

	gp_pnp->async_item.pfn_callback = __pnp_check_events;

	status = init_al_obj( &gp_pnp->obj, NULL, TRUE, NULL, NULL, __pnp_free );
	if( status != IB_SUCCESS )
	{
		__pnp_free( &gp_pnp->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj() failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}
	status = attach_al_obj( p_parent_obj, &gp_pnp->obj );
	if( status != IB_SUCCESS )
	{
		gp_pnp->obj.pfn_destroy( &gp_pnp->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_pnp->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_PNP );
	return( IB_SUCCESS );
}


static void
__pnp_free(
	IN				al_obj_t					*p_obj )
{
	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( PARENT_STRUCT( p_obj, al_pnp_mgr_t, obj ) == gp_pnp );
	CL_ASSERT( cl_is_qlist_empty( &gp_pnp->ca_reg_list ) );
	CL_ASSERT( cl_is_qlist_empty( &gp_pnp->port_reg_list ) );
	UNUSED_PARAM( p_obj );

	/* All CA's should have been removed by now. */
	CL_ASSERT( !cl_ptr_vector_get_size( &gp_pnp->ca_vector ) );
	cl_ptr_vector_destroy( &gp_pnp->ca_vector );

	destroy_al_obj( &gp_pnp->obj );
	cl_free( gp_pnp );
	gp_pnp = NULL;

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_reg_destroying(
	IN				al_obj_t					*p_obj )
{
	al_pnp_t		*p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_obj, al_pnp_t, obj );

	/* Reference the registration entry while we queue it to our PnP thread. */
	ref_al_obj( &p_reg->obj );

	/* Queue the registration for removal from the list. */
	cl_async_proc_queue( gp_async_pnp_mgr, &p_reg->dereg_item );

	AL_EXIT( AL_DBG_PNP );
}


static void
__al_pnp_process_dereg(
	IN				cl_async_proc_item_t*		p_item )
{
	al_pnp_t*		p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, dereg_item );

	/* Remove the registration information from the list. */
	switch( pnp_get_class( p_reg->pnp_class ) )
	{
	case IB_PNP_CA:
		cl_qlist_remove_item( &gp_pnp->ca_reg_list, &p_reg->list_item );
		break;

	case IB_PNP_PORT:
		cl_qlist_remove_item( &gp_pnp->port_reg_list, &p_reg->list_item );
		break;

	default:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_CA ||
			pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid PnP registartion type.\n") );
	}

	/* Release the reference we took for processing the deregistration. */
	deref_al_obj( &p_reg->obj );

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_reg_cleanup(
	IN				al_obj_t					*p_obj )
{
	al_pnp_t		*p_reg;
	cl_fmap_item_t	*p_item;
	IRP				*p_irp;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_obj, al_pnp_t, obj );

	/* Cleanup the context list. */
	while( cl_fmap_count( &p_reg->context_map ) )
	{
		p_item = cl_fmap_tail( &p_reg->context_map );
		cl_fmap_remove_item( &p_reg->context_map, p_item );
		cl_free( p_item );
	}

	p_irp = InterlockedExchangePointer( &p_reg->p_rearm_irp, NULL );
	if( p_irp )
	{
#pragma warning(push, 3)
		IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
		/* Complete the IRP. */
		p_irp->IoStatus.Status = STATUS_CANCELLED;
		p_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	}

	if( p_reg->p_dereg_irp )
	{
		p_reg->p_dereg_irp->IoStatus.Status = STATUS_SUCCESS;
		p_reg->p_dereg_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_reg->p_dereg_irp, IO_NO_INCREMENT );
		p_reg->p_dereg_irp = NULL;
	}

	/* Dereference the PnP manager. */
	deref_al_obj( &gp_pnp->obj );

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_reg_free(
	IN				al_obj_t					*p_obj )
{
	al_pnp_t		*p_reg;
	cl_fmap_item_t	*p_item;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_obj, al_pnp_t, obj );

	/* Cleanup the context list. */
	while( cl_fmap_count( &p_reg->context_map ) )
	{
		p_item = cl_fmap_tail( &p_reg->context_map );
		cl_fmap_remove_item( &p_reg->context_map, p_item );
		cl_free( p_item );
	}

	/* Free the registration structure. */
	destroy_al_obj( &p_reg->obj );
	cl_free( p_reg );

	AL_EXIT( AL_DBG_PNP );
}


/*
 * Helper functions.
 */



/*
 * Returns the context structure stored in a registration for
 * a given CA or port GUID.
 */
al_pnp_context_t*
pnp_get_context(
	IN		const	al_pnp_t* const				p_reg,
	IN				const void* const				p_key )
{
	cl_fmap_item_t		*p_context_item;

	AL_ENTER( AL_DBG_PNP );

	/* Search the context list for this CA. */
	p_context_item = cl_fmap_get( &p_reg->context_map, p_key );
	if( p_context_item != cl_fmap_end( &p_reg->context_map ) )
	{
		AL_EXIT( AL_DBG_PNP );
		return PARENT_STRUCT( p_context_item, al_pnp_context_t, map_item );
	}

	AL_EXIT( AL_DBG_PNP );
	return NULL;
}


void
pnp_reg_complete(
	IN				al_pnp_t* const				p_reg )
{
	ib_pnp_rec_t			user_rec;

	AL_ENTER( AL_DBG_PNP );

	/* Notify the user that the registration is complete. */
	if( (pnp_get_flag( p_reg->pnp_class ) & IB_PNP_FLAG_REG_COMPLETE) )
	{
		/* Setup the PnP record for the callback. */
		cl_memclr( &user_rec, sizeof(user_rec) );
		user_rec.h_pnp = p_reg;
		user_rec.pnp_event = IB_PNP_REG_COMPLETE;
		user_rec.pnp_context = (void*)p_reg->obj.context;

		/* Invoke the user callback. */
		p_reg->pfn_pnp_cb( &user_rec );
	}

	if( pnp_get_flag( p_reg->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
	{
		KeSetEvent( p_reg->p_sync_event, 0, FALSE );
		/*
		 * Proxy synchronizes PnP callbacks with registration, and thus can
		 * safely set the UM_EXPORT subtype after al_reg_pnp returns.
		 */
		if( p_reg->obj.type & AL_OBJ_SUBTYPE_UM_EXPORT )
			ObDereferenceObject( p_reg->p_sync_event );
		p_reg->p_sync_event = NULL;
	}

	AL_EXIT( AL_DBG_PNP );
}

/*
 * User notification.  Formats the PnP record delivered by the
 * callback, invokes the callback, and updates the contexts.
 */
static ib_api_status_t
__pnp_notify_user(
	IN				al_pnp_t* const				p_reg,
	IN				al_pnp_context_t* const		p_context,
	IN		const	al_pnp_ca_event_t* const	p_event_rec )
{
	ib_api_status_t			status;
	union
	{
		ib_pnp_rec_t		user_rec;
		ib_pnp_ca_rec_t		ca_rec;
		ib_pnp_port_rec_t	port_rec;
	}	u;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_reg );
	CL_ASSERT( p_context );
	CL_ASSERT( p_event_rec );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_event_rec->pnp_event = 0x%x (%s)\n",
		p_event_rec->pnp_event, ib_get_pnp_event_str( p_event_rec->pnp_event )) );

	/* Setup the PnP record for the callback. */
	cl_memclr( &u, sizeof(u) );
	u.user_rec.h_pnp = p_reg;
	u.user_rec.pnp_event = p_event_rec->pnp_event;
	u.user_rec.pnp_context = (void*)p_reg->obj.context;
	u.user_rec.context = (void*)p_context->context;

	switch( p_event_rec->pnp_event )
	{
	case IB_PNP_CA_ADD:
		/* Copy the attributes for use in calling users back. */
		u.ca_rec.p_ca_attr = ib_copy_ca_attr(
			p_event_rec->p_ci_ca->p_user_attr,
			p_event_rec->p_ci_ca->p_pnp_attr );

		/* Fall through */
	case IB_PNP_CA_REMOVE:
		u.user_rec.guid = p_event_rec->p_ci_ca->p_pnp_attr->ca_guid;
		break;

	case IB_PNP_PORT_ADD:
	case IB_PNP_PORT_INIT:
	case IB_PNP_PORT_ARMED:
	case IB_PNP_PORT_ACTIVE:
	case IB_PNP_PORT_DOWN:
	case IB_PNP_PKEY_CHANGE:
	case IB_PNP_SM_CHANGE:
	case IB_PNP_GID_CHANGE:
	case IB_PNP_LID_CHANGE:
	case IB_PNP_SUBNET_TIMEOUT_CHANGE:
		/* Copy the attributes for use in calling users back. */
		u.port_rec.p_ca_attr = ib_copy_ca_attr(
			p_event_rec->p_ci_ca->p_user_attr,
			p_event_rec->p_ci_ca->p_pnp_attr );

		/* Setup the port attribute pointer. */
		u.port_rec.p_port_attr =
			&u.port_rec.p_ca_attr->p_port_attr[p_event_rec->port_index];

		/* Fall through */
	case IB_PNP_PORT_REMOVE:
		u.user_rec.guid = p_event_rec->p_ci_ca->p_pnp_attr->p_port_attr[ 
			p_event_rec->port_index].port_guid;
		break;

	case IB_PNP_REG_COMPLETE:
		break;

	default:
		/* Invalid event type. */
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid event type (%d).\n", p_event_rec->pnp_event) );
		CL_ASSERT( p_event_rec->pnp_event == IB_PNP_CA_ADD ||
			p_event_rec->pnp_event == IB_PNP_PORT_ADD ||
			p_event_rec->pnp_event == IB_PNP_PORT_INIT ||
			p_event_rec->pnp_event == IB_PNP_PORT_ACTIVE ||
			p_event_rec->pnp_event == IB_PNP_PORT_DOWN ||
			p_event_rec->pnp_event == IB_PNP_PKEY_CHANGE ||
			p_event_rec->pnp_event == IB_PNP_SM_CHANGE ||
			p_event_rec->pnp_event == IB_PNP_GID_CHANGE ||
			p_event_rec->pnp_event == IB_PNP_LID_CHANGE ||
			p_event_rec->pnp_event == IB_PNP_SUBNET_TIMEOUT_CHANGE ||
			p_event_rec->pnp_event == IB_PNP_CA_REMOVE ||
			p_event_rec->pnp_event == IB_PNP_PORT_REMOVE );
		return IB_SUCCESS;
	}

	/* Invoke the user callback. */
	status = p_reg->pfn_pnp_cb( &u.user_rec );

	if( status == IB_SUCCESS )
	{
		/* Store the user's event context in the context block. */
		p_context->context = u.user_rec.context;
	}
	else
	{
		cl_fmap_remove_item( &p_reg->context_map, &p_context->map_item );
		cl_free( p_context );
	}

	AL_EXIT( AL_DBG_PNP );
	return status;
}



/*
 * Context creation.
 */
al_pnp_context_t*
pnp_create_context(
	IN				al_pnp_t* const				p_reg,
	IN				const void* const				p_key )
{
	al_pnp_context_t	*p_context;
	cl_fmap_item_t		*p_item;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_reg );

	/* No context exists for this port.  Create one. */
	p_context = (al_pnp_context_t*)cl_zalloc( sizeof(al_pnp_context_t) );
	if( !p_context )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to cl_zalloc al_pnp_context_t (%I64d bytes).\n",
			sizeof(al_pnp_context_t)) );
		return NULL;
	}
	/* Store the GUID in the context record. */
	cl_memcpy(&p_context->guid, p_key, sizeof(ib_net64_t) * 2);

	/* Add the context to the context list. */
	p_item = cl_fmap_insert( &p_reg->context_map, &p_context->guid,
		&p_context->map_item );
	if( p_item != &p_context->map_item )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
			("p_context is already in context map %I64x \n",p_context->guid));
		cl_free( p_context );
		p_context = NULL;
	}
	
	
	AL_EXIT( AL_DBG_PNP );
	return p_context;
}



/*
 * Report all local port information.  This notifies the user of PORT_ADD
 * events, along with port state events (PORT_INIT, PORT_ACTIVE).
 */
static void
__pnp_port_notify(
	IN				al_pnp_t					*p_reg,
	IN				al_ci_ca_t					*p_ci_ca )
{
	ib_api_status_t			status;
	al_pnp_context_t		*p_context;
	ib_port_attr_t			*p_port_attr;
	al_pnp_ca_event_t		event_rec;

	AL_ENTER( AL_DBG_PNP );

	event_rec.p_ci_ca = p_ci_ca;

	for( event_rec.port_index = 0;
		 event_rec.port_index < p_ci_ca->num_ports;
		 event_rec.port_index++ )
	{
		p_port_attr = p_ci_ca->p_pnp_attr->p_port_attr;
		p_port_attr += event_rec.port_index;

		/* Create a new context for user port information. */
		p_context = pnp_create_context( p_reg, &p_port_attr->port_guid);
		if( !p_context )
			continue;

		/* Notify the user of the port's existence. */
		event_rec.pnp_event = IB_PNP_PORT_ADD;
		status = __pnp_notify_user( p_reg, p_context, &event_rec );
		if( status != IB_SUCCESS )
			continue;

		/* Generate a port down event if the port is currently down. */
		if( p_port_attr->link_state == IB_LINK_DOWN )
		{
			event_rec.pnp_event = IB_PNP_PORT_DOWN;
			__pnp_notify_user( p_reg, p_context, &event_rec );
		}
		else
		{
			/* Generate port init event. */
			if( p_port_attr->link_state >= IB_LINK_INIT )
			{
				event_rec.pnp_event = IB_PNP_PORT_INIT;
				status = __pnp_notify_user( p_reg, p_context, &event_rec );
				if( status != IB_SUCCESS )
					continue;
			}
			/* Generate port armed event. */
			if( p_port_attr->link_state >= IB_LINK_ARMED )
			{
				event_rec.pnp_event = IB_PNP_PORT_ARMED;
				status = __pnp_notify_user( p_reg, p_context, &event_rec );
				if( status != IB_SUCCESS )
					continue;
			}
			/* Generate port up event. */
			if( p_port_attr->link_state >= IB_LINK_ACTIVE )
			{
				event_rec.pnp_event = IB_PNP_PORT_ACTIVE;
				__pnp_notify_user( p_reg, p_context, &event_rec );
			}
		}
	}
	AL_EXIT( AL_DBG_PNP );
}


/*
 * Registration and deregistration.
 */
static void
__pnp_reg_notify(
	IN				al_pnp_t* const				p_reg )
{
	al_pnp_ca_event_t		event_rec;
	size_t					i;
	al_pnp_context_t		*p_context;

	AL_ENTER( AL_DBG_PNP );

	for( i = 0; i < cl_ptr_vector_get_size( &gp_pnp->ca_vector ); i++ )
	{
		event_rec.p_ci_ca = (al_ci_ca_t*)
			cl_ptr_vector_get( &gp_pnp->ca_vector, i );
		if( !event_rec.p_ci_ca )
			continue;

		switch( pnp_get_class( p_reg->pnp_class ) )
		{
		case IB_PNP_CA:
			event_rec.pnp_event = IB_PNP_CA_ADD;
			p_context = pnp_create_context( p_reg,
				&event_rec.p_ci_ca->p_pnp_attr->ca_guid);
			if( !p_context )
				break;

			__pnp_notify_user( p_reg, p_context, &event_rec );
			break;

		case IB_PNP_PORT:
			__pnp_port_notify( p_reg, event_rec.p_ci_ca );
			break;

		default:
			CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_CA ||
				pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );
			continue;
		}
	}

	/* Notify the user that the registration is complete. */
	pnp_reg_complete( p_reg );

	AL_EXIT( AL_DBG_PNP );
}


static void
__al_pnp_process_reg(
	IN				cl_async_proc_item_t*		p_item )
{
	al_pnp_t*		p_reg;

	AL_ENTER( AL_DBG_PNP );

	p_reg = PARENT_STRUCT( p_item, al_pnp_t, async_item );

	/* Add the registrant to the list. */
	switch( pnp_get_class( p_reg->pnp_class ) )
	{
	case IB_PNP_CA:
		cl_qlist_insert_tail( &gp_pnp->ca_reg_list, &p_reg->list_item );
		break;

	case IB_PNP_PORT:
		cl_qlist_insert_tail( &gp_pnp->port_reg_list, &p_reg->list_item );
		break;

	default:
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_CA ||
			pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );
	}

	/* Generate all relevant events for the registration. */
	__pnp_reg_notify( p_reg );

	/* Release the reference taken in init_al_obj. */
	deref_al_obj( &p_reg->obj );

	AL_EXIT( AL_DBG_PNP );
}


ib_api_status_t
ib_reg_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_req_t* const			p_pnp_req,
		OUT			ib_pnp_handle_t* const		ph_pnp )
{
	ib_api_status_t		status;
	KEVENT				event;

	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_pnp_req || !ph_pnp )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	if( pnp_get_flag( p_pnp_req->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
		KeInitializeEvent( &event, SynchronizationEvent, FALSE );

	status = al_reg_pnp( h_al, p_pnp_req, &event, ph_pnp );
	/* Release the reference taken in init_al_obj. */
	if( status == IB_SUCCESS )
	{
		deref_ctx_al_obj( &(*ph_pnp)->obj, E_REF_INIT );
		
		/* Wait for registration to complete if synchronous. */
		if( pnp_get_flag( p_pnp_req->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
		{
			KeWaitForSingleObject(
				&event, Executive, KernelMode, TRUE, NULL );
		}
	}

	AL_EXIT( AL_DBG_PNP );
	return status;
}


ib_api_status_t
al_reg_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_req_t* const			p_pnp_req,
	IN				KEVENT						*p_sync_event,
		OUT			ib_pnp_handle_t* const		ph_pnp )
{
	ib_api_status_t		status;
	al_pnp_t*			p_reg;

	AL_ENTER( AL_DBG_PNP );

	/* Allocate a new registration info structure. */
	p_reg = (al_pnp_t*)cl_zalloc( sizeof(al_pnp_t) );
	if( !p_reg )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to cl_zalloc al_pnp_t (%I64d bytes).\n",
			sizeof(al_pnp_t)) );
		return( IB_INSUFFICIENT_MEMORY );
	}

	/* Initialize the registration info. */
	construct_al_obj( &p_reg->obj, AL_OBJ_TYPE_H_PNP );
	switch(pnp_get_class(p_pnp_req->pnp_class)){
		case IB_PNP_IOU:
		case IB_PNP_IOC:
			cl_fmap_init( &p_reg->context_map, __context_cmp128 );
			break;
		case IB_PNP_PORT:
		case IB_PNP_CA:
			cl_fmap_init( &p_reg->context_map, __context_cmp64 );
			break;
		default:
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("unknown pnp_class 0x%x.\n", pnp_get_class(p_pnp_req->pnp_class)));
	}
	status = init_al_obj( &p_reg->obj, p_pnp_req->pnp_context, TRUE,
		__pnp_reg_destroying, __pnp_reg_cleanup, __pnp_reg_free );
	if( status != IB_SUCCESS )
	{
		__pnp_reg_free( &p_reg->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj() failed with status %s.\n", ib_get_err_str(status)) );
		return( status );
	}
	status = attach_al_obj( &h_al->obj, &p_reg->obj );
	if( status != IB_SUCCESS )
	{
		p_reg->obj.pfn_destroy( &p_reg->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Reference the PnP Manager. */
	ref_al_obj( &gp_pnp->obj );

	/* Copy the request information. */
	p_reg->pnp_class = p_pnp_req->pnp_class;
	p_reg->pfn_pnp_cb = p_pnp_req->pfn_pnp_cb;

	p_reg->p_sync_event = p_sync_event;

	/* Send IOU/IOC registration to the IOC PnP manager. */
	if( pnp_get_class(p_pnp_req->pnp_class) == IB_PNP_IOU ||
		pnp_get_class(p_pnp_req->pnp_class) == IB_PNP_IOC )
	{
		p_reg->async_item.pfn_callback = ioc_pnp_process_reg;
		p_reg->dereg_item.pfn_callback = ioc_pnp_process_dereg;
	}
	else
	{
		p_reg->async_item.pfn_callback = __al_pnp_process_reg;
		p_reg->dereg_item.pfn_callback = __al_pnp_process_dereg;
	}

	/* Queue the registrant for addition to the list. */
	ref_al_obj( &p_reg->obj );
	cl_async_proc_queue( gp_async_pnp_mgr, &p_reg->async_item );

	/* Set the user handle. */
	*ph_pnp = p_reg;

	AL_EXIT( AL_DBG_PNP );
	return( IB_SUCCESS );
}


ib_api_status_t
ib_dereg_pnp(
	IN		const	ib_pnp_handle_t				h_pnp,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_pnp, AL_OBJ_TYPE_H_PNP ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &h_pnp->obj );
	h_pnp->obj.pfn_destroy( &h_pnp->obj, pfn_destroy_cb );

	AL_EXIT( AL_DBG_PNP );
	return( IB_SUCCESS );
}


/*
 * CA event handling.
 */
static void
__pnp_process_add_ca(
	IN				cl_async_proc_item_t		*p_item )
{
	al_pnp_t			*p_reg;
	al_pnp_ca_event_t	*p_event_rec;
	cl_list_item_t		*p_reg_item;
	al_pnp_context_t	*p_context;
	cl_status_t			cl_status;
	size_t				i;

	AL_ENTER( AL_DBG_PNP );

	p_event_rec = PARENT_STRUCT( p_item, al_pnp_ca_event_t, async_item );

	cl_spinlock_acquire( &gp_pnp->obj.lock );
	/* Add the CA to the CA vector. */
	for( i = 0; i < cl_ptr_vector_get_size( &gp_pnp->ca_vector ); i++ )
	{
		if( !cl_ptr_vector_get( &gp_pnp->ca_vector, i ) )
		{
			cl_status = cl_ptr_vector_set( &gp_pnp->ca_vector, i,
				p_event_rec->p_ci_ca );
			CL_ASSERT( cl_status == CL_SUCCESS );
			break;
		}
	}
	cl_spinlock_release( &gp_pnp->obj.lock );
	CL_ASSERT( i < cl_ptr_vector_get_size( &gp_pnp->ca_vector ) );

	/* Walk the list of registrants for notification. */
	for( p_reg_item = cl_qlist_head( &gp_pnp->ca_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->ca_reg_list );
		 p_reg_item = cl_qlist_next( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_CA );

		/* Allocate the user's context. */
		/*
		 * Moving this allocation to the pnp_ca_event call is left as an
		 * exercise to the open source community.
		 */
		p_context = pnp_create_context( p_reg,
			&p_event_rec->p_ci_ca->p_pnp_attr->ca_guid);
		if( !p_context )
			continue;

		/* Notify the user. */
		__pnp_notify_user( p_reg, p_context, p_event_rec );
	}

	/* Generate port add and state events. */
	for( p_reg_item = cl_qlist_head( &gp_pnp->port_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->port_reg_list );
		 p_reg_item = cl_qlist_next( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );
		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );
		__pnp_port_notify( p_reg, p_event_rec->p_ci_ca );
	}

	/* Cleanup the event record. */
	deref_al_obj( &gp_pnp->obj );
	cl_event_signal( &p_event_rec->p_ci_ca->event );
	cl_free( p_event_rec );

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_process_remove_port(
	IN		const	al_pnp_ca_event_t* const	p_event_rec )
{
	ib_api_status_t			status;
	al_pnp_t				*p_reg;
	cl_list_item_t			*p_reg_item;
	uint8_t					port_index;
	al_pnp_context_t		*p_context;
	al_pnp_ca_event_t		event_rec;
	ib_port_attr_t			*p_port_attr;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_event_rec->p_ci_ca->p_pnp_attr );
	CL_ASSERT( p_event_rec->p_ci_ca->p_pnp_attr->p_port_attr );

	/* Notify the IOC PnP manager of the port down event. */
	//***TODO: Make some call to the IOC PnP manager here, such as
	//***TODO: al_ioc_pnp_process_port_down( p_event_rec->p_ci_ca,
	//***TODO:		p_event_rec->port_index );

	cl_memclr( &event_rec, sizeof( al_pnp_ca_event_t ) );
	event_rec = *p_event_rec;

	/* Walk the list of registrants for notification. */
	for( p_reg_item = cl_qlist_tail( &gp_pnp->port_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->port_reg_list );
		 p_reg_item = cl_qlist_prev( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );

		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );

		for( port_index = 0;
			 port_index < p_event_rec->p_ci_ca->num_ports;
			 port_index++ )
		{
			p_port_attr = p_event_rec->p_ci_ca->p_pnp_attr->p_port_attr;
			p_port_attr += port_index;
			p_context = pnp_get_context( p_reg, &p_port_attr->port_guid );
			if( !p_context )
				continue;

			event_rec.port_index = port_index;

			if( p_port_attr->link_state >= IB_LINK_INIT )
			{
				/* Notify the user of the port down. */
				event_rec.pnp_event = IB_PNP_PORT_DOWN;
				status = __pnp_notify_user( p_reg, p_context, &event_rec );
				if( status != IB_SUCCESS )
					continue;
			}

			/* Notify the user of the port remove. */
			event_rec.pnp_event = IB_PNP_PORT_REMOVE;
			status = __pnp_notify_user( p_reg, p_context, &event_rec );
			if( status == IB_SUCCESS )
			{
				/*
				 * Remove the port context from the registrant's
				 * context list.
				 */
				cl_fmap_remove_item( &p_reg->context_map,
					&p_context->map_item );
				/* Free the context. */
				cl_free( p_context );
			}
		}
	}

	AL_EXIT( AL_DBG_PNP );
}


static void
__pnp_process_remove_ca(
	IN				cl_async_proc_item_t		*p_item )
{
	al_pnp_t			*p_reg;
	al_pnp_ca_event_t	*p_event_rec;
	cl_list_item_t		*p_reg_item;
	al_pnp_context_t	*p_context = NULL;
	size_t				i;

	AL_ENTER( AL_DBG_PNP );

	p_event_rec = PARENT_STRUCT( p_item, al_pnp_ca_event_t, async_item );

	/* Generate port remove events. */
	__pnp_process_remove_port( p_event_rec );

	/* Walk the list of registrants for notification. */
	for( p_reg_item = cl_qlist_tail( &gp_pnp->ca_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->ca_reg_list );
		 p_reg_item = cl_qlist_prev( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );

		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_CA );

		/* Search the context list for this CA. */
		p_context =
			pnp_get_context( p_reg, &p_event_rec->p_ci_ca->p_pnp_attr->ca_guid);

		/* Make sure we found a context. */
		if( !p_context )
			continue;

		/* Notify the user. */
		if( __pnp_notify_user( p_reg, p_context, p_event_rec ) == IB_SUCCESS )
		{
			/* Remove the context from the context list. */
			cl_fmap_remove_item( &p_reg->context_map, &p_context->map_item );

			/* Deallocate the context block. */
			cl_free( p_context );
		}
	}

	/* Remove the CA from the CA vector. */
	for( i = 0; i < cl_ptr_vector_get_size( &gp_pnp->ca_vector ); i++ )
	{
		if( cl_ptr_vector_get( &gp_pnp->ca_vector, i ) ==
			p_event_rec->p_ci_ca )
		{
			cl_ptr_vector_remove( &gp_pnp->ca_vector, i );
			break;
		}
	}

	/* Release the reference to the CA. */
	deref_al_obj( &p_event_rec->p_ci_ca->obj );

	/* Cleanup the event record. */
	deref_al_obj( &gp_pnp->obj );
	cl_event_signal( &p_event_rec->p_ci_ca->event );
	cl_free( p_event_rec );

	AL_EXIT( AL_DBG_PNP );
}


ib_api_status_t
pnp_ca_event(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_pnp_event_t				event )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	al_pnp_ca_event_t	*p_event_rec;
	ib_ca_attr_t		*p_old_ca_attr;

	AL_ENTER( AL_DBG_PNP );

	/* Allocate an event record. */
	p_event_rec = (al_pnp_ca_event_t*)cl_zalloc( sizeof(al_pnp_ca_event_t) );
	if( !p_event_rec )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to cl_zalloc al_pnp_ca_event_t (%I64d bytes).\n",
			sizeof(al_pnp_ca_event_t)) );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Store the event type. */
	p_event_rec->pnp_event = event;
	/* Store a pointer to the ca. */
	p_event_rec->p_ci_ca = p_ci_ca;

	switch( event )
	{
	case IB_PNP_CA_ADD:
		p_event_rec->async_item.pfn_callback = __pnp_process_add_ca;

		/* Reserve space for the CA in the CA vector. */
		cl_spinlock_acquire( &gp_pnp->obj.lock );
		cl_status = cl_ptr_vector_set_size( &gp_pnp->ca_vector,
				cl_ptr_vector_get_size( &gp_pnp->ca_vector ) + 1 );
		cl_spinlock_release( &gp_pnp->obj.lock );

		if( cl_status != CL_SUCCESS )
		{
			cl_free( p_event_rec );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("cl_ptr_vector_set_size failed with status %#x.\n",
				cl_status) );
			return ib_convert_cl_status( cl_status );
		}

		/* Read the CA attributes required to process the event. */
		status = ci_ca_update_attr( p_ci_ca, &p_old_ca_attr );
		if( status != IB_SUCCESS )
		{
			cl_spinlock_acquire( &gp_pnp->obj.lock );
			cl_status = cl_ptr_vector_set_size( &gp_pnp->ca_vector,
				cl_ptr_vector_get_size( &gp_pnp->ca_vector ) - 1 );
			CL_ASSERT( cl_status == CL_SUCCESS );
			cl_spinlock_release( &gp_pnp->obj.lock );
			cl_free( p_event_rec );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ci_ca_update_attr failed.\n") );
			return status;
		}

		/* Take out a reference to the CA until it is removed. */
		ref_al_obj( &p_ci_ca->obj );
		break;

	case IB_PNP_CA_REMOVE:
		if( !p_event_rec->p_ci_ca->p_pnp_attr )
		{
			/* The CA was never added by the PNP manager. */
			cl_free( p_event_rec );
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Ignoring removal request for unknown CA.\n") );
			return IB_NOT_FOUND;
		}

		p_event_rec->async_item.pfn_callback = __pnp_process_remove_ca;
		break;

	default:
		/* Invalid event for this function. */
		CL_ASSERT( event == IB_PNP_CA_ADD || event == IB_PNP_CA_REMOVE );
		cl_free( p_event_rec );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid event type.\n") );
		return IB_ERROR;
	}

	/* Queue the event to the async processing manager. */
	ref_al_obj( &gp_pnp->obj );
	cl_async_proc_queue( gp_async_pnp_mgr, &p_event_rec->async_item );

	/* wait for the end of event propagation 
	It is needed for enabling quick HCA disable/enable scenarios. */
	cl_status = cl_event_wait_on( &p_ci_ca->event, 
		EVENT_NO_TIMEOUT, AL_WAIT_ALERTABLE );
	if (cl_status != CL_SUCCESS)
		return IB_ERROR;

	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}


/*
 * Port event handling.
 */

/*
 * Processes a port event, reporting it to clients from the first
 * registrant to the last.
 */
static void
__pnp_process_port_forward(
	IN				al_pnp_ca_event_t*			p_event_rec )
{
	al_pnp_t				*p_reg;
	cl_list_item_t			*p_reg_item;
	al_pnp_context_t		*p_context;
	ib_port_attr_t			*p_port_attr;

	AL_ENTER( AL_DBG_PNP );

	/* Walk the list of registrants for notification. */
	for( p_reg_item = cl_qlist_head( &gp_pnp->port_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->port_reg_list );
		 p_reg_item = cl_qlist_next( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );

		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );

		p_port_attr = p_event_rec->p_ci_ca->p_pnp_attr->p_port_attr;
		p_port_attr += p_event_rec->port_index;

		p_context = pnp_get_context( p_reg, &p_port_attr->port_guid );
		if( !p_context )
			continue;

		/* Notify the user. */
		__pnp_notify_user( p_reg, p_context, p_event_rec );
	}

	AL_EXIT( AL_DBG_PNP );
}


/*
 * Processes a port event, reporting it to clients from the last
 * registrant to the first.
 */
static void
__pnp_process_port_backward(
	IN				al_pnp_ca_event_t*			p_event_rec )
{
	al_pnp_t				*p_reg;
	cl_list_item_t			*p_reg_item;
	al_pnp_context_t		*p_context;
	ib_port_attr_t			*p_port_attr;

	AL_ENTER( AL_DBG_PNP );

	/* Walk the list of registrants for notification. */
	for( p_reg_item = cl_qlist_tail( &gp_pnp->port_reg_list );
		 p_reg_item != cl_qlist_end( &gp_pnp->port_reg_list );
		 p_reg_item = cl_qlist_prev( p_reg_item ) )
	{
		p_reg = PARENT_STRUCT( p_reg_item, al_pnp_t, list_item );

		CL_ASSERT( pnp_get_class( p_reg->pnp_class ) == IB_PNP_PORT );

		p_port_attr = p_event_rec->p_ci_ca->p_pnp_attr->p_port_attr;
		p_port_attr += p_event_rec->port_index;

		p_context = pnp_get_context( p_reg, &p_port_attr->port_guid );
		if( !p_context )
			continue;

		/* Notify the user. */
		__pnp_notify_user( p_reg, p_context, p_event_rec );
	}

	AL_EXIT( AL_DBG_PNP );
}


/* 
 *send asynchronous events
 */
static void
__pnp_send_ae(
	IN				al_ci_ca_t* const			p_ci_ca )
{
	int ci, cnt, i;
	al_pnp_ca_event_t		event_rec;
	al_ae_info_t ae[MAX_AE]; /* pending Asynchronic Events */		

	if (!p_ci_ca->cnt)
		return;
	
	/* copy events in temp array */
	ci_ca_lock_attr( p_ci_ca );
	cnt = p_ci_ca->cnt;
	ci = p_ci_ca->ci;
	for (i=0; i<cnt; ++i)
	{
		ae[i] = p_ci_ca->ae[ci];
		if ( ++ci >= MAX_AE )
			ci = 0;
	}
	cnt = p_ci_ca->cnt;
	p_ci_ca->cnt = 0;
	p_ci_ca->ci = ci;
	ci_ca_unlock_attr( p_ci_ca );

	event_rec.p_ci_ca = p_ci_ca;
	for (i=0; i<cnt; ++i)
	{
		event_rec.pnp_event = ae[i].pnp_event;
		event_rec.port_index = ae[i].port_index;
		__pnp_process_port_forward( &event_rec );
	}
} 


/*
 * Check for port attribute changes.
 */
static void
__pnp_check_ports(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_ca_attr_t* const			p_old_ca_attr )
{
	uint16_t				index;
	al_pnp_ca_event_t		event_rec;
	ib_port_attr_t			*p_old_port_attr, *p_new_port_attr;

	AL_ENTER( AL_DBG_PNP );

	/* Store the event information. */
	event_rec.p_ci_ca = p_ci_ca;

	for( event_rec.port_index = 0;
		 event_rec.port_index < p_ci_ca->p_pnp_attr->num_ports;
		 event_rec.port_index++ )
	{
		p_old_port_attr = p_old_ca_attr->p_port_attr;
		p_old_port_attr += event_rec.port_index;
		p_new_port_attr = p_ci_ca->p_pnp_attr->p_port_attr;
		p_new_port_attr += event_rec.port_index;

		/* Check the link state. */
		if( p_old_port_attr->link_state != p_new_port_attr->link_state )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
				("new link_state = %d \n",
				p_new_port_attr->link_state) );
			
			switch( p_new_port_attr->link_state )
			{
			case IB_LINK_DOWN:
				event_rec.pnp_event = IB_PNP_PORT_DOWN;
				__pnp_process_port_backward( &event_rec );
				break;

			case IB_LINK_INIT:
				if( p_old_port_attr->link_state > IB_LINK_INIT )
				{
					/* Missed the down event. */
					event_rec.pnp_event = IB_PNP_PORT_DOWN;
					__pnp_process_port_backward( &event_rec );
				}
				event_rec.pnp_event = IB_PNP_PORT_INIT;
				__pnp_process_port_forward( &event_rec );
				break;

			case IB_LINK_ARMED:
				if( p_old_port_attr->link_state > IB_LINK_ARMED )
				{
					/* Missed the down and init events. */
					event_rec.pnp_event = IB_PNP_PORT_DOWN;
					__pnp_process_port_backward( &event_rec );
					event_rec.pnp_event = IB_PNP_PORT_INIT;
					__pnp_process_port_forward( &event_rec );
				}
				event_rec.pnp_event = IB_PNP_PORT_ARMED;
				__pnp_process_port_forward( &event_rec );
				break;

			case IB_LINK_ACTIVE:
			case IB_LINK_ACT_DEFER:
				if( p_old_port_attr->link_state == IB_LINK_DOWN )
				{
					/* Missed the init and armed event. */
					event_rec.pnp_event = IB_PNP_PORT_INIT;
					__pnp_process_port_forward( &event_rec );
					event_rec.pnp_event = IB_PNP_PORT_ARMED;
					__pnp_process_port_forward( &event_rec );
				}
				if( p_old_port_attr->link_state < IB_LINK_ACTIVE )
				{
					event_rec.pnp_event = IB_PNP_PORT_ACTIVE;
					__pnp_process_port_forward( &event_rec );
				}
				break;

			default:
				break;
			}
		}

		/*
		 * Check for P_Key and GID table changes.
		 * The tables are only valid in the armed or active states.
		 */
		if( ( (p_old_port_attr->link_state == IB_LINK_ARMED) ||
			(p_old_port_attr->link_state == IB_LINK_ACTIVE) )
			&&
			( (p_new_port_attr->link_state == IB_LINK_ARMED) ||
			(p_new_port_attr->link_state == IB_LINK_ACTIVE) ) )
		{

			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
				("pkey or gid changes\n") );

			/* A different number of P_Keys indicates a change.*/
			if( p_old_port_attr->num_pkeys != p_new_port_attr->num_pkeys )
			{
				event_rec.pnp_event = IB_PNP_PKEY_CHANGE;
				__pnp_process_port_forward( &event_rec );
			}
			else
			{
				/* Same number of P_Keys - compare the table contents. */
				for( index = 0; index < p_old_port_attr->num_pkeys; index++ )
				{
					if( p_old_port_attr->p_pkey_table[index] !=
						p_new_port_attr->p_pkey_table[index] )
					{
						event_rec.pnp_event = IB_PNP_PKEY_CHANGE;
						__pnp_process_port_forward( &event_rec );
						break;
					}
				}
			}

			/* A different number of GIDs indicates a change.*/
			if( p_old_port_attr->num_gids != p_new_port_attr->num_gids )
			{
				event_rec.pnp_event = IB_PNP_GID_CHANGE;
				__pnp_process_port_forward( &event_rec );
			}
			else
			{
				/* Same number of GIDs - compare the table contents. */
				for( index = 0; index < p_old_port_attr->num_gids; index++ )
				{
					if( cl_memcmp( p_old_port_attr->p_gid_table[index].raw,
						p_new_port_attr->p_gid_table[index].raw,
						sizeof( ib_gid_t ) ) )
					{
						event_rec.pnp_event = IB_PNP_GID_CHANGE;
						__pnp_process_port_forward( &event_rec );
						break;
					}
				}
			}
		}

		/* Check for LID change. */
		if( (p_old_port_attr->lid != p_new_port_attr->lid) ||
			(p_old_port_attr->lmc != p_new_port_attr->lmc) )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
				("lid/lmc changed \n") );
			event_rec.pnp_event = IB_PNP_LID_CHANGE;
			__pnp_process_port_forward( &event_rec );
		}
		/* Check for SM related changes. */
		if( (p_old_port_attr->sm_lid != p_new_port_attr->sm_lid) ||
			(p_old_port_attr->sm_sl != p_new_port_attr->sm_sl) )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
				("sm_lid/sm_sl changed \n") );
			event_rec.pnp_event = IB_PNP_SM_CHANGE;
			__pnp_process_port_forward( &event_rec );
		}
		/* Check for subnet timeout change. */
		if( p_old_port_attr->subnet_timeout !=
			p_new_port_attr->subnet_timeout )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
				("subnet_timeout changed \n") );
			event_rec.pnp_event = IB_PNP_SUBNET_TIMEOUT_CHANGE;
			__pnp_process_port_forward( &event_rec );
		}
	}

}



static boolean_t
__pnp_cmp_attr(
	IN			ib_ca_attr_t				*p_attr_1,
	IN			ib_ca_attr_t				*p_attr_2
	)
{
	uint8_t					port_index;
	ib_port_attr_t*			p_port_attr_1;
	ib_port_attr_t*			p_port_attr_2;

	CL_ASSERT( p_attr_1 && p_attr_2 );

	for( port_index = 0;
		 port_index < p_attr_1->num_ports;
		 port_index++ )
	{
		/* Initialize pointers to the port attributes. */
		p_port_attr_1 = &p_attr_1->p_port_attr[port_index];
		p_port_attr_2 = &p_attr_2->p_port_attr[port_index];

		CL_ASSERT( p_port_attr_1->port_guid == p_port_attr_2->port_guid );

		if( (cl_memcmp( p_port_attr_1, p_port_attr_2,
				offsetof( ib_port_attr_t, p_gid_table ) ) != 0 ) ||
			(cl_memcmp(p_port_attr_1->p_gid_table,p_port_attr_2->p_gid_table,
					   p_port_attr_1->num_gids*sizeof(p_port_attr_1->p_gid_table[0]))) ||
			(cl_memcmp(p_port_attr_1->p_pkey_table,p_port_attr_2->p_pkey_table,
					   p_port_attr_1->num_pkeys*sizeof(p_port_attr_1->p_pkey_table[0]))) 
			)

		{
			return FALSE;
		}
	}

	return TRUE;
}



static void
__pnp_check_events(
	IN				cl_async_proc_item_t*	p_item )
{
	al_ci_ca_t				*p_ci_ca;
	size_t					i;
	uint32_t				attr_size;
	ib_ca_attr_t			*p_old_ca_attr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_PNP );

	UNUSED_PARAM( p_item );
	CL_ASSERT( gp_pnp );

	/* Walk all known CAs. */
	for( i = 0; i < cl_ptr_vector_get_size( &gp_pnp->ca_vector ); i++ )
	{
		p_ci_ca = (al_ci_ca_t*)cl_ptr_vector_get( &gp_pnp->ca_vector, i );

		/* Check if the CA was just added to our list but is not ready. */
		if( !p_ci_ca )
			continue;

		attr_size = p_ci_ca->p_pnp_attr->size;
		status = ib_query_ca( p_ci_ca->h_ca, p_ci_ca->p_user_attr, &attr_size );

		/* Report changes if there is an attribute size difference. */
		if( ( attr_size != p_ci_ca->p_pnp_attr->size ) ||
			!__pnp_cmp_attr( p_ci_ca->p_pnp_attr, p_ci_ca->p_user_attr ) )
		{
			status = ci_ca_update_attr( p_ci_ca, &p_old_ca_attr );
			if( status == IB_SUCCESS )
			{
				/* Check port attributes and report changes. */
				__pnp_check_ports( p_ci_ca, p_old_ca_attr );

				/* Free the old CA attributes. */
				cl_free( p_old_ca_attr );
			}
			else
			{
				/*
				 * Could not get new attribute buffers.
				 * Skip this event - it should be picked up on the next check.
				 */
				continue;
			}
		}

		/* send asynchronous events */
		__pnp_send_ae( p_ci_ca );
		
	}

	/* Dereference the PnP Manager. */
	deref_al_obj( &gp_pnp->obj );
	gp_pnp->async_item_is_busy = FALSE;

	AL_EXIT( AL_DBG_PNP );
}



/*
 * Check and report PnP events.
 */
void
pnp_poll(
	void )
{
	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( gp_pnp );

	/* Determine if the PnP manager asynchronous processing item is busy. */
	cl_spinlock_acquire( &gp_pnp->obj.lock );

	if( gp_pnp->async_item_is_busy )
	{
		cl_spinlock_release( &gp_pnp->obj.lock );
		return;
	}

	gp_pnp->async_item_is_busy = TRUE;

	cl_spinlock_release( &gp_pnp->obj.lock );

	/* Reference the PnP Manager. */
	ref_al_obj( &gp_pnp->obj );

	/* Queue the request to check for PnP events. */
	cl_async_proc_queue( gp_async_pnp_mgr, &gp_pnp->async_item );

	AL_EXIT( AL_DBG_PNP );
}



static void
__pnp_process_ca_change(
	IN				cl_async_proc_item_t*	p_item )
{
	al_pnp_ca_change_t		*p_pnp_ca_change;
	ib_ca_attr_t			*p_old_ca_attr;
	al_ci_ca_t				*p_ci_ca;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_item );
	CL_ASSERT( gp_pnp );

	p_pnp_ca_change = PARENT_STRUCT( p_item, al_pnp_ca_change_t, async_item );

	p_ci_ca = p_pnp_ca_change->p_ci_ca;

	/*
	 * Prevent readers of the CA attributes from accessing them while
	 * we are updating the pointers.
	 */
	ci_ca_excl_lock_attr( p_ci_ca );

	/* Swap the old and new CA attributes. */
	p_old_ca_attr = p_ci_ca->p_pnp_attr;
	p_ci_ca->p_pnp_attr = p_pnp_ca_change->p_new_ca_attr;
	p_ci_ca->p_user_attr = (ib_ca_attr_t*)(((uint8_t*)p_ci_ca->p_pnp_attr) +
		p_ci_ca->p_pnp_attr->size);
	ci_ca_unlock_attr( p_ci_ca );

	/* Report changes. */
	__pnp_check_ports( p_ci_ca, p_old_ca_attr );

	/* Free the old CA attributes. */
	cl_free( p_old_ca_attr );

	/* Dereference the PnP Manager. */
	deref_al_obj( &gp_pnp->obj );

	AL_EXIT( AL_DBG_PNP );
}



/*
 *	Called by user mode AL to report a CA attribute change.
 */
ib_api_status_t
pnp_ca_change(
	IN				al_ci_ca_t* const		p_ci_ca,
	IN		const	ib_ca_attr_t*			p_ca_attr )
{
	ib_ca_attr_t*			p_new_ca_attr;
	al_pnp_ca_change_t*		p_pnp_ca_change;
	size_t					size;

	AL_ENTER( AL_DBG_PNP );

	CL_ASSERT( p_ci_ca );
	CL_ASSERT( p_ca_attr );

	/*
	 * Allocate the new CA attributes buffer.
	 * Double the buffer size for PnP and user reporting halves.
	 * Also include the CA change event structure in the allocation.
	 */
	size = ( p_ca_attr->size * 2 ) + sizeof( al_pnp_ca_change_t );
	p_new_ca_attr = (ib_ca_attr_t*)cl_zalloc( size );
	if( !p_new_ca_attr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR,AL_DBG_PNP,
			("Unable to allocate buffer for changed CA attributes\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Copy the attributes. */
	ib_copy_ca_attr( p_new_ca_attr, p_ca_attr );

	/* Initialize a pointer to the CA change event structure. */
	p_pnp_ca_change = (al_pnp_ca_change_t*)
		(((uint8_t*)p_new_ca_attr) + ( p_ca_attr->size * 2 ));

	/* Initialize the CA change event strucuture. */
	p_pnp_ca_change->async_item.pfn_callback = __pnp_process_ca_change;
	p_pnp_ca_change->p_ci_ca = p_ci_ca;
	p_pnp_ca_change->p_new_ca_attr = p_new_ca_attr;

	/* Reference the PnP Manager. */
	ref_al_obj( &gp_pnp->obj );

	/* Queue the CA change event. */
	cl_async_proc_queue( gp_async_pnp_mgr, &p_pnp_ca_change->async_item );

	AL_EXIT( AL_DBG_PNP );
	return IB_SUCCESS;
}



ib_api_status_t
ib_reject_ioc(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_handle_t				h_event )
{
	AL_ENTER( AL_DBG_PNP );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !h_event )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	AL_EXIT( AL_DBG_PNP );
	return IB_UNSUPPORTED;
}

void
pnp_force_event(
	IN 		struct _al_ci_ca	 * p_ci_ca,
	IN 		ib_pnp_event_t pnp_event,
	IN		uint8_t port_num)
{
#define PORT_INDEX_OFFSET 1

	ASSERT(p_ci_ca);
	
	if (!p_ci_ca)
		return;
	
	p_ci_ca->ae[p_ci_ca->pi].pnp_event = pnp_event;
	p_ci_ca->ae[p_ci_ca->pi].port_index = port_num - PORT_INDEX_OFFSET;
	ci_ca_lock_attr( p_ci_ca );
	if ( ++p_ci_ca->pi >= MAX_AE )
		p_ci_ca->pi = 0;
	ASSERT(p_ci_ca->cnt < MAX_AE);
	if ( ++p_ci_ca->cnt > MAX_AE )
	{
		p_ci_ca->cnt = MAX_AE;
		p_ci_ca->ci =p_ci_ca->pi;
	}
	ci_ca_unlock_attr( p_ci_ca );
}


