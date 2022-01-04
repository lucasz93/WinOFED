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

#include <complib/cl_atomic.h>
#include <complib/cl_async_proc.h>
#include <complib/cl_memory.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_vector.h>

#include <iba/ib_ci.h>
#include <iba\ibat.h>

#include "al.h"
#include "al_cm_cep.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_mgr.tmh"
#endif

#include "al_dm.h"
#include "al_mad_pool.h"
#include "al_mcast.h"
#include "al_mgr.h"
#include "al_pnp.h"
#include "al_ioc_pnp.h"
#include "al_query.h"
#include "al_res_mgr.h"
#include "al_smi.h"
#include "ib_common.h"

#ifndef CL_KERNEL
#include "ual_mgr.h"
#endif


#define AL_HDL_VECTOR_MIN	64
#define AL_HDL_VECTOR_GROW	64


static void
__free_al_mgr(
	IN				al_obj_t					*p_obj );

void
free_al(
	IN				al_obj_t					*p_obj );



ib_api_status_t
create_al_mgr()
{
	cl_status_t				cl_status;
	ib_api_status_t			status;

	AL_ENTER( AL_DBG_MGR );

	CL_ASSERT( !gp_al_mgr );

	gp_al_mgr = cl_zalloc( sizeof( al_mgr_t ) );
	if( !gp_al_mgr )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("cl_zalloc failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the AL manager components. */
	cl_qlist_init( &gp_al_mgr->ci_ca_list );
	cl_qlist_init( &gp_al_mgr->al_obj_list );
	cl_spinlock_construct( &gp_al_mgr->lock );

	/* Initialize the AL management components. */
	construct_al_obj( &gp_al_mgr->obj, AL_OBJ_TYPE_AL_MGR );
	status = init_al_obj( &gp_al_mgr->obj, gp_al_mgr, FALSE,
		NULL, NULL, __free_al_mgr );
	if( status != IB_SUCCESS )
	{
		__free_al_mgr( &gp_al_mgr->obj );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed, status = 0x%x.\n", status) );
		return status;
	}

	cl_status = cl_spinlock_init( &gp_al_mgr->lock );
	if( cl_status != CL_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_spinlock_init failed\n") );
		return ib_convert_cl_status( cl_status );
	}

	/* We should be able to open AL now. */
	status = ib_open_al_trk( AL_WLOCATION, &gh_al );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_open_al failed, status = 0x%x.\n", status) );
		return status;
	}

	/*
	 * Initialize the AL management services.
	 * Create the PnP manager first - the other services depend on PnP.
	 */
	status = create_pnp( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_pnp_create failed with %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Create the global AL MAD pool. */
	status = ib_create_mad_pool( gh_al, 0, 0, 64, &gh_mad_pool );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_create_mad_pool failed with %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the AL resource manager. */
	status = create_res_mgr( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_res_mgr failed with %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the AL special QP manager. */
	status = create_spl_qp_mgr( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_spl_qp_mgr failed with %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Initialize the AL SA request manager. */
	status = create_sa_req_mgr( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_sa_req_mgr failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Initialize CM */
	status = create_cep_mgr( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_cm_mgr failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Initialize the AL device management agent. */

/*
	Disable support of DM agent.

	status = create_dm_agent( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_dm_agent failed, status = 0x%x.\n", status) );
		return status;
	}
*/
	status = create_ioc_pnp( &gp_al_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("create_ioc_pnp failed, status = 0x%x.\n", status) );
		return status;
	}

    status = IbatInitialize();

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &gp_al_mgr->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}



static void
__free_al_mgr(
	IN				al_obj_t					*p_obj )
{
	CL_ASSERT( p_obj == &gp_al_mgr->obj );

    IbatCleanup();

	/*
	 * We need to destroy the AL object before the spinlock, since
	 * destroying the AL object will try to acquire the spinlock.
	 */
	destroy_al_obj( p_obj );

	/* Verify that the object list is empty. */
	print_al_objs( NULL );

	cl_spinlock_destroy( &gp_al_mgr->lock );
	cl_free( gp_al_mgr );
	gp_al_mgr = NULL;
}



/*
 * Register a new CI CA with the access layer.
 */
ib_api_status_t
ib_register_ca(
	IN		const	ci_interface_t*				p_ci,
	IN		const	PDEVICE_OBJECT				p_hca_dev,
	IN		const	PDEVICE_OBJECT				p_fdo
	)
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_MGR );

	if( !p_ci )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}
	CL_ASSERT( !find_ci_ca( p_ci->guid ) );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MGR,
		("CA guid %I64x.\n", p_ci->guid) );

	/* Check the channel interface verbs version. */
	if( p_ci->version != VERBS_VERSION )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Unsupported channel interface version, "
			 "expected = 0x%x, actual = 0x%x.\n",
			 VERBS_VERSION, p_ci->version) );
		return IB_UNSUPPORTED;
	}

	/* Construct and initialize the CA structure. */
	status = create_ci_ca( &gp_al_mgr->obj, p_ci, p_hca_dev, p_fdo );
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("al_mgr_ca_init failed.\n") );
		return status;
	}

	AL_EXIT( AL_DBG_MGR );
	return status;
}



/*
 * Process the removal of a CI CA from the system.
 */
ib_api_status_t
ib_deregister_ca(
	IN		const	net64_t						ca_guid )
{
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER( AL_DBG_MGR );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_MGR,
		("Deregistering CA guid %I64x.\n", ca_guid) );

	/* Locate the CA. */
	cl_spinlock_acquire( &gp_al_mgr->obj.lock );
	p_ci_ca = find_ci_ca( ca_guid );
	cl_spinlock_release( &gp_al_mgr->obj.lock );

	if( !p_ci_ca )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("CA not found.\n") );
		return IB_NOT_FOUND;
	}

	/*
	 * TODO: Before destroying, do a query PnP call and return IB_BUSY
	 * as needed.
	 */
	/* Destroy the CI CA. */
	ref_al_obj( &p_ci_ca->obj );
	p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}


/*
 * Initialize a proxy entry used to map user-mode to kernel-mode resources.
 */
static cl_status_t
__init_hdl(
	IN		void* const						p_element,
	IN		void*							context )
{
	al_handle_t		*p_h;

	p_h = (al_handle_t*)p_element;

	/* Chain free entries one after another. */
	p_h->p_obj = (al_obj_t*)(uintn_t)++(((ib_al_handle_t)context)->free_hdl);
	p_h->type = AL_OBJ_TYPE_UNKNOWN;

	return CL_SUCCESS;
}


/*
 * Create a new instance of the access layer.  This function is placed here
 * to prevent sharing the implementation with user-mode.
 */
static ib_api_status_t
__ib_open_al(
	IN			wchar_t 					*tag,
		OUT			ib_al_handle_t* const		ph_al )
{
	ib_al_handle_t			h_al;
	ib_api_status_t			status;
	cl_status_t				cl_status;

	AL_ENTER( AL_DBG_MGR );

	if( !ph_al )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate an access layer instance. */
	h_al = cl_zalloc( sizeof( ib_al_t ) );
	if( !h_al )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("cl_zalloc failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the instance. */
	construct_al_obj( &h_al->obj, AL_OBJ_TYPE_H_AL );
	cl_spinlock_construct( &h_al->mad_lock );
	cl_qlist_init( &h_al->mad_list );
	cl_qlist_init( &h_al->key_list );
	cl_qlist_init( &h_al->query_list );
	cl_qlist_init( &h_al->cep_list );

	cl_vector_construct( &h_al->hdl_vector );

	cl_status = cl_spinlock_init( &h_al->mad_lock );
	if( cl_status != CL_SUCCESS )
	{
		free_al( &h_al->obj );
		AL_EXIT( AL_DBG_MGR );
		return ib_convert_cl_status( cl_status );
	}

	/* set caller name */
	h_al->obj.p_caller_id = tag;
	
	/* Initialize the handle vector. */
	cl_status = cl_vector_init( &h_al->hdl_vector, AL_HDL_VECTOR_MIN,
		AL_HDL_VECTOR_GROW, sizeof(al_handle_t), __init_hdl, NULL, h_al );
	if( cl_status != CL_SUCCESS )
	{
		free_al( &h_al->obj );
		AL_EXIT( AL_DBG_MGR );
		return ib_convert_cl_status( cl_status );
	}
	h_al->free_hdl = 1;

	/* Initialize the base object. */
	status = init_al_obj( &h_al->obj, NULL, FALSE,
		destroying_al, NULL, free_al );
	if( status != IB_SUCCESS )
	{
		free_al( &h_al->obj );
		AL_EXIT( AL_DBG_MGR );
		return status;
	}
	status = attach_al_obj( &gp_al_mgr->obj, &h_al->obj );
	if( status != IB_SUCCESS )
	{
		h_al->obj.pfn_destroy( &h_al->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/*
	 * Self reference the AL instance so that all attached objects
	 * insert themselve in the instance's handle manager automatically.
	 */
	h_al->obj.h_al = h_al;

	*ph_al = h_al;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &h_al->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_MGR );
	return IB_SUCCESS;
}

ib_api_status_t
ib_open_al(
		OUT			ib_al_handle_t* const		ph_al )
{
	return __ib_open_al(NULL, ph_al);
}

ib_api_status_t
ib_open_al_trk(
	IN			wchar_t 					*tag,
		OUT			ib_al_handle_t* const		ph_al )
{
	return __ib_open_al(tag, ph_al);
}


uint64_t
al_hdl_insert(
	IN		const	ib_al_handle_t				h_al,
	IN				void* const					p_obj,
	IN		const	uint32_t					type )
{
	cl_status_t		status;
	size_t			size;
	uint64_t		hdl;
	al_handle_t		*p_h;

	AL_ENTER( AL_DBG_HDL );

	size = cl_vector_get_size( &h_al->hdl_vector );
	hdl = h_al->free_hdl;
	if( h_al->free_hdl == size )
	{
		/* Grow the vector pool. */
		status =
			cl_vector_set_size( &h_al->hdl_vector, size + AL_HDL_VECTOR_GROW );
		if( status != CL_SUCCESS )
		{
			AL_EXIT( AL_DBG_HDL );
			return AL_INVALID_HANDLE;
		}
		/*
		 * Return the the start of the free list since the
		 * entry initializer incremented it.
		 */
		h_al->free_hdl = size;
	}

	/* Get the next free entry. */
	p_h = (al_handle_t*)cl_vector_get_ptr( &h_al->hdl_vector, (size_t)hdl );

	/* Update the next entry index. */
	h_al->free_hdl = (size_t)p_h->p_obj;

	/* Update the entry. */
	p_h->type = type;
	p_h->p_obj = (al_obj_t*)p_obj;

	return hdl;
}


void
al_hdl_free(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl )
{
	al_handle_t			*p_h;

	CL_ASSERT( hdl < cl_vector_get_size( &h_al->hdl_vector ) );

	p_h = (al_handle_t*)cl_vector_get_ptr( &h_al->hdl_vector, (size_t)hdl );
	p_h->type = AL_OBJ_TYPE_UNKNOWN;
	p_h->p_obj = (al_obj_t*)(uintn_t)h_al->free_hdl;
	h_al->free_hdl = hdl;
}


al_obj_t*
al_hdl_ref(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type )
{
	al_handle_t			*p_h;
	al_obj_t			*p_obj;

	CL_ASSERT( type != AL_OBJ_TYPE_H_MAD && type != AL_OBJ_TYPE_H_CONN );

	cl_spinlock_acquire( &h_al->obj.lock );

	/* Validate index. */
	if( hdl >= cl_vector_get_size( &h_al->hdl_vector ) )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}

	/* Get the specified entry. */
	p_h = (al_handle_t*)cl_vector_get_ptr( &h_al->hdl_vector, (size_t)hdl );

	/* Make sure that the handle is valid and the correct type. */
	if( type == AL_OBJ_TYPE_UNKNOWN &&
		p_h->type != AL_OBJ_TYPE_H_PD && p_h->type != AL_OBJ_TYPE_H_CQ &&
		p_h->type != AL_OBJ_TYPE_H_AV && p_h->type != AL_OBJ_TYPE_H_QP &&
		p_h->type != AL_OBJ_TYPE_H_MR && p_h->type != AL_OBJ_TYPE_H_MW &&
		p_h->type != AL_OBJ_TYPE_H_SRQ )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}
	else if( p_h->type != type )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}

	p_obj = p_h->p_obj;
	if( !p_obj->hdl_valid )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}
	ref_al_obj( p_obj );
	cl_spinlock_release( &h_al->obj.lock );
	return p_obj;
}


void*
al_hdl_chk(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type )
{
	al_handle_t			*p_h;

	/* Validate index. */
	if( hdl >= cl_vector_get_size( &h_al->hdl_vector ) )
		return NULL;

	/* Get the specified entry. */
	p_h = (al_handle_t*)cl_vector_get_ptr( &h_al->hdl_vector, (size_t)hdl );

	/* Make sure that the handle is valid and the correct type. */
	if( (p_h->type != type) )
		return NULL;

	return p_h->p_obj;
}


void*
al_hdl_get(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type )
{
	al_handle_t			*p_h;
	void				*p_obj;

	cl_spinlock_acquire( &h_al->obj.lock );

	/* Validate index. */
	if( hdl >= cl_vector_get_size( &h_al->hdl_vector ) )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}

	/* Get the specified entry. */
	p_h = (al_handle_t*)cl_vector_get_ptr( &h_al->hdl_vector, (size_t)hdl );

	/* Make sure that the handle is valid and the correct type. */
	if( (p_h->type != type) )
	{
		cl_spinlock_release( &h_al->obj.lock );
		return NULL;
	}

	p_obj = (void*)p_h->p_obj;

	/* Clear the entry. */
	p_h->type = AL_OBJ_TYPE_UNKNOWN;
	p_h->p_obj = (al_obj_t*)(uintn_t)h_al->free_hdl;
	h_al->free_hdl = hdl;

	cl_spinlock_release( &h_al->obj.lock );
	return p_obj;
}

