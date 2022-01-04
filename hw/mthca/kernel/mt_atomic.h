/*
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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

#ifndef MT_ATOMIC_H
#define MT_ATOMIC_H

#include "complib/cl_atomic.h"

typedef atomic32_t atomic_t;

#define atomic_inc	cl_atomic_inc
#define atomic_dec	cl_atomic_dec

static inline atomic_t atomic_read(atomic_t *pval) 
{
	return *pval;	
}

static inline void atomic_set(atomic_t *pval, long val)
{
	*pval = val;
}

/**
* atomic_inc_and_test - decrement and test
* pval: pointer of type atomic_t
* 
* Atomically increments pval by 1 and
* returns true if the result is 0, or false for all other
* cases.
*/ 
static inline int
atomic_inc_and_test(atomic_t *pval)
{ 
	return cl_atomic_inc(pval) == 0;
}

/**
* atomic_dec_and_test - decrement and test
* pval: pointer of type atomic_t
* 
* Atomically decrements pval by 1 and
* returns true if the result is 0, or false for all other
* cases.
*/ 
static inline int
atomic_dec_and_test(atomic_t *pval)
{ 
	return cl_atomic_dec(pval) == 0;
}

#endif
