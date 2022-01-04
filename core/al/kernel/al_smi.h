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


#if !defined( __AL_SMI_H__ )
#define __AL_SMI_H__


#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include "al_common.h"
#include "al_mad.h"


/* Global special QP manager */
typedef struct _spl_qp_mgr
{
	al_obj_t					obj;		/* Child of gp_al_mgr */
	ib_pnp_handle_t				h_qp0_pnp;	/* Handle for QP0 port PnP events */
	ib_pnp_handle_t				h_qp1_pnp;	/* Handle for QP1 port PnP events */

	cl_timer_t					poll_timer;	/* Timer for polling HW SMIs */

	cl_qmap_t					smi_map;	/* List of SMI services */
	cl_qmap_t					gsi_map;	/* List of GSI services */

}	spl_qp_mgr_t;



typedef enum _spl_qp_svc_state
{
	SPL_QP_INIT = 0,
	SPL_QP_ACTIVE,
	SPL_QP_ERROR,
	SPL_QP_DESTROYING

}	spl_qp_svc_state_t;

/*
 * Attribute cache for port info saved to expedite local MAD processing.
 * Note that the cache accounts for the worst case GID and PKEY table size
 * but is allocated from paged pool, so it's nothing to worry about.
 */

typedef struct _guid_block
{
	boolean_t				valid;
	ib_guid_info_t			tbl;

}	guid_block_t;


typedef struct _pkey_block
{
	boolean_t				valid;
	ib_pkey_table_t	tbl;

}	pkey_block_t;

typedef struct _sl_vl_cache
{
	boolean_t				valid;
	ib_slvl_table_t			tbl;

}	sl_vl_cache_t;

typedef struct _vl_arb_block
{
	boolean_t				valid;
	ib_vl_arb_table_t		tbl;

}	vl_arb_block_t;

typedef struct _attr_cache
{
	guid_block_t		guid_block[32];
	pkey_block_t		pkey_tbl[2048];
	sl_vl_cache_t		sl_vl;
	vl_arb_block_t	vl_arb[4];

}	spl_qp_cache_t;


/* Per port special QP service */
typedef struct _spl_qp_svc
{
	al_obj_t						obj;		/* Child of spl_qp_agent_t */
	cl_map_item_t				map_item;	/* Item on SMI/GSI list */

	net64_t						port_guid;
	uint8_t						port_num;
	ib_net16_t					base_lid;
	uint8_t						lmc;
	
	ib_net16_t					sm_lid;
	uint8_t						sm_sl;
	ib_net64_t					m_key;

	spl_qp_cache_t				cache;
	cl_spinlock_t					cache_lock;
	boolean_t					cache_en;
	
	al_mad_disp_handle_t			h_mad_disp;
	ib_cq_handle_t				h_send_cq;
	ib_cq_handle_t				h_recv_cq;
	ib_qp_handle_t				h_qp;

#if defined( CL_USE_MUTEX )
	boolean_t					send_async_queued;
	cl_async_proc_item_t		send_async_cb;
	boolean_t					recv_async_queued;
	cl_async_proc_item_t		recv_async_cb;
#endif

	spl_qp_svc_state_t			state;
	atomic32_t					in_use_cnt;
	cl_async_proc_item_t		reset_async;

	uint32_t					max_qp_depth;
	al_mad_wr_t*				local_mad_wr;
	cl_qlist_t					send_queue;
	cl_qlist_t					recv_queue;
	cl_async_proc_item_t		send_async;

	ib_qp_handle_t				h_qp_alias;
	ib_pool_key_t				pool_key;
	ib_mad_svc_handle_t			h_mad_svc;

    KDPC                        send_dpc;
    KDPC                        recv_dpc;

}	spl_qp_svc_t;


typedef enum _mad_route
{
	ROUTE_DISPATCHER = 0,
	ROUTE_REMOTE,
	ROUTE_LOCAL,
	ROUTE_LOOPBACK,
	ROUTE_DISCARD

}	mad_route_t;


static inline boolean_t
is_dispatcher(
	IN		const	mad_route_t					route )
{
	return( route == ROUTE_DISPATCHER );
}


static inline boolean_t
is_remote(
	IN		const	mad_route_t					route )
{
	return( route == ROUTE_REMOTE );
}


static inline boolean_t
is_discard(
	IN		const	mad_route_t					route )
{
	return( route == ROUTE_DISCARD );
}


static inline boolean_t
is_loopback(
	IN		const	mad_route_t					route )
{
	return( route == ROUTE_LOOPBACK );
}


static inline boolean_t
is_local(
	IN		const	mad_route_t					route )
{
	/*
	 * Loopback implies a locally routed MAD.  Discarded MADs are always
	 * handled locally to maintain proper order of work completions.
	 */
	return( ( route == ROUTE_LOCAL ) ||
		is_loopback( route ) || is_discard( route ) );
}


ib_api_status_t
create_spl_qp_mgr(
	IN				al_obj_t*	const			p_parent_obj );


ib_api_status_t
acquire_smi_disp(
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t* const	ph_mad_disp );


ib_api_status_t
acquire_gsi_disp(
	IN		const	ib_net64_t					port_guid,
		OUT			al_mad_disp_handle_t* const	ph_mad_disp );


ib_api_status_t
spl_qp_svc_send(
	IN		const	ib_qp_handle_t				h_qp,
	IN				ib_send_wr_t* const			p_send_wr );


void
force_smi_poll(
	void );


#endif
