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

#if !defined(__AL_PNP_H__)
#define __AL_PNP_H__


#include "al_common.h"
#include "al_ca.h"
#include <iba/ib_al_ioctl.h>
#include <complib/cl_ptr_vector.h>
#include <complib/cl_fleximap.h>


typedef struct _al_pnp
{
	al_obj_t				obj;

	cl_async_proc_item_t	async_item;
#if defined( CL_KERNEL )
	KEVENT					*p_sync_event;
	cl_list_item_t			list_item;
	cl_async_proc_item_t	dereg_item;
	ib_pnp_class_t			pnp_class;
	cl_fmap_t				context_map;
	IRP						*p_rearm_irp;
	IRP						*p_dereg_irp;
#else	/* defined( CL_KERNEL ) */
	ual_rearm_pnp_ioctl_out_t	rearm;
	OVERLAPPED					ov;
	OVERLAPPED					destroy_ov;
#endif	/* defined( CL_KERNEL ) */
	ib_pfn_pnp_cb_t			pfn_pnp_cb;

}	al_pnp_t;
/*
* FIELDS
*	obj
*		AL object, used to manage relationships and destruction
*		synchronization.
*
*	list_item
*		Used to store the registration in the proper list for the class.
*
*	async_item
*		Asynchronous processing item used to handle registration and events.
*
*	dereg_item
*		Asynchronous processing item used to handle deregistration.  This item
*		is separate from the registration item to allow a user to immediately
*		call ib_dereg_pnp after ib_reg_pnp.
*
*	pnp_class
*		Class of PnP events for this registration.
*
*	pfn_pnp_cb
*		Client's PnP notification callback.
*
*	context_map
*		map of client contexts.
*********/


/*
 * Context information stored in a registration structure.
 */
typedef struct _al_pnp_context
{
	/* List item must be first. */
	cl_fmap_item_t			map_item;
	ib_net64_t				guid;
	ib_net64_t				ca_guid;
	const void				*context;

}	al_pnp_context_t;

/****f* Access Layer/create_pnp
* DESCRIPTION
*	Initialized the plug and play manager.
*
* SYNOPSIS
*/
ib_api_status_t
create_pnp(
	IN				al_obj_t* const			p_parent_obj );
/******/


#ifdef CL_KERNEL

/****f* Access Layer/al_pnp_ca_event
* NAME
*	pnp_ca_event
*
* DESCRIPTION
*	Reports a CA event to the plug and play manager.
*
* SYNOPSIS
*/
ib_api_status_t
pnp_ca_event(
	IN				al_ci_ca_t* const		p_ci_ca,
	IN		const	ib_pnp_event_t			event );
/*
* PARAMETERS
*	p_ci_ca
*		Pointer to the al_ci_ca_t structure for the ca for which the event
*		is being reported.
*
*	event
*		One of IB_PNP_CA_ADD, IB_PNP_CA_REMOVE to indicate the type of CA
*		event being reported.
*****/


/****f* Access Layer/pnp_ca_change
* NAME
*	pnp_ca_change
*
* DESCRIPTION
*	Called by user mode AL to report a CA attribute change.
*
* SYNOPSIS
*/
ib_api_status_t
pnp_ca_change(
	IN				al_ci_ca_t* const		p_ci_ca,
	IN		const	ib_ca_attr_t*			p_ca_attr );
/*
* PARAMETERS
*	p_ci_ca
*		Pointer to the al_ci_ca_t structure for the ca for which the change
*		is being reported.
*
*	p_ca_attr
*		Pointer to the updated CA attributes.
*****/


/****f* Access Layer/pnp_check_events
* NAME
*	pnp_poll
*
* DESCRIPTION
*	Check for PnP new events and report changes to registered clients.
*
* SYNOPSIS
*/
void
pnp_poll(
	void );
/******/


/****f* Access Layer/pnp_create_context
* NAME
*	pnp_create_context
*
* DESCRIPTION
*	Creates a context structure for a reported PnP event.
*
* SYNOPSIS
*/
al_pnp_context_t*
pnp_create_context(
	IN				al_pnp_t* const				p_reg,
	IN		const	void* const					p_key );
/******/

al_pnp_context_t*
pnp_get_context(
	IN		const	al_pnp_t* const				p_reg,
	IN		const	void* const					p_key );

void
pnp_reg_complete(
	IN				al_pnp_t* const				p_reg );

ib_api_status_t
al_reg_pnp(
	IN		const	ib_al_handle_t				h_al,
	IN		const	ib_pnp_req_t* const			p_pnp_req,
	IN				KEVENT						*p_sync_event,
		OUT			ib_pnp_handle_t* const		ph_pnp );

void
pnp_force_event(
	IN 		struct _al_ci_ca	 * p_ci_ca,
	IN 		ib_pnp_event_t pnp_event,
	IN 		uint8_t port_num);


#endif	/* CL_KERNEL */

static inline ib_pnp_class_t
pnp_get_class(
	IN		const	ib_pnp_class_t				pnp_class )
{
	return pnp_class & IB_PNP_CLASS_MASK;
}

static inline ib_pnp_class_t
pnp_get_flag(
	IN		const	ib_pnp_class_t				pnp_class )
{
	return pnp_class & IB_PNP_FLAG_MASK;
}

#endif	/* __AL_PNP_H__ */
