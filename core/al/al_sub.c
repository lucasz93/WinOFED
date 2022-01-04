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

#include <iba/ib_al.h>

#include "al.h"
#include "al_common.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_sub.tmh"
#endif


ib_api_status_t
ib_subscribe(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_sub_req_t* const			p_sub_req,
		OUT			ib_sub_handle_t* const		ph_sub )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_SUB );

	if( AL_OBJ_INVALID_HANDLE( h_al, AL_OBJ_TYPE_H_AL ) )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_AL_HANDLE\n") );
		return IB_INVALID_AL_HANDLE;
	}
	if( !p_sub_req || !ph_sub )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_PARAMETER\n") );
		return IB_INVALID_PARAMETER;
	}

	status = IB_UNSUPPORTED;

	AL_EXIT( AL_DBG_SUB );
	return status;
}


ib_api_status_t
ib_unsubscribe(
	IN		const	ib_sub_handle_t				h_sub,
	IN		const	ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL )
{
	AL_ENTER( AL_DBG_SUB );

	if( !h_sub )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("IB_INVALID_HANDLE\n") );
		return IB_INVALID_HANDLE;
	}

	UNUSED_PARAM( pfn_destroy_cb );

	AL_EXIT( AL_DBG_SUB );
	return IB_UNSUPPORTED;
}
