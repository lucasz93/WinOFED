/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2004, 2008 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2005 PathScale, Inc.  All rights reserved.
 *
 * This software is available to you under the OpenFabrics.org BSD license
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef INFINIBAND_VERBS_H
#define INFINIBAND_VERBS_H

#include <windows.h>
#include <rdma\winverbs.h>
#include <comp_channel.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Interfaces based on libibverbs 1.1.4.
 */

typedef unsigned __int8		uint8_t;
typedef unsigned __int16	uint16_t;
typedef unsigned __int32	uint32_t;
typedef unsigned __int64	uint64_t;

union ibv_gid
{
	uint8_t			raw[16];
	struct
	{
		uint64_t	subnet_prefix;
		uint64_t	interface_id;

	}	global;
};

enum ibv_node_type
{
	IBV_NODE_UNKNOWN	= -1,
	IBV_NODE_CA 		= 1,
	IBV_NODE_SWITCH,
	IBV_NODE_ROUTER,
	IBV_NODE_RNIC
};

enum ibv_transport_type
{
	IBV_TRANSPORT_UNKNOWN	= WvDeviceUnknown,
	IBV_TRANSPORT_IB		= WvDeviceInfiniband,
	IBV_TRANSPORT_IWARP		= WvDeviceIwarp
};

enum ibv_device_cap_flags
{
	IBV_DEVICE_RESIZE_MAX_WR		= WV_DEVICE_RESIZE_MAX_WR,
	IBV_DEVICE_BAD_PKEY_CNTR		= WV_DEVICE_BAD_PKEY_COUNTER,
	IBV_DEVICE_BAD_QKEY_CNTR		= WV_DEVICE_BAD_QKEY_COUNTER,
	IBV_DEVICE_RAW_MULTI			= 0,
	IBV_DEVICE_AUTO_PATH_MIG		= WV_DEVICE_PATH_MIGRATION,
	IBV_DEVICE_CHANGE_PHY_PORT		= WV_DEVICE_CHANGE_PHYSICAL_PORT,
	IBV_DEVICE_UD_AV_PORT_ENFORCE	= WV_DEVICE_AH_PORT_CHECKING,
	IBV_DEVICE_CURR_QP_STATE_MOD	= WV_DEVICE_QP_STATE_MODIFIER,
	IBV_DEVICE_SHUTDOWN_PORT		= WV_DEVICE_SHUTDOWN_PORT,
	IBV_DEVICE_INIT_TYPE			= WV_DEVICE_INIT_TYPE,
	IBV_DEVICE_PORT_ACTIVE_EVENT	= WV_DEVICE_PORT_ACTIVE_EVENT,
	IBV_DEVICE_SYS_IMAGE_GUID		= WV_DEVICE_SYSTEM_IMAGE_GUID,
	IBV_DEVICE_RC_RNR_NAK_GEN		= WV_DEVICE_RC_RNR_NAK_GENERATION,
	IBV_DEVICE_SRQ_RESIZE			= WV_DEVICE_SRQ_RESIZE,
	IBV_DEVICE_N_NOTIFY_CQ			= WV_DEVICE_BATCH_NOTIFY_CQ
};

enum ibv_atomic_cap
{
	IBV_ATOMIC_NONE = WvAtomicNone,
	IBV_ATOMIC_HCA	= WvAtomicDevice,
	IBV_ATOMIC_GLOB	= WvAtomicNode
};

struct ibv_device_attr
{
	char			fw_ver[64];
	uint64_t		node_guid;
	uint64_t		sys_image_guid;
	uint64_t		max_mr_size;
	uint64_t		page_size_cap;
	uint32_t		vendor_id;
	uint32_t		vendor_part_id;
	uint32_t		hw_ver;
	int				max_qp;
	int				max_qp_wr;
	int				device_cap_flags;
	int				max_sge;
	int				max_sge_rd;
	int				max_cq;
	int				max_cqe;
	int				max_mr;
	int				max_pd;
	int				max_qp_rd_atom;
	int				max_ee_rd_atom;
	int				max_res_rd_atom;
	int				max_qp_init_rd_atom;
	int				max_ee_init_rd_atom;
	enum ibv_atomic_cap	atomic_cap;
	int				max_ee;
	int				max_rdd;
	int				max_mw;
	int				max_raw_ipv6_qp;
	int				max_raw_ethy_qp;
	int				max_mcast_grp;
	int				max_mcast_qp_attach;
	int				max_total_mcast_qp_attach;
	int				max_ah;
	int				max_fmr;
	int				max_map_per_fmr;
	int				max_srq;
	int				max_srq_wr;
	int				max_srq_sge;
	uint16_t		max_pkeys;
	uint8_t			local_ca_ack_delay;
	uint8_t			phys_port_cnt;
};

enum ibv_mtu
{
	IBV_MTU_256  = 1,
	IBV_MTU_512  = 2,
	IBV_MTU_1024 = 3,
	IBV_MTU_2048 = 4,
	IBV_MTU_4096 = 5
};

enum ibv_port_state
{
	IBV_PORT_NOP			= WvPortNop,
	IBV_PORT_DOWN			= WvPortDown,
	IBV_PORT_INIT			= WvPortInit,
	IBV_PORT_ARMED			= WvPortArmed,
	IBV_PORT_ACTIVE			= WvPortActive,
	IBV_PORT_ACTIVE_DEFER	= WvPortActiveDefer
};

struct ibv_port_attr
{
	enum ibv_port_state	state;
	enum ibv_mtu		max_mtu;
	enum ibv_mtu		active_mtu;
	int					gid_tbl_len;
	uint32_t			port_cap_flags;
	uint32_t			max_msg_sz;
	uint32_t			bad_pkey_cntr;
	uint32_t			qkey_viol_cntr;
	uint16_t			pkey_tbl_len;
	uint16_t			lid;
	uint16_t			sm_lid;
	uint8_t				lmc;
	uint8_t				max_vl_num;
	uint8_t				sm_sl;
	uint8_t				subnet_timeout;
	uint8_t				init_type_reply;
	uint8_t				active_width;
	uint8_t				active_speed;
	uint8_t				phys_state;
	uint8_t				transport;
	uint8_t				ext_active_speed;
	uint8_t				link_encoding;
};

// Only device/port level events are currently supported.
enum ibv_event_type
{
	IBV_EVENT_CQ_ERR,
	IBV_EVENT_QP_FATAL,
	IBV_EVENT_QP_REQ_ERR,
	IBV_EVENT_QP_ACCESS_ERR,
	IBV_EVENT_COMM_EST,
	IBV_EVENT_SQ_DRAINED,
	IBV_EVENT_PATH_MIG,
	IBV_EVENT_PATH_MIG_ERR,
	IBV_EVENT_DEVICE_FATAL,
	IBV_EVENT_PORT_ACTIVE,
	IBV_EVENT_PORT_ERR,
	IBV_EVENT_LID_CHANGE,
	IBV_EVENT_PKEY_CHANGE,
	IBV_EVENT_SM_CHANGE,
	IBV_EVENT_SRQ_ERR,
	IBV_EVENT_SRQ_LIMIT_REACHED,
	IBV_EVENT_QP_LAST_WQE_REACHED,
	IBV_EVENT_CLIENT_REREGISTER
};

struct ibv_async_event
{
	union
	{
		struct ibv_cq  *cq;
		struct ibv_qp  *qp;
		struct ibv_srq *srq;
		int				port_num;

	}	element;
	enum ibv_event_type	event_type;
};

enum ibv_wc_status
{
	IBV_WC_SUCCESS				= WvWcSuccess,
	IBV_WC_LOC_LEN_ERR			= WvWcLocalLengthError,
	IBV_WC_LOC_QP_OP_ERR		= WvWcLocalOpError,
	IBV_WC_LOC_PROT_ERR			= WvWcLocalProtectionError,
	IBV_WC_WR_FLUSH_ERR			= WvWcFlushed,
	IBV_WC_MW_BIND_ERR			= WvWcMwBindError,
	IBV_WC_REM_ACCESS_ERR		= WvWcRemoteAccessError,
	IBV_WC_REM_OP_ERR			= WvWcRemoteOpError,
	IBV_WC_RNR_RETRY_EXC_ERR	= WvWcRnrRetryError,
	IBV_WC_RESP_TIMEOUT_ERR		= WvWcTimeoutRetryError,
	IBV_WC_REM_INV_REQ_ERR		= WvWcRemoteInvalidRequest,
	IBV_WC_BAD_RESP_ERR			= WvWcBadResponse,
	IBV_WC_LOC_ACCESS_ERR		= WvWcLocalAccessError,
	IBV_WC_GENERAL_ERR			= WvWcError,
	IBV_WC_FATAL_ERR			= -1,
	IBV_WC_RETRY_EXC_ERR		= -2,
	IBV_WC_REM_ABORT_ERR		= -3,
	IBV_WC_LOC_EEC_OP_ERR		= -4,
	IBV_WC_LOC_RDD_VIOL_ERR		= -5,
	IBV_WC_REM_INV_RD_REQ_ERR	= -6,
	IBV_WC_INV_EECN_ERR			= -7,
	IBV_WC_INV_EEC_STATE_ERR	= -8

};

__declspec(dllexport)
const char *ibv_wc_status_str(enum ibv_wc_status status);

enum ibv_wc_opcode
{
	IBV_WC_SEND					= WvSend,
	IBV_WC_RDMA_WRITE			= WvRdmaWrite,
	IBV_WC_RDMA_READ			= WvRdmaRead,
	IBV_WC_COMP_SWAP			= WvCompareExchange,
	IBV_WC_FETCH_ADD			= WvFetchAdd,
	IBV_WC_BIND_MW				= WvBindWindow,
/*
 * Set value of IBV_WC_RECV so consumers can test if a completion is a
 * receive by testing (opcode & IBV_WC_RECV).
 */
	IBV_WC_RECV					= WvReceive,
	IBV_WC_RECV_RDMA_WITH_IMM	= WvReceiveRdmaWrite
};

enum ibv_wc_flags
{
	IBV_WC_GRH			= WV_WC_GRH_VALID,
	IBV_WC_WITH_IMM		= WV_WC_IMMEDIATE
};

struct ibv_wc
{
	union
	{
		uint32_t			qp_num;
		void*				reserved;
	};
	uint64_t			wr_id;
	enum ibv_wc_opcode	opcode;
	uint32_t			byte_len;
	uint32_t			vendor_err2;
	uint32_t			vendor_err;
	enum ibv_wc_status	status;

	int					wc_flags;
	uint32_t			imm_data;	/* in network byte order */
	uint32_t			src_qp;
	uint16_t			pkey_index;
	uint16_t			slid;
	uint8_t				sl;
	uint8_t				dlid_path_bits;
};

enum ibv_access_flags
{
	IBV_ACCESS_LOCAL_WRITE		= WV_ACCESS_LOCAL_WRITE,
	IBV_ACCESS_REMOTE_WRITE		= WV_ACCESS_REMOTE_WRITE,
	IBV_ACCESS_REMOTE_READ		= WV_ACCESS_REMOTE_READ,
	IBV_ACCESS_REMOTE_ATOMIC	= WV_ACCESS_REMOTE_ATOMIC,
	IBV_ACCESS_MW_BIND			= WV_ACCESS_MW_BIND
};

struct ibv_pd
{
	struct ibv_context	*context;
	IWVProtectionDomain	*handle;
};

/* Reregister MR not supported by WinVerbs */
enum ibv_rereg_mr_flags
{
	IBV_REREG_MR_CHANGE_TRANSLATION	= (1 << 0),
	IBV_REREG_MR_CHANGE_PD			= (1 << 1),
	IBV_REREG_MR_CHANGE_ACCESS		= (1 << 2),
	IBV_REREG_MR_KEEP_VALID			= (1 << 3)
};

struct ibv_mr
{
	struct ibv_context	*context;
	struct ibv_pd		*pd;
	void				*addr;
	size_t				length;
	uint32_t			lkey;
	uint32_t			rkey;
};

/* Memory windows not implemented by WinVerbs */
enum ibv_mw_type
{
	IBV_MW_TYPE_1		= 1,
	IBV_MW_TYPE_2		= 2
};

/* Memory windows not implemented by WinVerbs */
struct ibv_mw
{
	struct ibv_context	*context;
	struct ibv_pd		*pd;
	uint32_t			rkey;
};

struct ibv_global_route
{
	union ibv_gid		dgid;
	uint32_t			flow_label;
	uint8_t				sgid_index;
	uint8_t				hop_limit;
	uint8_t				traffic_class;
};

struct ibv_grh
{
	uint32_t			version_tclass_flow;
	uint16_t			paylen;
	uint8_t				next_hdr;
	uint8_t				hop_limit;
	union ibv_gid		sgid;
	union ibv_gid		dgid;
};

enum ibv_rate
{
	IBV_RATE_MAX      = 0,
	IBV_RATE_2_5_GBPS = 2,
	IBV_RATE_5_GBPS   = 5,
	IBV_RATE_10_GBPS  = 3,
	IBV_RATE_20_GBPS  = 6,
	IBV_RATE_30_GBPS  = 4,
	IBV_RATE_40_GBPS  = 7,
	IBV_RATE_60_GBPS  = 8,
	IBV_RATE_80_GBPS  = 9,
	IBV_RATE_120_GBPS = 10
};

/**
 * ibv_rate_to_mult - Convert the IB rate enum to a multiple of the
 * base rate of 2.5 Gbit/sec.  For example, IBV_RATE_5_GBPS will be
 * converted to 2, since 5 Gbit/sec is 2 * 2.5 Gbit/sec.
 * @rate: rate to convert.
 */
__declspec(dllexport)
int ibv_rate_to_mult(enum ibv_rate rate);

/**
 * mult_to_ibv_rate - Convert a multiple of 2.5 Gbit/sec to an IB rate enum.
 * @mult: multiple to convert.
 */
__declspec(dllexport)
enum ibv_rate mult_to_ibv_rate(int mult);

struct ibv_ah_attr
{
	struct ibv_global_route	grh;
	uint16_t				dlid;
	uint8_t					sl;
	uint8_t					src_path_bits;
	uint8_t					static_rate;
	uint8_t					is_global;
	uint8_t					port_num;
};

enum ibv_srq_attr_mask
{
	IBV_SRQ_MAX_WR	= 1 << 0,
	IBV_SRQ_LIMIT	= 1 << 1
};

struct ibv_srq_attr
{
	uint32_t		max_wr;
	uint32_t		max_sge;
	uint32_t		srq_limit;
};

struct ibv_srq_init_attr
{
	void				*srq_context;
	struct ibv_srq_attr	attr;
};

enum ibv_qp_type
{
	IBV_QPT_RC = WvQpTypeRc,
	IBV_QPT_UC = WvQpTypeUc,
	IBV_QPT_UD = WvQpTypeUd
};

struct ibv_qp_cap
{
	uint32_t		max_send_wr;
	uint32_t		max_recv_wr;
	uint32_t		max_send_sge;
	uint32_t		max_recv_sge;
	uint32_t		max_inline_data;
};

struct ibv_qp_init_attr
{
	void				*qp_context;
	struct ibv_cq		*send_cq;
	struct ibv_cq		*recv_cq;
	struct ibv_srq		*srq;
	struct ibv_qp_cap	cap;
	enum ibv_qp_type	qp_type;
	int					sq_sig_all;
};

enum ibv_qp_attr_mask
{
	IBV_QP_STATE				= WV_QP_ATTR_STATE,
	IBV_QP_CUR_STATE			= WV_QP_ATTR_CURRENT_STATE,
	IBV_QP_EN_SQD_ASYNC_NOTIFY	= WV_QP_ATTR_FLAGS,
	IBV_QP_ACCESS_FLAGS			= WV_QP_ATTR_ACCESS_FLAGS,
	IBV_QP_PKEY_INDEX			= WV_QP_ATTR_PKEY_INDEX,
	IBV_QP_PORT					= WV_QP_ATTR_PORT_NUMBER,
	IBV_QP_QKEY					= WV_QP_ATTR_QKEY,
	IBV_QP_AV					= WV_QP_ATTR_AV,
	IBV_QP_PATH_MTU				= WV_QP_ATTR_AV,
	IBV_QP_TIMEOUT				= WV_QP_ATTR_ACK_TIMEOUT,
	IBV_QP_RETRY_CNT			= WV_QP_ATTR_ERROR_RETRY_COUNT,
	IBV_QP_RNR_RETRY			= WV_QP_ATTR_RNR_RETRY_COUNT,
	IBV_QP_RQ_PSN				= WV_QP_ATTR_RECEIVE_PSN,
	IBV_QP_MAX_QP_RD_ATOMIC		= WV_QP_ATTR_INITIATOR_DEPTH,
	IBV_QP_ALT_PATH				= WV_QP_ATTR_ALTERNATE_AV,
	IBV_QP_MIN_RNR_TIMER		= WV_QP_ATTR_RNR_NAK_TIMEOUT,
	IBV_QP_SQ_PSN				= WV_QP_ATTR_SEND_PSN,
	IBV_QP_MAX_DEST_RD_ATOMIC	= WV_QP_ATTR_RESPONDER_RESOURCES,
	IBV_QP_PATH_MIG_STATE		= WV_QP_ATTR_PATH_MIG_STATE,
	IBV_QP_CAP					= WV_QP_ATTR_CAPABILITIES,
	IBV_QP_DEST_QPN				= WV_QP_ATTR_DESTINATION_QPN
};

enum ibv_qp_state
{
	IBV_QPS_RESET	= WvQpStateReset,
	IBV_QPS_INIT	= WvQpStateInit,
	IBV_QPS_RTR		= WvQpStateRtr,
	IBV_QPS_RTS		= WvQpStateRts,
	IBV_QPS_SQD		= WvQpStateSqd,
	IBV_QPS_SQE		= WvQpStateSqError,
	IBV_QPS_ERR		= WvQpStateError
};

enum ibv_mig_state
{
	IBV_MIG_MIGRATED	= WvApmMigrated,
	IBV_MIG_REARM		= WvApmRearm,
	IBV_MIG_ARMED		= WvApmArmed
};

struct ibv_qp_attr
{
	enum ibv_qp_state	qp_state;
	enum ibv_qp_state	cur_qp_state;
	enum ibv_mtu		path_mtu;
	enum ibv_mig_state	path_mig_state;
	uint32_t			qkey;
	uint32_t			rq_psn;
	uint32_t			sq_psn;
	uint32_t			dest_qp_num;
	int					qp_access_flags;
	struct ibv_qp_cap	cap;
	struct ibv_ah_attr	ah_attr;
	struct ibv_ah_attr	alt_ah_attr;
	uint16_t			pkey_index;
	uint16_t			alt_pkey_index;
	uint8_t				en_sqd_async_notify;
	uint8_t				sq_draining;
	uint8_t				max_rd_atomic;
	uint8_t				max_dest_rd_atomic;
	uint8_t				min_rnr_timer;
	uint8_t				port_num;
	uint8_t				timeout;
	uint8_t				retry_cnt;
	uint8_t				rnr_retry;
	uint8_t				alt_port_num;
	uint8_t				alt_timeout;
};

enum ibv_wr_opcode
{
	IBV_WR_SEND = WvSend,
	IBV_WR_RDMA_WRITE = WvRdmaWrite,
	IBV_WR_RDMA_READ = WvRdmaRead,
	IBV_WR_ATOMIC_CMP_AND_SWP = WvCompareExchange,
	IBV_WR_ATOMIC_FETCH_AND_ADD = WvFetchAdd,
	IBV_WR_SEND_WITH_IMM = WvSend | 0x80000000,
	IBV_WR_RDMA_WRITE_WITH_IMM = WvRdmaWrite | 0x80000000,
};

enum ibv_send_flags
{
	IBV_SEND_FENCE		= WV_SEND_FENCE,
	IBV_SEND_SIGNALED	= WV_SEND_SIGNALED,
	IBV_SEND_SOLICITED	= WV_SEND_SOLICITED,
	IBV_SEND_INLINE		= WV_SEND_INLINE
};

struct ibv_sge
{
	uint64_t		addr;
	uint32_t		length;
	uint32_t		lkey;
};

struct ibv_send_wr
{
	uint64_t				wr_id;
	struct ibv_send_wr		*next;
	struct ibv_sge			*sg_list;
	int						num_sge;
	enum ibv_wr_opcode		opcode;
	int						send_flags;
	uint32_t				imm_data;	/* in network byte order */
	union
	{
		struct
		{
			uint64_t		remote_addr;
			uint32_t		rkey;

		}	rdma;
		struct
		{
			uint64_t		remote_addr;
			uint32_t		rkey;
			uint64_t		compare_add;
			uint64_t		swap;

		}	atomic;
		struct
		{
			struct ibv_ah  *ah;
			uint32_t		remote_qpn;
			uint32_t		remote_qkey;

		}	ud;
	}	wr;
};

struct ibv_recv_wr
{
	uint64_t				wr_id;
	struct ibv_recv_wr     *next;
	struct ibv_sge	       *sg_list;
	int						num_sge;
};

/* Memory windows not implemented by WinVerbs */
struct ibv_mw_bind
{
	uint64_t				wr_id;
	struct ibv_mr			*mr;
	void					*addr;
	size_t					length;
	int						send_flags;
	int						mw_access_flags;
};

struct ibv_srq
{
	struct ibv_context		*context;
	void					*srq_context;
	struct ibv_pd			*pd;
	IWVSharedReceiveQueue	*handle;
};

struct ibv_qp
{
	struct ibv_context		*context;
	void					*qp_context;
	struct ibv_pd			*pd;
	struct ibv_cq			*send_cq;
	struct ibv_cq			*recv_cq;
	struct ibv_srq			*srq;
	IWVQueuePair			*handle;
	union
	{
		IWVDatagramQueuePair	*ud_handle;
		IWVConnectQueuePair		*conn_handle;
	};
	uint32_t				qp_num;
	enum ibv_qp_state       state;
	enum ibv_qp_type		qp_type;
};

struct ibv_comp_channel
{
	struct ibv_context		*context;
	COMP_CHANNEL			comp_channel;
};

struct ibv_cq
{
	struct ibv_context		*context;
	struct ibv_comp_channel *channel;
	void					*cq_context;
	IWVCompletionQueue		*handle;
	int						cqe;
	COMP_ENTRY				comp_entry;
	LONG volatile			notify_cnt;
	LONG volatile			ack_cnt;
};

struct ibv_ah
{
	struct ibv_context		*context;
	struct ibv_pd			*pd;
	IWVAddressHandle		*handle;
	ULONG_PTR				key;
};

struct ibv_device;
struct ibv_context;

enum
{
	IBV_SYSFS_NAME_MAX	= 64
};

struct ibv_device
{
	enum ibv_node_type		node_type;
	enum ibv_transport_type	transport_type;
	char					name[IBV_SYSFS_NAME_MAX];
};

struct ibv_context
{
	struct ibv_device		*device;
	IWVDevice				*cmd_if;
	COMP_CHANNEL			channel;
};

/**
 * ibv_get_device_list - Get list of IB devices currently available
 * @num_devices: optional.  if non-NULL, set to the number of devices
 * returned in the array.
 *
 * Return a NULL-terminated array of IB devices.  The array can be
 * released with ibv_free_device_list().
 */
__declspec(dllexport)
struct ibv_device **ibv_get_device_list(int *num_devices);

/**
 * ibv_free_device_list - Free list from ibv_get_device_list()
 *
 * Free an array of devices returned from ibv_get_device_list().  Once
 * the array is freed, pointers to devices that were not opened with
 * ibv_open_device() are no longer valid.  Client code must open all
 * devices it intends to use before calling ibv_free_device_list().
 */
__declspec(dllexport)
void ibv_free_device_list(struct ibv_device **list);

/**
 * ibv_get_device_name - Return kernel device name
 */
__declspec(dllexport)
const char *ibv_get_device_name(struct ibv_device *device);

/**
 * ibv_get_device_guid - Return device's node GUID
 */
__declspec(dllexport)
uint64_t ibv_get_device_guid(struct ibv_device *device);

/**
 * ibv_open_device - Initialize device for use
 */
__declspec(dllexport)
struct ibv_context *ibv_open_device(struct ibv_device *device);

/**
 * ibv_close_device - Release device
 */
__declspec(dllexport)
int ibv_close_device(struct ibv_context *context);

/**
 * ibv_get_async_event - Get next async event
 * @event: Pointer to use to return async event
 *
 * All async events returned by ibv_get_async_event() must eventually
 * be acknowledged with ibv_ack_async_event().
 */
__declspec(dllexport)
int ibv_get_async_event(struct ibv_context *context,
						struct ibv_async_event *event);

/**
 * ibv_ack_async_event - Acknowledge an async event
 * @event: Event to be acknowledged.
 *
 * All async events which are returned by ibv_get_async_event() must
 * be acknowledged.  To avoid races, destroying an object (CQ, SRQ or
 * QP) will wait for all affiliated events to be acknowledged, so
 * there should be a one-to-one correspondence between acks and
 * successful gets.
 */
__declspec(dllexport)
void ibv_ack_async_event(struct ibv_async_event *event);

/**
 * ibv_query_device - Get device properties
 */
__declspec(dllexport)
int ibv_query_device(struct ibv_context *context,
					 struct ibv_device_attr *device_attr);

/**
 * ibv_query_port - Get port properties
 */
__declspec(dllexport)
int ibv_query_port(struct ibv_context *context, uint8_t port_num,
				   struct ibv_port_attr *port_attr);

/**
 * ibv_query_gid - Get a GID table entry
 */
__declspec(dllexport)
int ibv_query_gid(struct ibv_context *context, uint8_t port_num,
				  int index, union ibv_gid *gid);

/**
 * ibv_query_pkey - Get a P_Key table entry
 */
__declspec(dllexport)
int ibv_query_pkey(struct ibv_context *context, uint8_t port_num,
				   int index, uint16_t *pkey);

/**
 * ibv_alloc_pd - Allocate a protection domain
 */
__declspec(dllexport)
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context);

/**
 * ibv_dealloc_pd - Free a protection domain
 */
__declspec(dllexport)
int ibv_dealloc_pd(struct ibv_pd *pd);

/**
 * ibv_reg_mr - Register a memory region
 */
__declspec(dllexport)
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr,
						  size_t length, int access);

/**
 * ibv_dereg_mr - Deregister a memory region
 */
__declspec(dllexport)
int ibv_dereg_mr(struct ibv_mr *mr);

/**
 * ibv_create_comp_channel - Create a completion event channel
 */
__declspec(dllexport)
struct ibv_comp_channel *ibv_create_comp_channel(struct ibv_context *context);

/**
 * ibv_destroy_comp_channel - Destroy a completion event channel
 */
__declspec(dllexport)
int ibv_destroy_comp_channel(struct ibv_comp_channel *channel);

/**
 * ibv_create_cq - Create a completion queue
 * @context - Context CQ will be attached to
 * @cqe - Minimum number of entries required for CQ
 * @cq_context - Consumer-supplied context returned for completion events
 * @channel - Completion channel where completion events will be queued.
 *     May be NULL if completion events will not be used.
 * @comp_vector - Completion vector used to signal completion events.
 *     Must be >= 0 and < context->num_comp_vectors.
 */
__declspec(dllexport)
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
							 void *cq_context,
							 struct ibv_comp_channel *channel,
							 int comp_vector);

/**
 * ibv_resize_cq - Modifies the capacity of the CQ.
 * @cq: The CQ to resize.
 * @cqe: The minimum size of the CQ.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 */
__declspec(dllexport)
int ibv_resize_cq(struct ibv_cq *cq, int cqe);

/**
 * ibv_destroy_cq - Destroy a completion queue
 */
__declspec(dllexport)
int ibv_destroy_cq(struct ibv_cq *cq);

/**
 * ibv_get_cq_event - Read next CQ event
 * @channel: Channel to get next event from.
 * @cq: Used to return pointer to CQ.
 * @cq_context: Used to return consumer-supplied CQ context.
 *
 * All completion events returned by ibv_get_cq_event() must
 * eventually be acknowledged with ibv_ack_cq_events().
 */
__declspec(dllexport)
int ibv_get_cq_event(struct ibv_comp_channel *channel,
					 struct ibv_cq **cq, void **cq_context);

/**
 * ibv_ack_cq_events - Acknowledge CQ completion events
 * @cq: CQ to acknowledge events for
 * @nevents: Number of events to acknowledge.
 *
 * All completion events which are returned by ibv_get_cq_event() must
 * be acknowledged.  To avoid races, ibv_destroy_cq() will wait for
 * all completion events to be acknowledged, so there should be a
 * one-to-one correspondence between acks and successful gets.  An
 * application may accumulate multiple completion events and
 * acknowledge them in a single call to ibv_ack_cq_events() by passing
 * the number of events to ack in @nevents.
 */
__declspec(dllexport)
void ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents);

/**
 * ibv_poll_cq - Poll a CQ for work completions
 * @cq:the CQ being polled
 * @num_entries:maximum number of completions to return
 * @wc:array of at least @num_entries of &struct ibv_wc where completions
 *   will be returned
 *
 * Poll a CQ for (possibly multiple) completions.  If the return value
 * is < 0, an error occurred.  If the return value is >= 0, it is the
 * number of completions returned.  If the return value is
 * non-negative and strictly less than num_entries, then the CQ was
 * emptied.
 */
__declspec(dllexport)
int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);

/**
 * ibv_req_notify_cq - Request completion notification on a CQ.  An
 *   event will be added to the completion channel associated with the
 *   CQ when an entry is added to the CQ.
 * @cq: The completion queue to request notification for.
 * @solicited_only: If non-zero, an event will be generated only for
 *   the next solicited CQ entry.  If zero, any CQ entry, solicited or
 *   not, will generate an event.
 */
__declspec(dllexport)
int ibv_req_notify_cq(struct ibv_cq *cq, int solicited_only);

/**
 * ibv_create_srq - Creates a SRQ associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the SRQ.
 * @srq_init_attr: A list of initial attributes required to create the SRQ.
 *
 * srq_attr->max_wr and srq_attr->max_sge are read the determine the
 * requested size of the SRQ, and set to the actual values allocated
 * on return.  If ibv_create_srq() succeeds, then max_wr and max_sge
 * will always be at least as large as the requested values.
 */
__declspec(dllexport)
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd,
							   struct ibv_srq_init_attr *srq_init_attr);

/**
 * ibv_modify_srq - Modifies the attributes for the specified SRQ.
 * @srq: The SRQ to modify.
 * @srq_attr: On input, specifies the SRQ attributes to modify.  On output,
 *   the current values of selected SRQ attributes are returned.
 * @srq_attr_mask: A bit-mask used to specify which attributes of the SRQ
 *   are being modified.
 *
 * The mask may contain IBV_SRQ_MAX_WR to resize the SRQ and/or
 * IBV_SRQ_LIMIT to set the SRQ's limit and request notification when
 * the number of receives queued drops below the limit.
 */
__declspec(dllexport)
int ibv_modify_srq(struct ibv_srq *srq,
				   struct ibv_srq_attr *srq_attr,
				   int srq_attr_mask);

/**
 * ibv_query_srq - Returns the attribute list and current values for the
 *   specified SRQ.
 * @srq: The SRQ to query.
 * @srq_attr: The attributes of the specified SRQ.
 */
__declspec(dllexport)
int ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr);

/**
 * ibv_destroy_srq - Destroys the specified SRQ.
 * @srq: The SRQ to destroy.
 */
__declspec(dllexport)
int ibv_destroy_srq(struct ibv_srq *srq);

/**
 * ibv_post_srq_recv - Posts a list of work requests to the specified SRQ.
 * @srq: The SRQ to post the work request on.
 * @recv_wr: A list of work requests to post on the receive queue.
 * @bad_recv_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 */
__declspec(dllexport)
int ibv_post_srq_recv(struct ibv_srq *srq,
					  struct ibv_recv_wr *recv_wr,
					  struct ibv_recv_wr **bad_recv_wr);

/**
 * ibv_create_qp - Create a queue pair.
 */
__declspec(dllexport)
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
							 struct ibv_qp_init_attr *qp_init_attr);

/**
 * ibv_modify_qp - Modify a queue pair.
 */
__declspec(dllexport)
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
				  int attr_mask);

/**
 * ibv_query_qp - Returns the attribute list and current values for the
 *   specified QP.
 * @qp: The QP to query.
 * @attr: The attributes of the specified QP.
 * @attr_mask: A bit-mask used to select specific attributes to query.
 * @init_attr: Additional attributes of the selected QP.
 *
 * The qp_attr_mask may be used to limit the query to gathering only the
 * selected attributes.
 */
__declspec(dllexport)
int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
				 int attr_mask,
				 struct ibv_qp_init_attr *init_attr);

/**
 * ibv_destroy_qp - Destroy a queue pair.
 */
__declspec(dllexport)
int ibv_destroy_qp(struct ibv_qp *qp);

/**
 * ibv_post_send - Post a list of work requests to a send queue.
 *
 * If IBV_SEND_INLINE flag is set, the data buffers can be reused
 * immediately after the call returns.
 */
__declspec(dllexport)
int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
				  struct ibv_send_wr **bad_wr);

/**
 * ibv_post_recv - Post a list of work requests to a receive queue.
 */
__declspec(dllexport)
int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
				  struct ibv_recv_wr **bad_wr);

/**
 * ibv_create_ah - Create an address handle.
 */
__declspec(dllexport)
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);

/**
 * ibv_init_ah_from_wc - Initializes address handle attributes from a
 *   work completion.
 * @context: Device context on which the received message arrived.
 * @port_num: Port on which the received message arrived.
 * @wc: Work completion associated with the received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @ah_attr: Returned attributes that can be used when creating an address
 *   handle for replying to the message.
 */
__declspec(dllexport)
int ibv_init_ah_from_wc(struct ibv_context *context, uint8_t port_num,
						struct ibv_wc *wc, struct ibv_grh *grh,
						struct ibv_ah_attr *ah_attr);

/**
 * ibv_create_ah_from_wc - Creates an address handle associated with the
 *   sender of the specified work completion.
 * @pd: The protection domain associated with the address handle.
 * @wc: Work completion information associated with a received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @port_num: The outbound port number to associate with the address.
 *
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
__declspec(dllexport)
struct ibv_ah *ibv_create_ah_from_wc(struct ibv_pd *pd, struct ibv_wc *wc,
									 struct ibv_grh *grh, uint8_t port_num);

/**
 * ibv_destroy_ah - Destroy an address handle.
 */
__declspec(dllexport)
int ibv_destroy_ah(struct ibv_ah *ah);

/**
 * ibv_attach_mcast - Attaches the specified QP to a multicast group.
 * @qp: QP to attach to the multicast group.  The QP must be a UD QP.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 *
 * In order to route multicast packets correctly, subnet
 * administration must have created the multicast group and configured
 * the fabric appropriately.  The port associated with the specified
 * QP must also be a member of the multicast group.
 */
__declspec(dllexport)
int ibv_attach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid);

/**
 * ibv_detach_mcast - Detaches the specified QP from a multicast group.
 * @qp: QP to detach from the multicast group.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 */
__declspec(dllexport)
int ibv_detach_mcast(struct ibv_qp *qp, union ibv_gid *gid, uint16_t lid);

/**
 * ibv_node_type_str - Return string describing node_type enum value
 */
__declspec(dllexport)
const char *ibv_node_type_str(enum ibv_node_type node_type);

/**
 * ibv_port_state_str - Return string describing port_state enum value
 */
__declspec(dllexport)
const char *ibv_port_state_str(enum ibv_port_state port_state);

/**
 * ibv_event_type_str - Return string describing event_type enum value
 */
__declspec(dllexport)
const char *ibv_event_type_str(enum ibv_event_type event);

/*
 * Windows specific structures and interfaces
 */
struct ibvw_windata
{
	IWVProvider		*prov;
	COMP_MANAGER	*comp_mgr;
};

#define IBVW_WINDATA_VERSION 1

__declspec(dllexport)
int ibvw_get_windata(struct ibvw_windata *windata, int version);

__declspec(dllexport)
void ibvw_release_windata(struct ibvw_windata *windata, int version);

__declspec(dllexport)
int ibvw_wv_errno(HRESULT hr);

#ifdef __cplusplus
}
#endif

#endif /* INFINIBAND_VERBS_H */
