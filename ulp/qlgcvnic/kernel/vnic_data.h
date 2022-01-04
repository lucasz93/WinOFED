/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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

#ifndef _VNIC_DATA_H_
#define _VNIC_DATA_H_

#include "vnic_trailer.h"

typedef struct RdmaDest {
	LIST_ENTRY			listPtrs;
	IbRegion_t			region;
	struct _viport		*p_viport;
	NDIS_PACKET			*p_packet;
	NDIS_BUFFER			*p_buf;
	uint32_t			buf_sz;
	uint8_t				*data;
	struct ViportTrailer *pTrailer;
} RdmaDest_t;

typedef struct BufferPoolEntry {
	uint64_t remoteAddr;
	net32_t  rKey;
	uint32_t valid;
} BufferPoolEntry_t;

typedef struct RecvPool {
	uint32_t			bufferSz;
	uint32_t			poolSz;
	uint32_t			eiocPoolSz;
	uint32_t			eiocRdmaRkey;
	uint64_t			eiocRdmaAddr;
	uint32_t			nextFullBuf;
	uint32_t			nextFreeBuf;
	uint32_t			numFreeBufs;
	uint32_t			numPostedBufs;
	uint32_t			szFreeBundle;
	BOOLEAN			kickOnFree;
	BufferPoolEntry_t	*bufPool;
	RdmaDest_t			*pRecvBufs;
	LIST_ENTRY			availRecvBufs;
	NDIS_PACKET			**recv_pkt_array;
} RecvPool_t;

typedef struct XmitPool {
	uint32_t			bufferSz;
	uint32_t			poolSz;
	uint32_t			notifyCount;
	uint32_t			notifyBundle;
	uint32_t			nextXmitBuf;
	uint32_t			lastCompBuf;
	uint32_t			numXmitBufs;
	uint32_t			nextXmitPool;
	uint32_t			kickCount;
	uint32_t			kickByteCount;
	uint32_t			kickBundle;
	uint32_t			kickByteBundle;
	BOOLEAN				needBuffers;
	BOOLEAN				sendKicks;
	uint32_t			rdmaRKey;
	uint64_t			rdmaAddr;
	BufferPoolEntry_t	*bufPool;
	RdmaIo_t			*pXmitBufs;
} XmitPool_t;

typedef struct Data {
	struct _viport			*p_viport;
	DataConfig_t			*p_conf;
	IbRegion_t				*p_phy_region;
	IbRegion_t				region;
	IbRegion_t				rbuf_region;
	IbQp_t					qp;
	uint8_t					*pLocalStorage;
	uint32_t				localStorageSz;
	uint8_t					*p_recv_bufs;
	uint32_t				recv_bufs_sz;
	Inic_RecvPoolConfig_t	hostPoolParms;
	Inic_RecvPoolConfig_t	eiocPoolParms;
	RecvPool_t				recvPool;
	XmitPool_t				xmitPool;
	RdmaIo_t				freeBufsIo;
	SendIo_t				kickIo;
	LIST_ENTRY				recvIos;
	KSPIN_LOCK				recvListLock;
	NDIS_SPIN_LOCK			recvIosLock;
	NDIS_SPIN_LOCK			xmitBufLock;
	volatile LONG			kickTimerOn;
	BOOLEAN					connected;
	cl_timer_t				kickTimer;
	NDIS_HANDLE				h_recv_pkt_pool;
	NDIS_HANDLE				h_recv_buf_pool;
#ifdef VNIC_STATISTIC
	struct {
		uint32_t		xmitNum;
		uint32_t		recvNum;
		uint32_t		freeBufSends;
		uint32_t		freeBufNum;
		uint32_t		freeBufMin;
		uint32_t		kickRecvs;
		uint32_t		kickReqs;
		uint32_t		noXmitBufs;
		uint64_t		noXmitBufTime;
	} statistics;
#endif /* VNIC_STATISTIC */
} Data_t;

void
vnic_return_packet(
	IN		NDIS_HANDLE			adapter_context,
	IN		NDIS_PACKET* const	p_packet );

void
data_construct(
	IN		Data_t			*pData,
	IN		struct _viport	*pViport );

ib_api_status_t
data_init(
		Data_t			*pData,
		DataConfig_t	*p_conf,
		uint64_t		guid );

ib_api_status_t
data_connect(
			Data_t		*pData );

ib_api_status_t
data_connected(
			Data_t		*pData );

void
data_disconnect(
			Data_t		*pData );

BOOLEAN
data_xmitPacket(
			Data_t				*pData,
			NDIS_PACKET* const	p_pkt );

void
data_cleanup(
			 Data_t		*pData );

#define data_pathId(pData) (pData)->p_conf->pathId
#define data_eiocPool(pData) &(pData)->eiocPoolParms
#define data_hostPool(pData) &(pData)->hostPoolParms
#define data_eiocPoolMin(pData) &(pData)->p_conf->eiocMin
#define data_hostPoolMin(pData) &(pData)->p_conf->hostMin
#define data_eiocPoolMax(pData) &(pData)->p_conf->eiocMax
#define data_hostPoolMax(pData) &(pData)->p_conf->hostMax
#define data_localPoolAddr(pData) (pData)->xmitPool.rdmaAddr
#define data_localPoolRkey(pData) (pData)->xmitPool.rdmaRKey
#define data_remotePoolAddr(pData) &(pData)->recvPool.eiocRdmaAddr
#define data_remotePoolRkey(pData) &(pData)->recvPool.eiocRdmaRkey
#define data_maxMtu(pData)  MAX_PAYLOAD(min((pData)->recvPool.bufferSz, (pData)->xmitPool.bufferSz)) - ETH_VLAN_HLEN
#define data_len(pData, pTrailer)  ntoh16(pTrailer->dataLength)
#define data_offset(pData, pTrailer) \
				pData->recvPool.bufferSz - sizeof(struct ViportTrailer) \
				- (uint32_t)ROUNDUPP2(data_len(pData, pTrailer), VIPORT_TRAILER_ALIGNMENT) \
				+ pTrailer->dataAlignmentOffset


/* The following macros manipulate ring buffer indexes.
 * The ring buffer size must be a power of 2.
 */
#define ADD(index, increment, size) (((index) + (increment))&((size) - 1))
#define NEXT(index, size) ADD(index, 1, size)
#define INC(index, increment, size) (index) = ADD(index, increment, size)

#define VNIC_RECV_FROM_PACKET( P )	\
	(((RdmaDest_t **)P->MiniportReservedEx)[1])

#define VNIC_LIST_ITEM_FROM_PACKET( P ) \
		((LIST_ENTRY *)P->MiniportReservedEx)

#define VNIC_PACKET_FROM_LIST_ITEM( I ) \
		(PARENT_STRUCT( I, NDIS_PACKET, MiniportReservedEx ))

#endif /* _VNIC_DATA_H_ */
