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
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_proxy_verbs.tmh"
#endif

#include "al_dev.h"
/* Get the internal definitions of apis for the proxy */
#include "al_ca.h"
#include "al_pd.h"
#include "al_qp.h"
#include "al_srq.h"
#include "al_cq.h"
#include "al_mr.h"
#include "al_mw.h"
#include "al_av.h"
#include "al_ci_ca.h"
#include "al_mgr.h"
#include "ib_common.h"
#include "al_proxy.h"


extern al_mgr_t				*gp_al_mgr;


/*
 *
 * Utility function to:
 * a. allocate an umv_buf and p_buf in kernel space
 * b. copy umv_buf and the contents of p_buf from user-mode
 *
 * It is assumed that the p_buf does not have any embedded user-mode pointers
 */

ib_api_status_t
cpyin_umvbuf(
	IN		ci_umv_buf_t	*p_src,
		OUT	ci_umv_buf_t	**pp_dst )
{
	size_t			umv_buf_size;
	ci_umv_buf_t	*p_dest;

	/* Allocate space for umv_buf */
	CL_ASSERT( pp_dst );

	umv_buf_size = sizeof(ci_umv_buf_t);
	umv_buf_size += MAX(p_src->input_size, p_src->output_size);

	if( p_src->p_inout_buf )
	{
		if( p_src->input_size && cl_check_for_read(
			(void*)(ULONG_PTR)p_src->p_inout_buf, (size_t)p_src->input_size )
			!= CL_SUCCESS )
		{
			/* user-supplied memory area not readable */
			return IB_INVALID_PERMISSION;
		}
		if( p_src->output_size && cl_check_for_write(
			(void*)(ULONG_PTR)p_src->p_inout_buf, (size_t)p_src->output_size )
			!= CL_SUCCESS )
		{
			/* user-supplied memory area not writeable */
			return IB_INVALID_PERMISSION;
		}
	}
	p_dest = (ci_umv_buf_t*)cl_zalloc( (size_t)umv_buf_size );
	if( !p_dest )
		return IB_INSUFFICIENT_MEMORY;

	/* Copy the umv_buf structure. */
	*p_dest = *p_src;
	if( p_src->p_inout_buf )
		p_dest->p_inout_buf = (ULONG_PTR)(p_dest + 1);

	/* Setup the buffer - either we have an input or output buffer */
	if( p_src->input_size )
	{
		if( cl_copy_from_user( (void*)(ULONG_PTR)p_dest->p_inout_buf,
			(void*)(ULONG_PTR)p_src->p_inout_buf,
			(size_t)p_src->input_size ) != CL_SUCCESS )
		{
			cl_free( p_dest );
			return IB_INVALID_PERMISSION;
		}
	}
	*pp_dst = p_dest;
	return IB_SUCCESS;
}



/*
 *
 * Utility function to copy the results of umv_buf and the contents
 * of p_buf to umv_buf in user-space.
 *
 * It is assumed that the p_buf does not have any embedded user-mode pointers
 *
 * This function can NOT be called from asynchronous callbacks where
 * user process context may not be valid
 *
 */
ib_api_status_t
cpyout_umvbuf(
	IN		ci_umv_buf_t	*p_dest,
	IN		ci_umv_buf_t	*p_src)
{
	CL_ASSERT( p_dest );

	if( p_src )
	{
		CL_ASSERT( p_dest->command == p_src->command );
		CL_ASSERT( p_dest->input_size == p_src->input_size );
		/* Copy output buf only on success. */
		if( p_src->status == IB_SUCCESS )
		{
			uint32_t	out_size;

			out_size = MIN( p_dest->output_size, p_src->output_size );

			if( cl_copy_to_user(
				(void*)(ULONG_PTR)p_dest->p_inout_buf,
				(void*)(ULONG_PTR)p_src->p_inout_buf,
				out_size ) != CL_SUCCESS )
			{
				p_dest->output_size = 0;
				return IB_INVALID_PERMISSION;
			}
			p_dest->status = p_src->status;
			p_dest->output_size = out_size;
		}
	}
	return IB_SUCCESS;
}


void
free_umvbuf(
	IN				ci_umv_buf_t				*p_umv_buf )
{
	if( p_umv_buf )
		cl_free( p_umv_buf );
}



/*
 * Process the ioctl UAL_GET_VENDOR_LIBCFG:
 */
static cl_status_t
proxy_get_vendor_libcfg(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_get_uvp_name_ioctl_t	*p_ioctl =
			(ual_get_uvp_name_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	al_ci_ca_t					*p_ci_ca;

	AL_ENTER( AL_DBG_CA );

	UNUSED_PARAM( p_open_context );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	/* Find the CAguid */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CA,
			("CA guid %I64x.\n", p_ioctl->in.ca_guid) );

	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	p_ci_ca = find_ci_ca( p_ioctl->in.ca_guid );

	if( !p_ci_ca )
	{
		cl_spinlock_release( &gp_al_mgr->obj.lock );
		p_ioctl->out.status = IB_NOT_FOUND;
	}
	else
	{
		/* found the ca guid, copy the user-mode verbs provider libname */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CA,
				("CA guid %I64x. libname (%s)\n",
				p_ioctl->in.ca_guid, p_ci_ca->verbs.libname) );
		cl_memcpy( p_ioctl->out.uvp_lib_name, p_ci_ca->verbs.libname,
			sizeof(p_ci_ca->verbs.libname));
		cl_spinlock_release( &gp_al_mgr->obj.lock );
		p_ioctl->out.status = IB_SUCCESS;
	}
	*p_ret_bytes = sizeof(p_ioctl->out);
	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}


/*
 * Allocate an ioctl buffer of appropriate size
 * Copy the given ioctl buffer
 * Queue the ioctl buffer as needed
 */
boolean_t
proxy_queue_cb_buf(
	IN		uintn_t					cb_type,
	IN		al_dev_open_context_t	*p_context,
	IN		void					*p_cb_data,
	IN		al_obj_t				*p_al_obj		OPTIONAL )
{
	cl_qlist_t					*p_cb_list;
	al_proxy_cb_info_t			*p_cb_info;
	cl_ioctl_handle_t			h_ioctl;
	al_csq_t					*p_al_csq = &p_context->al_csq;
	uintn_t						ioctl_size;

	AL_ENTER( AL_DBG_DEV );
	
	/* Set up the appropriate callback list. */
	switch( cb_type )
	{
	case UAL_GET_COMP_CB_INFO:
		p_cb_list = &p_context->comp_cb_list;
		ioctl_size = sizeof( comp_cb_ioctl_info_t );
		break;

	case UAL_GET_MISC_CB_INFO:
		p_cb_list = &p_context->misc_cb_list;
		ioctl_size = sizeof( misc_cb_ioctl_info_t );
		break;

	default:
		return FALSE;
	}

	/* Get a callback record to queue the callback. */
	p_cb_info = proxy_cb_get( p_context );
	if( !p_cb_info )
		return FALSE;

	cl_memcpy( &p_cb_info->cb_type, p_cb_data, ioctl_size );

	/*
	 * If an AL object was specified, we need to reference it to prevent its
	 * destruction until the callback has been fully specified.
	 */
	if( p_al_obj )
	{
		p_cb_info->p_al_obj = p_al_obj;
		ref_al_obj( p_al_obj );
	}

	/* Insert the callback record into the callback list */
	cl_spinlock_acquire( &p_context->cb_lock );
	cl_qlist_insert_tail( p_cb_list, &p_cb_info->pool_item.list_item );

	/* remove ioctl from the cancel-safe queue (it makes it unpending) */
	h_ioctl = IoCsqRemoveNextIrp( (PIO_CSQ)p_al_csq, (PVOID)(ULONG_PTR)cb_type );
	if (h_ioctl) 
	{
		p_cb_info->reported = TRUE;
		/* Complete the IOCTL to return the callback information. */
		CL_ASSERT( cl_ioctl_out_size( h_ioctl ) >= ioctl_size );
		cl_memcpy( cl_ioctl_out_buf( h_ioctl ), p_cb_data, ioctl_size );
		cl_ioctl_complete( h_ioctl, CL_SUCCESS, ioctl_size );
		proxy_context_deref( p_context );
	}
	cl_spinlock_release( &p_context->cb_lock );

	AL_EXIT( AL_DBG_DEV );
	return TRUE;
}


/*
 * Proxy's ca error callback
 * The context in the error record is proxy's ca context
 * Context is the a list object in the CA list
 */
static void
proxy_ca_err_cb(
	IN ib_async_event_rec_t	*p_err_rec)
{
	ib_ca_handle_t			h_ca = p_err_rec->handle.h_ca;
	al_dev_open_context_t	*p_context = h_ca->obj.h_al->p_context;
	misc_cb_ioctl_info_t	cb_info;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return;
	}

	/* Set up context and callback record type appropriate for UAL */
	cb_info.rec_type = CA_ERROR_REC;
	/* Return the Proxy's open_ca handle and the user's context */
	cb_info.ioctl_rec.event_rec = *p_err_rec;
	cb_info.ioctl_rec.event_rec.handle.h_ca_padding = h_ca->obj.hdl;

	/* The proxy handle must be valid now. */
	if( !h_ca->obj.hdl_valid )
		h_ca->obj.hdl_valid = TRUE;

	proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &cb_info,
		&h_ca->obj );

	proxy_context_deref( p_context );
}


/*
 * Process the ioctl UAL_OPEN_CA:
 *
 * Returns the ca_list_obj as the handle to UAL
 */
static cl_status_t
proxy_open_ca(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_open_ca_ioctl_t		*p_ioctl =
		(ual_open_ca_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CA );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases. */
	*p_ret_bytes = sizeof(p_ioctl->out);

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_open_ca_err;

	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("proxy_open_ca: open  CA\n"));
	status = open_ca( p_context->h_al, p_ioctl->in.guid, proxy_ca_err_cb,
		(void*)(ULONG_PTR)p_ioctl->in.context, UserMode, &h_ca,
		p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_open_ca_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_ca = h_ca->obj.hdl;
		h_ca->obj.hdl_valid = TRUE;
		/* Release the reference taken in init_al_obj */
		deref_ctx_al_obj( &h_ca->obj, E_REF_INIT );
	}
	else
	{
		h_ca->obj.pfn_destroy( &h_ca->obj, NULL );

proxy_open_ca_err:	/* getting a handle failed. */
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_ca = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	p_ioctl->out.status = status;

	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_QUERY_CA:
 */
static cl_status_t
proxy_query_ca(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_ca_ioctl_t	*p_ioctl =
			(ual_query_ca_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
							(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ib_ca_attr_t			*p_ca_attr = NULL;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	uint32_t				byte_cnt = 0;

	AL_ENTER( AL_DBG_CA );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		status = IB_INVALID_CA_HANDLE;
		goto proxy_query_ca_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_ca_err;

	byte_cnt = p_ioctl->in.byte_cnt;
	if( p_ioctl->in.p_ca_attr && byte_cnt )
	{
		p_ca_attr = (ib_ca_attr_t*)cl_zalloc( byte_cnt );
		if( !p_ca_attr )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto proxy_query_ca_err;
		}
	}
	status = query_ca( h_ca, p_ca_attr, &byte_cnt, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_query_ca_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_query_ca_err;

	/* copy CA attribute back to user */
	if( p_ca_attr )
	{
		__try
		{
			ProbeForWrite( (void*)(ULONG_PTR)p_ioctl->in.p_ca_attr,
				byte_cnt, sizeof(void*) );
			ib_copy_ca_attr( (void*)(ULONG_PTR)p_ioctl->in.p_ca_attr,
				p_ca_attr );
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Failed to copy CA attributes to user buffer %016I64x\n",
				p_ioctl->in.p_ca_attr) );
			status = IB_INVALID_PERMISSION;
		}
	}

	/* Free the ca_attr buffer allocated locally */
	if( status != IB_SUCCESS )
	{
proxy_query_ca_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
	}
	if( p_ca_attr )
		cl_free( p_ca_attr );

	free_umvbuf( p_umv_buf );

	if( h_ca )
		deref_al_obj( &h_ca->obj );

	p_ioctl->out.status = status;
	p_ioctl->out.byte_cnt = byte_cnt;

	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_MODIFY_CA:
 */
static
cl_status_t
proxy_modify_ca(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_modify_ca_ioctl_t	*p_ioctl =
			(ual_modify_ca_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
							(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;

	AL_ENTER( AL_DBG_CA );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		p_ioctl->out.status = IB_INVALID_CA_HANDLE;
		AL_EXIT( AL_DBG_CA );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = ib_modify_ca( h_ca, p_ioctl->in.port_num,
		p_ioctl->in.ca_mod, &p_ioctl->in.port_attr_mod );

	deref_al_obj( &h_ca->obj );

	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_CLOSE_CA:
 */
static
cl_status_t
proxy_close_ca(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_close_ca_ioctl_t	*p_ioctl =
			(ual_close_ca_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
							(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;

	AL_ENTER( AL_DBG_CA );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		p_ioctl->out.status = IB_INVALID_CA_HANDLE;
		AL_EXIT( AL_DBG_CA );
		return CL_SUCCESS;
	}

	/*
	 * Note that we hold a reference on the CA, so we need to
	 * call close_ca, not ib_close_ca.  We also don't release the reference
	 * since close_ca will do so (by destroying the object).
	 */
	AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("proxy_close_ca: close  CA\n"));
	h_ca->obj.pfn_destroy( &h_ca->obj, ib_sync_destroy );
	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}


/*
 * Validates the proxy handles and converts them to AL handles
 */
static ib_api_status_t
__convert_to_al_handles(
	IN				al_dev_open_context_t*	const	p_context,
	IN				uint64_t*				const	um_handle_array,
	IN				uint32_t						num_handles,
		OUT			void**					const	p_handle_array )
{
	uint32_t				i;

	for( i = 0; i < num_handles; i++ )
	{
		/* Validate the handle in the resource map */
		p_handle_array[i] = al_hdl_ref(
			p_context->h_al, um_handle_array[i], AL_OBJ_TYPE_UNKNOWN );
		if( !p_handle_array[i] )
		{
			/* Release references taken so far. */
			while( i-- )
				deref_al_obj( p_handle_array[i] );

			/* Could not find the handle in the map */
			return IB_INVALID_HANDLE;
		}
	}

	return IB_SUCCESS;
}



static
cl_status_t
proxy_ci_call(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_ci_call_ioctl_t	*p_ioctl =
		(ual_ci_call_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ci_umv_buf_t			*p_umv_buf = NULL;
	void*					p_ci_op_buf = NULL;
	void*					p_ci_op_user_buf = NULL;
	void**					p_handle_array = NULL;
	size_t					ci_op_buf_size;
	ib_api_status_t			status;
	uint32_t				num_handles;

	AL_ENTER( AL_DBG_CA );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	num_handles = p_ioctl->in.num_handles;
	if( num_handles > 1 &&
		cl_ioctl_in_size( h_ioctl ) != (sizeof(uint64_t) * (num_handles - 1)) )
	{
		AL_EXIT( AL_DBG_CA );
		return CL_INVALID_PARAMETER;
	}

	ci_op_buf_size = (size_t)p_ioctl->in.ci_op.buf_size;

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		p_ioctl->out.status = IB_INVALID_CA_HANDLE;
		AL_EXIT( AL_DBG_CA );
		return CL_SUCCESS;
	}

	/* Save the user buffer address */
	p_ci_op_user_buf = p_ioctl->in.ci_op.p_buf;

	/* Validate the handle array */
	if( num_handles )
	{
		p_handle_array = cl_malloc( sizeof(void*) * num_handles );
		if( !p_handle_array )
		{
			p_ioctl->out.status = IB_INSUFFICIENT_MEMORY;
			deref_al_obj( &h_ca->obj );
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to allocate handle array.\n") );
			return CL_SUCCESS;
		}

		/*
		 * Now we have the handle array in kernel space. Replace
		 * the handles with the correct AL handles based on the
		 * type
		 */
		status = __convert_to_al_handles( p_context, p_ioctl->in.handle_array,
			num_handles, p_handle_array );
		if( status != IB_SUCCESS )
		{
			cl_free( p_handle_array );
			p_ioctl->out.status = status;
			deref_al_obj( &h_ca->obj );
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to convert handles.\n") );
			return CL_SUCCESS;
		}
	}

	/* Copy in the UMV buffer */
	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_ci_call_err;

	if( p_ioctl->in.ci_op.buf_size && p_ioctl->in.ci_op.p_buf )
	{
		p_ci_op_buf = cl_zalloc( ci_op_buf_size );
		if( !p_ci_op_buf )
		{
			status = IB_INSUFFICIENT_MEMORY;
			goto proxy_ci_call_err;
		}

		/* Copy from user the buffer */
		if( cl_copy_from_user( p_ci_op_buf, p_ioctl->in.ci_op.p_buf,
			ci_op_buf_size ) != CL_SUCCESS )
		{
			status = IB_INVALID_PERMISSION;
			goto proxy_ci_call_err;
		}
		/* Update the buffer pointer to reference the kernel copy. */
		p_ioctl->in.ci_op.p_buf = p_ci_op_buf;
	}

	status = ci_call( h_ca, p_handle_array,
		num_handles, &p_ioctl->in.ci_op, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_ci_call_err;

	/* Copy the umv_buf back to user space */
	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
		status = IB_INVALID_PERMISSION;
		goto proxy_ci_call_err;
	}

	/*
	 * Copy the data buffer.  Copy the buf size so that if the
	 * num_bytes_ret is greater than the buffer size, we copy
	 * only what the buffer can hold
	 */
	if( cl_copy_to_user( p_ci_op_user_buf, p_ioctl->in.ci_op.p_buf,
		ci_op_buf_size ) != CL_SUCCESS )
	{
		status = IB_INVALID_PERMISSION;
	}

proxy_ci_call_err:

	/* Restore the data buffer */
	p_ioctl->out.ci_op.p_buf = p_ci_op_user_buf;
	p_ioctl->out.status = status;

	/* Release the resources allocated */
	if( p_handle_array )
	{
		while( num_handles-- )
			deref_al_obj( (al_obj_t*)p_handle_array[num_handles] );
		cl_free( p_handle_array );
	}
	if( p_ci_op_buf )
		cl_free( p_ci_op_buf );

	free_umvbuf( p_umv_buf );

	deref_al_obj( &h_ca->obj );

	AL_EXIT( AL_DBG_CA );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_ALLOC_PD:
 *
 * Returns the pd_list_obj as the handle to UAL
 */
static
cl_status_t
proxy_alloc_pd(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_alloc_pd_ioctl_t	*p_ioctl =
		(ual_alloc_pd_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ib_pd_handle_t			h_pd;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_PD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_PD );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		status = IB_INVALID_CA_HANDLE;
		goto proxy_alloc_pd_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_alloc_pd_err;

	status = alloc_pd( h_ca, p_ioctl->in.type,
		(void*)(ULONG_PTR)p_ioctl->in.context, &h_pd, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_alloc_pd_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_pd = h_pd->obj.hdl;
		h_pd->obj.hdl_valid = TRUE;
		deref_al_obj( &h_pd->obj );
	}
	else
	{
		h_pd->obj.pfn_destroy( &h_pd->obj, NULL );

proxy_alloc_pd_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_pd = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	if( h_ca )
		deref_al_obj( &h_ca->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_PD );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_DEALLOC_PD:
 */
static cl_status_t
proxy_dealloc_pd(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_dealloc_pd_ioctl_t	*p_ioctl =
		(ual_dealloc_pd_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;

	AL_ENTER( AL_DBG_PD );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_PD );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		p_ioctl->out.status = IB_INVALID_PD_HANDLE;
		AL_EXIT( AL_DBG_PD );
		return CL_SUCCESS;
	}

	h_pd->obj.pfn_destroy( &h_pd->obj, ib_sync_destroy );
	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_PD );
	return CL_SUCCESS;
}


/*
 * Proxy's SRQ error handler
 */
static void
proxy_srq_err_cb(
	IN ib_async_event_rec_t	*p_err_rec )
{
	ib_srq_handle_t	h_srq = p_err_rec->handle.h_srq;
	al_dev_open_context_t	*p_context = h_srq->obj.h_al->p_context;
	misc_cb_ioctl_info_t	cb_info;

	AL_ENTER( AL_DBG_QP );

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return;
	}

	/* Set up context and callback record type appropriate for UAL */
	cb_info.rec_type = SRQ_ERROR_REC;
	/* Return the Proxy's SRQ handle and the user's context */
	cb_info.ioctl_rec.event_rec = *p_err_rec;
	cb_info.ioctl_rec.event_rec.handle.h_srq_padding = h_srq->obj.hdl;

	/* The proxy handle must be valid now. */
	if( !h_srq->obj.hdl_valid )
		h_srq->obj.hdl_valid = TRUE;

	proxy_queue_cb_buf(
		UAL_GET_MISC_CB_INFO, p_context, &cb_info, &h_srq->obj );

	proxy_context_deref( p_context );

	AL_EXIT( AL_DBG_QP );
}

/*
 * Process the ioctl UAL_CREATE_SRQ
 *
 * Returns the srq_list_obj as the handle to UAL
 */
static cl_status_t
proxy_create_srq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_srq_ioctl_t	*p_ioctl =
		(ual_create_srq_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_srq_handle_t			h_srq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	ib_pfn_event_cb_t		pfn_ev;

	AL_ENTER( AL_DBG_SRQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_SRQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate handles. */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd)
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_create_srq_err1;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_srq_err1;

	if( p_ioctl->in.ev_notify )
		pfn_ev = proxy_srq_err_cb;
	else
		pfn_ev = NULL;

	status = create_srq( h_pd, &p_ioctl->in.srq_attr,
		(void*)(ULONG_PTR)p_ioctl->in.context, pfn_ev, &h_srq, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_srq_err1;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_srq = h_srq->obj.hdl;
		h_srq->obj.hdl_valid = TRUE;
		/* Release the reference taken in create_srq (by init_al_obj) */
		deref_al_obj( &h_srq->obj );
	}
	else
	{
proxy_create_srq_err1:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_srq = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	AL_EXIT( AL_DBG_SRQ );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_QUERY_SRQ:
 */
static
cl_status_t
proxy_query_srq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_srq_ioctl_t	*p_ioctl =
		(ual_query_srq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_srq_handle_t			h_srq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_SRQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate SRQ handle */
	h_srq = (ib_srq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_srq, AL_OBJ_TYPE_H_SRQ );
	if( !h_srq )
	{
		status = IB_INVALID_SRQ_HANDLE;
		goto proxy_query_srq_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_srq_err;

	status = query_srq( h_srq, &p_ioctl->out.srq_attr, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_srq_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_query_srq_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		cl_memclr( &p_ioctl->out.srq_attr, sizeof(ib_srq_attr_t) );
	}
	free_umvbuf( p_umv_buf );

	if( h_srq )
		deref_al_obj( &h_srq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_SRQ );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_MODIFY_SRQ:
 */
static
cl_status_t
proxy_modify_srq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_modify_srq_ioctl_t	*p_ioctl =
		(ual_modify_srq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_srq_handle_t			h_srq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_SRQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_SRQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate SRQ handle */
	h_srq = (ib_srq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_srq, AL_OBJ_TYPE_H_SRQ );
	if( !h_srq )
	{
		status = IB_INVALID_SRQ_HANDLE;
		goto proxy_modify_srq_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_modify_srq_err;

	status = modify_srq( h_srq, &p_ioctl->in.srq_attr, p_ioctl->in.srq_attr_mask, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_modify_srq_err;
	
	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_modify_srq_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
	}
	free_umvbuf( p_umv_buf );

	if( h_srq )
		deref_al_obj( &h_srq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_SRQ );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_DESTROY_SRQ
 */
static cl_status_t
proxy_destroy_srq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_destroy_srq_ioctl_t	*p_ioctl =
		(ual_destroy_srq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_srq_handle_t			h_srq;

	AL_ENTER( AL_DBG_SRQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_SRQ );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate SRQ handle */
	h_srq = (ib_srq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_srq, AL_OBJ_TYPE_H_SRQ );
	if( !h_srq )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_SRQ_HANDLE\n") );
		p_ioctl->out.status = IB_INVALID_SRQ_HANDLE;
	}
	else
	{
		h_srq->obj.pfn_destroy( &h_srq->obj, ib_sync_destroy );
		p_ioctl->out.status = IB_SUCCESS;
	}

	AL_EXIT( AL_DBG_SRQ );
	return CL_SUCCESS;
}


/*
 * Proxy's QP error handler
 */
static void
proxy_qp_err_cb(
	IN ib_async_event_rec_t	*p_err_rec )
{
	ib_qp_handle_t			h_qp = p_err_rec->handle.h_qp;
	al_dev_open_context_t	*p_context = h_qp->obj.h_al->p_context;
	misc_cb_ioctl_info_t	cb_info;

	AL_ENTER( AL_DBG_QP );

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return;
	}

	/* Set up context and callback record type appropriate for UAL */
	cb_info.rec_type = QP_ERROR_REC;
	/* Return the Proxy's QP handle and the user's context */
	cb_info.ioctl_rec.event_rec = *p_err_rec;
	cb_info.ioctl_rec.event_rec.handle.h_qp_padding = h_qp->obj.hdl;

	/* The proxy handle must be valid now. */
	if( !h_qp->obj.hdl_valid )
		h_qp->obj.hdl_valid = TRUE;

	proxy_queue_cb_buf(
		UAL_GET_MISC_CB_INFO, p_context, &cb_info, &h_qp->obj );

	proxy_context_deref( p_context );

	AL_EXIT( AL_DBG_QP );
}



/*
 * Process the ioctl UAL_CREATE_QP
 *
 * Returns the qp_list_obj as the handle to UAL
 */
static cl_status_t
proxy_create_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_qp_ioctl_t	*p_ioctl =
		(ual_create_qp_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_qp_handle_t			h_qp;
	ib_srq_handle_t			h_srq = NULL;
	ib_cq_handle_t			h_sq_cq, h_rq_cq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	ib_pfn_event_cb_t		pfn_ev;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Validate handles. */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	h_sq_cq = (ib_cq_handle_t)al_hdl_ref( p_context->h_al,
		p_ioctl->in.qp_create.h_sq_cq_padding, AL_OBJ_TYPE_H_CQ );
	h_rq_cq = (ib_cq_handle_t)al_hdl_ref( p_context->h_al,
		p_ioctl->in.qp_create.h_rq_cq_padding, AL_OBJ_TYPE_H_CQ );
	if (p_ioctl->in.qp_create.h_srq) {
		h_srq = (ib_srq_handle_t)al_hdl_ref( p_context->h_al,
			p_ioctl->in.qp_create.h_srq_padding, AL_OBJ_TYPE_H_SRQ );
		if( !h_srq)
		{
			status = IB_INVALID_SRQ_HANDLE;
			goto proxy_create_qp_err1;
		}
	}
	if( !h_pd)
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_create_qp_err1;
	}
	if( !h_sq_cq )
	{
		status = IB_INVALID_CQ_HANDLE;
		goto proxy_create_qp_err1;
	}
	if( !h_rq_cq )
	{
		status = IB_INVALID_CQ_HANDLE;
		goto proxy_create_qp_err1;
	}

	/* Substitute rq_cq handle with AL's cq handle */
	p_ioctl->in.qp_create.h_sq_cq = h_sq_cq;
	/* Substitute rq_cq handle with AL's cq handle */
	p_ioctl->in.qp_create.h_rq_cq = h_rq_cq;
	/* Substitute srq handle with AL's srq handle */
	p_ioctl->in.qp_create.h_srq = h_srq;

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_qp_err1;

	if( p_ioctl->in.ev_notify )
		pfn_ev = proxy_qp_err_cb;
	else
		pfn_ev = NULL;

	status = create_qp( h_pd, &p_ioctl->in.qp_create,
		(void*)(ULONG_PTR)p_ioctl->in.context, pfn_ev, &h_qp, p_umv_buf );
	/* TODO: The create_qp call should return the attributes... */
	if( status != IB_SUCCESS )
		goto proxy_create_qp_err1;

	status = query_qp( h_qp, &p_ioctl->out.attr, NULL );
	if( status != IB_SUCCESS )
		goto proxy_create_qp_err2;
	
	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_qp = h_qp->obj.hdl;
		h_qp->obj.hdl_valid = TRUE;
		/* Release the reference taken in create_qp (by init_al_obj) */
		deref_ctx_al_obj( &h_qp->obj, E_REF_INIT );
	}
	else
	{
proxy_create_qp_err2:
		/*
		 * Note that we hold the reference taken in create_qp (by init_al_obj)
		 */
		h_qp->obj.pfn_destroy( &h_qp->obj, NULL );

proxy_create_qp_err1:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_qp = AL_INVALID_HANDLE;
		cl_memclr( &p_ioctl->out.attr, sizeof(ib_qp_attr_t) );
	}
	free_umvbuf( p_umv_buf );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	if( h_pd )
		deref_al_obj( &h_pd->obj );
	if( h_rq_cq )
		deref_al_obj( &h_rq_cq->obj );
	if( h_sq_cq )
		deref_al_obj( &h_sq_cq->obj );
	if( h_srq )
		deref_al_obj( &h_srq->obj );

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_QUERY_QP:
 */
static
cl_status_t
proxy_query_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_qp_ioctl_t	*p_ioctl =
		(ual_query_qp_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_query_qp_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_qp_err;

	status = query_qp( h_qp, &p_ioctl->out.attr, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_qp_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		if( p_ioctl->out.attr.h_pd )
		{
			p_ioctl->out.attr.h_pd_padding = p_ioctl->out.attr.h_pd->obj.hdl;
		}

		if( p_ioctl->out.attr.h_sq_cq )
		{
			p_ioctl->out.attr.h_sq_cq_padding =
				p_ioctl->out.attr.h_sq_cq->obj.hdl;
		}

		if( p_ioctl->out.attr.h_rq_cq )
		{
			p_ioctl->out.attr.h_rq_cq_padding =
				p_ioctl->out.attr.h_rq_cq->obj.hdl;
		}

		if( p_ioctl->out.attr.h_srq )
		{
			p_ioctl->out.attr.h_srq_padding = p_ioctl->out.attr.h_srq->obj.hdl;
		}
	}
	else
	{
proxy_query_qp_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		cl_memclr( &p_ioctl->out.attr, sizeof(ib_qp_attr_t) );
	}
	free_umvbuf( p_umv_buf );

	if( h_qp )
		deref_al_obj( &h_qp->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_MODIFY_QP:
 */
static
cl_status_t
proxy_modify_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_modify_qp_ioctl_t	*p_ioctl =
		(ual_modify_qp_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_modify_qp_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_modify_qp_err;

	status = modify_qp( h_qp, &p_ioctl->in.modify_attr, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_modify_qp_err;
	
	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_modify_qp_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
	}
	free_umvbuf( p_umv_buf );

	if( h_qp )
		deref_al_obj( &h_qp->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_DESTROY_QP
 */
static cl_status_t
proxy_destroy_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_destroy_qp_ioctl_t	*p_ioctl =
		(ual_destroy_qp_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_QP_HANDLE\n") );
		p_ioctl->out.status = IB_INVALID_QP_HANDLE;
	}
	else
	{
		h_qp->obj.pfn_destroy( &h_qp->obj, ib_sync_destroy );
		p_ioctl->out.status = IB_SUCCESS;
	}

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_CREATE_AV:
 */
static
cl_status_t
proxy_create_av(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_av_ioctl_t	*p_ioctl =
		(ual_create_av_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_av_handle_t			h_av;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_AV );
		return CL_INVALID_PARAMETER;
	}

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_create_av_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_av_err;

	status = create_av( h_pd, &p_ioctl->in.attr, &h_av, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_create_av_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_av = h_av->obj.hdl;
		h_av->obj.hdl_valid = TRUE;
		/* Release the reference taken in create_av. */
		deref_al_obj( &h_av->obj );
	}
	else
	{
		h_av->obj.pfn_destroy( &h_av->obj, NULL );
#if DBG
		insert_pool_trace(&g_av_trace, h_av, POOL_PUT, 11);
#endif

proxy_create_av_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_av = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);
	
	AL_EXIT( AL_DBG_AV );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_QUERY_AV:
 */
static
cl_status_t
proxy_query_av(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_av_ioctl_t	*p_ioctl =
		(ual_query_av_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_av_handle_t			h_av;
	ib_pd_handle_t			h_pd;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_AV );
		return CL_INVALID_PARAMETER;
	}

	/* Validate AV handle */
	h_av = (ib_av_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_av, AL_OBJ_TYPE_H_AV );
	if( !h_av )
	{
		status = IB_INVALID_AV_HANDLE;
		goto proxy_query_av_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_av_err;

	status = query_av( h_av, &p_ioctl->out.attr, &h_pd, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_query_av_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_query_av_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		cl_memclr( &p_ioctl->out.attr, sizeof(ib_av_attr_t) );
	}
	free_umvbuf( p_umv_buf );

	if( h_av )
		deref_al_obj( &h_av->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_AV );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_MODIFY_AV:
 */
static
cl_status_t
proxy_modify_av(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_modify_av_ioctl_t	*p_ioctl =
		(ual_modify_av_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_av_handle_t			h_av;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_AV );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_AV );
		return CL_INVALID_PARAMETER;
	}

	/* Validate AV handle */
	h_av = (ib_av_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_av, AL_OBJ_TYPE_H_AV );
	if( !h_av )
	{
		status = IB_INVALID_AV_HANDLE;
		goto proxy_modify_av_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_modify_av_err;

	status = modify_av( h_av, &p_ioctl->in.attr, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_modify_av_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_modify_av_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
	}
	free_umvbuf( p_umv_buf );

	if( h_av )
		deref_al_obj( &h_av->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_AV );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_DESTROY_AV:
 */
static
cl_status_t
proxy_destroy_av(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_destroy_av_ioctl_t	*p_ioctl =
		(ual_destroy_av_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_av_handle_t			h_av;

	AL_ENTER( AL_DBG_AV );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_AV );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate AV handle */
	h_av = (ib_av_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_av, AL_OBJ_TYPE_H_AV );
	if( !h_av )
	{
		p_ioctl->out.status = IB_INVALID_AV_HANDLE;
		AL_EXIT( AL_DBG_AV );
		return CL_SUCCESS;
	}

	h_av->obj.pfn_destroy( &h_av->obj, NULL );
#if DBG
	insert_pool_trace(&g_av_trace, h_av, POOL_PUT, 12);
#endif
	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_AV );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_MODIFY_CQ:
 */
static
cl_status_t
proxy_modify_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_modify_cq_ioctl_t	*p_ioctl =
		(ual_modify_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	uint32_t				size;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		status = IB_INVALID_CQ_HANDLE;
		goto proxy_modify_cq_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_modify_cq_err;

	size = p_ioctl->in.size;
	status = modify_cq( h_cq, &size, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_modify_cq_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.size = size;
	}
	else
	{
proxy_modify_cq_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.size = 0;
	}
	free_umvbuf( p_umv_buf );

	if( h_cq )
		deref_al_obj( &h_cq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}



/*
 * Proxy's CQ completion callback
 */
static void
proxy_cq_comp_cb(
	IN				ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	comp_cb_ioctl_info_t	cb_info;
	al_dev_open_context_t	*p_context = h_cq->obj.h_al->p_context;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return;
	}

	/* Set up context and callback record type appropriate for UAL */
	cb_info.cq_context = (ULONG_PTR)cq_context;

	/* The proxy handle must be valid now. */
	if( !h_cq->obj.hdl_valid )
		h_cq->obj.hdl_valid = TRUE;

	proxy_queue_cb_buf( UAL_GET_COMP_CB_INFO, p_context, &cb_info,
		&h_cq->obj );
	
	proxy_context_deref( p_context );
}



/*
 * Proxy's CQ error callback
 */
static void
proxy_cq_err_cb(
	IN				ib_async_event_rec_t		*p_err_rec)
{
	ib_cq_handle_t			h_cq = p_err_rec->handle.h_cq;
	al_dev_open_context_t	*p_context = h_cq->obj.h_al->p_context;
	misc_cb_ioctl_info_t	cb_info;

	/*
	 * If we're already closing the device - do not queue a callback, since
	 * we're cleaning up the callback lists.
	 */
	if( !proxy_context_ref( p_context ) )
	{
		proxy_context_deref( p_context );
		return;
	}

	/* Set up context and callback record type appropriate for UAL */
	cb_info.rec_type = CQ_ERROR_REC;
	/* Return the Proxy's cq handle and the user's context */
	cb_info.ioctl_rec.event_rec = *p_err_rec;
	cb_info.ioctl_rec.event_rec.handle.h_cq_padding = h_cq->obj.hdl;

	/* The proxy handle must be valid now. */
	if( !h_cq->obj.hdl_valid )
		h_cq->obj.hdl_valid = TRUE;

	proxy_queue_cb_buf( UAL_GET_MISC_CB_INFO, p_context, &cb_info,
		&h_cq->obj );
	proxy_context_deref( p_context );
}



/*
 * Process the ioctl UAL_CREATE_CQ:
 */
static cl_status_t
proxy_create_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_cq_ioctl_t	*p_ioctl =
		(ual_create_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_ca_handle_t			h_ca;
	ib_cq_handle_t			h_cq;
	ib_cq_create_t			cq_create;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	ib_pfn_event_cb_t		pfn_ev;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CA handle */
	h_ca = (ib_ca_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_ca, AL_OBJ_TYPE_H_CA );
	if( !h_ca )
	{
		status = IB_INVALID_CA_HANDLE;
		goto proxy_create_cq_err1;
	}

	cq_create.size = p_ioctl->in.size;

	if( p_ioctl->in.h_wait_obj )
	{
		cq_create.pfn_comp_cb = NULL;
		cq_create.h_wait_obj = cl_waitobj_ref( p_ioctl->in.h_wait_obj );
		if( !cq_create.h_wait_obj )
		{
			status = IB_INVALID_PARAMETER;
			goto proxy_create_cq_err1;
		}
	}
	else
	{
		/* Override with proxy's cq callback */
		cq_create.pfn_comp_cb = proxy_cq_comp_cb;
		cq_create.h_wait_obj = NULL;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_cq_err2;

	if( p_ioctl->in.ev_notify )
		pfn_ev = proxy_cq_err_cb;
	else
		pfn_ev = NULL;

	status = create_cq( h_ca, &cq_create,
		(void*)(ULONG_PTR)p_ioctl->in.context, pfn_ev, &h_cq, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_create_cq_err2;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.size = cq_create.size;
		p_ioctl->out.h_cq = h_cq->obj.hdl;
		h_cq->obj.hdl_valid = TRUE;
		deref_ctx_al_obj( &h_cq->obj, E_REF_INIT );
	}
	else
	{
		h_cq->obj.pfn_destroy( &h_cq->obj, NULL );

proxy_create_cq_err2:
		if( cq_create.h_wait_obj )
			cl_waitobj_deref( cq_create.h_wait_obj );

proxy_create_cq_err1:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_cq = AL_INVALID_HANDLE;
		p_ioctl->out.size = 0;
	}
	free_umvbuf( p_umv_buf );

	if( h_ca )
		deref_al_obj( &h_ca->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_QUERY_CQ:
 */
static
cl_status_t
proxy_query_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_cq_ioctl_t	*p_ioctl =
		(ual_query_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		status = IB_INVALID_CQ_HANDLE;
		goto proxy_query_cq_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_cq_err;

	status = query_cq( h_cq, &p_ioctl->out.size, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_query_cq_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_query_cq_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.size = 0;
	}
	free_umvbuf( p_umv_buf );

	if( h_cq )
		deref_al_obj( &h_cq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_DESTROY_CQ
 */
static
cl_status_t
proxy_destroy_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_destroy_cq_ioctl_t	*p_ioctl =
		(ual_destroy_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;
	cl_waitobj_handle_t		h_wait_obj;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		p_ioctl->out.status = IB_INVALID_CQ_HANDLE;
		AL_EXIT( AL_DBG_CQ );
		return CL_SUCCESS;
	}

	h_wait_obj = h_cq->h_wait_obj;

	h_cq->obj.pfn_destroy( &h_cq->obj, ib_sync_destroy );

	/* Deref the wait object, if any. */
	if( h_wait_obj )
		cl_waitobj_deref( h_wait_obj );

	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_POST_SEND
 */
static
cl_status_t
proxy_post_send(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_post_send_ioctl_t	*p_ioctl =
		(ual_post_send_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;
	ib_av_handle_t			h_av;
	ib_send_wr_t			*p_wr;
	ib_send_wr_t			*p_send_failure;
	uintn_t					i = 0;
	ib_local_ds_t			*p_ds;
	uintn_t					num_ds = 0;
	ib_api_status_t			status;
	size_t					in_buf_sz;

	AL_ENTER( AL_DBG_QP );
	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/*
	 * Additional input buffer validation based on actual settings.
	 * Note that this validates that work requests are actually
	 * being passed in.
	 */
	in_buf_sz = sizeof(p_ioctl->in);
	in_buf_sz += sizeof(ib_send_wr_t) * (p_ioctl->in.num_wr - 1);
	in_buf_sz += sizeof(ib_local_ds_t) * p_ioctl->in.num_ds;
	if( cl_ioctl_in_size( h_ioctl ) != in_buf_sz )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Setup p_send_failure to head of list. */
	p_send_failure = p_wr = p_ioctl->in.send_wr;

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_post_send_done;
	}

	/* Setup the base data segment pointer. */
	p_ds = (ib_local_ds_t*)&p_ioctl->in.send_wr[p_ioctl->in.num_wr];

	/* Setup the user's work requests and data segments and translate. */
	for( i = 0; i < p_ioctl->in.num_wr; i++ )
	{
		if( h_qp->type == IB_QPT_UNRELIABLE_DGRM )
		{
			/* Validate the AV handle for UD */
			h_av = (ib_av_handle_t)al_hdl_ref( p_context->h_al,
				(ULONG_PTR) p_wr[i].dgrm.ud.h_av, AL_OBJ_TYPE_H_AV );
			if( !h_av )
			{
				status = IB_INVALID_AV_HANDLE;
				goto proxy_post_send_done;
			}
			/* substitute with KAL AV handle */
			p_wr[i].dgrm.ud.h_av = h_av;
		}

		/* Setup the data segments, if any. */
		if( p_wr[i].num_ds )
		{
			num_ds += p_wr[i].num_ds;
			if( num_ds > p_ioctl->in.num_ds )
			{
				/*
				* The work request submitted exceed the number of data
				* segments specified in the IOCTL.
				*/
				status = IB_INVALID_PARAMETER;
				goto proxy_post_send_done;
			}
			p_wr[i].ds_array = p_ds;
			p_ds += p_wr->num_ds;
		}
		else
		{
			p_wr[i].ds_array = NULL;
		}

		p_wr[i].p_next = &p_wr[i + 1];
	}

	/* Mark the end of list. */
	p_wr[i - 1].p_next = NULL;

	/* so much for the set up, let's roll! */
	status = ib_post_send( h_qp, p_wr, &p_send_failure );

	if( status == IB_SUCCESS )
	{
		p_ioctl->out.failed_cnt = 0;
	}
	else
	{
proxy_post_send_done:
		/* First set up as if all failed. */
		p_ioctl->out.failed_cnt = p_ioctl->in.num_wr;
		/* Now subtract successful ones. */
		p_ioctl->out.failed_cnt -= (uint32_t)(
			(((uintn_t)p_send_failure) - ((uintn_t)p_wr))
			/ sizeof(ib_send_wr_t));
	}

	/* releases the references on address vectors. */
	if( h_qp )
	{
		if( h_qp->type == IB_QPT_UNRELIABLE_DGRM )
		{
			while( i-- )
				deref_al_obj( &p_wr[i].dgrm.ud.h_av->obj );
		}
		deref_al_obj( &h_qp->obj );
	}

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_POST_RECV
 */
static
cl_status_t
proxy_post_recv(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_post_recv_ioctl_t	*p_ioctl =
		(ual_post_recv_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t			h_qp;
	ib_recv_wr_t			*p_wr;
	ib_recv_wr_t			*p_recv_failure;
	uintn_t					i;
	ib_local_ds_t			*p_ds;
	uintn_t					num_ds = 0;
	ib_api_status_t			status;
	size_t					in_buf_sz;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/*
	 * Additional input buffer validation based on actual settings.
	 * Note that this validates that work requests are actually
	 * being passed in.
	 */
	in_buf_sz = sizeof(p_ioctl->in);
	in_buf_sz += sizeof(ib_recv_wr_t) * (p_ioctl->in.num_wr - 1);
	in_buf_sz += sizeof(ib_local_ds_t) * p_ioctl->in.num_ds;
	if( cl_ioctl_in_size( h_ioctl ) != in_buf_sz )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Setup p_send_failure to head of list. */
	p_recv_failure = p_wr = p_ioctl->in.recv_wr;

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_post_recv_done;
	}

	/* Setup the base data segment pointer. */
	p_ds = (ib_local_ds_t*)&p_ioctl->in.recv_wr[p_ioctl->in.num_wr];

	/* Setup the user's work requests and data segments and translate. */
	for( i = 0; i < p_ioctl->in.num_wr; i++ )
	{
		/* Setup the data segments, if any. */
		if( p_wr[i].num_ds )
		{
			num_ds += p_wr[i].num_ds;
			if( num_ds > p_ioctl->in.num_ds )
			{
				/*
				* The work request submitted exceed the number of data
				* segments specified in the IOCTL.
				*/
				status = IB_INVALID_PARAMETER;
				goto proxy_post_recv_done;
			}
			p_wr[i].ds_array = p_ds;
			p_ds += p_wr->num_ds;
		}
		else
		{
			p_wr[i].ds_array = NULL;
		}

		p_wr[i].p_next = &p_wr[i + 1];
	}

	/* Mark the end of list. */
	p_wr[i-1].p_next = NULL;

	status = ib_post_recv( h_qp, p_wr, &p_recv_failure );

	if( status == IB_SUCCESS )
	{
		p_ioctl->out.failed_cnt = 0;
	}
	else
	{
proxy_post_recv_done:
		/* First set up as if all failed. */
		p_ioctl->out.failed_cnt = p_ioctl->in.num_wr;
		/* Now subtract successful ones. */
		p_ioctl->out.failed_cnt -= (uint32_t)(
			(((uintn_t)p_recv_failure) - ((uintn_t)p_wr))
			/ sizeof(ib_recv_wr_t));
	}

	if( h_qp )
		deref_al_obj( &h_qp->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_POST_SRQ_RECV
 */
static
cl_status_t
proxy_post_srq_recv(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_post_srq_recv_ioctl_t	*p_ioctl =
		(ual_post_srq_recv_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_srq_handle_t			h_srq;
	ib_recv_wr_t			*p_wr;
	ib_recv_wr_t			*p_recv_failure;
	uintn_t					i;
	ib_local_ds_t			*p_ds;
	uintn_t					num_ds = 0;
	ib_api_status_t			status;
	size_t					in_buf_sz;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) < sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/*
	 * Additional input buffer validation based on actual settings.
	 * Note that this validates that work requests are actually
	 * being passed in.
	 */
	in_buf_sz = sizeof(p_ioctl->in);
	in_buf_sz += sizeof(ib_recv_wr_t) * (p_ioctl->in.num_wr - 1);
	in_buf_sz += sizeof(ib_local_ds_t) * p_ioctl->in.num_ds;
	if( cl_ioctl_in_size( h_ioctl ) != in_buf_sz )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Setup p_send_failure to head of list. */
	p_recv_failure = p_wr = p_ioctl->in.recv_wr;

	/* Validate SRQ handle */
	h_srq = (ib_srq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_srq, AL_OBJ_TYPE_H_QP );
	if( !h_srq )
	{
		status = IB_INVALID_SRQ_HANDLE;
		goto proxy_post_recv_done;
	}

	/* Setup the base data segment pointer. */
	p_ds = (ib_local_ds_t*)&p_ioctl->in.recv_wr[p_ioctl->in.num_wr];

	/* Setup the user's work requests and data segments and translate. */
	for( i = 0; i < p_ioctl->in.num_wr; i++ )
	{
		/* Setup the data segments, if any. */
		if( p_wr[i].num_ds )
		{
			num_ds += p_wr[i].num_ds;
			if( num_ds > p_ioctl->in.num_ds )
			{
				/*
				* The work request submitted exceed the number of data
				* segments specified in the IOCTL.
				*/
				status = IB_INVALID_PARAMETER;
				goto proxy_post_recv_done;
			}
			p_wr[i].ds_array = p_ds;
			p_ds += p_wr->num_ds;
		}
		else
		{
			p_wr[i].ds_array = NULL;
		}

		p_wr[i].p_next = &p_wr[i + 1];
	}

	/* Mark the end of list. */
	p_wr[i-1].p_next = NULL;

	status = ib_post_srq_recv( h_srq, p_wr, &p_recv_failure );

	if( status == IB_SUCCESS )
	{
		p_ioctl->out.failed_cnt = 0;
	}
	else
	{
proxy_post_recv_done:
		/* First set up as if all failed. */
		p_ioctl->out.failed_cnt = p_ioctl->in.num_wr;
		/* Now subtract successful ones. */
		p_ioctl->out.failed_cnt -= (uint32_t)(
			(((uintn_t)p_recv_failure) - ((uintn_t)p_wr))
			/ sizeof(ib_recv_wr_t));
	}

	if( h_srq )
		deref_al_obj( &h_srq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}


/*
 * Process the ioctl UAL_PEEK_CQ
 */
static cl_status_t
proxy_peek_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_peek_cq_ioctl_t		*p_ioctl =
		(ual_peek_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		p_ioctl->out.status = IB_INVALID_CQ_HANDLE;
		p_ioctl->out.n_cqes = 0;
		AL_EXIT( AL_DBG_CQ );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = ib_peek_cq( h_cq, &p_ioctl->out.n_cqes );

	deref_al_obj( &h_cq->obj );

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_POLL_CQ
 */
static cl_status_t
proxy_poll_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_poll_cq_ioctl_t		*p_ioctl;
	al_dev_open_context_t	*p_context;
	ib_cq_handle_t			h_cq;
	ib_wc_t					*p_free_wc;
	ib_wc_t					*p_done_wc = NULL;
	uint32_t				i, num_wc;
	size_t					out_buf_sz;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_CQ );

	p_ioctl = (ual_poll_cq_ioctl_t*)cl_ioctl_in_buf( h_ioctl );
	p_context = (al_dev_open_context_t*)p_open_context;

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) < sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/*
	 * Additional validation of input and output sizes.
	 * Note that this also checks that work completions are actually
	 * being passed in.
	 */
	out_buf_sz = sizeof(p_ioctl->out);
	out_buf_sz += sizeof(ib_wc_t) * (p_ioctl->in.num_wc - 1);
	if( cl_ioctl_out_size( h_ioctl ) != out_buf_sz )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Validate CQ handle. */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		status = IB_INVALID_CQ_HANDLE;
		goto proxy_poll_cq_err;
	}

	p_free_wc = p_ioctl->out.wc;
	num_wc = p_ioctl->in.num_wc;
	for( i = 0; i < num_wc; i++ )
		p_free_wc[i].p_next = &p_free_wc[i+1];
	p_free_wc[i - 1].p_next = NULL;

	status = ib_poll_cq( h_cq, &p_free_wc, &p_done_wc );

	/*
	 * If any of the completions are done, copy to user
	 * otherwise, just return
	 */
	if( status == IB_SUCCESS )
	{
		CL_ASSERT( p_done_wc );
		/* Calculate the number of WCs. */
		if( !p_free_wc )
		{
			p_ioctl->out.num_wc = num_wc;
		}
		else
		{
			p_ioctl->out.num_wc = (uint32_t)
				(((uintn_t)p_free_wc) - ((uintn_t)p_done_wc)) /
				sizeof(ib_wc_t);
		}
	}
	else
	{
proxy_poll_cq_err:
		p_ioctl->out.num_wc = 0;
	}

	if( h_cq )
		deref_al_obj( &h_cq->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out) - sizeof(ib_wc_t);
	if( p_ioctl->out.num_wc )
		*p_ret_bytes += (sizeof(ib_wc_t) * (p_ioctl->out.num_wc));

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_REARM_CQ
 */
static cl_status_t
proxy_rearm_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_rearm_cq_ioctl_t	*p_ioctl =
		(ual_rearm_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		p_ioctl->out.status = IB_INVALID_CQ_HANDLE;
		AL_EXIT( AL_DBG_CQ );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = ib_rearm_cq( h_cq, p_ioctl->in.solicited );

	deref_al_obj( &h_cq->obj );

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_REARM_N_CQ
 */
static
cl_status_t
proxy_rearm_n_cq(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_rearm_n_cq_ioctl_t *p_ioctl =
		(ual_rearm_n_cq_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_cq_handle_t			h_cq;

	AL_ENTER( AL_DBG_CQ );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_CQ );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate CQ handle */
	h_cq = (ib_cq_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_cq, AL_OBJ_TYPE_H_CQ );
	if( !h_cq )
	{
		p_ioctl->out.status = IB_INVALID_CQ_HANDLE;
		AL_EXIT( AL_DBG_CQ );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = ib_rearm_n_cq( h_cq, p_ioctl->in.n_cqes );

	deref_al_obj( &h_cq->obj );

	AL_EXIT( AL_DBG_CQ );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_REGISTER_MEM:
 */
static cl_status_t
proxy_register_mr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_reg_mem_ioctl_t		*p_ioctl =
		(ual_reg_mem_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MR );
		return CL_INVALID_PARAMETER;
	}

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_register_mr_err;
	}

	/* Validate input region size. */
	if( p_ioctl->in.mem_create.length > ~((size_t)0) )
	{
		status = IB_INVALID_SETTING;
		goto proxy_register_mr_err;
	}

	status = reg_mem( h_pd, &p_ioctl->in.mem_create, (ULONG_PTR)p_ioctl->in.mem_create.vaddr, &p_ioctl->out.lkey,
		&p_ioctl->out.rkey, &h_mr, TRUE );

	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_mr = h_mr->obj.hdl;
		h_mr->obj.hdl_valid = TRUE;
		deref_al_obj( &h_mr->obj );
	}
	else
	{
proxy_register_mr_err:
		p_ioctl->out.h_mr = AL_INVALID_HANDLE;
		p_ioctl->out.lkey = 0;
		p_ioctl->out.rkey = 0;
	}

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MR );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_QUERY_MEM:
 */
static cl_status_t
proxy_query_mr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
	OUT		size_t					*p_ret_bytes )
{
	ual_query_mr_ioctl_t	*p_ioctl =
		(ual_query_mr_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MR );
		return CL_INVALID_PARAMETER;
	}

	/* Validate MR handle */
	h_mr = (ib_mr_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mr, AL_OBJ_TYPE_H_MR );
	if( !h_mr )
	{
		status = IB_INVALID_MR_HANDLE;
		goto proxy_query_mr_err;
	}

	status = ib_query_mr( h_mr, &p_ioctl->out.attr );

	if( status == IB_SUCCESS )
	{
		/* Replace the pd handle with proxy's handle */
		p_ioctl->out.attr.h_pd_padding = p_ioctl->out.attr.h_pd->obj.hdl;
	}
	else
	{
proxy_query_mr_err:
		cl_memclr( &p_ioctl->out.attr, sizeof(ib_mr_attr_t) );
	}

	if( h_mr )
		deref_al_obj( &h_mr->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MR );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_MODIFY_MEM:
 */
static cl_status_t
proxy_modify_mr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_rereg_mem_ioctl_t	*p_ioctl =
		(ual_rereg_mem_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mr_handle_t			h_mr;
	ib_pd_handle_t			h_pd = NULL;
	ib_mr_create_t			*p_mr_create;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MR );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MR );
		return CL_INVALID_PARAMETER;
	}

	/* Validate MR handle */
	h_mr = (ib_mr_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mr, AL_OBJ_TYPE_H_MR );
	if( !h_mr )
	{
		status = IB_INVALID_MR_HANDLE;
		goto proxy_modify_mr_err;
	}

	/* Validate input region size. */
	if( p_ioctl->in.mem_create.length > ~((size_t)0) )
	{
		status = IB_INVALID_SETTING;
		goto proxy_modify_mr_err;
	}

	if( p_ioctl->in.mem_mod_mask & IB_MR_MOD_PD )
	{
		if( !p_ioctl->in.h_pd )
		{
			status = IB_INVALID_PD_HANDLE;
			goto proxy_modify_mr_err;
		}
		/* This is a modify PD request, validate the PD handle */
		h_pd = (ib_pd_handle_t)
			al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
		if( !h_pd )
		{
			status = IB_INVALID_PD_HANDLE;
			goto proxy_modify_mr_err;
		}
	}
	else
	{
		h_pd = NULL;
	}

	if( p_ioctl->in.mem_mod_mask != IB_MR_MOD_PD )
		p_mr_create = &p_ioctl->in.mem_create;
	else
		p_mr_create = NULL;

	status = rereg_mem( h_mr, p_ioctl->in.mem_mod_mask,
		p_mr_create, &p_ioctl->out.lkey, &p_ioctl->out.rkey, h_pd, TRUE );

	if( status != IB_SUCCESS )
	{
proxy_modify_mr_err:
		p_ioctl->out.lkey = 0;
		p_ioctl->out.rkey = 0;
	}

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	if( h_mr )
		deref_al_obj( &h_mr->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MR );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_REG_SHARED_MEM:
 */
static cl_status_t
proxy_shared_mr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_reg_shared_ioctl_t	*p_ioctl =
		(ual_reg_shared_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_mr_handle_t			h_mr, h_cur_mr;
	ib_api_status_t			status;
	uint64_t				vaddr;

	AL_ENTER( AL_DBG_MR );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MR );
		return CL_INVALID_PARAMETER;
	}

	/*
	 * TODO: Must support taking an input handle that isn't
	 * in this process's context.
	 */
	/* Validate MR handle */
	h_cur_mr = (ib_mr_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mr, AL_OBJ_TYPE_H_MR );
	if( !h_cur_mr )
	{
		h_pd = NULL;
		status = IB_INVALID_MR_HANDLE;
		goto proxy_shared_mr_err;
	}

	/* Validate the PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_shared_mr_err;
	}

	vaddr = p_ioctl->in.vaddr;
	status = reg_shared( h_cur_mr, h_pd,
		p_ioctl->in.access_ctrl, &vaddr, &p_ioctl->out.lkey,
		&p_ioctl->out.rkey, &h_mr, TRUE );

	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_new_mr = h_mr->obj.hdl;
		p_ioctl->out.vaddr = vaddr;
		h_mr->obj.hdl_valid = TRUE;
		deref_al_obj( &h_mr->obj );
	}
	else
	{
proxy_shared_mr_err:
		cl_memclr( &p_ioctl->out, sizeof(p_ioctl->out) );
	}

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	if( h_cur_mr )
		deref_al_obj( &h_cur_mr->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MR );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_DEREGISTER_MEM:
 */
static cl_status_t
proxy_deregister_mr(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_dereg_mr_ioctl_t	*p_ioctl =
		(ual_dereg_mr_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mr_handle_t			h_mr;

	AL_ENTER( AL_DBG_MR );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MR );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate MR handle */
	h_mr = (ib_mr_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mr, AL_OBJ_TYPE_H_MR );
	if( !h_mr )
	{
		p_ioctl->out.status = IB_INVALID_MR_HANDLE;
		AL_EXIT( AL_DBG_MR );
		return CL_SUCCESS;
	}

	p_ioctl->out.status = dereg_mr( h_mr );

	if( p_ioctl->out.status != IB_SUCCESS )
		deref_al_obj( &h_mr->obj );

	AL_EXIT( AL_DBG_MR );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_CREATE_MW:
 */
static cl_status_t
proxy_create_mw(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_create_mw_ioctl_t	*p_ioctl =
		(ual_create_mw_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_mw_handle_t			h_mw;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MW );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MW );
		return CL_INVALID_PARAMETER;
	}

	/* Validate PD handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_create_mw_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_mw_err;

	status = create_mw( h_pd, &p_ioctl->out.rkey, &h_mw, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_create_mw_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_mw = h_mw->obj.hdl;
		h_mw->obj.hdl_valid = TRUE;
		deref_al_obj( &h_mw->obj );
	}
	else
	{
		h_mw->obj.pfn_destroy( &h_mw->obj, NULL );

proxy_create_mw_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.rkey = 0;
		p_ioctl->out.h_mw = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MW );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_QUERY_MW:
 */
static cl_status_t
proxy_query_mw(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_query_mw_ioctl_t	*p_ioctl =
		(ual_query_mw_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mw_handle_t			h_mw;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;
	ib_pd_handle_t			h_pd;

	AL_ENTER( AL_DBG_MW );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MW );
		return CL_INVALID_PARAMETER;
	}

	/* Validate MW handle */
	h_mw = (ib_mw_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mw, AL_OBJ_TYPE_H_MW );
	if( !h_mw )
	{
		status = IB_INVALID_MW_HANDLE;
		goto proxy_query_mw_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_query_mw_err;

	status = query_mw( h_mw, &h_pd, &p_ioctl->out.rkey, p_umv_buf );

	if( status != IB_SUCCESS )
		goto proxy_query_mw_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status != IB_SUCCESS )
	{
proxy_query_mw_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.rkey = 0;
	}
	free_umvbuf( p_umv_buf );

	if( h_mw )
		deref_al_obj( &h_mw->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MW );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_BIND_MW:
 */
static cl_status_t
proxy_bind_mw(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_bind_mw_ioctl_t	*p_ioctl =
		(ual_bind_mw_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mw_handle_t			h_mw;
	ib_qp_handle_t			h_qp;
	ib_mr_handle_t			h_mr;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MW );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MW );
		return CL_INVALID_PARAMETER;
	}

	/* Validate MW handle */
	h_mw = (ib_mw_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mw, AL_OBJ_TYPE_H_MW );
	if( !h_mw )
	{
		status = IB_INVALID_MW_HANDLE;
		goto proxy_bind_mw_err1;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_bind_mw_err2;
	}

	/* Validate MR handle */
	h_mr = (ib_mr_handle_t)al_hdl_ref( p_context->h_al,
		p_ioctl->in.mw_bind.h_mr_padding, AL_OBJ_TYPE_H_MR );
	if( !h_mr )
	{
		status = IB_INVALID_MR_HANDLE;
		goto proxy_bind_mw_err3;
	}

	/* Update bind attribute with the kernel space handles */
	p_ioctl->in.mw_bind.h_mr = h_mr;

	status = ib_bind_mw( h_mw, h_qp,
		&p_ioctl->in.mw_bind, &p_ioctl->out.r_key );

	deref_al_obj( &h_mr->obj );
proxy_bind_mw_err3:
	deref_al_obj( &h_qp->obj );
proxy_bind_mw_err2:
	deref_al_obj( &h_mw->obj );
proxy_bind_mw_err1:

	if( status != IB_SUCCESS )
		p_ioctl->out.r_key = 0;

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MW );
	return CL_SUCCESS;
}



/*
 * Process the ioctl UAL_DESTROY_MW:
 */
static cl_status_t
proxy_destroy_mw(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_destroy_mw_ioctl_t	*p_ioctl =
		(ual_destroy_mw_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_mw_handle_t			h_mw;

	AL_ENTER( AL_DBG_MW );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MW );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate MW handle */
	h_mw = (ib_mw_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_mw, AL_OBJ_TYPE_H_MW );
	if( !h_mw )
	{
		p_ioctl->out.status = IB_INVALID_MW_HANDLE;
		AL_EXIT( AL_DBG_MW );
		return CL_SUCCESS;
	}
	p_ioctl->out.status = destroy_mw( h_mw );

	if( p_ioctl->out.status != IB_SUCCESS )
		deref_al_obj( &h_mw->obj );

	AL_EXIT( AL_DBG_MW );
	return CL_SUCCESS;
}


cl_status_t
proxy_get_spl_qp(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_spl_qp_ioctl_t	*p_ioctl =
		(ual_spl_qp_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t	*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_pd_handle_t			h_pd;
	ib_qp_handle_t			h_qp;
	ci_umv_buf_t			*p_umv_buf = NULL;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_QP );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_QP );
		return CL_INVALID_PARAMETER;
	}

	/* Validate pd handle */
	h_pd = (ib_pd_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_pd, AL_OBJ_TYPE_H_PD );
	if( !h_pd )
	{
		status = IB_INVALID_PD_HANDLE;
		goto proxy_get_spl_qp_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_get_spl_qp_err;

	/* We obtain the pool_key separately from the special QP. */
	status = get_spl_qp( h_pd, p_ioctl->in.port_guid,
		&p_ioctl->in.qp_create, (void*)(ULONG_PTR)p_ioctl->in.context,
		proxy_qp_err_cb, NULL, &h_qp, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_get_spl_qp_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_qp = h_qp->obj.hdl;
		h_qp->obj.hdl_valid = TRUE;
		deref_al_obj( &h_qp->obj );
	}
	else
	{
		h_qp->obj.pfn_destroy( &h_qp->obj, NULL );

proxy_get_spl_qp_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_qp = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	if( h_pd )
		deref_al_obj( &h_pd->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_QP );
	return CL_SUCCESS;
}



static cl_status_t
proxy_attach_mcast(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_attach_mcast_ioctl_t	*p_ioctl =
		(ual_attach_mcast_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t		*p_context =
		(al_dev_open_context_t *)p_open_context;
	ib_qp_handle_t				h_qp;
	al_attach_handle_t			h_attach;
	ci_umv_buf_t				*p_umv_buf = NULL;
	ib_api_status_t				status;

	AL_ENTER( AL_DBG_MCAST );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MCAST );
		return CL_INVALID_PARAMETER;
	}

	/* Validate QP handle */
	h_qp = (ib_qp_handle_t)
		al_hdl_ref( p_context->h_al, p_ioctl->in.h_qp, AL_OBJ_TYPE_H_QP );
	if( !h_qp )
	{
		status = IB_INVALID_QP_HANDLE;
		goto proxy_attach_mcast_err;
	}

	status = cpyin_umvbuf( &p_ioctl->in.umv_buf, &p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_attach_mcast_err;

	status = al_attach_mcast( h_qp, &p_ioctl->in.mgid,
		p_ioctl->in.mlid, &h_attach, p_umv_buf );
	if( status != IB_SUCCESS )
		goto proxy_attach_mcast_err;

	status = cpyout_umvbuf( &p_ioctl->out.umv_buf, p_umv_buf );
	if( status == IB_SUCCESS )
	{
		p_ioctl->out.h_attach = h_attach->obj.hdl;
		h_attach->obj.hdl_valid = TRUE;
		deref_ctx_al_obj( &h_attach->obj, E_REF_INIT );
	}
	else
	{
		h_attach->obj.pfn_destroy( &h_attach->obj, NULL );

proxy_attach_mcast_err:
		p_ioctl->out.umv_buf = p_ioctl->in.umv_buf;
		p_ioctl->out.h_attach = AL_INVALID_HANDLE;
	}
	free_umvbuf( p_umv_buf );

	if( h_qp )
		deref_al_obj( &h_qp->obj );

	p_ioctl->out.status = status;
	*p_ret_bytes = sizeof(p_ioctl->out);

	AL_EXIT( AL_DBG_MCAST );
	return CL_SUCCESS;
}



static cl_status_t
proxy_detach_mcast(
	IN		void					*p_open_context,
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	ual_detach_mcast_ioctl_t	*p_ioctl =
		(ual_detach_mcast_ioctl_t *)cl_ioctl_in_buf( h_ioctl );
	al_dev_open_context_t		*p_context =
		(al_dev_open_context_t *)p_open_context;
	al_attach_handle_t			h_attach;

	AL_ENTER( AL_DBG_MCAST );

	/* Validate input buffers. */
	if( !cl_ioctl_in_buf( h_ioctl ) || !cl_ioctl_out_buf( h_ioctl ) ||
		cl_ioctl_in_size( h_ioctl ) != sizeof(p_ioctl->in) ||
		cl_ioctl_out_size( h_ioctl ) != sizeof(p_ioctl->out) )
	{
		AL_EXIT( AL_DBG_MCAST );
		return CL_INVALID_PARAMETER;
	}

	/* Set the return bytes in all cases */
	*p_ret_bytes = sizeof(p_ioctl->out);

	/* Validate mcast handle */
	h_attach = (al_attach_handle_t)al_hdl_ref(
		p_context->h_al, p_ioctl->in.h_attach, AL_OBJ_TYPE_H_ATTACH );
	if( !h_attach )
	{
		p_ioctl->out.status = IB_INVALID_MCAST_HANDLE;
		AL_EXIT( AL_DBG_MCAST );
		return CL_SUCCESS;
	}

	h_attach->obj.pfn_destroy( &h_attach->obj, ib_sync_destroy );
	p_ioctl->out.status = IB_SUCCESS;

	AL_EXIT( AL_DBG_MCAST );
	return CL_SUCCESS;
}



cl_status_t
verbs_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t				cl_status;
	IO_STACK_LOCATION		*p_io_stack;
	void					*p_context;

	AL_ENTER( AL_DBG_DEV );

	p_io_stack = IoGetCurrentIrpStackLocation( h_ioctl );
	p_context = p_io_stack->FileObject->FsContext;

	if( !p_context )
	{
		AL_EXIT( AL_DBG_DEV );
		return CL_INVALID_PARAMETER;
	}

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_OPEN_CA:
		cl_status = proxy_open_ca( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_CA:
		cl_status = proxy_query_ca( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_CA:
		cl_status = proxy_modify_ca( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CI_CALL:
		cl_status = proxy_ci_call( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_ALLOC_PD:
		cl_status = proxy_alloc_pd( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CREATE_AV:
		cl_status = proxy_create_av( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_AV:
		cl_status = proxy_query_av( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_AV:
		cl_status = proxy_modify_av( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CREATE_SRQ:
		cl_status = proxy_create_srq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_SRQ:
		cl_status = proxy_query_srq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_SRQ:
		cl_status = proxy_modify_srq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_SRQ:
		cl_status = proxy_destroy_srq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_POST_SRQ_RECV:
		cl_status = proxy_post_srq_recv( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CREATE_QP:
		cl_status = proxy_create_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_QP:
		cl_status = proxy_query_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_QP:
		cl_status = proxy_modify_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CREATE_CQ:
		cl_status = proxy_create_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_CQ:
		cl_status = proxy_query_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_CQ:
		cl_status = proxy_modify_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REG_MR:
		cl_status = proxy_register_mr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_MR:
		cl_status = proxy_query_mr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_MODIFY_MR:
		cl_status = proxy_modify_mr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REG_SHARED:
		cl_status = proxy_shared_mr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CREATE_MW:
		cl_status = proxy_create_mw( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_QUERY_MW:
		cl_status = proxy_query_mw( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_BIND_MW:
		cl_status = proxy_bind_mw( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_POST_SEND:
		cl_status = proxy_post_send( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_POST_RECV:
		cl_status = proxy_post_recv( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_PEEK_CQ:
		cl_status = proxy_peek_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_POLL_CQ:
		cl_status = proxy_poll_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REARM_CQ:
		cl_status = proxy_rearm_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_REARM_N_CQ:
		cl_status = proxy_rearm_n_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_ATTACH_MCAST:
		cl_status = proxy_attach_mcast( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_GET_SPL_QP_ALIAS:
		cl_status = proxy_get_spl_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_CLOSE_CA:
		cl_status = proxy_close_ca( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DEALLOC_PD:
		cl_status = proxy_dealloc_pd( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_AV:
		cl_status = proxy_destroy_av( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_QP:
		cl_status = proxy_destroy_qp( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_CQ:
		cl_status = proxy_destroy_cq( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DEREG_MR:
		cl_status = proxy_deregister_mr( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DESTROY_MW:
		cl_status = proxy_destroy_mw( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_DETACH_MCAST:
		cl_status = proxy_detach_mcast( p_context, h_ioctl, p_ret_bytes );
		break;
	case UAL_GET_VENDOR_LIBCFG:
		cl_status =
			proxy_get_vendor_libcfg( p_context, h_ioctl, p_ret_bytes );
		break;
	default:
		cl_status = CL_INVALID_PARAMETER;
		break;
	}

	AL_EXIT( AL_DBG_DEV );
	return cl_status;
}
