/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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
#ifndef _VNIC_IB_H_
#define _VNIC_IB_H_


#include <iba/ib_al.h>
#include <complib/comp_lib.h>
#include "vnic_trailer.h"
struct Io;

typedef void (CompRoutine_t)(struct Io *pIo);

#define MAX_HCAS	4
#define MAX_NUM_SGE 8

#define	MAX_PHYS_MEMORY   0xFFFFFFFFFFFFFFFF
#define CONTROL_SID		0
#define DATA_SID		1

#include <complib/cl_packon.h>
typedef union _vnic_sid {
	uint64_t	as_uint64;
	struct {
		uint8_t		base_id; /* base id for vnic is 0x10 */
		uint8_t		oui[3];  /* OUI */
		uint16_t	reserved; /* should be zero */
		uint8_t		type;     /* control or data */
		uint8_t		ioc_num;  /* ioc number */
	}s;
} vnic_sid_t;

typedef union _vnic_ioc_guid {
	uint64_t	as_uint64;
	struct {
		uint8_t		oui[3];
		uint8_t		ioc_num;
		uint32_t	counter;	/* SST device type: 8 bits, counter:24 bits */
	}s;
} vnic_ioc_guid_t;
#include <complib/cl_packoff.h>

typedef enum {
	IB_UNINITTED = 0,
	IB_INITTED,
	IB_ATTACHING,
	IB_ATTACHED,
	IB_DETACHING,
	IB_DETACHED,
	IB_DISCONNECTED
} IbState_t;
#pragma warning ( disable : 4324 )
typedef struct _vnic_path_record {
	cl_list_item_t		list_entry;
	ib_path_rec_t		path_rec;
} vnic_path_record_t;
#pragma warning( default : 4324 )

typedef struct IbQp {
	LIST_ENTRY		listPtrs;
	struct _viport	*pViport;
	struct IbConfig	*p_conf;
	NDIS_SPIN_LOCK	qpLock;
	volatile LONG	qpState;
	uint32_t		qpNumber;
	struct IbCa		*pCa;
	uint64_t		portGuid;
	ib_qp_handle_t	qp;
	ib_cq_handle_t	cq;
#ifdef VNIC_STATISTIC
	struct {
		int64_t		connectionTime;
		int64_t		rdmaPostTime;
		uint32_t	rdmaPostIos;
		int64_t		rdmaCompTime;
		uint32_t	rdmaCompIos;
		int64_t		sendPostTime;
		uint32_t	sendPostIos;
		int64_t		sendCompTime;
		uint32_t	sendCompIos;
		int64_t		recvPostTime;
		uint32_t	recvPostIos;
		int64_t		recvCompTime;
		uint32_t	recvCompIos;
		uint32_t	numIos;
		uint32_t	numCallbacks;
		uint32_t	maxIos;
	} statistics;
#endif /* VNIC_STATISTIC */
} IbQp_t;

typedef struct IbRegion {
	uint64_t		virtAddress;
	net64_t			len;
	ib_mr_handle_t	h_mr;
	net32_t			lkey;
	net32_t			rkey;
} IbRegion_t;


#define VNIC_CA_MAX_PORTS	2
typedef struct IbCa {
	cl_list_item_t		list_entry;
	net64_t				caGuid;
	ib_pd_handle_t		hPd;
	IbRegion_t			region;
	uint32_t			numPorts;
	uint64_t			portGuids[VNIC_CA_MAX_PORTS];
} IbCa_t;

typedef enum _OpType {
	RECV, 
	RDMA,
	SEND 
} OpType_t;

typedef struct Io {
		LIST_ENTRY			listPtrs;
		struct _viport		*pViport;
		CompRoutine_t		*pRoutine;
		ib_send_wr_t		wrq;
		ib_recv_wr_t		r_wrq;
		ib_wc_status_t		wc_status;
#ifdef VNIC_STATISTIC
		int64_t         time;
#endif /* VNIC_STATISTIC */
		OpType_t		type;
} Io_t;

typedef struct RdmaIo {
		Io_t				io;
		ib_local_ds_t		dsList[MAX_NUM_SGE];
		uint16_t			index;
		uint32_t			len;
		NDIS_PACKET			*p_packet;
		NDIS_BUFFER			*p_buf;
		ULONG				packet_sz;
		struct ViportTrailer *p_trailer;
		uint8_t				data[2* VIPORT_TRAILER_ALIGNMENT];
} RdmaIo_t;

typedef struct SendIo {
		Io_t				io;
		ib_local_ds_t		dsList;
} SendIo_t;

typedef struct RecvIo {
		Io_t				io;
		ib_local_ds_t		dsList;
} RecvIo_t;

void
ibqp_construct(
	IN	OUT		IbQp_t			*pQp,
	IN			struct _viport	*pViport );

ib_api_status_t 
ibqp_init(
	IN		IbQp_t				*pQp, 
	IN		uint64_t			guid,
	IN	OUT	struct IbConfig		*p_conf);

ib_api_status_t 
ibqp_connect(
	IN		IbQp_t				*pQp);

void
ibqp_detach(
	IN		IbQp_t				*pQp);

void
ibqp_cleanup(
	IN		IbQp_t				*pQp);

ib_api_status_t
ibqp_postSend(
	IN		IbQp_t		*pQp,
	IN		Io_t		*pIo);

ib_api_status_t
ibqp_postRecv(
	IN		IbQp_t		*pQp,
	IN		Io_t		*pIo);
uint8_t
ibca_findPortNum( 
	IN		struct _viport	*p_viport,
	IN		uint64_t		guid );

ib_api_status_t
ibregion_init(
	IN		struct _viport		*p_viport,
	OUT		IbRegion_t			*pRegion,
	IN		ib_pd_handle_t		hPd,
	IN		void*				vaddr,
	IN		uint64_t			len,
	IN		ib_access_t			access_ctrl );

void
ibregion_cleanup(
	IN		struct _viport		*p_viport,
	IN		IbRegion_t			*pRegion );

void 
ib_asyncEvent( 
	IN		ib_async_event_rec_t	*pEventRecord );

#define ibpd_fromCa(pCa) (&(pCa)->pd)


#endif /* _VNIC_IB_H_ */
