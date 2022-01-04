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

#if !defined(__AL_DM_H__)
#define __AL_DM_H__

#include <iba/ib_al.h>
#include "al_common.h"


typedef struct _dm_agent		/* Global device management agent struct */
{
	al_obj_t					obj;		/* Child of al_mgr_t */
	ib_pnp_handle_t				h_ca_pnp;	/* Handle for CA PnP events */
	ib_pnp_handle_t				h_port_pnp;	/* Handle for Port PnP events */

	/*
	 * Lock and state to synchronize user-mode device management
	 * agent destruction with PnP callbacks to create IO units.
	 */
	cl_spinlock_t				lock;
	boolean_t					destroying;

}	dm_agent_t;


typedef struct _al_iou			/* IO unit struct - max of one per CA */
{
	al_obj_t					obj;		/* Child of dm_agent_t */

	uint16_t					change_id;
	cl_qlist_t					ioc_list;	/* List of IOCs */

}	al_iou_t;


typedef struct _al_iou_port		/* Per-port object of an IO unit */
{
	al_obj_t					obj;		/* Child of al_iou_t */

	uint8_t						port_num;
	net64_t						port_guid;
	ib_gid_t					port_gid;
	ib_net16_t					port_pkey;
	ib_qp_handle_t				h_qp_alias;
	ib_mad_svc_handle_t			h_mad_svc;

	ib_pool_key_t				pool_key;

	ib_reg_svc_handle_t			svc_handle;	/* Service registration handle */

}	al_iou_port_t;


typedef enum _ioc_state			/* An IOC represents a slot in an IO unit */
{
	IOC_INIT = 0,
	EMPTY_SLOT,
	SLOT_IN_USE,
	IOC_ACTIVE

}	ioc_state_t;


#pragma warning(disable:4324)
typedef struct _al_ioc
{
	al_obj_t					obj;		/* Child of ib_ca_t */

	cl_list_item_t				iou_item;	/* Item on IO Unit list */
	al_iou_t*					p_iou;

	ioc_state_t					state;
	ib_ioc_profile_t			ioc_profile;

	atomic32_t					in_use_cnt;

}	al_ioc_t;
#pragma warning(default:4324)


typedef struct _al_svc_entry
{
	al_obj_t					obj;		/* Child of al_ioc_t */
	ib_svc_entry_t				svc_entry;

}	al_svc_entry_t;


ib_api_status_t
create_dm_agent(
	IN				al_obj_t*	const			p_parent_obj );


#endif /* __AL_DM_H__ */
