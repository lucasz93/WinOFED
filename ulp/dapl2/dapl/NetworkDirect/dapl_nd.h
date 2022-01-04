/*
 * Copyright (c) 2013 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 */
/* 
 * Definitions specific to DAPL over NetworkDirect (ND) provider.
 */

#ifndef _DAPL_ND_H_
#define _DAPL_ND_H_

#define NETWORKDIRECT 1

#include "precomp.h"

#define ND_FLUSHED 0x10000L	/* undocumented ND error code */
#define ND_DISCONNECTED 0xc000020C 

#ifdef DAT_EXTENSIONS
#include <dat2/dat_ib_extensions.h>
#endif

#ifndef __cplusplus
#define false 0
#define true  1
#endif

/* Typedefs map common DAPL types to NetworkDirect constructs */

typedef	IND2Adapter		*ib_hca_handle_t;
typedef	struct dapl_nd_qp	*ib_qp_handle_t;
typedef struct dapl_nd_cq	*ib_cq_handle_t;
typedef	struct dapl_nd_mw	*ib_mw_handle_t;
typedef	struct dapl_nd_mr	*ib_mr_handle_t;
typedef	struct dapl_nd_io_result ib_work_completion_t;

typedef void  			*ib_gid_handle_t;
typedef	void			*ib_pd_handle_t;
typedef void			*ib_error_record_t;

typedef int 			ib_send_op_type_t;
typedef	ND2_SGE			ib_data_segment_t;
typedef int			ib_qp_state_t;
typedef	int			ib_async_event_type;
typedef int			ib_bool_t;
typedef int			GID;
typedef char			*IB_HCA_NAME;
typedef uint16_t		ib_hca_port_t;
typedef int 			ib_shm_transport_t; /* shared memory support */

#define MAX_SCATTER_GATHER_ENTRIES_ALLOC 16 

#define IB_INVALID_HANDLE	NULL
#define DAPL_ND_MAX_DEV_NAME 32

/* IB connection events - used in dapl_cr_callback.c */
typedef enum {
	IB_CME_CONNECTED,
	IB_CME_DISCONNECTED,
	IB_CME_DISCONNECTED_ON_LINK_DOWN,
	IB_CME_CONNECTION_REQUEST_PENDING,
	IB_CME_CONNECTION_REQUEST_PENDING_PRIVATE_DATA,
	IB_CME_CONNECTION_REQUEST_ACKED,
	IB_CME_DESTINATION_REJECT,
	IB_CME_DESTINATION_REJECT_PRIVATE_DATA,
	IB_CME_DESTINATION_UNREACHABLE,
	IB_CME_TOO_MANY_CONNECTION_REQUESTS,
	IB_CME_LOCAL_FAILURE,
	IB_CME_BROKEN,
	IB_CME_TIMEOUT,
	IB_CME_REPLY_RECEIVED,
	IB_CME_REPLY_RECEIVED_PRIVATE_DATA

} ib_cm_events_t;

/* CQ notifications */
typedef enum
{
	IB_NOTIFY_ON_NEXT_COMP,
	IB_NOTIFY_ON_SOLIC_COMP

} ib_notification_type_t;

/* CQEntry opcode / DTO OPs, ordered for DAPL ENUM definitions */
#define OP_RDMA_WRITE           0
#define OP_RDMA_WRITE_IMM       1
#define OP_SEND                 2
#define OP_SEND_IMM             3
#define OP_RDMA_READ            4
#define OP_COMP_AND_SWAP        5
#define OP_FETCH_AND_ADD        6
#define OP_RECEIVE              7   /* internal op */
#define OP_RECEIVE_IMM		8   /* rdma write with immed, internel op */
#define OP_RECEIVE_MSG_IMM	9   /* recv msg with immed, internel op */
#define OP_BIND_MW              10   /* internal op */
#define OP_SEND_UD              11  /* internal op */
#define OP_RECV_UD              12  /* internal op */
#define OP_INVALID		0xff

/* Definitions to map QP state */
#define IB_QP_STATE_RESET	0
#define IB_QP_STATE_INIT	1
#define IB_QP_STATE_RTR		2
#define IB_QP_STATE_RTS		3
#define IB_QP_STATE_SQD		4
#define IB_QP_STATE_SQE		5
#define IB_QP_STATE_ERROR	6

#define IB_QPT_RC 0

enum ibv_mtu
{
	IBV_MTU_256  = 1,
	IBV_MTU_512  = 2,
	IBV_MTU_1024 = 3,
	IBV_MTU_2048 = 4,
	IBV_MTU_4096 = 5
};

/* async handler for DTO, CQ, QP, and unafiliated */
typedef void (*ib_async_dto_handler_t)(
    IN    ib_hca_handle_t    ib_hca_handle,
    IN    ib_error_record_t  *err_code,
    IN    void               *context);

typedef void (*ib_async_cq_handler_t)(
    IN    ib_hca_handle_t    ib_hca_handle,
    IN    ib_cq_handle_t     ib_cq_handle,
    IN    ib_error_record_t  *err_code,
    IN    void               *context);

typedef void (*ib_async_qp_handler_t)(
    IN    ib_hca_handle_t    ib_hca_handle,
    IN    ib_qp_handle_t     ib_qp_handle,
    IN    ib_error_record_t  *err_code,
    IN    void               *context);

typedef void (*ib_async_handler_t)(
    IN    ib_hca_handle_t    ib_hca_handle,
    IN    ib_error_record_t  *err_code,
    IN    void               *context);


/* prototypes */
int dapls_ib_init(void);
int dapls_ib_release(void);

enum ibv_mtu dapl_ib_mtu(int mtu);
char *dapl_ib_mtu_str(enum ibv_mtu mtu);

extern void dapl_llist_init_head (
    DAPL_LLIST_HEAD * 	head);

extern DAT_BOOLEAN dapl_llist_is_empty (
    DAPL_LLIST_HEAD * 	head);

extern void dapl_llist_add_tail (
    DAPL_LLIST_HEAD * 	head,
    DAPL_LLIST_ENTRY * 	entry,
    void * 		data);

extern void * dapl_llist_remove_head (
    DAPL_LLIST_HEAD *	head);


/* cm_id connection states */
typedef enum dapl_cm_state
{
	DCM_INIT,
	DCM_LISTEN,
	DCM_CONN_PENDING,
	DCM_REP_PENDING,
	DCM_ACCEPTING,
	DCM_ACCEPTING_DATA,
	DCM_ACCEPTED,
	DCM_REJECTING,
	DCM_REJECTED,
	DCM_CONNECTED,
	DCM_RELEASE,
	DCM_DISCONNECTING,
	DCM_DISCONNECTED,
	DCM_DESTROY,
	DCM_RTU_PENDING,
	DCM_DISC_RECV,
	DCM_FREE,

} DAPL_CM_STATE;


/* ND operation type used in DAPL_OVERLAPPED struct to identify type of I/O
 * operations which has just completed; IOCompletion processing.
 */
typedef enum dapl_nd_io_request_type
{
	IOR_GetOverLapResult=1,
	IOR_GetConnReq,
	IOR_Accept,
	IOR_Connect,
	IOR_CompleteConnect,
	IOR_SendQ,
	IOR_RecvQ,
	IOR_Disconnect,
	IOR_DisconnectNotify,
	IOR_CQNotify,
	IOR_CQArm,
	IOR_Send,
	IOR_Recv,
	IOR_RDMA_Read,
	IOR_RDMA_Write,
	IOR_MemoryRegion,
	IOR_MAX

} DAPL_ND_IOR;


/* DAPL OVERLAPPED struct for ND I/O completion callback dispatch */
typedef struct _dapl_overlapped {
	OVERLAPPED	Ov;	// std MS OVERLAPPED
	DAPL_ND_IOR	ior_type;
	void		*context;

} DAPL_OVERLAPPED;


typedef struct dapl_nd_cq
{
	DAPL_OVERLAPPED		OverLap;
	IND2CompletionQueue	*ND_CQ;
	SIZE_T			depth;
	void			*context;	// evd_ptr
	struct dapl_nd_qp	*h_qp;		// set @ QP creation.
//	DAPL_LLIST_HEAD		ND_Results;
//	DAT_COUNT		num_results;
	DAPL_ATOMIC		armed;		// expected # of Notify callbacks
	HANDLE			Event;		// waitfor notify callback
	DWORD			Magic;

} DAPL_ND_CQ;

#define DAPL_CQ_MAGIC 0xcafebabe
	
typedef struct dapl_nd_qp
{
	IND2QueuePair		*ND_QP;
	IND2Adapter		*pAdapter;
	struct dapl_cm_id	*pConn;
	DAPL_ND_CQ		*pSendCQ;
	DAPL_ND_CQ		*pRecvCQ;
	DAPL_LLIST_HEAD		PrePostedRx;/* pre-connect posted Rx bufs */
	DAPL_ATOMIC		posted_recvs;
	int			qp_type;
	ULONG			recv_queue_depth;
	ULONG			send_queue_depth;
	ULONG			recv_max_sge;
	ULONG			send_max_sge;
	ULONG			max_inline_send;

} DAPL_ND_QP;
	

typedef struct dapl_nd_mw	// memory window
{
	IND2MemoryWindow	ND_MemWindow;
	struct dapl_nd_mr	*h_mr;
	DAPL_ND_QP		*h_qp;
	IND2Adapter		*pAdapter;
	ND2_RESULT		nd_result;

} DAPL_ND_MW;


typedef struct dapl_nd_mr	// memory region
{
	DAPL_OVERLAPPED		OverLap;
	IND2MemoryRegion	*ND_MR;
	UINT32			l_key;
	ND2_RESULT		nd_result;

} DAPL_ND_MR;


typedef struct dapl_nd_io_result
{
	// Need to keep upper level DAPL code happy...sigh XXX
	DAT_UINT64		wr_id;
	uint32_t		opcode;
	uint32_t		byte_len;
	uint32_t		vendor_err;
	uint32_t		vendor_err2;
	int			status;
	int			wc_flags;

}  DAPL_ND_IO_RESULT;


#define DAPL_RX_MAGIC 0xcafe
#define DAPL_TX_MAGIC 0xbabe

#define IB_RC_RETRY_COUNT       7
#define IB_RNR_RETRY_COUNT      7
#define IB_CM_RESPONSE_TIMEOUT  23	/* 16 sec */
#define IB_CM_RETRIES           15	/* 240 sec total default */
#define IB_ARP_TIMEOUT		4000	/* 4 sec */
#define IB_ARP_RETRY_COUNT	15	/* 60 sec total */
#define IB_ROUTE_TIMEOUT	4000	/* 4 sec */
#define IB_ROUTE_RETRY_COUNT	15	/* 60 sec total */
#define IB_MAX_AT_RETRY		3

/* CMA private data areas, use CMA max with known transport definitions */
#define RDMA_MAX_PRIVATE_DATA 64 

#define CMA_PDATA_HDR		36
#define	IB_MAX_REQ_PDATA_SIZE	DAPL_MIN((92-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)
#define	IB_MAX_REP_PDATA_SIZE	DAPL_MIN((196-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)
#define	IB_MAX_REJ_PDATA_SIZE	DAPL_MIN((148-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)
#define	IB_MAX_DREQ_PDATA_SIZE	DAPL_MIN((220-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)
#define	IB_MAX_DREP_PDATA_SIZE	DAPL_MIN((224-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)
#define	IWARP_MAX_PDATA_SIZE	DAPL_MIN((512-CMA_PDATA_HDR),RDMA_MAX_PRIVATE_DATA)

/* DAPL CM objects MUST include list_entry, ref_count, event for EP linking */

struct dapl_cm_id {
	struct dapl_llist_entry		list_entry;
	DAPL_CM_STATE			ConnState;
	DAPL_OS_WAIT_OBJECT		Event;
	DAPL_OS_LOCK			lock;
	int				ref_count;
	int				arp_retries;
	int				arp_timeout;
	int				route_retries;
	int				route_timeout;
	DAPL_HCA			*hca;
	DAPL_SP				*sp;
	DAPL_EP				*ep;
	DAT_SOCK_ADDR6			remote_addr;
	DAT_SOCK_ADDR6			local_addr;
	SIZE_T				p_len;
	UCHAR				p_data[DAPL_MAX_PRIVATE_DATA_SIZE];
	SIZE_T				PeerInboundReadLimit;
	SIZE_T				PeerOutboundReadLimit;
	IND2Adapter			*pAdapter;   /* ND adapter */
	IND2Listener		*pListen;
	IND2Connector		*pConnector;
	IND2Connector		*pConnector_dc;
	HANDLE				OvFileHandle;/* copy of AdapterOvFileHdl*/
	OVERLAPPED			serial;
	DAPL_OVERLAPPED		OverLap;
	DAPL_OVERLAPPED		DisconnectNotify;
	DAPL_ATOMIC			GetConnReq;	// # of expected callbacks
	DAPL_ND_QP			*h_qp;
};

typedef struct dapl_cm_id	*dp_ib_cm_handle_t;
typedef struct dapl_cm_id	*ib_cm_srvc_handle_t;
typedef struct dapl_cm_id	DAPL_CM_ID;

/* ib_hca_transport_t, specific to this Provider implementation */

typedef struct _ib_hca_transport
{
	struct dapl_llist_entry	entry;
	DAPL_ATOMIC		num_connections;
	int			arp_retries;
	int			arp_timeout;
	int			route_retries;
	int			route_timeout;

	/* device attributes */
	HANDLE			AdapterOvFileHandle;	// for Overlapped operations
	char			dev_name[DAPL_ND_MAX_DEV_NAME];
	ND2_ADAPTER_INFO	nd_adapter_info;
	ib_hca_handle_t		ib_ctx;
	DAT_NAMED_ATTR		named_attr;

	uint8_t			max_cm_timeout;	// getenv() setable
	uint8_t			max_cm_retries;	// getenv() setable
	uint8_t			mtu;
	uint8_t			global;
#ifdef DEFINE_ATTR_LINK_LAYER
	uint8_t			transport_type;// TODO:IBV_TRANSPORT_IB or iWARP, ?
	uint8_t			link_layer;// TODO ?
#endif

#ifdef DAT_IB_COLLECTIVES
	/* Collective member device and address information */
	ib_thread_state_t 	coll_thread_state;
	DAPL_OS_THREAD 		coll_thread;
	DAPL_OS_LOCK 		coll_lock;
	DAPL_OS_WAIT_OBJECT 	coll_event;
	struct dapl_llist_entry *grp_list;
	user_progress_func_t 	*user_func;
	int 			l_sock;
	struct sockaddr_in	m_addr;
	void 			*m_ctx;
	void			*m_info;
	void			*f_info;
	int			m_size;
	int			f_size;
	int			t_id;
#endif

}  ib_hca_transport_t;


/* prototypes */

char *DTO_error_str(DAT_DTO_COMPLETION_STATUS ds);
char * ND_error_str(HRESULT hr);
char * ND_RequestType_str(ND2_REQUEST_TYPE rqt);
DAT_RETURN dapl_cvt_ND_to_DAT_status(HRESULT hr);
char * dapli_IPaddr_str(DAT_SOCK_ADDR *sa, OPTIONAL char *buf);
char * dapli_IPv4_addr_str(DAT_SOCK_ADDR6 *ipa, char *buf);
char * dapli_IPv6_addr_str(DAT_SOCK_ADDR6 *ipa, char *buf);
char * dapli_IOR_str(DAPL_ND_IOR);
char * dapli_cm_state_str(DAPL_CM_STATE);
char * dapli_ib_cm_event_str(ib_cm_events_t);

DAPL_CM_ID *dapls_alloc_cm_id(struct dapl_hca *hca, HANDLE ovf);
void dapls_cm_acquire(dp_ib_cm_handle_t cm);
void dapls_cm_release(dp_ib_cm_handle_t cm);
void dapls_cm_free(dp_ib_cm_handle_t cm_ptr);
void IOCompletion_cb( __in DWORD, __in DWORD, __inout LPOVERLAPPED );
DAT_RETURN dapli_queue_recv(
		DAPL_EP		*ep_ptr,
		DAPL_COOKIE	*cookie,
		DAT_COUNT	segments,
		DAT_LMR_TRIPLET	*local_iov );
void dapli_post_queued_recvs( DAPL_ND_QP *h_qp );


#endif /*  _DAPL_ND_H_ */
