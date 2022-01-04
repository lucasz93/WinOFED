/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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


#include <iba/ib_al.h>
#include <complib/cl_vector.h>
#include <complib/cl_rbmap.h>
#include <complib/cl_qmap.h>
#include <complib/cl_spinlock.h>
#include <iba/ib_al_ifc.h>
#include <iba/ib_cm_ifc.h>
#include <limits.h>
#include "al_common.h"
#include "al_cm_cep.h"
#include "al_cm_conn.h"
#include "al_cm_sidr.h"
#include "al_debug.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_cm_cep.tmh"
#endif
#include "ib_common.h"
#include "al_mgr.h"
#include "al_ca.h"
#include "al.h"
#include "al_mad.h"
#include "al_qp.h"


/*
 * The vector object uses a list item at the front of the buffers
 * it allocates.  Take the list item into account so that allocations
 * are for full page sizes.
 */
#define CEP_CID_MIN			\
	((PAGE_SIZE - sizeof(cl_list_item_t)) / sizeof(cep_cid_t))
#define CEP_CID_GROW		\
	((PAGE_SIZE - sizeof(cl_list_item_t)) / sizeof(cep_cid_t))

/*
 * We reserve the upper byte of the connection ID as a revolving counter so
 * that connections that are retried by the client change connection ID.
 * This counter is never zero, so it is OK to use all CIDs since we will never
 * have a full CID (base + counter) that is zero.
 * See the IB spec, section 12.9.8.7 for details about REJ retry.
 */
#define CEP_MAX_CID					(0x00FFFFFF)
#define CEP_MAX_CID_MASK			(0x00FFFFFF)

#define CEP_MAD_SQ_DEPTH			(128)
#define CEP_MAD_RQ_DEPTH			(1)	/* ignored. */
#define CEP_MAD_SQ_SGE				(1)
#define CEP_MAD_RQ_SGE				(1)	/* ignored. */


/* Global connection manager object. */
typedef struct _al_cep_mgr
{
	al_obj_t				obj;

	cl_qmap_t				port_map;

	KSPIN_LOCK				lock;

	/* Bitmap of CEPs, indexed by CID. */
	cl_vector_t				cid_vector;
	uint32_t				free_cid;

	/* List of active listens. */
	cl_rbmap_t				listen_map;

	/* Map of CEP by remote CID and CA GUID. */
	cl_rbmap_t				conn_id_map;
	/* Map of CEP by remote QPN, used for stale connection matching. */
	cl_rbmap_t				conn_qp_map;

	NPAGED_LOOKASIDE_LIST	cep_pool;
	NPAGED_LOOKASIDE_LIST	req_pool;

	/*
	 * Periodically walk the list of connections in the time wait state
	 * and flush them as appropriate.
	 */
	cl_timer_t				timewait_timer;
	cl_qlist_t				timewait_list;

	ib_pnp_handle_t			h_pnp;

}	al_cep_mgr_t;


/* Per-port CM object. */
typedef struct _cep_port_agent
{
	al_obj_t			obj;

	cl_map_item_t		item;

	ib_ca_handle_t		h_ca;
	ib_pd_handle_t		h_pd;
	ib_qp_handle_t		h_qp;
	ib_pool_key_t		pool_key;
	ib_mad_svc_handle_t	h_mad_svc;

	net64_t				port_guid;
	uint8_t				port_num;
	net16_t				base_lid;

}	cep_agent_t;


/*
 * Note: the REQ, REP, and LAP values must be 1, 2, and 4 respectively.
 * This allows shifting 1 << msg_mraed from an MRA to figure out for what
 * message the MRA was sent for.
 */
#define CEP_STATE_RCVD			0x10000000
#define CEP_STATE_SENT			0x20000000
#define CEP_STATE_MRA			0x01000000
#define CEP_STATE_REQ			0x00000001
#define CEP_STATE_REP			0x00000002
#define CEP_STATE_LAP			0x00000004
#define CEP_STATE_RTU			0x00000008
#define CEP_STATE_DREQ			0x00000010
#define CEP_STATE_DREP			0x00000020
#define CEP_STATE_DESTROYING	0x00010000
#define CEP_STATE_USER			0x00020000

#define CEP_MSG_MASK			0x000000FF
#define CEP_OP_MASK				0xF0000000

#define CEP_STATE_PREP			0x00100000

/* States match CM state transition diagrams from spec. */
typedef enum _cep_state
{
	CEP_STATE_IDLE,
	CEP_STATE_LISTEN,
	CEP_STATE_ESTABLISHED,
	CEP_STATE_TIMEWAIT,
	CEP_STATE_SREQ_SENT,
	CEP_STATE_SREQ_RCVD,
	CEP_STATE_ERROR,
	CEP_STATE_DESTROY = CEP_STATE_DESTROYING,
	CEP_STATE_PRE_REQ = CEP_STATE_IDLE | CEP_STATE_PREP,
	CEP_STATE_REQ_RCVD = CEP_STATE_REQ | CEP_STATE_RCVD,
	CEP_STATE_PRE_REP = CEP_STATE_REQ_RCVD | CEP_STATE_PREP,
	CEP_STATE_REQ_SENT = CEP_STATE_REQ | CEP_STATE_SENT,
	CEP_STATE_REQ_MRA_RCVD = CEP_STATE_REQ_SENT | CEP_STATE_MRA,
	CEP_STATE_REQ_MRA_SENT = CEP_STATE_REQ_RCVD | CEP_STATE_MRA,
	CEP_STATE_PRE_REP_MRA_SENT = CEP_STATE_REQ_MRA_SENT | CEP_STATE_PREP,
	CEP_STATE_REP_RCVD = CEP_STATE_REP | CEP_STATE_RCVD,
	CEP_STATE_REP_SENT = CEP_STATE_REP | CEP_STATE_SENT,
	CEP_STATE_REP_MRA_RCVD = CEP_STATE_REP_SENT | CEP_STATE_MRA,
	CEP_STATE_REP_MRA_SENT = CEP_STATE_REP_RCVD | CEP_STATE_MRA,
	CEP_STATE_LAP_RCVD = CEP_STATE_LAP | CEP_STATE_RCVD,
	CEP_STATE_PRE_APR = CEP_STATE_LAP_RCVD | CEP_STATE_PREP,
	CEP_STATE_LAP_SENT = CEP_STATE_LAP | CEP_STATE_SENT,
	CEP_STATE_LAP_MRA_RCVD = CEP_STATE_LAP_SENT | CEP_STATE_MRA,
	CEP_STATE_LAP_MRA_SENT = CEP_STATE_LAP_RCVD | CEP_STATE_MRA,
	CEP_STATE_PRE_APR_MRA_SENT = CEP_STATE_LAP_MRA_SENT | CEP_STATE_PREP,
	CEP_STATE_DREQ_SENT = CEP_STATE_DREQ | CEP_STATE_SENT,
	CEP_STATE_DREQ_RCVD = CEP_STATE_DREQ | CEP_STATE_RCVD,
	CEP_STATE_DREQ_DESTROY = CEP_STATE_DREQ_SENT | CEP_STATE_DESTROYING

} cep_state_t;


/* Active side CEP state transitions:
*	al_create_cep	-> IDLE
*	al_cep_pre_req	-> PRE_REQ
*	al_cep_send_req	-> REQ_SENT
*	Recv REQ MRA	-> REQ_MRA_RCVD
*	Recv REP		-> REP_RCVD
*	al_cep_mra		-> REP_MRA_SENT
*	al_cep_rtu		-> ESTABLISHED
*
* Passive side CEP state transitions:
*	al_create_cep	-> IDLE
*	Recv REQ		-> REQ_RCVD
*	al_cep_mra*		-> REQ_MRA_SENT
*	al_cep_pre_rep	-> PRE_REP
*	al_cep_mra*		-> PRE_REP_MRA_SENT
*	al_cep_send_rep	-> REP_SENT
*	Recv RTU		-> ESTABLISHED
*
*	*al_cep_mra can only be called once - either before or after PRE_REP.
*/

#define CEP_AV_FLAG_GENERATE_PRIO_TAG			0x1

typedef struct _al_kcep_av
{
	ib_av_attr_t				attr;
	net64_t						port_guid;
	uint16_t					pkey_index;
	uint32_t					flags;
}	kcep_av_t;


typedef struct _al_kcep
{
	net32_t						cid;
	void*						context;

	struct _cep_cid				*p_cid;

	net64_t						sid;

	/* Port guid for filtering incoming requests. */
	net64_t						port_guid;

	uint8_t*					p_cmp_buf;
	uint8_t						cmp_offset;
	uint8_t						cmp_len;

	boolean_t					p2p;

	/* Used to store connection structure with owning AL instance. */
	cl_list_item_t				al_item;

	/* Flag to indicate whether a user is processing events. */
	boolean_t					signalled;

	/* Destroy callback. */
	ib_pfn_destroy_cb_t			pfn_destroy_cb;

	ib_mad_element_t			*p_mad_head;
	ib_mad_element_t			*p_mad_tail;
	al_pfn_cep_cb_t				pfn_cb;

	IRP							*p_irp;

	/* MAP item for finding listen CEPs. */
	cl_rbmap_item_t				listen_item;

	/* Map item for finding CEPs based on remote comm ID & CA GUID. */
	cl_rbmap_item_t				rem_id_item;

	/* Map item for finding CEPs based on remote QP number. */
	cl_rbmap_item_t				rem_qp_item;

	/* Communication ID's for the connection. */
	net32_t						local_comm_id;
	net32_t						remote_comm_id;

	net64_t						local_ca_guid;
	net64_t						remote_ca_guid;

	/* Remote QP, used for stale connection checking. */
	net32_t						remote_qpn;

	/* Parameters to format QP modification structure. */
	net32_t						sq_psn;
	net32_t						rq_psn;
	/*
     * Note that we store the requested initiator depth as received in the REQ
     * and cap it when sending the REP to the actual capabilities of the HCA.
     */
	uint8_t						resp_res;
	uint8_t						init_depth;
	uint8_t						rnr_nak_timeout;

	/*
	 * Local QP number, used for the "additional check" required
	 * of the DREQ.
	 */
	net32_t						local_qpn;

	/* PKEY to make sure a LAP is on the same partition. */
	net16_t						pkey;

	/*
	 * Primary and alternate path info, used to create the address vectors for
	 * sending MADs, to locate the port CM agent to use for outgoing sends,
	 * and for creating the address vectors for transitioning QPs.
	 */
	kcep_av_t					av[2];
	uint8_t						idx_primary;

	/* Temporary AV and CEP port GUID used when processing LAP. */
	kcep_av_t					alt_av;
	uint8_t						alt_2pkt_life;

	/* maxium packet lifetime * 2 of any path used on a connection. */
	uint8_t						max_2pkt_life;
	/* Given by the REP, used for alternate path setup. */
	uint8_t						target_ack_delay;
	/* Stored to help calculate the local ACK delay in the LAP. */
	uint8_t						local_ack_delay;

	/* Volatile to allow using atomic operations for state checks. */
	cep_state_t					state;

	/*
	 * Flag that indicates whether a connection took the active role during
	 * establishment. 
	 */
	boolean_t					was_active;

	/*
	 * Handle to the sent MAD, used for cancelling. We store the handle to
	 * the mad service so that we can properly cancel.  This should not be a
	 * problem since all outstanding sends should be completed before the
	 * mad service completes its destruction and the handle becomes invalid.
	 */
	ib_mad_svc_handle_t			h_mad_svc;
	ib_mad_element_t			*p_send_mad;

	atomic32_t					ref_cnt;

	/* MAD transaction ID to use when sending MADs. */
	uint64_t					tid;

	/* Maximum retries per MAD.  Set at REQ time, stored to retry LAP. */
	uint8_t						max_cm_retries;
	/* Timeout value, in milliseconds. Set at REQ time, stored to retry LAP. */
	uint32_t					retry_timeout;

	/* Timer that will be signalled when the CEP exits timewait. */
	KTIMER						timewait_timer;
	LARGE_INTEGER				timewait_time;
	cl_list_item_t				timewait_item;

	/*
	 * Pointer to a formatted MAD.  The pre_req, pre_rep and pre_apr calls
	 * allocate and format the MAD, and the send_req, send_rep and send_apr
	 * calls send it.
	 */
	ib_mad_element_t			*p_mad;

	/* Cache the last MAD sent for retransmission. */
	union _mads
	{
		ib_mad_t				hdr;
		mad_cm_mra_t			mra;
		mad_cm_rtu_t			rtu;
		mad_cm_drep_t			drep;

	}	mads;

	/*
	 * NDI stuff - TODO: manage above core kernel CM code
	 */

	/* private data of REQ, REP, REJ CM requests */	
	uint8_t						psize;
	uint8_t						pdata[IB_REP_PDATA_SIZE];

}	kcep_t;


/* Structures stored in the CID vector. */
typedef struct _cep_cid
{
	/* Owning AL handle.  NULL if invalid. */
	ib_al_handle_t	h_al;
	/* Pointer to CEP, or index of next free entry if h_al is NULL. */
	kcep_t			*p_cep;
	/* For REJ Retry support */
	uint8_t			modifier;

}	cep_cid_t;


/* Global instance of the CM agent. */
al_cep_mgr_t		*gp_cep_mgr = NULL;


static ib_api_status_t
__format_drep(
	IN				kcep_t* const				p_cep,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len,
	IN	OUT			mad_cm_drep_t* const		p_drep );

static ib_api_status_t
__cep_queue_mad(
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t*			p_mad );

static inline void
__process_cep(
	IN				kcep_t* const				p_cep );

static inline uint32_t
__calc_mad_timeout(
	IN		const	uint8_t						pkt_life );

static inline void
__calc_timewait(
	IN				kcep_t* const				p_cep );

static kcep_t*
__create_cep( void );

static int32_t
__cleanup_cep(
	IN				kcep_t* const				p_cep );

static void
__destroy_cep(
	IN				kcep_t* const				p_cep );

static inline void
__bind_cep(
	IN				kcep_t* const				p_cep,
	IN				ib_al_handle_t				h_al,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context );

static inline void
__unbind_cep(
	IN				kcep_t* const				p_cep );

static void
__pre_destroy_cep(
	IN				kcep_t* const				p_cep );

static kcep_t*
__lookup_by_id(
	IN				net32_t						remote_comm_id,
	IN				net64_t						remote_ca_guid );

static kcep_t*
__lookup_listen(
	IN				net64_t						sid,
	IN				net64_t						port_guid,
	IN				void						*p_pdata );

static inline kcep_t*
__lookup_cep(
	IN				ib_al_handle_t				h_al OPTIONAL,
	IN				net32_t						cid );

static inline kcep_t*
__insert_cep(
	IN				kcep_t* const				p_new_cep );

static inline void
__remove_cep(
	IN				kcep_t* const				p_cep );

static inline void
__insert_timewait(
	IN				kcep_t* const				p_cep );

static ib_api_status_t
__cep_get_mad(
	IN				kcep_t* const				p_cep,
	IN				net16_t						attr_id,
		OUT			cep_agent_t** const			pp_port_cep,
		OUT			ib_mad_element_t** const	pp_mad );

static ib_api_status_t
__cep_send_mad(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad );

/* Returns the 1-based port index of the CEP agent with the specified GID. */
static cep_agent_t*
__find_port_cep(
	IN		const	ib_gid_t* const				p_gid,
	IN		const	net16_t						lid,
	IN		const	net16_t						pkey,
		OUT			uint16_t* const				p_pkey_index );

static cep_cid_t*
__get_lcid(
		OUT			net32_t* const				p_cid );

static void
__process_cep_send_comp(
	IN				cl_async_proc_item_t		*p_item );


/******************************************************************************
* Per-port CEP agent
******************************************************************************/


static inline void
__format_mad_hdr(
	IN				ib_mad_t* const				p_mad,
	IN		const	kcep_t* const				p_cep,
	IN				net16_t						attr_id )
{
	p_mad->base_ver = 1;
	p_mad->mgmt_class = IB_MCLASS_COMM_MGMT;
	p_mad->class_ver = IB_MCLASS_CM_VER_2;
	p_mad->method = IB_MAD_METHOD_SEND;
	p_mad->status = 0;
	p_mad->class_spec = 0;
	p_mad->trans_id = p_cep->tid;
	p_mad->attr_id = attr_id;
	p_mad->resv = 0;
	p_mad->attr_mod = 0;
}


/* Consumes the input MAD. */
static void
__reject_mad(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t* const		p_mad,
	IN				ib_rej_status_t				reason )
{
	mad_cm_rej_t		*p_rej;

	AL_ENTER( AL_DBG_CM );

	p_rej = (mad_cm_rej_t*)p_mad->p_mad_buf;

	__format_mad_hdr( p_mad->p_mad_buf, p_cep, CM_REJ_ATTR_ID );

	p_rej->local_comm_id = p_cep->local_comm_id;
	p_rej->remote_comm_id = p_cep->remote_comm_id;
	p_rej->reason = reason;

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		conn_rej_set_msg_rejected( 0, p_rej );
		break;

	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
		conn_rej_set_msg_rejected( 1, p_rej );
		break;

	default:
		CL_ASSERT( reason == IB_REJ_TIMEOUT );
		conn_rej_set_msg_rejected( 2, p_rej );
		break;
	}

	conn_rej_clr_rsvd_fields( p_rej );
	__cep_send_mad( p_port_cep, p_mad );

	AL_EXIT( AL_DBG_CM );
}


static void
__reject_timeout(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN		const	ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	ib_mad_element_t	*p_rej_mad;
	ib_mad_t			*p_mad_buf;
	ib_grh_t			*p_grh;

	AL_ENTER( AL_DBG_CM );

	status = ib_get_mad( p_port_cep->pool_key, MAD_BLOCK_SIZE, &p_rej_mad );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_mad returned %s\n", ib_get_err_str( status )) );
		return;
	}

	/* Save the buffer pointers from the new element. */
	p_mad_buf = p_rej_mad->p_mad_buf;
	p_grh = p_rej_mad->p_grh;

	/*
	 * Copy the input MAD element to the reject - this gives us
	 * all appropriate addressing information.
	 */
	cl_memcpy( p_rej_mad, p_mad, sizeof(ib_mad_element_t) );
	cl_memcpy( p_grh, p_mad->p_grh, sizeof(ib_grh_t) );

	/* Restore the buffer pointers now that the copy is complete. */
	p_rej_mad->p_mad_buf = p_mad_buf;
	p_rej_mad->p_grh = p_grh;

	status = conn_rej_set_pdata( NULL, 0, (mad_cm_rej_t*)p_mad_buf );
	CL_ASSERT( status == IB_SUCCESS );

	/* Copy the local CA GUID into the ARI. */
	switch( p_mad->p_mad_buf->attr_id )
	{
	case CM_REQ_ATTR_ID:
		status = conn_rej_set_ari(
			(uint8_t*)&p_cep->local_ca_guid,
			sizeof(p_cep->local_ca_guid), (mad_cm_rej_t*)p_mad_buf );
		CL_ASSERT( status == IB_SUCCESS );
		__reject_mad( p_port_cep, p_cep, p_rej_mad, IB_REJ_TIMEOUT );
		break;

	case CM_REP_ATTR_ID:
		status = conn_rej_set_ari(
			(uint8_t*)&p_cep->local_ca_guid,
			sizeof(p_cep->local_ca_guid), (mad_cm_rej_t*)p_mad_buf );
		CL_ASSERT( status == IB_SUCCESS );
		__reject_mad( p_port_cep, p_cep, p_rej_mad, IB_REJ_TIMEOUT );
		break;

	default:
		CL_ASSERT( p_mad->p_mad_buf->attr_id == CM_REQ_ATTR_ID ||
			p_mad->p_mad_buf->attr_id == CM_REP_ATTR_ID );
		ib_put_mad( p_rej_mad );
		return;
	}

	AL_EXIT( AL_DBG_CM );
}


static void
__reject_req(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad,
	IN		const	ib_rej_status_t				reason )
{
	mad_cm_req_t	*p_req;
	mad_cm_rej_t	*p_rej;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_port_cep );
	CL_ASSERT( p_mad );
	CL_ASSERT( reason != 0 );

	p_req = (mad_cm_req_t*)p_mad->p_mad_buf;
	p_rej = (mad_cm_rej_t*)p_mad->p_mad_buf;

	/*
	 * Format the reject information, overwriting the REQ data and send
	 * the response.
	 */
	p_rej->hdr.attr_id = CM_REJ_ATTR_ID;
	p_rej->remote_comm_id = p_req->local_comm_id;
	p_rej->local_comm_id = 0;
	conn_rej_set_msg_rejected( 0, p_rej );
	p_rej->reason = reason;
	conn_rej_set_ari( NULL, 0, p_rej );
	conn_rej_set_pdata( NULL, 0, p_rej );
	conn_rej_clr_rsvd_fields( p_rej );

	p_mad->retry_cnt = 0;
	p_mad->send_opt = 0;
	p_mad->timeout_ms = 0;
	p_mad->resp_expected = FALSE;

	/* Switch src and dst in GRH */
	if(p_mad->grh_valid)
	{
		ib_gid_t dest_gid = {0};
		
		memcpy(&dest_gid, &p_mad->p_grh->src_gid, sizeof(ib_gid_t));
		memcpy(&p_mad->p_grh->src_gid, &p_mad->p_grh->dest_gid, sizeof(ib_gid_t));
		memcpy(&p_mad->p_grh->dest_gid, &dest_gid, sizeof(ib_gid_t));
	}
	
	__cep_send_mad( p_port_cep, p_mad );

	AL_EXIT( AL_DBG_CM );
}


static void
__format_req_av(
	IN				kcep_t* const				p_cep,
	IN		const	mad_cm_req_t* const			p_req,
	IN		const	uint8_t						idx )
{
	cep_agent_t				*p_port_cep;
	const req_path_info_t	*p_path;
	int force_grh;
	int is_rdma_port;
	uint8_t	sl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cep );
	CL_ASSERT( p_req );

	cl_memclr( &p_cep->av[idx], sizeof(kcep_av_t) );

	p_path = &((&p_req->primary_path)[idx]);

	p_port_cep = __find_port_cep( &p_path->remote_gid,
		p_path->remote_lid, p_req->pkey, &p_cep->av[idx].pkey_index );
	if( !p_port_cep )
	{
		if( !idx )
			p_cep->local_ca_guid = 0;
		AL_EXIT( AL_DBG_CM );
		return;
	}

	if( !idx )
		p_cep->local_ca_guid = p_port_cep->h_ca->obj.p_ci_ca->verbs.guid;

	/* Check that CA GUIDs match if formatting the alternate path. */
	if( idx &&
		p_port_cep->h_ca->obj.p_ci_ca->verbs.guid != p_cep->local_ca_guid )
	{
		AL_EXIT( AL_DBG_CM );
		return;
	}

	/*
	 * Pkey indeces must match if formating the alternat path - the QP
	 * modify structure only allows for a single PKEY index to be specified.
	 */
	if( idx &&
		p_cep->av[0].pkey_index != p_cep->av[1].pkey_index )
	{
		AL_EXIT( AL_DBG_CM );
		return;
	}

	p_cep->av[idx].port_guid = p_port_cep->port_guid;
	p_cep->av[idx].attr.port_num = p_port_cep->port_num;

	is_rdma_port = p_port_cep->h_ca->obj.p_ci_ca->verbs.rdma_port_get_transport(
		p_port_cep->h_ca->obj.p_ci_ca->h_ci_ca, p_port_cep->port_num) == RDMA_TRANSPORT_RDMAOE;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("Received REQ SL %d", conn_req_path_get_svc_lvl( p_path )) );

	sl = p_port_cep->h_ca->obj.p_ci_ca->verbs.get_sl_for_ip_port(
			p_port_cep->h_ca->obj.p_ci_ca->h_ci_ca, p_port_cep->port_num, ib_cm_rdma_sid_port(p_cep->sid));

	if(! is_rdma_port || sl == (uint8_t) -1)
	{
		p_cep->av[idx].attr.sl = conn_req_path_get_svc_lvl( p_path );
		p_cep->av[idx].flags = 0;
	}
	else
	{
		p_cep->av[idx].attr.sl = sl;
		p_cep->av[idx].flags = CEP_AV_FLAG_GENERATE_PRIO_TAG;
	}

	p_cep->av[idx].attr.dlid = p_path->local_lid;

	
	force_grh = is_rdma_port;
	
	if(force_grh || !conn_req_path_get_subn_lcl( p_path ) )
	{
		p_cep->av[idx].attr.grh_valid = TRUE;
		p_cep->av[idx].attr.grh.ver_class_flow = ib_grh_set_ver_class_flow(
			1, p_path->traffic_class, conn_req_path_get_flow_lbl( p_path ) );
		p_cep->av[idx].attr.grh.hop_limit = p_path->hop_limit;
		p_cep->av[idx].attr.grh.dest_gid = p_path->local_gid;
		p_cep->av[idx].attr.grh.src_gid = p_path->remote_gid;
	}
	else
	{
		p_cep->av[idx].attr.grh_valid = FALSE;
	}
	p_cep->av[idx].attr.static_rate = conn_req_path_get_pkt_rate( p_path );
	p_cep->av[idx].attr.path_bits =
		(uint8_t)(p_path->remote_lid - p_port_cep->base_lid);

	/*
	 * Note that while we never use the connected AV attributes internally,
	 * we store them so we can pass them back to users.
	 */
	p_cep->av[idx].attr.conn.path_mtu = conn_req_get_mtu( p_req );
	p_cep->av[idx].attr.conn.local_ack_timeout =
		conn_req_path_get_lcl_ack_timeout( p_path );
	p_cep->av[idx].attr.conn.seq_err_retry_cnt =
		conn_req_get_retry_cnt( p_req );
	p_cep->av[idx].attr.conn.rnr_retry_cnt =
		conn_req_get_rnr_retry_cnt( p_req );

	AL_EXIT( AL_DBG_CM );
}


/*
 * + Validates the path information provided in the REQ and stores the
 *	 associated CA attributes and port indeces.
 * + Transitions a connection object from active to passive in the peer case.
 * + Sets the path information in the connection and sets the CA GUID
 *	 in the REQ callback record.
 */
static void
__save_wire_req(
	IN	OUT			kcep_t*	 const				p_cep,
	IN	OUT			mad_cm_req_t* const			p_req )
{
	AL_ENTER( AL_DBG_CM );

	p_cep->state = CEP_STATE_REQ_RCVD;
	p_cep->was_active = FALSE;

	p_cep->sid = p_req->sid;

	/* Store pertinent information in the connection. */
	p_cep->remote_comm_id = p_req->local_comm_id;
	p_cep->remote_ca_guid = p_req->local_ca_guid;

	p_cep->remote_qpn = conn_req_get_lcl_qpn( p_req );
	p_cep->local_qpn = 0;

	p_cep->retry_timeout =
		__calc_mad_timeout( conn_req_get_lcl_resp_timeout( p_req ) );

	/* Store the retry count. */
	p_cep->max_cm_retries = conn_req_get_max_cm_retries( p_req );

	/*
	 * Copy the paths from the req_rec into the connection for
	 * future use.  Note that if the primary path is invalid,
	 * the REP will fail.
	 */
	__format_req_av( p_cep, p_req, 0 );

	if( p_req->alternate_path.local_lid )
		__format_req_av( p_cep, p_req, 1 );
	else
		cl_memclr( &p_cep->av[1], sizeof(kcep_av_t) );

	p_cep->idx_primary = 0;

	/* Store the maximum packet lifetime, used to calculate timewait. */
	p_cep->max_2pkt_life = conn_req_path_get_lcl_ack_timeout( &p_req->primary_path );
	p_cep->max_2pkt_life = max( p_cep->max_2pkt_life,
		conn_req_path_get_lcl_ack_timeout( &p_req->alternate_path ) );

	/*
	 * Make sure the target ack delay is cleared - the above
	 * "packet life" includes it.
	 */
	p_cep->target_ack_delay = 0;

	/* Store the requested initiator depth. */
	p_cep->resp_res = conn_req_get_init_depth( p_req );

	/*
	 * Store the provided responder resources.  These turn into the local
	 * QP's initiator depth.
	 */
	p_cep->init_depth = conn_req_get_resp_res( p_req );

	p_cep->sq_psn = conn_req_get_starting_psn( p_req );

	p_cep->tid = p_req->hdr.trans_id;
	/* copy mad info for cm handoff */
	/* TODO: Do need to support CM handoff? */
	//p_cep->mads.req = *p_req;

    /* Cache the private data. */
	p_cep->psize = IB_REQ_PDATA_SIZE;
	memcpy( p_cep->pdata, p_req->pdata, IB_REQ_PDATA_SIZE );

	AL_EXIT( AL_DBG_CM );
}


/* Must be called with the CEP lock held. */
static void
__repeat_mad(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_port_cep );
	CL_ASSERT( p_cep );
	CL_ASSERT( p_mad );

	/* Repeat the last mad sent for the connection. */
	switch( p_cep->state )
	{
	case CEP_STATE_REQ_MRA_SENT:	/* resend MRA(REQ) */
	case CEP_STATE_REP_MRA_SENT:	/* resend MRA(REP) */
	case CEP_STATE_LAP_MRA_SENT:	/* resend MRA(LAP) */
	case CEP_STATE_ESTABLISHED:		/* resend RTU */
	case CEP_STATE_TIMEWAIT:		/* resend the DREP */
		cl_memcpy( p_mad->p_mad_buf, &p_cep->mads, MAD_BLOCK_SIZE );
		p_mad->send_context1 = NULL;
		p_mad->send_context2 = NULL;
		__cep_send_mad( p_port_cep, p_mad );
		break;

	default:
		/* Return the MAD to the mad pool */
		ib_put_mad( p_mad );
		break;
	}

	AL_EXIT( AL_DBG_CM );
}


static ib_api_status_t
__process_rej(
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_rej_t		*p_rej;

	AL_ENTER( AL_DBG_CM );

	ASSERT( p_cep );
	ASSERT( p_mad );
	ASSERT( p_mad->p_mad_buf );

	p_rej = (mad_cm_rej_t*)p_mad->p_mad_buf;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
		("Request rejected p_rej %p, reason - %d.\n", 
		p_rej, cl_ntoh16(p_rej->reason) ) );

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_SENT:
		/*
		 * Ignore rejects with the status set to IB_REJ_INVALID_SID.  We will
		 * continue to retry (up to max_cm_retries) to connect to the remote
		 * side.  This is required to support peer-to-peer connections.
		 */
		if( p_cep->p2p && p_rej->reason == IB_REJ_INVALID_SID )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
				("Request rejected (invalid SID) - retrying.\n") );
			goto err1;
		}

		/* Fall through */
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REP_MRA_RCVD:
		/* Cancel any outstanding MAD. */
		if( p_cep->p_send_mad )
		{
			ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
			p_cep->p_send_mad = NULL;
		}

		/* Fall through */
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_REP_MRA_SENT:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		if( p_cep->state & CEP_STATE_PREP )
		{
			CL_ASSERT( p_cep->p_mad );
			ib_put_mad( p_cep->p_mad );
			p_cep->p_mad = NULL;
		}
		/* Abort connection establishment. No transition to timewait. */
		__remove_cep( p_cep );
		p_cep->state = CEP_STATE_IDLE;
		break;

	case CEP_STATE_REQ_RCVD:
		__remove_cep( p_cep );
		p_cep->state = CEP_STATE_IDLE;
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return IB_NO_MATCH;

	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
	case CEP_STATE_PRE_APR:
	case CEP_STATE_PRE_APR_MRA_SENT:
		if( p_cep->state & CEP_STATE_PREP )
		{
			CL_ASSERT( p_cep->p_mad );
			ib_put_mad( p_cep->p_mad );
			p_cep->p_mad = NULL;
		}
		p_cep->state = CEP_STATE_TIMEWAIT;
		__insert_timewait( p_cep );
		break;

	default:
		/* Ignore the REJ. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("REJ received in invalid state.\n") );
err1:
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return IB_NO_MATCH;
	}

    /* Cache the private data. */
	p_cep->psize = IB_REJ_PDATA_SIZE;
	memcpy( p_cep->pdata, p_rej->pdata, IB_REJ_PDATA_SIZE );

	status = __cep_queue_mad( p_cep, p_mad );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__process_stale(
	IN				kcep_t* const				p_cep )
{
	ib_api_status_t		status;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;
	mad_cm_rej_t		*p_rej;

	status = __cep_get_mad( p_cep, CM_REJ_ATTR_ID, &p_port_cep, &p_mad );
	if( status != IB_SUCCESS )
		return status;

	p_rej = ib_get_mad_buf( p_mad );

	conn_rej_set_ari( NULL, 0, p_rej );
	conn_rej_set_pdata( NULL, 0, p_rej );

	p_rej->local_comm_id = p_cep->remote_comm_id;
	p_rej->remote_comm_id = p_cep->local_comm_id;
	p_rej->reason = IB_REJ_STALE_CONN;

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		conn_rej_set_msg_rejected( 0, p_rej );
		break;

	case CEP_STATE_REQ_SENT:
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
		conn_rej_set_msg_rejected( 1, p_rej );
		break;

	default:
		conn_rej_set_msg_rejected( 2, p_rej );
		break;
	}
	conn_rej_clr_rsvd_fields( p_rej );

	return __process_rej( p_cep, p_mad );
}


static void
__req_handler(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status = IB_SUCCESS;
	mad_cm_req_t		*p_req;
	kcep_t				*p_cep, *p_new_cep, *p_stale_cep = NULL;
	KLOCK_QUEUE_HANDLE	hdl;
	ib_rej_status_t		reason;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_req = (mad_cm_req_t*)p_mad->p_mad_buf;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
		("REQ: comm_id (x%x) qpn (x%x) received\n",
		p_req->local_comm_id, conn_req_get_lcl_qpn( p_req )) );

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );

	if( conn_req_get_qp_type( p_req ) > IB_QPT_UNRELIABLE_CONN ||
		conn_req_get_lcl_qpn( p_req ) == 0 )
	{
		/* Reserved value.  Reject. */
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid transport type received.\n") );
		reason = IB_REJ_INVALID_XPORT;
		goto reject;
	}

	/* Match against pending connections using remote comm ID and CA GUID. */
	p_cep = __lookup_by_id( p_req->local_comm_id, p_req->local_ca_guid );
	if( p_cep )
	{
		/* Already received the REQ. */
		switch( p_cep->state )
		{
		case CEP_STATE_REQ_MRA_SENT:
			__repeat_mad( p_port_cep, p_cep, p_mad );
			break;

		case CEP_STATE_TIMEWAIT:
		case CEP_STATE_DESTROY:
			/* Send a reject. */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
				("REQ received for connection in TIME_WAIT state.\n") );
			__reject_req( p_port_cep, p_mad, IB_REJ_STALE_CONN );
			break;

		default:
			/*
			 * Let regular retries repeat the MAD.  If our last message was
			 * dropped, resending only adds to the congestion.  If it wasn't
			 * dropped, then the remote CM will eventually process it, and
			 * we'd just be adding traffic.
			 */
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("Duplicate REQ received.\n") );
			ib_put_mad( p_mad );
		}
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	/*
	 * Match against listens using SID and compare data, also provide the receiving
	 * MAD service's port GUID so we can properly filter.
	 */
	p_cep = __lookup_listen( p_req->sid, p_port_cep->port_guid, p_req->pdata );
	if( p_cep )
	{
		/*
		 * Allocate a new CEP for the new request.  This will
		 * prevent multiple identical REQs from queueing up for processing.
		 */
		p_new_cep = __create_cep();
		if( !p_new_cep )
		{
			/* Reject the request for insufficient resources. */
			reason = IB_REJ_INSUF_RESOURCES;
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("__create_cep failed\nREJ sent for insufficient resources.\n") );
			goto reject;
		}

		__save_wire_req( p_new_cep, p_req );

		__bind_cep( p_new_cep, p_cep->p_cid->h_al, p_cep->pfn_cb, NULL );
		AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM,
			("Created CEP with CID = %d, h_al %p, remote = %d\n",
			p_new_cep->cid, p_cep->p_cid->h_al, p_new_cep->remote_comm_id) );

		/* Add the new CEP to the map so that repeated REQs match up. */
		p_stale_cep = __insert_cep( p_new_cep );
		if( p_stale_cep != p_new_cep )
		{
			/* Duplicate - must be a stale connection. */
			reason = IB_REJ_STALE_CONN;
			/* Fail the local stale CEP. */
			status = __process_stale( p_stale_cep );
			goto unbind;
		}

		/* __cep_queue_mad may complete a pending IRP */
		p_mad->send_context1 = p_new_cep;	 

		/*
		 * Queue the mad - the return value indicates whether we should
		 * invoke the callback.
		 */
		status = __cep_queue_mad( p_cep, p_mad );
		switch( status )
		{
		case IB_SUCCESS:
		case IB_PENDING:
			break;

		case IB_UNSUPPORTED:
			p_mad->send_context1 = NULL;
			reason = IB_REJ_USER_DEFINED;
			goto unbind;
		
		default:
			p_mad->send_context1 = NULL;
			reason = IB_REJ_INSUF_RESOURCES;
			goto unbind;
		}
	}
	else
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("No listens active!\n") );

		/* Match against peer-to-peer requests using SID and compare data. */
		//p_cep = __lookup_peer();
		//if( p_cep )
		//{
		//	p_mad->send_context2 = NULL;
		//	p_list_item = cl_qlist_find_from_head( &gp_cep_mgr->pending_list,
		//		__match_peer, p_req );
		//	if( p_list_item != cl_qlist_end( &gp_cep_mgr->pending_list ) )
		//	{
		//		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		//		p_conn = PARENT_STRUCT( p_list_item, kcep_t, map_item );
		//		__peer_req( p_port_cep, p_conn, p_async_mad->p_mad );
		//		cl_free( p_async_mad );
		//		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
		//			("REQ matched a peer-to-peer request.\n") );
		//		return;
		//	}
		//	reason = IB_REJ_INVALID_SID;
		//	goto free;
		//}
		//else
		{
			/* No match found.  Reject. */
			reason = IB_REJ_INVALID_SID;
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("REQ received but no match found.\n") );
			goto reject;
		}
	}

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	/* Process any queued MADs for the CEP. */
	if( status == IB_SUCCESS )
		__process_cep( p_cep );

	AL_EXIT( AL_DBG_CM );
	return;

unbind:
	__unbind_cep( p_new_cep );

	/*
	 * Move the CEP in the idle state so that we don't send a reject
	 * for it when cleaning up.  Also clear the RQPN and RCID so that
	 * we don't try to remove it from our maps (since it isn't inserted).
	 */
	p_new_cep->state = CEP_STATE_IDLE;
	p_new_cep->remote_comm_id = 0;
	p_new_cep->remote_qpn = 0;
	__cleanup_cep( p_new_cep );

reject:
	__reject_req( p_port_cep, p_mad, reason );

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	if( reason == IB_REJ_STALE_CONN && status == IB_SUCCESS )
		__process_cep( p_stale_cep );

	AL_EXIT( AL_DBG_CM );
}


static void
__save_wire_rep(
	IN	OUT			kcep_t*				const	p_cep,
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	AL_ENTER( AL_DBG_CM );

	/* The send should have been cancelled during MRA processing. */
	p_cep->state = CEP_STATE_REP_RCVD;

	/* Store pertinent information in the connection. */
	p_cep->remote_comm_id = p_rep->local_comm_id;
	p_cep->remote_ca_guid = p_rep->local_ca_guid;

	p_cep->remote_qpn = conn_rep_get_lcl_qpn( p_rep );

	/* Store the remote endpoint's target ACK delay. */
	p_cep->target_ack_delay = conn_rep_get_target_ack_delay( p_rep );

	/* Update the local ACK delay stored in the AV's. */
	p_cep->av[0].attr.conn.local_ack_timeout = calc_lcl_ack_timeout(
		p_cep->av[0].attr.conn.local_ack_timeout, p_cep->target_ack_delay );
	p_cep->av[0].attr.conn.rnr_retry_cnt = conn_rep_get_rnr_retry_cnt( p_rep );

	if( p_cep->av[1].port_guid )
	{
		p_cep->av[1].attr.conn.local_ack_timeout = calc_lcl_ack_timeout(
			p_cep->av[1].attr.conn.local_ack_timeout,
			p_cep->target_ack_delay );
		p_cep->av[1].attr.conn.rnr_retry_cnt =
			p_cep->av[0].attr.conn.rnr_retry_cnt;
	}

	p_cep->init_depth = p_rep->resp_resources;
	p_cep->resp_res = p_rep->initiator_depth;

	p_cep->sq_psn = conn_rep_get_starting_psn( p_rep );

    /* Cache the private data. */
	p_cep->psize = IB_REP_PDATA_SIZE;
	memcpy( p_cep->pdata, p_rep->pdata, IB_REP_PDATA_SIZE );

	AL_EXIT( AL_DBG_CM );
}


static void
__mra_handler(
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_mra_t		*p_mra;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_mra = (mad_cm_mra_t*)p_mad->p_mad_buf;

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_mra->remote_comm_id );
	if( !p_cep )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
			("MRA received that could not be matched.\n") );
		goto err;
	}

	if( p_cep->remote_comm_id )
	{
		if( p_cep->remote_comm_id != p_mra->local_comm_id )
		{
			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
				("MRA received that could not be matched.\n") );
			goto err;
		}
	}

	/*
	 * Note that we don't update the CEP's remote comm ID - it messes up REP
	 * processing since a non-zero RCID implies the connection is in the RCID
	 * map.  Adding it here requires checking there and conditionally adding
	 * it.  Ignoring it is a valid thing to do.
	 */
	if( !(p_cep->state & CEP_STATE_SENT) ||
		(1 << conn_mra_get_msg_mraed( p_mra ) !=
		(p_cep->state & CEP_MSG_MASK)) )
	{
		/* Invalid state. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("MRA received in invalid state.\n") );
		goto err;
	}

	/* Delay the current send. */
	CL_ASSERT( p_cep->p_send_mad );
	ib_delay_mad( p_cep->h_mad_svc, p_cep->p_send_mad,
		__calc_mad_timeout( conn_mra_get_svc_timeout( p_mra ) ) +
		__calc_mad_timeout( p_cep->max_2pkt_life - 1 ) );

	/* We only invoke a single callback for MRA. */
	if( p_cep->state & CEP_STATE_MRA )
	{
		/* Invalid state. */
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("Already received MRA.\n") );
		goto err;
	}

	p_cep->state |= CEP_STATE_MRA;

	status = __cep_queue_mad( p_cep, p_mad );

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	if( status == IB_SUCCESS )
		__process_cep( p_cep );

	AL_EXIT( AL_DBG_CM );
	return;

err:
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	ib_put_mad( p_mad );
	AL_EXIT( AL_DBG_CM );
}


static void
__rej_handler(
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_rej_t		*p_rej;
	kcep_t				*p_cep = NULL;
	KLOCK_QUEUE_HANDLE	hdl;
	net64_t				ca_guid;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_rej = (mad_cm_rej_t*)p_mad->p_mad_buf;

	/* Either one of the communication IDs must be set. */
	if( !p_rej->remote_comm_id && !p_rej->local_comm_id )
		goto err1;

	/* Check the pending list by the remote CA GUID and connection ID. */
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	if( p_rej->remote_comm_id )
	{
		p_cep = __lookup_cep( NULL, p_rej->remote_comm_id );
	}
	else if( p_rej->reason == IB_REJ_TIMEOUT &&
		conn_rej_get_ari_len( p_rej ) == sizeof(net64_t) )
	{
		cl_memcpy( &ca_guid, p_rej->ari, sizeof(net64_t) );
		p_cep = __lookup_by_id( p_rej->local_comm_id, ca_guid );
	}

	if( !p_cep )
	{
		goto err2;
	}

	if( p_cep->remote_comm_id &&
		p_cep->remote_comm_id != p_rej->local_comm_id )
	{
	err2:
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	err1:
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	status = __process_rej( p_cep, p_mad );

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	if( status == IB_SUCCESS )
		__process_cep( p_cep );

	AL_EXIT( AL_DBG_CM );
}


static void
__rep_handler(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_rep_t		*p_rep;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_state_t			old_state;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_rep = (mad_cm_rep_t*)p_mad->p_mad_buf;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
		("REP: comm_id (x%x) received\n", p_rep->local_comm_id ) );

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_rep->remote_comm_id );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
			("REP received that could not be matched.\n") );
		return;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REQ_SENT:
		old_state = p_cep->state;
		/* Save pertinent information and change state. */
		__save_wire_rep( p_cep, p_rep );

		if( __insert_cep( p_cep ) != p_cep )
		{
			/* Roll back the state change. */
			__reject_mad( p_port_cep, p_cep, p_mad, IB_REJ_STALE_CONN );
			p_cep->state = old_state;
			status = __process_stale( p_cep );
		}
		else
		{
			/*
			 * Cancel any outstanding send.  Note that we do this only after
			 * inserting the CEP - if we failed, then the send will timeout
			 * and we'll finish our way through the state machine.
			 */
			if( p_cep->p_send_mad )
			{
				ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
				p_cep->p_send_mad = NULL;
			}

			status = __cep_queue_mad( p_cep, p_mad );
		}

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

		if( status == IB_SUCCESS )
			__process_cep( p_cep );

		AL_EXIT( AL_DBG_CM );
		return;

	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
	case CEP_STATE_REP_MRA_SENT:
		/* Repeate the MRA or RTU. */
		__repeat_mad( p_port_cep, p_cep, p_mad );
		break;

	default:
		ib_put_mad( p_mad );
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("REP received in invalid state.\n") );
		break;
	}

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	AL_EXIT( AL_DBG_CM );
}


static void
__rtu_handler(
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_rtu_t		*p_rtu;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_rtu = (mad_cm_rtu_t*)p_mad->p_mad_buf;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
		("RTU: comm_id (x%x) received\n", p_rtu->local_comm_id) );

	/* Find the connection by local connection ID. */
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_rtu->remote_comm_id );
	if( !p_cep || p_cep->remote_comm_id != p_rtu->local_comm_id )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("RTU received that could not be matched.\n") );
		goto done;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
		/* Cancel any outstanding send. */
		if( p_cep->p_send_mad )
		{
			ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
			p_cep->p_send_mad = NULL;
		}

		p_cep->state = CEP_STATE_ESTABLISHED;

		status = __cep_queue_mad( p_cep, p_mad );

		/* Update timewait time. */
		__calc_timewait( p_cep );

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

		if( status == IB_SUCCESS )
			__process_cep( p_cep );

		AL_EXIT( AL_DBG_CM );
		return;

	default:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("RTU received in invalid state.\n") );
		break;
	}

done:
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	ib_put_mad( p_mad );
	AL_EXIT( AL_DBG_CM );
}


static ib_api_status_t
__format_drep_mad(
	IN				uint32_t					local_comm_id,
	IN				uint32_t					remote_comm_id,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len,
	IN	OUT			mad_cm_drep_t* const		p_drep )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	p_drep->local_comm_id = local_comm_id;
	p_drep->remote_comm_id = remote_comm_id;

	/* copy optional data */
	status = conn_drep_set_pdata( p_pdata, pdata_len, p_drep );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__send_unaffiliated_drep(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	mad_cm_dreq_t		*p_dreq;

	AL_ENTER( AL_DBG_CM );

	p_dreq = (mad_cm_dreq_t*)p_mad->p_mad_buf;

	p_mad->p_mad_buf->attr_id = CM_DREP_ATTR_ID;
	/* __format_drep returns always SUCCESS while no private data */
	__format_drep_mad( p_dreq->remote_comm_id, p_dreq->local_comm_id, NULL, 0, (mad_cm_drep_t*)p_mad->p_mad_buf );
	__cep_send_mad( p_port_cep, p_mad );

	AL_EXIT( AL_DBG_CM );
}


static void
__dreq_handler(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_dreq_t		*p_dreq;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_dreq = (mad_cm_dreq_t*)p_mad->p_mad_buf;

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
		("DREQ: comm_id (x%x) qpn (x%x) received\n",
		p_dreq->local_comm_id, conn_dreq_get_remote_qpn( p_dreq )) );

	/* Find the connection by connection IDs. */
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_dreq->remote_comm_id );
	if( !p_cep ||
		p_cep->remote_comm_id != p_dreq->local_comm_id ||
		p_cep->local_qpn != conn_dreq_get_remote_qpn( p_dreq ) )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("DREQ received that could not be matched.\n") );
		__send_unaffiliated_drep( p_port_cep, p_mad );
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
	case CEP_STATE_DREQ_SENT:
		/* Cancel the outstanding MAD. */
		if( p_cep->p_send_mad )
		{
			ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
			p_cep->p_send_mad = NULL;
		}

		/* Fall through and process as DREQ received case. */
	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
		p_cep->state = CEP_STATE_DREQ_RCVD;

		status = __cep_queue_mad( p_cep, p_mad );

		/* Store the TID for use in the reply DREP. */
		p_cep->tid = p_dreq->hdr.trans_id;

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

		if( status == IB_SUCCESS )
			__process_cep( p_cep );
		AL_EXIT( AL_DBG_CM );
		return;

	case CEP_STATE_TIMEWAIT:
	case CEP_STATE_DESTROY:
		/* Repeat the DREP. */
		__repeat_mad( p_port_cep, p_cep, p_mad );
		break;

	case CEP_STATE_DREQ_DESTROY:
		/* Send the DREP with no private data. */

		ib_put_mad( p_mad );	/* release DREQ MAD */

		status = __cep_get_mad( p_cep, CM_DREP_ATTR_ID, &(cep_agent_t*)p_port_cep, 
			&(ib_mad_element_t*)p_mad );
		if( status != IB_SUCCESS )
			break;

		p_mad->p_mad_buf->attr_id = CM_DREP_ATTR_ID;
		/* __format_drep returns always SUCCESS while no private data */
		__format_drep( p_cep, NULL, 0, (mad_cm_drep_t*)p_mad->p_mad_buf );
		__cep_send_mad( p_port_cep, p_mad );
		break;

	default:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("DREQ received in invalid state.\n") );
	case CEP_STATE_DREQ_RCVD:
		ib_put_mad( p_mad );
		break;
	}

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	AL_EXIT( AL_DBG_CM );
}


static void
__drep_handler(
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_drep_t		*p_drep;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_drep = (mad_cm_drep_t*)p_mad->p_mad_buf;

	/* Find the connection by local connection ID. */
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_drep->remote_comm_id );
	if( !p_cep || p_cep->remote_comm_id != p_drep->local_comm_id )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("DREP received that could not be matched.\n") );
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	if( p_cep->state != CEP_STATE_DREQ_SENT &&
		p_cep->state != CEP_STATE_DREQ_DESTROY )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("DREP received in invalid state.\n") );

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	/* Cancel the DREQ. */
	if( p_cep->p_send_mad )
	{
		ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
		p_cep->p_send_mad = NULL;
	}

	if( p_cep->state == CEP_STATE_DREQ_SENT )
	{
		p_cep->state = CEP_STATE_TIMEWAIT;

		status = __cep_queue_mad( p_cep, p_mad );
	}
	else
	{
		/* State is DREQ_DESTROY - move to DESTROY to allow cleanup. */
		CL_ASSERT( p_cep->state == CEP_STATE_DREQ_DESTROY );
		p_cep->state = CEP_STATE_DESTROY;

		ib_put_mad( p_mad );
		status = IB_INVALID_STATE;
	}

	__insert_timewait( p_cep );

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	if( status == IB_SUCCESS )
		__process_cep( p_cep );

	AL_EXIT( AL_DBG_CM );
}


static boolean_t
__format_lap_av(
	IN				kcep_t* const				p_cep,
	IN		const	lap_path_info_t* const		p_path )
{
	cep_agent_t		*p_port_cep;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cep );
	CL_ASSERT( p_path );

	cl_memclr( &p_cep->alt_av, sizeof(kcep_av_t) );

	p_port_cep = __find_port_cep( &p_path->remote_gid, p_path->remote_lid,
		p_cep->pkey, &p_cep->alt_av.pkey_index );
	if( !p_port_cep )
	{
		AL_EXIT( AL_DBG_CM );
		return FALSE;
	}

	if( p_port_cep->h_ca->obj.p_ci_ca->verbs.guid != p_cep->local_ca_guid )
	{
		AL_EXIT( AL_DBG_CM );
		return FALSE;
	}

	p_cep->alt_av.port_guid = p_port_cep->port_guid;
	p_cep->alt_av.attr.port_num = p_port_cep->port_num;

	p_cep->alt_av.attr.sl = conn_lap_path_get_svc_lvl( p_path );
	p_cep->alt_av.attr.dlid = p_path->local_lid;

	if( !conn_lap_path_get_subn_lcl( p_path ) )
	{
		p_cep->alt_av.attr.grh_valid = TRUE;
		p_cep->alt_av.attr.grh.ver_class_flow = ib_grh_set_ver_class_flow(
			1, conn_lap_path_get_tclass( p_path ),
			conn_lap_path_get_flow_lbl( p_path ) );
		p_cep->alt_av.attr.grh.hop_limit = p_path->hop_limit;
		p_cep->alt_av.attr.grh.dest_gid = p_path->local_gid;
		p_cep->alt_av.attr.grh.src_gid = p_path->remote_gid;
	}
	else
	{
		p_cep->alt_av.attr.grh_valid = FALSE;
	}
	p_cep->alt_av.attr.static_rate = conn_lap_path_get_pkt_rate( p_path );
	p_cep->alt_av.attr.path_bits =
		(uint8_t)(p_path->remote_lid - p_port_cep->base_lid);

	/*
	 * Note that while we never use the connected AV attributes internally,
	 * we store them so we can pass them back to users.  For the LAP, we
	 * first copy the settings from the current primary - MTU and retry
	 * counts are only specified in the REQ.
	 */
	p_cep->alt_av.attr.conn = p_cep->av[p_cep->idx_primary].attr.conn;
	p_cep->alt_av.attr.conn.local_ack_timeout =
		conn_lap_path_get_lcl_ack_timeout( p_path );

	AL_EXIT( AL_DBG_CM );
	return TRUE;
}


static void
__lap_handler(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_lap_t		*p_lap;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_lap = (mad_cm_lap_t*)p_mad->p_mad_buf;

	/* Find the connection by local connection ID. */
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_lap->remote_comm_id );
	if( !p_cep || p_cep->remote_comm_id != p_lap->local_comm_id )
	{
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		AL_PRINT_EXIT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("LAP received that could not be matched.\n") );
		return;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
		/*
		 * These two cases handle the RTU being dropped.  Receipt of
		 * a LAP indicates that the connection is established.
		 */
	case CEP_STATE_ESTABLISHED:
		/*
		 * We don't check for other "established" states related to
		 * alternate path management (CEP_STATE_LAP_RCVD, etc)
		 */

		/* We only support receiving LAP if we took the passive role. */
		if( p_cep->was_active )
		{
			ib_put_mad( p_mad );
			break;
		}

		/* Store the transaction ID for use during the LAP exchange. */
		p_cep->tid = p_lap->hdr.trans_id;

		/*
		 * Copy the path record into the connection for use when
		 * sending the APR and loading the path.
		 */
		if( !__format_lap_av( p_cep, &p_lap->alternate_path ) )
		{
			/* Trap an invalid path. */
			ib_put_mad( p_mad );
			break;
		}

		p_cep->state = CEP_STATE_LAP_RCVD;

		status = __cep_queue_mad( p_cep, p_mad );

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

		if( status == IB_SUCCESS )
			__process_cep( p_cep );

		AL_EXIT( AL_DBG_CM );
		return;

	case CEP_STATE_LAP_MRA_SENT:
		__repeat_mad( p_port_cep, p_cep, p_mad );
		break;

	default:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("LAP received in invalid state.\n") );
		ib_put_mad( p_mad );
		break;
	}

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	AL_EXIT( AL_DBG_CM );
}


static void
__apr_handler(
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_apr_t		*p_apr;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_apr = (mad_cm_apr_t*)p_mad->p_mad_buf;

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( NULL, p_apr->remote_comm_id );
	if( !p_cep || p_cep->remote_comm_id != p_apr->local_comm_id )
	{
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("APR received that could not be matched.\n") );
		goto done;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
		/* Cancel sending the LAP. */
		if( p_cep->p_send_mad )
		{
			ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
			p_cep->p_send_mad = NULL;
		}

		/* Copy the temporary alternate AV. */
		p_cep->av[(p_cep->idx_primary + 1) & 0x1] = p_cep->alt_av;

		/* Update the maximum packet lifetime. */
		p_cep->max_2pkt_life = max( p_cep->max_2pkt_life, p_cep->alt_2pkt_life );

		/* Update the timewait time. */
		__calc_timewait( p_cep );

		p_cep->state = CEP_STATE_ESTABLISHED;

		status = __cep_queue_mad( p_cep, p_mad );

		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

		if( status == IB_SUCCESS )
			__process_cep( p_cep );

		AL_EXIT( AL_DBG_CM );
		return;

	default:
		AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM, ("APR received in invalid state.\n") );
		break;
	}

done:
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
	ib_put_mad( p_mad );
	AL_EXIT( AL_DBG_CM );
}


static void
__cep_mad_recv_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*context,
	IN				ib_mad_element_t			*p_mad )
{
	cep_agent_t		*p_port_cep;
	ib_mad_t		*p_hdr;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	UNUSED_PARAM( h_mad_svc );
	p_port_cep = (cep_agent_t*)context;

	CL_ASSERT( p_mad->p_next == NULL );

	p_hdr = (ib_mad_t*)p_mad->p_mad_buf;

	/*
	 * TODO: Add filtering in all the handlers for unsupported class version.
	 * See 12.6.7.2 Rejection Reason, code 31.
	 */

	switch( p_hdr->attr_id )
	{
	case CM_REQ_ATTR_ID:
		__req_handler( p_port_cep, p_mad );
		break;

	case CM_MRA_ATTR_ID:
		__mra_handler( p_mad );
		break;

	case CM_REJ_ATTR_ID:
		__rej_handler( p_mad );
		break;

	case CM_REP_ATTR_ID:
		__rep_handler( p_port_cep, p_mad );
		break;

	case CM_RTU_ATTR_ID:
		__rtu_handler( p_mad );
		break;

	case CM_DREQ_ATTR_ID:
		__dreq_handler( p_port_cep, p_mad );
		break;

	case CM_DREP_ATTR_ID:
		__drep_handler( p_mad );
		break;

	case CM_LAP_ATTR_ID:
		__lap_handler( p_port_cep, p_mad );
		break;

	case CM_APR_ATTR_ID:
		__apr_handler( p_mad );
		break;

	case CM_SIDR_REQ_ATTR_ID:
//		p_async_mad->item.pfn_callback = __process_cm_sidr_req;
//		break;
//
	case CM_SIDR_REP_ATTR_ID:
//		p_async_mad->item.pfn_callback = __process_cm_sidr_rep;
//		break;
//
	default:
		ib_put_mad( p_mad );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Invalid CM MAD attribute ID.\n") );
		return;
	}

	AL_EXIT( AL_DBG_CM );
}


static inline cep_agent_t*
__get_cep_agent(
	IN				kcep_t* const				p_cep )
{
	cl_map_item_t		*p_item;

	CL_ASSERT( p_cep );

	/* Look up the primary CEP port agent */
	p_item = cl_qmap_get( &gp_cep_mgr->port_map,
		p_cep->av[p_cep->idx_primary].port_guid );
	if( p_item == cl_qmap_end( &gp_cep_mgr->port_map ) )
		return NULL;

	return PARENT_STRUCT( p_item, cep_agent_t, item );
}


static inline void
__format_mad_av(
		OUT			ib_mad_element_t* const		p_mad,
	IN				kcep_av_t* const			p_av )
{
	/* Set the addressing information in the MAD. */
	p_mad->grh_valid = p_av->attr.grh_valid;
	if( p_av->attr.grh_valid )
		cl_memcpy( p_mad->p_grh, &p_av->attr.grh, sizeof(ib_grh_t) );

	p_mad->remote_sl = p_av->attr.sl;
	p_mad->remote_lid = p_av->attr.dlid;
	p_mad->path_bits = p_av->attr.path_bits;
	p_mad->pkey_index = p_av->pkey_index;
	p_mad->remote_qp = IB_QP1;
	p_mad->send_opt = IB_SEND_OPT_SIGNALED;
	p_mad->remote_qkey = IB_QP1_WELL_KNOWN_Q_KEY;
	/* Let the MAD service manage the AV for us. */
	p_mad->h_av = NULL;
}


static ib_api_status_t
__cep_send_mad(
	IN				cep_agent_t* const			p_port_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_port_cep );
	CL_ASSERT( p_mad );

	/* Use the mad's attributes already present */
	p_mad->resp_expected = FALSE;
	p_mad->retry_cnt = 0;
	p_mad->timeout_ms = 0;

	/* Clear the contexts since the send isn't associated with a CEP. */
	p_mad->context1 = NULL;
	p_mad->context2 = NULL;

	status = ib_send_mad( p_port_cep->h_mad_svc, p_mad, NULL );
	if( status != IB_SUCCESS )
	{
		ib_put_mad( p_mad );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_send_mad failed with status %s.\n", ib_get_err_str(status)) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__cep_send_retry(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cep );
	CL_ASSERT( p_mad );
	CL_ASSERT( p_mad->p_mad_buf->attr_id == CM_REQ_ATTR_ID ||
		p_mad->p_mad_buf->attr_id == CM_REP_ATTR_ID ||
		p_mad->p_mad_buf->attr_id == CM_LAP_ATTR_ID ||
		p_mad->p_mad_buf->attr_id == CM_DREQ_ATTR_ID );

	/*
		* REQ, REP, and DREQ are retried until either a response is
		* received or the operation times out.
		*/
	p_mad->resp_expected = TRUE;
	p_mad->retry_cnt = p_cep->max_cm_retries;
	p_mad->timeout_ms = p_cep->retry_timeout;

	CL_ASSERT( !p_cep->p_send_mad );

	/* Store the mad & mad service handle in the CEP for cancelling. */
	p_cep->h_mad_svc = p_port_cep->h_mad_svc;
	p_cep->p_send_mad = p_mad;

	/* reference the connection for which we are sending the MAD. */
	cl_atomic_inc( &p_cep->ref_cnt );

	/* Set the context. */
	p_mad->context1 = p_cep;
	p_mad->context2 = NULL;

	/* Fire in the hole! */
	status = ib_send_mad( p_cep->h_mad_svc, p_mad, NULL );
	if( status != IB_SUCCESS )
	{
		/*
		 * Note that we don't need to check for destruction here since
		 * we're holding the global lock.
		 */
		cl_atomic_dec( &p_cep->ref_cnt );
		p_cep->p_send_mad = NULL;
		ib_put_mad( p_mad );
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_send_mad failed with status %s.\n", ib_get_err_str(status)) );
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__cep_mad_send_cb(
	IN				ib_mad_svc_handle_t			h_mad_svc,
	IN				void						*context,
	IN				ib_mad_element_t			*p_mad )
{
	ib_api_status_t		status;
	cep_agent_t			*p_port_cep;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	ib_pfn_destroy_cb_t	pfn_destroy_cb;
	void				*cep_context;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( h_mad_svc );
	CL_ASSERT( p_mad->p_next == NULL );
	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	p_port_cep = (cep_agent_t*)context;

	p_cep = (kcep_t*)p_mad->context1;

	/* The cep context is only set for MADs that are retried. */
	if( !p_cep )
	{
		ib_put_mad( p_mad );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	CL_ASSERT( p_mad->status != IB_WCS_SUCCESS );
	p_mad->context1 = NULL;

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );
	if( p_cep->p_send_mad != p_mad )
	{
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		goto done;
	}

	/* Clear the sent MAD pointer so that we don't try cancelling again. */
	p_cep->p_send_mad = NULL;

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_SENT:
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
		/* Send the REJ. */
		__reject_timeout( p_port_cep, p_cep, p_mad );
		__remove_cep( p_cep );
		p_cep->state = CEP_STATE_IDLE;
		break;

	case CEP_STATE_DREQ_DESTROY:
		p_cep->state = CEP_STATE_DESTROY;
		__insert_timewait( p_cep );
		/* Fall through. */

	case CEP_STATE_DESTROY:
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );
		ib_put_mad( p_mad );
		goto done;

	case CEP_STATE_DREQ_SENT:
		/*
		 * Make up a DREP mad so we can respond if we receive
		 * a DREQ while in timewait.
		 */
		__format_mad_hdr( &p_cep->mads.drep.hdr, p_cep, CM_DREP_ATTR_ID );
		__format_drep( p_cep, NULL, 0, &p_cep->mads.drep );
		p_cep->state = CEP_STATE_TIMEWAIT;
		__insert_timewait( p_cep );
		break;

	case CEP_STATE_LAP_SENT:
		/*
		 * Before CEP was sent, we have been in CEP_STATE_ESTABLISHED as we
		 * failed to send, we return to that state.
		 */
		p_cep->state = CEP_STATE_ESTABLISHED;
		break;
	default:
		break;
	}

	status = __cep_queue_mad( p_cep, p_mad );
	CL_ASSERT( status != IB_INVALID_STATE );
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	if( status == IB_SUCCESS )
		__process_cep( p_cep );

done:
	pfn_destroy_cb = p_cep->pfn_destroy_cb;
	cep_context = p_cep->context;

	if( !cl_atomic_dec( &p_cep->ref_cnt ) && pfn_destroy_cb )
		pfn_destroy_cb( cep_context );
	AL_EXIT( AL_DBG_CM );
}


static void
__cep_qp_event_cb(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );

	/*
	 * Most of the QP events are trapped by the real owner of the QP.
	 * For real events, the CM may not be able to do much anyways!
	 */
}


static ib_api_status_t
__init_data_svc(
	IN				cep_agent_t* const			p_port_cep,
	IN		const	ib_port_attr_t* const		p_port_attr )
{
	ib_api_status_t		status;
	ib_qp_create_t		qp_create;
	ib_mad_svc_t		mad_svc;

	AL_ENTER( AL_DBG_CM );

	/*
	 * Create the PD alias.  We use the port CM's al_obj_t as the context
	 * to allow using deref_al_obj as the destroy callback.
	 */
	status = ib_alloc_pd( p_port_cep->h_ca, IB_PDT_ALIAS, &p_port_cep->obj,
		&p_port_cep->h_pd );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_alloc_pd failed with status %s\n", ib_get_err_str(status)) );
		return status;
	}
	/* Reference the port object on behalf of the PD. */
	ref_al_obj( &p_port_cep->obj );

	/* Create the MAD QP. */
	cl_memclr( &qp_create, sizeof( ib_qp_create_t ) );
	qp_create.qp_type = IB_QPT_QP1_ALIAS;
	qp_create.rq_depth = CEP_MAD_RQ_DEPTH;
	qp_create.sq_depth = CEP_MAD_SQ_DEPTH;
	qp_create.rq_sge = CEP_MAD_RQ_SGE;
	qp_create.sq_sge = CEP_MAD_SQ_SGE;
	qp_create.sq_signaled = TRUE;
	/*
	 * We use the port CM's al_obj_t as the context to allow using
	 * deref_al_obj as the destroy callback.
	 */
	status = ib_get_spl_qp( p_port_cep->h_pd, p_port_attr->port_guid,
		&qp_create, &p_port_cep->obj, __cep_qp_event_cb, &p_port_cep->pool_key,
		&p_port_cep->h_qp );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_spl_qp failed with status %s\n", ib_get_err_str(status)) );
		return status;
	}
	/* Reference the port object on behalf of the QP. */
	ref_al_obj( &p_port_cep->obj );

	/* Create the MAD service. */
	cl_memclr( &mad_svc, sizeof(mad_svc) );
	mad_svc.mad_svc_context = p_port_cep;
	mad_svc.pfn_mad_recv_cb = __cep_mad_recv_cb;
	mad_svc.pfn_mad_send_cb = __cep_mad_send_cb;
	mad_svc.support_unsol = TRUE;
	mad_svc.mgmt_class = IB_MCLASS_COMM_MGMT;
	mad_svc.mgmt_version = IB_MCLASS_CM_VER_2;
	mad_svc.method_array[IB_MAD_METHOD_SEND] = TRUE;
	status =
		ib_reg_mad_svc( p_port_cep->h_qp, &mad_svc, &p_port_cep->h_mad_svc );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_mad_svc failed with status %s\n", ib_get_err_str(status)) );
		return status;
	}

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


/*
 * Performs immediate cleanup of resources.
 */
static void
__destroying_port_cep(
	IN				al_obj_t					*p_obj )
{
	cep_agent_t			*p_port_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	ib_port_attr_mod_t	port_attr_mod;

	AL_ENTER( AL_DBG_CM );

	p_port_cep = PARENT_STRUCT( p_obj, cep_agent_t, obj );

	if( p_port_cep->port_guid )
	{
		KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
		cl_qmap_remove_item( &gp_cep_mgr->port_map, &p_port_cep->item );
		KeReleaseInStackQueuedSpinLock( &hdl );
	}

	if( p_port_cep->h_qp )
	{
		ib_destroy_qp( p_port_cep->h_qp, (ib_pfn_destroy_cb_t)deref_al_obj_cb );
		p_port_cep->h_qp = NULL;
	}

	if( p_port_cep->h_pd )
	{
		ib_dealloc_pd( p_port_cep->h_pd, (ib_pfn_destroy_cb_t)deref_al_obj_cb );
		p_port_cep->h_pd = NULL;
	}

	if( p_port_cep->h_ca )
	{
		/* Update local port attributes */
		port_attr_mod.cap.cm = FALSE;
		ib_modify_ca( p_port_cep->h_ca, p_port_cep->port_num,
			IB_CA_MOD_IS_CM_SUPPORTED, &port_attr_mod );
		deref_al_obj( &p_port_cep->h_ca->obj );
		p_port_cep->h_ca = NULL;
	}

	AL_EXIT( AL_DBG_CM );
}



/*
 * Release all resources allocated by a port CM agent.  Finishes any cleanup
 * for a port agent.
 */
static void
__free_port_cep(
	IN				al_obj_t					*p_obj )
{
	cep_agent_t			*p_port_cep;

	AL_ENTER( AL_DBG_CM );

	p_port_cep = PARENT_STRUCT( p_obj, cep_agent_t, obj );
	destroy_al_obj( &p_port_cep->obj );
	cl_free( p_port_cep );

	AL_EXIT( AL_DBG_CM );
}


/*
 * Create a port agent for a given port.
 */
static ib_api_status_t
__create_port_cep(
	IN				ib_pnp_port_rec_t			*p_pnp_rec )
{
	cep_agent_t			*p_port_cep;
	ib_api_status_t		status;
	ib_port_attr_mod_t	port_attr_mod;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	/* calculate size of port_cm struct */
	p_port_cep = (cep_agent_t*)cl_zalloc( sizeof(cep_agent_t) );
	if( !p_port_cep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed to cl_zalloc port CM agent.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &p_port_cep->obj, AL_OBJ_TYPE_CM );

	status = init_al_obj( &p_port_cep->obj, p_port_cep, TRUE,
		__destroying_port_cep, NULL, __free_port_cep );
	if( status != IB_SUCCESS )
	{
		__free_port_cep( &p_port_cep->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/* Attach to the global CM object. */
	status = attach_al_obj( &gp_cep_mgr->obj, &p_port_cep->obj );
	if( status != IB_SUCCESS )
	{
		p_port_cep->obj.pfn_destroy( &p_port_cep->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	p_port_cep->port_guid = p_pnp_rec->p_port_attr->port_guid;
	p_port_cep->port_num = p_pnp_rec->p_port_attr->port_num;
	p_port_cep->base_lid = p_pnp_rec->p_port_attr->lid;

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	cl_qmap_insert(
		&gp_cep_mgr->port_map, p_port_cep->port_guid, &p_port_cep->item );
	KeReleaseInStackQueuedSpinLock( &hdl );

	/* Get a reference to the CA on which we are loading. */
	p_port_cep->h_ca = acquire_ca( p_pnp_rec->p_ca_attr->ca_guid );
	if( !p_port_cep->h_ca )
	{
		p_port_cep->obj.pfn_destroy( &p_port_cep->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("acquire_ca failed.\n") );
		return IB_INVALID_GUID;	}

	status = __init_data_svc( p_port_cep, p_pnp_rec->p_port_attr );
	if( status != IB_SUCCESS )
	{
		p_port_cep->obj.pfn_destroy( &p_port_cep->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("__init_data_svc failed with status %s.\n",
			ib_get_err_str(status)) );
		return status;
	}

	/* Update local port attributes */
	cl_memclr( &port_attr_mod, sizeof(ib_port_attr_mod_t) );
	port_attr_mod.cap.cm = TRUE;
	status = ib_modify_ca( p_port_cep->h_ca, p_pnp_rec->p_port_attr->port_num,
		IB_CA_MOD_IS_CM_SUPPORTED, &port_attr_mod );

	/* Update the PNP context to reference this port. */
	p_pnp_rec->pnp_rec.context = p_port_cep;

	/* Release the reference taken in init_al_obj. */
	deref_ctx_al_obj( &p_port_cep->obj, E_REF_INIT );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


/******************************************************************************
* Global CEP manager
******************************************************************************/

static cep_cid_t*
__get_lcid(
		OUT			net32_t* const				p_cid )
{
	cl_status_t			status;
	uint32_t			size, cid;
	cep_cid_t			*p_cep_cid;

	AL_ENTER( AL_DBG_CM );

	size = (uint32_t)cl_vector_get_size( &gp_cep_mgr->cid_vector );
	cid = gp_cep_mgr->free_cid;
	if( gp_cep_mgr->free_cid == size )
	{
		/* Grow the vector pool. */
		status =
			cl_vector_set_size( &gp_cep_mgr->cid_vector, size + CEP_CID_GROW );
		if( status != CL_SUCCESS )
		{
			AL_EXIT( AL_DBG_CM );
			return NULL;
		}
		/*
		 * Return the the start of the free list since the
		 * entry initializer incremented it.
		 */
		gp_cep_mgr->free_cid = size;
	}

	/* Get the next free entry. */
	p_cep_cid = (cep_cid_t*)cl_vector_get_ptr( &gp_cep_mgr->cid_vector, cid );

	/* Update the next entry index. */
	gp_cep_mgr->free_cid = (uint32_t)(uintn_t)p_cep_cid->p_cep;

	*p_cid = cid;

	AL_EXIT( AL_DBG_CM );
	return p_cep_cid;
}


static inline kcep_t*
__lookup_cep(
	IN				ib_al_handle_t				h_al OPTIONAL,
	IN				net32_t						cid )
{
	size_t				idx;
	cep_cid_t			*p_cid;

	/* Mask off the counter bits so we get the index in our vector. */
	idx = cid & CEP_MAX_CID_MASK;

	if( idx >= cl_vector_get_size( &gp_cep_mgr->cid_vector ) )
		return NULL;

	p_cid = (cep_cid_t*)cl_vector_get_ptr( &gp_cep_mgr->cid_vector, idx );
	if( !p_cid->h_al )
		return NULL;

	/*
	 * h_al is NULL when processing MADs, so we need to match on
	 * the actual local communication ID.  If h_al is non-NULL, we
	 * are doing a lookup from a call to our API, and only need to match
	 * on the index in the vector (without the modifier).
	 */
	if( h_al )
	{
		if( p_cid->h_al != h_al )
			return NULL;
	}
	else if( p_cid->p_cep->local_comm_id != cid )
	{
		return NULL;
	}

	return p_cid->p_cep;
}


/*
 * Lookup a CEP by remote comm ID and CA GUID.
 */
static kcep_t*
__lookup_by_id(
	IN				net32_t						remote_comm_id,
	IN				net64_t						remote_ca_guid )
{
	cl_rbmap_item_t		*p_item;
	kcep_t			*p_cep;

	AL_ENTER( AL_DBG_CM );

	/* Match against pending connections using remote comm ID and CA GUID. */
	p_item = cl_rbmap_root( &gp_cep_mgr->conn_id_map );
	while( p_item != cl_rbmap_end( &gp_cep_mgr->conn_id_map ) )
	{
		p_cep = PARENT_STRUCT( p_item, kcep_t, rem_id_item );

		if( remote_comm_id < p_cep->remote_comm_id )
			p_item = cl_rbmap_left( p_item );
		else if( remote_comm_id > p_cep->remote_comm_id )
			p_item = cl_rbmap_right( p_item );
		else if( remote_ca_guid < p_cep->remote_ca_guid )
			p_item = cl_rbmap_left( p_item );
		else if( remote_ca_guid > p_cep->remote_ca_guid )
			p_item = cl_rbmap_right( p_item );
		else
			return p_cep;
	}

	AL_EXIT( AL_DBG_CM );
	return NULL;
}


static intn_t
__cm_rdma_req_cmp(
	__in UINT64 mask,
	__in const ib_cm_rdma_req_t* p_cmp1,
	__in const ib_cm_rdma_req_t* p_cmp2 )
{
	intn_t cmp;

	if( p_cmp1->maj_min_ver != p_cmp2->maj_min_ver )
	{
		return (intn_t)p_cmp1->maj_min_ver - (intn_t)p_cmp2->maj_min_ver;
	}

	if( p_cmp1->ipv != p_cmp2->ipv )
	{
		return (intn_t)p_cmp1->ipv - (intn_t)p_cmp2->ipv;
	}

	if( mask & IB_REQ_CM_RDMA_CMP_SRC_PORT )
	{
		if( p_cmp1->src_port != p_cmp2->src_port )
		{
			return (intn_t)p_cmp1->src_port - (intn_t)p_cmp2->src_port;
		}
	}

	if( mask & IB_REQ_CM_RDMA_CMP_SRC_IP )
	{
		cmp = cl_memcmp( p_cmp1->src_ip_addr,
			p_cmp2->src_ip_addr, sizeof(p_cmp2->src_ip_addr) );
		if( cmp != 0 )
		{
			return cmp;
		}
	}

	if( mask & IB_REQ_CM_RDMA_CMP_DST_IP )
	{
		cmp = cl_memcmp( p_cmp1->dst_ip_addr,
			p_cmp2->dst_ip_addr, sizeof(p_cmp2->dst_ip_addr) );
		if( cmp != 0 )
		{
			return cmp;
		}
	}

	/*
	 * TODO: Richer compare options to allow specifying pdata compare
	 */
	return 0;
}


/*
 * Lookup a CEP by Service ID and private data.
 */
static kcep_t*
__lookup_listen(
	IN				net64_t						sid,
	IN				net64_t						port_guid,
	IN				uint8_t						*p_pdata )
{
	cl_rbmap_item_t		*p_item;
	kcep_t				*p_cep;
	intn_t				cmp;

	AL_ENTER( AL_DBG_CM );

	/* Match against pending connections using remote comm ID and CA GUID. */
	p_item = cl_rbmap_root( &gp_cep_mgr->listen_map );
	while( p_item != cl_rbmap_end( &gp_cep_mgr->listen_map ) )
	{
		p_cep = PARENT_STRUCT( p_item, kcep_t, listen_item );

		if( sid == p_cep->sid )
			goto port_cmp;
		else if( sid < p_cep->sid )
			p_item = cl_rbmap_left( p_item );
		else
			p_item = cl_rbmap_right( p_item );

		continue;

port_cmp:
		if( p_cep->port_guid != IB_ALL_PORTS )
		{
			if( port_guid == p_cep->port_guid )
				goto pdata_cmp;
			else if( port_guid < p_cep->port_guid )
				p_item = cl_rbmap_left( p_item );
			else
				p_item = cl_rbmap_right( p_item );

			continue;
		}

pdata_cmp:
		if( p_cep->p_cmp_buf && p_pdata )
		{
			if( ib_cm_is_rdma_cm_sid(sid) )
			{
				ib_cm_rdma_req_t *p_rdma_req = (ib_cm_rdma_req_t *)p_pdata;
				CL_ASSERT(p_cep->cmp_len >= FIELD_OFFSET(ib_cm_rdma_req_t, pdata));

				/* reject connection request with incorrect version parameters */
				if( ib_cm_is_rdma_cm_req_valid( p_rdma_req ) == FALSE )
				{
					AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
						("RDMA CM connection req is invalid: maj_min_ver %d, ipv %#x \n", 
						p_rdma_req->maj_min_ver, p_rdma_req->ipv ) );
					return NULL;
				}

				cmp = __cm_rdma_req_cmp(
					p_cep->cmp_offset,
					p_rdma_req,
					(ib_cm_rdma_req_t*)p_cep->p_cmp_buf
					);
			}
			else
			{
				/*
				 * TODO: this check seems to be for catching a malformed listen, and should
				 * be trapped when the listen is created.  Checking after the fact is dumb.
				 */
				int len = min(p_cep->cmp_len, IB_REQ_PDATA_SIZE - p_cep->cmp_offset);
				cmp = cl_memcmp( &p_pdata[p_cep->cmp_offset],
					p_cep->p_cmp_buf, len );
			}

			if( !cmp )
				goto match;
			else if( cmp < 0 )
				p_item = cl_rbmap_left( p_item );
			else
				p_item = cl_rbmap_right( p_item );

			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
				("Svc ID match but compare buffer mismatch.\n") );
			continue;
		}

match:
		/* Everything matched. */
		AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("] matched CID = %d\n", p_cep->cid) );
		return p_cep;
	}

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("] not found\n") );
	return NULL;
}


static kcep_t*
__insert_by_id(
	IN				kcep_t* const				p_new_cep )
{
	kcep_t				*p_cep;
	cl_rbmap_item_t		*p_item, *p_insert_at;
	boolean_t			left = TRUE;

	AL_ENTER( AL_DBG_CM );

	p_item = cl_rbmap_root( &gp_cep_mgr->conn_id_map );
	p_insert_at = p_item;
	while( p_item != cl_rbmap_end( &gp_cep_mgr->conn_id_map ) )
	{
		p_insert_at = p_item;
		p_cep = PARENT_STRUCT( p_item, kcep_t, rem_id_item );

		if( p_new_cep->remote_comm_id < p_cep->remote_comm_id )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_new_cep->remote_comm_id > p_cep->remote_comm_id )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_new_cep->remote_ca_guid < p_cep->remote_ca_guid )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_new_cep->remote_ca_guid > p_cep->remote_ca_guid )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else
		{
			AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
				("WARNING: Duplicate remote CID and CA GUID.\n") );
			goto done;
		}
	}

	cl_rbmap_insert(
		&gp_cep_mgr->conn_id_map, p_insert_at, &p_new_cep->rem_id_item, left );
	p_cep = p_new_cep;

done:
	AL_EXIT( AL_DBG_CM );
	return p_cep;
}


static kcep_t*
__insert_by_qpn(
	IN				kcep_t* const				p_new_cep )
{
	kcep_t				*p_cep;
	cl_rbmap_item_t		*p_item, *p_insert_at;
	boolean_t			left = TRUE;

	AL_ENTER( AL_DBG_CM );

	p_item = cl_rbmap_root( &gp_cep_mgr->conn_qp_map );
	p_insert_at = p_item;
	while( p_item != cl_rbmap_end( &gp_cep_mgr->conn_qp_map ) )
	{
		p_insert_at = p_item;
		p_cep = PARENT_STRUCT( p_item, kcep_t, rem_id_item );

		if( p_new_cep->remote_qpn < p_cep->remote_qpn )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_new_cep->remote_qpn > p_cep->remote_qpn )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else if( p_new_cep->remote_ca_guid < p_cep->remote_ca_guid )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else if( p_new_cep->remote_ca_guid > p_cep->remote_ca_guid )
			p_item = cl_rbmap_right( p_item ), left = FALSE;
		else
		{
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("WARNING: Duplicate remote QPN and CA GUID.\n") );
			goto done;
		}
	}

	cl_rbmap_insert(
		&gp_cep_mgr->conn_qp_map, p_insert_at, &p_new_cep->rem_qp_item, left );
	p_cep = p_new_cep;

done:
	AL_EXIT( AL_DBG_CM );
	return p_cep;
}


static inline kcep_t*
__insert_cep(
	IN				kcep_t* const				p_new_cep )
{
	kcep_t				*p_cep;

	AL_ENTER( AL_DBG_CM );

	p_cep = __insert_by_qpn( p_new_cep );
	if( p_cep != p_new_cep )
		goto err;

	p_cep = __insert_by_id( p_new_cep );
	if( p_cep != p_new_cep )
	{
		cl_rbmap_remove_item(
			&gp_cep_mgr->conn_qp_map, &p_new_cep->rem_qp_item );
err:
		/*
		 * Clear the remote QPN and comm ID so that we don't try
		 * to remove the CEP from those maps.
		 */
		p_new_cep->remote_qpn = 0;
		p_new_cep->remote_comm_id = 0;
	}

	AL_EXIT( AL_DBG_CM );
	return p_cep;
}


static inline void
__remove_cep(
	IN				kcep_t* const				p_cep )
{
	AL_ENTER( AL_DBG_CM );

	if( p_cep->remote_comm_id )
	{
		cl_rbmap_remove_item(
			&gp_cep_mgr->conn_id_map, &p_cep->rem_id_item );
		p_cep->remote_comm_id = 0;
	}
	if( p_cep->remote_qpn )
	{
		cl_rbmap_remove_item(
			&gp_cep_mgr->conn_qp_map, &p_cep->rem_qp_item );
		p_cep->remote_qpn = 0;
	}

	AL_EXIT( AL_DBG_CM );
}


static boolean_t
__is_lid_valid(
	IN				ib_net16_t					lid,
	IN				ib_net16_t					port_lid,
	IN				uint8_t						lmc )
{
	uint16_t		lid1;
	uint16_t		lid2;
	uint16_t		path_bits;

	if(lmc)
	{
		lid1 = CL_NTOH16(lid);
		lid2 = CL_NTOH16(port_lid);
		path_bits = 0;

		if( lid1 < lid2 )
			return FALSE;

		while( lmc-- )
			path_bits = (uint16_t)( (path_bits << 1) | 1 );

		lid2 |= path_bits;

		if( lid1 > lid2)
			return FALSE;
	}
	else
	{
		if (lid != port_lid)
			return FALSE;
	}

	return TRUE;
}


static inline boolean_t
__is_gid_valid(
	IN		const	ib_port_attr_t* const		p_port_attr,
	IN		const	ib_gid_t* const				p_gid )
{
	uint16_t	idx;

	for( idx = 0; idx < p_port_attr->num_gids; idx++ )
	{
		if( !cl_memcmp(
			p_gid, &p_port_attr->p_gid_table[idx], sizeof(ib_gid_t) ) )
		{
			return TRUE;
		}
	}
	return FALSE;
}


static inline boolean_t
__get_pkey_index(
	IN		const	ib_port_attr_t* const		p_port_attr,
	IN		const	net16_t						pkey,
		OUT			uint16_t* const				p_pkey_index )
{
	uint16_t	idx;

	for( idx = 0; idx < p_port_attr->num_pkeys; idx++ )
	{
		if( p_port_attr->p_pkey_table[idx] == pkey )
		{
			*p_pkey_index = idx;
			return TRUE;
		}
	}

	return FALSE;
}


/* Returns the 1-based port index of the CEP agent with the specified GID. */
static cep_agent_t*
__find_port_cep(
	IN		const	ib_gid_t* const				p_gid,
	IN		const	net16_t						lid,
	IN		const	net16_t						pkey,
		OUT			uint16_t* const				p_pkey_index )
{
	cep_agent_t				*p_port_cep;
	cl_list_item_t			*p_item;
	const ib_port_attr_t	*p_port_attr;

	AL_ENTER( AL_DBG_CM );

	cl_spinlock_acquire( &gp_cep_mgr->obj.lock );
	for( p_item = cl_qlist_head( &gp_cep_mgr->obj.obj_list );
		p_item != cl_qlist_end( &gp_cep_mgr->obj.obj_list );
		p_item = cl_qlist_next( p_item ) )
	{
		p_port_cep = PARENT_STRUCT( p_item, cep_agent_t, obj.pool_item );

		CL_ASSERT( p_port_cep->port_num );

		ci_ca_lock_attr( p_port_cep->h_ca->obj.p_ci_ca );

		p_port_attr = p_port_cep->h_ca->obj.p_ci_ca->p_pnp_attr->p_port_attr;
		p_port_attr += (p_port_cep->port_num - 1);

		if( __is_lid_valid( lid, p_port_attr->lid, p_port_attr->lmc ) &&
			__is_gid_valid( p_port_attr, p_gid ) &&
			__get_pkey_index( p_port_attr, pkey, p_pkey_index ) )
		{
			ci_ca_unlock_attr( p_port_cep->h_ca->obj.p_ci_ca );
			cl_spinlock_release( &gp_cep_mgr->obj.lock );
			AL_EXIT( AL_DBG_CM );
			return p_port_cep;
		}

		ci_ca_unlock_attr( p_port_cep->h_ca->obj.p_ci_ca );
	}
	cl_spinlock_release( &gp_cep_mgr->obj.lock );
	AL_EXIT( AL_DBG_CM );
	return NULL;
}


/*
 * PnP callback for port event notifications.
 */
static ib_api_status_t
__cep_pnp_cb(
	IN				ib_pnp_rec_t				*p_pnp_rec )
{
	ib_api_status_t		status = IB_SUCCESS;

	AL_ENTER( AL_DBG_CM );

	AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_PNP,
		("p_pnp_rec->pnp_event = 0x%x (%s)\n",
		p_pnp_rec->pnp_event, ib_get_pnp_event_str( p_pnp_rec->pnp_event )) );

	switch( p_pnp_rec->pnp_event )
	{
	case IB_PNP_PORT_ADD:
		/* Create the port agent. */
		CL_ASSERT( !p_pnp_rec->context );
		status = __create_port_cep( (ib_pnp_port_rec_t*)p_pnp_rec );
		break;

	case IB_PNP_PORT_REMOVE:
		CL_ASSERT( p_pnp_rec->context );

		/* Destroy the port agent. */
		ref_al_obj( &((cep_agent_t*)p_pnp_rec->context)->obj );
		((cep_agent_t*)p_pnp_rec->context)->obj.pfn_destroy(
			&((cep_agent_t*)p_pnp_rec->context)->obj, NULL );
		break;

	default:
		break;	/* Ignore other PNP events. */
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


static inline int64_t
__min_timewait(
	IN				int64_t						current_min,
	IN				kcep_t* const				p_cep )
{
	/*
	 * The minimum timer interval is 50 milliseconds.  This means
	 * 500000 100ns increments.  Since __process_timewait divides the
	 * result in half (so that the worst cast timewait interval is 150%)
	 * we compensate for this here.  Note that relative time values are
	 * expressed as negative.
	 */
#define MIN_TIMEWAIT_100NS	-1000000

	/* Still in timewait - try again next time. */
	if( !current_min )
	{
		return min( p_cep->timewait_time.QuadPart, MIN_TIMEWAIT_100NS );
	}
	else
	{
		return max( current_min,
			min( p_cep->timewait_time.QuadPart, MIN_TIMEWAIT_100NS ) );
	}
}


/*
 * Timer callback to process CEPs in timewait state.  Returns time in ms.
 */
static uint32_t
__process_timewait()
{
	cl_list_item_t		*p_item;
	kcep_t				*p_cep;
	LARGE_INTEGER		timeout;
	int64_t				min_timewait = 0;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	timeout.QuadPart = 0;

	p_item = cl_qlist_head( &gp_cep_mgr->timewait_list );
	while( p_item != cl_qlist_end( &gp_cep_mgr->timewait_list ) )
	{
		p_cep = PARENT_STRUCT( p_item, kcep_t, timewait_item );
		p_item = cl_qlist_next( p_item );

		CL_ASSERT( p_cep->state == CEP_STATE_DESTROY ||
			p_cep->state == CEP_STATE_TIMEWAIT );

		CL_ASSERT( !p_cep->p_mad );

		if( KeWaitForSingleObject( &p_cep->timewait_timer, Executive,
			KernelMode, FALSE, &timeout ) != STATUS_SUCCESS )
		{
			/* Still in timewait - try again next time. */
			min_timewait = __min_timewait( min_timewait, p_cep );
			continue;
		}

		if( p_cep->ref_cnt )
		{
			/* Send outstanding or destruction in progress. */
			min_timewait = __min_timewait( min_timewait, p_cep );
			continue;
		}

		/* Remove from the timewait list. */
		cl_qlist_remove_item( &gp_cep_mgr->timewait_list, &p_cep->timewait_item );

		/*
		 * Not in timewait.  Remove the CEP from the maps - it should
		 * no longer be matched against.
		 */
		__remove_cep( p_cep );

		if( p_cep->state == CEP_STATE_DESTROY )
		{
			__destroy_cep( p_cep );
		}
		else
		{
			/* Move the CEP to the IDLE state so that it can be used again. */
			p_cep->state = CEP_STATE_IDLE;
		}
	}

	AL_EXIT( AL_DBG_CM );
	return (uint32_t)(min_timewait / -20000);
}


/*
 * Timer callback to process CEPs in timewait state.
 */
static void
__cep_timewait_cb(
	IN				void						*context )
{
	KLOCK_QUEUE_HANDLE	hdl;
	uint32_t			min_timewait;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( context );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	KeAcquireInStackQueuedSpinLockAtDpcLevel( &gp_cep_mgr->lock, &hdl );

	min_timewait = __process_timewait();

	if( cl_qlist_count( &gp_cep_mgr->timewait_list ) )
	{
		/*
		 * Reset the timer for half of the shortest timeout - this results
		 * in a worst case timeout of 150% of timewait.
		 */
		cl_timer_trim( &gp_cep_mgr->timewait_timer, min_timewait );
	}

	KeReleaseInStackQueuedSpinLockFromDpcLevel( &hdl );

	AL_EXIT( AL_DBG_CM );
}


/*
 * Starts immediate cleanup of the CM.  Invoked during al_obj destruction.
 */
static void
__destroying_cep_mgr(
	IN				al_obj_t*					p_obj )
{
	ib_api_status_t		status;
	KLOCK_QUEUE_HANDLE	hdl;
	cl_list_item_t		*p_item;
	kcep_t				*p_cep;
	LARGE_INTEGER		timeout;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( &gp_cep_mgr->obj == p_obj );
	UNUSED_PARAM( p_obj );

	/* Deregister from PnP notifications. */
	if( gp_cep_mgr->h_pnp )
	{
		status = ib_dereg_pnp(
			gp_cep_mgr->h_pnp, (ib_pfn_destroy_cb_t)deref_al_obj_cb );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("ib_dereg_pnp failed with status %s.\n",
				ib_get_err_str(status)) );
			deref_al_obj( &gp_cep_mgr->obj );
		}
	}

	/* Cancel all timewait timers. */
	timeout.QuadPart = 0;
	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	for( p_item = cl_qlist_head( &gp_cep_mgr->timewait_list );
		p_item != cl_qlist_end( &gp_cep_mgr->timewait_list );
		p_item = cl_qlist_next( p_item ) )
	{
		p_cep = PARENT_STRUCT( p_item, kcep_t, timewait_item );
		KeSetTimer( &p_cep->timewait_timer, timeout, NULL );
	}
	__process_timewait();
	KeReleaseInStackQueuedSpinLock( &hdl );

	AL_EXIT( AL_DBG_CM );
}


/*
 * Frees the global CEP agent.  Invoked during al_obj destruction.
 */
static void
__free_cep_mgr(
	IN				al_obj_t*					p_obj )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( &gp_cep_mgr->obj == p_obj );
	/* All listen request should have been cleaned up by this point. */
	CL_ASSERT( cl_is_rbmap_empty( &gp_cep_mgr->listen_map ) );
	/* All connections should have been cancelled/disconnected by now. */
	CL_ASSERT( cl_is_rbmap_empty( &gp_cep_mgr->conn_id_map ) );
	CL_ASSERT( cl_is_rbmap_empty( &gp_cep_mgr->conn_qp_map ) );

	cl_vector_destroy( &gp_cep_mgr->cid_vector );

	cl_timer_destroy( &gp_cep_mgr->timewait_timer );

	/*
	 * All CM port agents should have been destroyed by now via the
	 * standard child object destruction provided by the al_obj.
	 */
	ExDeleteNPagedLookasideList( &gp_cep_mgr->cep_pool );
	destroy_al_obj( p_obj );

	cl_free( gp_cep_mgr );
	gp_cep_mgr = NULL;

	AL_EXIT( AL_DBG_CM );
}


static cl_status_t
__cid_init(
	IN		void* const						p_element,
	IN		void*							context )
{
	cep_cid_t		*p_cid;

	UNUSED_PARAM( context );

	p_cid = (cep_cid_t*)p_element;

	p_cid->h_al = NULL;
	p_cid->p_cep = (kcep_t*)(uintn_t)++gp_cep_mgr->free_cid;
	p_cid->modifier = 0;

	return CL_SUCCESS;
}


/*
 * Allocates and initialized the global CM agent.
 */
ib_api_status_t
create_cep_mgr(
	IN				al_obj_t* const				p_parent_obj )
{
	ib_api_status_t		status;
	cl_status_t			cl_status;
	ib_pnp_req_t		pnp_req;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( gp_cep_mgr == NULL );

	/* Allocate the global CM agent. */
	gp_cep_mgr = (al_cep_mgr_t*)cl_zalloc( sizeof(al_cep_mgr_t) );
	if( !gp_cep_mgr )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("Failed allocation of global CM agent.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	construct_al_obj( &gp_cep_mgr->obj, AL_OBJ_TYPE_CM );
	ExInitializeNPagedLookasideList( &gp_cep_mgr->cep_pool, NULL, NULL,
		0, sizeof(kcep_t), 'PECK', 0 );
	cl_qmap_init( &gp_cep_mgr->port_map );
	cl_rbmap_init( &gp_cep_mgr->listen_map );
	cl_rbmap_init( &gp_cep_mgr->conn_id_map );
	cl_rbmap_init( &gp_cep_mgr->conn_qp_map );
	cl_qlist_init( &gp_cep_mgr->timewait_list );
	/* Timer initialization can't fail in kernel-mode. */
	cl_timer_init( &gp_cep_mgr->timewait_timer, __cep_timewait_cb, NULL );
	cl_vector_construct( &gp_cep_mgr->cid_vector );

	status = init_al_obj( &gp_cep_mgr->obj, NULL, FALSE,
		__destroying_cep_mgr, NULL, __free_cep_mgr );
	if( status != IB_SUCCESS )
	{
		__free_cep_mgr( &gp_cep_mgr->obj );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("init_al_obj failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}
	/* Attach to the parent object. */
	status = attach_al_obj( p_parent_obj, &gp_cep_mgr->obj );
	if( status != IB_SUCCESS )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("attach_al_obj returned %s.\n", ib_get_err_str(status)) );
		return status;
	}

	cl_status = cl_vector_init( &gp_cep_mgr->cid_vector,
		CEP_CID_MIN, CEP_CID_GROW, sizeof(cep_cid_t), __cid_init, NULL, NULL );
	if( cl_status != CL_SUCCESS )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("cl_vector_init failed with status %#x.\n",
			cl_status) );
		return ib_convert_cl_status( cl_status );
	}

	gp_cep_mgr->free_cid = 1;

	/* Register for port PnP notifications. */
	cl_memclr( &pnp_req, sizeof(pnp_req) );
	pnp_req.pnp_class = IB_PNP_PORT;
	pnp_req.pnp_context = &gp_cep_mgr->obj;
	pnp_req.pfn_pnp_cb = __cep_pnp_cb;
	status = ib_reg_pnp( gh_al, &pnp_req, &gp_cep_mgr->h_pnp );
	if( status != IB_SUCCESS )
	{
		gp_cep_mgr->obj.pfn_destroy( &gp_cep_mgr->obj, NULL );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_reg_pnp failed with status %s.\n", ib_get_err_str(status)) );
		return status;
	}

	/*
	 * Leave the reference taken in init_al_obj oustanding since PnP
	 * deregistration is asynchronous.  This replaces a call to ref and
	 * deref the object.
	 */

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}

/******************************************************************************
* CEP manager API
******************************************************************************/


/* Called with the CEP and CEP manager locks held */
static ib_api_status_t
__cep_queue_mad(
	IN				kcep_t* const				p_cep,
	IN				ib_mad_element_t*			p_mad )
{
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", p_cep->cid) );

	CL_ASSERT( !p_mad->p_next );

	if( p_cep->state == CEP_STATE_DESTROY )
	{
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_STATE;
	}

	/* Queue this MAD for processing. */
	if( p_cep->p_mad_head )
	{
		CL_ASSERT( p_cep->signalled );
		/*
		 * If there's already a MAD at the head of the list, we will not
		 * invoke the callback.  Just queue and exit.
		 */
		CL_ASSERT( p_cep->p_mad_tail );
		p_cep->p_mad_tail->p_next = p_mad;
		p_cep->p_mad_tail = p_mad;
		AL_EXIT( AL_DBG_CM );
		return IB_PENDING;
	}

	p_cep->p_mad_head = p_mad;
	p_cep->p_mad_tail = p_mad;

	if( p_cep->signalled )
	{
		/* signalled was already non-zero.  Don't invoke the callback again. */
		AL_EXIT( AL_DBG_CM );
		return IB_PENDING;
	}

	p_cep->signalled = TRUE;

	/* Take a reference since we're about to invoke the callback. */
	cl_atomic_inc( &p_cep->ref_cnt );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


static inline void
__cep_complete_irp(
	IN				kcep_t* const				p_cep,
	IN				NTSTATUS					status,
	IN				CCHAR						increment )
{
	IRP					*p_irp;

	AL_ENTER( AL_DBG_CM );

	p_irp = InterlockedExchangePointer( &p_cep->p_irp, NULL );

	if( p_irp )
	{
#pragma warning(push, 3)
		IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)

		/* Complete the IRP. */
		p_irp->IoStatus.Status = status;
		p_irp->IoStatus.Information = 0;
		IoCompleteRequest( p_irp, increment );
	}

	AL_EXIT( AL_DBG_CM );
}


static inline void
__process_cep(
	IN				kcep_t* const				p_cep )
{
	ib_pfn_destroy_cb_t		pfn_destroy_cb;
	void					*context;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	/* Signal to the user there are callback waiting. */
	if( p_cep->pfn_cb )
		p_cep->pfn_cb( p_cep->p_cid->h_al, p_cep->cid );
	else
		__cep_complete_irp( p_cep, STATUS_SUCCESS, IO_NETWORK_INCREMENT );

	pfn_destroy_cb = p_cep->pfn_destroy_cb;
	context = p_cep->context;

	/*
	 * Release the reference for the callback and invoke the destroy
	 * callback if necessary.
	 */
	if( !cl_atomic_dec( &p_cep->ref_cnt ) && pfn_destroy_cb )
		pfn_destroy_cb( context );

	AL_EXIT( AL_DBG_CM );
}


static uint32_t
__calc_mad_timeout(
	IN		const	uint8_t						pkt_life )
{
	/*
	 * Calculate the retry timeout.
	 * All timeout values in micro seconds are expressed as 4.096 * 2^x,
	 * where x is the timeout.  The formula to approximates this to
	 * milliseconds using just shifts and subtraction is:
	 *	timeout_ms = 67 << (x - 14)
	 * The results are off by 0.162%.
	 *
	 * Note that we will never return less than 1 millisecond.  We also
	 * trap exceedingly large values to prevent wrapping.
	 */
	if( pkt_life > 39 )
		return INT_MAX;
	if( pkt_life > 14 )
		return 67 << (pkt_life - 14);
	else if( pkt_life > 8 )
		return 67 >> (14 - pkt_life);
	else
		return 1;
}


/* CEP manager lock is held when calling this function. */
static kcep_t*
__create_cep()
{
	kcep_t				*p_cep;

	AL_ENTER( AL_DBG_CM );

	p_cep = ExAllocateFromNPagedLookasideList( &gp_cep_mgr->cep_pool );
	if( !p_cep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to allocate CEP.\n") );
		return NULL;
	}

	cl_memclr( p_cep, sizeof(kcep_t) );

	KeInitializeTimer( &p_cep->timewait_timer );

	p_cep->state = CEP_STATE_IDLE;

	/*
	 * Pre-charge the reference count to 1.  The code will invoke the
	 * destroy callback once the ref count reaches to zero.
	 */
	p_cep->ref_cnt = 1;
	p_cep->signalled = FALSE;

	/* Find a free entry in the CID vector. */
	p_cep->p_cid = __get_lcid( &p_cep->cid );

	if( !p_cep->p_cid )
	{
		ExFreeToNPagedLookasideList( &gp_cep_mgr->cep_pool, p_cep );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed to get CID.\n") );
		return NULL;
	}

	p_cep->p_cid->modifier++;
	/*
	 * We don't ever want a modifier of zero for the CID at index zero
	 * since it would result in a total CID of zero.
	 */
	if( !p_cep->cid && !p_cep->p_cid->modifier )
		p_cep->p_cid->modifier++;

	p_cep->local_comm_id = p_cep->cid | (p_cep->p_cid->modifier << 24);
	p_cep->tid = p_cep->local_comm_id;

	p_cep->p_cid->p_cep = p_cep;

	ref_al_obj( &gp_cep_mgr->obj );

	AL_EXIT( AL_DBG_CM );
	return p_cep;
}


static inline void
__bind_cep(
	IN				kcep_t* const				p_cep,
	IN				ib_al_handle_t				h_al,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context )
{
	CL_ASSERT( p_cep );
	CL_ASSERT( p_cep->p_cid );
	CL_ASSERT( h_al );

	p_cep->p_cid->h_al = h_al;
	p_cep->pfn_cb = pfn_cb;
	p_cep->context = context;

	/* Track the CEP in its owning AL instance. */
	cl_spinlock_acquire( &h_al->obj.lock );
	cl_qlist_insert_tail( &h_al->cep_list, &p_cep->al_item );
	cl_spinlock_release( &h_al->obj.lock );
}

ib_api_status_t
kal_cep_alloc(
	IN				ib_al_handle_t				h_al,
	IN	OUT			net32_t* const				p_cid )
{
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __create_cep();
	KeReleaseInStackQueuedSpinLock( &hdl );

	if( !p_cep )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Failed\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	__bind_cep(p_cep, h_al, NULL, NULL);
	*p_cid = p_cep->cid;
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("allocated CID = %d\n", p_cep->cid) );
	return IB_SUCCESS;
}

ib_api_status_t
kal_cep_config(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb )
{
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( p_cep == NULL )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		return IB_INVALID_HANDLE;
	}

	p_cep->pfn_cb = pfn_cb;
	p_cep->context = context;
	p_cep->pfn_destroy_cb = pfn_destroy_cb;

	KeReleaseInStackQueuedSpinLock( &hdl );
	return IB_SUCCESS;
}

static inline void
__unbind_cep(
	IN				kcep_t* const				p_cep )
{
	CL_ASSERT( p_cep );
	CL_ASSERT( p_cep->p_cid );
	CL_ASSERT( p_cep->p_cid->h_al );

	/* Track the CEP in its owning AL instance. */
	cl_spinlock_acquire( &p_cep->p_cid->h_al->obj.lock );
	cl_qlist_remove_item( &p_cep->p_cid->h_al->cep_list, &p_cep->al_item );
	cl_spinlock_release( &p_cep->p_cid->h_al->obj.lock );

	/*
	 * Set to the internal AL handle - it needs to be non-NULL to indicate it's
	 * a valid entry, and it can't be a user's AL instance to prevent using a
	 * destroyed CEP.
	 */
	p_cep->p_cid->h_al = gh_al;
#ifdef _DEBUG_
	p_cep->pfn_cb = NULL;
#endif	/* _DEBUG_ */
}


static inline void
__calc_timewait(
	IN				kcep_t* const				p_cep )
{

	/*
	 * Use the CEP's stored packet lifetime to calculate the time at which
	 * the CEP exits timewait.  Packet lifetime is expressed as
	 * 4.096 * 2^pkt_life microseconds, and we need a timeout in 100ns
	 * increments.  The formual using just shifts and subtraction is this:
	 *	timeout = (41943 << (pkt_life - 10));
	 * The results are off by .0001%, which should be more than adequate.
	 */
	if( p_cep->max_2pkt_life > 10 )
	{
		p_cep->timewait_time.QuadPart =
			-(41943i64 << (p_cep->max_2pkt_life - 10));
	}
	else
	{
		p_cep->timewait_time.QuadPart =
			-(41943i64 >> (10 - p_cep->max_2pkt_life));
	}
	if( p_cep->target_ack_delay > 10 )
	{
		p_cep->timewait_time.QuadPart -=
			(41943i64 << (p_cep->target_ack_delay - 10));
	}
	else
	{
		p_cep->timewait_time.QuadPart -=
			(41943i64 >> (10 - p_cep->target_ack_delay));
	}
}


/* Called with CEP manager and CEP locks held. */
static inline void
__insert_timewait(
	IN				kcep_t* const				p_cep )
{
	cl_qlist_insert_tail( &gp_cep_mgr->timewait_list, &p_cep->timewait_item );

	KeSetTimer( &p_cep->timewait_timer, p_cep->timewait_time, NULL );

	/*
	 * Reset the timer for half of the shortest timeout - this results
	 * in a worst case timeout of 150% of timewait.
	 */
	cl_timer_trim( &gp_cep_mgr->timewait_timer,
		(uint32_t)(-p_cep->timewait_time.QuadPart / 20000) );
}


static inline ib_api_status_t
__do_cep_rej(
	IN				kcep_t* const				p_cep,
	IN				ib_rej_status_t				rej_status,
	IN		const	uint8_t* const				p_ari,
	IN				uint8_t						ari_len,
	IN		const	uint8_t* const				p_pdata,
	IN				uint8_t						pdata_len )
{
	ib_api_status_t		status;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;

	p_port_cep = __get_cep_agent( p_cep );
	if( !p_port_cep )
		return IB_INSUFFICIENT_RESOURCES;

	status = ib_get_mad( p_port_cep->pool_key, MAD_BLOCK_SIZE, &p_mad );
	if( status != IB_SUCCESS )
		return status;

	__format_mad_av( p_mad, &p_cep->av[p_cep->idx_primary] );

	status = conn_rej_set_ari(
		p_ari, ari_len, (mad_cm_rej_t*)p_mad->p_mad_buf );
	if( status != IB_SUCCESS )
		return status;

	status = conn_rej_set_pdata(
		p_pdata, pdata_len, (mad_cm_rej_t*)p_mad->p_mad_buf );
	if( status != IB_SUCCESS )
		return status;

	__reject_mad( p_port_cep, p_cep, p_mad, rej_status );
	return IB_SUCCESS;
}


static ib_api_status_t
__cep_get_mad(
	IN				kcep_t* const				p_cep,
	IN				net16_t						attr_id,
		OUT			cep_agent_t** const			pp_port_cep,
		OUT			ib_mad_element_t** const	pp_mad )
{
	cep_agent_t			*p_port_cep;
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	p_port_cep = __get_cep_agent( p_cep );
	if( !p_port_cep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("__get_cep_agent failed.\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	status = ib_get_mad( p_port_cep->pool_key, MAD_BLOCK_SIZE, pp_mad );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("ib_get_mad returned %s.\n", ib_get_err_str( status )) );
		return status;
	}

	__format_mad_av( *pp_mad, &p_cep->av[p_cep->idx_primary] );

	__format_mad_hdr( (*pp_mad)->p_mad_buf, p_cep, attr_id );

	*pp_port_cep = p_port_cep;
	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__format_dreq(
	IN				kcep_t* const				p_cep, 
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len,
	IN	OUT			ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_dreq_t		*p_dreq;

	AL_ENTER( AL_DBG_CM );

	p_dreq = (mad_cm_dreq_t*)p_mad->p_mad_buf;

	p_dreq->local_comm_id = p_cep->local_comm_id;
	p_dreq->remote_comm_id = p_cep->remote_comm_id;

	conn_dreq_set_remote_qpn( p_cep->remote_qpn, p_dreq );

	/* copy optional data */
	status = conn_dreq_set_pdata( p_pdata, pdata_len, p_dreq );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__dreq_cep(
	IN				kcep_t* const				p_cep )
{
	ib_api_status_t		status;
	cep_agent_t			*p_agt;
	ib_mad_element_t	*p_mad;

	status = __cep_get_mad( p_cep, CM_DREQ_ATTR_ID, &p_agt, &p_mad );
	if( status != IB_SUCCESS )
		return status;

	status = __format_dreq( p_cep, NULL, 0, p_mad );
	if( status != IB_SUCCESS )
		return status;

	return __cep_send_retry( p_agt, p_cep, p_mad );
}


static ib_api_status_t
__format_drep(
	IN				kcep_t* const				p_cep,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len,
	IN	OUT			mad_cm_drep_t* const		p_drep )
{
	ib_api_status_t		status;

	AL_ENTER( AL_DBG_CM );

	status = __format_drep_mad(
		p_cep->local_comm_id,
		p_cep->remote_comm_id,
		p_pdata,
		pdata_len,
		p_drep );

	/* Store the RTU MAD so we can repeat it if we get a repeated DREP. */
	if( status == IB_SUCCESS && p_drep != &p_cep->mads.drep )
		p_cep->mads.drep = *p_drep;

	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__drep_cep(
	IN				kcep_t* const				p_cep )
{
	cep_agent_t			*p_agt;
	ib_mad_element_t	*p_mad;

	AL_ENTER( AL_DBG_CM );

	if( __cep_get_mad( p_cep, CM_DREP_ATTR_ID, &p_agt, &p_mad ) != IB_SUCCESS )
		return;

	if( __format_drep( p_cep, NULL, 0, (mad_cm_drep_t*)p_mad->p_mad_buf )
		!= IB_SUCCESS )
	{
		return;
	}

	__cep_send_mad( p_agt, p_mad );

	AL_EXIT( AL_DBG_CM );
}


static inline void
__cleanup_mad_list(
	IN				kcep_t* const				p_cep )
{
	ib_mad_element_t	*p_mad;
	kcep_t				*p_new_cep;

	/* Cleanup the pending MAD list. */
	while( p_cep->p_mad_head )
	{
		p_mad = p_cep->p_mad_head;
		p_cep->p_mad_head = p_mad->p_next;
		p_mad->p_next = NULL;
		if( p_mad->send_context1 )
		{
			p_new_cep = (kcep_t*)p_mad->send_context1;

			__unbind_cep( p_new_cep );
			__cleanup_cep( p_new_cep );
		}
		ib_put_mad( p_mad );
	}
}


static void
__cancel_listen(
	IN				kcep_t* const				p_cep )
{
	CL_ASSERT( p_cep->state == CEP_STATE_LISTEN );
	/* Remove from listen map. */
	cl_rbmap_remove_item( &gp_cep_mgr->listen_map, &p_cep->listen_item );

	if( p_cep->p_cmp_buf )
	{
		cl_free( p_cep->p_cmp_buf );
		p_cep->p_cmp_buf = NULL;
	}
}


/* Called with CEP manager lock held. */
static int32_t
__cleanup_cep(
	IN				kcep_t* const				p_cep )
{
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM,
		("[ p_cep = %p (CID = %d)\n", p_cep, p_cep->cid) );

	CL_ASSERT( p_cep );
	CL_ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	/* If we've already come through here, we're done. */
	CL_ASSERT( p_cep->state != CEP_STATE_DESTROY &&
		p_cep->state != CEP_STATE_DREQ_DESTROY );

	/* Cleanup the pending MAD list. */
	__cleanup_mad_list( p_cep );

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Fall through. */
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_REP_MRA_SENT:
		/* Reject the connection. */
		__do_cep_rej( p_cep, IB_REJ_USER_DEFINED, NULL, 0, NULL, 0 );
		break;

	case CEP_STATE_REQ_SENT:
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
		/* Cancel the send. */
		CL_ASSERT( p_cep->h_mad_svc );
		CL_ASSERT( p_cep->p_send_mad );
		ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
		/* Reject the connection. */
		__do_cep_rej( p_cep, IB_REJ_TIMEOUT, (uint8_t*)&p_cep->local_ca_guid,
			sizeof(p_cep->local_ca_guid), NULL, 0 );
		break;

	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
		ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
		/* fall through */
	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
	case CEP_STATE_PRE_APR:
	case CEP_STATE_PRE_APR_MRA_SENT:
		/* Disconnect the connection. */
		if( __dreq_cep( p_cep ) != IB_SUCCESS )
			break;
		/* Fall through. */

	case CEP_STATE_DREQ_SENT:
		ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
		p_cep->state = CEP_STATE_DREQ_DESTROY;
		goto out;

	case CEP_STATE_DREQ_RCVD:
		/* Send the DREP. */
		__drep_cep( p_cep );
		break;

	case CEP_STATE_SREQ_RCVD:
		/* TODO: Reject the SIDR request. */
		break;

	case CEP_STATE_LISTEN:
		__cancel_listen( p_cep );
		break;

	case CEP_STATE_PRE_REQ:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Fall through. */
	case CEP_STATE_IDLE:
		break;

	default:
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("CEP in state %d.\n", p_cep->state) );
	case CEP_STATE_TIMEWAIT:
		/* Already in timewait - so all is good. */
		p_cep->state = CEP_STATE_DESTROY;
		goto out;
	}

	p_cep->state = CEP_STATE_DESTROY;
	__insert_timewait( p_cep );

out:
	AL_EXIT( AL_DBG_CM );
	return cl_atomic_dec( &p_cep->ref_cnt );
}


static void
__destroy_cep(
	IN				kcep_t* const				p_cep )
{
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT(
		p_cep->cid < cl_vector_get_size( &gp_cep_mgr->cid_vector ) );

	CL_ASSERT( p_cep->p_cid == (cep_cid_t*)cl_vector_get_ptr(
		&gp_cep_mgr->cid_vector, p_cep->cid ) );

	/* Free the CID. */
	p_cep->p_cid->p_cep = (kcep_t*)(uintn_t)gp_cep_mgr->free_cid;
	p_cep->p_cid->h_al = NULL;
	gp_cep_mgr->free_cid = p_cep->cid;

	KeCancelTimer( &p_cep->timewait_timer );

	ExFreeToNPagedLookasideList( &gp_cep_mgr->cep_pool, p_cep );

	deref_al_obj( &gp_cep_mgr->obj );

	AL_EXIT( AL_DBG_CM );
}


ib_api_status_t
al_create_cep(
	IN				ib_al_handle_t				h_al,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb,
	IN	OUT			net32_t* const				p_cid )
{
	ib_api_status_t	status;

	AL_ENTER( AL_DBG_CM );
	CL_ASSERT( h_al );

	status = kal_cep_alloc(h_al, p_cid);
	if ( status == IB_SUCCESS )
	{
		status = kal_cep_config(h_al, *p_cid, pfn_cb, context, pfn_destroy_cb);
	}

	AL_EXIT( AL_DBG_CM );
	return status;
}


void
al_destroy_cep(
	IN				ib_al_handle_t				h_al,
	IN	OUT			net32_t* const				p_cid,
	IN				boolean_t					reusable )
{
    net32_t             cid;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	void				*context;
    ib_pfn_destroy_cb_t pfn_destroy_cb;
	int32_t				ref_cnt;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM,("[ CID = %d\n", *p_cid) );

	CL_ASSERT( h_al );

	/*
	 * Remove the CEP from the CID vector - no further API calls
	 * will succeed for it.
	 */
	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	cid = *p_cid;
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		/* Invalid handle. */
        if( !reusable )
            *p_cid = AL_RESERVED_CID;

		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return;
	}

	context = p_cep->context;
	pfn_destroy_cb = p_cep->pfn_destroy_cb;

	/* Cancel any queued IRP */
	__cep_complete_irp( p_cep, STATUS_CANCELLED, IO_NO_INCREMENT );

	__unbind_cep( p_cep );
	ref_cnt = __cleanup_cep( p_cep );
    if( reusable )
        *p_cid = AL_INVALID_CID;
    else
        *p_cid = AL_RESERVED_CID;

	KeReleaseInStackQueuedSpinLock( &hdl );

	if( !ref_cnt && pfn_destroy_cb )
		pfn_destroy_cb( context );

	AL_PRINT(TRACE_LEVEL_INFORMATION ,AL_DBG_CM ,
		("Destroyed CEP with cid %d, h_al %p, context %p \n", 
		cid, h_al, context ));

	AL_EXIT( AL_DBG_CM );
}

void
kal_cep_destroy(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				NTSTATUS					status )
{
	KLOCK_QUEUE_HANDLE	hdl;
	kcep_t				*p_cep;
    ib_pfn_destroy_cb_t pfn_destroy_cb;
	void				*context;
	int32_t				ref_cnt;

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	CL_ASSERT( p_cep );

	context = p_cep->context;
	pfn_destroy_cb = p_cep->pfn_destroy_cb;

	__unbind_cep( p_cep );
	/* Drop new REQs so they can be retried when resources may be available */
	if( status == STATUS_NO_MORE_ENTRIES &&
		(p_cep->state == CEP_STATE_REQ_RCVD ||
		 p_cep->state == CEP_STATE_REQ_MRA_SENT) )
	{
		p_cep->state = CEP_STATE_IDLE;
	}
	ref_cnt = __cleanup_cep( p_cep );
	KeReleaseInStackQueuedSpinLock( &hdl );

	if( !ref_cnt && pfn_destroy_cb )
		pfn_destroy_cb( context );
}


ib_api_status_t
kal_cep_cancel_listen(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	KLOCK_QUEUE_HANDLE	hdl;
	kcep_t				*p_cep;

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( p_cep == NULL )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		return IB_INVALID_HANDLE;
	}

	__cleanup_mad_list( p_cep );
	__cancel_listen( p_cep );
	p_cep->state = CEP_STATE_IDLE;

	KeReleaseInStackQueuedSpinLock( &hdl );
	return STATUS_SUCCESS;
}


ib_api_status_t
al_cep_listen(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_cep_listen_t* const		p_listen_info )
{
	ib_api_status_t		status;
	kcep_t				*p_cep, *p_listen;
	cl_rbmap_item_t		*p_item, *p_insert_at;
	boolean_t			left = TRUE;
	intn_t				cmp;
	KLOCK_QUEUE_HANDLE	hdl;
	ib_cm_rdma_req_t*	p_rdma_req = (ib_cm_rdma_req_t*)p_listen_info->p_cmp_buf;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_listen_info );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REQ:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Must change state here in case listen fails */
		p_cep->state = CEP_STATE_IDLE;
		/* Fall through. */
	case CEP_STATE_IDLE:
		break;
	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		goto done;
	}

	if( ib_cm_is_rdma_cm_sid(p_listen_info->svc_id) && p_rdma_req != NULL )
	{
		if( p_listen_info->cmp_len < FIELD_OFFSET(ib_cm_rdma_req_t, pdata) )
		{
			status = IB_INVALID_SETTING;
			goto done;
		}

		if( ib_cm_is_rdma_cm_req_valid(p_rdma_req) == FALSE )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
				("RDMA CM listen is invalid: maj_min_ver %d, ipv %#x \n", 
				p_rdma_req->maj_min_ver, p_rdma_req->ipv ) );
			status = IB_INVALID_SETTING;
			goto done;
		}
	}

	/* Insert the CEP into the listen map. */
	p_item = cl_rbmap_root( &gp_cep_mgr->listen_map );
	p_insert_at = p_item;
	while( p_item != cl_rbmap_end( &gp_cep_mgr->listen_map ) )
	{
		p_insert_at = p_item;

		p_listen = PARENT_STRUCT( p_item, kcep_t, listen_item );

		if( p_listen_info->svc_id == p_listen->sid )
			goto port_cmp;
		
		if( p_listen_info->svc_id < p_listen->sid )
			p_item = cl_rbmap_left( p_item ), left = TRUE;
		else
			p_item = cl_rbmap_right( p_item ), left = FALSE;

		continue;

port_cmp:
		if( p_listen_info->port_guid != IB_ALL_PORTS &&
			p_listen->port_guid != IB_ALL_PORTS )
		{
			if( p_listen_info->port_guid == p_listen->port_guid )
				goto pdata_cmp;
			
			if( p_listen_info->port_guid < p_listen->port_guid )
				p_item = cl_rbmap_left( p_item ), left = TRUE;
			else
				p_item = cl_rbmap_right( p_item ), left = FALSE;

			continue;
		}

pdata_cmp:
		/*
		 * If an existing listen doesn't have a compare buffer,
		 * then we found a duplicate.
		 */
		if( !p_listen->p_cmp_buf || !p_listen_info->p_cmp_buf )
			break;

		if( p_listen_info->p_cmp_buf )
		{
			/* Compare length must match. */
			if( p_listen_info->cmp_len != p_listen->cmp_len )
				break;

			/* Compare offset (or mask for RDMA CM) must match. */
			if( p_listen_info->cmp_offset != p_listen->cmp_offset )
				break;

			if( ib_cm_is_rdma_cm_sid(p_listen_info->svc_id) )
			{
				cmp = __cm_rdma_req_cmp(
					p_listen->cmp_offset,
					p_rdma_req,
					(ib_cm_rdma_req_t*)p_listen->p_cmp_buf
					);
			}
			else
			{
				cmp = cl_memcmp( &p_listen_info->p_cmp_buf,
					p_listen->p_cmp_buf, p_listen->cmp_len );
			}

			if( cmp < 0 )
				p_item = cl_rbmap_left( p_item ), left = TRUE;
			else if( cmp > 0 )
				p_item = cl_rbmap_right( p_item ), left = FALSE;
			else
				break;

			AL_PRINT( TRACE_LEVEL_INFORMATION, AL_DBG_CM,
				("Svc ID match but compare buffer mismatch.\n") );
			continue;
		}
	}

	if( p_item != cl_rbmap_end( &gp_cep_mgr->listen_map ) )
	{
		/* Duplicate!!! */
		status = IB_INVALID_SETTING;
		goto done;
	}

	/* Set up the CEP. */
	if( p_listen_info->p_cmp_buf )
	{
		p_cep->p_cmp_buf = cl_malloc( p_listen_info->cmp_len );
		if( !p_cep->p_cmp_buf )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Failed to allocate compare buffer.\n") );
			status = IB_INSUFFICIENT_MEMORY;
			goto done;
		}

		cl_memcpy( p_cep->p_cmp_buf,
			p_listen_info->p_cmp_buf, p_listen_info->cmp_len );
	}
	p_cep->cmp_len = p_listen_info->cmp_len;
	p_cep->cmp_offset = p_listen_info->cmp_offset;
	p_cep->sid = p_listen_info->svc_id;
	p_cep->port_guid = p_listen_info->port_guid;
	p_cep->state = CEP_STATE_LISTEN;

	cl_rbmap_insert( &gp_cep_mgr->listen_map, p_insert_at,
		&p_cep->listen_item, left );

	status = IB_SUCCESS;

done:
	KeReleaseInStackQueuedSpinLock( &hdl );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static cep_agent_t*
__format_path_av(
		kcep_t				*p_cep,
		IN		const	ib_path_rec_t* const		p_path,
		OUT			kcep_av_t* const			p_av )
{
	cep_agent_t*		p_port_cep;
	int force_grh;
	int is_rdma_port;
	uint8_t	sl;
	
	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_path );
	CL_ASSERT( p_av );

	cl_memclr( p_av, sizeof(kcep_av_t) );

	p_port_cep = __find_port_cep( &p_path->sgid, p_path->slid,
		p_path->pkey, &p_av->pkey_index );
	if( !p_port_cep )
	{
		AL_EXIT( AL_DBG_CM );
		return NULL;
	}

	p_av->port_guid = p_port_cep->port_guid;

	p_av->attr.port_num = p_port_cep->port_num;

	is_rdma_port = p_port_cep->h_ca->obj.p_ci_ca->verbs.rdma_port_get_transport(
		p_port_cep->h_ca->obj.p_ci_ca->h_ci_ca, p_port_cep->port_num) == RDMA_TRANSPORT_RDMAOE;

	sl = p_port_cep->h_ca->obj.p_ci_ca->verbs.get_sl_for_ip_port(
			p_port_cep->h_ca->obj.p_ci_ca->h_ci_ca, p_port_cep->port_num, ib_cm_rdma_sid_port(p_cep->sid));

	if(! is_rdma_port || sl == (uint8_t) -1)
	{
		p_av->attr.sl = ib_path_rec_sl( p_path );
		p_av->flags = 0;
	}
	else
	{
		p_av->attr.sl = sl;
		p_av->flags = CEP_AV_FLAG_GENERATE_PRIO_TAG;
	}
	p_av->attr.dlid = p_path->dlid;

	p_av->attr.grh.ver_class_flow = ib_grh_set_ver_class_flow(
		1, p_path->tclass, ib_path_rec_flow_lbl( p_path ) );
	p_av->attr.grh.hop_limit = ib_path_rec_hop_limit( p_path );
	p_av->attr.grh.src_gid = p_path->sgid;
	p_av->attr.grh.dest_gid = p_path->dgid;

	force_grh = is_rdma_port;
	p_av->attr.grh_valid = force_grh || (!ib_gid_is_link_local( &p_path->dgid )) ||
		       (ib_path_rec_hop_limit(p_path) > 1);

	p_av->attr.static_rate = ib_path_rec_rate( p_path );
	p_av->attr.path_bits = (uint8_t)(p_path->slid - p_port_cep->base_lid);

	/*
	 * Note that while we never use the connected AV attributes internally,
	 * we store them so we can pass them back to users.
	 */
	p_av->attr.conn.path_mtu = ib_path_rec_mtu( p_path );
	p_av->attr.conn.local_ack_timeout = calc_lcl_ack_timeout(
		ib_path_rec_pkt_life( p_path ) + 1, 0 );

	AL_EXIT( AL_DBG_CM );
	return p_port_cep;
}


/*
 * Formats a REQ mad's path information given a path record.
 */
static void
__format_req_path(
	IN		const	ib_path_rec_t* const		p_path,
	IN		const	uint8_t						ack_delay,
	IN				int							force_grh,
		OUT			req_path_info_t* const		p_req_path )
{
	AL_ENTER( AL_DBG_CM );

	p_req_path->local_lid = p_path->slid;
	p_req_path->remote_lid = p_path->dlid;
	p_req_path->local_gid = p_path->sgid;
	p_req_path->remote_gid = p_path->dgid;

	conn_req_path_set_flow_lbl( ib_path_rec_flow_lbl( p_path ),
		p_req_path );
	conn_req_path_set_pkt_rate( ib_path_rec_rate( p_path ),
		p_req_path );

	/* Traffic class & hop limit */
	p_req_path->traffic_class = p_path->tclass;
	p_req_path->hop_limit = ib_path_rec_hop_limit( p_path );

	/* SL & Subnet Local fields */
	conn_req_path_set_svc_lvl( ib_path_rec_sl( p_path ),
		p_req_path );
	conn_req_path_set_subn_lcl(
		!force_grh && ib_gid_is_link_local( &p_path->dgid ), p_req_path );

	conn_req_path_set_lcl_ack_timeout(
		calc_lcl_ack_timeout( ib_path_rec_pkt_life( p_path ) + 1,
		ack_delay ), p_req_path );

	conn_req_path_clr_rsvd_fields( p_req_path );

	AL_EXIT( AL_DBG_CM );
}


static ib_api_status_t
__format_req(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN		const	iba_cm_req* const			p_cm_req )
{
	ib_api_status_t	status;
	mad_cm_req_t*	p_req;
	int force_grh;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cep );
	CL_ASSERT( p_cm_req );
	CL_ASSERT( p_cep->p_mad );

	/* Format the MAD header. */
	__format_mad_hdr( p_cep->p_mad->p_mad_buf, p_cep, CM_REQ_ATTR_ID );

	/* Set the addressing information in the MAD. */
	__format_mad_av( p_cep->p_mad, &p_cep->av[p_cep->idx_primary] );

	p_req = (mad_cm_req_t*)p_cep->p_mad->p_mad_buf;

	ci_ca_lock_attr( p_port_cep->h_ca->obj.p_ci_ca );
	/*
	 * Store the local CA's ack timeout for use when computing
	 * the local ACK timeout.
	 */
	p_cep->local_ack_delay =
		p_port_cep->h_ca->obj.p_ci_ca->p_pnp_attr->local_ack_delay;
	ci_ca_unlock_attr( p_port_cep->h_ca->obj.p_ci_ca );

	/* Format the primary path. */
	force_grh = p_port_cep->h_ca->obj.p_ci_ca->verbs.rdma_port_get_transport(p_port_cep->h_ca->obj.p_ci_ca->h_ci_ca, p_port_cep->port_num) == RDMA_TRANSPORT_RDMAOE;
	__format_req_path( p_cm_req->p_primary_path,
		p_cep->local_ack_delay, force_grh, &p_req->primary_path );

	if( p_cm_req->p_alt_path )
	{
		/* Format the alternate path. */
		__format_req_path( p_cm_req->p_alt_path,
			p_cep->local_ack_delay, force_grh, &p_req->alternate_path );
	}
	else
	{
		cl_memclr( &p_req->alternate_path, sizeof(req_path_info_t) );
	}

	/* Set the local communication in the REQ. */
	p_req->local_comm_id = p_cep->local_comm_id;
	p_req->sid = p_cm_req->service_id;
	p_req->local_ca_guid = p_port_cep->h_ca->obj.p_ci_ca->verbs.guid;

	conn_req_set_lcl_qpn( p_cep->local_qpn, p_req );
	conn_req_set_resp_res( p_cm_req->resp_res, p_req );
	conn_req_set_init_depth( p_cm_req->init_depth, p_req );
	conn_req_set_remote_resp_timeout( p_cm_req->remote_resp_timeout, p_req );
	conn_req_set_qp_type( p_cm_req->qp_type, p_req );
	conn_req_set_flow_ctrl( p_cm_req->flow_ctrl, p_req );
	conn_req_set_starting_psn( p_cm_req->starting_psn, p_req );

	conn_req_set_lcl_resp_timeout( p_cm_req->local_resp_timeout, p_req );
	conn_req_set_retry_cnt( p_cm_req->retry_cnt, p_req );

	p_req->pkey = p_cm_req->p_primary_path->pkey;

	conn_req_set_mtu( ib_path_rec_mtu( p_cm_req->p_primary_path ), p_req );
	conn_req_set_rnr_retry_cnt( p_cm_req->rnr_retry_cnt, p_req );

	conn_req_set_max_cm_retries( p_cm_req->max_cm_retries, p_req );
	status = conn_req_set_pdata(
		p_cm_req->p_pdata, p_cm_req->pdata_len, p_req );

	conn_req_clr_rsvd_fields( p_req );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__save_user_req(
	IN				kcep_t* const				p_cep,
	IN		const	iba_cm_req* const			p_cm_req,
	IN				uint8_t						rnr_nak_timeout,
		OUT			cep_agent_t** const			pp_port_cep )
{
	cep_agent_t		*p_port_cep;

	AL_ENTER( AL_DBG_CM );

	if( !p_cm_req->p_primary_path )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid primary path record.\n") );
		return IB_INVALID_SETTING;
	}

	p_cep->sid = p_cm_req->service_id;
	p_cep->idx_primary = 0;
	p_cep->p2p = FALSE;
	p_cep->p_cmp_buf = NULL;
	p_cep->cmp_len = 0;
	p_cep->cmp_offset = 0;
	p_cep->was_active = TRUE;

	/* Validate the primary path. */
	p_port_cep = __format_path_av( p_cep, p_cm_req->p_primary_path, &p_cep->av[0] );
	if( !p_port_cep )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Primary path unrealizable.\n") );
		return IB_INVALID_SETTING;
	}

	p_cep->av[0].attr.conn.seq_err_retry_cnt = p_cm_req->retry_cnt;

	p_cep->local_ca_guid = p_port_cep->h_ca->obj.p_ci_ca->verbs.guid;

	*pp_port_cep = p_port_cep;

	/*
	 * Store the PKEY so we can ensure that alternate paths are
	 * on the same partition.
	 */
	p_cep->pkey = p_cm_req->p_primary_path->pkey;
	
	p_cep->max_2pkt_life = ib_path_rec_pkt_life( p_cm_req->p_primary_path ) + 1;

	if( p_cm_req->p_alt_path )
	{
		/* MTUs must match since they are specified only once. */
		if( ib_path_rec_mtu( p_cm_req->p_primary_path ) !=
			ib_path_rec_mtu( p_cm_req->p_alt_path ) )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Mismatched primary and alternate path MTUs.\n") );
			return IB_INVALID_SETTING;
		}

		/* The PKEY must match too. */
		if( p_cm_req->p_alt_path->pkey != p_cm_req->p_primary_path->pkey )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Mismatched pimary and alternate PKEYs.\n") );
			return IB_INVALID_SETTING;
		}

		p_port_cep =
			__format_path_av( p_cep, p_cm_req->p_alt_path, &p_cep->av[1] );
		if( p_port_cep &&
			p_port_cep->h_ca->obj.p_ci_ca->verbs.guid != p_cep->local_ca_guid )
		{
			/* Alternate path is not on same CA. */
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Alternate path unrealizable.\n") );
			return IB_INVALID_SETTING;
		}

		p_cep->av[1].attr.conn.seq_err_retry_cnt = p_cm_req->retry_cnt;

		p_cep->max_2pkt_life = max( p_cep->max_2pkt_life,
			(ib_path_rec_pkt_life( p_cm_req->p_alt_path ) + 1) );
	}
	else
	{
		cl_memclr( &p_cep->av[1], sizeof(kcep_av_t) );
	}

	p_cep->p_cid->modifier++;
	/*
	 * We don't ever want a modifier of zero for the CID at index zero
	 * since it would result in a total CID of zero.
	 */
	if( !p_cep->cid && !p_cep->p_cid->modifier )
		p_cep->p_cid->modifier++;

	/* Store pertinent information in the connection. */
	p_cep->local_comm_id = p_cep->cid | (p_cep->p_cid->modifier << 24);
	p_cep->remote_comm_id = 0;

	/* Cache the local QPN. */
	p_cep->local_qpn = p_cm_req->qpn;
	p_cep->remote_ca_guid = 0;
	p_cep->remote_qpn = 0;

	/* Retry timeout is remote CM response timeout plus 2 * packet life. */
	p_cep->retry_timeout = __calc_mad_timeout( p_cep->max_2pkt_life ) +
		__calc_mad_timeout( p_cm_req->remote_resp_timeout );
		

	/* Store the retry count. */
	p_cep->max_cm_retries = p_cm_req->max_cm_retries;

	/*
	 * Clear the maximum packet lifetime, used to calculate timewait.
	 * It will be set when we transition into the established state.
	 */
	p_cep->timewait_time.QuadPart = 0;

	p_cep->rq_psn = p_cm_req->starting_psn;

	p_cep->rnr_nak_timeout = rnr_nak_timeout;

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
kal_cep_pre_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	iba_cm_req* const			p_cm_req,
	IN				uint8_t						rnr_nak_timeout,
	IN	OUT			ib_qp_mod_t* const			p_init OPTIONAL )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_cm_req );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REQ:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Fall through. */
	case CEP_STATE_IDLE:
		status = __save_user_req( p_cep, p_cm_req, rnr_nak_timeout, &p_port_cep );
		if( status != IB_SUCCESS )
			break;

		status =
			ib_get_mad( p_port_cep->pool_key, MAD_BLOCK_SIZE, &p_cep->p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_req( p_port_cep, p_cep, p_cm_req );
		if( status != IB_SUCCESS )
		{
			AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Invalid pdata length.\n") );
			ib_put_mad( p_cep->p_mad );
			p_cep->p_mad = NULL;
			break;
		}

		/* Format the INIT qp modify attributes. */
		if( p_init )
		{
			p_init->req_state = IB_QPS_INIT;
			p_init->state.init.primary_port =
				p_cep->av[p_cep->idx_primary].attr.port_num;
			p_init->state.init.qkey = 0;
			p_init->state.init.pkey_index =
				p_cep->av[p_cep->idx_primary].pkey_index;
			p_init->state.init.access_ctrl = IB_AC_LOCAL_WRITE;
		}
		p_cep->state = CEP_STATE_PRE_REQ;
		break;

	case CEP_STATE_TIMEWAIT:
		status = IB_QP_IN_TIMEWAIT;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}

ib_api_status_t
al_cep_pre_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_req_t* const			p_cm_req,
		OUT			ib_qp_mod_t* const			p_init )
{
	iba_cm_req req;

	RtlZeroMemory(&req, sizeof req);
	req.service_id = p_cm_req->svc_id;

	req.p_primary_path = p_cm_req->p_primary_path;
	req.p_alt_path = p_cm_req->p_alt_path;

	req.qpn = p_cm_req->h_qp->num;
	req.qp_type = p_cm_req->qp_type;
	req.starting_psn = req.qpn;

	req.p_pdata = (void *) p_cm_req->p_req_pdata;
	req.pdata_len = p_cm_req->req_length;

	req.max_cm_retries = p_cm_req->max_cm_retries;
	req.resp_res = p_cm_req->resp_res;
	req.init_depth = p_cm_req->init_depth;
	req.remote_resp_timeout = p_cm_req->remote_resp_timeout;
	req.flow_ctrl = (uint8_t) p_cm_req->flow_ctrl;
	req.local_resp_timeout = p_cm_req->local_resp_timeout;
	req.rnr_retry_cnt = p_cm_req->rnr_retry_cnt;
	req.retry_cnt = p_cm_req->retry_cnt;
	req.srq = (uint8_t) (p_cm_req->h_qp->h_srq != NULL);

	return kal_cep_pre_req(h_al, cid, &req, p_cm_req->rnr_nak_timeout, p_init);
}

ib_api_status_t
al_cep_send_req(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REQ:
		CL_ASSERT( p_cep->p_mad );
		p_port_cep = __get_cep_agent( p_cep );
		if( !p_port_cep )
		{
			ib_put_mad( p_cep->p_mad );
			p_cep->state = CEP_STATE_IDLE;
			status = IB_INVALID_SETTING;
		}
		else
		{
			status = __cep_send_retry( p_port_cep, p_cep, p_cep->p_mad );

			if( status == IB_SUCCESS )
				p_cep->state = CEP_STATE_REQ_SENT;
			else
				p_cep->state = CEP_STATE_IDLE;
		}
		p_cep->p_mad = NULL;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__save_user_rep(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN		const	iba_cm_rep* const			p_cm_rep,
	IN				uint8_t						rnr_nak_timeout )
{
	AL_ENTER( AL_DBG_CM );

	p_cep->local_qpn = p_cm_rep->qpn;
	p_cep->rq_psn = p_cm_rep->starting_psn;
	p_cep->init_depth = p_cm_rep->init_depth;

	//
	// set responder resources, claimed by Accept
	//
	// The following code was absent , so there can be applications
	// that do not set 'p_cm_rep->resp_res' and work OK
	//
	// For catch then in debug we set ASSERT
	// For not to break them we added 'if' before assigning
	//ASSERT( p_cm_rep->resp_res );
	if ( p_cm_rep->resp_res )
		p_cep->resp_res = p_cm_rep->resp_res;

	ci_ca_lock_attr( p_port_cep->h_ca->obj.p_ci_ca );
	/* Check the CA's responder resource max and trim if necessary. */
	if( p_port_cep->h_ca->obj.p_ci_ca->p_pnp_attr->max_qp_resp_res <
		p_cep->resp_res )
	{
		/*
		 * The CA cannot handle the requested responder resources.
		 * Set the response to the CA's maximum.
		 */
		p_cep->resp_res = 
			p_port_cep->h_ca->obj.p_ci_ca->p_pnp_attr->max_qp_resp_res;
	}
	ci_ca_unlock_attr( p_port_cep->h_ca->obj.p_ci_ca );

	p_cep->rnr_nak_timeout = rnr_nak_timeout;

	AL_EXIT( AL_DBG_CM );
}


static ib_api_status_t
__format_rep(
	IN				cep_agent_t* const			p_port_cep,
	IN				kcep_t* const				p_cep,
	IN		const	iba_cm_rep* const			p_cm_rep )
{
	ib_api_status_t		status;
	mad_cm_rep_t		*p_rep;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cep );
	CL_ASSERT( p_cm_rep );
	CL_ASSERT( p_cep->p_mad );

	/* Format the MAD header. */
	__format_mad_hdr( p_cep->p_mad->p_mad_buf, p_cep, CM_REP_ATTR_ID );

	/* Set the addressing information in the MAD. */
	__format_mad_av( p_cep->p_mad, &p_cep->av[p_cep->idx_primary] );

	p_rep = (mad_cm_rep_t*)p_cep->p_mad->p_mad_buf;

	p_rep->local_comm_id = p_cep->local_comm_id;
	p_rep->remote_comm_id = p_cep->remote_comm_id;
	conn_rep_set_lcl_qpn( p_cep->local_qpn, p_rep );
	conn_rep_set_starting_psn( p_cep->rq_psn, p_rep );

	if( p_cm_rep->failover_accepted != IB_FAILOVER_ACCEPT_SUCCESS )
	{
		/*
		 * Failover rejected - clear the alternate AV information.
		 * Note that at this point, the alternate is always at index 1.
		 */
		cl_memclr( &p_cep->av[1], sizeof(kcep_av_t) );
	}
	else if( !p_cep->av[1].port_guid )
	{
		/*
		 * Always reject alternate path if it's zero.  We might
		 * have cleared the AV because it was unrealizable when
		 * processing the REQ.
		 */
		conn_rep_set_failover( IB_FAILOVER_ACCEPT_ERROR, p_rep );
	}
	else
	{
		conn_rep_set_failover( p_cm_rep->failover_accepted, p_rep );
	}

	p_rep->resp_resources = p_cep->resp_res;

	ci_ca_lock_attr( p_port_cep->h_ca->obj.p_ci_ca );
	conn_rep_set_target_ack_delay(
		p_port_cep->h_ca->obj.p_ci_ca->p_pnp_attr->local_ack_delay, p_rep );
	ci_ca_unlock_attr( p_port_cep->h_ca->obj.p_ci_ca );

	p_rep->initiator_depth = p_cep->init_depth;

	conn_rep_set_e2e_flow_ctl( p_cm_rep->flow_ctrl, p_rep );

	conn_rep_set_rnr_retry_cnt(
		(uint8_t)(p_cm_rep->rnr_retry_cnt & 0x07), p_rep );

	/* Local CA guid should have been set when processing the received REQ. */
	CL_ASSERT( p_cep->local_ca_guid );
	p_rep->local_ca_guid = p_cep->local_ca_guid;

	status = conn_rep_set_pdata(
		p_cm_rep->p_pdata, p_cm_rep->pdata_len, p_rep );

	conn_rep_clr_rsvd_fields( p_rep );

	AL_EXIT( AL_DBG_CM );
	return status;
}


static void
__al_cep_fill_init_attr(
	__in kcep_t *p_cep,
	__out ib_qp_mod_t* p_init
	)
{
	/* Format the INIT qp modify attributes. */
	cl_memclr(p_init, sizeof(*p_init));
	p_init->req_state = IB_QPS_INIT;
	p_init->state.init.primary_port =
		p_cep->av[p_cep->idx_primary].attr.port_num;
	p_init->state.init.qkey = 0;
	p_init->state.init.pkey_index =
		p_cep->av[p_cep->idx_primary].pkey_index;
	p_init->state.init.access_ctrl = IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE;
	if ( p_cep->resp_res )
	{
		p_init->state.init.access_ctrl |= IB_AC_RDMA_READ | IB_AC_ATOMIC;
	}
}


static ib_api_status_t
__al_cep_pre_rep(
	IN				kcep_t						*p_cep,
	IN		const	iba_cm_rep* const			p_cm_rep,
	IN				uint8_t						rnr_nak_timeout,
		OUT			ib_qp_mod_t* const			p_init OPTIONAL )
{
	ib_api_status_t		status;
	cep_agent_t			*p_port_cep;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( p_cm_rep );

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Fall through. */
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
		CL_ASSERT( !p_cep->p_mad );
		status =
			__cep_get_mad( p_cep, CM_REP_ATTR_ID, &p_port_cep, &p_cep->p_mad );
		if( status != IB_SUCCESS )
			break;

		__save_user_rep( p_port_cep, p_cep, p_cm_rep, rnr_nak_timeout );

		status = __format_rep( p_port_cep, p_cep, p_cm_rep );
		if( status != IB_SUCCESS )
		{
			ib_put_mad( p_cep->p_mad );
			p_cep->p_mad = NULL;
			break;
		}

		/* Format the INIT qp modify attributes. */
		if( p_init )
		{
			__al_cep_fill_init_attr(p_cep, p_init);
		}

		/* Just OR in the PREP bit into the state. */
		p_cep->state |= CEP_STATE_PREP;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_pre_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				void*						context,
	IN				ib_pfn_destroy_cb_t			pfn_destroy_cb,
	IN		const	ib_cm_rep_t* const			p_cm_rep,
	IN	OUT			net32_t* const				p_cid,
		OUT			ib_qp_mod_t* const			p_init )
{
	kcep_t				*p_cep;
	iba_cm_rep			rep;
	KLOCK_QUEUE_HANDLE	hdl;
	ib_api_status_t		status;

	RtlZeroMemory(&rep, sizeof rep);
	rep.qpn = p_cm_rep->h_qp->num;
	rep.starting_psn = rep.qpn;

	rep.p_pdata = (void *) p_cm_rep->p_rep_pdata;
	rep.pdata_len = p_cm_rep->rep_length;

	rep.failover_accepted = p_cm_rep->failover_accepted;
	rep.init_depth = p_cm_rep->init_depth;
	rep.flow_ctrl = (uint8_t) p_cm_rep->flow_ctrl;
	rep.rnr_retry_cnt = p_cm_rep->rnr_retry_cnt;
	rep.srq = (uint8_t) (p_cm_rep->h_qp->h_srq != NULL);

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if (!p_cep )
	{
		status = IB_INVALID_HANDLE;
		goto out;
	}

	rep.resp_res = p_cep->resp_res;
	status = __al_cep_pre_rep( p_cep, &rep, p_cm_rep->rnr_nak_timeout, p_init );
	if ( status == IB_SUCCESS )
	{
		p_cep->context = context;
		p_cep->pfn_destroy_cb = pfn_destroy_cb;
		*p_cid = cid;
	}

out:
	KeReleaseInStackQueuedSpinLock( &hdl );
	return status;
}


ib_api_status_t
kal_cep_pre_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	iba_cm_rep* const			p_cm_rep,
	IN				uint8_t						rnr_nak_timeout,
		OUT			ib_qp_mod_t* const			p_init OPTIONAL )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_cm_rep );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		status = IB_INVALID_HANDLE;
		goto out;
	}

	status = __al_cep_pre_rep( p_cep, p_cm_rep, rnr_nak_timeout, p_init );

out:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}

ib_api_status_t
al_cep_send_rep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		status = IB_INVALID_HANDLE;
		goto out;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
		CL_ASSERT( p_cep->p_mad );
		p_port_cep = __get_cep_agent( p_cep );
		if( !p_port_cep )
		{
			// Why call ib_put_mad() here but not below?
			ib_put_mad( p_cep->p_mad );
			// Why call __remove_cep() below but not here?
			p_cep->state = CEP_STATE_IDLE;
			status = IB_INSUFFICIENT_RESOURCES;
		}
		else
		{
			status = __cep_send_retry( p_port_cep, p_cep, p_cep->p_mad );
			if( status == IB_SUCCESS )
			{
				p_cep->state = CEP_STATE_REP_SENT;
			}
			else
			{
				__remove_cep( p_cep );
				p_cep->state = CEP_STATE_IDLE;
			}
		}
		p_cep->p_mad = NULL;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}
out:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


static inline ib_api_status_t
__format_rtu(
	IN				kcep_t* const				p_cep, 
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len,
	IN	OUT			ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_rtu_t		*p_rtu;

	AL_ENTER( AL_DBG_CM );

	p_rtu = (mad_cm_rtu_t*)p_mad->p_mad_buf;

	p_rtu->local_comm_id = p_cep->local_comm_id;
	p_rtu->remote_comm_id = p_cep->remote_comm_id;

	/* copy optional data */
	status = conn_rtu_set_pdata( p_pdata, pdata_len, p_rtu );

	/* Store the RTU MAD so we can repeat it if we get a repeated REP. */
	if( status == IB_SUCCESS )
		p_cep->mads.rtu = *p_rtu;

	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_rtu(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t*					p_pdata OPTIONAL,
	IN				uint8_t						pdata_len )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
		status = __cep_get_mad( p_cep, CM_RTU_ATTR_ID, &p_port_cep, &p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_rtu( p_cep, p_pdata, pdata_len, p_mad );
		if( status != IB_SUCCESS )
		{
			ib_put_mad( p_mad );
			break;
		}

		/* Update the timewait time. */
		__calc_timewait( p_cep );

		p_cep->state = CEP_STATE_ESTABLISHED;

		__cep_send_mad( p_port_cep, p_mad );
		/* Send failures will get another chance if we receive a repeated REP. */
		status = IB_SUCCESS;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_rej(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_rej_status_t				rej_status,
	IN		const	uint8_t* const				p_ari,
	IN				uint8_t						ari_len,
	IN		const	uint8_t* const				p_pdata,
	IN				uint8_t						pdata_len )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
		status = __do_cep_rej(
			p_cep, rej_status, p_ari, ari_len, p_pdata, pdata_len );
		__remove_cep( p_cep );
		p_cep->state = CEP_STATE_IDLE;
		break;

	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
		status = __do_cep_rej(
			p_cep, rej_status, p_ari, ari_len, p_pdata, pdata_len );
		p_cep->state = CEP_STATE_TIMEWAIT;
		__insert_timewait( p_cep );
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
	}

	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__format_mra(
	IN				kcep_t* const				p_cep,
	IN		const	uint8_t						msg_mraed,
	IN		const	ib_cm_mra_t* const			p_cm_mra,
	IN	OUT			ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_mra_t		*p_mra;

	AL_ENTER( AL_DBG_CM );

	p_mra = (mad_cm_mra_t*)p_mad->p_mad_buf;

	conn_mra_set_msg_mraed( msg_mraed, p_mra );

	p_mra->local_comm_id = p_cep->local_comm_id;
	p_mra->remote_comm_id = p_cep->remote_comm_id;

	conn_mra_set_svc_timeout( p_cm_mra->svc_timeout, p_mra );
	status = conn_mra_set_pdata(
		p_cm_mra->p_mra_pdata, p_cm_mra->mra_length, p_mra );
	if( status != IB_SUCCESS )
	{
		AL_EXIT( AL_DBG_CM );
		return status;
	}
	conn_mra_clr_rsvd_fields( p_mra );

	/* Save the MRA so we can repeat it if we get a repeated message. */
	p_cep->mads.mra = *p_mra;

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
al_cep_mra(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_mra_t* const			p_cm_mra )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;
	uint8_t				msg_mraed;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_cm_mra );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_PRE_REP:
		msg_mraed = 0;
		break;

	case CEP_STATE_REP_RCVD:
		msg_mraed = 1;
		break;

	case CEP_STATE_PRE_APR:
	case CEP_STATE_LAP_RCVD:
		msg_mraed = 2;
		break;

	default:
		status = IB_INVALID_STATE;
		goto done;
	}

	status = __cep_get_mad( p_cep, CM_MRA_ATTR_ID, &p_port_cep, &p_mad );
	if( status != IB_SUCCESS )
		goto done;

	status = __format_mra( p_cep, msg_mraed, p_cm_mra, p_mad );
	if( status != IB_SUCCESS )
	{
		ib_put_mad( p_mad );
		goto done;
	}

	p_cep->state |= CEP_STATE_MRA;

	__cep_send_mad( p_port_cep, p_mad );
	status = IB_SUCCESS;

done:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}



static ib_api_status_t
__format_lap(
	IN				kcep_t* const				p_cep,
	IN		const	ib_cm_lap_t* const			p_cm_lap,
	IN	OUT			ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_lap_t		*p_lap;

	AL_ENTER( AL_DBG_CM );

	__format_mad_hdr( p_mad->p_mad_buf, p_cep, CM_LAP_ATTR_ID );

	__format_mad_av( p_mad, &p_cep->av[((p_cep->idx_primary + 1) & 0x1)] );

	p_lap = (mad_cm_lap_t*)p_mad->p_mad_buf;

	p_lap->alternate_path.local_lid = p_cm_lap->p_alt_path->slid;
	p_lap->alternate_path.remote_lid = p_cm_lap->p_alt_path->dlid;
	p_lap->alternate_path.local_gid = p_cm_lap->p_alt_path->sgid;
	p_lap->alternate_path.remote_gid = p_cm_lap->p_alt_path->dgid;

	/* Set Flow Label and Packet Rate */
	conn_lap_path_set_flow_lbl(
		ib_path_rec_flow_lbl( p_cm_lap->p_alt_path ), &p_lap->alternate_path );
	conn_lap_path_set_tclass(
		p_cm_lap->p_alt_path->tclass, &p_lap->alternate_path );

	p_lap->alternate_path.hop_limit =
		ib_path_rec_hop_limit( p_cm_lap->p_alt_path );
	conn_lap_path_set_pkt_rate(
		ib_path_rec_rate( p_cm_lap->p_alt_path ), &p_lap->alternate_path );

	/* Set SL and Subnet Local */
	conn_lap_path_set_svc_lvl(
		ib_path_rec_sl( p_cm_lap->p_alt_path ), &p_lap->alternate_path );
	conn_lap_path_set_subn_lcl(
		ib_gid_is_link_local( &p_cm_lap->p_alt_path->dgid ),
		&p_lap->alternate_path );

	conn_lap_path_set_lcl_ack_timeout(
		calc_lcl_ack_timeout( ib_path_rec_pkt_life( p_cm_lap->p_alt_path ) + 1,
		p_cep->local_ack_delay), &p_lap->alternate_path );

	conn_lap_path_clr_rsvd_fields( &p_lap->alternate_path );

	p_lap->local_comm_id = p_cep->local_comm_id;
	p_lap->remote_comm_id = p_cep->remote_comm_id;
	conn_lap_set_remote_qpn( p_cep->remote_qpn, p_lap );
	conn_lap_set_resp_timeout( p_cm_lap->remote_resp_timeout, p_lap );

	status = conn_lap_set_pdata(
		p_cm_lap->p_lap_pdata, p_cm_lap->lap_length, p_lap );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("lap pdata invalid.\n") );
		return status;
	}

	conn_lap_clr_rsvd_fields( p_lap );

	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
al_cep_lap(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_lap_t* const			p_cm_lap )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_cm_lap );
	CL_ASSERT( p_cm_lap->p_alt_path );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_ESTABLISHED:
		if( !p_cep->was_active )
		{
			/* Only the side that took the active role can initialte a LAP. */
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Only the active side of a connection can initiate a LAP.\n") );
			status = IB_INVALID_STATE;
			break;
		}

		/*
		 * Format the AV information - store in the temporary location until we
		 * get the APR indicating acceptance.
		 */
		p_port_cep = __format_path_av( p_cep, p_cm_lap->p_alt_path, &p_cep->alt_av );
		if( !p_port_cep )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("Alternate path invalid!\n") );
			status = IB_INVALID_SETTING;
			break;
		}

		p_cep->alt_av.attr.conn.seq_err_retry_cnt =
			p_cep->av[p_cep->idx_primary].attr.conn.seq_err_retry_cnt;
		p_cep->alt_av.attr.conn.rnr_retry_cnt =
			p_cep->av[p_cep->idx_primary].attr.conn.rnr_retry_cnt;

		if( p_port_cep->h_ca->obj.p_ci_ca->verbs.guid != p_cep->local_ca_guid )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("Alternate CA GUID different from current!\n") );
			status = IB_INVALID_SETTING;
			break;
		}

		/* Store the alternate path info temporarilly. */
		p_cep->alt_2pkt_life = ib_path_rec_pkt_life( p_cm_lap->p_alt_path ) + 1;

		status = ib_get_mad( p_port_cep->pool_key, MAD_BLOCK_SIZE, &p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_lap( p_cep, p_cm_lap, p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __cep_send_retry( p_port_cep, p_cep, p_mad );
		if( status == IB_SUCCESS )
			p_cep->state = CEP_STATE_LAP_SENT;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


static ib_api_status_t
__format_apr(
	IN				kcep_t* const				p_cep,
	IN		const	ib_cm_apr_t* const			p_cm_apr,
	IN	OUT			ib_mad_element_t* const		p_mad )
{
	ib_api_status_t		status;
	mad_cm_apr_t		*p_apr;

	AL_ENTER( AL_DBG_CM );

	p_apr = (mad_cm_apr_t*)p_mad->p_mad_buf;

	p_apr->local_comm_id = p_cep->local_comm_id;
	p_apr->remote_comm_id = p_cep->remote_comm_id;
	p_apr->status = p_cm_apr->apr_status;

	status = conn_apr_set_apr_info( p_cm_apr->p_info->data,
		p_cm_apr->info_length, p_apr );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("apr_info invalid\n") );
		return status;
	}

	status = conn_apr_set_pdata( p_cm_apr->p_apr_pdata,
		p_cm_apr->apr_length, p_apr );
	if( status != IB_SUCCESS )
	{
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("apr pdata invalid\n") );
		return status;
	}

	conn_apr_clr_rsvd_fields( p_apr );
	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
al_cep_pre_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	ib_cm_apr_t* const			p_cm_apr,
		OUT			ib_qp_mod_t* const			p_apr )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_cm_apr );
	CL_ASSERT( p_apr );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_APR:
	case CEP_STATE_PRE_APR_MRA_SENT:
		CL_ASSERT( p_cep->p_mad );
		ib_put_mad( p_cep->p_mad );
		p_cep->p_mad = NULL;
		/* Fall through. */
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
		CL_ASSERT( !p_cep->p_mad );
		status = __cep_get_mad( p_cep, CM_APR_ATTR_ID, &p_port_cep, &p_cep->p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_apr( p_cep, p_cm_apr, p_cep->p_mad );
		if( status != IB_SUCCESS )
		{
			ib_put_mad( p_cep->p_mad );
			p_cep->p_mad = NULL;
			break;
		}

		if( !p_cm_apr->apr_status )
		{
			/*
			 * Copy the temporary AV and port GUID information into
			 * the alternate path.
			 */
			p_cep->av[((p_cep->idx_primary + 1) & 0x1)] = p_cep->alt_av;

			/* Update our maximum packet lifetime. */
			p_cep->max_2pkt_life =
				max( p_cep->max_2pkt_life, p_cep->alt_2pkt_life );

			/* Update our timewait time. */
			__calc_timewait( p_cep );

			/* Fill in the QP attributes. */
			cl_memclr( p_apr, sizeof(ib_qp_mod_t) );
			p_apr->req_state = IB_QPS_RTS;
			p_apr->state.rts.opts =
				IB_MOD_QP_ALTERNATE_AV | IB_MOD_QP_APM_STATE;
			p_apr->state.rts.alternate_av = p_cep->alt_av.attr;
			p_apr->state.rts.apm_state = IB_APM_REARM;
		}

		p_cep->state |= CEP_STATE_PREP;
		status = IB_SUCCESS;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_send_apr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_APR:
	case CEP_STATE_PRE_APR_MRA_SENT:
		CL_ASSERT( p_cep->p_mad );
		p_port_cep = __get_cep_agent( p_cep );
		if( !p_port_cep )
		{
			ib_put_mad( p_cep->p_mad );
			status = IB_INSUFFICIENT_RESOURCES;
		}
		else
		{
			p_cep->state = CEP_STATE_ESTABLISHED;

			__cep_send_mad( p_port_cep, p_cep->p_mad );
			status = IB_SUCCESS;
		}
		p_cep->p_mad = NULL;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_dreq(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata,
	IN		const	uint8_t						pdata_len )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
		status = __cep_get_mad( p_cep, CM_DREQ_ATTR_ID, &p_port_cep, &p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_dreq( p_cep, p_pdata, pdata_len, p_mad );
		if( status != IB_SUCCESS )
		{
			AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
				("__format_dreq returned %s.\n", ib_get_err_str( status )) );
			break;
		}

		status = __cep_send_retry( p_port_cep, p_cep, p_mad );
		if( status == IB_SUCCESS )
		{
			p_cep->state = CEP_STATE_DREQ_SENT;
		}
		else
		{
			p_cep->state = CEP_STATE_TIMEWAIT;
			__insert_timewait( p_cep );
		}

		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_drep(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN		const	uint8_t* const				p_pdata OPTIONAL,
	IN		const	uint8_t						pdata_len )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	cep_agent_t			*p_port_cep;
	ib_mad_element_t	*p_mad;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_DREQ_RCVD:
		status = __cep_get_mad( p_cep, CM_DREP_ATTR_ID, &p_port_cep, &p_mad );
		if( status != IB_SUCCESS )
			break;

		status = __format_drep( p_cep, p_pdata,
			pdata_len, (mad_cm_drep_t*)p_mad->p_mad_buf );
		if( status != IB_SUCCESS )
			break;

		__cep_send_mad( p_port_cep, p_mad );
		p_cep->state = CEP_STATE_TIMEWAIT;
		__insert_timewait( p_cep );
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_migrate(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_ESTABLISHED:
	case CEP_STATE_LAP_SENT:
	case CEP_STATE_LAP_RCVD:
	case CEP_STATE_LAP_MRA_SENT:
	case CEP_STATE_LAP_MRA_RCVD:
		if( p_cep->av[(p_cep->idx_primary + 1) & 0x1].port_guid )
		{
			p_cep->idx_primary++;
			p_cep->idx_primary &= 0x1;
			status = IB_SUCCESS;
			break;
		}

		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, ("No alternate path avaialble.\n") );

		/* Fall through. */
	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_established(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
		CL_ASSERT( p_cep->p_send_mad );
		ib_cancel_mad( p_cep->h_mad_svc, p_cep->p_send_mad );
		p_cep->p_send_mad = NULL;
		p_cep->state = CEP_STATE_ESTABLISHED;
		status = IB_SUCCESS;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}

static void
__format_path(ib_path_rec_t *p_path, req_path_info_t *p_info,
			  ib_net16_t pkey, uint8_t mtu)
{
	p_path->service_id = 0;
	p_path->dgid = p_info->local_gid;
	p_path->sgid = p_info->remote_gid;
	p_path->dlid = p_info->local_lid;
	p_path->slid = p_info->remote_lid;
	ib_path_rec_set_hop_flow_raw(p_path, p_info->hop_limit, 
								 conn_req_path_get_flow_lbl(p_info), 0);
	p_path->tclass = p_info->traffic_class;
	p_path->num_path = 0;
	p_path->pkey = pkey;
	ib_path_rec_set_sl(p_path, conn_req_path_get_svc_lvl(p_info));
	ib_path_rec_set_qos_class(p_path, 0);
	p_path->mtu = mtu;
	p_path->rate = conn_req_path_get_pkt_rate(p_info);
	p_path->pkt_life = conn_req_path_get_lcl_ack_timeout(p_info);
	p_path->preference = 0;
	memset(p_path->resv2, 0, sizeof(p_path->resv2));
}

static void
__format_event_req(kcep_t *p_cep, mad_cm_req_t *p_mad, iba_cm_req_event *p_req)
{
	p_req->local_ca_guid = p_cep->local_ca_guid;
	p_req->remote_ca_guid = p_cep->remote_ca_guid;
	p_req->pkey_index = p_cep->av[0].pkey_index;
	p_req->port_num = p_cep->av[0].attr.port_num;
	p_req->service_id = p_mad->sid;

	p_req->qpn = conn_req_get_lcl_qpn(p_mad);
	p_req->qp_type = conn_req_get_qp_type(p_mad);
	p_req->starting_psn = conn_req_get_starting_psn(p_mad);

	cl_memcpy(p_req->pdata, p_mad->pdata, IB_REQ_PDATA_SIZE);

	p_req->max_cm_retries = conn_req_get_max_cm_retries(p_mad);
	p_req->resp_res = conn_req_get_init_depth(p_mad);
	p_req->init_depth = conn_req_get_resp_res(p_mad);
	p_req->remote_resp_timeout = conn_req_get_resp_timeout(p_mad);
	p_req->flow_ctrl = (uint8_t) conn_req_get_flow_ctrl(p_mad);
	p_req->local_resp_timeout = conn_req_get_lcl_resp_timeout(p_mad);
	p_req->rnr_retry_cnt = conn_req_get_rnr_retry_cnt(p_mad);
	p_req->retry_cnt = conn_req_get_retry_cnt(p_mad);
	p_req->srq = 0; // TODO: fix mad_cm_req_t

	__format_path(&p_req->primary_path, &p_mad->primary_path,
				  p_mad->pkey, conn_req_get_mtu(p_mad));

	if (p_mad->alternate_path.remote_lid != 0) {
		__format_path(&p_req->alt_path, &p_mad->alternate_path,
					  p_req->primary_path.pkey, p_req->primary_path.mtu);
	} else {
		cl_memclr(&p_req->alt_path, sizeof p_req->alt_path);
	}
}

static void
__format_event_rep(mad_cm_rep_t *p_mad, iba_cm_rep_event *p_rep)
{
	p_rep->ca_guid = p_mad->local_ca_guid;
	p_rep->target_ack_delay = conn_rep_get_target_ack_delay(p_mad);
	p_rep->rep.qpn = conn_rep_get_lcl_qpn(p_mad);
	p_rep->rep.starting_psn = conn_rep_get_starting_psn(p_mad);

	p_rep->rep.p_pdata = p_mad->pdata;
	p_rep->rep.pdata_len = IB_REP_PDATA_SIZE;

	p_rep->rep.failover_accepted = conn_rep_get_failover(p_mad);
	p_rep->rep.resp_res = p_mad->initiator_depth;
	p_rep->rep.init_depth = p_mad->resp_resources;
	p_rep->rep.flow_ctrl = (uint8_t) conn_rep_get_e2e_flow_ctl(p_mad);
	p_rep->rep.rnr_retry_cnt = conn_rep_get_rnr_retry_cnt(p_mad);
}

static void
__format_event_rej(mad_cm_rej_t *p_mad, iba_cm_rej_event *p_rej)
{
	p_rej->ari = p_mad->ari;
	p_rej->p_pdata = p_mad->pdata;
	p_rej->reason = p_mad->reason;
	p_rej->ari_length = conn_rej_get_ari_len(p_mad);
	p_rej->pdata_len = IB_REJ_PDATA_SIZE;
}

static void
__format_event_mra(mad_cm_mra_t *p_mad, iba_cm_mra_event *p_mra)
{
	p_mra->p_pdata = p_mad->pdata;
	p_mra->pdata_len = IB_MRA_PDATA_SIZE;
	p_mra->service_timeout = conn_mra_get_svc_timeout(p_mad);
}

/*
 * Called after polling a MAD from a CEP to parse the received CM message
 * into readable event data.
 */
void
kal_cep_format_event(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				ib_mad_element_t			*p_mad,
	IN	OUT			iba_cm_event				*p_event)
{
	KLOCK_QUEUE_HANDLE	hdl;
	kcep_t				*p_cep;
	
	switch (p_mad->p_mad_buf->attr_id) {
	case CM_REQ_ATTR_ID:
		KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
		p_cep = __lookup_cep( h_al, cid );
		if (p_mad->status == IB_SUCCESS && p_cep != NULL) {
			p_event->type = iba_cm_req_received;
			__format_event_req(p_cep, (mad_cm_req_t*) p_mad->p_mad_buf, &p_event->data.req);
		} else {
			p_event->type = iba_cm_req_error;
		}
		KeReleaseInStackQueuedSpinLock( &hdl );
		break;
	case CM_REP_ATTR_ID:
		if (p_mad->status == IB_SUCCESS) {
			p_event->type = iba_cm_rep_received;
			__format_event_rep((mad_cm_rep_t*) p_mad->p_mad_buf, &p_event->data.rep);
		} else {
			p_event->type = iba_cm_rep_error;
		}
		break;
	case CM_RTU_ATTR_ID:
		p_event->type = iba_cm_rtu_received;
		p_event->data.rtu.p_pdata = ((mad_cm_rtu_t*) p_mad)->pdata;
		p_event->data.rtu.pdata_len = IB_RTU_PDATA_SIZE;
		break;
	case CM_DREQ_ATTR_ID:
		if (p_mad->status == IB_SUCCESS) {
			p_event->type = iba_cm_dreq_received;
			p_event->data.dreq.p_pdata = ((mad_cm_dreq_t*) p_mad)->pdata;
			p_event->data.dreq.pdata_len = IB_DREQ_PDATA_SIZE;
		} else {
			p_event->type = iba_cm_dreq_error;
		}
		break;
	case CM_DREP_ATTR_ID:
		p_event->type = iba_cm_drep_received;
		p_event->data.drep.p_pdata = ((mad_cm_drep_t*) p_mad)->pdata;
		p_event->data.drep.pdata_len = IB_DREP_PDATA_SIZE;
		break;
	case CM_REJ_ATTR_ID:
		p_event->type = iba_cm_rej_received;
		__format_event_rej((mad_cm_rej_t*) p_mad->p_mad_buf, &p_event->data.rej);
		break;
	case CM_MRA_ATTR_ID:
		p_event->type = iba_cm_mra_received;
		__format_event_mra((mad_cm_mra_t*) p_mad->p_mad_buf, &p_event->data.mra);
		break;
	case CM_LAP_ATTR_ID:
		if (p_mad->status == IB_SUCCESS) {
			p_event->type = iba_cm_lap_received;
			// TODO: format lap event
		} else {
			p_event->type = iba_cm_lap_error;
		}
		break;
	case CM_APR_ATTR_ID:
		p_event->type = iba_cm_apr_received;
		// TODO: format apr event
		break;
	case CM_SIDR_REQ_ATTR_ID:
		if (p_mad->status == IB_SUCCESS) {
			p_event->type = iba_cm_sidr_req_received;
			// TODO: format sidr req event
		} else {
			p_event->type = iba_cm_sidr_req_error;
		}
		break;
	case CM_SIDR_REP_ATTR_ID:
		p_event->type = iba_cm_sidr_rep_received;
		// TODO: format sidr rep event
		break;
	default:
		CL_ASSERT(0);
	}
}


ib_api_status_t
al_cep_get_init_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_init )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		status = IB_INVALID_HANDLE;
		goto out;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_PRE_REQ:
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_REQ_SENT:
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_PRE_REP_MRA_SENT:
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
	case CEP_STATE_REP_MRA_SENT:
	case CEP_STATE_ESTABLISHED:
		/* Format the INIT qp modify attributes. */
		__al_cep_fill_init_attr(p_cep, p_init);
		status = IB_SUCCESS;
		break;
	default:
		status = IB_INVALID_STATE;
		break;
	}

out:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}

ib_api_status_t
al_cep_get_rtr_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rtr )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_rtr );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_MRA_RCVD:
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
	case CEP_STATE_ESTABLISHED:
		cl_memclr( p_rtr, sizeof(ib_qp_mod_t) );
		p_rtr->req_state = IB_QPS_RTR;

		/* Required params. */
		p_rtr->state.rtr.rq_psn = p_cep->rq_psn;
		p_rtr->state.rtr.dest_qp = p_cep->remote_qpn;
		p_rtr->state.rtr.primary_av = p_cep->av[p_cep->idx_primary].attr;
		p_rtr->state.rtr.resp_res = p_cep->resp_res;
		p_rtr->state.rtr.rnr_nak_timeout = p_cep->rnr_nak_timeout;

		/* Optional params. */
		p_rtr->state.rtr.opts = 0;
		if( p_cep->av[(p_cep->idx_primary + 1) & 0x1].port_guid )
		{
			p_rtr->state.rtr.opts |= IB_MOD_QP_ALTERNATE_AV;
			p_rtr->state.rtr.alternate_av =
				p_cep->av[(p_cep->idx_primary + 1) & 0x1].attr;
		}
		if((p_cep->av[p_cep->idx_primary].flags & CEP_AV_FLAG_GENERATE_PRIO_TAG) != 0)
		{
			p_rtr->state.rtr.opts |= IB_MOD_QP_GENERATE_PRIO_TAG;
		}
		status = IB_SUCCESS;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_get_rts_attr(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			ib_qp_mod_t* const			p_rts )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_rts );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	switch( p_cep->state )
	{
	case CEP_STATE_REQ_SENT:
	case CEP_STATE_REQ_RCVD:
	case CEP_STATE_REQ_MRA_SENT:
	case CEP_STATE_REQ_MRA_RCVD:
	case CEP_STATE_REP_SENT:
	case CEP_STATE_REP_RCVD:
	case CEP_STATE_REP_MRA_SENT:
	case CEP_STATE_REP_MRA_RCVD:
	case CEP_STATE_PRE_REP:
	case CEP_STATE_PRE_REP_MRA_SENT:
	case CEP_STATE_ESTABLISHED:
		cl_memclr( p_rts, sizeof(ib_qp_mod_t) );
		p_rts->req_state = IB_QPS_RTS;

		/* Required params. */
		p_rts->state.rts.sq_psn = p_cep->sq_psn;
		p_rts->state.rts.retry_cnt =
			p_cep->av[p_cep->idx_primary].attr.conn.seq_err_retry_cnt;
		p_rts->state.rts.rnr_retry_cnt =
			p_cep->av[p_cep->idx_primary].attr.conn.rnr_retry_cnt;
		p_rts->state.rts.local_ack_timeout =
			p_cep->av[p_cep->idx_primary].attr.conn.local_ack_timeout;
		p_rts->state.rts.init_depth = p_cep->init_depth;

		/* Optional params. */
		p_rts->state.rts.opts = 0;
		if( p_cep->av[(p_cep->idx_primary + 1) & 0x1].port_guid )
		{
			p_rts->state.rts.opts =
				IB_MOD_QP_ALTERNATE_AV | IB_MOD_QP_APM_STATE;
			p_rts->state.rts.apm_state = IB_APM_REARM;
			p_rts->state.rts.alternate_av =
				p_cep->av[(p_cep->idx_primary + 1) & 0x1].attr;
		}
		status = IB_SUCCESS;
		break;

	default:
		AL_PRINT( TRACE_LEVEL_WARNING, AL_DBG_CM,
			("Invalid state: %d\n", p_cep->state) );
		status = IB_INVALID_STATE;
		break;
	}
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return status;
}


ib_api_status_t
al_cep_get_timewait(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			uint64_t* const				p_timewait_us )
{
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	*p_timewait_us = p_cep->timewait_time.QuadPart / 10;
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return IB_SUCCESS;
}


ib_api_status_t
al_cep_poll(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
		OUT			void**						p_context,
		OUT			net32_t* const				p_new_cid,
		OUT			ib_mad_element_t** const	pp_mad )
{
	ib_api_status_t		status;
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );
	CL_ASSERT( p_new_cid );
	CL_ASSERT( pp_mad );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return IB_INVALID_HANDLE;
	}

	*p_context = p_cep->context;

	if( !p_cep->p_mad_head )
	{
		p_cep->signalled = FALSE;
		status = IB_NOT_DONE;
		goto done;
	}

	/* Set the MAD. */
	*pp_mad = p_cep->p_mad_head;
	p_cep->p_mad_head = p_cep->p_mad_head->p_next;
	(*pp_mad)->p_next = NULL;

	/* We're done with the input CEP.  Reuse the variable */
	p_cep = (kcep_t*)(*pp_mad)->send_context1;
	if( p_cep )
		*p_new_cid = p_cep->cid;
	else
		*p_new_cid = AL_INVALID_CID;

	status = IB_SUCCESS;

done:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("] return %d\n", status) );
	return status;
}

#ifdef NTDDI_WIN8
static DRIVER_CANCEL __cep_cancel_irp;
#endif
static void
__cep_cancel_irp(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				IRP*						p_irp )
{
	net32_t					cid;
	ib_al_handle_t			h_al;
	KLOCK_QUEUE_HANDLE		hdl;
	kcep_t					*p_cep;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( p_dev_obj );
	CL_ASSERT( p_irp );

	cid = (net32_t)(size_t)p_irp->Tail.Overlay.DriverContext[0];
	h_al = (ib_al_handle_t)p_irp->Tail.Overlay.DriverContext[1];
	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( p_cep )
		__cep_complete_irp( p_cep, STATUS_CANCELLED, IO_NO_INCREMENT );

	KeReleaseInStackQueuedSpinLock( &hdl );

	IoReleaseCancelSpinLock( p_irp->CancelIrql );

	AL_EXIT( AL_DBG_CM );
}


NTSTATUS
al_cep_queue_irp(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				IRP* const					p_irp )
{
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_ENTER( AL_DBG_CM );

	CL_ASSERT( h_al );
	CL_ASSERT( p_irp );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_EXIT( AL_DBG_CM );
		return STATUS_INVALID_PARAMETER;
	}

	/*
	 * Store the CID an AL handle in the IRP's driver context
	 * so we can cancel it.
	 */
	p_irp->Tail.Overlay.DriverContext[0] = (void*)(size_t)cid;
	p_irp->Tail.Overlay.DriverContext[1] = (void*)h_al;
#pragma warning(push, 3)
	IoSetCancelRoutine( p_irp, __cep_cancel_irp );
#pragma warning(pop)
	IoMarkIrpPending( p_irp );

	/* Always dequeue  and complete whatever IRP is there. */
	__cep_complete_irp( p_cep, STATUS_CANCELLED, IO_NO_INCREMENT );

	InterlockedExchangePointer( &p_cep->p_irp, p_irp );

	/* Complete the IRP if there are MADs to be reaped. */
	if( p_cep->p_mad_head )
		__cep_complete_irp( p_cep, STATUS_SUCCESS, IO_NETWORK_INCREMENT );

	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return STATUS_PENDING;
}


void
al_cep_cleanup_al(
	IN		const	ib_al_handle_t				h_al )
{
	cl_list_item_t		*p_item;
	net32_t				cid;

	AL_ENTER( AL_DBG_CM );

	/* Destroy all CEPs associated with the input instance of AL. */
	cl_spinlock_acquire( &h_al->obj.lock );
	for( p_item = cl_qlist_head( &h_al->cep_list );
		p_item != cl_qlist_end( &h_al->cep_list );
		p_item = cl_qlist_head( &h_al->cep_list ) )
	{
		/*
		 * Note that we don't walk the list - we can't hold the AL
		 * lock when cleaning up its CEPs because the cleanup path
		 * takes the CEP's lock.  We always want to take the CEP
		 * before the AL lock to prevent any possibilities of deadlock.
		 *
		 * So we just get the CID, and then release the AL lock and try to
		 * destroy.  This should unbind the CEP from the AL instance and
		 * remove it from the list, allowing the next CEP to be cleaned up
		 * in the next pass through.
		 */
		cid = PARENT_STRUCT( p_item, kcep_t, al_item )->cid;
		cl_spinlock_release( &h_al->obj.lock );
		al_destroy_cep( h_al, &cid, FALSE );
		cl_spinlock_acquire( &h_al->obj.lock );
	}
	cl_spinlock_release( &h_al->obj.lock );

	AL_EXIT( AL_DBG_CM );
}

#ifdef NTDDI_WIN8
_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_requires_min_(DISPATCH_LEVEL)
__drv_at(p_irp->CancelIrql, __drv_restoresIRQL)
#endif
static void
__cep_cancel_ndi_irp(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				IRP*						p_irp )
{
	KLOCK_QUEUE_HANDLE		hdl;

	AL_ENTER( AL_DBG_CM );

	UNUSED_PARAM( p_dev_obj );
	CL_ASSERT( p_irp );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	RemoveEntryList( &p_irp->Tail.Overlay.ListEntry );
	KeReleaseInStackQueuedSpinLock( &hdl );

#pragma warning(push, 3)
	IoSetCancelRoutine( p_irp, NULL );
#pragma warning(pop)
	IoReleaseCancelSpinLock( p_irp->CancelIrql );
		
	/* Complete the IRP. */
	p_irp->IoStatus.Status = STATUS_CANCELLED;
	p_irp->IoStatus.Information = 0;
	IoCompleteRequest( p_irp, IO_NO_INCREMENT );

	AL_EXIT( AL_DBG_CM );
}


NTSTATUS
al_cep_get_pdata(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
        OUT         uint8_t                     *p_init_depth,
        OUT         uint8_t                     *p_resp_res,
	IN	OUT			uint8_t						*p_psize,
		OUT			uint8_t*					pdata )
{
	kcep_t				*p_cep;
	KLOCK_QUEUE_HANDLE	hdl;
	uint8_t             copy_len;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		KeReleaseInStackQueuedSpinLock( &hdl );
		AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("CEP not found for cid %d, h_al %p\n", cid, h_al ));
		return STATUS_CONNECTION_INVALID;
	}

    *p_init_depth = p_cep->init_depth;
    *p_resp_res = p_cep->resp_res;

    copy_len = min( *p_psize, p_cep->psize );

    if( copy_len )
        RtlCopyMemory( pdata, p_cep->pdata, copy_len );

    // Always report the maximum available user private data.
    *p_psize = p_cep->psize;

	AL_PRINT(TRACE_LEVEL_INFORMATION ,AL_DBG_CM ,
		("al_cep_get_pdata: get %d of pdata from CEP with cid %d, h_al %p, context %p \n", 
		copy_len, cid, h_al, p_cep->context ));

	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_EXIT( AL_DBG_CM );
	return STATUS_SUCCESS;
}


/*
 * This function is designed to support moving the NetorkDirect IRP queue to the CEP
 * without performing major surgery on the CEP manager.
 *
 * It retrieves the context associated with a CEP, using the pfn_addref function
 * to prevent the context from being destroyed after it is returned.
 *
 * It returns NULL if there is no context, requiring contexts to be pointers.
 */
void*
kal_cep_get_context(
	IN				ib_al_handle_t				h_al,
	IN				net32_t						cid,
	IN				al_pfn_cep_cb_t				pfn_cb,
	IN				ib_pfn_destroy_cb_t			pfn_addref )
{
	kcep_t				*p_cep;
	void*				context = NULL;
	KLOCK_QUEUE_HANDLE	hdl;

	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("[ CID = %d\n", cid) );

	CL_ASSERT( h_al );

	KeAcquireInStackQueuedSpinLock( &gp_cep_mgr->lock, &hdl );
	p_cep = __lookup_cep( h_al, cid );
	if( !p_cep )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR, 
			("CEP not found for cid %d, h_al %p\n", cid, h_al ));
		goto out;
	}

	if( pfn_cb && p_cep->pfn_cb != pfn_cb )
	{
		AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
			("CEP callback mismatch for cid %d, h_al %p\n", cid, h_al ));
		goto out;
	}

	context = p_cep->context;
	if( pfn_addref && context != NULL )
	{
		pfn_addref( context );
	}

out:
	KeReleaseInStackQueuedSpinLock( &hdl );
	AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_CM, ("] returning %p\n", context) );
	return context;
}
