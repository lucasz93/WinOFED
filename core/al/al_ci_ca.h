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

#if !defined(__AL_CI_CA_H__)
#define __AL_CI_CA_H__

#include <complib/cl_types.h>
#ifdef CL_KERNEL
#include <iba/ib_ci.h>
#include <complib/cl_atomic.h>
#else
#include "ual_ci_ca.h"
#endif	/* CL_KERNEL */


#include <complib/cl_event.h>
#include <complib/cl_qlist.h>
#include <complib/cl_passivelock.h>

#include "al_common.h"


#ifdef CL_KERNEL
typedef ci_interface_t			verbs_interface_t;

ib_api_status_t
create_ci_ca(
	IN				al_obj_t					*p_parent_obj,
	IN		const	ci_interface_t*				p_ci,
	IN		const	PDEVICE_OBJECT				p_hca_dev,
	IN		const	PDEVICE_OBJECT				p_fdo
	);

DEVICE_OBJECT*
get_ca_dev(
	IN		const	ib_ca_handle_t				h_ca );
#endif

#define MAX_AE				32
typedef struct _al_ae_info {
	ib_pnp_event_t			pnp_event;
	uint8_t					port_index;
} al_ae_info_t;


typedef struct _al_ci_ca
{
	al_obj_t					obj;
	cl_list_item_t				list_item;

	cl_async_proc_item_t		dereg_async_item;

	verbs_interface_t			verbs;

	ib_ca_handle_t				h_ci_ca;		/* CI handle */
	ib_ca_handle_t				h_ca;			/* AL handle */
	ib_pd_handle_t				h_pd;			/* AL handle */
	ib_pd_handle_t				h_pd_alias;		/* AL handle */
	ib_pool_key_t				pool_key;		/* AL handle */

	/* Opened instances of this CA. */
	cl_qlist_t					ca_list;

	/*
	 * Last known attributes as reported by the PnP manager.
	 * Updated by the PnP manager through the asynchronous processing thread.
	 */
	ib_ca_attr_t				*p_pnp_attr;
	ib_ca_attr_t				*p_user_attr;
	cl_spinlock_t				attr_lock;

	cl_qpool_t					event_pool;

	uint8_t						num_ports;

	/* Shared memory registrations across processes. */
	cl_qlist_t					shmid_list;

	/* Array of port GUIDs on this CI CA. */
	ib_net64_t					*port_array;

	/* "end of PnP handling" event */
	cl_event_t					event;

	/* Array of pending AEs (Asynchronic events) */
	al_ae_info_t				ae[MAX_AE];	/* pending Asynchronic Events */		
	int 						ci;			/* Consumer Index - first event to handle */
	int 						pi;			/* Producer index - next not handled event */
	int							cnt;		/* number of not handled events */

}	al_ci_ca_t;


ib_api_status_t
get_port_info(
	IN				al_ci_ca_t					*p_ci_ca );


/*
 * Asynchronous event reporting.
 */
typedef struct _event_item
{
	cl_async_proc_item_t		async_item;
	ib_async_event_rec_t		event_rec;

}	event_item_t;


void
add_ca(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_ca_handle_t				h_ca );

void
remove_ca(
	IN		const	ib_ca_handle_t				h_ca );


void
ca_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec );

void
free_ci_ca(
	IN				al_obj_t*					p_obj );


void
ci_ca_async_event(
	IN		const	ib_async_event_rec_t* const	p_event_rec );


struct _al_shmid;

void
add_shmid(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN				struct _al_shmid			*p_shmid );

ib_api_status_t
acquire_shmid(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN				int							shmid,
		OUT			struct _al_shmid			**pp_shmid );

void
release_shmid(
	IN				struct _al_shmid			*p_shmid );



ib_api_status_t
get_port_num(
	IN				al_ci_ca_t* const			p_ci_ca,
	IN		const	ib_net64_t					port_guid,
		OUT			uint8_t						*p_port_num OPTIONAL );

ib_port_attr_t*
get_port_attr(
	IN				ib_ca_attr_t * const		p_ca_attr,
	IN				ib_gid_t * const			p_gid );


#define BAD_PKEY_INDEX	0xFFFF

uint16_t
get_pkey_index(
	IN				ib_port_attr_t * const		p_port_attr,
	IN		const	ib_net16_t					pkey );

ib_api_status_t
ci_ca_update_attr(
	IN				al_ci_ca_t*					p_ci_ca,
		OUT			ib_ca_attr_t**				pp_old_pnp_attr );



#define ci_ca_lock_attr(p_ci_ca) \
	CL_ASSERT( p_ci_ca ); \
	cl_spinlock_acquire( &p_ci_ca->attr_lock );


#define ci_ca_excl_lock_attr(p_ci_ca ) \
	CL_ASSERT( p_ci_ca ); \
	cl_spinlock_acquire( &p_ci_ca->attr_lock );

#define ci_ca_unlock_attr(p_ci_ca) \
	CL_ASSERT( p_ci_ca ); \
	cl_spinlock_release( &p_ci_ca->attr_lock );


ib_api_status_t
ci_call(
	IN				ib_ca_handle_t				h_ca,
	IN		const	void**				const	handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t*			const	p_ci_op,
	IN				ci_umv_buf_t*		const	p_umv_buf OPTIONAL );

void
destroy_outstanding_mads(	
	IN				al_obj_t*					p_obj );

#endif /* __AL_CI_CA_H__ */
