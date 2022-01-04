/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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



#ifndef _SRP_DATA_PATH_H_
#define _SRP_DATA_PATH_H_


#include <iba/ib_al.h>
#include <iba/ioc_ifc.h>
#include "srp.h"
#include "srp_data.h"

typedef struct _srp_conn_info
{
	uint64_t                vaddr;
	net32_t                 lkey;
	net32_t                 rkey;
	uint32_t                init_to_targ_iu_sz;
	uint64_t                tag;
	uint32_t                max_scatter_gather_entries;
	BOOLEAN                 signal_send_completion;
	DATA_BUFFER_DESCRIPTOR_FORMAT descriptor_format;
} srp_conn_info_t;

void
srp_send_completion_cb(
	IN	const	ib_cq_handle_t		h_cq,
	IN			void				*p_context );

void
srp_recv_completion_cb(
	IN	const	ib_cq_handle_t		h_cq,
	IN			void				*p_context );

BOOLEAN
srp_format_io_request(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb );

void
srp_post_io_request(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb );

void
srp_abort_command(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  );

void
srp_lun_reset(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb  );

#endif  /* _SRP_DATA_PATH_H_ */
