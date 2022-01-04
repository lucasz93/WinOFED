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




#ifndef _CL_SPINLOCK_OSD_H_
#define _CL_SPINLOCK_OSD_H_


#include "complib/cl_types.h"
#include "complib/cl_memory.h"


/* Spinlock object definition. */
typedef struct _cl_spinlock
{
	KSPIN_LOCK			lock;
	KIRQL				irql;

} cl_spinlock_t;


#ifdef __cplusplus
extern "C"
{
#endif


CL_INLINE void
cl_spinlock_construct( 
	IN	cl_spinlock_t* const	p_spinlock )
{
	cl_memclr( p_spinlock, sizeof(cl_spinlock_t*) );
}


CL_INLINE cl_status_t
cl_spinlock_init( 
	IN	cl_spinlock_t* const	p_spinlock )
{
	CL_ASSERT( p_spinlock );

	cl_spinlock_construct( p_spinlock );

	KeInitializeSpinLock( &p_spinlock->lock );
	return( CL_SUCCESS );
}


CL_INLINE void
cl_spinlock_destroy( 
	IN	cl_spinlock_t* const	p_spinlock )
{
	UNUSED_PARAM( p_spinlock );
}

#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_raises_(DISPATCH_LEVEL)
__drv_at(p_spinlock->lock, __drv_acquiresExclusiveResource(KSPIN_LOCK))
__drv_at(p_spinlock->irql, __drv_savesIRQL)
#endif
CL_INLINE void
cl_spinlock_acquire( 
	IN	cl_spinlock_t* const	p_spinlock )
{
	CL_ASSERT( p_spinlock );
	KeAcquireSpinLock( &p_spinlock->lock, &p_spinlock->irql );
}

#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
__drv_at(p_spinlock->lock, __drv_releasesExclusiveResource(KSPIN_LOCK))
__drv_at(p_spinlock->irql, __drv_restoresIRQL)
#endif
#pragma prefast(suppress: 28157, "The irql level remains the same if restored from DISPATH_LEVEL")
CL_INLINE void cl_spinlock_release(
	IN	cl_spinlock_t* const	p_spinlock )
{
	CL_ASSERT( p_spinlock );

	KeReleaseSpinLock( &p_spinlock->lock, p_spinlock->irql );
}


#ifdef __cplusplus
}	// extern "C"
#endif

#endif // _CL_SPINLOCK_OSD_H_
