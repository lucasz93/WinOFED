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

#if !defined(__AL_CA_H__)
#define __AL_CA_H__

#include <iba/ib_ci.h>
#include <complib/cl_qlist.h>

#include "al_common.h"
#include "al_ci_ca.h"



typedef struct _ib_ca
{
	al_obj_t				obj;

	ib_pfn_event_cb_t		pfn_event_cb;
	cl_list_item_t			list_item;
#if defined(CL_KERNEL)
	ib_ca_handle_t			h_um_ca;
	PDEVICE_OBJECT			p_hca_dev;
	PDEVICE_OBJECT			p_fdo;
#endif

}	ib_ca_t;


ib_api_status_t
open_ca(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_net64_t					ca_guid,
	IN		const	ib_pfn_event_cb_t			pfn_ca_event_cb OPTIONAL,
	IN		const	void* const					ca_context,
#if defined(CL_KERNEL)
	IN				KPROCESSOR_MODE				mode,
#endif
		OUT			ib_ca_handle_t* const		ph_ca,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf OPTIONAL );


ib_api_status_t
query_ca(
	IN		const	ib_ca_handle_t				h_ca,
		OUT			ib_ca_attr_t* const			p_ca_attr OPTIONAL,
	IN	OUT			uint32_t* const				p_size,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


ib_api_status_t
al_convert_to_ci_handles(
	IN				void**				const	dst_handle_array,
	IN		const	void**				const	src_handle_array,
	IN				uint32_t					num_handles );


#endif /* __AL_CA_H__ */
