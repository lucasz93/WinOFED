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



#include "complib/cl_thread.h"
#include <process.h>


static DWORD WINAPI
cl_thread_callback( 
	IN	cl_thread_t* p_thread )
{
	/* Call the user's thread function. */
	(*p_thread->pfn_callback)( (void*)p_thread->context );

	/* 
	 * Use endthreadex so that the thread handle is not closed.  It will
	 * be closed in the cl_thread_destroy.
	 */
	ExitThread( 0 );
}


void
cl_thread_construct( 
	IN	cl_thread_t* const	p_thread )
{
	p_thread->osd.h_thread = NULL;
	p_thread->osd.thread_id = 0;
}


cl_status_t
cl_thread_init(
	IN	cl_thread_t* const			p_thread,
	IN	cl_pfn_thread_callback_t	pfn_callback, 
	IN	const void* const			context,
	IN	const char* const			name )
{
	CL_ASSERT( p_thread && pfn_callback );

	UNUSED_PARAM( name );

	cl_thread_construct( p_thread );

	p_thread->pfn_callback = pfn_callback;
	p_thread->context = context;

	/* Create a new thread, storing both the handle and thread id. */
	p_thread->osd.h_thread = (HANDLE)CreateThread( NULL, 0, 
		cl_thread_callback, p_thread, 0, &p_thread->osd.thread_id );

	if( !p_thread->osd.h_thread )
		return( CL_ERROR );

	return( CL_SUCCESS );
}


void
cl_thread_destroy( 
	IN	cl_thread_t* const	p_thread )
{
	CL_ASSERT( p_thread );

	if( !p_thread->osd.h_thread )
		return;

	/* Wait for the thread to exit. */
	WaitForSingleObject( p_thread->osd.h_thread, INFINITE );

	/* Close the handle to the thread. */
	CloseHandle( p_thread->osd.h_thread );

	/*
	 * Reset the handle in case the user calls destroy and the thread is 
	 * no longer active.
	 */
	p_thread->osd.h_thread = NULL;
}


uint32_t
cl_proc_count( void )
{
	SYSTEM_INFO	system_info;

	GetSystemInfo( &system_info );
	return system_info.dwNumberOfProcessors;
}


boolean_t
cl_is_current_thread(
	IN	const cl_thread_t* const	p_thread )
{
	return( p_thread->osd.thread_id == GetCurrentThreadId() );
}