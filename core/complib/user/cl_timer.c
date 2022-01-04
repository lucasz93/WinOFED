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
 

// Using FFFFFFFF will not work on windows 2003
#define CL_MAX_TIME 0xFFFFFFFE


/*
 * If timeout_time is 0, the timer has been stopped.
 */
static void CALLBACK
__timer_callback( 
	IN cl_timer_t* const p_timer,
	IN BOOLEAN timer_signalled )
{
	uint64_t timeout;
	UNUSED_PARAM( timer_signalled );

	EnterCriticalSection( &p_timer->cb_lock );
	EnterCriticalSection( &p_timer->lock );
	timeout = p_timer->timeout_time;
	p_timer->timeout_time = 0;
	LeaveCriticalSection( &p_timer->lock );

	if( timeout )
		(p_timer->pfn_callback)( (void*)p_timer->context );
	LeaveCriticalSection( &p_timer->cb_lock );
}


void
cl_timer_construct(
	IN	cl_timer_t* const	p_timer )
{
	memset(p_timer, 0, sizeof *p_timer);
}



cl_status_t
cl_timer_init( 
	IN	cl_timer_t* const		p_timer,
	IN	cl_pfn_timer_callback_t	pfn_callback,
	IN	const void* const		context )
{
	CL_ASSERT( p_timer );
	CL_ASSERT( pfn_callback );

	cl_timer_construct( p_timer );

	p_timer->pfn_callback = pfn_callback;
	p_timer->context = context;
	InitializeCriticalSection( &p_timer->lock );
	InitializeCriticalSection( &p_timer->cb_lock );
	if( !CreateTimerQueueTimer( &p_timer->h_timer, NULL, __timer_callback,
		p_timer, CL_MAX_TIME, CL_MAX_TIME, WT_EXECUTEINIOTHREAD ) )
		return CL_ERROR;
	return( CL_SUCCESS );
}


void
cl_timer_destroy(
	IN	cl_timer_t* const	p_timer )
{
	CL_ASSERT( p_timer );
	
	DeleteTimerQueueTimer( NULL, p_timer->h_timer, INVALID_HANDLE_VALUE );
	DeleteCriticalSection( &p_timer->lock );
	DeleteCriticalSection( &p_timer->cb_lock );
}


cl_status_t 
cl_timer_start(
	IN	cl_timer_t* const	p_timer,
	IN	const uint32_t		time_ms )
{
	return cl_timer_trim( p_timer, time_ms );
}


cl_status_t 
cl_timer_trim(
	IN	cl_timer_t* const	p_timer,
	IN	const uint32_t		time_ms )
{
	uint64_t	timeout;
	cl_status_t	status = CL_SUCCESS;

	CL_ASSERT( p_timer );
	CL_ASSERT( p_timer->pfn_callback );

	EnterCriticalSection( &p_timer->lock );
	timeout = cl_get_time_stamp() + (((uint64_t)time_ms) * 1000);
	if ( !p_timer->timeout_time || timeout < p_timer->timeout_time )
	{
		if( ChangeTimerQueueTimer( NULL, p_timer->h_timer, time_ms, CL_MAX_TIME ) )
			p_timer->timeout_time = timeout;
		else
			status = CL_ERROR;
	}
	LeaveCriticalSection( &p_timer->lock );
	return status;
}


/*
 * Acquire cb_lock to ensure that all callbacks have completed.
 */
void
cl_timer_stop(
	IN	cl_timer_t* const	p_timer )
{
	CL_ASSERT( p_timer );

	EnterCriticalSection( &p_timer->cb_lock );
	EnterCriticalSection( &p_timer->lock );
	p_timer->timeout_time = 0;
	ChangeTimerQueueTimer( NULL, p_timer->h_timer, CL_MAX_TIME, CL_MAX_TIME );
	LeaveCriticalSection( &p_timer->lock );
	LeaveCriticalSection( &p_timer->cb_lock );
}


#define SEC_TO_MICRO		CL_CONST64(1000000)	// s to µs conversion

uint64_t
cl_get_time_stamp( void )
{
	LARGE_INTEGER	tick_count, frequency;

	if( !QueryPerformanceFrequency( &frequency ) )
		return( 0 );

	if( !QueryPerformanceCounter( &tick_count ) )
		return( 0 );

	return( SEC_TO_MICRO * tick_count.QuadPart / frequency.QuadPart );
}

uint32_t
cl_get_time_stamp_sec( void )
{
	return( (uint32_t)(cl_get_time_stamp() / SEC_TO_MICRO) );
}


uint64_t
cl_get_tick_count( void )
{
	LARGE_INTEGER	tick_count;

	if( !QueryPerformanceCounter( &tick_count ) )
		return( 0 );

	return tick_count.QuadPart;
}


uint64_t
cl_get_tick_freq( void )
{
	LARGE_INTEGER	frequency;

	if( !QueryPerformanceFrequency( &frequency ) )
		return( 0 );

	return frequency.QuadPart;
}
