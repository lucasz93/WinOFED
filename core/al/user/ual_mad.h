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

#if !defined(__IB_UAL_MAD_H__)
#define __IB_UAL_MAD_H__

#include <iba/ib_al.h>
#include "al_mad_pool.h"


/*
 * Create a pool key for the internal mad pool.  This pool key will never
 * be registered on anything.
 */
ib_api_status_t
ual_reg_global_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
		OUT			ib_pool_key_t* const		pp_pool_key );

ib_api_status_t
ual_reg_mad_svc(
	IN		const	ib_qp_handle_t				h_qp,
	IN		const	ib_mad_svc_t* const			p_mad_svc,
		OUT			ib_mad_svc_handle_t* const	ph_mad_svc );

ib_api_status_t
ual_dereg_mad_svc(
	IN		const	ib_mad_svc_handle_t			h_mad_svc );


ib_api_status_t
ual_spl_qp_mad_send(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element_list,
		OUT			ib_mad_element_t			**pp_mad_failure OPTIONAL );

ib_api_status_t
ual_spl_qp_cancel_mad(
	IN		const	ib_mad_svc_handle_t			h_mad_svc,
	IN				ib_mad_element_t* const		p_mad_element );

void
ual_dereg_destroy_mad_pool(
	IN		const	ib_pool_key_t				pool_key );

ib_api_status_t
ual_create_reg_mad_pool(
	IN		const	ib_pool_handle_t			h_pool,
	IN		const	ib_pd_handle_t				h_pd,
	IN				ib_pool_key_t				p_pool_key );


ib_api_status_t
ual_get_recv_mad(
	IN				ib_pool_key_t				p_pool_key,
	IN		const	uint64_t					h_mad,
	IN		const	size_t						buf_size,
		OUT			ib_mad_element_t** const	pp_mad_element );

ib_api_status_t
ual_local_mad(
IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	void* const					p_mad_in,
	IN				void*						p_mad_out );

#endif /* __IB_UAL_MAD_H__ */
