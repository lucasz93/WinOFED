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


#include "al_mad_pool.h"
#include "al_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ual_mad_pool.tmh"
#endif




/*
 * Function prototypes.
 */
static void
__ual_free_pool_key(
	IN				al_obj_t*					p_obj );


/*
 * Register a MAD pool with a protection domain.
 */
ib_api_status_t
ual_reg_global_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
		OUT			ib_pool_key_t* const		pp_pool_key )
{
	al_pool_key_t*			p_pool_key;
	ib_api_status_t			status;

	AL_ENTER(AL_DBG_MAD_POOL);

	if( AL_OBJ_INVALID_HANDLE( h_pool, AL_OBJ_TYPE_H_MAD_POOL ) )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}
	if( !pp_pool_key )
	{
		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR , ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	/* Allocate a pool key structure. */
	p_pool_key = cl_zalloc( sizeof( al_pool_key_t ) );
	if( !p_pool_key )
		return IB_INSUFFICIENT_MEMORY;

	/* Initialize the pool key. */
	construct_al_obj( &p_pool_key->obj, AL_OBJ_TYPE_H_POOL_KEY );
	p_pool_key->type = AL_KEY_ALIAS;
	p_pool_key->h_pool = h_pool;
	p_pool_key->h_pd = NULL;

	/* Initialize the pool key object. */
	status = init_al_obj( &p_pool_key->obj, p_pool_key, TRUE,
		NULL, NULL, __ual_free_pool_key );
	if( status != IB_SUCCESS )
	{
		__ual_free_pool_key( &p_pool_key->obj );

		AL_PRINT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR ,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	status = attach_al_obj( &h_pool->obj, &p_pool_key->obj );
	if( status != IB_SUCCESS )
	{
		p_pool_key->obj.pfn_destroy( &p_pool_key->obj, NULL );
		AL_PRINT_EXIT(TRACE_LEVEL_ERROR ,AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}
	p_pool_key->h_al = p_pool_key->obj.h_al;

	/* Return the pool key. */
	*pp_pool_key = (ib_pool_key_t)p_pool_key;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_pool_key->obj, E_REF_INIT );

	AL_EXIT(AL_DBG_MAD_POOL);
	return IB_SUCCESS;
}



/*
 * Free a pool key.
 */
static void
__ual_free_pool_key(
	IN				al_obj_t*					p_obj )
{
	al_pool_key_t*			p_pool_key;

	CL_ASSERT( p_obj );
	p_pool_key = PARENT_STRUCT( p_obj, al_pool_key_t, obj );

	CL_ASSERT( !p_pool_key->mad_cnt );
	destroy_al_obj( &p_pool_key->obj );
	cl_free( p_pool_key );
}

