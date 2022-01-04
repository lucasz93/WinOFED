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

#if !defined(__AL_PD_H__)
#define __AL_PD_H__

#include <iba/ib_al.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>

#include "al_common.h"



typedef struct _ib_pd
{
	al_obj_t					obj;

	/*
	 * MW list used to order destruction between MW's and MR's.  MR and MW
	 * can be created in any order, so we can't rely on their order in the
	 * al_obj list for proper destruction.
	 */
	cl_qlist_t					mw_list;

	ib_pd_type_t				type;
	ib_pd_handle_t				h_ci_pd;		/* Actual CI PD handle. */

}	ib_pd_t;



ib_api_status_t
alloc_pd(
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	ib_pd_type_t				pd_type,
	IN		const	void * const				pd_context,
		OUT			ib_pd_handle_t* const		ph_pd,
	IN	OUT			ci_umv_buf_t* const			p_umv_buf );


void
pd_insert_mw(
	IN		const	ib_mw_handle_t				h_mw );

void
pd_remove_mw(
	IN		const	ib_mw_handle_t				h_mw );


#endif /* __AL_PD_H__ */
