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


/*
 * Abstract:
 *	Implementation of asynchronous request manager.
 *
 * Environment:
 *	All
 */


#include <complib/cl_reqmgr.h>
#include <complib/cl_memory.h>


/* minimum number of objects to allocate */
#define REQ_MGR_START_SIZE 10
/* minimum number of objects to grow */
#define REQ_MGR_GROW_SIZE 10


/****i* Component Library: Request Manager/cl_request_object_t
* NAME
*	cl_request_object_t
*
* DESCRIPTION
*	Request manager structure.
*
*	The cl_request_object_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_request_object
{
	cl_pool_item_t	pool_item;
	size_t			count;
	boolean_t		partial_ok;
	cl_pfn_req_cb_t	pfn_callback;
	const void		*context1;
	const void		*context2;

} cl_request_object_t;
/*
* FIELDS
*	pool_item
*		Pool item to store request in a pool or list.
*
*	count
*		Number of items requested.
*
*	partial_ok
*		Is it okay to return some of the items.
*
*	pfn_callback
*		Notification routine when completed.
*
*	context1
*		Callback context information.
*
*	context2
*		Callback context information.
*
* SEE ALSO
*	Overview
*********/


void
cl_req_mgr_construct(
	IN	cl_req_mgr_t* const	p_req_mgr )
{
	CL_ASSERT( p_req_mgr );

	/* Clear the structure. */
	cl_memclr( p_req_mgr, sizeof(cl_req_mgr_t) );

	/* Initialize the state of the free request stack. */
	cl_qpool_construct( &p_req_mgr->request_pool );
}


cl_status_t
cl_req_mgr_init(
	IN	cl_req_mgr_t* const			p_req_mgr,
	IN	cl_pfn_reqmgr_get_count_t	pfn_get_count,
	IN	const void* const			get_context )
{
	cl_status_t		status;

	CL_ASSERT( p_req_mgr );
	CL_ASSERT( pfn_get_count );

	cl_qlist_init( &p_req_mgr->request_queue );

	status = cl_qpool_init( &p_req_mgr->request_pool, REQ_MGR_START_SIZE, 0,
		REQ_MGR_GROW_SIZE, sizeof(cl_request_object_t), NULL, NULL, NULL );

	if( status != CL_SUCCESS )
		return( status );

	/* Store callback information for the count function. */
	p_req_mgr->pfn_get_count = pfn_get_count;
	p_req_mgr->get_context = get_context;

	return( CL_SUCCESS );
}


void
cl_req_mgr_destroy(
	IN	cl_req_mgr_t* const	p_req_mgr )
{
	CL_ASSERT( p_req_mgr );

	/* Return all requests to the grow pool. */
	if( cl_is_qpool_inited( &p_req_mgr->request_pool ) )
	{
		cl_qpool_put_list( &p_req_mgr->request_pool,
			&p_req_mgr->request_queue );
	}

	cl_qpool_destroy( &p_req_mgr->request_pool );
}


cl_status_t
cl_req_mgr_get(
	IN		cl_req_mgr_t* const	p_req_mgr,
	IN OUT	size_t* const		p_count,
	IN		const cl_req_type_t	req_type,
	IN		cl_pfn_req_cb_t		pfn_callback,
	IN		const void* const	context1,
	IN		const void* const	context2 )
{
	size_t					available_count;
	size_t					count;
	cl_request_object_t		*p_request;

	CL_ASSERT( p_req_mgr );
	CL_ASSERT( cl_is_qpool_inited( &p_req_mgr->request_pool ) );
	CL_ASSERT( p_count );
	CL_ASSERT( *p_count );

	/* Get the number of available objects in the grow pool. */
	available_count =
		p_req_mgr->pfn_get_count( (void*)p_req_mgr->get_context );

	/*
	 * Check to see if there is nothing on the queue, and there are
	 * enough items to satisfy the whole request.
	 */
	if( cl_is_qlist_empty( &p_req_mgr->request_queue ) &&
		*p_count <= available_count )
	{
		return( CL_SUCCESS );
	}

	if( req_type == REQ_GET_SYNC )
		return( CL_INSUFFICIENT_RESOURCES );

	/* We need a request object to place on the request queue. */
	p_request = (cl_request_object_t*)
		cl_qpool_get( &p_req_mgr->request_pool );

	if( !p_request )
		return( CL_INSUFFICIENT_MEMORY );

	/*
	 * We can return the available number of objects but we still need
	 * to queue a request for the remainder.
	 */
	if( req_type == REQ_GET_PARTIAL_OK &&
		cl_is_qlist_empty( &p_req_mgr->request_queue ) )
	{
		count = *p_count - available_count;
		*p_count = available_count;
		p_request->partial_ok = TRUE;
	}
	else
	{
		/*
		 * We cannot return any objects.  We queue a request for
		 * all of them.
		 */
		count = *p_count;
		*p_count = 0;
		p_request->partial_ok = FALSE;
	}

	/* Set the request fields and enqueue it. */
	p_request->pfn_callback = pfn_callback;
	p_request->context1 = context1;
	p_request->context2 = context2;
	p_request->count = count;

	cl_qlist_insert_tail( &p_req_mgr->request_queue,
		&p_request->pool_item.list_item );

	return( CL_PENDING );
}


cl_status_t
cl_req_mgr_resume(
	IN	cl_req_mgr_t* const		p_req_mgr,
	OUT	size_t* const			p_count,
	OUT	cl_pfn_req_cb_t* const	ppfn_callback,
	OUT	const void** const		p_context1,
	OUT	const void** const		p_context2 )
{
	size_t					available_count;
	cl_request_object_t		*p_queued_request;

	CL_ASSERT( p_req_mgr );
	CL_ASSERT( cl_is_qpool_inited( &p_req_mgr->request_pool ) );

	/* If no requests are pending, there's nothing to return. */
	if( cl_is_qlist_empty( &p_req_mgr->request_queue ) )
		return( CL_NOT_DONE );

	/*
	 * Get the item at the head of the request queue,
	 * but do not remove it yet.
	 */
	p_queued_request = (cl_request_object_t*)
		cl_qlist_head( &p_req_mgr->request_queue );

	*ppfn_callback = p_queued_request->pfn_callback;
	*p_context1 = p_queued_request->context1;
	*p_context2 = p_queued_request->context2;

	available_count =
		p_req_mgr->pfn_get_count( (void*)p_req_mgr->get_context );

	/* See if the request can be fulfilled. */
	if( p_queued_request->count > available_count )
	{
		if( !p_queued_request->partial_ok )
			return( CL_INSUFFICIENT_RESOURCES );

		p_queued_request->count -= available_count;

		*p_count = available_count;
		return( CL_PENDING );
	}

	*p_count = p_queued_request->count;

	/* The entire request can be met.  Remove it from the request queue. */
	cl_qlist_remove_head( &p_req_mgr->request_queue );

	/* Return the internal request object to the free stack. */
	cl_qpool_put( &p_req_mgr->request_pool,
		&p_queued_request->pool_item );
	return( CL_SUCCESS );
}
