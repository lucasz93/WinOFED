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

#if !defined(__UAL_MGR_H__)
#define __UAL_MGR_H__

#include "ual_support.h"
#include "al_ci_ca.h"
#include <complib/cl_event.h>
#include <complib/cl_ptr_vector.h>


typedef struct _ual_mgr
{
	ib_al_handle_t				h_al;	/* UAL's internal implicit open_al */

	cl_event_t					sync_event;

	boolean_t					exit_thread;

	/* Pointer vector of threads used to handle async IOCTL completions. */
	cl_ptr_vector_t				cb_threads;
	/* Completion port handle that cb threads use to get I/O completions. */
	HANDLE						h_cb_port;

	/* Thread to handle work request completions */
	HANDLE						h_cq_file;
	comp_cb_ioctl_info_t		comp_cb_info;
	OVERLAPPED					cq_ov;

	/* Thread to handle Miscellaneous notifications such as:
	 * cq/qp error
	 * cq/qp destroyed notification
	 * MAD notifications, SM, Query
	 * mcast_join completion
	 * pnp event
	 * completion of subscription
	 * Fabric event
	 * ca destroyed
	 * cancellation of subscription, notify req
	 */
	HANDLE						h_misc_file;
	misc_cb_ioctl_info_t		misc_cb_info;
	OVERLAPPED					misc_ov;

}	ual_mgr_t;


/* Global mad pool key for internal MADs. */
ib_pool_key_t				g_pool_key;


ib_api_status_t
do_open_al(
		OUT			ib_al_handle_t* const		ph_al );

ib_api_status_t
do_close_al(
	IN		const	ib_al_handle_t				h_al );


/*
 * Proto types for asynchronous event processing threads
 */
void
ual_cm_thread_start(
	IN				void	*context);

void
ual_comp_thread_start(
	IN				void	*context);

void
ual_misc_thread_start(
	IN				void	*context);


/* Prototype for creating a file and binding it to the internal thread pool */
HANDLE
ual_create_async_file(
	IN				uint32_t					type );

void
sa_req_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov );

void
pnp_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov );

void
cm_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov );

void
cq_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov );

void
misc_cb(
	IN				DWORD						error_code,
	IN				DWORD						ret_bytes,
	IN				LPOVERLAPPED				p_ov );

#endif // __UAL_MGR_H__
