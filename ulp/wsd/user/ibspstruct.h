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

#include <complib/cl_qlist.h>
#include <complib/cl_fleximap.h>
#include <complib/cl_rbmap.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_debug.h>



enum ibsp_socket_state
{
	IBSP_CREATE = 0,
	IBSP_BIND,
	IBSP_CONNECT,
	IBSP_LISTEN,
	IBSP_CONNECTED,
	IBSP_DUPLICATING_OLD,		/* duplicating socket on the original controlling process */
	IBSP_DUPLICATING_NEW,		/* duplicating socket on the new controlling process */
	IBSP_DUPLICATING_REMOTE,	/* duplicating socket on the remote side */
	IBSP_DISCONNECTED,
	IBSP_CLOSED,
	IBSP_NUM_STATES
};

extern char *ibsp_socket_state_str[IBSP_NUM_STATES];

#define IBSP_SOCKET_STATE_STR(state) \
	(state < IBSP_NUM_STATES)?ibsp_socket_state_str[state]:"INVALID"

/* Socket Options structure */
struct ibsp_socket_options
{
	BOOL debug;					/* SO_DEBUG */
	GROUP group_id;				/* SO_GROUP_ID */
	int group_priority;			/* SO_GROUP_PRIORITY */
	unsigned int max_msg_size;	/* SO_MAX_MSG_SIZE */
	int max_rdma_size;			/* SO_MAX_RDMA_SIZE */
	int rdma_threshold_size;	/* SO_RDMA_THRESHOLD_SIZE */
};

/* Used to discriminate between various listen on the same ports. 
 * We need this for socket duplication. 
 * { 0, 0 } is a standard connection request. */
struct listen_req_param
{
	DWORD dwProcessId;
	GUID identifier;
};

/* Parameters given to establish a connection */
struct cm_req_params
{
	struct listen_req_param listen_req_param;
	struct sockaddr_in dest;
	struct sockaddr_in source;	/* Source of connect() */
};

/* Listen structure.
 * Used to remember an incoming connection. */
struct listen_incoming
{
	cl_list_item_t			item;

	ib_cm_req_rec_t			cm_req_received;
	struct cm_req_params	params;
};


/* Keeps track of the posted WR */
struct _wr
{
	LPWSAOVERLAPPED				lpOverlapped;
	struct ibsp_socket_info		*socket_info;
};


/* Keeps track of the posted WR */
struct _recv_wr
{
	struct _wr		wr;
	ib_recv_wr_t	recv;
	ib_local_ds_t	ds_array[QP_ATTRIB_RQ_SGE];
#ifdef IBSP_LOGGING
	LONG			idx;
#endif
};


/* Keeps track of the registered MRs */
struct mr_list
{
	cl_qlist_t list;			/* Regions registered through IBSPRegisterMemory */
	cl_spinlock_t mutex;		/* Protects the list */
};

/* Information necessary to duplicate sockets */
struct ibsp_duplicate_info
{
	GUID identifier;
	struct ibsp_socket_options socket_options;
	struct sockaddr_in local_addr;
	struct sockaddr_in peer_addr;
	DWORD dwProcessId;
};

/* Give the reason for disconnections */
struct disconnect_reason
{
	enum
	{
		DISC_INVALID,
		DISC_SHUTDOWN,			/* normal shutdown */
		DISC_DUPLICATING		/* socket duplication */
	} type;

	struct _disconnect_reason_dup
	{
		GUID identifier;
		DWORD dwProcessId;

	}	duplicating;
};


/* Internal node describing a registered region. */
struct memory_reg
{
	cl_list_item_t	item;
	/*
	 * List count serves as reference count.  The memory registration
	 * can be released when the list is empty.
	 */
	cl_qlist_t		node_list;

#ifdef _DEBUG_
#define MR_NODE_MAGIC 0x7fba43ce
	int magic;
#endif

	/* Characteristics of that region. */
	ib_mr_create_t	type;

	/* Memory registration parameters, returned by ib_reg_mem. */
	uint32_t		lkey;
	uint32_t		rkey;
	ib_mr_handle_t	mr_handle;
};


struct memory_node
{
	/* List item to track within a socket structure. */
	cl_list_item_t			socket_item;
	struct ibsp_socket_info	*s;
	/* List item to track within the registration structure. */
	cl_list_item_t			mr_item;
	struct memory_reg		*p_reg1;
};



/* Descriptor given back to WSPRegisterRdmaMemory */
struct rdma_memory_desc
{
	uint64_t iova;
	uint32_t lkey;
	uint32_t rkey;
	struct memory_node *node1;	/* valid only on registering node */
};

struct cq_thread_info
{
	cl_list_item_t		list_item;

	cl_waitobj_handle_t cq_waitobj;
	ib_cq_handle_t cq;

	/* Number of qp's using this cq */
	atomic32_t qp_count;

	/* Current cqe size */
	uint32_t cqe_size;

	HANDLE ib_cq_thread;
	DWORD	ib_cq_thread_id;
	BOOL ib_cq_thread_exit_wanted;
	cl_spinlock_t cq_spinlock;

	struct ibsp_hca *hca;		/* HCA to which this cq belongs. */
};


enum APM_STATE {
	APM_ARMED,
	APM_MIGRATED,
	APM_LAP_SENT

};


/* Structure representing the context information stored for each
 * socket created */
struct ibsp_socket_info
{
	cl_list_item_t item;		/* Link to next SOCKET_INFO in the global list */
	cl_rbmap_item_t	conn_item;
	cl_spinlock_t mutex1;		/* protect this structure */

	/* Switch socket handle created by WPUCreateSocketHandle. */
	SOCKET switch_socket;

	/* IP address and port this socket is bound to. Set by WSPBind */
	struct sockaddr_in local_addr;

	/* Remote address of peer entity after connect/accept is complete */
	struct sockaddr_in peer_addr;

	/* Back pointer to the port to which this socket is
	 * bound. It is NULL until the socket is bound, except if the listen
	 * binds to INADDR_ANY. */
	struct ibsp_port *port;

	enum ibsp_socket_state socket_state;	/* represents current socket state */

	struct
	{
		/* Listening socket */
		unsigned int backlog;	/* Maximum number of pending connections */
		cl_qlist_t list;	/* list of pending connections */
		ib_listen_handle_t handle;
		struct listen_req_param listen_req_param;
	} listen;

	/* Event for blocking accept, and connect */
	HANDLE	h_event;

	/* Variables associated with IBSPSelectEvent */
	WSAEVENT event_select;		/* Handle to Event Object */
	long event_mask;			/* Events we care about */
	long network_events;		/* Events that happenned */
	int errno_connect;			/* errno code (if any) returned by connect */

	struct ibsp_socket_options socket_options;	/* Socket Options */

	/* Infiniband ressources */
	ib_pd_handle_t hca_pd;		/* Copy of the HCA PD, for faster access. */

	/* Pointer to completion queue and thread assigned to this socket */
	struct cq_thread_info *cq_tinfo;

	ib_qp_handle_t		qp;
	uint32_t			max_inline;

	/* State on the QP. This is only valid when the socket state is IBSP_CONNECTED.
	 *   0  : good
	 *   <0 : an error occurred, contains a windoes error *ie WSAExxxx
	 *   -1 : disconected, for duplication process.
	 */
	int qp_error;

	/* Send request processing. */
	cl_spinlock_t	send_lock;
	ib_send_opt_t	send_opt;
	struct _wr		send_wr[QP_ATTRIB_SQ_DEPTH];
	uint8_t			send_idx;
	atomic32_t		send_cnt; /* Used to limit access to send_wr array. */

	/* Receive request processing. */
	cl_spinlock_t	recv_lock;
	struct _recv_wr	recv_wr[QP_ATTRIB_RQ_DEPTH];
	uint8_t			recv_idx;
	atomic32_t		recv_cnt; /* Used to limit access to recv_wr array. */

	/*
	 * Used to stall destruction of switch socket until all completion
	 * upcalls have unwound.
	 */
	atomic32_t		ref_cnt1;

#ifdef _DEBUG_
	atomic32_t		send_comp;
	atomic32_t		recv_comp;
#endif

	struct _recv_wr	dup_wr[QP_ATTRIB_RQ_DEPTH];
	uint8_t			dup_idx;
	atomic32_t		dup_cnt;

	/*
	 * The switch will register local and RDMA memory for use in RDMA
	 * transfers.  All RDMA registrations are cached in the HCA structure,
	 * and have memory_node structures referencing them stored here in the
	 * socket structures.
	 */
	cl_qlist_t		mr_list;

	/* Stuff for socket duplication */
	struct
	{
		GUID identifier;		/* Unique identifier */
		DWORD dwProcessId;
	} duplicate;
	BOOL active_side; // Tell if we have started this call
	enum APM_STATE apm_state;
	UINT SuccesfulMigrations;
	ib_net64_t dest_port_guid;

#ifdef IBSP_LOGGING
	DataLogger		SendDataLogger;
	DataLogger		RecvDataLogger;
	long			recv_log_idx;
	long			send_log_idx;
#endif
};


inline void
ibsp_css(
					char						*calling_func,
					int							line,
					struct ibsp_socket_info		*s,
					enum ibsp_socket_state		new_state )
{
	enum ibsp_socket_state old_state;

	UNUSED_PARAM( calling_func );
	UNUSED_PARAM( line );

	old_state = s->socket_state;

	if( old_state == new_state )
	{
		/* Nothing to change */
		return;
	}

	/* IBSP_CLOSED is a dead end state */
	if( old_state == IBSP_CLOSED )
	{
		return;
	}


	s->socket_state = new_state;
}

#define IBSP_CHANGE_SOCKET_STATE(socket_info, new_state) \
	ibsp_css(__FUNCTION__, __LINE__, socket_info, new_state)


/*--------------------------------------------------------------------------*/

/* Describes an IP address */
struct ibsp_ip_addr
{
	cl_fmap_item_t		item;			/* next IP for that port */

	struct ibsp_port	*p_port;		/* port that owns this IP address */
	struct in_addr		ip_addr;		/* IPv4 address */
};

/* describes a port */
struct ibsp_port
{
	cl_list_item_t item;

	struct ibsp_hca *hca;		/* HCA to which this port belong. */

	ib_net64_t guid;
	uint8_t port_num;
};

/* Describes a hca */
struct ibsp_hca
{
	cl_list_item_t item;

	ib_net64_t guid;
	uint16_t	dev_id;	/* Device ID to selectively cap MTU to 1K for Tavor. */
	ib_ca_handle_t hca_handle;

	ib_pd_handle_t pd;

	/* Memory management */
	struct mr_list rdma_mem_list;	/* Regions registered through IBSPRegisterRdmaMemory */

	cl_spinlock_t	port_lock;
	cl_qlist_t		port_list;

	/*
	 * The CQ list is a circular list without an end.  The pointer here
	 * points to the entry that should be used for the next allocation.
	 */
	cl_spinlock_t	cq_lock;
	struct cq_thread_info *cq_tinfo;
};

struct apm_data_t
{
	HANDLE	hThread;
	HANDLE	hEvent;
	BOOL ThreadExit;
};

/* There is only one instance of that structure. */
struct ibspdll_globals
{
	/* Special values. Keep first and in this order. These are not reset
	 * between WSAStartupEx and WSACleanup calls. */
	cl_spinlock_t mutex;
	UINT entry_count;

	/* Provider */
	WSPUPCALLTABLEEX up_call_table;	/* MUST keep afetr entry_count */
	HANDLE heap;
	cl_qlist_t socket_info_list;	/* List of all the created sockets */
	cl_rbmap_t conn_map;	/* rb tree of all connections to ensure unique 4-tuple */
	cl_spinlock_t socket_info_mutex;

	WSAPROTOCOL_INFOW protocol_info;

	/* Infiniband */
	ib_al_handle_t al_handle;
	ib_pnp_handle_t pnp_handle_ca;
	ib_pnp_handle_t pnp_handle_port;

	cl_qlist_t hca_list;
	cl_spinlock_t hca_mutex;

	HANDLE			h_ibat_dev;
	cl_fmap_t		ip_map;			/* list of all IP addresses supported by all the ports. */
	cl_spinlock_t	ip_mutex;

	struct apm_data_t apm_data;

#ifdef _DEBUG_
	/* Statistics */
	atomic32_t qp_num;
	atomic32_t cq_num;
	atomic32_t pd_num;
	atomic32_t al_num;
	atomic32_t mr_num;
	atomic32_t ca_num;
	atomic32_t listen_num;
	atomic32_t pnp_num;
	atomic32_t thread_num;
	atomic32_t wpusocket_num;

	atomic32_t overlap_h0_count;
	atomic32_t overlap_h1_comp_count;
	atomic32_t overlap_h1_count;
	atomic32_t max_comp_count;
	atomic32_t send_count;
	atomic32_t recv_count;
	atomic32_t total_send_count;
	atomic32_t total_recv_count;
	atomic32_t total_recv_compleated;
	atomic32_t CloseSocket_count;
#endif
};
