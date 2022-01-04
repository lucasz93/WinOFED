/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_init.tmh"
#endif

#include "al_dev.h"
#include "al_init.h"
#include "al_mgr.h"

#include "ib_common.h"


#if DBG
#include "al_ref_trace.h"
#endif


uint32_t				g_al_dbg_level = TRACE_LEVEL_ERROR;
uint32_t				g_al_dbg_flags = 0xf0;
/*
 * Device driver initialization routine.
 */
ib_api_status_t
al_initialize( void )
{
	cl_status_t		cl_status;
	ib_api_status_t	status = IB_ERROR;

	AL_ENTER( AL_DBG_DEV );
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV, ("Hello World! =)\n") );

	/*
	 * Initialize access layer services.
	 */
#if AL_OBJ_PRIVATE_ASYNC_PROC
	gp_async_proc_mgr = cl_malloc( sizeof(cl_async_proc_t) * 3 );
#else
	gp_async_proc_mgr = cl_malloc( sizeof(cl_async_proc_t) * 2 );
#endif
	if( !gp_async_proc_mgr )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("alloc_async_proc failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
	gp_async_pnp_mgr = gp_async_proc_mgr + 1;
	cl_async_proc_construct( gp_async_proc_mgr );
	cl_async_proc_construct( gp_async_pnp_mgr );
#if AL_OBJ_PRIVATE_ASYNC_PROC
	gp_async_obj_mgr = gp_async_proc_mgr + 2;
	cl_async_proc_construct( gp_async_obj_mgr );
	cl_status = cl_async_proc_init( gp_async_obj_mgr, 1, "AL_OBJ" );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to init async_obj_mgr: status = 0x%x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}
#endif
	cl_status = cl_async_proc_init( gp_async_proc_mgr, 1, "AL_MISC" );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to init async_proc_mgr: status = 0x%x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	cl_status = cl_async_proc_init( gp_async_pnp_mgr, 1, "AL_PNP" );
	if( cl_status != CL_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to init async_pnp_mgr: status = 0x%x.\n", cl_status) );
		return ib_convert_cl_status( cl_status );
	}

#if DBG
	ref_trace_db_init(EQUAL_REF_DEREF_PAIRS | SAME_REF_OR_DEREF_ENTRY);
	g_av_trace.trace_index = 0;
	g_mad_trace.trace_index = 0;
#endif

	status = create_al_mgr();
	if( status != IB_SUCCESS )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_mgr: status = 0x%x.\n", status) );
		return status;
	}

	AL_EXIT( AL_DBG_DEV );
	return status;
}



/*
 * Device driver cleanup routine.
 */
void
al_cleanup( void )
{
	AL_ENTER( AL_DBG_DEV );

	/*
	 * Destroy access layer device interface.
	 */
	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV, ("Destroying %s device.\n",
		(const char *)AL_DEVICE_NAME) );

	/*
	 * Destroy access layer services.
	 */
	if( gp_al_mgr )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV, ("Destroying AL Mgr.\n") );
		ref_al_obj( &gp_al_mgr->obj );
		gp_al_mgr->obj.pfn_destroy( &gp_al_mgr->obj, NULL );
	}

#if AL_OBJ_PRIVATE_ASYNC_PROC
	if( gp_async_obj_mgr )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
			("Destroying async obj mgr.\n") );
		cl_async_proc_destroy( gp_async_obj_mgr );
		gp_async_obj_mgr = NULL;
	}
#endif

	if( gp_async_pnp_mgr )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
			("Destroying async pnp mgr.\n") );
		cl_async_proc_destroy( gp_async_pnp_mgr );
		gp_async_pnp_mgr = NULL;
	}

	if( gp_async_proc_mgr )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
			("Destroying async proc mgr.\n") );
		cl_async_proc_destroy( gp_async_proc_mgr );
		cl_free( gp_async_proc_mgr );
		gp_async_proc_mgr = NULL;
	}

	AL_PRINT_EXIT( TRACE_LEVEL_WARNING, AL_DBG_DEV, ("Goodbye Cruel World =(\n") );
}
