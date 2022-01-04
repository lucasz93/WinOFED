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

#if !defined(__AL_MCAST_MGR_H__)
#define __AL_MCAST_MGR_H__

#include "al_mcast.h"


void mcast_mgr_init();
void mcast_mgr_shutdown(); 

ib_api_status_t
mcast_mgr_add_ca_ports(
	IN		const	ib_net64_t					ci_ca_guid);

ib_api_status_t
mcast_mgr_remove_ca_ports(
	IN		const	ib_net64_t					ci_ca_guid);


#ifdef __cplusplus
    extern "C" {
#endif


ib_api_status_t 
mcast_mgr_join_mcast(
	IN		const ib_mcast_req_t* const 	mcast_req,
	OUT		mcast_mgr_request_t**			pp_mcast_request);

ib_api_status_t
mcast_mgr_leave_mcast(
	IN		mcast_mgr_request_t*		p_join_request,
	IN		ib_pfn_destroy_cb_t			pfn_destroy_cb OPTIONAL );

ib_api_status_t 
mcast_mgr_cancel_join(
	IN		mcast_mgr_request_t*			p_user_request,
	OUT		boolean_t*						p_mcast_connected);

void 
mcast_mgr_request_add_ref(
	IN		mcast_mgr_request_t* 	p_mcast_request,
	IN		const char*				file_name,
	IN		uint32_t				line );

void 
mcast_mgr_request_release(
	IN		mcast_mgr_request_t* 	p_mcast_request,
	IN		const char*				file_name,
	IN		uint32_t				line );




#ifdef __cplusplus
} //    extern "C" 
#endif


#endif /* __AL_MCAST_MGR_H__ */

