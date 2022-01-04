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

#if !defined(__AL_AV_H__)
#define __AL_AV_H__


#include "al_ca.h"
#include <complib/cl_qpool.h>
#include <complib/cl_qlist.h>


typedef struct _ib_av
{
	al_obj_t					obj;
	ib_av_handle_t				h_ci_av;		/* Actual HW CI AV. */

	ib_av_attr_t				av_attr;

	cl_list_item_t				list_item;		/* item to manage AL AV's */

}	ib_av_t;



cl_status_t
av_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );

void
av_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void*						context );


ib_api_status_t
create_av(
	IN		const	ib_pd_handle_t				h_pd,
	IN		const	ib_av_attr_t* const			p_av_attr,
		OUT			ib_av_handle_t* const		ph_av,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
query_av(
	IN		const	ib_av_handle_t				h_av,
		OUT			ib_av_attr_t* const			p_av_attr,
		OUT			ib_pd_handle_t* const		ph_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
modify_av(
	IN		const	ib_av_handle_t				h_av,
	IN		const	ib_av_attr_t* const			p_av_mod,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


#endif /* __AL_AV_H__ */
