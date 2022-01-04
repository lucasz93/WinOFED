/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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
#include <complib/cl_ptr_vector.h>
#include <complib/cl_qlist.h>
#include "al_common.h"
#include "al_cm_cep.h"
#include "al_cm_conn.h"
#include "al_cm_sidr.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_cm_cep.tmh"
#endif

#include "ib_common.h"
#include "al_mgr.h"
//#include "al_ca.h"
#include "al.h"
//#include "al_mad.h"
#include "al_qp.h"


#define UAL_CEP_MIN					(512)
#define UAL_CEP_GROW				(256)


/* Global connection manager object. */
typedef struct _ual_cep_mgr
{
	al_obj_t				obj;

	cl_ptr_vector_t			cep_vector;

	/* File handle on which to issue query IOCTLs. */
	HANDLE					h_file;

}	ual_cep_mgr_t;


typedef struct _al_ucep
{
	al_pfn_cep_cb_t				pfn_cb;
	ib_al_handle_t				h_al;
	cl_list_item_t				al_item;

	ib_pfn_destroy_cb_t			pfn_destroy_cb;
	void*						destroy_context;
	net32_t						cid;

	OVERLAPPED					ov;
	atomic32_t					ref_cnt;

}	ucep_t;


/* Global instance of the CM agent. */
ual_cep_mgr_t		*gp_cep_mgr = NULL;


/*
 * Frees the global CEP manager.  Invoked during al_obj destruction.
 */
static void
__free_cep_mgr(
	IN				al_obj_t*					p_obj )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( &gp_cep_mgr->obj == p_obj );

	if( gp_cep_mgr->h_file != INVALID_HANDLE_VALUE )
		CloseHandle( gp_cep_mgr->h_file );

	cl_ptr_vector_destroy( &gp_cep_mgr->cep_vector );

	destroy_al_obj( p_obj );

	cl_free( gp_cep_mgr );
	gp_cep_mgr = NULL;

	AL_EXIT( AL_DBG_CM );
}


/*
 * Allocates and initialized the global user-mode CM agent.
 */
ib_api_status_t
create_cep_mgr(
	IN				al_obj_t* const				p_parent_obj )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( gp_cep_mgr == NULL );

	/* Allocate the global CM agent. */
	gp_cep_mgr = (ual_cep_mgr_t*)cl_zalloc( sizeof(ual_cep_mgr_t) );
	if( !gp_cep_mgr )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("Failed allocation of global CEP manager.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &gp_cep_mgr->obj, AL_OBJ_TYPE_CM );
	cl_ptr_vector_construct( &gp_cep_mgr->cep_vector );
	gp_cep_mgr->h_file = INVALID_HANDLE_VALUE;

	status = init_al_obj( &gp_cep_mgr->obj, NULL, FALSE,
		NULL, NULL, __free_cep_mgr );
	if( status != IB_SUCCESS )
	{
		__free_cep_mgr( &gp_cep_mgr->obj );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}
	/* Attach to the parent object. */
	status = attach_al_obj( p_parent_obj, &gp_cep_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	cl_status = cl_ptr_vector_init(
		&gp_cep_mgr->cep_vector, UAL_CEP_MIN, UAL_CEP_GROW );
	if( cl_status != CL_SUCCESS )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("cl_vector_init failed with status %s.\n",
			CL_STATUS_MSG(cl_status)) );
		return ib_convert_cl_status( cl_status );
	}

	/* Create a file object on which to issue all CM requests. */
	gp_cep_mgr->h_file = ual_create_async_file( UAL_BIND_CM );
	if( gp_cep_mgr->h_file == INVALID_HANDLE_VALUE )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_create_async_file for UAL_BIND_CM returned %d.\n",
			GetLastError()) );
		return IB_ERROR;
	}

	/* Release the reference from init_al_obj */
	deref_ctx_al_obj( &gp_cep_mgr->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


void
al_cep_cleanup_al(
	IN		const	ib_al_handle_t				h_al )
{
	cl_list_item_t		*p_item;
	net32_t				cid;

	AL_ENTER( AL_DBG_CM );

	/* Destroy all CEPs associated with the input instance of AL. */
	cl_spinlock_acquire( &h_al->obj.lock );
	for( p_item = cl_qlist_head( &h_al->cep_list );
		p_item != cl_qlist_end( &h_al->cep_list );
		p_item = cl_qlist_head( &h_al->cep_list ) )
	{
		/*
		 * Note that we don't walk the list - we can't hold the AL
		 * lock when cleaning up its CEPs because the cleanup path
		 * takes the CEP's lock.  We always want to take the CEP
		 * before the AL lock to prevent any possibilities of deadlock.
		 *
		 * So we just get the CID, and then release the AL lock and try to
		 * destroy.  This should unbind the CEP from the AL instance and
		 * remove it from the list, allowing the next CEP to be cleaned up
		 * in the next pass through.
		 */
		cid = PARENT_STRUCT( p_item, ucep_t, al_item )->cid;
		cl_spinlock_release( &h_al->obj.lock );
		al_destroy_cep( h_al, &cid, FALSE );
		cl_spinlock_acquire( &h_al->obj.lock );
	}
	cl_spinlock_release( &h_al->obj.lock );

	AL_EXIT( AL_DBG_CM );
}


static void
__destroy_ucep(
	IN				ucep_t* const				p_cep )
{
	if( p_cep->pfn_destroy_cb )
		p_cep->pfn_destroy_cb( p_cep->destroy_context );
	cl_free( p_cep );
}


ib_api_status_t
__create_ucep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb,
	IN	OUT			net32_t* const				p_cid )
{
	ucep_t					*p_cep;
	DWORD					bytes_ret;
	ual_create_cep_ioctl_t	ioctl;

	AL_ENTER( AL_DBG_CM );

	p_cep = cl_zalloc( sizeof(ucep_t) );
	if( !p_cep )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("Failed to allocate ucep_t\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Initialize to two - one for the CEP, and one for the IOCTL. */
	p_cep->ref_cnt = 2;

	/* Store user parameters. */
	p_cep->pfn_cb = pfn_cb;
	p_cep->destroy_context = context;

	/* Create a kernel CEP only if we don't already have a CID. */
	if( cid == AL_INVALID_CID )
	{
		if( !DeviceIoControl( g_al_device, UAL_CREATE_CEP, NULL,
			0, &ioctl, sizeof(ioctl), &bytes_ret, NULL ) ||
			bytes_ret != sizeof(ioctl) )
		{
			__destroy_ucep( p_cep );
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("UAL_CREATE_CEP IOCTL failed with %d.\n", GetLastError()) );
			return IB_ERROR;
		}

		if( ioctl.status != IB_SUCCESS )
		{
			__destroy_ucep( p_cep );
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR, ("UAL_CREATE_CEP IOCTL returned %s\n",
				ib_get_err_str( ioctl.status )) );
			return ioctl.status;
		}

		p_cep->cid = ioctl.cid;
	}
	else
	{
		p_cep->cid = cid;
	}

	/* Track the CEP before we issue any further IOCTLs on it. */
	cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
	cl_ptr_vector_set_min_size( &gp_cep_mgr->cep_vector, p_cep->cid + 1 );
	CL_ASSERT( !cl_ptr_vector_get( &gp_cep_mgr->cep_vector, p_cep->cid ) );
	cl_ptr_vector_set( &gp_cep_mgr->cep_vector, p_cep->cid, p_cep );
	cl_spinlock_release( &gp_cep_mgr->obj.lock );

	/* Now issue a poll request.  This request is async. */
	if( DeviceIoControl( gp_cep_mgr->h_file, UAL_CEP_GET_EVENT,
		&p_cep->cid, sizeof(p_cep->cid),
		NULL, 0, NULL, &p_cep->ov ) ||
		GetLastError() != ERROR_IO_PENDING )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("Failed to issue CEP poll IOCTL.\n") );
		cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
		cl_ptr_vector_set( &gp_cep_mgr->cep_vector, p_cep->cid, NULL );
		cl_spinlock_release( &gp_cep_mgr->obj.lock );

		DeviceIoControl( g_al_device, UAL_DESTROY_CEP, &p_cep->cid,
			sizeof(p_cep->cid), NULL, 0, &bytes_ret, NULL );

		__destroy_ucep( p_cep );
		AL_EXIT( AL_DBG_CM );
		return IB_ERROR;
	}

	p_cep->h_al = h_al;

	/* Track the CEP in its owning AL instance. */
	cl_spinlock_acquire( &h_al->obj.lock );
	if( p_cid )
	{
		if( *p_cid != AL_INVALID_CID )
		{
			cl_spinlock_release( &h_al->obj.lock );
			al_destroy_cep( h_al, &cid, TRUE );
			return IB_INVALID_STATE;
		}
		p_cep->pfn_destroy_cb = pfn_destroy_cb;
		*p_cid = p_cep->cid;
	}

	cl_qlist_insert_tail( &h_al->cep_list, &p_cep->al_item );

	cl_spinlock_release( &h_al->obj.lock );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
al_create_cep(
	IN				ib_al_handle_t				h_al,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb,
	IN	OUT			net32_t* const				p_cid )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	status = __create_ucep(
		h_al, AL_INVALID_CID, pfn_cb, context, pfn_destroy_cb, p_cid );

	AL_EXIT( AL_DBG_CM );
	return status;
}


/*
 * Note that destroy_cep is synchronous.  It does however handle the case
 * where a user calls it from a callback context.
 */
void
al_destroy_cep(
	IN				ib_al_handle_t				h_al,
	IN	OUT			net32_t* const				p_cid,
	IN				boolean_t					reusable )
{
	ucep_t		*p_cep;
	DWORD		bytes_ret;

	AL_ENTER( AL_DBG_CM );
	
#if DBG
	if ( !h_al) {
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("h_al == NULL, probably because qp creation was failed.\n") );
	}
#endif

	cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
	if( *p_cid < cl_ptr_vector_get_size( &gp_cep_mgr->cep_vector ) )
	{
		p_cep = cl_ptr_vector_get( &gp_cep_mgr->cep_vector, *p_cid );
		if( p_cep && p_cep->h_al == h_al )
			cl_ptr_vector_set( &gp_cep_mgr->cep_vector, *p_cid, NULL );
		else
			goto invalid;
	}
	else
	{
invalid:
		if( !reusable )
			*p_cid = AL_RESERVED_CID;

		cl_spinlock_release( &gp_cep_mgr->obj.lock );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	/*
	 * Destroy the kernel CEP right away.  We must synchronize with issuing
	 * the next GET_EVENT IOCTL.
	 */
	DeviceIoControl( g_al_device, UAL_DESTROY_CEP, &p_cep->cid,
		sizeof(p_cep->cid), NULL, 0, &bytes_ret, NULL );
	if( reusable )
		*p_cid = AL_INVALID_CID;
	else
		*p_cid = AL_RESERVED_CID;

	cl_spinlock_release( &gp_cep_mgr->obj.lock );

	/*
	 * Remove from the AL instance.  Note that once removed, all
	 * callbacks for an item will stop.
	 */
	cl_spinlock_acquire( &h_al->obj.lock );
	cl_qlist_remove_item( &h_al->cep_list, &p_cep->al_item );

	cl_spinlock_release( &h_al->obj.lock );

	if( !cl_atomic_dec( &p_cep->ref_cnt ) )
	{
		/* We have no remaining refrences. */
		__destroy_ucep( p_cep );
	}

	AL_EXIT( AL_DBG_CM );
}


ib_api_status_t
al_cep_listen(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_cep_listen_t* const		p_listen_info )
{
	ual_cep_listen_ioctl_t	ioctl;
	ib_api_status_t			status;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_listen_info )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	ioctl.cid = cid;
	ioctl.cep_listen = *p_listen_info;
	ioctl.cep_listen.p_cmp_buf_padding = 0;
	if( p_listen_info->p_cmp_buf )
	{
		if( p_listen_info->cmp_len > IB_REQ_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("Listen compare data larger than REQ private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.cep_listen.p_cmp_buf_padding = 1;
		cl_memcpy( ioctl.compare, p_listen_info->p_cmp_buf,
			p_listen_info->cmp_len );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_LISTEN, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_cep_listen IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_pre_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_req_t* const			p_cm_req,
		OUT			ib_qp_mod_t* const			p_init )
{
	ual_cep_req_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_req )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	if( !p_init )
	{
		AL_EXIT( AL_DBG_ERROR );
		return IB_INVALID_PARAMETER;
	}

	ioctl.in.cid = cid;
	ioctl.in.cm_req = *p_cm_req;
	ioctl.in.cm_req.h_qp_padding = p_cm_req->h_qp->obj.hdl;
	ioctl.in.paths[0] = *(p_cm_req->p_primary_path);
	ioctl.in.cm_req.p_alt_path_padding = 0;
	if( p_cm_req->p_alt_path )
	{
		ioctl.in.cm_req.p_alt_path_padding = 1;
		ioctl.in.paths[1] = *(p_cm_req->p_alt_path);
	}
	/* Copy private data, if any. */
	ioctl.in.cm_req.p_req_pdata_padding = 0;
	if( p_cm_req->p_req_pdata )
	{
		if( p_cm_req->req_length > IB_REQ_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than REQ private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.in.cm_req.p_req_pdata_padding = 1;
		cl_memcpy( ioctl.in.pdata, p_cm_req->p_req_pdata,
			p_cm_req->req_length );
	}

	/* Copy compare data, if any. */
	ioctl.in.cm_req.p_compare_buffer_padding = 0;
	if( p_cm_req->p_compare_buffer )
	{
		if( p_cm_req->compare_length > IB_REQ_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("REQ compare data larger than REQ private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.in.cm_req.p_compare_buffer_padding = 1;
		cl_memcpy( ioctl.in.compare, p_cm_req->p_compare_buffer,
			p_cm_req->compare_length );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_PRE_REQ, &ioctl,
		sizeof(ioctl.in), &ioctl, sizeof(ioctl.out), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_PRE_REQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.out.status == IB_SUCCESS )
		*p_init = ioctl.out.init;

	AL_EXIT( AL_DBG_CM );
	return ioctl.out.status;
}


ib_api_status_t
al_cep_send_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	DWORD				bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_SEND_REQ, &cid,
		sizeof(cid), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_SEND_REQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_pre_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb,
	IN		const	ib_cm_rep_t* const			p_cm_rep,
	IN	OUT			net32_t* const				p_cid,
		OUT			ib_qp_mod_t* const			p_init )
{
	ucep_t					*p_cep;
	ual_cep_rep_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_rep )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	if( !p_init )
	{
		AL_EXIT( AL_DBG_ERROR );
		return IB_INVALID_PARAMETER;
	}

	/* Store the context for the CEP. */
	cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
	p_cep = cl_ptr_vector_get( &gp_cep_mgr->cep_vector, cid );
	if( !p_cep )
	{
		cl_spinlock_release( &gp_cep_mgr->obj.lock );
		AL_EXIT( AL_DBG_ERROR );
		return IB_INVALID_PARAMETER;
	}
	p_cep->destroy_context = context;
	cl_spinlock_release( &gp_cep_mgr->obj.lock );

	ioctl.in.context = (ULONG_PTR)context;
	ioctl.in.cid = cid;
	ioctl.in.cm_rep = *p_cm_rep;
	ioctl.in.cm_rep.h_qp_padding = p_cm_rep->h_qp->obj.hdl;
	/* Copy private data, if any. */
	ioctl.in.cm_rep.p_rep_pdata_padding = 0;
	if( p_cm_rep->p_rep_pdata )
	{
		if( p_cm_rep->rep_length > IB_REP_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than REP private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.in.cm_rep.p_rep_pdata_padding = 1;
		cl_memcpy( ioctl.in.pdata, p_cm_rep->p_rep_pdata,
			p_cm_rep->rep_length );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_PRE_REP, &ioctl,
		sizeof(ioctl.in), &ioctl, sizeof(ioctl.out), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_PRE_REQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.out.status == IB_SUCCESS )
	{
		cl_spinlock_acquire( &h_al->obj.lock );
		if( *p_cid != AL_INVALID_CID )
		{
			cl_spinlock_release( &h_al->obj.lock );
			return IB_INVALID_STATE;
		}
		p_cep->pfn_destroy_cb = pfn_destroy_cb;
		*p_cid = p_cep->cid;
		*p_init = ioctl.out.init;
		cl_spinlock_release( &h_al->obj.lock );
	}

	AL_EXIT( AL_DBG_CM );
	return ioctl.out.status;
}


ib_api_status_t
al_cep_send_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	DWORD				bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_SEND_REP, &cid,
		sizeof(cid), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_SEND_REP IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_get_rtr_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rtr )
{
	ual_cep_get_rtr_ioctl_t	ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_rtr )
	{
		AL_EXIT( AL_DBG_ERROR );
		return IB_INVALID_PARAMETER;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_GET_RTR, &cid,
		sizeof(cid), &ioctl, sizeof(ioctl), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_GET_RTR IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.status == IB_SUCCESS )
		*p_rtr = ioctl.rtr;

	AL_EXIT( AL_DBG_CM );
	return ioctl.status;
}


ib_api_status_t
al_cep_get_rts_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rts )
{
	ual_cep_get_rts_ioctl_t	ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_rts )
	{
		AL_EXIT( AL_DBG_ERROR );
		return IB_INVALID_PARAMETER;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_GET_RTS, &cid,
		sizeof(cid), &ioctl, sizeof(ioctl), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_GET_RTS IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.status == IB_SUCCESS )
		*p_rts = ioctl.rts;

	AL_EXIT( AL_DBG_CM );
	return ioctl.status;
}


ib_api_status_t
al_cep_rtu(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len )
{
	ib_api_status_t			status;
	ual_cep_rtu_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	/* Copy private data, if any. */
	if( p_pdata )
	{
		if( pdata_len > IB_RTU_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than RTU private data.\n") );
			return IB_INVALID_SETTING;
		}

		cl_memcpy( ioctl.pdata, p_pdata, pdata_len );
	}
	ioctl.pdata_len = pdata_len;

	if( !DeviceIoControl( g_al_device, UAL_CEP_RTU, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_RTU IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_rej(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_rej_status_t				rej_status,
	IN		const	uint8_t* const				p_ari,
	IN				uint8_t						ari_len,
	IN		const	uint8_t* const				p_pdata,
	IN				uint8_t						pdata_len )
{
	ib_api_status_t			status;
	ual_cep_rej_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	ioctl.rej_status = rej_status;
	if( p_ari )
	{
		if( ari_len > IB_ARI_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than REJ ARI data.\n") );
			return IB_INVALID_SETTING;
		}

		cl_memcpy( ioctl.ari, p_ari, ari_len );
		ioctl.ari_len = ari_len;
	}
	else
	{
		ioctl.ari_len = 0;
	}
	/* Copy private data, if any. */
	if( p_pdata)
	{
		if( pdata_len > IB_REJ_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than REJ private data.\n") );
			return IB_INVALID_SETTING;
		}

		cl_memcpy( ioctl.pdata, p_pdata, pdata_len );
		ioctl.pdata_len = pdata_len;
	}
	else
	{
		ioctl.pdata_len = 0;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_REJ, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_PRE_REQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_mra(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_mra_t* const			p_cm_mra )
{
	ib_api_status_t			status;
	ual_cep_mra_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_mra )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	ioctl.cm_mra = *p_cm_mra;
	ioctl.cm_mra.p_mra_pdata_padding = 0;
	/* Copy private data, if any. */
	if( p_cm_mra->p_mra_pdata )
	{
		if( p_cm_mra->mra_length > IB_MRA_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than MRA private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.cm_mra.p_mra_pdata_padding = 1;
		cl_memcpy(
			ioctl.pdata, p_cm_mra->p_mra_pdata, p_cm_mra->mra_length );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_MRA, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_MRA IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_lap(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_lap_t* const			p_cm_lap )
{
	ib_api_status_t			status;
	ual_cep_lap_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_lap )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_lap->p_alt_path )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	ioctl.cm_lap = *p_cm_lap;
	ioctl.cm_lap.h_qp_padding = p_cm_lap->h_qp->obj.hdl;
	ioctl.alt_path = *(p_cm_lap->p_alt_path);
	/* Copy private data, if any. */
	ioctl.cm_lap.p_lap_pdata_padding = 0;
	if( p_cm_lap->p_lap_pdata )
	{
		if( p_cm_lap->lap_length > IB_LAP_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than LAP private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.cm_lap.p_lap_pdata_padding = 1;
		cl_memcpy(
			ioctl.pdata, p_cm_lap->p_lap_pdata, p_cm_lap->lap_length );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_LAP, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_LAP IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_pre_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_apr_t* const			p_cm_apr,
		OUT			ib_qp_mod_t* const			p_apr )
{
	ual_cep_apr_ioctl_t		ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_cm_apr || !p_apr )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	ioctl.in.cid = cid;
	ioctl.in.cm_apr = *p_cm_apr;
	ioctl.in.cm_apr.h_qp_padding = p_cm_apr->h_qp->obj.hdl;
	ioctl.in.cm_apr.p_info_padding = 0;
	if( p_cm_apr->p_info )
	{
		if( p_cm_apr->info_length > IB_APR_INFO_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than APR info data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.in.cm_apr.p_info_padding = 1;
		cl_memcpy(
			ioctl.in.apr_info, p_cm_apr->p_info, p_cm_apr->info_length );
	}
	/* Copy private data, if any. */
	ioctl.in.cm_apr.p_apr_pdata_padding = 0;
	if( p_cm_apr->p_apr_pdata )
	{
		if( p_cm_apr->apr_length > IB_APR_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than APR private data.\n") );
			return IB_INVALID_SETTING;
		}

		ioctl.in.cm_apr.p_apr_pdata_padding = 1;
		cl_memcpy(
			ioctl.in.pdata, p_cm_apr->p_apr_pdata, p_cm_apr->apr_length );
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_PRE_APR, &ioctl.in,
		sizeof(ioctl.in), &ioctl.out, sizeof(ioctl.out), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl.out) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_PRE_REQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}
	
	if( ioctl.out.status == IB_SUCCESS )
		*p_apr = ioctl.out.apr;

	AL_EXIT( AL_DBG_CM );
	return ioctl.out.status;
}


ib_api_status_t
al_cep_send_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	DWORD				bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_SEND_APR, &cid,
		sizeof(cid), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_SEND_APR IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_dreq(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata OPTIONAL,
	IN		const	uint8_t						pdata_len )
{
	ib_api_status_t			status;
	ual_cep_dreq_ioctl_t	ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	/* Copy private data, if any. */
	if( p_pdata )
	{
		if( pdata_len > IB_DREQ_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than DREQ private data.\n") );
			return IB_INVALID_SETTING;
		}

		cl_memcpy( ioctl.pdata, p_pdata, pdata_len );
		ioctl.pdata_len = pdata_len;
	}
	else
	{
		ioctl.pdata_len = 0;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_DREQ, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_DREQ IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_drep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata OPTIONAL,
	IN		const	uint8_t						pdata_len )
{
	ib_api_status_t			status;
	ual_cep_drep_ioctl_t	ioctl;
	DWORD					bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	ioctl.cid = cid;
	/* Copy private data, if any. */
	if( p_pdata )
	{
		if( pdata_len > IB_DREP_PDATA_SIZE )
		{
			AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
				("private data larger than DREP private data.\n") );
			return IB_INVALID_SETTING;
		}

		cl_memcpy( ioctl.pdata, p_pdata, pdata_len );
		ioctl.pdata_len = pdata_len;
	}
	else
	{
		ioctl.pdata_len = 0;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_DREP, &ioctl,
		sizeof(ioctl), &status, sizeof(status), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(status) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_DREP IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_get_timewait(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			uint64_t* const				p_timewait_us )
{
	ual_cep_get_timewait_ioctl_t	ioctl;
	DWORD							bytes_ret;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_timewait_us )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !DeviceIoControl( g_al_device, UAL_CEP_GET_TIMEWAIT, &cid, sizeof(cid),
		&ioctl, sizeof(ioctl), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl) )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_DREP IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.status == IB_SUCCESS )
		*p_timewait_us = ioctl.timewait_us;

	AL_EXIT( AL_DBG_CM );
	return ioctl.status;
}
//
//
//ib_api_status_t
//al_cep_migrate(
//	IN				ib_al_handle_t				h_al,
//	IN				net32_t						cid );
//
//
//ib_api_status_t
//al_cep_established(
//	IN				ib_al_handle_t				h_al,
//	IN				net32_t						cid );


ib_api_status_t
al_cep_poll(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			void**						p_context,
		OUT			net32_t* const				p_new_cid,
		OUT			ib_mad_element_t** const	pp_mad )
{
	ucep_t					*p_cep;
	ib_api_status_t			status;
	ual_cep_poll_ioctl_t	ioctl;
	DWORD					bytes_ret;
	ib_mad_element_t		*p_mad;
	ib_grh_t				*p_grh;
	ib_mad_t				*p_mad_buf;

	AL_ENTER( AL_DBG_CM );

	if( !h_al )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	if( !p_new_cid || !pp_mad )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
	if( cid > cl_ptr_vector_get_size( &gp_cep_mgr->cep_vector ) )
		p_cep = NULL;
	else
		p_cep = cl_ptr_vector_get( &gp_cep_mgr->cep_vector, cid );
	cl_spinlock_release( &gp_cep_mgr->obj.lock );
	if( !p_cep )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_PARAMETER;
	}

	status = ib_get_mad( g_pool_key, MAD_BLOCK_SIZE, &p_mad );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ib_get_mad returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	p_mad_buf = p_mad->p_mad_buf;
	p_grh = p_mad->p_grh;

	if( !DeviceIoControl( g_al_device, UAL_CEP_POLL, &cid,
		sizeof(cid), &ioctl, sizeof(ioctl), &bytes_ret, NULL ) ||
		bytes_ret != sizeof(ioctl) )
	{
		ib_put_mad( p_mad );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("UAL_CEP_POLL IOCTL failed with %d.\n", GetLastError()) );
		return IB_ERROR;
	}

	if( ioctl.status == IB_SUCCESS )
	{
		if( ioctl.new_cid != AL_INVALID_CID )
		{
			/* Need to create a new CEP for user-mode. */
			status = __create_ucep( p_cep->h_al, ioctl.new_cid,
				p_cep->pfn_cb, NULL, NULL, NULL );
			if( status != IB_SUCCESS )
			{
				DeviceIoControl( g_al_device, UAL_DESTROY_CEP,
					&ioctl.new_cid, sizeof(ioctl.new_cid),
					NULL, 0, &bytes_ret, NULL );
				goto err;
			}
		}

		/* Copy the MAD payload as it's all that's used. */
		*p_mad = ioctl.element;
		p_mad->p_grh = p_grh;
		if( p_mad->grh_valid )
			cl_memcpy( p_mad->p_grh, &ioctl.grh, sizeof(ib_grh_t) );
		p_mad->p_mad_buf = p_mad_buf;
		
		cl_memcpy( p_mad->p_mad_buf, ioctl.mad_buf, MAD_BLOCK_SIZE );

		*p_context = p_cep->destroy_context;
		*p_new_cid = ioctl.new_cid;
		*pp_mad = p_mad;
	}
	else
	{
err:
		ib_put_mad( p_mad );
	}

	AL_EXIT( AL_DBG_CM );
	return ioctl.status;
}


/* Callback to process CM events */
void
cm_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov )
{
	ucep_t			*p_cep;
	BOOL			ret;

	AL_ENTER( AL_DBG_CM );

	/* The UAL_CEP_GET_EVENT IOCTL does not have any output data. */
	UNUSED_PARAM( ret_bytes );

	p_cep = PARENT_STRUCT( p_ov, ucep_t, ov );

	if( !error_code  )
	{
		p_cep->pfn_cb( p_cep->h_al, p_cep->cid );

		/* Synchronize with destruction. */
		cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
		ret = DeviceIoControl( gp_cep_mgr->h_file, UAL_CEP_GET_EVENT,
			&p_cep->cid, sizeof(p_cep->cid), NULL, 0,
			NULL, &p_cep->ov );
		cl_spinlock_release( &gp_cep_mgr->obj.lock );
		if( !ret && GetLastError() == ERROR_IO_PENDING )
		{
			AL_EXIT( AL_DBG_CM );
			return;
		}
		else if( GetLastError() != ERROR_INVALID_PARAMETER )
		{
			/* We can get ERROR_INVALID_PARAMETER if the CEP was destroyed. */
			AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
				("DeviceIoControl for CEP callback request returned %d.\n",
				GetLastError()) );
		}
	}
	else
	{
		AL_PRINT(TRACE_LEVEL_WARNING ,AL_DBG_CM ,
			("UAL_CEP_GET_EVENT IOCTL returned %d.\n", error_code) );
	}

	/*
	 * We failed to issue the next request or the previous request was
	 * cancelled.  Release the reference held by the previous IOCTL and exit.
	 */
	if( !cl_atomic_dec( &p_cep->ref_cnt ) )
		__destroy_ucep( p_cep );

	AL_EXIT( AL_DBG_CM );
}
