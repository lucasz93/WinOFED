/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#include "complib/cl_timer.h"
#include "complib/cl_memory.h"

#ifdef NTDDI_WIN8
static KDEFERRED_ROUTINE __timer_callback;
#endif
static void
__timer_callback( 
	IN	PRKDPC		p_dpc,
	IN	cl_timer_t*	p_timer,
	IN	void*		arg1,
	IN	void*		arg2 )
{
	KLOCK_QUEUE_HANDLE hlock;

	UNUSED_PARAM( p_dpc );
	UNUSED_PARAM( arg1 );
	UNUSED_PARAM( arg2 );

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &p_timer->spinlock, &hlock );
	p_timer->timeout_time = 0;
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hlock );

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &p_timer->cb_lock, &hlock );
	(p_timer->pfn_callback)( (void*)p_timer->context );
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hlock );
}


void
cl_timer_construct(
	IN	cl_timer_t* const	p_timer )
{
	cl_memclr( p_timer, sizeof(cl_timer_t) );
}



cl_status_t
cl_timer_init( 
	IN	cl_timer_t* const		p_timer,
	IN	cl_pfn_timer_callback_t	pfn_callback,
	IN	const void* const		context )
{
	CL_ASSERT( p_timer && pfn_callback );

	cl_timer_construct( p_timer );

	p_timer->pfn_callback = pfn_callback;
	p_timer->context = context;

	KeInitializeTimer( &p_timer->timer );
	KeInitializeDpc( &p_timer->dpc, __timer_callback, p_timer );
	KeInitializeSpinLock( &p_timer->spinlock );
	KeInitializeSpinLock( &p_timer->cb_lock );

	return( CL_SUCCESS );
}


void
cl_timer_destroy(
	IN	cl_timer_t* const	p_timer )
{
	CL_ASSERT( p_timer );
	
	if( !p_timer->pfn_callback )
		return;

	/* Ensure that the timer is stopped. */
	cl_timer_stop( p_timer );

	/*
	 * Flush the DPCs to ensure that no callbacks occur after the timer is
	 * destroyed.
	 */
	KeFlushQueuedDpcs();

	p_timer->pfn_callback = NULL;
}


cl_status_t 
cl_timer_start(
	IN	cl_timer_t* const	p_timer,
	IN	const uint32_t		time_ms )
{
	LARGE_INTEGER	due_time;
	uint64_t		timeout_time;
	KLOCK_QUEUE_HANDLE hlock;

	CL_ASSERT( p_timer );
	CL_ASSERT( p_timer->pfn_callback );
	CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );

	/* Due time is in 100 ns increments.  Negative for relative time. */
	due_time.QuadPart = -(int64_t)(((uint64_t)time_ms) * 10000);
	timeout_time = cl_get_time_stamp() + (((uint64_t)time_ms) * 1000);

	KeAcquireInStackQueuedSpinLock( &p_timer->spinlock, &hlock );
	p_timer->timeout_time = timeout_time;
	KeSetTimer( &p_timer->timer, due_time, &p_timer->dpc );
	KeReleaseInStackQueuedSpinLock( &hlock );

	return( CL_SUCCESS );
}


cl_status_t 
cl_timer_trim(
	IN	cl_timer_t* const	p_timer,
	IN	const uint32_t		time_ms )
{
	LARGE_INTEGER	due_time;
	uint64_t		timeout_time;
	KLOCK_QUEUE_HANDLE hlock;

	CL_ASSERT( p_timer );
	CL_ASSERT( p_timer->pfn_callback );

	timeout_time = cl_get_time_stamp() + (((uint64_t)time_ms) * 1000);

	/* Only pull in the timeout time. */
	KeAcquireInStackQueuedSpinLock( &p_timer->spinlock, &hlock );
	if( !p_timer->timeout_time || p_timer->timeout_time > timeout_time )
	{
		p_timer->timeout_time = timeout_time;
		due_time.QuadPart = -(int64_t)(((uint64_t)time_ms) * 10000);
		KeSetTimer( &p_timer->timer, due_time, &p_timer->dpc );
	}
	KeReleaseInStackQueuedSpinLock( &hlock );
	return( CL_SUCCESS );
}


void
cl_timer_stop(
	IN	cl_timer_t* const	p_timer )
{
	KLOCK_QUEUE_HANDLE hlock;

	CL_ASSERT( p_timer );
	CL_ASSERT( p_timer->pfn_callback );
	CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	
	/* Cancel the timer.  This also cancels any queued DPCs for the timer. */
	KeAcquireInStackQueuedSpinLock( &p_timer->spinlock, &hlock );
	p_timer->timeout_time = 0;
	KeCancelTimer( &p_timer->timer );
	KeReleaseInStackQueuedSpinLock( &hlock );
}
