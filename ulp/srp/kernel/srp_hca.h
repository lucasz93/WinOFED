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



#ifndef _SRP_HCA_H_
#define _SRP_HCA_H_


#include <iba/ib_al.h>
#include "srp_hba.h"

typedef struct _srp_hca
{
	srp_hba_t				*p_hba;

	ib_ca_handle_t			h_ca;
	ib_pd_handle_t			h_pd;
	ib_mr_handle_t			h_mr;
	mlnx_fmr_pool_handle_t	h_fmr_pool;
	uint32_t					fmr_page_size;
	uint32_t					fmr_page_shift;
	uint64_t					vaddr;
	net32_t					lkey;
	net32_t					rkey;

}	srp_hca_t;

ib_api_status_t
srp_open_ca(
	IN	OUT			srp_hca_t					*p_hca,
	IN				void						*p_context );

void
srp_close_ca(
	IN OUT  srp_hca_t   *p_hca );

ib_api_status_t
srp_get_responder_resources(
	IN				srp_hca_t					*p_hca,
		OUT			uint8_t						*p_responder_resources );

ib_api_status_t
srp_init_hca(
	IN	OUT			srp_hca_t					*p_hca,
	IN				srp_hba_t					*p_hba );

#endif  /* _SRP_HCA_H_ */
