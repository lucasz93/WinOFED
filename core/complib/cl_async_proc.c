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



#include <complib/cl_async_proc.h>
#include "complib/cl_debug.h"

#define CL_ASYNC_PROC_MIN		16
#define CL_ASYNC_PROC_GROWSIZE	16


/* Worker function declaration. */
static void
__cl_async_proc_worker(
	IN	void* const	context );


void
cl_async_proc_construct(
	IN	cl_async_proc_t* const	p_async_proc )
{
	CL_ASSERT( p_async_proc );

	cl_qlist_init( &p_async_proc->item_queue );
	cl_spinlock_construct( &p_async_proc->lock );
	cl_thread_pool_construct( &p_async_proc->thread_pool );
	p_async_proc->state = CL_UNINITIALIZED;
}


cl_status_t
cl_async_proc_init(
	IN	cl_async_proc_t* const	p_async_proc,
	IN	const uint32_t			thread_count,
	IN	const char* const		name )
{
	cl_status_t		status;

	CL_ASSERT( p_async_proc );

	cl_async_proc_construct( p_async_proc );

	status = cl_spinlock_init( &p_async_proc->lock );
	if( status != CL_SUCCESS )
	{
		cl_async_proc_destroy( p_async_proc );
		return( status );
	}

	status = cl_thread_pool_init( &p_async_proc->thread_pool, thread_count,
		__cl_async_proc_worker, p_async_proc, name );
	if( status != CL_SUCCESS )
	{
		cl_async_proc_destroy( p_async_proc );
		return (status);
	}

	p_async_proc->state = CL_INITIALIZED;
	return( status );
}


void
cl_async_proc_destroy(
	IN	cl_async_proc_t* const	p_async_proc )
{
	/* Destroy the thread pool first so that the threads stop. */
	cl_thread_pool_destroy( &p_async_proc->thread_pool );

	/* Flush all queued callbacks. */
	if( p_async_proc->state == CL_INITIALIZED )
		__cl_async_proc_worker( p_async_proc );

	/* Destroy the spinlock. */
	cl_spinlock_destroy( &p_async_proc->lock );

	p_async_proc->state = CL_DESTROYED;
}


void
cl_async_proc_queue(
	IN	cl_async_proc_t* const		p_async_proc,
	IN	cl_async_proc_item_t* const	p_item )
{
	CL_ASSERT( p_async_proc );
	CL_ASSERT( p_item->pfn_callback );
	CL_ASSERT( p_async_proc->state == CL_INITIALIZED );

	/* Enqueue this item for processing. */
	cl_spinlock_acquire( &p_async_proc->lock );
	cl_qlist_insert_tail( &p_async_proc->item_queue,
		&p_item->pool_item.list_item );
	cl_spinlock_release( &p_async_proc->lock );

	/* Signal the thread pool to wake up. */
	cl_thread_pool_signal( &p_async_proc->thread_pool );
}

#pragma warning(disable:4127) //conditional expression is constant
static void
__cl_async_proc_worker(
	IN	void* const	context)
{
	cl_async_proc_t			*p_async_proc = (cl_async_proc_t*)context;
	cl_list_item_t			*p_list_item;
	cl_async_proc_item_t	*p_item;

	uint64_t start_time = 0;
	uint64_t end_time = 0;
	uint64_t total_time = 0;
	cl_pfn_async_proc_cb_t	pfn_callback = NULL;

	CL_ASSERT( p_async_proc->state == CL_INITIALIZED );

	/* Process items from the head of the queue until it is empty. */
	cl_spinlock_acquire( &p_async_proc->lock );
	p_list_item = cl_qlist_remove_head( &p_async_proc->item_queue );
	while( p_list_item != cl_qlist_end( &p_async_proc->item_queue ) )
	{
		/* Release the lock during the user's callback. */
		cl_spinlock_release( &p_async_proc->lock );

		/* Invoke the user callback. */
		p_item = (cl_async_proc_item_t*)p_list_item;
		start_time = cl_get_time_stamp()/1000;
		pfn_callback = p_item->pfn_callback;
#if defined( CL_KERNEL )
		CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);
#endif
		p_item->pfn_callback( p_item );
#if defined( CL_KERNEL )
		CL_ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);
#endif

		end_time = cl_get_time_stamp()/1000;

		total_time = end_time - start_time;
		if (total_time > 5000)
			CL_PRINT(CL_DBG_ERROR, CL_DBG_ALL, ("async_proc %p: The processing of %p took %d miliseconds\n", 
												p_async_proc, pfn_callback, total_time));

		/* Acquire the lock again to continue processing. */
		cl_spinlock_acquire( &p_async_proc->lock );
		/* Get the next item in the queue. */
		p_list_item = cl_qlist_remove_head( &p_async_proc->item_queue );
	}

	/* The queue is empty.  Release the lock and return. */
	cl_spinlock_release( &p_async_proc->lock );
}
