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

#include "al_ca.h"
#include "al_debug.h"

#include "al_dm.h"
#include "al_mgr.h"
#include "ib_common.h"

#ifdef CL_KERNEL
#include <ntstrsafe.h>
#endif

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_dm.tmh"
#endif

/*
 * This code implements a minimal device management agent.
 */


static dm_agent_t*				gp_dm_agent = NULL;


#define	SVC_REG_TIMEOUT				2000		// Milliseconds
#define	SVC_REG_RETRY_CNT			3
#define	DM_CLASS_RESP_TIME_VALUE	20


#define	SET_NIBBLE( nibble_array, nibble_num, value )						\
{																			\
	((uint8_t*)(nibble_array))[(nibble_num) >> 1]	= (uint8_t)				\
		((((nibble_num) & 1) == 0) ?										\
			((uint8_t*)(nibble_array))[(nibble_num) >> 1] & 0x0f :			\
			((uint8_t*)(nibble_array))[(nibble_num) >> 1] & 0xf0);			\
	((uint8_t*)(nibble_array))[(nibble_num) >> 1] |=						\
		( ((nibble_num) & 1) == 0) ? ((value) << 4) : ((value) & 0x0f);		\
}


void
free_ioc(
	IN				al_obj_t*					p_obj );

void
free_svc_entry(
	IN				al_obj_t*					p_obj );

al_iou_t*
acquire_iou(
	IN		const	ib_net64_t					ca_guid );

al_iou_t*
get_iou(
	IN		const	ib_ioc_handle_t				h_ioc );

ib_ioc_handle_t 
get_ioc(
	IN		const	ib_ca_handle_t				h_ca );

ib_api_status_t
add_ioc(
	IN				al_iou_t*					p_iou,
	IN				ib_ioc_handle_t				h_ioc );

void
ioc_change(
	IN				ib_ioc_handle_t				h_ioc );

void
iou_change(
	IN				al_iou_t*					p_iou );

ib_api_status_t
set_port_dm_attr(
	IN				al_iou_port_t*				p_iou_port );

void
iou_port_svc_reg_cb(
	IN				ib_reg_svc_rec_t*			p_reg_svc_rec );

void
destroying_dm_agent(
	IN				al_obj_t*					p_obj );

void
free_dm_agent(
	IN				al_obj_t*					p_obj );

ib_api_status_t
dm_agent_reg_pnp(
	IN				ib_pnp_class_t				pnp_class,
	IN				ib_pnp_handle_t *			ph_pnp );

ib_api_status_t
dm_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec );

ib_api_status_t
create_iou(
	IN				ib_pnp_rec_t*				p_pnp_rec );

void
cleanup_iou(
	IN				al_obj_t*					p_obj );

void
free_iou(
	IN				al_obj_t*					p_obj );

ib_api_status_t
create_iou_port(
	IN				ib_pnp_port_rec_t*			p_pnp_rec );

void
destroying_iou_port(
	IN				al_obj_t*					p_obj );

void
free_iou_port(
	IN				al_obj_t*					p_obj );

void
iou_port_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );

void
dm_agent_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_response );

void
dm_agent_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_request );

void
dm_agent_get(
	IN				al_iou_port_t*				p_iou_port,
	IN				ib_mad_t*					p_mad_req,
	IN				ib_mad_t*					p_mad_rsp );

void
dm_agent_set(
	IN				al_iou_port_t*				p_iou_port,
	IN				ib_mad_t*					p_mad_req,
	IN				ib_mad_t*					p_mad_rsp );

void
get_class_port_info(
	IN				al_iou_t*					p_iou,
	IN				ib_dm_mad_t*				p_dm_mad );

void
get_io_unit_info(
	IN				al_iou_t*					p_iou,
	IN				ib_dm_mad_t*				p_dm_mad );

void
get_ioc_profile(
	IN				al_iou_t*					p_iou,
	IN				uint8_t						slot,
	IN				ib_dm_mad_t*				p_dm_mad );

void
get_svc_entries(
	IN				al_iou_t*					p_iou,
	IN				uint8_t						slot,
	IN				uint8_t						svc_num_lo,
	IN				uint8_t						svc_num_hi,
	IN				ib_dm_mad_t*				p_dm_mad );




ib_api_status_t
ib_create_ioc(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_ioc_profile_t* const		p_ioc_profile,
		OUT			ib_ioc_handle_t* const		ph_ioc )
{
	ib_ioc_handle_t			h_ioc;

	AL_ENTER( AL_DBG_IOC );

	if( AL_OBJ_INVALID_HANDLE( h_ca, AL_OBJ_TYPE_H_CA ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_CA_HANDLE\n") );
		return IB_INVALID_CA_HANDLE;
	}
	if( !p_ioc_profile || !ph_ioc )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Get an IOC. */
	h_ioc = get_ioc( h_ca );
	if( !h_ioc )
		return IB_INSUFFICIENT_MEMORY;

	/* Save the IOC profile. */
	cl_memcpy( &h_ioc->ioc_profile, p_ioc_profile, sizeof(ib_ioc_profile_t) );

	/* Clear the service entry count. */
	h_ioc->ioc_profile.num_svc_entries = 0;

	/* Return the IOC handle to the user. */
	*ph_ioc = h_ioc;

	AL_EXIT( AL_DBG_IOC );
	return IB_SUCCESS;
}



ib_api_status_t
ib_destroy_ioc(
	IN		const	ib_ioc_handle_t				h_ioc )
{
	AL_ENTER( AL_DBG_IOC );

	if( AL_OBJ_INVALID_HANDLE( h_ioc, AL_OBJ_TYPE_H_IOC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	ref_al_obj( &h_ioc->obj );
	h_ioc->obj.pfn_destroy( &h_ioc->obj, NULL );

	AL_EXIT( AL_DBG_IOC );
	return IB_SUCCESS;
}



/*
 * Free an IOC.
 */
void
free_ioc(
	IN				al_obj_t*					p_obj )
{
	ib_ioc_handle_t			h_ioc;

	CL_ASSERT( p_obj );

	h_ioc = PARENT_STRUCT( p_obj, al_ioc_t, obj );

	/*
	 * To maintain slot ordering, IOCs attached to an IO unit are freed when
	 * the IO unit is destroyed.  Otherwise, unattached IOCs may be freed now.
	 */
	if( h_ioc->p_iou )
	{
		/* Mark the IOC slot as empty. */
		h_ioc->state = EMPTY_SLOT;
		reset_al_obj( p_obj );
		deref_al_obj( &h_ioc->p_iou->obj );

		/* Report that a change occurred on the IOC. */
		ioc_change( h_ioc );
	}
	else
	{
		/* Unattached IOCs can be destroyed. */
		destroy_al_obj( p_obj );
		cl_free( h_ioc );
	}
}



ib_api_status_t
ib_reg_ioc(
	IN		const	ib_ioc_handle_t				h_ioc )
{
	al_iou_t*				p_iou;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_IOC );

	if( AL_OBJ_INVALID_HANDLE( h_ioc, AL_OBJ_TYPE_H_IOC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	/* Get an IO unit for this IOC. */
	p_iou = get_iou( h_ioc );
	if( !p_iou )
		return IB_INSUFFICIENT_MEMORY;

	/* Register the IOC with the IO unit. */
	status = add_ioc( p_iou, h_ioc );

	AL_EXIT( AL_DBG_IOC );
	return status;
}



ib_api_status_t
ib_add_svc_entry(
	IN		const	ib_ioc_handle_t				h_ioc,
	IN		const	ib_svc_entry_t* const		p_svc_entry,
		OUT			ib_svc_handle_t* const		ph_svc )
{
	ib_svc_handle_t			h_svc;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_IOC );

	if( AL_OBJ_INVALID_HANDLE( h_ioc, AL_OBJ_TYPE_H_IOC ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	if( !p_svc_entry || !ph_svc )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/*
	 * Synchronize the addition of a service entry with the removal.
	 * Cannot hold a lock on the IOC when attaching a service entry
	 * object.  Wait here until the IOC is no longer in use.
	 */
	cl_spinlock_acquire( &h_ioc->obj.lock );
	while( h_ioc->in_use_cnt )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		cl_thread_suspend( 0 );
		cl_spinlock_acquire( &h_ioc->obj.lock );
	}
	/* Flag the IOC as in use by this thread. */
	cl_atomic_inc( &h_ioc->in_use_cnt );
	cl_spinlock_release( &h_ioc->obj.lock );

	/* Check the current service entry count. */
	if( h_ioc->ioc_profile.num_svc_entries == MAX_NUM_SVC_ENTRIES )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		AL_EXIT( AL_DBG_IOC );
		return IB_INSUFFICIENT_RESOURCES;
	}
	h_svc = cl_zalloc( sizeof( ib_svc_handle_t ) );
	if( !h_svc )
	{
		AL_EXIT( AL_DBG_IOC );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the service entry. */
	construct_al_obj( &h_svc->obj, AL_OBJ_TYPE_H_SVC_ENTRY );

	/* Save the service entry. */
	cl_memcpy( &h_svc->svc_entry, p_svc_entry, sizeof( ib_svc_entry_t ) );

	/* Initialize the service entry object. */
	status = init_al_obj( &h_svc->obj, h_svc, FALSE, NULL, NULL,
		free_svc_entry );
	if( status != IB_SUCCESS )
	{
		free_svc_entry( &h_svc->obj );
		AL_EXIT( AL_DBG_IOC );
		return status;
	}

	/* Attach the service entry to the IOC. */
	status = attach_al_obj( &h_ioc->obj, &h_svc->obj );
	if( status != IB_SUCCESS )
	{
		h_svc->obj.pfn_destroy( &h_svc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	h_ioc->ioc_profile.num_svc_entries++;

	/* Indicate that a change occured on the IOC. */
	ioc_change( h_ioc );

	/* No longer in use by this thread. */
	cl_atomic_dec( &h_ioc->in_use_cnt );

	/* Return the service entry handle to the user. */
	*ph_svc = h_svc;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_svc->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_IOC );
	return IB_SUCCESS;
}



ib_api_status_t
ib_remove_svc_entry(
	IN		const	ib_svc_handle_t				h_svc )
{
	ib_ioc_handle_t			h_ioc;

	AL_ENTER( AL_DBG_IOC );

	if( AL_OBJ_INVALID_HANDLE( h_svc, AL_OBJ_TYPE_H_SVC_ENTRY ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	h_ioc = PARENT_STRUCT( h_svc->obj.p_parent_obj, al_ioc_t, obj );

	/*
	 * Synchronize the removal of a service entry with the addition.
	 * Cannot hold a lock on the IOC when detaching a service entry
	 * object.  Wait here until the IOC is no longer in use.
	 */
	cl_spinlock_acquire( &h_ioc->obj.lock );
	while( h_ioc->in_use_cnt )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		cl_thread_suspend( 0 );
		cl_spinlock_acquire( &h_ioc->obj.lock );
	}
	/* Flag the IOC as in use by this thread. */
	cl_atomic_inc( &h_ioc->in_use_cnt );
	cl_spinlock_release( &h_ioc->obj.lock );

	/*
	 * Synchronously destroy the service entry.
	 * The service handle is invalid when this call returns.
	 */
	ref_al_obj( &h_svc->obj );
	h_svc->obj.pfn_destroy( &h_svc->obj, NULL );

	/* Decrement the service entry count. */
	h_ioc->ioc_profile.num_svc_entries--;

	/* Indicate that a change occured on the IOC. */
	ioc_change( h_ioc );

	/* No longer in use by this thread. */
	cl_atomic_dec( &h_ioc->in_use_cnt );

	AL_EXIT( AL_DBG_IOC );
	return IB_SUCCESS;
}



/*
 * Free a service entry.
 */
void
free_svc_entry(
	IN				al_obj_t*					p_obj )
{
	ib_svc_handle_t			h_svc;

	CL_ASSERT( p_obj );
	h_svc = PARENT_STRUCT( p_obj, al_svc_entry_t, obj );

	destroy_al_obj( &h_svc->obj );
	cl_free( h_svc );
}



/*
 * Acquire the IO unit matching the given CA GUID.
 */
al_iou_t*
acquire_iou(
	IN		const	ib_net64_t					ca_guid )
{
	cl_list_item_t*			p_iou_item;
	al_obj_t*				p_obj;
	al_iou_t*				p_iou;

	/* Search for an existing IO unit matching the CA GUID. */
	cl_spinlock_acquire( &gp_dm_agent->obj.lock );
	for( p_iou_item = cl_qlist_head( &gp_dm_agent->obj.obj_list );
		p_iou_item != cl_qlist_end( &gp_dm_agent->obj.obj_list );
		p_iou_item = cl_qlist_next( p_iou_item ) )
	{
		p_obj = PARENT_STRUCT( p_iou_item, al_obj_t, pool_item );
		p_iou = PARENT_STRUCT( p_obj, al_iou_t, obj );

		/* Check for a GUID match. */
		if( p_iou->obj.p_ci_ca->verbs.guid == ca_guid )
		{
			/* Reference the IO unit on behalf of the client. */
			ref_al_obj( &p_iou->obj );

			cl_spinlock_release( &gp_dm_agent->obj.lock );
			return p_iou;
		}
	}
	cl_spinlock_release( &gp_dm_agent->obj.lock );

	return NULL;
}



/*
 * Get the IO unit for the given IOC.
 */
al_iou_t*
get_iou(
	IN		const	ib_ioc_handle_t				h_ioc )
{
	CL_ASSERT( h_ioc );

	/* Check if the IOC is already attached to an IO unit. */
	if( h_ioc->p_iou )
		return h_ioc->p_iou;

	/* The IOC is a new slot.  Acquire the IO unit. */
	return acquire_iou( h_ioc->obj.p_ci_ca->verbs.guid );
}



ib_ioc_handle_t 
get_ioc(
	IN		const	ib_ca_handle_t				h_ca )
{
	cl_list_item_t*			p_ioc_item;
	al_iou_t*				p_iou;
	ib_ioc_handle_t			h_ioc;
	boolean_t				found;
	ib_api_status_t			status;

	found = FALSE;
	h_ioc = NULL;

	/* Acquire the IO unit. */
	p_iou = acquire_iou( h_ca->obj.p_ci_ca->verbs.guid );

	if( p_iou )
	{
		/* Search for an empty IOC slot in the IO unit. */
		cl_spinlock_acquire( &p_iou->obj.lock );
		for( p_ioc_item = cl_qlist_head( &p_iou->ioc_list );
			(p_ioc_item != cl_qlist_end( &p_iou->ioc_list )) && !found;
			p_ioc_item = cl_qlist_next( p_ioc_item ) )
		{
			h_ioc = PARENT_STRUCT( p_ioc_item, al_ioc_t, iou_item );

			if( h_ioc->state == EMPTY_SLOT )
			{
				/*
				 * An empty slot was found.
				 * Change the state to indicate that the slot is in use.
				 */
				h_ioc->state = SLOT_IN_USE;
				found = TRUE;
			}
		}
		cl_spinlock_release( &p_iou->obj.lock );
	}

	/* Allocate a new IOC if one was not found. */
	if( !found )
	{
		h_ioc = cl_zalloc( sizeof( al_ioc_t ) );
		if( !h_ioc )
			return NULL;

		/* Construct the IOC. */
		construct_al_obj( &h_ioc->obj, AL_OBJ_TYPE_H_IOC );

		/* Initialize the IOC object. */
		status =
			init_al_obj( &h_ioc->obj, h_ioc, FALSE, NULL, NULL, free_ioc );
		if( status != IB_SUCCESS )
		{
			free_ioc( &h_ioc->obj );
			return NULL;
		}
	}

	/* Attach the IOC to the CA. */
	status = attach_al_obj( &h_ca->obj, &h_ioc->obj );
	if( status != IB_SUCCESS )
	{
		h_ioc->obj.pfn_destroy( &h_ioc->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return NULL;
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_ioc->obj, E_REF_INIT );

	return h_ioc;
}



ib_api_status_t
add_ioc(
	IN				al_iou_t*					p_iou,
	IN				ib_ioc_handle_t				h_ioc )
{
	cl_list_item_t*			p_list_item;
	al_obj_t*				p_obj;
	al_iou_port_t*			p_iou_port;
	ib_api_status_t			status;

	CL_ASSERT( p_iou );
	CL_ASSERT( h_ioc );

	/* Attach the IOC to the IO unit. */
	if( !h_ioc->p_iou )
	{
		cl_spinlock_acquire( &p_iou->obj.lock );

		/* Make sure the IO unit can support the new IOC slot. */
		if( cl_qlist_count( &p_iou->ioc_list ) >=
			( sizeof( ((ib_iou_info_t*)0)->controller_list ) - 1) )
		{
			cl_spinlock_release( &p_iou->obj.lock );
			deref_al_obj( &p_iou->obj );
			return IB_INSUFFICIENT_RESOURCES;
		}

		/* Add a new IOC slot to the IO unit. */
		cl_qlist_insert_tail( &p_iou->ioc_list, &h_ioc->iou_item );
		h_ioc->p_iou = p_iou;

		cl_spinlock_release( &p_iou->obj.lock );
	}
	else
	{
		/* The IOC is being added to an empty IO unit slot. */
		CL_ASSERT( h_ioc->p_iou == p_iou );
		CL_ASSERT( h_ioc->state == SLOT_IN_USE );
	}

	/* Enable the IOC. */
	h_ioc->state = IOC_ACTIVE;

	/* Indicate that a change occured on the IO unit. */
	iou_change( p_iou );

	/* Flag each port on the IO unit CA as supporting device management. */
	status = IB_SUCCESS;
	cl_spinlock_acquire( &p_iou->obj.lock );
	for( p_list_item = cl_qlist_head( &p_iou->obj.obj_list );
		 p_list_item != cl_qlist_end( &p_iou->obj.obj_list );
		 p_list_item = cl_qlist_next( p_list_item ) )
	{
		p_obj = PARENT_STRUCT( p_list_item, al_obj_t, pool_item );
		p_iou_port = PARENT_STRUCT( p_obj, al_iou_port_t, obj );

		status = set_port_dm_attr( p_iou_port );
		if( status != IB_SUCCESS ) break;
	}
	cl_spinlock_release( &p_iou->obj.lock );

	if( status != IB_SUCCESS )
		h_ioc->state = SLOT_IN_USE;

	return status;
}



void
ioc_change(
	IN				ib_ioc_handle_t				h_ioc )
{
	CL_ASSERT( h_ioc );

	/* Report a change to the IO unit which the IOC is attached. */
	if( h_ioc->p_iou ) iou_change( h_ioc->p_iou );
}



void
iou_change(
	IN				al_iou_t*					p_iou )
{
	CL_ASSERT( p_iou );

	/* Increment the IO unit change counter. */
	cl_spinlock_acquire( &p_iou->obj.lock );
	p_iou->change_id++;
	cl_spinlock_release( &p_iou->obj.lock );
}



ib_api_status_t
set_port_dm_attr(
	IN				al_iou_port_t*				p_iou_port )
{
	ib_port_attr_mod_t		port_attr_mod;
	ib_reg_svc_req_t		reg_svc_req;
	ib_api_status_t			status;

	CL_ASSERT( p_iou_port );

	/* Initialize a port attribute modification structure. */
	cl_memclr( &port_attr_mod, sizeof( ib_port_attr_mod_t ) );
	port_attr_mod.cap.dev_mgmt = TRUE;

	/* Flag each port on the IO unit CA as supporting device management. */
	status = ib_modify_ca( p_iou_port->obj.p_ci_ca->h_ca, p_iou_port->port_num,
		IB_CA_MOD_IS_DEV_MGMT_SUPPORTED, &port_attr_mod );

	if( status != IB_SUCCESS )
		return status;

	/* The register a service with the SA if one is needed. */
	if( !p_iou_port->svc_handle )
	{
		/* Build the service registration request. */
		cl_memclr( &reg_svc_req, sizeof( ib_reg_svc_req_t ) );

		reg_svc_req.svc_rec.service_lease = 0xffffffff;
#ifdef CL_KERNEL
	RtlStringCchCopyA(
		(char*)reg_svc_req.svc_rec.service_name,
		sizeof(reg_svc_req.svc_rec.service_name),
		DM_SVC_NAME
		);	
#else
		strcpy_s(
			(char*)reg_svc_req.svc_rec.service_name,
			sizeof(reg_svc_req.svc_rec.service_name),
			DM_SVC_NAME
			);
#endif		
		reg_svc_req.svc_rec.service_gid = p_iou_port->port_gid;
		reg_svc_req.port_guid = p_iou_port->port_guid;

		reg_svc_req.timeout_ms		= SVC_REG_TIMEOUT;
		reg_svc_req.retry_cnt		= SVC_REG_RETRY_CNT;
		reg_svc_req.svc_context		= p_iou_port;
		reg_svc_req.pfn_reg_svc_cb	= iou_port_svc_reg_cb;
		reg_svc_req.svc_data_mask	= IB_SR_COMPMASK_SGID |
									  IB_SR_COMPMASK_SPKEY |
									  IB_SR_COMPMASK_SLEASE |
									  IB_SR_COMPMASK_SNAME;

		/* Reference the IO unit port on behalf of the ib_reg_svc call. */
		ref_al_obj( &p_iou_port->obj );

		status = ib_reg_svc( gh_al, &reg_svc_req, &p_iou_port->svc_handle );

		if( status != IB_SUCCESS )
		{
			deref_al_obj( &p_iou_port->obj );

			/* Ignore this error - the SM will sweep port attribute changes. */
			status = IB_SUCCESS;
		}
	}

	return status;
}



void
iou_port_svc_reg_cb(
	IN				ib_reg_svc_rec_t*			p_reg_svc_rec )
{
	al_iou_port_t*			p_iou_port;

	CL_ASSERT( p_reg_svc_rec );

	p_iou_port = (al_iou_port_t*)p_reg_svc_rec->svc_context;

	if( p_reg_svc_rec->req_status != IB_SUCCESS )
		deref_al_obj( &p_iou_port->obj );
}


/*
 * Device Management Agent
 */


/*
 * Create the device management agent.
 */
ib_api_status_t
create_dm_agent(
	IN				al_obj_t*	const			p_parent_obj )
{
	cl_status_t				cl_status;
	ib_api_status_t			status;

	CL_ASSERT( p_parent_obj );
	CL_ASSERT( !gp_dm_agent );

	gp_dm_agent = cl_zalloc( sizeof( dm_agent_t ) );
	if( !gp_dm_agent )
		return IB_INSUFFICIENT_MEMORY;

	/* Construct the device management agent. */
	construct_al_obj( &gp_dm_agent->obj, AL_OBJ_TYPE_DM );
	cl_spinlock_construct( &gp_dm_agent->lock );

	cl_status = cl_spinlock_init( &gp_dm_agent->lock );
	if( cl_status != CL_SUCCESS )
	{
		free_dm_agent( &gp_dm_agent->obj );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the device management agent object. */
	status = init_al_obj( &gp_dm_agent->obj, gp_dm_agent, TRUE,
		destroying_dm_agent, NULL, free_dm_agent );
	if( status != IB_SUCCESS )
	{
		free_dm_agent( &gp_dm_agent->obj );
		return status;
	}

	/* Attach the device management agent to the parent object. */
	status = attach_al_obj( p_parent_obj, &gp_dm_agent->obj );
	if( status != IB_SUCCESS )
	{
		gp_dm_agent->obj.pfn_destroy( &gp_dm_agent->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Register for CA PnP events. */
	status = dm_agent_reg_pnp( IB_PNP_CA, &gp_dm_agent->h_ca_pnp );
	if (status != IB_SUCCESS)
	{
		gp_dm_agent->obj.pfn_destroy( &gp_dm_agent->obj, NULL );
		return status;
	}

	/* Register for port PnP events. */
	status = dm_agent_reg_pnp( IB_PNP_PORT, &gp_dm_agent->h_port_pnp );
	if (status != IB_SUCCESS)
	{
		gp_dm_agent->obj.pfn_destroy( &gp_dm_agent->obj, NULL );
		return status;
	}

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_dm_agent->obj, E_REF_INIT );

	return IB_SUCCESS;
}



/*
 * Pre-destroy the device management agent.
 */
void
destroying_dm_agent(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	CL_ASSERT( gp_dm_agent == PARENT_STRUCT( p_obj, dm_agent_t, obj ) );
	UNUSED_PARAM( p_obj );

	/* Mark that we're destroying the agent. */
	cl_spinlock_acquire( &gp_dm_agent->lock );
	gp_dm_agent->destroying = TRUE;
	cl_spinlock_release( &gp_dm_agent->lock );

	/* Deregister for port PnP events. */
	if( gp_dm_agent->h_port_pnp )
	{
		status = ib_dereg_pnp( gp_dm_agent->h_port_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	/* Deregister for CA PnP events. */
	if( gp_dm_agent->h_ca_pnp )
	{
		status = ib_dereg_pnp( gp_dm_agent->h_ca_pnp,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Free the device management agent.
 */
void
free_dm_agent(
	IN				al_obj_t*					p_obj )
{
	CL_ASSERT( p_obj );
	CL_ASSERT( gp_dm_agent == PARENT_STRUCT( p_obj, dm_agent_t, obj ) );
	UNUSED_PARAM( p_obj );

	destroy_al_obj( &gp_dm_agent->obj );
	cl_free( gp_dm_agent );
	gp_dm_agent = NULL;
}



/*
 * Register the device management agent for the given PnP class events.
 */
ib_api_status_t
dm_agent_reg_pnp(
	IN				ib_pnp_class_t				pnp_class,
	IN				ib_pnp_handle_t *			ph_pnp )
{
	ib_api_status_t			status;
	ib_pnp_req_t			pnp_req;

	CL_ASSERT( ph_pnp );

	cl_memclr( &pnp_req, sizeof( ib_pnp_req_t ) );
	pnp_req.pnp_class	= pnp_class;
	pnp_req.pnp_context = gp_dm_agent;
	pnp_req.pfn_pnp_cb	= dm_agent_pnp_cb;

	status = ib_reg_pnp( gh_al, &pnp_req, ph_pnp );

	/* Reference the DM agent on behalf of the ib_reg_pnp call. */
	if( status == IB_SUCCESS )
		ref_al_obj( &gp_dm_agent->obj );

	return status;
}



/*
 * Device managment agent PnP event callback.
 */
ib_api_status_t
dm_agent_pnp_cb(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	ib_api_status_t			status;
	al_iou_t*				p_iou;
	al_iou_port_t*			p_iou_port;

	CL_ASSERT( p_pnp_rec );
	CL_ASSERT( p_pnp_rec->pnp_context == gp_dm_agent );

	/* Dispatch based on the PnP event type. */
	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_CA_ADD:
		status = create_iou( p_pnp_rec );
		break;

	case IB_PNP_CA_REMOVE:
		CL_ASSERT( p_pnp_rec->context );
		p_iou = p_pnp_rec->context;
		ref_al_obj( &p_iou->obj );
		p_iou->obj.pfn_destroy( &p_iou->obj, NULL );
		status = IB_SUCCESS;
		break;

	case IB_PNP_PORT_ADD:
		CL_ASSERT( !p_pnp_rec->context );
		status = create_iou_port( (ib_pnp_port_rec_t*)p_pnp_rec );
		break;

	case IB_PNP_PORT_REMOVE:
		CL_ASSERT( p_pnp_rec->context );
		p_iou_port = p_pnp_rec->context;
		ref_al_obj( &p_iou_port->obj );
		p_iou_port->obj.pfn_destroy( &p_iou_port->obj, NULL );

	default:
		/* All other events are ignored. */
		status = IB_SUCCESS;
		break;
	}

	return status;
}



/*
 * Create an IO unit.
 */
ib_api_status_t
create_iou(
	IN				ib_pnp_rec_t*				p_pnp_rec )
{
	al_iou_t*				p_iou;
	ib_ca_handle_t			h_ca;
	ib_api_status_t			status;

	CL_ASSERT( p_pnp_rec );

	p_iou = cl_zalloc( sizeof( al_iou_t ) );
	if( !p_iou )
		return IB_INSUFFICIENT_MEMORY;

	/* Construct the IO unit object. */
	construct_al_obj( &p_iou->obj, AL_OBJ_TYPE_IOU );

	/* Initialize the IO unit object. */
	status =
		init_al_obj( &p_iou->obj, p_iou, TRUE, NULL, cleanup_iou, free_iou );
	if( status != IB_SUCCESS )
	{
		free_iou( &p_iou->obj );
		return status;
	}

	/*
	 * Attach the IO unit to the device management agent.  Lock and
	 * check to synchronize the destruction of the user-mode device
	 * management agent with the creation of the IO unit through a
	 * PnP callback.
	 */
	cl_spinlock_acquire( &gp_dm_agent->lock );
	if( gp_dm_agent->destroying )
	{
		p_iou->obj.pfn_destroy( &p_iou->obj, NULL );
		cl_spinlock_release( &gp_dm_agent->lock );
		return IB_INVALID_STATE;
	}
	status = attach_al_obj( &gp_dm_agent->obj, &p_iou->obj );
	if( status != IB_SUCCESS )
	{
		p_iou->obj.pfn_destroy( &p_iou->obj, NULL );
		cl_spinlock_release( &gp_dm_agent->lock );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}
	cl_spinlock_release( &gp_dm_agent->lock );

	/* It is now safe to acquire the CA and initialize the p_ci_ca pointer. */
	h_ca = acquire_ca( p_pnp_rec->guid );
	if( !h_ca )
	{
		p_iou->obj.pfn_destroy( &p_iou->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("acquire_ca for GUID %016I64x failed.\n", p_pnp_rec->guid) );
		return IB_INVALID_CA_HANDLE;
	}

	p_iou->obj.p_ci_ca = h_ca->obj.p_ci_ca;

	/* Initialize the IO unit IOC list. */
	cl_qlist_init( &p_iou->ioc_list );

	/* Set the context of the PnP event to this child object. */
	p_pnp_rec->context = p_iou;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_iou->obj, E_REF_INIT );

	return IB_SUCCESS;
}



/*
 * Cleanup an IO unit.
 */
void
cleanup_iou(
	IN				al_obj_t*					p_obj )
{
	al_iou_t*				p_iou;
	cl_list_item_t*			p_ioc_item;
	ib_ioc_handle_t			h_ioc;

	CL_ASSERT( p_obj );
	p_iou = PARENT_STRUCT( p_obj, al_iou_t, obj );

	/* No need to lock during cleanup. */
	for( p_ioc_item = cl_qlist_remove_head( &p_iou->ioc_list );
		p_ioc_item != cl_qlist_end( &p_iou->ioc_list );
		p_ioc_item = cl_qlist_remove_head( &p_iou->ioc_list ) )
	{
		h_ioc = PARENT_STRUCT( p_ioc_item, al_ioc_t, obj );

		CL_ASSERT( h_ioc->state == EMPTY_SLOT );

		/* Detach the IOC from the IO unit. */
		CL_ASSERT( h_ioc->p_iou == p_iou );
		h_ioc->p_iou = NULL;

		/* Destroy the IOC. */
		ref_al_obj( &h_ioc->obj );
		h_ioc->obj.pfn_destroy( &h_ioc->obj, NULL );
	}
}



/*
 * Free an IO unit.
 */
void
free_iou(
	IN				al_obj_t*					p_obj )
{
	al_iou_t*				p_iou;

	CL_ASSERT( p_obj );

	p_iou = PARENT_STRUCT( p_obj, al_iou_t, obj );

	/* Dereference the CA. */
	if( p_iou->obj.p_ci_ca )
		deref_al_obj( &p_iou->obj.p_ci_ca->h_ca->obj );

	destroy_al_obj( &p_iou->obj );
	cl_free( p_iou );
}



/*
 * Create an IO unit port.
 */
ib_api_status_t
create_iou_port(
	IN				ib_pnp_port_rec_t*			p_pnp_rec )
{
	al_iou_port_t*			p_iou_port;
	al_iou_t*				p_iou;
	ib_qp_create_t			qp_create;
	ib_mad_svc_t			mad_svc;
	ib_api_status_t			status;

	CL_ASSERT( p_pnp_rec );

	CL_ASSERT( p_pnp_rec->p_ca_attr );
	CL_ASSERT( p_pnp_rec->p_port_attr );

	p_iou_port = cl_zalloc( sizeof( al_iou_port_t ) );
	if( !p_iou_port )
		return IB_INSUFFICIENT_MEMORY;

	/* Construct the IO unit port object. */
	construct_al_obj( &p_iou_port->obj, AL_OBJ_TYPE_IOU );

	/* Initialize the IO unit port object. */
	status = init_al_obj( &p_iou_port->obj, p_iou_port, TRUE,
		destroying_iou_port, NULL, free_iou_port );
	if( status != IB_SUCCESS )
	{
		free_iou_port( &p_iou_port->obj );
		return status;
	}

	/* Acquire the IO unit. */
	p_iou = acquire_iou( p_pnp_rec->p_ca_attr->ca_guid );
	if( !p_iou )
	{
		p_iou_port->obj.pfn_destroy( &p_iou_port->obj, NULL );
		return IB_INVALID_GUID;
	}

	/* Attach the IO unit port to the IO unit. */
	status = attach_al_obj( &p_iou->obj, &p_iou_port->obj );
	if( status != IB_SUCCESS )
	{
		p_iou_port->obj.pfn_destroy( &p_iou_port->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}
	deref_al_obj( &p_iou->obj );

	/* Save the port number. */
	p_iou_port->port_num = p_pnp_rec->p_port_attr->port_num;

	/* Save the port GUID - used in svc reg. */
	p_iou_port->port_guid = p_pnp_rec->pnp_rec.guid;

	/* Save the default port gid and pkey */
	p_iou_port->port_gid = p_pnp_rec->p_port_attr->p_gid_table[0];
	p_iou_port->port_pkey = p_pnp_rec->p_port_attr->p_pkey_table[0];

	/* Create a QP alias. */
	cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
	qp_create.qp_type		= IB_QPT_QP1_ALIAS;
	qp_create.sq_depth		= 1;
	qp_create.sq_sge		= 1;
	qp_create.sq_signaled	= TRUE;

	status = ib_get_spl_qp( p_iou_port->obj.p_ci_ca->h_pd_alias,
		p_pnp_rec->p_port_attr->port_guid, &qp_create,
		p_iou_port, iou_port_event_cb, &p_iou_port->pool_key,
		&p_iou_port->h_qp_alias );

	if (status != IB_SUCCESS)
	{
		p_iou_port->obj.pfn_destroy( &p_iou_port->obj, NULL );
		return status;
	}

	/* Reference the IO unit port on behalf of ib_get_spl_qp. */
	ref_al_obj( &p_iou_port->obj );

	/* Register a service the MAD service for device management. */
	cl_memclr( &mad_svc, sizeof( ib_mad_svc_t ) );
	mad_svc.mad_svc_context = p_iou_port;
	mad_svc.pfn_mad_send_cb = dm_agent_send_cb;
	mad_svc.pfn_mad_recv_cb = dm_agent_recv_cb;
	mad_svc.support_unsol	= TRUE;
	mad_svc.mgmt_class		= IB_MCLASS_DEV_MGMT;
	mad_svc.mgmt_version	= 1;
	mad_svc.method_array[ IB_MAD_METHOD_GET ] = TRUE;
	mad_svc.method_array[ IB_MAD_METHOD_SET ] = TRUE;

	status = ib_reg_mad_svc( p_iou_port->h_qp_alias, &mad_svc,
		&p_iou_port->h_mad_svc );
	if( status != IB_SUCCESS )
	{
		p_iou_port->obj.pfn_destroy( &p_iou_port->obj, NULL );
		return status;
	}

	/* Determine if any IOCs are attached to this IO unit. */
	cl_spinlock_acquire( &p_iou->obj.lock );
	if( !cl_is_qlist_empty( &p_iou->ioc_list ) )
	{
		/* Set the device management port attribute. */
		status = set_port_dm_attr( p_iou_port );
		CL_ASSERT( status == IB_SUCCESS );
	}
	cl_spinlock_release( &p_iou->obj.lock );

	/* Set the context of the PnP event to this child object. */
	p_pnp_rec->pnp_rec.context = p_iou_port;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_iou_port->obj, E_REF_INIT );

	return IB_SUCCESS;
}



/*
 * Pre-destroy an IO unit port.
 */
void
destroying_iou_port(
	IN				al_obj_t*					p_obj )
{
	al_iou_port_t*			p_iou_port;
	ib_api_status_t			status;

	CL_ASSERT( p_obj );
	p_iou_port = PARENT_STRUCT( p_obj, al_iou_port_t, obj );

	/* Deregister the device management service. */
	if( p_iou_port->svc_handle )
	{
		status = ib_dereg_svc( p_iou_port->svc_handle,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}

	/* Destroy the QP alias. */
	if( p_iou_port->h_qp_alias )
	{
		status = ib_destroy_qp( p_iou_port->h_qp_alias,
			(ib_pfn_destroy_cb_t)deref_al_obj_cb );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Free an IO unit port.
 */
void
free_iou_port(
	IN				al_obj_t*					p_obj )
{
	al_iou_port_t*			p_iou_port;

	CL_ASSERT( p_obj );

	p_iou_port = PARENT_STRUCT( p_obj, al_iou_port_t, obj );

	destroy_al_obj( &p_iou_port->obj );
	cl_free( p_iou_port );
}



/*
 * IO unit port asynchronous event callback.
 */
void
iou_port_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );

	/* The QP is an alias, so if we've received an error, it is unusable. */
}



/*
 * Device management agent send completion callback.
 */
void
dm_agent_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_response )
{
	ib_api_status_t			status;

	CL_ASSERT( mad_svc_context );
	CL_ASSERT( p_mad_response );
	UNUSED_PARAM( h_mad_svc );
	UNUSED_PARAM( mad_svc_context );

	/* Return the MAD. */
	status = ib_destroy_av_ctx( p_mad_response->h_av, 1 );
	p_mad_response->h_av = NULL;
	CL_ASSERT( status == IB_SUCCESS );
	status = ib_put_mad( p_mad_response );
	CL_ASSERT( status == IB_SUCCESS );
}



/*
 * Device management agent receive completion callback.
 */
void
dm_agent_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void*						mad_svc_context,
	IN				ib_mad_element_t*			p_mad_request )
{
	al_iou_port_t*			p_iou_port;
	ib_mad_element_t*		p_mad_response;
	ib_mad_t*				p_mad_req;
	ib_mad_t*				p_mad_rsp;
	ib_av_attr_t			av_attr;
	ib_api_status_t			status;

	CL_ASSERT( mad_svc_context );
	CL_ASSERT( p_mad_request );

	p_iou_port = mad_svc_context;
	p_mad_req = ib_get_mad_buf( p_mad_request );

	/* Get a MAD element for the response. */
	status = ib_get_mad( p_iou_port->pool_key, MAD_BLOCK_SIZE,
		&p_mad_response );

	if( status != IB_SUCCESS )
	{
		status = ib_put_mad( p_mad_request );
		CL_ASSERT( status == IB_SUCCESS );
		return;
	}

	/* Initialize the response MAD element. */
	p_mad_response->remote_qp	= p_mad_request->remote_qp;
	p_mad_response->remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
	p_mad_rsp = ib_get_mad_buf( p_mad_response );

	/* Create an address vector for the response. */
	cl_memclr( &av_attr, sizeof( ib_av_attr_t ) );
	av_attr.port_num	= p_iou_port->port_num;
	av_attr.sl			= p_mad_request->remote_sl;
	av_attr.dlid		= p_mad_request->remote_lid;
	av_attr.path_bits	= p_mad_request->path_bits;
	av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
	if( p_mad_request->grh_valid )
	{
		av_attr.grh_valid	= TRUE;
		av_attr.grh			= *p_mad_request->p_grh;
	}

	status = ib_create_av_ctx( p_iou_port->obj.p_ci_ca->h_pd_alias, &av_attr, 1,
		&p_mad_response->h_av );

	if( status != IB_SUCCESS )
	{
		status = ib_put_mad( p_mad_request );
		CL_ASSERT( status == IB_SUCCESS );
		status = ib_put_mad( p_mad_response );
		CL_ASSERT( status == IB_SUCCESS );
		return;
	}

	/* Initialize the response header. */
	ib_mad_init_response( p_mad_req, p_mad_rsp, 0 );

	/* Process the MAD request. */
	switch( p_mad_req->method )
	{
	case IB_MAD_METHOD_GET:
		dm_agent_get( p_iou_port, p_mad_req, p_mad_rsp );
		break;

	case IB_MAD_METHOD_SET:
		dm_agent_set( p_iou_port, p_mad_req, p_mad_rsp );
		break;

	default:
		p_mad_rsp->status = IB_MAD_STATUS_UNSUP_METHOD;
		break;
	}

	/* Return the request to the pool. */
	status = ib_put_mad( p_mad_request );
	CL_ASSERT( status == IB_SUCCESS );

	/* Send the response. */
	status = ib_send_mad( h_mad_svc, p_mad_response, NULL );

	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_send_mad in dm_agent_recv_cb returned %s.\n", ib_get_err_str(status)) );
		status = ib_destroy_av_ctx( p_mad_response->h_av, 2 );
		p_mad_response->h_av = NULL;
		CL_ASSERT( status == IB_SUCCESS );
		status = ib_put_mad( p_mad_response );
		CL_ASSERT( status == IB_SUCCESS );
	}
}



/*
 * Device management agent get method MAD.
 */
void
dm_agent_get(
	IN				al_iou_port_t*				p_iou_port,
	IN				ib_mad_t*					p_mad_req,
	IN				ib_mad_t*					p_mad_rsp )
{
	al_iou_t*				p_iou;
	ib_dm_mad_t*			p_dm_mad;

	CL_ASSERT( p_iou_port );
	CL_ASSERT( p_mad_req );
	CL_ASSERT( p_mad_rsp );

	p_iou = PARENT_STRUCT( p_iou_port->obj.p_parent_obj, al_iou_t, obj );

	p_dm_mad = (ib_dm_mad_t*)p_mad_rsp;

	switch( p_mad_req->attr_id )
	{
	case IB_MAD_ATTR_CLASS_PORT_INFO:
		get_class_port_info( p_iou, p_dm_mad );
		break;

	case IB_MAD_ATTR_IO_UNIT_INFO:
		get_io_unit_info( p_iou, p_dm_mad );
		break;

	case IB_MAD_ATTR_IO_CONTROLLER_PROFILE:
	{
		uint8_t		slot;

		slot = (uint8_t)CL_NTOH32( p_dm_mad->hdr.attr_mod );

		get_ioc_profile( p_iou, slot, p_dm_mad );
		break;
	}

	case IB_MAD_ATTR_SERVICE_ENTRIES:
	{
		uint8_t		slot;
		uint8_t		svc_num_hi;
		uint8_t		svc_num_lo;

		ib_dm_get_slot_lo_hi( p_dm_mad->hdr.attr_mod, &slot,
			&svc_num_hi, &svc_num_lo );

		get_svc_entries( p_iou, slot, svc_num_lo, svc_num_hi, p_dm_mad );
		break;
	}

	case IB_MAD_ATTR_DIAGNOSTIC_TIMEOUT:
	case IB_MAD_ATTR_PREPARE_TO_TEST:
	case IB_MAD_ATTR_DIAG_CODE:
	default:
		p_mad_rsp->status = IB_MAD_STATUS_UNSUP_METHOD_ATTR;
		break;
	}
}



/*
 * Device management agent set method MAD.
 */
void
dm_agent_set(
	IN				al_iou_port_t*				p_iou_port,
	IN				ib_mad_t*					p_mad_req,
	IN				ib_mad_t*					p_mad_rsp )
{
	ib_dm_mad_t*			p_dm_mad;

	CL_ASSERT( p_iou_port );
	CL_ASSERT( p_mad_req );
	CL_ASSERT( p_mad_rsp );
	UNUSED_PARAM( p_iou_port );

	p_dm_mad = (ib_dm_mad_t*)p_mad_rsp;

	switch( p_mad_req->attr_id )
	{
	case IB_MAD_ATTR_CLASS_PORT_INFO:
		break;

	case IB_MAD_ATTR_PREPARE_TO_TEST:
	case IB_MAD_ATTR_TEST_DEVICE_ONCE:
	case IB_MAD_ATTR_TEST_DEVICE_LOOP:
	default:
		p_mad_rsp->status = IB_MAD_STATUS_UNSUP_METHOD_ATTR;
		break;
	}
}


void
get_class_port_info(
	IN				al_iou_t*					p_iou,
	IN				ib_dm_mad_t*				p_dm_mad )
{
	ib_class_port_info_t*	p_class_port_info;

	CL_ASSERT( p_iou );
	CL_ASSERT( p_dm_mad );
	UNUSED_PARAM( p_iou );

	p_class_port_info = (ib_class_port_info_t*)&p_dm_mad->data;

	p_class_port_info->base_ver	 = 1;
	p_class_port_info->class_ver = 1;
	p_class_port_info->cap_mask2_resp_time = CL_HTON32( DM_CLASS_RESP_TIME_VALUE );
}



void
get_io_unit_info(
	IN				al_iou_t*					p_iou,
	IN				ib_dm_mad_t*				p_dm_mad )
{
	ib_iou_info_t*			p_iou_info;
	cl_list_item_t*			p_ioc_item;
	ib_ioc_handle_t			h_ioc;
	uint8_t					slot;

	CL_ASSERT( p_iou );
	CL_ASSERT( p_dm_mad );

	p_iou_info = (ib_iou_info_t*)&p_dm_mad->data;

	cl_spinlock_acquire( &p_iou->obj.lock );

	p_iou_info->change_id = p_iou->change_id;

	/* Mark all slots as non-existant. */
	SET_NIBBLE( &slot, 0, SLOT_DOES_NOT_EXIST );
	SET_NIBBLE( &slot, 1, SLOT_DOES_NOT_EXIST );
	cl_memset( p_iou_info->controller_list, slot, sizeof( p_iou->ioc_list ) );

	/* Now mark the existing slots. */
	slot = 1;
	for( p_ioc_item = cl_qlist_head( &p_iou->ioc_list );
		 p_ioc_item != cl_qlist_end( &p_iou->ioc_list );
		 p_ioc_item = cl_qlist_next( p_ioc_item ) )
	{
		h_ioc = PARENT_STRUCT( p_ioc_item, al_ioc_t, iou_item );

		switch( h_ioc->state )
		{
		case EMPTY_SLOT:
		case SLOT_IN_USE:
			SET_NIBBLE( p_iou_info->controller_list, slot, IOC_NOT_INSTALLED );
			break;

		case IOC_ACTIVE:
			SET_NIBBLE( p_iou_info->controller_list, slot, IOC_INSTALLED );
			break;

		default:
			break;
		}
		slot++;
	}

	p_iou_info->max_controllers = slot;

	cl_spinlock_release( &p_iou->obj.lock );
}



void
get_ioc_profile(
	IN				al_iou_t*					p_iou,
	IN				uint8_t						slot,
	IN				ib_dm_mad_t*				p_dm_mad )
{
	ib_ioc_profile_t*		p_ioc_profile;
	cl_list_item_t*			p_ioc_item;
	ib_ioc_handle_t			h_ioc;

	CL_ASSERT( p_iou );
	CL_ASSERT( p_dm_mad );

	p_ioc_profile = (ib_ioc_profile_t*)&p_dm_mad->data;

	cl_spinlock_acquire( &p_iou->obj.lock );

	/* Verify that the slot number is within range. */
	if( ( slot == 0 ) ||
		( slot > cl_qlist_count( &p_iou->ioc_list ) ) )
	{
		cl_spinlock_release( &p_iou->obj.lock );
		p_dm_mad->hdr.status = IB_MAD_STATUS_INVALID_FIELD;
		return;
	}

	/* The remaining code assumes the slot number starts at zero. */
	for( p_ioc_item = cl_qlist_head( &p_iou->ioc_list );
		 p_ioc_item != cl_qlist_end( &p_iou->ioc_list ) && slot;
		 p_ioc_item = cl_qlist_next( p_ioc_item ) )
	{
		slot--;
	}

	h_ioc = PARENT_STRUCT( p_ioc_item, al_ioc_t, iou_item );

	cl_spinlock_acquire( &h_ioc->obj.lock );

	/* Verify the IOC state. */
	if( h_ioc->state != IOC_ACTIVE )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		cl_spinlock_release( &p_iou->obj.lock );
		p_dm_mad->hdr.status = IB_DM_MAD_STATUS_NO_IOC_RESP;
		return;
	}

	/* Copy the IOC profile. */
	*p_ioc_profile = h_ioc->ioc_profile;

	cl_spinlock_release( &h_ioc->obj.lock );
	cl_spinlock_release( &p_iou->obj.lock );
}



void
get_svc_entries(
	IN				al_iou_t*					p_iou,
	IN				uint8_t						slot,
	IN				uint8_t						svc_num_lo,
	IN				uint8_t						svc_num_hi,
	IN				ib_dm_mad_t*				p_dm_mad )
{
	ib_svc_entries_t*		p_svc_entries;
	cl_list_item_t*			p_ioc_item;
	cl_list_item_t*			p_list_item;
	ib_ioc_handle_t			h_ioc;
	al_obj_t*				p_obj;
	al_svc_entry_t*			p_svc_entry;
	uint8_t					i, j, k;

	CL_ASSERT( p_iou );
	CL_ASSERT( p_dm_mad );

	p_svc_entries = (ib_svc_entries_t*)&p_dm_mad->data;

	cl_spinlock_acquire( &p_iou->obj.lock );

	/*
	 * Verify that the slot number is within range and
	 * a maximum of SVC_ENTRY_COUNT entries is requested.
	 */
	if( ( slot == 0 ) ||
		( slot > cl_qlist_count( &p_iou->ioc_list ) ) ||
		( ( svc_num_hi - svc_num_lo + 1) > SVC_ENTRY_COUNT ) )
	{
		cl_spinlock_release( &p_iou->obj.lock );
		p_dm_mad->hdr.status = IB_MAD_STATUS_INVALID_FIELD;
		return;
	}

	/* The remaining code assumes the slot number starts at zero. */
	for( p_ioc_item = cl_qlist_head( &p_iou->ioc_list );
		 p_ioc_item != cl_qlist_end( &p_iou->ioc_list ) && slot;
		 p_ioc_item = cl_qlist_next( p_ioc_item ) )
	{
		slot--;
	}

	h_ioc = PARENT_STRUCT( p_ioc_item, al_ioc_t, iou_item );

	cl_spinlock_acquire( &h_ioc->obj.lock );

	/* Verify the IOC state. */
	if( h_ioc->state != IOC_ACTIVE )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		cl_spinlock_release( &p_iou->obj.lock );
		p_dm_mad->hdr.status = IB_DM_MAD_STATUS_NO_IOC_RESP;
		return;
	}

	/* Verify the service entry range. */
	if( ( svc_num_lo > h_ioc->ioc_profile.num_svc_entries ) ||
		( svc_num_hi >= h_ioc->ioc_profile.num_svc_entries ) )
	{
		cl_spinlock_release( &h_ioc->obj.lock );
		cl_spinlock_release( &p_iou->obj.lock );
		p_dm_mad->hdr.status = IB_MAD_STATUS_INVALID_FIELD;
		return;
	}

	for( i = svc_num_lo, j = 0; j < ( svc_num_hi - svc_num_lo + 1 ); i++, j++ )
	{
		k = i;

		/* Locate the service entry. Traverse until k=0. */
		for( p_list_item = cl_qlist_head( &h_ioc->obj.obj_list );
			 k && ( p_list_item != cl_qlist_end( &h_ioc->obj.obj_list ) );
			 p_list_item = cl_qlist_next( p_list_item ) )
		{
			k--;
		}

		if( p_list_item == cl_qlist_end( &h_ioc->obj.obj_list ) )
		{
			/* The service entry list was empty or the end was reached. */
			cl_spinlock_release( &h_ioc->obj.lock );
			cl_spinlock_release( &p_iou->obj.lock );
			p_dm_mad->hdr.status = IB_DM_MAD_STATUS_NO_SVC_ENTRIES;
			return;
		}

		p_obj = PARENT_STRUCT( p_list_item, al_obj_t, obj_list );
		p_svc_entry = PARENT_STRUCT( p_obj, al_svc_entry_t, obj );

		/* Copy the service entry. */
		p_svc_entries->service_entry[ j ] = p_svc_entry->svc_entry;
	}

	cl_spinlock_release( &h_ioc->obj.lock );
	cl_spinlock_release( &p_iou->obj.lock );
}
