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



#ifndef _SRP_HBA_H_
#define _SRP_HBA_H_

#include <iba/ioc_ifc.h>
#include <complib/cl_event.h>
#include <complib/cl_obj.h>
#include <complib/cl_qlist.h>

#define SRP_MAX_SERVICE_ENTRIES 255

typedef struct _srp_session *p_srp_session_t;


#pragma warning(disable:4324)
typedef struct _srp_path_record
{
	cl_list_item_t	list_item;
	ib_path_rec_t	path_rec;

}	srp_path_record_t;
#pragma warning(default:4324)


typedef struct _srp_hba
{
	cl_obj_t				obj;
	cl_obj_rel_t			rel;

	/* The extension is	needed for StorPort	calls. */
	struct _srp_ext			*p_ext;

	ib_al_handle_t			h_al;
	ib_pnp_handle_t			h_pnp;

	ib_al_ifc_t				ifc;
	ioc_ifc_data_t			info;

	ib_ioc_info_t			ioc_info;
	ib_svc_entry_t			*p_svc_entries;

	srp_path_record_t		*p_srp_path_record;
	cl_qlist_t				path_record_list;
	cl_spinlock_t			path_record_list_lock;
	BOOLEAN					adapter_stopped;

	uint32_t				max_sg;
	uint32_t				max_srb_ext_sz;
	/* List	of sessions	indexed	by target id */
	p_srp_session_t			session_list[SRP_MAX_SERVICE_ENTRIES];
	BOOLEAN					session_paused[SRP_MAX_SERVICE_ENTRIES];
}	srp_hba_t;


/* Pointer to the PDO for an instance being initialized. */
DEVICE_OBJECT		*gp_self_do;


ib_api_status_t
srp_hba_create(
	IN				cl_obj_t* const				p_drv_obj,
		OUT			struct _srp_ext* const		p_ext );

void
srp_disconnect_sessions(
	IN				srp_hba_t					*p_hba );

#endif	/* _SRP_HBA_H_ */
