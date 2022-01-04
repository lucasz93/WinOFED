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

/*
 * Abstract:
 *	This header file defines data structures for the kernel-mode NDI support 
 *
 * Environment:
 *	Kernel .
 */


#ifndef _ALPROXY_NDI_H_
#define _ALPROXY_NDI_H_

#include "complib/cl_ioctl_osd.h"
#include "al_cq.h"
#include "al_ndi_cq.h"
#include "al_qp.h"
#include "al_ndi_cm.h"

/* functions from al_proxy_verbs.c */
ib_api_status_t
cpyin_umvbuf(
	IN		ci_umv_buf_t	*p_src,
		OUT	ci_umv_buf_t	**pp_dst );

ib_api_status_t
cpyout_umvbuf(
	IN		ci_umv_buf_t	*p_dest,
	IN		ci_umv_buf_t	*p_src);

void
free_umvbuf(
	IN				ci_umv_buf_t				*p_umv_buf );


#endif



