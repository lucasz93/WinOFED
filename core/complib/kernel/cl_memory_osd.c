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


#include "complib/cl_memory.h"


void*
__cl_malloc_priv(
	IN	const size_t	size,
	IN	const boolean_t	pageable )
{
	CL_ASSERT(size != 0);
	if (size ==0) {
		return NULL;
	}
	if( pageable )
	{
		CL_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );
		return( ExAllocatePoolWithTag( PagedPool, size, 'virp' ) );
	}
	else
	{
		CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
		return( ExAllocatePoolWithTag( NonPagedPool, size, 'virp' ) );
	}
}


void
__cl_free_priv( 
	IN	void* const	p_memory )
{
	CL_ASSERT( KeGetCurrentIrql() <= DISPATCH_LEVEL );
	ExFreePoolWithTag( p_memory, 'virp' );
}
