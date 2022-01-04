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


#include <complib/comp_lib.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>
#include "al.h"
#include "al_mr.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_proxy.tmh"
#endif
#include "al_dev.h"
#include "al_ci_ca.h"
#include "al_mgr.h"
#include "al_pnp.h"
#include "al_proxy.h"
#include "ib_common.h"



/*
 * Acquire an object used to queue callbacks.
 */
al_proxy_cb_info_t*
proxy_cb_get(
	IN		al_dev_open_context_t	*p_context )
{
	al_proxy_cb_info_t		*p_cb_info;

	if( !p_context )
		return NULL;

	cl_spinlock_acquire( &p_context->cb_pool_lock );
	p_cb_info = (al_proxy_cb_info_t*)cl_qpool_get( &p_context->cb_pool );
	cl_spinlock_release( &p_context->cb_pool_lock );

	if( p_cb_info )
		p_cb_info->p_context = p_context;

	return p_cb_info;
}



/*
 * Release an object used to report callbacks.
 */
void
proxy_cb_put(
	IN		al_proxy_cb_info_t		*p_cb_info )
{
	al_dev_open_context_t	*p_context;

	if( !p_cb_info )
		return;

	p_context = p_cb_info->p_context;

	p_cb_info->reported = FALSE;
	p_cb_info->p_al_obj = NULL;

	cl_spinlock_acquire( &p_context->cb_pool_lock );
	cl_qpool_put( &p_context->cb_pool, &p_cb_info->pool_item );
	cl_spinlock_release( &p_context->cb_pool_lock );
}



/*
 * Process the ioctl UAL_REG_SHMID:
 */
static
cl_status_t
proxy_reg_shmid(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_reg_shmid_ioctl_t	*p_ioctl =
			(ual_reg_shmid_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
							(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_mr_handle_t			h_mr;
	uint64_t				vaddr;
	net32_t					lkey, rkey;

	AL_ENTER( AL_DBG_DEV );

	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(ual_reg_shmid_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) < sizeof(ual_reg_shmid_ioctl_t) )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		cl_memclr( &p_ioctl->out, sizeof(p_ioctl->out) );
		p_ioctl->out.status = IB_INVALID_PD_HANDLE;
		goto done;
	}

	/* Validate input region size. */
	if( p_ioctl->in.mr_create.length > ~((size_t)0) )
	{
		cl_memclr( &p_ioctl->out, sizeof(p_ioctl->out) );
		p_ioctl->out.status = IB_INVALID_SETTING;
		goto done;
	}

	p_ioctl->out.status = reg_shmid(
							h_pd,
							p_ioctl->in.shmid,
							&p_ioctl->in.mr_create,
							&vaddr,
							&lkey,
							&rkey,
							&h_mr );

	if( p_ioctl->out.status == IB_SUCCESS )
	{
		/* We put the kernel al handle itself in the al_list for the process */
		p_ioctl->out.vaddr = vaddr;
		p_ioctl->out.lkey = lkey;
		p_ioctl->out.rkey = rkey;
		p_ioctl->out.h_mr = h_mr->obj.hdl;
		h_mr->obj.hdl_valid = TRUE;
		deref_al_obj( &h_mr->obj );
	}
	else
	{
		/* release the memory handle allocated */
		p_ioctl->out.vaddr = 0;
		p_ioctl->out.lkey = 0;
		p_ioctl->out.rkey = 0;
		p_ioctl->out.h_mr = AL_INVALID_HANDLE;
	}

done:
	*p_ret_bytes = sizeof(p_ioctl->out);
	AL_EXIT( AL_DBG_DEV );
	return CL_SUCCESS;
}


/*
 * Retrieve a callback record from the appropriate callback list
 * and fill the ioctl buffer.
 *
 * If no callback record is available, queue the ioctl buffer.
 * Queued ioctl buffer will put the calling process to sleep and complete
 * when complete when a callback record is available.
 */
static cl_status_t
proxy_queue_ioctl_buf(
	IN				uintn_t						cb_type,
	IN				al_dev_open_context_t		*p_context,
	IN				cl_ioctl_handle_t			h_ioctl )
{
	cl_qlist_t					*p_cb_list;
	al_proxy_cb_info_t			*p_cb_info;
	al_csq_t					*p_al_csq = &p_context->al_csq;
	uintn_t						ioctl_size;

	AL_ENTER( AL_DBG_DEV );

	/* Set up the appropriate callback list. */
	switch( cb_type )
	{
	case UAL_GET_COMP_CB_INFO:
		p_cb_list = &p_context->comp_cb_list;
		/* TODO: Use output size only. */
		ioctl_size = sizeof( comp_cb_ioctl_info_t );
		break;

	case UAL_GET_MISC_CB_INFO:
		p_cb_list = &p_context->misc_cb_list;
		/* TODO: Use output size only. */
		ioctl_size = sizeof( misc_cb_ioctl_info_t );
		break;

	default:
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	/* Process queued callbacks. */
	cl_spinlock_acquire( &p_context->cb_lock );
	while( !cl_is_qlist_empty( p_cb_list ) )
	{
		p_cb_info = (al_proxy_cb_info_t*)cl_qlist_head( p_cb_list );

		/* Check to see if we've already reported the callback. */
		if( !p_cb_info->reported )
		{
			p_cb_info->reported = TRUE;

			/* Return the callback to the user. */
			CL_ASSERT( cl_ioctl_out_size( h_ioctl ) >= ioctl_size );
			cl_memcpy(
				cl_ioctl_out_buf( h_ioctl ), &p_cb_info->cb_type, ioctl_size );
			cl_ioctl_complete( h_ioctl, CL_SUCCESS, ioctl_size );
			cl_spinlock_release( &p_context->cb_lock );
			AL_EXIT( AL_DBG_DEV );
			return CL_COMPLETED;
		}
		if( p_cb_info->p_al_obj )
			deref_al_obj( p_cb_info->p_al_obj );

		cl_qlist_remove_head( p_cb_list );
		proxy_cb_put( p_cb_info );
	}

	/* There are no callbacks to report.  Mark this IOCTL as pending. */

	/* If we're closing down, complete the IOCTL with a canceled status. */
	if( p_context->closing )
	{
		cl_spinlock_release( &p_context->cb_lock );
		AL_EXIT( AL_DBG_DEV );
		return CL_CANCELED;
	}

	/* put ioctl on the cancel-safe queue (it makes it pending) */
	IoCsqInsertIrp( (PIO_CSQ)p_al_csq, h_ioctl, NULL );

	/* Ref the context until the IOCTL is either completed or cancelled. */
	proxy_context_ref( p_context );
	cl_spinlock_release( &p_context->cb_lock );

	AL_EXIT( AL_DBG_DEV );
	return CL_PENDING;
}



/*
 * Process the ioctl UAL_GET_COMP_CB_INFO:
 * Get a completion callback record from the queue of CM callback records
 */
static cl_status_t
proxy_get_comp_cb(
	IN		cl_ioctl_handle_t		h_ioctl )
{
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	al_dev_open_context_t	*p_context;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = (al_dev_open_context_t*)p_io_stack->FileObject->FsContext;
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_H_CQ )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	/* Check the size of the ioctl */
	if( !p_context || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(comp_cb_ioctl_info_t) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("No output buffer, or buffer too small.\n") );
		return CL_INVALID_PARAMETER;
	}

	cl_status = proxy_queue_ioctl_buf( UAL_GET_COMP_CB_INFO,
		p_context, h_ioctl );

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}



/*
 * Process the ioctl UAL_GET_MISC_CB_INFO:
 * Get a miscellaneous callback record from the queue of CM callback records
 */
static cl_status_t
proxy_get_misc_cb(
	IN		cl_ioctl_handle_t		h_ioctl )
{
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	al_dev_open_context_t	*p_context;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = (al_dev_open_context_t*)p_io_stack->FileObject->FsContext;
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_AL_MGR )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	/* Check the size of the ioctl */
	if( !p_context || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(misc_cb_ioctl_info_t) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("No output buffer, or buffer too small.\n") );
		return CL_INVALID_PARAMETER;
	}

	cl_status = proxy_queue_ioctl_buf( UAL_GET_MISC_CB_INFO,
		p_context, h_ioctl );

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}



/*
 * Process a PnP callback for a CA.
 */
ib_api_status_t
proxy_pnp_ca_cb(
	IN		ib_pnp_rec_t		*p_pnp_rec	)
{
	misc_cb_ioctl_info_t	misc_cb_info;
	misc_cb_ioctl_rec_t		*p_misc_rec = &misc_cb_info.ioctl_rec;
	al_dev_open_context_t	*p_context;

	AL_ENTER( AL_DBG_PROXY_CB );

	p_context = p_pnp_rec->pnp_context;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return IB_ERROR;
	}

	/* Initialize the PnP callback information to return to user-mode. */
	cl_memclr( &misc_cb_info, sizeof(misc_cb_info) );
	misc_cb_info.rec_type = PNP_REC;
	p_misc_rec->pnp_cb_ioctl_rec.pnp_event = p_pnp_rec->pnp_event;

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_CA_ADD:
	case IB_PNP_CA_REMOVE:
		/* Queue the add/remove pnp record */
		p_misc_rec->pnp_cb_ioctl_rec.pnp_info.ca.ca_guid = p_pnp_rec->guid;
		proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &misc_cb_info,
			NULL );
		break;

	default:
		/* We only handle CA adds and removals. */
		break;
	}

	proxy_context_deref( p_context );
	AL_EXIT( AL_DBG_PROXY_CB );
	return IB_SUCCESS;
}



/*
 * Process a PnP callback for a port.
 */
ib_api_status_t
proxy_pnp_port_cb(
	IN		ib_pnp_rec_t		*p_pnp_rec	)
{
	ib_pnp_port_rec_t		*p_port_rec;
	misc_cb_ioctl_info_t	misc_cb_info;
	misc_cb_ioctl_rec_t		*p_misc_rec = &misc_cb_info.ioctl_rec;
	al_dev_open_context_t	*p_context;

	AL_ENTER( AL_DBG_PROXY_CB );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	p_context = p_pnp_rec->pnp_context;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return IB_ERROR;
	}

	p_port_rec = (ib_pnp_port_rec_t*)p_pnp_rec;

	/* Initialize the PnP callback information to return to user-mode. */
	cl_memclr( &misc_cb_info, sizeof(misc_cb_info) );
	misc_cb_info.rec_type = PNP_REC;
	p_misc_rec->pnp_cb_ioctl_rec.pnp_event = p_pnp_rec->pnp_event;

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
	case IB_PNP_PORT_REMOVE:
		/* Port add/remove will be generated automatically by uAL. */
		break;

	case IB_PNP_REG_COMPLETE:
		/*
		 * Once our registration for ports is complete, report this to the
		 * user-mode library.  This indicates to the that the current
		 * system state has been reported.
		 */
		proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &misc_cb_info,
			NULL );
		break;

	default:
		p_misc_rec->pnp_cb_ioctl_rec.pnp_info.ca.ca_guid =
			p_port_rec->p_ca_attr->ca_guid;

		proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &misc_cb_info,
			NULL );
		break;
	}

	proxy_context_deref( p_context );
	AL_EXIT( AL_DBG_PROXY_CB );
	return IB_SUCCESS;
}



cl_status_t
proxy_get_ca_attr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	al_dev_open_context_t			*p_context;
	uint64_t						*ph_ca_attr;
	ib_ca_attr_t					*p_src;

	AL_ENTER( AL_DBG_DEV );

	UNREFERENCED_PARAMETER( p_ret_bytes );

	/* Check the size of the ioctl */
	if( !cl_ioctl_in_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(uint64_t) )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("invalid buffer size\n") );
		return CL_INVALID_PARAMETER;
	}
	p_context = (al_dev_open_context_t*)p_open_context;
	ph_ca_attr = (uint64_t*)cl_ioctl_in_buf( h_ioctl );

	p_src = (ib_ca_attr_t*)al_hdl_get(
		p_context->h_al, *ph_ca_attr, AL_OBJ_TYPE_H_CA_ATTR );
	if( !p_src )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("invalid attr handle\n") );
		return CL_INVALID_PARAMETER;
	}

	cl_free(p_src);

	AL_EXIT( AL_DBG_DEV );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_BIND_SA:
 * Get a completion callback record from the queue of CM callback records
 */
static cl_status_t
proxy_bind_file(
	IN				cl_ioctl_handle_t			h_ioctl,
	IN		const	uint32_t					type )
{
	NTSTATUS				status;
	IO_STACK_LOCATION		*p_io_stack;
	al_dev_open_context_t	*p_context;
	ual_bind_file_ioctl_t	*p_ioctl;
	FILE_OBJECT				*p_file_obj;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = (al_dev_open_context_t*)p_io_stack->FileObject->FsContext;

	/* Check the size of the ioctl */
	if( !p_context ||
		!cl_ioctl_in_buf( h_ioctl ) || cl_ioctl_out_size( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(ual_bind_file_ioctl_t) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("No input buffer, or buffer too small.\n") );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );

	status = ObReferenceObjectByHandle( p_ioctl->h_file,
		READ_CONTROL, *IoFileObjectType, h_ioctl->RequestorMode,
		&p_file_obj, NULL );
	if( !NT_SUCCESS(status) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ObReferenceObjectByHandle returned 0x%08X\n", status) );
		return CL_INVALID_PARAMETER;
	}

	p_file_obj->FsContext = p_context;
	p_file_obj->FsContext2 = (void*)(ULONG_PTR)type;

	ObDereferenceObject( p_file_obj );

	AL_EXIT( AL_DBG_DEV );
	return CL_SUCCESS;
}



cl_status_t
proxy_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_DEV );

	UNUSED_PARAM( p_ret_bytes );

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_GET_MISC_CB_INFO:
		cl_status = proxy_get_misc_cb( h_ioctl );
		break;
	case UAL_GET_COMP_CB_INFO:
		cl_status = proxy_get_comp_cb( h_ioctl );
		break;
	case UAL_BIND:
		cl_status = al_dev_open( h_ioctl );
		break;
	case UAL_BIND_SA:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_SA_REQ_SVC );
		break;
	case UAL_BIND_DESTROY:
	case UAL_BIND_PNP:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_PNP_MGR );
		break;
	case UAL_BIND_CM:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_CM );
		break;
	case UAL_BIND_CQ:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_H_CQ );
		break;
	case UAL_BIND_MISC:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_AL_MGR );
		break;
	case UAL_BIND_ND:
		cl_status = proxy_bind_file( h_ioctl, AL_OBJ_TYPE_NDI );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}


static ib_api_status_t
__proxy_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec )
{
	proxy_pnp_evt_t				*p_evt;
	uint32_t					rec_size;
	proxy_pnp_recs_t			*p_evt_rec, *p_rec;
	IRP							*p_irp;
	IO_STACK_LOCATION			*p_io_stack;
	ual_rearm_pnp_ioctl_out_t	*p_ioctl;
	al_dev_open_context_t		*p_context;
	uint64_t					hdl;
	cl_status_t					cl_status;
	ib_api_status_t				ret_status;

	AL_ENTER( AL_DBG_PNP );

	p_rec = (proxy_pnp_recs_t*)p_pnp_rec;

	/*
	 * If an add event, return error to suppress all further
	 * events for this target.
	 */
	if( p_pnp_rec->pnp_event & IB_PNP_EVENT_ADD )
		ret_status = IB_ERROR;
	else
		ret_status = IB_SUCCESS;

	p_context = p_pnp_rec->pnp_context;
	ASSERT( p_context );

	/* Must take and release mutex to synchronize with registration. */
	cl_mutex_acquire( &p_context->pnp_mutex );
	cl_mutex_release( &p_context->pnp_mutex );

	p_irp = InterlockedExchangePointer( &p_pnp_rec->h_pnp->p_rearm_irp, NULL );
	if( !p_irp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("No rearm IRP queued for PnP event.\n") );
		return ret_status;
	}

	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	p_context = p_io_stack->FileObject->FsContext;
	ASSERT( p_context );
#pragma warning(push, 3)
	IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
	switch( pnp_get_class( p_pnp_rec->pnp_event ) )
	{
	case IB_PNP_CA:
		if( p_pnp_rec->pnp_event == IB_PNP_CA_REMOVE )
			rec_size = sizeof(ib_pnp_ca_rec_t);
		else
			rec_size = sizeof(ib_pnp_ca_rec_t) + p_rec->ca.p_ca_attr->size;
		break;
	case IB_PNP_PORT:
		if( p_pnp_rec->pnp_event == IB_PNP_PORT_REMOVE )
			rec_size = sizeof(ib_pnp_port_rec_t);
		else
			rec_size = sizeof(ib_pnp_port_rec_t) + p_rec->port.p_ca_attr->size;
		break;
	case IB_PNP_IOU:
		rec_size = sizeof(ib_pnp_iou_rec_t);
		break;
	case IB_PNP_IOC:
		switch( p_pnp_rec->pnp_event )
		{
		case IB_PNP_IOC_PATH_ADD:
		case IB_PNP_IOC_PATH_REMOVE:
			rec_size = sizeof( ib_pnp_ioc_path_rec_t);
			break;
		default:
			rec_size = sizeof( ib_pnp_ioc_rec_t ) + (sizeof(ib_svc_entry_t) *
				(p_rec->ioc.info.profile.num_svc_entries - 1));
		}
		break;
	default:
		/* The REG_COMPLETE event is not associated with any class. */
		rec_size = sizeof( ib_pnp_rec_t );
		break;
	}

	p_evt = cl_zalloc( rec_size + sizeof(proxy_pnp_evt_t) );
	if( !p_evt )
		return ret_status;

	/* Note that cl_event_init cannot fail in kernel-mode. */
	cl_event_init( &p_evt->event, FALSE );

	p_evt->rec_size = rec_size;

	p_evt_rec = (proxy_pnp_recs_t*)(p_evt + 1);

	/* Copy the PnP event data. */
	switch( pnp_get_class( p_pnp_rec->pnp_event ) )
	{
	case IB_PNP_CA:
		cl_memcpy( p_evt_rec, p_pnp_rec, sizeof(ib_pnp_ca_rec_t) );
		if( p_pnp_rec->pnp_event == IB_PNP_CA_REMOVE )
		{
			p_evt_rec->ca.p_ca_attr = NULL;
		}
		else
		{
			p_evt_rec->ca.p_ca_attr = (ib_ca_attr_t*)(&p_evt_rec->ca + 1);
			ib_copy_ca_attr( p_evt_rec->ca.p_ca_attr, p_rec->ca.p_ca_attr );
		}
		break;
	case IB_PNP_PORT:
		cl_memcpy( p_evt_rec, p_pnp_rec, sizeof(ib_pnp_port_rec_t) );
		if( p_pnp_rec->pnp_event == IB_PNP_PORT_REMOVE )
		{
			p_evt_rec->port.p_ca_attr = NULL;
			p_evt_rec->port.p_port_attr = NULL;
		}
		else
		{
			p_evt_rec->port.p_ca_attr = (ib_ca_attr_t*)(&p_evt_rec->port + 1);
			ib_copy_ca_attr(
				p_evt_rec->port.p_ca_attr, p_rec->port.p_ca_attr );
			p_evt_rec->port.p_port_attr = &p_evt_rec->port.p_ca_attr->
				p_port_attr[p_rec->port.p_port_attr->port_num - 1];
		}
		break;
	case IB_PNP_IOU:
		cl_memcpy( p_evt_rec, p_pnp_rec, sizeof(ib_pnp_iou_rec_t) );
		break;
	case IB_PNP_IOC:
		switch( p_pnp_rec->pnp_event )
		{
		case IB_PNP_IOC_PATH_ADD:
		case IB_PNP_IOC_PATH_REMOVE:
			cl_memcpy( p_evt_rec, p_pnp_rec, sizeof(ib_pnp_ioc_path_rec_t) );
			break;
		default:
			cl_memcpy( p_evt_rec, p_pnp_rec, sizeof(ib_pnp_ioc_rec_t) );
		}
		break;
	default:
		p_evt_rec->pnp = *p_pnp_rec;
		break;
	}

	p_evt_rec->pnp.h_pnp_padding = p_pnp_rec->h_pnp->obj.hdl;
	p_pnp_rec->h_pnp->obj.hdl_valid = TRUE;

	hdl =
		al_hdl_lock_insert( p_context->h_al, p_evt, AL_OBJ_TYPE_H_PNP_EVENT );
	if( hdl == AL_INVALID_HANDLE )
	{
		cl_free( p_evt );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to insert PnP event in handle map.\n") );
		return ret_status;
	}

	p_ioctl = cl_ioctl_out_buf( p_irp );
	p_ioctl->evt_hdl = hdl;
	p_ioctl->evt_size = rec_size;

	/* Hold callback lock to synchronize with registration. */
	cl_spinlock_acquire( &p_context->cb_lock );
	p_irp->IoStatus.Status = STATUS_SUCCESS;
	p_irp->IoStatus.Information = sizeof(ual_rearm_pnp_ioctl_out_t);
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );
	cl_spinlock_release( &p_context->cb_lock );

	/* Now wait on the event. */
	cl_status = cl_event_wait_on( &p_evt->event, PROXY_PNP_TIMEOUT_US, FALSE );
	if( cl_status == CL_SUCCESS )
	{
		/* Update the event context with the user's requested value. */
		p_pnp_rec->context = p_evt->evt_context;
		/* Forward the user's status. */
		ret_status = p_evt->evt_status;
	}
	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	al_hdl_free( p_context->h_al, hdl );
	cl_spinlock_release( &p_context->h_al->obj.lock );
	cl_event_destroy( &p_evt->event );
	cl_free( p_evt );

	AL_EXIT( AL_DBG_PNP );
	return ret_status;
}

#ifdef NTDDI_WIN8
static DRIVER_CANCEL __cancel_rearm_pnp;
#endif
static void
__cancel_rearm_pnp(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				IRP*						p_irp )
{
	al_dev_open_context_t	*p_context;
	PIO_STACK_LOCATION		p_io_stack;
	uint64_t				hdl;
	al_pnp_t				*h_pnp;

	AL_ENTER( AL_DBG_DEV );

	UNUSED_PARAM( p_dev_obj );

	/* Get the stack location. */
	p_io_stack = IoGetCurrentIrpStackLocation( p_irp );

	p_context = (al_dev_open_context_t *)p_io_stack->FileObject->FsContext;
	ASSERT( p_context );

	hdl = (size_t)InterlockedExchangePointer(
		&p_irp->Tail.Overlay.DriverContext[0], NULL );
	if( hdl != AL_INVALID_HANDLE )
	{
		h_pnp = (al_pnp_t*)
			al_hdl_ref( p_context->h_al, hdl, AL_OBJ_TYPE_H_PNP );
		if( h_pnp )
		{
			if( InterlockedExchangePointer( &h_pnp->p_rearm_irp, NULL ) ==
				p_irp )
			{
#pragma warning(push, 3)
				IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
				/* Complete the IRP. */
				p_irp->IoStatus.Status = STATUS_CANCELLED;
				p_irp->IoStatus.Information = 0;
				IoCompleteRequest( p_irp, IO_NO_INCREMENT );
			}
			deref_al_obj( &h_pnp->obj );
		}
	}

	IoReleaseCancelSpinLock( p_irp->CancelIrql );
}


/*
 * Process the ioctl UAL_REG_PNP:
 */
#pragma prefast(suppress: 28167, "The irql level is restored here")
static cl_status_t proxy_reg_pnp(
	IN				void						*p_open_context,
	IN				cl_ioctl_handle_t			h_ioctl )
{
	ual_reg_pnp_ioctl_in_t	*p_ioctl;
	al_dev_open_context_t	*p_context;
	IO_STACK_LOCATION		*p_io_stack;
	ib_pnp_req_t			pnp_req;
	ib_api_status_t			status, *p_user_status;
	uint64_t				*p_user_hdl;
	ib_pnp_handle_t			h_pnp;
	cl_status_t				cl_status;
	KEVENT					*p_sync_event;
	NTSTATUS				nt_status;

	AL_ENTER( AL_DBG_PNP );

	p_context = p_open_context;

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_PNP_MGR )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	if( cl_ioctl_in_size( h_ioctl ) < sizeof(ual_reg_pnp_ioctl_in_t) ||
		cl_ioctl_out_size( h_ioctl ) < sizeof(ual_rearm_pnp_ioctl_out_t) )
	{
		AL_EXIT( AL_DBG_PNP );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );

	pnp_req.pnp_class = p_ioctl->pnp_class;
	pnp_req.pnp_context = p_open_context;
	pnp_req.pfn_pnp_cb = __proxy_pnp_cb;

	p_user_status = (ib_api_status_t*)(ULONG_PTR)p_ioctl->p_status;
	p_user_hdl = (uint64_t*)(ULONG_PTR)p_ioctl->p_hdl;

	if( pnp_get_flag( p_ioctl->pnp_class ) & IB_PNP_FLAG_REG_SYNC )
	{
		nt_status = ObReferenceObjectByHandle( p_ioctl->sync_event,
			STANDARD_RIGHTS_ALL, *ExEventObjectType, h_ioctl->RequestorMode,
			(PVOID*)&p_sync_event, NULL );
		if( !NT_SUCCESS( nt_status ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid sync event handle\n") );
			return CL_INVALID_PARAMETER;
		}
	}
	else
	{
		p_sync_event = NULL;
	}

	cl_mutex_acquire( &p_context->pnp_mutex );
	status = al_reg_pnp( p_context->h_al, &pnp_req, p_sync_event, &h_pnp );
	if( status == IB_SUCCESS )
	{
		CL_ASSERT( h_pnp );
		h_pnp->p_rearm_irp = h_ioctl;

		h_ioctl->Tail.Overlay.DriverContext[0] = (void*)(size_t)h_pnp->obj.hdl;
#pragma warning(push, 3)
		IoSetCancelRoutine( h_ioctl, __cancel_rearm_pnp );
#pragma warning(pop)
		IoMarkIrpPending( h_ioctl );

		cl_copy_to_user( p_user_hdl, &h_pnp->obj.hdl, sizeof(uint64_t) );

		/* Mark the registration as a user-mode one. */
		h_pnp->obj.type |= AL_OBJ_SUBTYPE_UM_EXPORT;
		h_pnp->obj.hdl_valid = TRUE;
		deref_ctx_al_obj( &h_pnp->obj, E_REF_INIT );

		cl_status = CL_PENDING;
	}
	else
	{
		cl_status = CL_INVALID_PARAMETER;
	}

	cl_copy_to_user( p_user_status, &status, sizeof(ib_api_status_t) );
	cl_mutex_release( &p_context->pnp_mutex );

	AL_EXIT( AL_DBG_PNP );
	return cl_status;
}


/*
 * Process the ioctl UAL_REG_PNP:
 */
static cl_status_t
proxy_poll_pnp(
	IN				void						*p_open_context,
	IN				cl_ioctl_handle_t			h_ioctl,
		OUT			size_t						*p_ret_bytes )
{
	ual_poll_pnp_ioctl_t	*p_ioctl;
	al_dev_open_context_t	*p_context;
	proxy_pnp_evt_t			*p_evt;

	AL_ENTER( AL_DBG_PNP );

	p_context = p_open_context;

	if( cl_ioctl_in_size( h_ioctl ) < sizeof(uint64_t) ||
		cl_ioctl_out_size( h_ioctl ) < sizeof(ib_pnp_rec_t) )
	{
		AL_EXIT( AL_DBG_PNP );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );
	CL_ASSERT( cl_ioctl_in_buf( h_ioctl ) == cl_ioctl_out_buf( h_ioctl ) );

	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	p_evt = al_hdl_chk(
		p_context->h_al, p_ioctl->in.evt_hdl, AL_OBJ_TYPE_H_PNP_EVENT );
	if( p_evt )
	{
		if( cl_ioctl_out_size( h_ioctl ) < p_evt->rec_size )
		{
			cl_spinlock_release( &p_context->h_al->obj.lock );
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Buffer too small!\n") );
			return CL_INVALID_PARAMETER;
		}

		cl_memcpy( &p_ioctl->out.pnp_rec, p_evt + 1, p_evt->rec_size );
		*p_ret_bytes = p_evt->rec_size;
	}
	cl_spinlock_release( &p_context->h_al->obj.lock );

	AL_EXIT( AL_DBG_PNP );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_REG_PNP:
 */
static cl_status_t
proxy_rearm_pnp(
	IN				void						*p_open_context,
	IN				cl_ioctl_handle_t			h_ioctl )
{
	ual_rearm_pnp_ioctl_in_t	*p_ioctl;
	al_dev_open_context_t		*p_context;
	IO_STACK_LOCATION			*p_io_stack;
	proxy_pnp_evt_t				*p_evt;
	ib_pnp_handle_t				h_pnp;
	IRP							*p_old_irp;

	AL_ENTER( AL_DBG_PNP );

	p_context = p_open_context;

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_PNP_MGR )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	if( cl_ioctl_in_size( h_ioctl ) != sizeof(ual_rearm_pnp_ioctl_in_t) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(ual_rearm_pnp_ioctl_out_t) )
	{
		AL_EXIT( AL_DBG_PNP );
		return CL_INVALID_PARAMETER;
	}

	p_ioctl = cl_ioctl_in_buf( h_ioctl );

	h_pnp = (al_pnp_t*)
		al_hdl_ref( p_context->h_al, p_ioctl->pnp_hdl, AL_OBJ_TYPE_H_PNP );
	if( !h_pnp )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_PNP,
			("Invalid PNP handle.\n") );
		return CL_INVALID_PARAMETER;
	}
#pragma warning(push, 3)
	IoSetCancelRoutine( h_ioctl, __cancel_rearm_pnp );
#pragma warning(pop)
	IoMarkIrpPending( h_ioctl );
	h_ioctl->Tail.Overlay.DriverContext[0] = (void*)(size_t)h_pnp->obj.hdl;

	/*
	 * Update the object context before signalling the event since that value
	 * is returned by the PnP callback.
	 */
	p_old_irp = InterlockedExchangePointer( &h_pnp->p_rearm_irp, h_ioctl );
	if( p_old_irp )
	{
#pragma warning(push, 3)
		IoSetCancelRoutine( p_old_irp, NULL );
#pragma warning(pop)
		/* Complete the IRP. */
		p_old_irp->IoStatus.Status = STATUS_CANCELLED;
		p_old_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_old_irp, IO_NO_INCREMENT );
	}

	cl_spinlock_acquire( &p_context->h_al->obj.lock );
	p_evt = al_hdl_chk(
		p_context->h_al, p_ioctl->last_evt_hdl, AL_OBJ_TYPE_H_PNP_EVENT );
	if( p_evt )
	{
		p_evt->evt_context = (void*)(ULONG_PTR)p_ioctl->last_evt_context;
		p_evt->evt_status = p_ioctl->last_evt_status;
		cl_event_signal( &p_evt->event );
	}
	cl_spinlock_release( &p_context->h_al->obj.lock );

	deref_al_obj( &h_pnp->obj );

	AL_EXIT( AL_DBG_PNP );
	return CL_PENDING;
}


/*
 * Process the ioctl UAL_DEREG_PNP:
 */
static cl_status_t
proxy_dereg_pnp(
	IN				void						*p_open_context,
	IN				cl_ioctl_handle_t			h_ioctl )
{
	uint64_t				*p_hdl;
	al_dev_open_context_t	*p_context;
	IO_STACK_LOCATION		*p_io_stack;
	ib_pnp_handle_t			h_pnp;

	AL_ENTER( AL_DBG_PNP );
	p_context = p_open_context;

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	if( (uintn_t)p_io_stack->FileObject->FsContext2 != AL_OBJ_TYPE_PNP_MGR )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid file object type for request: %p\n",
			p_io_stack->FileObject->FsContext2) );
		return CL_INVALID_PARAMETER;
	}

	if( cl_ioctl_in_size( h_ioctl ) < sizeof(ual_dereg_pnp_ioctl_t) ||
		cl_ioctl_out_size( h_ioctl ) )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	p_hdl = cl_ioctl_in_buf( h_ioctl );

	h_pnp = (ib_pnp_handle_t)
		al_hdl_ref( p_context->h_al, *p_hdl, AL_OBJ_TYPE_H_PNP );
	if( !h_pnp )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	h_pnp->p_dereg_irp = h_ioctl;

	IoMarkIrpPending( h_ioctl );

	h_pnp->obj.pfn_destroy( &h_pnp->obj, NULL );

	AL_EXIT( AL_DBG_PNP );
	return CL_PENDING;
}



cl_status_t
al_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	void					*p_context;

	AL_ENTER( AL_DBG_DEV );

	CL_ASSERT( h_ioctl && p_ret_bytes );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = p_io_stack->FileObject->FsContext;

	if( !p_context )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_REG_SHMID:
		cl_status = proxy_reg_shmid( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_GET_CA_ATTR_INFO:
		cl_status = proxy_get_ca_attr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REG_PNP:
		cl_status = proxy_reg_pnp( p_context, h_ioctl );
		break;
	case UAL_POLL_PNP:
		cl_status = proxy_poll_pnp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REARM_PNP:
		cl_status = proxy_rearm_pnp( p_context, h_ioctl );
		break;
	case UAL_DEREG_PNP:
		cl_status = proxy_dereg_pnp( p_context, h_ioctl );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}


NTSTATUS
ib_to_ntstatus(
    IN  ib_api_status_t ib_status )
{
    switch( ib_status )
    {
    case IB_SUCCESS:
        return STATUS_SUCCESS;
	case IB_INSUFFICIENT_RESOURCES:
	case IB_MAX_MCAST_QPS_REACHED:
        return STATUS_INSUFFICIENT_RESOURCES;
	case IB_INSUFFICIENT_MEMORY:
        return STATUS_NO_MEMORY;
	case IB_INVALID_PARAMETER:
	case IB_INVALID_SETTING:
	case IB_INVALID_PKEY:
	case IB_INVALID_LKEY:
	case IB_INVALID_RKEY:
	case IB_INVALID_MAX_WRS:
	case IB_INVALID_MAX_SGE:
	case IB_INVALID_CQ_SIZE:
	case IB_INVALID_SRQ_SIZE:
	case IB_INVALID_SERVICE_TYPE:
	case IB_INVALID_GID:
	case IB_INVALID_LID:
	case IB_INVALID_GUID:
	case IB_INVALID_WR_TYPE:
	case IB_INVALID_PORT:
	case IB_INVALID_INDEX:
        return STATUS_INVALID_PARAMETER;
	case IB_NO_MATCH:
	case IB_NOT_FOUND:
        return STATUS_NOT_FOUND;
	case IB_TIMEOUT:
        return STATUS_TIMEOUT;
	case IB_CANCELED:
        return STATUS_CANCELLED;
	case IB_INTERRUPTED:
	case IB_NOT_DONE:
        return STATUS_ABANDONED;
	case IB_INVALID_PERMISSION:
        return STATUS_ACCESS_DENIED;
	case IB_UNSUPPORTED:
	case IB_QP_IN_TIMEWAIT:
	case IB_EE_IN_TIMEWAIT:
        return STATUS_INVALID_DEVICE_REQUEST;
	case IB_OVERFLOW:
        return STATUS_BUFFER_OVERFLOW;
	case IB_INVALID_QP_STATE:
		return STATUS_CONNECTION_INVALID;
	case IB_INVALID_APM_STATE:
	case IB_INVALID_PORT_STATE:
	case IB_INVALID_STATE:
        return STATUS_INVALID_DEVICE_STATE;
	case IB_RESOURCE_BUSY:
        return STATUS_DEVICE_BUSY;
	case IB_INVALID_CA_HANDLE:
	case IB_INVALID_AV_HANDLE:
	case IB_INVALID_CQ_HANDLE:
	case IB_INVALID_QP_HANDLE:
	case IB_INVALID_SRQ_HANDLE:
	case IB_INVALID_PD_HANDLE:
	case IB_INVALID_MR_HANDLE:
	case IB_INVALID_FMR_HANDLE:
	case IB_INVALID_MW_HANDLE:
	case IB_INVALID_MCAST_HANDLE:
	case IB_INVALID_CALLBACK:
	case IB_INVALID_AL_HANDLE:
	case IB_INVALID_HANDLE:
        return STATUS_INVALID_HANDLE;
	case IB_VERBS_PROCESSING_DONE:
        return STATUS_EVENT_DONE;
	case IB_PENDING:
        return STATUS_PENDING;
	case IB_ERROR:
	case IB_REMOTE_ERROR:
    default:
        return STATUS_UNSUCCESSFUL;
    }
}
