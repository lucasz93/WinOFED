/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#if !defined(__AL_REF_TRACE_H__)
#define __AL_REF_TRACE_H__

#include <iba/ib_al.h>

#include <complib/cl_atomic.h>
#include <complib/cl_async_proc.h>
#include <complib/cl_event.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_obj.h>
//#include <complib/cl_vector.h>

typedef enum {
	AL_REF,
	AL_DEREF 
} al_ref_change_type_t;


enum e_ref_origin {
	E_REF_INIT = 1,
	E_REF_CA_ADD_REMOVE,
	E_REF_CI_CA_ASYNC_EVENT,
	E_REF_MAD_ADD_REMOVE,
	E_REF_KEY_ADD_REMOVE,
	E_REF_CONSTRUCT_CHILD
};


typedef enum {
	EQUAL_REF_DEREF_PAIRS = 1 << 0,
	SAME_REF_OR_DEREF_ENTRY = 1 << 1,
} purge_policy_flags_t;


#ifdef CL_KERNEL
typedef struct _obj_ref_trace
{
	LIST_ENTRY					ref_chain;
	cl_spinlock_t				lock; 
	volatile long				list_size;
} obj_ref_trace_t;
#endif

void
ref_trace_db_init(ULONG policy);

void
ref_trace_init(
	IN				void * const			p_obj);

void
ref_trace_destroy(
	IN				void * const			p_obj );

int32_t
ref_trace_insert(
    __in char* file,
    __in LONG 		 line,
    __in void * const		 p_obj,
    __in al_ref_change_type_t	 change_type,
    __in uint8_t ref_ctx
    );

void 
ref_trace_print(
	IN void * const 	 p_obj);

void 
ref_trace_dirty_print(
	IN void * const 	 p_obj);

#endif /* __AL_REF_TRACE_H__ */
