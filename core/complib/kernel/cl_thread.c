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

#ifdef NTDDI_WIN8
static KSTART_ROUTINE __thread_callback;
#endif
static void
__thread_callback( 
	IN	cl_thread_t* p_thread )
{
	/* Bump the thread's priority. */
	KeSetPriorityThread( KeGetCurrentThread(), LOW_REALTIME_PRIORITY );

	/* Call the user's thread function. */
	(*p_thread->pfn_callback)( (void*)p_thread->context );

	PsTerminateSystemThread( STATUS_SUCCESS );
}


void
cl_thread_construct( 
	IN	cl_thread_t* const	p_thread )
{
	p_thread->osd.h_thread = NULL;
	p_thread->osd.p_thread = NULL;
}


cl_status_t
cl_thread_init(
	IN	cl_thread_t* const			p_thread,
	IN	cl_pfn_thread_callback_t	pfn_callback, 
	IN	const void* const			context,
	IN	const char* const			name )
{
	NTSTATUS			status;
	OBJECT_ATTRIBUTES	attr;

	CL_ASSERT( p_thread && pfn_callback );
	CL_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	cl_thread_construct( p_thread );

	p_thread->pfn_callback = pfn_callback;
	p_thread->context = context;
#ifdef NTDDI_WIN8
	strncpy_s( &p_thread->name[0], sizeof(p_thread->name), name, sizeof(p_thread->name) );
#else
	//TODO to use ntrsafe.dll and remove this call
	strncpy( &p_thread->name[0], name, sizeof(p_thread->name) );
#endif
	/* Create a new thread, storing both the handle and thread id. */
	InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );
	status = PsCreateSystemThread( &p_thread->osd.h_thread, THREAD_ALL_ACCESS,
		&attr, NULL, NULL, __thread_callback, p_thread );

	if( !NT_SUCCESS( status ) )
		return( CL_ERROR );

	/* get pointer to thread object to wait on it's exit */
	status = ObReferenceObjectByHandle( p_thread->osd.h_thread, THREAD_ALL_ACCESS,
		NULL, KernelMode, (PVOID*)&p_thread->osd.p_thread, NULL );
    CL_ASSERT(status == STATUS_SUCCESS); // According to MSDN, must succeed if I set the params

	/* Close the handle to the thread. */
	status = ZwClose( p_thread->osd.h_thread );
    CL_ASSERT(NT_SUCCESS(status)); // Should always succeed

	return( CL_SUCCESS );
}


void
cl_thread_destroy( 
	IN	cl_thread_t* const	p_thread )
{
	CL_ASSERT( p_thread );

	if( !p_thread->osd.h_thread )
		return;

	/* Wait until the kernel thread pointer is stored in the thread object. */
	while( !p_thread->osd.p_thread )
		cl_thread_suspend( 0 );

	/* Wait for the thread to exit. */
	KeWaitForSingleObject( p_thread->osd.p_thread, Executive, KernelMode,
		FALSE, NULL );

	/* Release the reference to thread object */
	ObDereferenceObject( p_thread->osd.p_thread );

	/*
	 * Reset the handle in case the user calls destroy and the thread is 
	 * no longer active.
	 */
	cl_thread_construct( p_thread );
}


uint32_t
cl_proc_count( void )
{
#if WINVER >= 0x602
	return KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
#else 
	return KeQueryActiveProcessorCount(NULL);
#endif
}


boolean_t
cl_is_current_thread(
	IN	const cl_thread_t* const	p_thread )
{
	return( p_thread->osd.p_thread == KeGetCurrentThread() );
}
