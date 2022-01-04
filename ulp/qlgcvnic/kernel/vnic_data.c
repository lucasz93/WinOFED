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
#include <complib/comp_lib.h>
#include "vnic_driver.h"

static void    data_postRecvs(Data_t *pData);
static void    _data_receivedKick(Io_t *pIo);
static void    _data_xmitComplete(Io_t *pIo);
static void    data_sendKickMessage(Data_t *pData);
static void    _data_kickTimeoutHandler( void *context );
static BOOLEAN data_allocXmitBuffer(Data_t *pData,
					BufferPoolEntry_t **ppBpe, RdmaIo_t **ppRdmaIo, BOOLEAN *pLast);
static void    data_checkXmitBuffers(Data_t *pData);

static
ib_api_status_t
data_rdmaPacket(
				Data_t				*pData,
				BufferPoolEntry_t	*pBpe,
				RdmaIo_t			*pRdmaIo );
static 
BOOLEAN
_data_recv_to_ndis_pkt( 
				Data_t			*pData,
				RdmaDest_t		*pRdmaDest );

static void
_data_allocBuffers(
					Data_t		*pData,
					BOOLEAN		initialAllocation );

static void
_data_addFreeBuffer(
					Data_t		*pData,
					int			index,
					RdmaDest_t	*pRdmaDest );

static uint32_t
_data_incomingRecv(
					Data_t		*pData );

static void   
_data_sendFreeRecvBuffers(
					Data_t		*pData );

static uint8_t
_tx_chksum_flags(
 IN		NDIS_PACKET* const		p_packet );

static void
_data_return_recv(
	  IN	NDIS_PACKET		*p_packet );

static void
_data_kickTimer_start(
		  IN	Data_t		*pData,
		  IN	uint32_t	microseconds );

static void
_data_kickTimer_stop(
		  IN	Data_t		*pData );

#define INBOUND_COPY

#ifdef VNIC_STATISTIC
int64_t recvRef;
#endif /* VNIC_STATISTIC */

void
data_construct(
	IN		Data_t			*pData,
	IN		viport_t		*pViport )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	RtlZeroMemory( pData, sizeof(*pData) );

	pData->p_viport = pViport;
	pData->p_phy_region = &pViport->p_adapter->ca.region;
	InitializeListHead( &pData->recvIos );
	KeInitializeSpinLock( &pData->recvListLock );
	NdisAllocateSpinLock( &pData->recvIosLock );
	NdisAllocateSpinLock( &pData->xmitBufLock );
	cl_timer_construct( &pData->kickTimer );

	ibqp_construct( &pData->qp, pViport );

	VNIC_EXIT( VNIC_DBG_DATA );
}


ib_api_status_t
data_init(
	IN		Data_t			*pData,
	IN		DataConfig_t	*p_conf,
	IN		uint64_t		guid )
{
	ib_api_status_t	ib_status;

	VNIC_ENTER( VNIC_DBG_DATA );

	ASSERT( pData->p_viport != NULL );
	pData->p_conf = p_conf;

	cl_timer_init( &pData->kickTimer, _data_kickTimeoutHandler, pData );

	ib_status = ibqp_init(&pData->qp, guid, &p_conf->ibConfig );
	if( ib_status != IB_SUCCESS )
		VNIC_TRACE( VNIC_DBG_ERROR, ("data ibqp_init failed\n") );

	VNIC_EXIT( VNIC_DBG_DATA );
	return ib_status;
}


ib_api_status_t
data_connect(
		IN		Data_t		*pData )
{
	NDIS_STATUS			status;
	ib_api_status_t		ib_status;
	XmitPool_t        *pXmitPool = &pData->xmitPool;
	RecvPool_t        *pRecvPool = &pData->recvPool;
	RecvIo_t          *pRecvIo;
	SendIo_t          *pSendIo;
	RdmaIo_t          *pRdmaIo;
	RdmaDest_t        *pRdmaDest;
	uint8_t           *pRegionData;
	int               sz, regionSz;
	unsigned int      i, j;

	VNIC_ENTER( VNIC_DBG_DATA );

	pRecvPool->poolSz           = pData->p_conf->hostRecvPoolEntries;
	pRecvPool->eiocPoolSz       = pData->hostPoolParms.numRecvPoolEntries;

	if ( pRecvPool->poolSz > pRecvPool->eiocPoolSz )
	{
		pRecvPool->poolSz = pData->hostPoolParms.numRecvPoolEntries;
	}
	pRecvPool->szFreeBundle     =
			pData->hostPoolParms.freeRecvPoolEntriesPerUpdate;
	pRecvPool->numFreeBufs      = 0;
	pRecvPool->numPostedBufs    = 0;
	pRecvPool->nextFullBuf      = 0;
	pRecvPool->nextFreeBuf      = 0;
	pRecvPool->kickOnFree       = FALSE;
	pRecvPool->bufferSz         = pData->hostPoolParms.sizeRecvPoolEntry;

	pXmitPool->bufferSz         = pData->eiocPoolParms.sizeRecvPoolEntry;
	pXmitPool->poolSz           = pData->eiocPoolParms.numRecvPoolEntries;
	pXmitPool->notifyCount      = 0;
	pXmitPool->notifyBundle     = pData->p_conf->notifyBundle;
	pXmitPool->nextXmitPool     = 0;

#if LIMIT_OUTSTANDING_SENDS
	pXmitPool->numXmitBufs      = pXmitPool->notifyBundle * 2;
#else /* !LIMIT_OUTSTANDING_SENDS */
	pXmitPool->numXmitBufs      = pXmitPool->poolSz;
#endif /* LIMIT_OUTSTANDING_SENDS */

	pXmitPool->nextXmitBuf      = 0;
	pXmitPool->lastCompBuf      = pXmitPool->numXmitBufs - 1;
	pXmitPool->kickCount        = 0;
	pXmitPool->kickByteCount    = 0;
	pXmitPool->sendKicks        =
		(BOOLEAN)(( pData->eiocPoolParms.numRecvPoolEntriesBeforeKick != 0 )
		|| ( pData->eiocPoolParms.numRecvPoolBytesBeforeKick != 0 ));
	pXmitPool->kickBundle       =
		pData->eiocPoolParms.numRecvPoolEntriesBeforeKick;
	pXmitPool->kickByteBundle   =
		pData->eiocPoolParms.numRecvPoolBytesBeforeKick;
	pXmitPool->needBuffers      = TRUE;

	sz  = sizeof(RdmaDest_t) * pRecvPool->poolSz;
	sz += sizeof(RecvIo_t) * pData->p_conf->numRecvs;
	sz += sizeof(RdmaIo_t) * pXmitPool->numXmitBufs;

	regionSz  = 4 * pData->p_conf->numRecvs;
	regionSz += sizeof(BufferPoolEntry_t) * pRecvPool->eiocPoolSz;
	regionSz += sizeof(BufferPoolEntry_t) * pXmitPool->poolSz;
	sz       += regionSz;

 	status = NdisAllocateMemoryWithTag( &pData->pLocalStorage,
										(UINT)sz,
 										'grtS' );
 	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Failed allocating %d bytes local storage\n", sz ) );
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto err1;
	}

	NdisZeroMemory( pData->pLocalStorage, sz );
	pData->localStorageSz = sz;

	pRecvPool->pRecvBufs = (RdmaDest_t *)pData->pLocalStorage;
	sz                   = sizeof(RdmaDest_t) * pRecvPool->poolSz;
	pRecvIo              = (RecvIo_t *)(pData->pLocalStorage + sz);
	sz                  += sizeof(RecvIo_t) * pData->p_conf->numRecvs;

	pXmitPool->pXmitBufs = (RdmaIo_t *)(pData->pLocalStorage + sz);
	sz                  += sizeof(RdmaIo_t) * pXmitPool->numXmitBufs;

	pRegionData          = pData->pLocalStorage + sz;
	sz                  += 4 * pData->p_conf->numRecvs;

	pRecvPool->bufPool   = (BufferPoolEntry_t *)(pData->pLocalStorage + sz);
	sz                  += sizeof(BufferPoolEntry_t) * pRecvPool->eiocPoolSz;
	pXmitPool->bufPool   = (BufferPoolEntry_t *)(pData->pLocalStorage + sz);

	ib_status = ibregion_init( pData->p_viport, &pData->region,
		pData->p_viport->p_adapter->ca.hPd, pRegionData, regionSz,
		( IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE ) );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("ib_region_init failed\n") );
		goto err2;
	}

	pRdmaIo = &pData->freeBufsIo;
	pRdmaIo->io.pViport          = pData->p_viport;
	pRdmaIo->io.pRoutine         = NULL;
	pRdmaIo->io.wrq.p_next       = NULL;
	pRdmaIo->io.wrq.wr_type      = WR_RDMA_WRITE;
	pRdmaIo->io.wrq.wr_id        = (ULONG_PTR)pRdmaIo;
	pRdmaIo->io.wrq.num_ds       = 1;
	pRdmaIo->io.wrq.ds_array     = pRdmaIo->dsList;
	pRdmaIo->dsList[0].lkey      = pData->region.lkey;
	pRdmaIo->io.wrq.send_opt     = IB_SEND_OPT_SIGNALED;

	pSendIo = &pData->kickIo;
	pSendIo->io.pViport          = pData->p_viport;
	pSendIo->io.pRoutine         = NULL;
	pSendIo->io.wrq.p_next       = NULL;
	pSendIo->io.wrq.wr_type      = WR_SEND;
	pSendIo->io.wrq.wr_id        = (ULONG_PTR)pSendIo;
	pSendIo->io.wrq.num_ds       = 1;
	pSendIo->io.wrq.ds_array     = &pSendIo->dsList;

	pSendIo->io.wrq.send_opt     = IB_SEND_OPT_SIGNALED;

	pSendIo->dsList.length       = 0;
	pSendIo->dsList.vaddr        = (ULONG_PTR)pRegionData;
	pSendIo->dsList.lkey         = pData->region.lkey;

	for ( i = 0; i < pData->p_conf->numRecvs; i++ )
	{
		pRecvIo[i].io.pViport         = pData->p_viport;
		pRecvIo[i].io.pRoutine        = _data_receivedKick;
		pRecvIo[i].io.r_wrq.wr_id     = (ULONG_PTR)&pRecvIo[i].io;
		pRecvIo[i].io.r_wrq.p_next    = NULL;
		pRecvIo[i].io.r_wrq.num_ds    = 1;
		pRecvIo[i].io.r_wrq.ds_array  = &pRecvIo[i].dsList;
		pRecvIo[i].dsList.length      = 4;
		pRecvIo[i].dsList.vaddr       = (ULONG_PTR)pRegionData;
		pRecvIo[i].dsList.lkey        = pData->region.lkey;
		
		InitializeListHead( &pRecvIo[i].io.listPtrs );
		ExInterlockedInsertTailList( &pData->recvIos, &pRecvIo[i].io.listPtrs, &pData->recvListLock );
        /* Do not need to move pointer since the receive info
         * is not read.  Note, we could reduce the amount
         * of memory allocated and the size of the region.
		 * pRegionData                  += 4;
         * */
	}

	sz = pRecvPool->poolSz * pRecvPool->bufferSz;
	status = NdisAllocateMemoryWithTag(&pData->p_recv_bufs,
										sz, 'fubr');
	if( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("Allocate recv buffers failed\n"));
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto err3;
	}
	NdisZeroMemory( pData->p_recv_bufs, sz );

	pData->recv_bufs_sz = sz;

	ib_status = ibregion_init( pData->p_viport, &pData->rbuf_region,
		pData->p_viport->p_adapter->ca.hPd, pData->p_recv_bufs, sz,
		(IB_AC_LOCAL_WRITE | IB_AC_RDMA_WRITE) );
	if( ib_status != IB_SUCCESS )
	{
		goto err4;
	}

	NdisAllocatePacketPool( &status,
							&pData->h_recv_pkt_pool,
							pRecvPool->poolSz,
							PROTOCOL_RESERVED_SIZE_IN_PACKET );

	if( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Allocate packet pool failed status %#x\n", status ));
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto err5;
	}

	NdisAllocateBufferPool(
		&status, &pData->h_recv_buf_pool, pRecvPool->poolSz );

	if( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Allocate packet pool failed status %#x\n", status ));
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto err6;
	}
	pData->recvPool.recv_pkt_array =
		cl_zalloc( sizeof(NDIS_PACKET*)* pRecvPool->poolSz );
	if( !pData->recvPool.recv_pkt_array )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("Allocate packet array failed\n" ) );
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto err7;
	}

	InitializeListHead( &pRecvPool->availRecvBufs );

	for ( i = 0; i < pRecvPool->poolSz; i++ )
	{
		pRdmaDest = &pRecvPool->pRecvBufs[i];
		pRdmaDest->p_viport = pData->p_viport;
		pRdmaDest->data = pData->p_recv_bufs + (i * pRecvPool->bufferSz );
		pRdmaDest->region = pData->rbuf_region;
		InsertTailList( &pRecvPool->availRecvBufs, &pRdmaDest->listPtrs );
	}

	for ( i = 0; i < pXmitPool->numXmitBufs; i++ )
	{
		pRdmaIo = &pXmitPool->pXmitBufs[i];
		pRdmaIo->index               = (uint16_t)i;
		pRdmaIo->io.pViport          = pData->p_viport;
		pRdmaIo->io.pRoutine         = _data_xmitComplete;
		pRdmaIo->io.wrq.p_next       = NULL;
		pRdmaIo->io.wrq.wr_type      = WR_RDMA_WRITE;
		pRdmaIo->io.wrq.wr_id        = (ULONG_PTR)pRdmaIo;
		pRdmaIo->io.wrq.num_ds       = MAX_NUM_SGE; // will set actual number when transmit
		pRdmaIo->io.wrq.ds_array     = pRdmaIo->dsList;
		pRdmaIo->p_trailer			=  (ViportTrailer_t *)&pRdmaIo->data[0];
		for( j = 0; j < MAX_NUM_SGE; j++ )
		{
			pRdmaIo->dsList[j].lkey      = pData->p_phy_region->lkey;
		}
	}

	pXmitPool->rdmaRKey      = pData->region.rkey;
	pXmitPool->rdmaAddr      = (ULONG_PTR)pXmitPool->bufPool;

	data_postRecvs( pData );

	ib_status = ibqp_connect( &pData->qp );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, ("ibqp_connect returned %s\n",
			pData->p_viport->p_adapter->ifc.get_err_str( ib_status )) );
err7:
		NdisFreeBufferPool( pData->h_recv_buf_pool );
		pData->h_recv_buf_pool = NULL;
err6:
		NdisFreePacketPool( pData->h_recv_pkt_pool );
		pData->h_recv_pkt_pool = NULL;
err5:
		ibregion_cleanup( pData->p_viport, &pData->rbuf_region );
err4:
		NdisFreeMemory( pData->p_recv_bufs, pData->recv_bufs_sz, 0 );
		pData->p_recv_bufs = NULL;
err3:
		ibregion_cleanup(pData->p_viport, &pData->region );
err2:
		NdisFreeMemory( pData->pLocalStorage, pData->localStorageSz, 0 );
		pData->pLocalStorage = NULL;
err1:
		pRecvPool->poolSz = 0;
	}

	VNIC_EXIT( VNIC_DBG_DATA );
	return ib_status;
}


ib_api_status_t
data_connected(
		IN		Data_t		*pData )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	pData->freeBufsIo.io.wrq.remote_ops.rkey =
							pData->recvPool.eiocRdmaRkey;

	_data_allocBuffers(pData, TRUE);
	_data_sendFreeRecvBuffers(pData);
	
	if( pData->p_viport->errored )
	{
		return IB_ERROR;
	}
	
	pData->connected = TRUE;

	VNIC_EXIT( VNIC_DBG_DATA );
	return IB_SUCCESS;
}

void
data_disconnect(
		IN		Data_t		*pData )
{
	RecvPool_t *pRecvPool = &pData->recvPool;
	viport_t	*p_viport = pData->p_viport;
	NDIS_PACKET		*p_packet;
	NDIS_BUFFER		*p_buf;
	unsigned int        i;

	VNIC_ENTER( VNIC_DBG_DATA );

	_data_kickTimer_stop ( pData );

	pData->connected = FALSE;

	ibqp_detach( &pData->qp );

	ibregion_cleanup( p_viport, &pData->rbuf_region );
	ibregion_cleanup( p_viport, &pData->region );

	for ( i = 0; i < pRecvPool->poolSz; i++ )
	{
		p_packet = pRecvPool->pRecvBufs[i].p_packet;
		if ( p_packet != NULL )
		{
			pRecvPool->pRecvBufs[i].p_packet = NULL;
			NdisUnchainBufferAtFront( p_packet, &p_buf );
			if( p_buf )
			{
			/* Return the NDIS packet and NDIS buffer to their pools. */
				NdisFreeBuffer( p_buf );
			}
			NdisFreePacket( p_packet );
		}
	}

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

BOOLEAN
data_xmitPacket(
		IN		Data_t				*pData,
		IN		NDIS_PACKET* const	p_packet )
{
	XmitPool_t		*p_xmitPool;
	RdmaIo_t		*pRdmaIo;
	BufferPoolEntry_t *pBpe;
	BOOLEAN			last;
	uint8_t			*p_buf;
	uint32_t		buf_len;
	uint32_t		pkt_size;
	NDIS_BUFFER		*p_buf_desc;
	eth_hdr_t*		p_eth_hdr;
	int				pad;
	SCATTER_GATHER_LIST		*p_sgl;
	uint32_t		i;
	PHYSICAL_ADDRESS phy_addr;
	NDIS_PACKET_8021Q_INFO vlanInfo;
	net16_t			pri_vlan = 0;

	VNIC_ENTER( VNIC_DBG_DATA );

	p_sgl = NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet,
											ScatterGatherListPacketInfo );
	if ( p_sgl == NULL )
	{
		return FALSE;
	}

	NdisGetFirstBufferFromPacketSafe( p_packet,
									&p_buf_desc,
									&p_buf,
									&buf_len,
									&pkt_size,
									NormalPagePriority );

	if( pkt_size > pData->xmitPool.bufferSz )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Outbound packet too large, size = %d\n", pkt_size ) );
		return FALSE;
	}

	if ( p_sgl->NumberOfElements > (ULONG)MAX_NUM_SGE - 1 )
	{
		VNIC_TRACE( VNIC_DBG_DATA,
			(" Xmit packet exceeded SGE limit - %d\n",
										p_sgl->NumberOfElements ) );
		return FALSE;
	}

	vlanInfo.Value = NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet, Ieee8021QInfo );
	p_eth_hdr = (eth_hdr_t *)p_buf;

	if( vlanInfo.Value ) /* there is VLAN_ID specified the packet */
	{
		if( vlanInfo.TagHeader.VlanId != 0 )
		{
			/* packet vlanId does not match */
			if( pData->p_viport->p_adapter->vlan_info &&
				( pData->p_viport->p_adapter->vlan_info & 0x00000fff ) != vlanInfo.TagHeader.VlanId )
			{
				return FALSE;
			}
			pri_vlan = (uint16_t)vlanInfo.TagHeader.VlanId;
			pri_vlan |= (uint16_t)( vlanInfo.TagHeader.UserPriority << 13 );
		}
		else if( pData->p_viport->p_adapter->vlan_info )
		{
			pri_vlan = (uint16_t)( pData->p_viport->p_adapter->vlan_info );
		}
	}
	else  /* no VLAN_ID info in a packet */
	{
		if( pData->p_viport->p_adapter->vlan_info )
		{
			pri_vlan = (uint16_t)( pData->p_viport->p_adapter->vlan_info );
		}
	}

	if( !data_allocXmitBuffer( pData, &pBpe, &pRdmaIo, &last ) )
	{
		return FALSE;
	}

	pRdmaIo->p_packet = p_packet;
	pRdmaIo->packet_sz = pkt_size;

	pRdmaIo->len = (uint32_t)ROUNDUPP2(
		max(MIN_PACKET_LEN, pRdmaIo->packet_sz), VIPORT_TRAILER_ALIGNMENT );
	pad = pRdmaIo->len - pRdmaIo->packet_sz;

	pRdmaIo->p_trailer = (ViportTrailer_t *)&pRdmaIo->data[pad];
	
	cl_memclr( pRdmaIo->data, pad + sizeof( ViportTrailer_t ) );
	cl_memcpy( pRdmaIo->p_trailer->destMacAddr, p_eth_hdr->dst.addr, MAC_ADDR_LEN );
	
	pRdmaIo->p_trailer->dataLength =
					cl_hton16( (uint16_t)max( MIN_PACKET_LEN, pRdmaIo->packet_sz ) );
	
	if( pri_vlan )
	{
		/* if tagged frame */
		if( *(uint16_t *)(p_buf + 12 ) == ETH_PROT_VLAN_TAG )
		{
			/* strip vlan tag from header */
			RtlMoveMemory( p_buf + 4, p_buf, 12 );
			/* adjust data length */
			pRdmaIo->p_trailer->dataLength = 
				cl_hton16( (uint16_t)max( MIN_PACKET_LEN, pRdmaIo->packet_sz ) - 4 );
		}
		pRdmaIo->p_trailer->vLan = cl_hton16( pri_vlan );
		pRdmaIo->p_trailer->pktFlags |= PF_VLAN_INSERT;
	}

	for( i=0; i < p_sgl->NumberOfElements; i++ )
	{
		pRdmaIo->dsList[i].vaddr = p_sgl->Elements[i].Address.QuadPart;
		pRdmaIo->dsList[i].length = p_sgl->Elements[i].Length;
	}
	if( viport_canTxCsum( pData->p_viport ) && 
		pData->p_viport->p_adapter->params.UseTxCsum )
	{
		pRdmaIo->p_trailer->txChksumFlags = _tx_chksum_flags( p_packet );
	}
	else
	{
		pRdmaIo->p_trailer->txChksumFlags = 0;
	}
	pRdmaIo->p_trailer->connectionHashAndValid = CHV_VALID;

	if( last || pData->xmitPool.needBuffers )
		pRdmaIo->p_trailer->pktFlags |= PF_KICK;

	/* fill last data segment with trailer and pad */
	phy_addr = MmGetPhysicalAddress( pRdmaIo->data );

	pRdmaIo->dsList[p_sgl->NumberOfElements].vaddr = phy_addr.QuadPart;
	pRdmaIo->dsList[p_sgl->NumberOfElements].length = pRdmaIo->len -
													  pRdmaIo->packet_sz +
													  sizeof( ViportTrailer_t );

    pRdmaIo->io.wrq.num_ds =p_sgl->NumberOfElements + 1;

	if( data_rdmaPacket( pData, pBpe, pRdmaIo ) != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
					("RDMA WRITE Failed\n"));
		return FALSE;
	}
	p_xmitPool = &pData->xmitPool;
	if( p_xmitPool->sendKicks )
	{
		/* EIOC needs kicks to inform it of sent packets */

		p_xmitPool->kickCount++;
		p_xmitPool->kickByteCount += pRdmaIo->packet_sz;
		if( ( p_xmitPool->kickCount >= p_xmitPool->kickBundle )
			 || ( p_xmitPool->kickByteCount >= p_xmitPool->kickByteBundle ) )
		{
			data_sendKickMessage( pData );
		}
		else if( p_xmitPool->kickCount == 1 )
		{
			_data_kickTimer_start( pData, pData->eiocPoolParms.timeoutBeforeKick );
		}
	}
	return TRUE;
}

static uint8_t
_tx_chksum_flags(
		  IN	NDIS_PACKET* const		p_packet )

{
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO	*p_packet_info;
	ULONG								packet_info;
	uint8_t			txChksumFlags = 0;

	if( NDIS_PROTOCOL_ID_TCP_IP == NDIS_GET_PACKET_PROTOCOL_TYPE(p_packet) )
	{
		packet_info = PtrToUlong( NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet, TcpIpChecksumPacketInfo));
		p_packet_info = ( NDIS_TCP_IP_CHECKSUM_PACKET_INFO *)&packet_info;

		if( p_packet_info )
		{
			if( p_packet_info->Transmit.NdisPacketChecksumV4 )
			{
				txChksumFlags = TX_CHKSUM_FLAGS_CHECKSUM_V4
				| ( p_packet_info->Transmit.NdisPacketIpChecksum ? TX_CHKSUM_FLAGS_IP_CHECKSUM: 0 )
				| ( p_packet_info->Transmit.NdisPacketTcpChecksum ? TX_CHKSUM_FLAGS_TCP_CHECKSUM: 0 )
				| ( p_packet_info->Transmit.NdisPacketUdpChecksum ? TX_CHKSUM_FLAGS_UDP_CHECKSUM: 0 );
			}
			else if( p_packet_info->Transmit.NdisPacketChecksumV6 )
			{
				txChksumFlags = TX_CHKSUM_FLAGS_CHECKSUM_V6
				| ( p_packet_info->Transmit.NdisPacketIpChecksum ? TX_CHKSUM_FLAGS_IP_CHECKSUM: 0 )
				| ( p_packet_info->Transmit.NdisPacketTcpChecksum ? TX_CHKSUM_FLAGS_TCP_CHECKSUM: 0 )
				| ( p_packet_info->Transmit.NdisPacketUdpChecksum ? TX_CHKSUM_FLAGS_UDP_CHECKSUM: 0 );
			}
		}
	}
	
	VNIC_TRACE( VNIC_DBG_DATA ,
		("txChksumFlags = %#x: V4 %c, V6 %c, IP %c, TCP %c, UDP %c\n",
				txChksumFlags,
				((txChksumFlags & TX_CHKSUM_FLAGS_CHECKSUM_V4 )? '+': '-'),
				((txChksumFlags & TX_CHKSUM_FLAGS_CHECKSUM_V6 )? '+': '-'),
				((txChksumFlags & TX_CHKSUM_FLAGS_IP_CHECKSUM )? '+': '-'),
				((txChksumFlags & TX_CHKSUM_FLAGS_TCP_CHECKSUM )? '+': '-'),
				((txChksumFlags & TX_CHKSUM_FLAGS_UDP_CHECKSUM )? '+': '-') ));

	return txChksumFlags;
}

static void
data_postRecvs(
		IN		Data_t		*pData )
{
	RecvIo_t		*pRecvIo;
	LIST_ENTRY		*p_list_entry;
	ib_api_status_t	ib_status;
	
	VNIC_ENTER ( VNIC_DBG_DATA );

	if( pData->p_viport->errored )
		return;

	while( ( p_list_entry = ExInterlockedRemoveHeadList( &pData->recvIos, &pData->recvListLock  ) )	!= NULL )
	{
		pRecvIo = (RecvIo_t *)p_list_entry;

		ib_status = ibqp_postRecv( &pData->qp, &pRecvIo->io );
		if( ib_status != IB_SUCCESS )
		{
			VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("ibqp_postRecv returned %s\n",
				pData->p_viport->p_adapter->ifc.get_err_str( ib_status )) );
			viport_failure( pData->p_viport );
			return;
		}
	}

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static void
_data_receivedKick(
			IN		Io_t		*pIo )
{

	uint32_t	num_pkts = 0;

	VNIC_ENTER( VNIC_DBG_DATA );

	NdisAcquireSpinLock( &pIo->pViport->data.recvIosLock );

#ifdef VNIC_STATISTIC
	recvRef = cl_get_tick_count();
#endif /* VNIC_STATISTIC */

	ExInterlockedInsertTailList( &pIo->pViport->data.recvIos, 
								&pIo->listPtrs, 
								&pIo->pViport->data.recvListLock );

	data_postRecvs( &pIo->pViport->data );

#ifdef VNIC_STATISTIC
	pIo->pViport->data.statistics.kickRecvs++;
#endif /* VNIC_STATISTIC */

	data_checkXmitBuffers( &pIo->pViport->data );

	num_pkts = _data_incomingRecv( &pIo->pViport->data );

	NdisReleaseSpinLock(&pIo->pViport->data.recvIosLock );

	if( num_pkts )
	{
		viport_recvPacket( pIo->pViport,
			pIo->pViport->data.recvPool.recv_pkt_array,
			num_pkts );

		pIo->pViport->stats.ifInOk += num_pkts;
	}

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static void
_data_xmitComplete(
			IN		Io_t		*pIo )
{

	Data_t         *pData;
	XmitPool_t     *p_xmitPool;
	NDIS_PACKET		*p_packet;
	NDIS_STATUS		ndis_status;
	uint32_t		io_index;

	VNIC_ENTER( VNIC_DBG_DATA );
	
	NdisAcquireSpinLock( &pIo->pViport->data.xmitBufLock );
	
	io_index = ((RdmaIo_t *)pIo)->index;
	pData = &pIo->pViport->data;
	p_xmitPool = &pData->xmitPool;

	while ( p_xmitPool->lastCompBuf != io_index )
	{
		INC(p_xmitPool->lastCompBuf, 1, p_xmitPool->numXmitBufs);
	
		p_packet = p_xmitPool->pXmitBufs[p_xmitPool->lastCompBuf].p_packet;
	
		p_xmitPool->pXmitBufs[p_xmitPool->lastCompBuf].p_packet = NULL;

		if( p_packet != NULL )
		{
			if( pIo->wc_status != IB_WCS_SUCCESS )
			{
				ndis_status = NDIS_STATUS_FAILURE;
				pIo->pViport->stats.ifOutErrors++;
				pIo->wc_status = IB_WCS_SUCCESS;
			}
			else
			{
				ndis_status = NDIS_STATUS_SUCCESS;
				pIo->pViport->stats.ifOutOk++;
			}
			NDIS_SET_PACKET_STATUS( p_packet, ndis_status );
			NdisMSendComplete( pIo->pViport->p_adapter->h_handle,
								p_packet, ndis_status );
		}
	}
	NdisReleaseSpinLock( &pData->xmitBufLock );

	if( !pData->p_viport->errored )
	{
		data_checkXmitBuffers( pData );
	}
	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static void
data_sendKickMessage(
		IN		Data_t		*pData )
{
	XmitPool_t *pPool = &pData->xmitPool;

	VNIC_ENTER( VNIC_DBG_DATA );

	/* stop timer for BundleTimeout */
	_data_kickTimer_stop( pData );

	pPool->kickCount = 0;
	pPool->kickByteCount = 0;

	/* TBD: Keep track of when kick is outstanding, and
	 * don't reuse until complete
	 */
	if ( ibqp_postSend( &pData->qp, &pData->kickIo.io ) != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
					("Unable to send kick to EIOC\n") );
		viport_failure( pData->p_viport );
	}

	VNIC_EXIT( VNIC_DBG_DATA );
}

static void
_data_kickTimeoutHandler( void * context )
{
	Data_t* pData = (Data_t *)context;

	VNIC_ENTER( VNIC_DBG_DATA );
	
	InterlockedExchange( &pData->kickTimerOn, FALSE );
	data_sendKickMessage( pData );

	VNIC_EXIT( VNIC_DBG_DATA );

	return;
}

static BOOLEAN
data_allocXmitBuffer(
		IN		Data_t				*pData,
		OUT		BufferPoolEntry_t	**ppBpe,
		OUT		RdmaIo_t			**ppRdmaIo,
		OUT		BOOLEAN				*pLast )
{
	XmitPool_t    *p_xmitPool;

	VNIC_ENTER( VNIC_DBG_DATA );

	NdisAcquireSpinLock( &pData->xmitBufLock );
	
	p_xmitPool = &pData->xmitPool;
	*pLast = FALSE;
	*ppRdmaIo = &p_xmitPool->pXmitBufs[p_xmitPool->nextXmitBuf];
	*ppBpe = &p_xmitPool->bufPool[p_xmitPool->nextXmitPool];

	if ( (*ppBpe)->valid && p_xmitPool->nextXmitBuf != p_xmitPool->lastCompBuf )
	{
		INC(p_xmitPool->nextXmitBuf, 1, p_xmitPool->numXmitBufs);
		INC(p_xmitPool->nextXmitPool, 1, p_xmitPool->poolSz);

		if ( !p_xmitPool->bufPool[p_xmitPool->nextXmitPool].valid )
		{
			VNIC_TRACE( VNIC_DBG_DATA,
				("Just used the last EIOU receive buffer\n") );

			*pLast = TRUE;
			p_xmitPool->needBuffers = TRUE;
			viport_stopXmit( pData->p_viport );
#ifdef VNIC_STATISTIC
			pData->statistics.kickReqs++;
#endif /* VNIC_STATISTIC */
		}
		else if ( p_xmitPool->nextXmitBuf == p_xmitPool->lastCompBuf )
		{
			VNIC_TRACE( VNIC_DBG_DATA,
						("Just used our last xmit buffer\n") );
		
			p_xmitPool->needBuffers = TRUE;
			viport_stopXmit( pData->p_viport );
		}

		(*ppBpe)->valid  = 0;

		NdisReleaseSpinLock( &pData->xmitBufLock );
		return TRUE;
	}
	else
	{
#ifdef VNIC_STATISTIC
		pData->statistics.noXmitBufs++;
#endif /* VNIC_STATISTIC */
	
		VNIC_TRACE( VNIC_DBG_ERROR,
					("Out of xmit buffers\n") );

		p_xmitPool->needBuffers = TRUE;
		viport_stopXmit( pData->p_viport );

		NdisReleaseSpinLock( &pData->xmitBufLock );
		return FALSE;
	}
}

static void
data_checkXmitBuffers(
		IN			Data_t		*pData )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	NdisAcquireSpinLock( &pData->xmitBufLock );

	if ( pData->xmitPool.needBuffers
		&& pData->xmitPool.bufPool[pData->xmitPool.nextXmitPool].valid
		&& pData->xmitPool.nextXmitBuf != pData->xmitPool.lastCompBuf )
	{
		pData->xmitPool.needBuffers = FALSE;
		viport_restartXmit( pData->p_viport );

		VNIC_TRACE( VNIC_DBG_DATA,
						("There are free xmit buffers\n") );
	}

	NdisReleaseSpinLock( &pData->xmitBufLock );

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static
ib_api_status_t
data_rdmaPacket(
			IN			Data_t				*pData,
			IN			BufferPoolEntry_t	*pBpe,
			IN			RdmaIo_t			*pRdmaIo )
{
	ib_send_wr_t	*pWrq;
	uint64_t		remote_addr;

	VNIC_ENTER( VNIC_DBG_DATA );

	pWrq = &pRdmaIo->io.wrq;

	remote_addr  = ntoh64( pBpe->remoteAddr );
	remote_addr += pData->xmitPool.bufferSz;
	remote_addr -= ( pRdmaIo->len + sizeof(ViportTrailer_t) );

	pWrq->remote_ops.vaddr		= remote_addr;
	pWrq->remote_ops.rkey		= pBpe->rKey;

	pData->xmitPool.notifyCount++;

	if( pData->xmitPool.notifyCount >= pData->xmitPool.notifyBundle )
	{
		pData->xmitPool.notifyCount = 0;
		pWrq->send_opt = IB_SEND_OPT_SIGNALED;
	}
	else
	{
		pWrq->send_opt &= ~IB_SEND_OPT_SIGNALED;
	}
	pWrq->send_opt = IB_SEND_OPT_SIGNALED;

	if( pData->p_viport->featuresSupported & INIC_FEAT_RDMA_IMMED )
	{
		pWrq->send_opt |= IB_SEND_OPT_IMMEDIATE;
		pWrq->immediate_data = 0;
	}

	if( ibqp_postSend( &pData->qp, &pRdmaIo->io ) != IB_SUCCESS )
	{
		VNIC_TRACE(VNIC_DBG_ERROR,
					("Failed sending data to EIOC\n") );
		return IB_ERROR;
	}
#ifdef VNIC_STATISTIC
	pData->statistics.xmitNum++;
#endif /* VNIC_STATISTIC */

	VNIC_EXIT( VNIC_DBG_DATA );
	return IB_SUCCESS;
}

static BOOLEAN
_data_recv_to_ndis_pkt(
		IN		Data_t			*pData,
		IN		RdmaDest_t		*pRdmaDest )
{
	struct ViportTrailer *pTrailer;
	NDIS_PACKET			*p_packet;
	NDIS_STATUS			ndis_status;
	int					start;
	unsigned int		len;
	NDIS_TCP_IP_CHECKSUM_PACKET_INFO  packet_info;
	NDIS_PACKET_8021Q_INFO vlan_info;
	uint16_t			vlanId;
	uint8_t				rxChksumFlags;

	VNIC_ENTER( VNIC_DBG_DATA );

	pTrailer = pRdmaDest->pTrailer;
	p_packet = pRdmaDest->p_packet;

	start = (int)data_offset(pData, pTrailer);
	len = data_len(pData, pTrailer);

	NdisAllocateBuffer( &ndis_status,
						&pRdmaDest->p_buf,
						pData->h_recv_buf_pool,
						pRdmaDest->data + start,
						len );

	if ( ndis_status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			( "NdisAllocateBuffer failed %#x\n", ndis_status ) );
		return FALSE;
	}

	NdisChainBufferAtFront( p_packet, pRdmaDest->p_buf );
	pRdmaDest->buf_sz = len;

	if ( pTrailer->pktFlags & PF_VLAN_INSERT &&
		!( pData->p_viport->featuresSupported & INIC_FEAT_IGNORE_VLAN ) )
	{
		/*	handle VLAN	tag insertion */
		vlan_info.Value = NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet, Ieee8021QInfo );
		vlanId = cl_ntoh16( pTrailer->vLan );

		if( !( vlanId & 0xfff ) ||
			( ( (vlanId & 0xfff ) != 0 ) && 
			  (vlanId & 0xfff ) == ((uint16_t)pData->p_viport->p_adapter->vlan_info & 0xfff ) ))
		{
			if( pTrailer->pktFlags & PF_PVID_OVERRIDDEN )
			{
				vlan_info.TagHeader.VlanId =  0;
			}
			else
			{
				vlan_info.TagHeader.VlanId =  vlanId & 0xfff;
			}
			vlan_info.TagHeader.UserPriority = vlanId & 0xe000;
			vlan_info.TagHeader.CanonicalFormatId = 0;
			vlan_info.TagHeader.Reserved = 0;
			NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet, Ieee8021QInfo ) = vlan_info.Value;
		}
	}

	if( viport_canRxCsum( pData->p_viport ) &&
		pData->p_viport->p_adapter->params.UseRxCsum )
	{
		rxChksumFlags = pTrailer->rxChksumFlags;

		VNIC_TRACE( VNIC_DBG_DATA,
			("rxChksumFlags = %#x, LOOP = %c, IP = %c, TCP = %c, UDP = %c\n",
			rxChksumFlags,
			(rxChksumFlags & RX_CHKSUM_FLAGS_LOOPBACK)? 'Y': 'N',
			(rxChksumFlags & RX_CHKSUM_FLAGS_IP_CHECKSUM_SUCCEEDED)? 'Y':
		(rxChksumFlags & RX_CHKSUM_FLAGS_IP_CHECKSUM_FAILED)? 'N': '-',
			(rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_SUCCEEDED)? 'Y':
		(rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_FAILED)? 'N': '-',
			(rxChksumFlags & RX_CHKSUM_FLAGS_UDP_CHECKSUM_SUCCEEDED)? 'Y':
		(rxChksumFlags & RX_CHKSUM_FLAGS_UDP_CHECKSUM_FAILED)? 'N': '-') );

		packet_info.Value = 0;

		if( rxChksumFlags & RX_CHKSUM_FLAGS_IP_CHECKSUM_SUCCEEDED )
			packet_info.Receive.NdisPacketIpChecksumSucceeded = TRUE;
		else if( rxChksumFlags & RX_CHKSUM_FLAGS_IP_CHECKSUM_FAILED )
			packet_info.Receive.NdisPacketIpChecksumFailed = TRUE;

		if( rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_SUCCEEDED )
			packet_info.Receive.NdisPacketTcpChecksumSucceeded = TRUE;
		else if( rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_FAILED )
			packet_info.Receive.NdisPacketTcpChecksumFailed = TRUE;

		if( rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_SUCCEEDED )
			packet_info.Receive.NdisPacketUdpChecksumSucceeded = TRUE;
		else if( rxChksumFlags & RX_CHKSUM_FLAGS_TCP_CHECKSUM_FAILED )
			packet_info.Receive.NdisPacketUdpChecksumFailed = TRUE;

		NDIS_PER_PACKET_INFO_FROM_PACKET( p_packet, TcpIpChecksumPacketInfo )=
			(void *)(uintn_t)packet_info.Value;
	}

	NDIS_SET_PACKET_STATUS( p_packet, NDIS_STATUS_SUCCESS );
	NDIS_SET_PACKET_HEADER_SIZE( p_packet, sizeof(eth_hdr_t) );
	VNIC_RECV_FROM_PACKET( p_packet ) = pRdmaDest;

	VNIC_EXIT( VNIC_DBG_DATA );
	return TRUE;
}

/* NOTE: This routine is not reentrant */
static void
_data_allocBuffers(
		IN			Data_t		*pData,
		IN			BOOLEAN		initialAllocation )
{
	NDIS_STATUS		ndis_status;
	RecvPool_t     *p_recvPool = &pData->recvPool;
	RdmaDest_t     *pRdmaDest;
	LIST_ENTRY		*p_list_entry;
	int            index;

	VNIC_ENTER( VNIC_DBG_DATA );

	index = ADD(p_recvPool->nextFreeBuf, p_recvPool->numFreeBufs, p_recvPool->eiocPoolSz);

	while ( !IsListEmpty( &p_recvPool->availRecvBufs ) )
	{
		p_list_entry = RemoveHeadList( &p_recvPool->availRecvBufs );
		pRdmaDest	= (RdmaDest_t*)p_list_entry;
	
		if( initialAllocation )
		{
			pRdmaDest->buf_sz = p_recvPool->bufferSz;
			pRdmaDest->pTrailer =
				(struct ViportTrailer*)(pRdmaDest->data + pRdmaDest->buf_sz
				- sizeof(struct ViportTrailer));
			pRdmaDest->pTrailer->connectionHashAndValid = 0;

			NdisAllocatePacket( &ndis_status,
								&pRdmaDest->p_packet,
								pData->h_recv_pkt_pool );

			if ( ndis_status != NDIS_STATUS_SUCCESS )
			{
				VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
					( "NdisAllocatePacket failed %#x\n", ndis_status ) );
				return;
			}
		}

		_data_addFreeBuffer( pData, index, pRdmaDest );
		index = NEXT(index,p_recvPool->eiocPoolSz);
	}

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static void
_data_addFreeBuffer(
		IN		Data_t			*pData,
		IN		int				index,
	    IN		RdmaDest_t		*pRdmaDest )
{
	RecvPool_t        *p_recvPool = &pData->recvPool;
	BufferPoolEntry_t *pBpe;

	pRdmaDest->pTrailer->connectionHashAndValid = 0;
	pBpe = &p_recvPool->bufPool[index];

	pBpe->rKey       = pRdmaDest->region.rkey;
	pBpe->remoteAddr = hton64( (ULONG_PTR)pRdmaDest->data );
	pBpe->valid      = (uint32_t)(pRdmaDest - &p_recvPool->pRecvBufs[0]) + 1;
	++p_recvPool->numFreeBufs;

	return;
}

static uint32_t
_data_incomingRecv(
		IN		Data_t		*pData )
{
	RecvPool_t        *p_recvPool = &pData->recvPool;
	RdmaDest_t        *pRdmaDest;
	ViportTrailer_t   *pTrailer;
	BufferPoolEntry_t *pBpe;
	uint32_t		idx = 0;
	BOOLEAN status = FALSE;

	VNIC_ENTER( VNIC_DBG_DATA );

	while( !status )
	{
		if ( p_recvPool->nextFullBuf == p_recvPool->nextFreeBuf )
			return idx;

		pBpe = &p_recvPool->bufPool[p_recvPool->nextFullBuf];

		CL_ASSERT(pBpe->valid != 0 );

		pRdmaDest = &p_recvPool->pRecvBufs[pBpe->valid - 1];
		pTrailer = pRdmaDest->pTrailer;

		if ( ( pTrailer != NULL ) &&
			( pTrailer->connectionHashAndValid & CHV_VALID ) )
		{
			/* received a packet */
			if ( pTrailer->pktFlags & PF_KICK )
			{
				p_recvPool->kickOnFree = TRUE;
			}
			/* we do not want to indicate packet if filter is not set */
			if( pData->p_viport->p_adapter->xmitStarted &&
				pData->p_viport->p_adapter->packet_filter &&
				p_recvPool->numPostedBufs > 0 )
			{
				if ( _data_recv_to_ndis_pkt( pData, pRdmaDest ) )
				{
					p_recvPool->recv_pkt_array[idx++] = pRdmaDest->p_packet;
				}
			}
			else
			{	/* put back to free buffers pool */
				InsertTailList( &p_recvPool->availRecvBufs,
					&pRdmaDest->listPtrs );
			}
			pBpe->valid = 0;
			INC( p_recvPool->nextFullBuf, 1, p_recvPool->eiocPoolSz );
			p_recvPool->numPostedBufs--;
#ifdef VNIC_STATISTIC
			pData->statistics.recvNum++;
#endif /* VNIC_STATISTIC */
		}
		else
			break;
	}
	return idx;
}


void
vnic_return_packet(
	IN		NDIS_HANDLE		adapter_context,
	IN		NDIS_PACKET	* const p_packet )
{

	RdmaDest_t	*p_rdma_dest;
	viport_t	*p_viport;
	
	VNIC_ENTER( VNIC_DBG_DATA );

	UNREFERENCED_PARAMETER( adapter_context );

	p_rdma_dest = VNIC_RECV_FROM_PACKET( p_packet );
	p_viport = p_rdma_dest->p_viport;

	ASSERT( p_rdma_dest->p_packet == p_packet );	

	NdisAcquireSpinLock( &p_viport->data.recvIosLock );
	
	_data_return_recv( p_packet );
	
	NdisReinitializePacket( p_packet );
	
	InsertTailList( &p_viport->data.recvPool.availRecvBufs,
					&p_rdma_dest->listPtrs );
	_data_allocBuffers( &p_viport->data, FALSE );
	_data_sendFreeRecvBuffers( &p_viport->data );

	NdisReleaseSpinLock(&p_viport->data.recvIosLock );

	VNIC_EXIT( VNIC_DBG_DATA );
}
static void
_data_return_recv(
	  IN	NDIS_PACKET		*p_packet )
{
	NDIS_BUFFER		*p_buf;
	PNDIS_PACKET_OOB_DATA p_oob_data;
  
	/* Unchain the NDIS buffer. */
	NdisUnchainBufferAtFront( p_packet, &p_buf );
	if( p_buf )
	{
		/* Return the NDIS packet and NDIS buffer to their pools. */
		NdisFreeBuffer( p_buf );
	}
	/* take care of OOB_DATA block since NdisReinitializePacket doesn't */
	p_oob_data = NDIS_OOB_DATA_FROM_PACKET( p_packet );
	NdisZeroMemory( p_oob_data, sizeof( NDIS_PACKET_OOB_DATA ));
}

static void
_data_sendFreeRecvBuffers(
			IN			Data_t		*pData )
{
	RecvPool_t  *p_recvPool = &pData->recvPool;
	ib_send_wr_t *pWrq = &pData->freeBufsIo.io.wrq;
	BOOLEAN     bufsSent = FALSE;
	uint64_t    rdmaAddr;
	uint32_t    offset;
	uint32_t    sz;
	unsigned int numToSend,
	            nextIncrement;

	VNIC_ENTER( VNIC_DBG_DATA );

	for (	numToSend = p_recvPool->szFreeBundle;
			numToSend <= p_recvPool->numFreeBufs;
			numToSend += p_recvPool->szFreeBundle )
	{

		/* Handle multiple bundles as one when possible. */
		nextIncrement = numToSend + p_recvPool->szFreeBundle;
		if (( nextIncrement <= p_recvPool->numFreeBufs )
				&& ( p_recvPool->nextFreeBuf + nextIncrement <= p_recvPool->eiocPoolSz ) )
		{
			continue;
		}

		offset   = p_recvPool->nextFreeBuf * sizeof(BufferPoolEntry_t);
		sz       = numToSend * sizeof(BufferPoolEntry_t);
		rdmaAddr = p_recvPool->eiocRdmaAddr + offset;
	
		pWrq->ds_array->length  = sz;
		pWrq->ds_array->vaddr = (ULONG_PTR)((uint8_t *)p_recvPool->bufPool + offset);
		pWrq->remote_ops.vaddr = rdmaAddr;

		if ( ibqp_postSend( &pData->qp, &pData->freeBufsIo.io ) != IB_SUCCESS )
		{
			VNIC_TRACE(VNIC_DBG_ERROR,
					("Unable to rdma free buffers to EIOC\n") );

			viport_failure( pData->p_viport );
			break;
		}

		INC( p_recvPool->nextFreeBuf, numToSend, p_recvPool->eiocPoolSz );
		p_recvPool->numFreeBufs -= numToSend;
		p_recvPool->numPostedBufs += numToSend;
		bufsSent = TRUE;
	}

	if( bufsSent )
	{
		if( p_recvPool->kickOnFree )
		{
			data_sendKickMessage( pData );
		}
	}
	if( p_recvPool->numPostedBufs == 0 )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Unable to allocate receive buffers NumFreeBufs: %d nextFreeBuf: %d nextFullBuf %d eiocPoolSz: %d\n",
			pData->p_viport->ioc_num, p_recvPool->numFreeBufs,
			p_recvPool->nextFreeBuf, p_recvPool->nextFullBuf,
			p_recvPool->eiocPoolSz ) );
	
		//viport_failure( pData->p_viport );
	}
	VNIC_EXIT( VNIC_DBG_DATA );
}


void
data_cleanup(
	IN			Data_t	*pData )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	VNIC_TRACE(VNIC_DBG_DATA,
			("IOC[%d]data cleanup\n", pData->p_viport->ioc_num ));

	if( pData->p_recv_bufs )
	{
		NdisFreeMemory( pData->p_recv_bufs, pData->recv_bufs_sz, 0 );
		pData->p_recv_bufs = NULL;
	}

	if( pData->recvPool.recv_pkt_array )
	{
		cl_free( pData->recvPool.recv_pkt_array );
		pData->recvPool.recv_pkt_array = NULL;
		pData->recvPool.poolSz = 0;
	}

	if ( pData->pLocalStorage )
	{
		NdisFreeMemory( pData->pLocalStorage, pData->localStorageSz, 0 );
		pData->pLocalStorage = NULL;
	}

	if(  pData->h_recv_buf_pool )
	{
		NdisFreeBufferPool( pData->h_recv_buf_pool );
		pData->h_recv_buf_pool = NULL;
	}

	if ( pData->h_recv_pkt_pool )
	{
		if( NdisPacketPoolUsage(pData->h_recv_pkt_pool) != 0)
		{
			VNIC_TRACE( VNIC_DBG_WARN,
				("Recv packet pool is not empty!!!\n") );
		}
		NdisFreePacketPool( pData->h_recv_pkt_pool );
		pData->h_recv_pkt_pool = NULL;
	}
	// clear Qp struct for reuse
	cl_memclr( &pData->qp, sizeof( IbQp_t) );

	cl_timer_destroy( &pData->kickTimer );

	VNIC_EXIT( VNIC_DBG_DATA );
}


static void
_data_kickTimer_start(
		  IN	Data_t		*pData,
		  IN	uint32_t	microseconds )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	InterlockedExchange( (LONG *)&pData->kickTimerOn, TRUE );

	usec_timer_start(&pData->kickTimer, microseconds );

	VNIC_EXIT( VNIC_DBG_DATA );
	return;
}

static void
_data_kickTimer_stop(
		  IN	Data_t		*pData )
{
	VNIC_ENTER( VNIC_DBG_DATA );

	if( InterlockedExchange( &pData->kickTimerOn, FALSE ) == TRUE )
	{
		cl_timer_stop( &pData->kickTimer );
	}

	VNIC_EXIT( VNIC_DBG_DATA );
}
