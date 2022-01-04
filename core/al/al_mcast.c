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
#include "al_ca.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mcast.tmh"
#endif

#include "al_mgr.h"
#include "al_qp.h"
#include "al_verbs.h"

#include "ib_common.h"


/*
 * Function prototypes.
 */
static ib_api_status_t
send_join(
	IN				ib_mcast_t					*p_mcast,
	IN		const	ib_mcast_req_t* const		p_mcast_req );

static void
join_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response );

static void
join_async_cb(
	IN				cl_async_proc_item_t		*p_item );

static void
leave_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response );

static void
leave_async_cb(
	IN				cl_async_proc_item_t		*p_item );

static void
__destroying_mcast(
	IN				al_obj_t					*p_obj );

static void
__cleanup_mcast(
	IN				al_obj_t					*p_obj );

static void
__free_mcast(
	IN				al_obj_t					*p_obj );

#ifdef CL_KERNEL
static void
__cleanup_attach(
	IN				al_obj_t					*p_obj );

static void
__free_attach(
	IN				al_obj_t					*p_obj );
#endif


ib_api_status_t
al_join_mcast_common(
	IN		const	ib_qp_handle_t				h_qp OPTIONAL,
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	ib_mcast_handle_t		h_mcast;
	ib_api_status_t			status;
	cl_status_t				cl_status;
	boolean_t				sync;

	AL_ENTER( AL_DBG_MCAST );

	if (p_h_mcast)
	{
		*p_h_mcast = NULL;
	}

	/* Allocate a new multicast request. */
	h_mcast = cl_zalloc( sizeof( ib_mcast_t ) );
	if( !h_mcast )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("zalloc of h_mcast failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the AL object so we can call destroy_al_obj in case of failure. */
	construct_al_obj( &h_mcast->obj, AL_OBJ_TYPE_H_MCAST );

	/* Check for synchronous operation. */
	h_mcast->flags = p_mcast_req->flags;
	cl_event_construct( &h_mcast->event );
	sync = ( (h_mcast->flags & IB_FLAGS_SYNC) == IB_FLAGS_SYNC );
	if( sync )
	{
		if( !cl_is_blockable() )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Thread context not blockable\n") );
			__free_mcast( &h_mcast->obj );
			return IB_INVALID_SETTING;
		}

		cl_status = cl_event_init( &h_mcast->event, TRUE );
		if( cl_status != CL_SUCCESS )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("unable to initialize event for sync operation\n") );
			__free_mcast( &h_mcast->obj );
			return ib_convert_cl_status( cl_status );
		}
	}

	/* Initialize the AL object now. */
	status = init_al_obj( &h_mcast->obj, p_mcast_req->mcast_context, TRUE,
		__destroying_mcast, __cleanup_mcast, __free_mcast );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj returned %s\n", ib_get_err_str( status )) );
		__free_mcast( &h_mcast->obj );
		return status;
	}

	/* Copy the multicast context information. */
	h_mcast->pfn_mcast_cb = p_mcast_req->pfn_mcast_cb;
	/*
	 * Copy the mcast member record so that we can leave without requiring the
	 * user to provide the settings.
	 */
	h_mcast->member_rec = p_mcast_req->member_rec;
	h_mcast->port_guid = p_mcast_req->port_guid;

	/* Track the multicast with the QP instance. */

	if (h_qp) {
		status = attach_al_obj( &h_qp->obj, &h_mcast->obj );
		if( status != IB_SUCCESS )
		{
			h_mcast->obj.pfn_destroy( &h_mcast->obj, NULL );
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
			return status;
		}
	}

	if (p_h_mcast) 
	{
		ref_al_obj(&h_mcast->obj);
		*p_h_mcast = h_mcast;
	}

	/* Issue the MAD to the SA. */
	status = send_join( h_mcast, p_mcast_req );
	if( status == IB_SUCCESS )
	{
		/* If synchronous, wait for the completion. */
		if( sync )
		{
			do
			{
				cl_status = cl_event_wait_on(
					&h_mcast->event, EVENT_NO_TIMEOUT, AL_WAIT_ALERTABLE );
			} while( cl_status == CL_NOT_DONE );
			CL_ASSERT( cl_status == CL_SUCCESS );
		}
	}
	else
	{
		if (p_h_mcast) 
		{
			deref_al_obj(&h_mcast->obj);
			*p_h_mcast = NULL;
		}
		
		// usually it happens, when opensm is not running- that's why warning
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_ERROR,
			("unable to send join request: %s\n", ib_get_err_str(status)) );
		h_mcast->obj.pfn_destroy( &h_mcast->obj, NULL );
	}

	/*
	 * Note: Don't release the reference taken in init_al_obj while we
	 * have the SA req outstanding.
	 */

	AL_EXIT( AL_DBG_MCAST );
	return status;
}


ib_api_status_t
al_join_mcast_no_qp(
	IN		const	ib_mcast_req_t* const		p_mcast_req, 
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	return al_join_mcast_common(NULL, p_mcast_req, p_h_mcast );

}

ib_api_status_t
al_join_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mcast_req_t* const		p_mcast_req,
	OUT		ib_mcast_handle_t*					p_h_mcast	)
{
	ib_api_status_t			status;
	/*
	 * Validate the port GUID.  There is no need to validate the pkey index as
	 * the user could change it later to make it invalid.  There is also no
	 * need to perform any QP transitions as ib_init_dgrm_svc resets the QP and
	 * starts from scratch.
	 */

	status = get_port_num( h_qp->obj.p_ci_ca, p_mcast_req->port_guid, NULL );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("get_port_num failed, status: %s\n", ib_get_err_str(status)) );
		return status;
	}

	return al_join_mcast_common(h_qp, p_mcast_req, p_h_mcast);
}

static void
__destroying_mcast(
	IN				al_obj_t					*p_obj )
{
	ib_mcast_handle_t		h_mcast;
	ib_user_query_t			sa_mad_data;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MCAST );

	h_mcast = PARENT_STRUCT( p_obj, ib_mcast_t, obj );

	if( h_mcast->state != SA_REG_STARTING && h_mcast->state != SA_REG_ACTIVE )
	{
		AL_EXIT( AL_DBG_MCAST );
		return;
	}

	if( h_mcast->state == SA_REG_STARTING )
	{
		cl_spinlock_acquire( &h_mcast->obj.lock );
		/* Cancel all outstanding join requests. */
		h_mcast->state = SA_REG_CANCELING;
#if defined( CL_KERNEL )
		if( h_mcast->sa_reg_req.p_sa_req_svc )
			al_cancel_sa_req( &h_mcast->sa_reg_req );
#else	/* defined( CL_KERNEL ) */
		if( h_mcast->sa_reg_req.hdl )
			al_cancel_sa_req( &h_mcast->sa_reg_req );
#endif	/* defined( CL_KERNEL ) */
		cl_spinlock_release( &h_mcast->obj.lock );
	}

	/* Set the request information. */
	h_mcast->sa_dereg_req.pfn_sa_req_cb = leave_req_cb;

	/* Set the MAD attributes and component mask correctly. */
	sa_mad_data.method = IB_MAD_METHOD_DELETE;
	sa_mad_data.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
	sa_mad_data.attr_size = sizeof( ib_member_rec_t );

	/* Set the component mask. */
	sa_mad_data.comp_mask = IB_MCR_COMPMASK_MGID |
		IB_MCR_COMPMASK_PORT_GID | IB_MCR_COMPMASK_JOIN_STATE;

	sa_mad_data.p_attr = &h_mcast->member_rec;

	ref_al_obj( &h_mcast->obj );
	status = al_send_sa_req(
		&h_mcast->sa_dereg_req, h_mcast->port_guid, 500, 0, &sa_mad_data, 0 );
	if( status != IB_SUCCESS )
		deref_al_obj( &h_mcast->obj );

	AL_EXIT( AL_DBG_MCAST );
}


static void
__cleanup_mcast(
	IN				al_obj_t					*p_obj )
{
	ib_mcast_handle_t		h_mcast;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MCAST );

	h_mcast = PARENT_STRUCT( p_obj, ib_mcast_t, obj );

	/*
	 * Detach from the multicast group to ensure that multicast messages
	 * are not received on this QP again.  Note that we need to check for
	 * a valid verbs handle in case the attach failed earlier, and we are
	 * just calling ib_leave_mcast to notify the SA.
	 */
	if( h_mcast->h_ci_mcast )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MCAST,
			("detaching from multicast group\n") );
		status = verbs_detach_mcast( h_mcast );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("detach failed: %s\n", ib_get_err_str(status)) );
		}
	}

	AL_EXIT( AL_DBG_MCAST );
}


static void
__free_mcast(
	IN				al_obj_t					*p_obj )
{
	ib_mcast_handle_t		h_mcast;

	h_mcast = PARENT_STRUCT( p_obj, ib_mcast_t, obj );

	cl_event_destroy( &h_mcast->event );
	destroy_al_obj( &h_mcast->obj );
	cl_free( h_mcast );
}



/*
 * Format an SA request based on the user's request.
 */
static ib_api_status_t
send_join(
	IN				ib_mcast_t					*p_mcast,
	IN		const	ib_mcast_req_t* const		p_mcast_req )
{
	ib_user_query_t			sa_mad_data;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MCAST );

	/* Set the request information. */
	p_mcast->sa_reg_req.pfn_sa_req_cb = join_req_cb;

	ib_gid_set_default( &p_mcast->member_rec.port_gid, p_mcast_req->port_guid );

	/* Set the MAD attributes and component mask correctly. */
	sa_mad_data.method = IB_MAD_METHOD_SET;
	sa_mad_data.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
	sa_mad_data.attr_size = sizeof( ib_member_rec_t );

	/* Initialize the component mask. */
	sa_mad_data.comp_mask = IB_MCR_COMPMASK_MGID |
		IB_MCR_COMPMASK_PORT_GID | IB_MCR_COMPMASK_JOIN_STATE;

	if( p_mcast_req->create	)
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MCAST,
			("requesting creation of mcast group\n") );

		/* Set the necessary creation components. */
		sa_mad_data.comp_mask |= IB_MCR_COMPMASK_QKEY |
			IB_MCR_COMPMASK_TCLASS | IB_MCR_COMPMASK_PKEY |
			IB_MCR_COMPMASK_FLOW | IB_MCR_COMPMASK_SL;

		/* Set the MTU mask if so requested. */
		if( p_mcast_req->member_rec.mtu )
		{
			sa_mad_data.comp_mask |= IB_MCR_COMPMASK_MTU_SEL;
			if( (p_mcast_req->member_rec.mtu >> 6) !=
				IB_PATH_SELECTOR_LARGEST )
			{
				sa_mad_data.comp_mask |= IB_MCR_COMPMASK_MTU;
			}
		}

		/* Set the rate mask if so requested. */
		if( p_mcast_req->member_rec.rate )
		{
			sa_mad_data.comp_mask |= IB_MCR_COMPMASK_RATE_SEL;
			if( (p_mcast_req->member_rec.rate >> 6) !=
				IB_PATH_SELECTOR_LARGEST )
			{
				sa_mad_data.comp_mask |= IB_MCR_COMPMASK_RATE;
			}
		}

		/* Set the packet lifetime mask if so requested. */
		if( p_mcast_req->member_rec.pkt_life )
		{
			sa_mad_data.comp_mask |= IB_MCR_COMPMASK_LIFE_SEL;
			if( (p_mcast_req->member_rec.pkt_life >> 6) !=
				IB_PATH_SELECTOR_LARGEST )
			{
				sa_mad_data.comp_mask |= IB_MCR_COMPMASK_LIFE;
			}
		}
	}

	sa_mad_data.p_attr = &p_mcast->member_rec;

	p_mcast->state = SA_REG_STARTING;
	status = al_send_sa_req( &p_mcast->sa_reg_req, p_mcast->port_guid,
		p_mcast_req->timeout_ms, p_mcast_req->retry_cnt, &sa_mad_data, 0 );

	AL_EXIT( AL_DBG_MCAST );
	return status;
}


/*
 * Multicast join completion callback.
 */
static void
join_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response )
{
	ib_mcast_handle_t		h_mcast;
	ib_sa_mad_t				*p_sa_mad;

	AL_ENTER( AL_DBG_MCAST );
	h_mcast = PARENT_STRUCT( p_sa_req, ib_mcast_t, sa_reg_req );

	/* Record the status of the join request. */
	h_mcast->req_status = p_sa_req->status;

	if( p_mad_response )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MCAST, ("processing response\n") );
		p_sa_mad = (ib_sa_mad_t*)ib_get_mad_buf( p_mad_response );
		h_mcast->resp_status = p_sa_mad->status;

		/* Record the join membership information. */
		if( h_mcast->req_status == IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MCAST, ("join successful\n") );
			h_mcast->member_rec = *((ib_member_rec_t*)p_sa_mad->data);
		}

		/* We no longer need the response MAD. */
		ib_put_mad( p_mad_response );
	}

	/*
	 * Finish processing the join in the async callback context since
	 * we can't attach the QP to the mcast group at dispatch.
	 */
	h_mcast->async.pfn_callback = join_async_cb;
	cl_async_proc_queue( gp_async_proc_mgr, &h_mcast->async );
	AL_EXIT( AL_DBG_MCAST );
}


/*
 * Process the results of a join request.  This call is invoked from
 * the asynchronous processing manager to allow invoking the
 * VPD's attach_mcast entrypoint at passive level.
 */
static void
join_async_cb(
	IN				cl_async_proc_item_t		*p_item )
{
	ib_api_status_t			status;
	ib_mcast_handle_t		h_mcast;
	ib_mcast_rec_t			mcast_rec;
	boolean_t				sync;

	AL_ENTER( AL_DBG_MCAST );
#if defined( CL_KERNEL )
	CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);
#endif

	h_mcast = PARENT_STRUCT( p_item, ib_mcast_t, async );

	cl_spinlock_acquire( &h_mcast->obj.lock );
#if defined( CL_KERNEL )
	CL_ASSERT( h_mcast->sa_reg_req.p_sa_req_svc );
	h_mcast->sa_reg_req.p_sa_req_svc = NULL;
#else	/* defined( CL_KERNEL ) */
	h_mcast->sa_reg_req.hdl = AL_INVALID_HANDLE;
#endif	/* defined( CL_KERNEL ) */
	cl_spinlock_release( &h_mcast->obj.lock );

	/* Initialize the user's response. */
	cl_memclr( &mcast_rec, sizeof( ib_mcast_rec_t ) );
	mcast_rec.mcast_context = h_mcast->obj.context;
	status = h_mcast->req_status;
	mcast_rec.error_status = h_mcast->resp_status;
	mcast_rec.p_member_rec = &h_mcast->member_rec;

	/* If a synchronous join fails, the blocking thread needs to do cleanup. */
	sync = ((h_mcast->flags & IB_FLAGS_SYNC) == IB_FLAGS_SYNC);

	/* See if the join operation was successful. */
	if( status == IB_SUCCESS )
	{
		/* Ensure that the user wants the join operation to proceed. */
		if( h_mcast->state == SA_REG_STARTING )
		{
			/*
			 * Change the state here so that we avoid trying to cancel
			 * the request if the verb operation fails.
			 */
			h_mcast->state = SA_REG_ACTIVE;
			/* Attach the QP to the multicast group. */
			if(ib_member_get_state(mcast_rec.p_member_rec->scope_state) == IB_MC_REC_STATE_FULL_MEMBER &&
				(((ib_qp_handle_t)h_mcast->obj.p_parent_obj) != NULL))
			{
				status = verbs_attach_mcast(h_mcast);
				if( status != IB_SUCCESS )
					AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_MCAST, ("attach_mcast failed\n") );
			}
			mcast_rec.h_mcast = h_mcast;
			
		}
		else
		{
			/*
			 * The operation was canceled as a result of destroying the QP.
			 * Invoke the user's callback notifying them that the join was
			 * canceled.  The join succeeded with the SA, but we don't
			 * attach the QP to the multicast group, so the user will not
			 * be aware that the join succeeded.
			 */
			CL_ASSERT( h_mcast->state == SA_REG_CANCELING );
			status = IB_CANCELED;
		}
	}

	mcast_rec.status = status;
	CL_ASSERT( h_mcast->pfn_mcast_cb );
#if defined( CL_KERNEL )
	CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);
#endif
	h_mcast->pfn_mcast_cb( &mcast_rec );
#if defined( CL_KERNEL )
	CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);
#endif

	/* If synchronous, signal that the join is done. */
	if( sync )
		cl_event_signal( &h_mcast->event );

	/* Dereference the mcast object now that the SA operation is complete. */
	if( status != IB_SUCCESS )
		h_mcast->obj.pfn_destroy( &h_mcast->obj, NULL );
	else
		deref_al_obj( &h_mcast->obj );

	AL_EXIT( AL_DBG_MCAST );
}



ib_api_status_t
ib_leave_mcast(
	IN		const	ib_mcast_handle_t			h_mcast,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{

	AL_ENTER( AL_DBG_MCAST );

	if( !h_mcast )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("IB_INVALID_MCAST_HANDLE\n") );
		return IB_INVALID_MCAST_HANDLE;
	}

	/* Record that we're already leaving the multicast group. */
	ref_al_obj( &h_mcast->obj );
	h_mcast->obj.pfn_destroy( &h_mcast->obj, pfn_destroy_cb );
	AL_EXIT( AL_DBG_MCAST );
	return IB_SUCCESS;
}


/*
 * Multicast leave completion callback.
 */
static void
leave_req_cb(
	IN				al_sa_req_t					*p_sa_req,
	IN				ib_mad_element_t			*p_mad_response )
{
	ib_mcast_handle_t		h_mcast;

	AL_ENTER( AL_DBG_MCAST );
	h_mcast = PARENT_STRUCT( p_sa_req, ib_mcast_t, sa_dereg_req );

	if( p_mad_response )
		ib_put_mad( p_mad_response );

	/*
	 * Release the reference on the mcast object now that
	 * the SA operation is complete.
	 */
	deref_al_obj( &h_mcast->obj );
	AL_EXIT( AL_DBG_MCAST );
}



#if defined( CL_KERNEL )

/*
 * Called by proxy to attach a QP to a multicast group.
 */
ib_api_status_t
al_attach_mcast(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_gid_t					*p_mcast_gid,
	IN		const	ib_net16_t					mcast_lid,
		OUT			al_attach_handle_t			*ph_attach,
	IN	OUT			ci_umv_buf_t				*p_umv_buf OPTIONAL )
{
	al_attach_handle_t		h_attach;
	ib_api_status_t			status;

	CL_ASSERT( h_qp );
	CL_ASSERT( ph_attach );

	/* Allocate a attachment object. */
	h_attach = (al_attach_handle_t)cl_zalloc( sizeof( al_attach_t ) );
	if( !h_attach )
	{
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the attachment object. */
	construct_al_obj( &h_attach->obj, AL_OBJ_TYPE_H_ATTACH );

	status = init_al_obj( &h_attach->obj, NULL, FALSE,
		NULL, __cleanup_attach, __free_attach );
	if( status != IB_SUCCESS )
	{
		__free_attach( &h_attach->obj );
		return status;
	}
	status = attach_al_obj( &h_qp->obj, &h_attach->obj );
	if( status != IB_SUCCESS )
	{
		h_attach->obj.pfn_destroy( &h_attach->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Attach the QP. */
	status = h_qp->obj.p_ci_ca->verbs.attach_mcast( h_qp->h_ci_qp,
		p_mcast_gid, mcast_lid, &h_attach->h_ci_mcast, p_umv_buf );
	if( status != IB_SUCCESS )
	{
		h_attach->obj.pfn_destroy( &h_attach->obj, NULL );
		return status;
	}

	/* The proxy will release the reference taken in init_al_obj. */
	*ph_attach = h_attach;
	return status;
}



static void
__cleanup_attach(
	IN				al_obj_t					*p_obj )
{
	ib_api_status_t			status;
	al_attach_handle_t		h_attach;

	CL_ASSERT( p_obj );
	h_attach = PARENT_STRUCT( p_obj, al_attach_t, obj );

	if( h_attach->h_ci_mcast )
	{
		status = h_attach->obj.p_ci_ca->verbs.detach_mcast(
			h_attach->h_ci_mcast );
		CL_ASSERT( status == IB_SUCCESS );
	}
}


static void
__free_attach(
	IN				al_obj_t					*p_obj )
{
	al_attach_handle_t		h_attach;

	CL_ASSERT( p_obj );
	h_attach = PARENT_STRUCT( p_obj, al_attach_t, obj );

	destroy_al_obj( p_obj );
	cl_free( h_attach );
}

#endif	/* CL_KERNEL */
