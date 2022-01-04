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

#if !defined(__AL_REG_SVC_H__)
#define __AL_REG_SVC_H__

#include <complib/cl_async_proc.h>

#include "al_common.h"
#include "al_query.h"



typedef struct _al_reg_svc
{
	al_obj_t					obj;

	al_sa_req_t					sa_req;

	/* Status of the registration request. */
	ib_api_status_t				req_status;
	/* Additional status information returned in the registration response. */
	ib_net16_t					resp_status;

	al_sa_reg_state_t			state;
	ib_pfn_reg_svc_cb_t			pfn_reg_svc_cb;

	/* Store service record to report to SA later. */
	ib_service_record_t			svc_rec;
	ib_net64_t					port_guid;

} al_reg_svc_t;



#endif /* __AL_REG_SVC_H__ */
