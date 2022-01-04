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


#include "al.h"
#include "al_mgr.h"
#include "al_ci_ca.h"
#include "ual_ca.h"
#include "al_pnp.h"
#include "al_pd.h"
#include "ib_common.h"


#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_ci_ca.tmh"
#endif


extern ib_pool_handle_t		gh_mad_pool;
extern ib_al_handle_t		gh_al;
extern cl_async_proc_t		*gp_async_proc_mgr;

#define	EVENT_POOL_MIN			4
#define	EVENT_POOL_MAX			0
#define	EVENT_POOL_GROW			1


static void
ci_ca_async_proc_cb(
	IN				struct _cl_async_proc_item	*p_item );

void
destroying_ci_ca(
	IN				al_obj_t*					p_obj );

void
cleanup_ci_ca(
	IN				al_obj_t*					p_obj );

/* To be called only if a CI is not opened yet by UAL */

/* This gets called by ual_mgr when a CA is opened for the first time.
 * The CA remains open for the process life-time.
 * ib_open_ca will not go through this code.
 */

ib_api_status_t
create_ci_ca(
	IN ib_al_handle_t						h_al,
	IN al_obj_t								*p_parent_obj,
	IN ib_net64_t							ca_guid )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER(AL_DBG_CA);

	/* Allocate a new CA structure. */
	p_ci_ca = (al_ci_ca_t *)cl_zalloc( sizeof( al_ci_ca_t ) );
	if( p_ci_ca == NULL )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("Failed to cl_malloc al_ci_ca_t\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Construct the CI CA */
	construct_al_obj( &p_ci_ca->obj, AL_OBJ_TYPE_CI_CA );
	cl_qlist_init( &p_ci_ca->ca_list );
	cl_qpool_construct( &p_ci_ca->event_pool );
	cl_spinlock_construct( &p_ci_ca->attr_lock );

	cl_status = cl_spinlock_init( &p_ci_ca->attr_lock );
	if( cl_status != CL_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("cl_spinlock_init failed, status = 0x%x.\n",
			ib_convert_cl_status(cl_status) ) );
		return ib_convert_cl_status( cl_status );
	}

	/* Create a pool of items to report asynchronous events. */
	cl_status = cl_qpool_init( &p_ci_ca->event_pool, EVENT_POOL_MIN,
		EVENT_POOL_MAX, EVENT_POOL_GROW, sizeof( event_item_t ), NULL,
		NULL, p_ci_ca );
	if( cl_status != CL_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("cl_qpool_init failed, status = 0x%x.\n",
			ib_convert_cl_status(cl_status) ) );
		return ib_convert_cl_status( cl_status );
	}

	/* Initialize the al object and attach it to the parent so that the
	 * cleanups will work fine on all cases including error conditions
	 * encountered here.  We use synchronous destruction to ensure that
	 * the internal CA handle is destroyed before the global AL instance
	 * is destroyed during the shutdown procedure.
	 */
	status = init_al_obj( &p_ci_ca->obj, p_ci_ca, FALSE,
		destroying_ci_ca, cleanup_ci_ca, free_ci_ca );
	if( status != IB_SUCCESS )
	{
		free_ci_ca( &p_ci_ca->obj );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed, status = 0x%x.\n", status) );
		return status;
	}

	attach_al_obj( p_parent_obj, &p_ci_ca->obj );

	p_ci_ca->dereg_async_item.pfn_callback = ci_ca_async_proc_cb;

	/* We need to open a CA and allocate a PD for internal use.  Doing this
	 * will result in the creation of user-mode and kernel-mode objects that
	 * will be children under our global AL instance and associated kernel AL
	 * instance.  We need to take a reference on our user-mode AL instance
	 * while the CI CA exists, to ensure that our kernel-mode counterpart
	 * does not go away during application exit.
	 */
	ref_al_obj( &gh_al->obj );

	/* Register ourselves with the AL manager, so that the open call below
	 * will succeed.
	 */
	add_ci_ca( p_ci_ca );
	open_vendor_lib( ca_guid, &p_ci_ca->verbs );

	/* Now open the UAL CA to be assigned to p_ci_ca */
	status = ib_open_ca( h_al, ca_guid, ca_event_cb, p_ci_ca,
		&p_ci_ca->h_ca );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ib_open_ca failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Now open the CA by sending the ioctl down to kernel */
	status = ual_open_ca( ca_guid, p_ci_ca );
	if (status != IB_SUCCESS)
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );

		/* Note that we don't release it here.
		 * It is done through async queuing and the callback
		 * and the associated destroy/cleanup in the AL's
		 * object model
		 */
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ual_open_ca failed, status = 0x%x.\n", status) );
		return IB_ERROR;
	}

	/* Increase the max timeout for the CI CA to handle driver unload. */
	set_al_obj_timeout( &p_ci_ca->obj, AL_MAX_TIMEOUT_MS );

	/*
	 * Allocate a PD for use by AL itself.  Note that we need to use the
	 * PD in the kernel, so we create an alias PD for the global PD.
	 */
	status = ib_alloc_pd( p_ci_ca->h_ca, IB_PDT_ALIAS, p_ci_ca,
		&p_ci_ca->h_pd );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ib_alloc_pd failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Allocate an alias in the kernel for this global PD alias. */
	status = ual_allocate_pd( p_ci_ca->h_ca, IB_PDT_ALIAS, p_ci_ca->h_pd );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("ual_allocate_pd returned %s\n", ib_get_err_str( status )) );
		return status;
	}

	/* Now create an alias PD in user-mode for AL services. */
	status = ib_alloc_pd( p_ci_ca->h_ca, IB_PDT_ALIAS, p_ci_ca,
		&p_ci_ca->h_pd_alias );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ib_alloc_pd failed, status = 0x%x.\n", status) );
		return status;
	}

	status = get_port_info( p_ci_ca );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("get_port_info failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Register the global MAD pool on this CA. */
	status = ib_reg_mad_pool( gh_mad_pool, p_ci_ca->h_pd, &p_ci_ca->pool_key );
	if( status != IB_SUCCESS )
	{
		p_ci_ca->obj.pfn_destroy( &p_ci_ca->obj, NULL );
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("ib_reg_mad_pool failed, status = 0x%x.\n", status) );
		return status;
	}

	/* Update the PnP attributes buffer. */
	ci_ca_update_attr( p_ci_ca, NULL );

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_ci_ca->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_CA);
	return IB_SUCCESS;
}



static void
ci_ca_async_proc_cb(
	IN				struct _cl_async_proc_item	*p_item )
{
	al_ci_ca_t			*p_ci_ca;

	p_ci_ca = PARENT_STRUCT( p_item, al_ci_ca_t, dereg_async_item );

	/* Release all AL resources acquired by the CI CA. */
	ib_close_ca( p_ci_ca->h_ca, NULL );
}



/*
 * This overrides the implementation in shared AL
 * UAL-specific destroy_ci_ca
 */
void
destroying_ci_ca(
	IN				al_obj_t*					p_obj )
{
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER(AL_DBG_CA);
	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	/*
	 * We queue a request to the asynchronous processing manager to close
	 * the CA after the PNP remove CA event has been delivered.  This avoids
	 * the ib_close_ca() call from immediately removing resouces (PDs, QPs)
	 * that are in use by clients waiting on the remove CA event.
	 */
	if( p_ci_ca->h_ca )
		cl_async_proc_queue( gp_async_pnp_mgr, &p_ci_ca->dereg_async_item );

	AL_EXIT(AL_DBG_CA);
}



/*
 * This overrides the implementation in shared AL
 *
 * Remove H/W resource used. From UAL perspective, it is the UVP lib
 * UAL-specific
 */
void
cleanup_ci_ca(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t		status;
	al_ci_ca_t			*p_ci_ca;

	AL_ENTER(AL_DBG_CA);

	CL_ASSERT( p_obj );
	p_ci_ca = PARENT_STRUCT( p_obj, al_ci_ca_t, obj );

	if( p_ci_ca->h_ca )
	{
		/* Remove the associated kernel CA object. */
		status = ual_close_ca( p_ci_ca );
		CL_ASSERT( status == IB_SUCCESS );
	}

	remove_ci_ca( p_ci_ca );

	/* We have finished cleaning up all associated kernel resources.  We can
	 * now safely dereference the global AL instance.
	 */
	deref_al_obj( &gh_al->obj );

	close_vendor_lib( &p_ci_ca->verbs );

	AL_EXIT(AL_DBG_CA);
}
