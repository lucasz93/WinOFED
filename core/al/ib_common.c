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

#include <iba/ib_types.h>
#include "ib_common.h"



ib_api_status_t
ib_convert_cl_status(
	IN		const	cl_status_t					cl_status )
{
	switch( cl_status )
	{
	case CL_SUCCESS:
		return IB_SUCCESS;
	case CL_INVALID_STATE:
		return IB_INVALID_STATE;
	case CL_INVALID_SETTING:
		return IB_INVALID_SETTING;
	case CL_INVALID_PARAMETER:
		return IB_INVALID_PARAMETER;
	case CL_INSUFFICIENT_RESOURCES:
		return IB_INSUFFICIENT_RESOURCES;
	case CL_INSUFFICIENT_MEMORY:
		return IB_INSUFFICIENT_MEMORY;
	case CL_INVALID_PERMISSION:
		return IB_INVALID_PERMISSION;
	case CL_COMPLETED:
		return IB_SUCCESS;
	case CL_INVALID_OPERATION:
		return IB_UNSUPPORTED;
	case CL_TIMEOUT:
		return IB_TIMEOUT;
	case CL_NOT_DONE:
		return IB_NOT_DONE;
	case CL_CANCELED:
		return IB_CANCELED;
	case CL_NOT_FOUND:
		return IB_NOT_FOUND;
	case CL_BUSY:
		return IB_RESOURCE_BUSY;
	case CL_PENDING:
		return IB_PENDING;
	case CL_OVERRUN:
		return IB_OVERFLOW;
	case CL_ERROR:
	case CL_REJECT:
	case CL_UNAVAILABLE:
	case CL_DISCONNECT:
	case CL_DUPLICATE:
	default:
		return IB_ERROR;
	}
}


void
ib_fixup_ca_attr(
	IN				ib_ca_attr_t* const			p_dest,
	IN		const	ib_ca_attr_t* const			p_src )
{
	uint8_t		i;
	uintn_t		offset = (uintn_t)p_dest - (uintn_t)p_src;
	ib_port_attr_t			*p_tmp_port_attr = NULL;

	CL_ASSERT( p_dest );
	CL_ASSERT( p_src );

	/* Fix up the pointers to point within the destination buffer. */
	p_dest->p_page_size =
		(uint32_t*)(((uint8_t*)p_dest->p_page_size) + offset);

	p_tmp_port_attr =
		(ib_port_attr_t*)(((uint8_t*)p_dest->p_port_attr) + offset);

	/* Fix up each port attribute's gid and pkey table pointers. */
	for( i = 0; i < p_dest->num_ports; i++ )
	{
		p_tmp_port_attr[i].p_gid_table = (ib_gid_t*)
			(((uint8_t*)p_tmp_port_attr[i].p_gid_table) + offset);

		p_tmp_port_attr[i].p_pkey_table =(ib_net16_t*)
			(((uint8_t*)p_tmp_port_attr[i].p_pkey_table) + offset);
	}
	p_dest->p_port_attr = p_tmp_port_attr;
}


ib_ca_attr_t*
ib_copy_ca_attr(
	IN				ib_ca_attr_t* const			p_dest,
	IN		const	ib_ca_attr_t* const			p_src )
{
	CL_ASSERT( p_dest );
	CL_ASSERT( p_src );

	/* Copy the attibutes buffer. */
	cl_memcpy( p_dest, p_src, p_src->size );

	ib_fixup_ca_attr( p_dest, p_src );

	return p_dest;
}
