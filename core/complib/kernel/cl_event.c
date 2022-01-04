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


#include "complib/cl_event.h"


cl_status_t
cl_event_wait_on( 
	IN	cl_event_t* const	p_event,
	IN	const uint32_t		wait_us,
	IN	const boolean_t		interruptible )
{
	NTSTATUS	status;
	LARGE_INTEGER	wait;

	CL_ASSERT( p_event );

	if( wait_us == EVENT_NO_TIMEOUT )
	{
		CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
		status = KeWaitForSingleObject( p_event, Executive, KernelMode, 
			(BOOLEAN)interruptible, NULL );
	}
	else
	{
		CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
		/* Timeout is in 100 ns increments. */
		wait.QuadPart = -(int64_t)(((uint64_t)wait_us) * 10);
		status = KeWaitForSingleObject( p_event, Executive, KernelMode,
			(BOOLEAN)interruptible, &wait );
	}

	switch( status )
	{
	case STATUS_SUCCESS:
		return( CL_SUCCESS );
	case STATUS_USER_APC:
		return( CL_NOT_DONE );
	case STATUS_TIMEOUT:
		return( CL_TIMEOUT );
	case STATUS_ALERTED:
	default:
		return( CL_ERROR );
	}
}
