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

#if !defined(__AL_MW_H__)
#define __AL_MW_H__


#include <complib/cl_qpool.h>

#include "al_ca.h"



typedef struct _ib_mw
{
	al_obj_t					obj;

	/* List item used to store MW in PD list for proper destruction order. */
	cl_list_item_t				pd_list_item;
	ib_mw_handle_t				h_ci_mw;	/* Actual HW handle. */

}	ib_mw_t;



ib_api_status_t
create_mw(
	IN		const	ib_pd_handle_t				h_pd,
		OUT			uint32_t* const				p_rkey,
		OUT			ib_mw_handle_t* const		ph_mw,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
query_mw(
	IN		const	ib_mw_handle_t				h_mw,
		OUT			ib_pd_handle_t* const		ph_pd,
		OUT			uint32_t* const				p_rkey,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );

ib_api_status_t
destroy_mw(
	IN		const	ib_mw_handle_t				h_mw );

#endif /* __AL_MW_H__ */
