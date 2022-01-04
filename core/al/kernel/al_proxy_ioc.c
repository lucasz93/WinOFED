/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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


#include <complib/comp_lib.h>
#include <iba/ib_al.h>
#include <iba/ib_al_ioctl.h>
#include "al_debug.h"
#include "al.h"
#include "al_dev.h"
#include "al_proxy.h"
#include "ib_common.h"

cl_status_t
ioc_ioctl(
	IN		cl_ioctl_handle_t		h_ioctl,
		OUT	size_t					*p_ret_bytes )
{
	cl_status_t cl_status;

	UNUSED_PARAM( p_ret_bytes );

	switch( cl_ioctl_ctl_code( h_ioctl ) )
	{
	case UAL_REQ_CREATE_PDO:
		{
			cl_status = bus_add_pkey(h_ioctl);
			break;
		}

	case UAL_REQ_REMOVE_PDO:
		{
			cl_status = bus_rem_pkey(h_ioctl);
			break;
		}
		default:
			cl_status = CL_INVALID_PARAMETER;
			break;
	}
	return cl_status;
}
