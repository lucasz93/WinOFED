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


/*
 * Abstract:
 *	Declaration of mutex object.
 *
 * Environment:
 *	Windows Kernel Mode
 */


#ifndef _CL_MUTEX_OSD_H_
#define _CL_MUTEX_OSD_H_


#include <complib/cl_types.h>


typedef FAST_MUTEX	cl_mutex_t;


#ifdef __cplusplus
extern "C"
{
#endif


CL_INLINE void
cl_mutex_construct(
	IN	cl_mutex_t* const	p_mutex )
{
	UNUSED_PARAM( p_mutex );
}


CL_INLINE cl_status_t
cl_mutex_init(
	IN	cl_mutex_t* const	p_mutex )
{
	CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	ExInitializeFastMutex( p_mutex );
	return CL_SUCCESS;
}


CL_INLINE void
cl_mutex_destroy(
	IN	cl_mutex_t* const	p_mutex )
{
	UNUSED_PARAM( p_mutex );
}

/*
//TODO : according to MSDN(http://msdn.microsoft.com/en-us/library/ff547911(v=VS.85).aspx),
the definition of these fast mutex wrappers should be:
But it DOES NOT work and still generates warnings 28157, 28158 !
_IRQL_requires_max_(APC_LEVEL)
_IRQL_raises_(APC_LEVEL)
CL_INLINE VOID 
  cl_mutex_acquire(
    _Inout_ 
        __drv_out(_IRQL_saves_
            __drv_acquiresResource(FastMutex)) 
    PFAST_MUTEX  FastMutex
    )
{
	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
	ExAcquireFastMutex( FastMutex );
}

_IRQL_requires_(APC_LEVEL)
CL_INLINE VOID 
  cl_mutex_release(
    _Inout_  
        __drv_in(_IRQL_restores_
            __drv_releasesResource(FastMutex)) 
    PFAST_MUTEX  FastMutex
    )

{
	CL_ASSERT( KeGetCurrentIrql() == APC_LEVEL );
	ExReleaseFastMutex( FastMutex );
}
*/

#ifdef NTDDI_WIN8
_IRQL_requires_max_(APC_LEVEL)
_IRQL_raises_(APC_LEVEL)
	__drv_at(p_mutex, __drv_acquiresResource(FAST_MUTEX))
//	__drv_at(p_mutex, __drv_savesIRQL)
//_IRQL_saves_global_(p_mutex, FAST_MUTEX)
//TODO http://www.osronline.com/showthread.cfm?link=196119
__drv_savesIRQLGlobal(FastMutex, FastMutex)
#endif
CL_INLINE void
cl_mutex_acquire(
	IN	cl_mutex_t* const	p_mutex )
{
	CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
	ExAcquireFastMutex( p_mutex );
}

#ifdef NTDDI_WIN8
_IRQL_requires_(APC_LEVEL)
__drv_at(p_mutex, __drv_releasesResource(FAST_MUTEX))
//__drv_at(p_mutex, __drv_restoresIRQL)
//_IRQL_restores_global_(p_mutex, FAST_MUTEX)
//TODO http://www.osronline.com/showthread.cfm?link=196119
__drv_restoresIRQLGlobal(FastMutex, FastMutex)
#endif
CL_INLINE void
#pragma prefast(suppress: 28167, "The irql level is restored by ExReleaseFastMutex")
cl_mutex_release(
	IN	cl_mutex_t* const	p_mutex )
{
	CL_ASSERT( KeGetCurrentIrql() == APC_LEVEL );
	ExReleaseFastMutex( p_mutex );
}


#ifdef __cplusplus
}	/* extern "C" */
#endif

#endif /* _CL_MUTEX_OSD_H_ */
