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

#if !defined(__AL_H__)
#define __AL_H__

#include <complib/cl_event.h>
#include <complib/cl_qlist.h>
#include <complib/cl_ptr_vector.h>
#include <complib/cl_spinlock.h>

#include "al_common.h"
#include "al_mad_pool.h"
#include "al_query.h"
#include "al_reg_svc.h"
#ifdef CL_KERNEL
#include "al_proxy.h"
#endif



typedef struct _al_handle
{
	uint32_t		type;
	al_obj_t		*p_obj;

}	al_handle_t;


#define AL_INVALID_HANDLE	0


/*
 * AL instance structure.
 */
typedef struct _ib_al
{
	al_obj_t					obj;

	/* Asynchronous processing item used to deregister services with the SA. */
	cl_async_proc_item_t		dereg_svc_async;

	cl_qlist_t					mad_list;
	/*
	 * The MAD list must have a dedicated lock protecting it to prevent
	 * deadlocks.  The MAD service gets/puts MADs from/to pools, which
	 * needs to track the MAD in the associated AL instance's mad_list.
	 * When cancelling SA requests, the AL instance's object lock is held
	 * and MAD cancellation takes the MAD service's lock.
	 */
	cl_spinlock_t				mad_lock;

	cl_qlist_t					key_list;
	cl_qlist_t					query_list;
	cl_qlist_t					cep_list;

#ifdef CL_KERNEL
	/* Handle manager is only needed in the kernel. */
	cl_vector_t					hdl_vector;
	uint64_t					free_hdl;

	/* Proxy context. */
	al_dev_open_context_t		*p_context;
#endif

}	ib_al_t;



ib_api_status_t
init_al(
	IN				al_obj_t					*p_parent_obj,
	IN		const	ib_al_handle_t				h_al );


void
destroying_al(
	IN				al_obj_t					*p_obj );


void
free_al(
	IN				al_obj_t					*p_obj );



/*
 * Insert a pnp registration in the PnP vector.
 */
ib_api_status_t
al_insert_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_handle_t				h_pnp );

/*
 * Remove a pnp registration from the PnP vector.
 */
void
al_remove_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_handle_t				h_pnp );


void
al_insert_mad(
	IN		const	ib_al_handle_t				h_al,
	IN				al_mad_element_t*	const	p_mad );


void
al_remove_mad(
	IN				al_mad_element_t*	const	p_mad );


void
al_handoff_mad(
	IN		const	ib_al_handle_t				h_al,
	IN				ib_mad_element_t*	const	p_mad_element );


void
al_insert_key(
	IN		const	ib_al_handle_t				h_al,
	IN				al_pool_key_t* const		p_pool_key );


void
al_remove_key(
	IN				al_pool_key_t* const		p_pool_key );


void
al_dereg_pool(
	IN		const	ib_al_handle_t				h_al,
	IN				ib_pool_handle_t const		h_pool );


void
al_insert_query(
	IN		const	ib_al_handle_t				h_al,
	IN				al_query_t* const			p_query );


void
al_remove_query(
	IN				al_query_t* const			p_query );

void
al_insert_conn(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_cm_handle_t				h_conn );

void
al_remove_conn(
	IN		const	ib_cm_handle_t				h_conn );

#ifdef CL_KERNEL
// TODO: Once all things in the handle vector are al_obj_t,
// TODO: we can remove the type parameter.
uint64_t
al_hdl_insert(
	IN		const	ib_al_handle_t				h_al,
	IN				void* const					p_obj,
	IN		const	uint32_t					type );


static inline uint64_t
al_hdl_lock_insert(
	IN		const	ib_al_handle_t				h_al,
	IN				void* const					p_obj,
	IN		const	uint32_t					type )
{
	uint64_t	hdl;
	cl_spinlock_acquire( &h_al->obj.lock );
	hdl = al_hdl_insert( h_al, p_obj, type );
	cl_spinlock_release( &h_al->obj.lock );
	return hdl;
}


void
al_hdl_free(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl );


static inline uint64_t
al_hdl_insert_obj(
	IN				al_obj_t* const				p_obj )
{
	uint64_t		hdl;

	CL_ASSERT( p_obj->h_al );

	cl_spinlock_acquire( &p_obj->h_al->obj.lock );
	hdl = al_hdl_insert( p_obj->h_al, p_obj, AL_BASE_TYPE( p_obj->type ) );
	cl_spinlock_release( &p_obj->h_al->obj.lock );

	return hdl;
}


static inline void
al_hdl_free_obj(
	IN				al_obj_t* const				p_obj )
{
	CL_ASSERT( p_obj->h_al );
	CL_ASSERT( p_obj->hdl != AL_INVALID_HANDLE );
	cl_spinlock_acquire( &p_obj->h_al->obj.lock );

	al_hdl_free( p_obj->h_al, p_obj->hdl );
	p_obj->hdl = AL_INVALID_HANDLE;
	p_obj->hdl_valid = FALSE;

	cl_spinlock_release( &p_obj->h_al->obj.lock );
}


al_obj_t*
al_hdl_ref(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type );

/* Validate an object. */
void*
al_hdl_chk(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type );

/* Validate and remove an object. */
void*
al_hdl_get(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					type );

/* Validate and removes a MAD element. */
static inline ib_mad_element_t*
al_hdl_get_mad(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl )
{
	return (ib_mad_element_t*)al_hdl_get( h_al, hdl, AL_OBJ_TYPE_H_MAD );
}

/* Validate and reference a connection.  Used for MRA */
struct _al_conn*
al_hdl_ref_conn(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					sub_type );

/* Validate, reference, and remove a connection. */
struct _al_conn*
al_hdl_get_conn(
	IN		const	ib_al_handle_t				h_al,
	IN		const	uint64_t					hdl,
	IN		const	uint32_t					sub_type );

/* free all outstanding mads from this al mad_list */
void
free_outstanding_mads(	
	IN				const	ib_al_handle_t		h_al );


#endif	/* CL_KERNEL */

#endif	/* __AL_H__ */
