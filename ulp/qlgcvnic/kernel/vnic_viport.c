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

#include "iba/ib_al.h"
#include "vnic_util.h"
#include "vnic_driver.h"
#include "vnic_viport.h"
#include "vnic_control.h"
#include "vnic_data.h"
#include "vnic_config.h"
#include "vnic_controlpkt.h"

extern  vnic_globals_t g_vnic;

static void
viport_timeout( 
		void		*context );

static ib_api_status_t 
viport_initMacAddresses( 
		viport_t	*p_viport );

BOOLEAN
viport_config_defaults(
			IN			viport_t			*p_viport )
{

	vnic_adapter_t	*p_adapter = p_viport->p_adapter;
	ViportConfig_t	*pConfig = &p_viport->port_config;
	ControlConfig_t	*pControlConfig = &p_viport->port_config.controlConfig;
	DataConfig_t	*pDataConfig  = &p_viport->port_config.dataConfig;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	p_viport->state     = VIPORT_DISCONNECTED;
	p_viport->linkState = LINK_RETRYWAIT;
	p_viport->newMtu    = 1500;
	p_viport->newFlags  = 0;
	p_viport->hb_send_pending = FALSE;

	cl_timer_init( &p_viport->timer, viport_timeout, p_viport );

	pConfig->statsInterval	= p_adapter->params.ViportStatsInterval;
	pConfig->hbInterval		= p_adapter->params.ViportHbInterval;
	pConfig->hbTimeout		= p_adapter->params.ViportHbTimeout;

	cl_memcpy ( pConfig->ioc_string,
				p_adapter->ioc_info.profile.id_string,
				min( sizeof( p_adapter->ioc_info.profile.id_string ),
					 sizeof( pConfig->ioc_string )) );

	pControlConfig->ibConfig.sid = 0; /* will set it later, from svc entries */
	pControlConfig->ibConfig.connData.pathId		= 0;
	pControlConfig->ibConfig.connData.inicInstance	= 0; // assign instance id in update_path
	pControlConfig->ibConfig.connData.pathNum		= 0;

	pControlConfig->ibConfig.retryCount		= p_adapter->params.RetryCount;
	pControlConfig->ibConfig.rnrRetryCount	= p_adapter->params.RetryCount;
	pControlConfig->ibConfig.minRnrTimer	= (uint8_t)p_adapter->params.MinRnrTimer;
	pControlConfig->ibConfig.numRecvs		= 5; /* Not configurable */
	pControlConfig->ibConfig.numSends		= 1; /* Not configurable */
	pControlConfig->ibConfig.recvScatter	= 1; /* Not configurable */
	pControlConfig->ibConfig.sendGather		= 1; /* Not configurable */

	/* indicate new features support capabilities */
	pControlConfig->ibConfig.connData.featuresSupported =
				hton32((uint32_t)(INIC_FEAT_IGNORE_VLAN | INIC_FEAT_RDMA_IMMED ));

	cl_memcpy ( pControlConfig->ibConfig.connData.nodename,
				g_vnic.host_name,
				min( sizeof( g_vnic.host_name ),
					sizeof( pControlConfig->ibConfig.connData.nodename )) );

	pControlConfig->numRecvs = pControlConfig->ibConfig.numRecvs;

	pControlConfig->inicInstance		= pControlConfig->ibConfig.connData.inicInstance;
	pControlConfig->maxAddressEntries	= (uint16_t)p_adapter->params.MaxAddressEntries;
	pControlConfig->minAddressEntries	= (uint16_t)p_adapter->params.MinAddressEntries;
	pControlConfig->reqRetryCount		= (uint8_t)p_adapter->params.ControlReqRetryCount;
	pControlConfig->rspTimeout			= p_adapter->params.ControlRspTimeout;

	pDataConfig->ibConfig.sid = 0;  /* will set it later, from svc entries */
	pDataConfig->ibConfig.connData.pathId		= get_time_stamp_ms();
	pDataConfig->ibConfig.connData.inicInstance	= pControlConfig->inicInstance;
	pDataConfig->ibConfig.connData.pathNum		= 0;

	pDataConfig->ibConfig.retryCount			= p_adapter->params.RetryCount;
	pDataConfig->ibConfig.rnrRetryCount			= p_adapter->params.RetryCount;
	pDataConfig->ibConfig.minRnrTimer			= (uint8_t)p_adapter->params.MinRnrTimer;

	/*
	 * NOTE: The numRecvs size assumes that the EIOC could
	 * RDMA enough packets to fill all of the host recv
	 * pool entries, plus send a kick message after each
	 * packet, plus RDMA new buffers for the size of
	 * the EIOC recv buffer pool, plus send kick messages
	 * after each MinHostUpdateSz of new buffers all
	 * before the Host can even pull off the first completed
	 * receive off the completion queue, and repost the
	 * receive. NOT LIKELY!
	 */
	pDataConfig->ibConfig.numRecvs = p_adapter->params.HostRecvPoolEntries +
		( p_adapter->params.MaxEiocPoolSz / p_adapter->params.MinHostUpdateSz );

#if LIMIT_OUTSTANDING_SENDS

	pDataConfig->ibConfig.numSends =  (2 * p_adapter->params.NotifyBundleSz ) +
		( p_adapter->params.HostRecvPoolEntries / p_adapter->params.MinEiocUpdateSz ) + 1;

#else /* !defined(LIMIT_OUTSTANDING_SENDS) */
	/*
	 * NOTE: The numSends size assumes that the HOST could
	 * post RDMA sends for every single buffer in the EIOCs
	 * receive pool, and allocate a full complement of
	 * receive buffers on the host, and RDMA free buffers
	 * every MinEiocUpdateSz entries all before the HCA
	 * can complete a single RDMA transfer. VERY UNLIKELY,
	 * BUT NOT COMPLETELY IMPOSSIBLE IF THERE IS AN IB
	 * PROBLEM!
	 */
	pDataConfig->ibConfig.numSends = p_adapter->params.MaxEiocPoolSz +
		( p_adapter->params.HostRecvPoolEntries / p_adapter->params.MinEiocUpdateSz ) + 1;

#endif /* !defined(LIMIT_OUTSTANDING_SENDS) */

	pDataConfig->ibConfig.recvScatter		= 1; /* Not configurable */
	pDataConfig->ibConfig.sendGather		= MAX_NUM_SGE; /* Not configurable */

	pDataConfig->numRecvs			= pDataConfig->ibConfig.numRecvs;
	pDataConfig->pathId				= pDataConfig->ibConfig.connData.pathId;

	pDataConfig->hostMin.sizeRecvPoolEntry =
		(uint32_t)BUFFER_SIZE(ETH_VLAN_HLEN + p_adapter->params.MinMtu);
	pDataConfig->hostMax.sizeRecvPoolEntry =
		(uint32_t)BUFFER_SIZE(ETH_VLAN_HLEN + p_adapter->params.MaxMtu);
	pDataConfig->eiocMin.sizeRecvPoolEntry =
		(uint32_t)BUFFER_SIZE(ETH_VLAN_HLEN + p_adapter->params.MinMtu);
	pDataConfig->eiocMax.sizeRecvPoolEntry = MAX_PARAM_VALUE;

	pDataConfig->hostRecvPoolEntries	= p_adapter->params.HostRecvPoolEntries;
	pDataConfig->notifyBundle			= p_adapter->params.NotifyBundleSz;

	pDataConfig->hostMin.numRecvPoolEntries = p_adapter->params.MinHostPoolSz;
	pDataConfig->hostMax.numRecvPoolEntries = MAX_PARAM_VALUE;
	pDataConfig->eiocMin.numRecvPoolEntries = p_adapter->params.MinEiocPoolSz;
	pDataConfig->eiocMax.numRecvPoolEntries = p_adapter->params.MaxEiocPoolSz;

	pDataConfig->hostMin.timeoutBeforeKick = p_adapter->params.MinHostKickTimeout;
	pDataConfig->hostMax.timeoutBeforeKick = p_adapter->params.MaxHostKickTimeout;
	pDataConfig->eiocMin.timeoutBeforeKick = 0;
	pDataConfig->eiocMax.timeoutBeforeKick = MAX_PARAM_VALUE;

	pDataConfig->hostMin.numRecvPoolEntriesBeforeKick = p_adapter->params.MinHostKickEntries;
	pDataConfig->hostMax.numRecvPoolEntriesBeforeKick = p_adapter->params.MaxHostKickEntries;
	pDataConfig->eiocMin.numRecvPoolEntriesBeforeKick = 0;
	pDataConfig->eiocMax.numRecvPoolEntriesBeforeKick = MAX_PARAM_VALUE;

	pDataConfig->hostMin.numRecvPoolBytesBeforeKick = p_adapter->params.MinHostKickBytes;
	pDataConfig->hostMax.numRecvPoolBytesBeforeKick = p_adapter->params.MaxHostKickBytes;
	pDataConfig->eiocMin.numRecvPoolBytesBeforeKick = 0;
	pDataConfig->eiocMax.numRecvPoolBytesBeforeKick = MAX_PARAM_VALUE;

	pDataConfig->hostMin.freeRecvPoolEntriesPerUpdate = p_adapter->params.MinHostUpdateSz;
	pDataConfig->hostMax.freeRecvPoolEntriesPerUpdate = p_adapter->params.MaxHostUpdateSz;
	pDataConfig->eiocMin.freeRecvPoolEntriesPerUpdate = p_adapter->params.MinEiocUpdateSz;
	pDataConfig->eiocMax.freeRecvPoolEntriesPerUpdate = p_adapter->params.MaxEiocUpdateSz;

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return TRUE;
}

static BOOLEAN config_isValid(ViportConfig_t *pConfig)
{
	UNREFERENCED_PARAMETER( pConfig );
	return TRUE;
}


void
viport_cleanup(
			  viport_t	*p_viport )
{
	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( p_viport )
	{
		if( InterlockedExchange( (volatile LONG *)&p_viport->disconnect, TRUE ) )
		{
			return;
		}

		VNIC_TRACE(VNIC_DBG_VIPORT,
			("IOC[%d]viport cleanup\n", p_viport->ioc_num ));

		data_disconnect( &p_viport->data );

		control_cleanup( &p_viport->control );

		data_cleanup( &p_viport->data );

		if( p_viport->macAddresses != NULL )
		{
			NdisFreeMemory( p_viport->macAddresses,
				p_viport->numMacAddresses * sizeof(Inic_AddressOp_t), 0 );
		}

		cl_timer_destroy( &p_viport->timer );

		NdisFreeMemory( p_viport, sizeof(viport_t), 0 );
		p_viport = NULL;
	}

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

BOOLEAN
viport_setParent(
		IN		viport_t		*p_viport,
		IN		Netpath_t		*pNetpath )
{
	VNIC_ENTER( VNIC_DBG_VIPORT );


	if(p_viport->p_netpath != NULL)
	{
		return FALSE;
	}

	p_viport->p_netpath = pNetpath;

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return TRUE;
}

BOOLEAN
viport_unsetParent(
		IN		viport_t		*p_viport )
{
	if( !p_viport || 
		!p_viport->p_netpath )
	{
		return FALSE;
	}
	InterlockedExchange( &p_viport->p_netpath->carrier, FALSE );
	p_viport->p_netpath->pViport = NULL;
	p_viport->p_netpath = NULL;
	
	if( p_viport->state == VIPORT_CONNECTED )
	{
		p_viport->p_adapter->num_paths--;
	}
	return TRUE;
}


NDIS_STATUS
viport_setLink(
		IN		viport_t		*p_viport,
		IN		uint8_t			flags,
		IN		uint16_t		mtu	,
		IN		BOOLEAN			sync )
{

	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( mtu > data_maxMtu(&p_viport->data) )
	{
		viport_failure(p_viport);
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("IOC[%d] Configuration error. Mtu of %d unsupported\n", p_viport->ioc_num, mtu ) );
		return NDIS_STATUS_FAILURE;
	}

	NdisAcquireSpinLock( &p_viport->lock );
	
	if( ( (p_viport->newFlags & flags ) != flags ) || 
		( p_viport->newMtu != mtu ) )
	{
		p_viport->newFlags = flags;
		p_viport->newMtu = mtu;
		InterlockedOr( &p_viport->updates, NEED_LINK_CONFIG );
	
		NdisReleaseSpinLock( &p_viport->lock );
	
		status = _viport_process_query( p_viport, sync );
	}
	else
		NdisReleaseSpinLock( &p_viport->lock );

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return status;
}

BOOLEAN
viport_setUnicast(
		IN		viport_t* const	p_viport,
		IN		uint8_t			*p_address )
{
	NDIS_STATUS	status = NDIS_STATUS_SUCCESS;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( !p_viport )
		return FALSE;

	NdisAcquireSpinLock( &p_viport->lock );

	if( p_viport->macAddresses == NULL )
	{
		NdisReleaseSpinLock( &p_viport->lock );
		return FALSE;
	}

	if( cl_memcmp(p_viport->macAddresses[UNICAST_ADDR].address,
					p_address, MAC_ADDR_LEN ) )
	{

		cl_memcpy( p_viport->macAddresses[UNICAST_ADDR].address,
		       p_address, MAC_ADDR_LEN );

		VNIC_TRACE( VNIC_DBG_VIPORT,
			("Change Viport MAC From : %02x %02x %02x %02x %02x %02x -> to :  %02x %02x %02x %02x %02x %02x\n",
				p_viport->hwMacAddress[0],	p_viport->hwMacAddress[1],	p_viport->hwMacAddress[2],
				p_viport->hwMacAddress[3],	p_viport->hwMacAddress[4],	p_viport->hwMacAddress[5],
				p_viport->macAddresses[UNICAST_ADDR].address[0], p_viport->macAddresses[UNICAST_ADDR].address[1],
				p_viport->macAddresses[UNICAST_ADDR].address[2], p_viport->macAddresses[UNICAST_ADDR].address[3],
				p_viport->macAddresses[UNICAST_ADDR].address[4], p_viport->macAddresses[UNICAST_ADDR].address[5] 
				) );

		NdisMoveMemory( p_viport->hwMacAddress,
						p_viport->macAddresses[UNICAST_ADDR].address, MAC_ADDR_LEN );
		
		p_viport->macAddresses[UNICAST_ADDR].operation = INIC_OP_SET_ENTRY;

		InterlockedOr( &p_viport->updates, NEED_ADDRESS_CONFIG );

		NdisReleaseSpinLock( &p_viport->lock );

		status = _viport_process_query( p_viport, FALSE );
		if( status != NDIS_STATUS_SUCCESS &&
			status != NDIS_STATUS_PENDING )
		{
			VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("Viport MAC set failed\n") );
			return FALSE;
		}
	}

	NdisReleaseSpinLock( &p_viport->lock );

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return TRUE;
}


/* Returns flags for state machine operations. */
NDIS_STATUS
viport_setMulticast(
		IN		viport_t*	const	p_viport )
{
	vnic_adapter_t	*p_adapter = p_viport->p_adapter;
	uint32_t		updates = 0;
	int				i;
	NDIS_STATUS		status;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	NdisAcquireSpinLock( &p_viport->lock );

	if( p_viport->macAddresses == NULL )
	{
		NdisReleaseSpinLock( &p_viport->lock );
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	ASSERT( (p_viport->updates & ~MCAST_OVERFLOW) == 0 );

	if( p_adapter->mc_count == 0 )
	{
		/* invalidate all entries for the remote */
		for (i = MCAST_ADDR_START;
			 i < min( MAX_ADDR_ARRAY, p_viport->numMacAddresses ); i++ )
		{
			p_viport->macAddresses[i].valid = 0;
			p_viport->macAddresses[i].operation = INIC_OP_SET_ENTRY;
		}
		/* do we have to report to remote ? */
		p_viport->updates = 0;
		NdisReleaseSpinLock( &p_viport->lock );
		return NDIS_STATUS_SUCCESS;
	}

	if( p_adapter->mc_count > p_viport->numMacAddresses - MCAST_ADDR_START )
	{
		updates |= NEED_LINK_CONFIG | MCAST_OVERFLOW;
	}
	else
	{
		if( InterlockedAnd(
			&p_viport->updates, ~MCAST_OVERFLOW ) & MCAST_OVERFLOW )
		{
			updates |= NEED_LINK_CONFIG;
		}
		/* Brute force algorithm */
		for (i = MCAST_ADDR_START;
			i < min( MAX_ADDR_ARRAY, p_adapter->mc_count + MCAST_ADDR_START );
			i++ )
		{
			if( p_viport->macAddresses[i].valid &&
				NdisEqualMemory( p_viport->macAddresses[i].address,
				p_adapter->mcast_array[i - MCAST_ADDR_START].addr,
				MAC_ADDR_LEN ) )
			{
				continue;
			}

			NdisMoveMemory( &p_viport->macAddresses[i].address,
				p_adapter->mcast_array[i - MCAST_ADDR_START].addr,
				MAC_ADDR_LEN );

			p_viport->macAddresses[i].valid = 1;
			p_viport->macAddresses[i].operation = INIC_OP_SET_ENTRY;

			updates |= NEED_ADDRESS_CONFIG;
		}
		for (; i < min( MAX_ADDR_ARRAY, p_viport->numMacAddresses ); i++ )
		{
			if( !p_viport->macAddresses[i].valid )
				continue;

			updates |= NEED_ADDRESS_CONFIG;

			p_viport->macAddresses[i].valid = 0;
			p_viport->macAddresses[i].operation = INIC_OP_SET_ENTRY;
		}
	}

	/*
	 * Now that the mac array is setup, we can set the update bits
	 * to send the request.
	 */
	InterlockedOr( &p_viport->updates, updates );
	NdisReleaseSpinLock( &p_viport->lock );

	status = _viport_process_query( p_viport, FALSE );

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return status;
}

NDIS_STATUS
viport_getStats(
		IN		viport_t		*p_viport )
{
	uint64_t		stats_update_ms;
	NDIS_STATUS		status = STATUS_SUCCESS;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	stats_update_ms = get_time_stamp_ms();

	if( stats_update_ms > p_viport->lastStatsTime + p_viport->port_config.statsInterval )
	{
		p_viport->lastStatsTime = (uint32_t)stats_update_ms;

		InterlockedOr( &p_viport->updates, NEED_STATS );
		
		status = _viport_process_query( p_viport, FALSE );
		if ( status != NDIS_STATUS_SUCCESS )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
					("Query NEED_STATS Failed\n") );
		}
	}

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return status;
}


BOOLEAN
viport_xmitPacket(
		IN		viport_t*	const	p_viport,
		IN		NDIS_PACKET* const	p_packet )
{
	BOOLEAN status;
	LIST_ENTRY	*p_list_item;
	NDIS_PACKET	*p_pending_packet;
	vnic_adapter_t*	p_adapter;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	NdisAcquireSpinLock( &p_viport->lock );
	
	p_adapter = p_viport->p_netpath->p_adapter;

	while( ( p_list_item = NdisInterlockedRemoveHeadList(
				&p_adapter->send_pending_list,
				&p_adapter->pending_list_lock ) ) != NULL )
	{
		p_pending_packet = VNIC_PACKET_FROM_LIST_ITEM( p_list_item );

		status = data_xmitPacket( &p_viport->data, p_pending_packet );
		if( !status )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d] Xmit Pending Packet failed\n", p_viport->ioc_num ));

			/* put it back on pending list - will complete it on cleanup */
			NdisInterlockedInsertHeadList( 
								&p_adapter->send_pending_list,
								VNIC_LIST_ITEM_FROM_PACKET( p_pending_packet ),
								&p_adapter->pending_list_lock );
			/*do not try to send packet, just exit */
			goto err;
		}
	}

	/* send a packet */
	status = data_xmitPacket( &p_viport->data, p_packet );
	if( status )
	{
		NdisReleaseSpinLock( &p_viport->lock );
	
		VNIC_EXIT( VNIC_DBG_VIPORT );
		return status;
	}

err:
	NdisInterlockedInsertTailList( 
				&p_adapter->send_pending_list,
				VNIC_LIST_ITEM_FROM_PACKET( p_packet ),
				&p_adapter->pending_list_lock );
	
	viport_stopXmit( p_viport );

	NdisReleaseSpinLock( &p_viport->lock );
	
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return status;
}

void
viport_cancel_xmit( 
		viport_t	*p_viport, 
		PVOID		cancel_id )
{
	NDIS_PACKET	*p_packet;
	LIST_ENTRY	*p_list_item;
	LIST_ENTRY	*p_list;
	PVOID		_id;
	vnic_adapter_t*	p_adapter;

	NdisAcquireSpinLock( &p_viport->lock );

	p_adapter = p_viport->p_netpath->p_adapter;
	p_list = &p_adapter->send_pending_list;

	while( !IsListEmpty( &p_adapter->send_pending_list ) )
	{
		p_list_item = p_list->Flink;

		if ( p_list_item->Flink == &p_adapter->send_pending_list )
			break;

		p_packet = VNIC_PACKET_FROM_LIST_ITEM( p_list_item );

		_id = NdisGetPacketCancelId( p_packet );

		if( _id == cancel_id )
		{
			NdisInterlockedRemoveHeadList( p_list, &p_adapter->pending_list_lock );
			NdisInterlockedInsertTailList( &p_adapter->cancel_send_list, p_list_item,
											&p_adapter->cancel_list_lock );
			p_list_item = p_list->Flink;
		}
		else
		{
			p_list = p_list_item;
		}
	}

	while( !IsListEmpty( &p_adapter->cancel_send_list ) )
	{
		p_list_item = NdisInterlockedRemoveHeadList( &p_adapter->cancel_send_list,
			&p_adapter->cancel_list_lock );
		
		p_packet = VNIC_PACKET_FROM_LIST_ITEM( p_list_item );
		
		if( p_packet )
		{
			NDIS_SET_PACKET_STATUS( p_packet, NDIS_STATUS_REQUEST_ABORTED );
			NdisMSendComplete( p_adapter->h_handle,	p_packet, NDIS_STATUS_REQUEST_ABORTED );
		}
	}

	NdisReleaseSpinLock( &p_viport->lock );
}

void
viport_linkUp(viport_t *p_viport)
{
	VNIC_ENTER( VNIC_DBG_VIPORT );

	netpath_linkUp( p_viport->p_netpath );

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

void
viport_linkDown(viport_t *p_viport)
{
	VNIC_ENTER( VNIC_DBG_VIPORT );

	netpath_linkDown( p_viport->p_netpath );
	
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

void
viport_stopXmit(viport_t *p_viport)
{
	VNIC_ENTER( VNIC_DBG_VIPORT );
	netpath_stopXmit( p_viport->p_netpath );
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

void
viport_restartXmit(viport_t *p_viport)
{
	VNIC_ENTER( VNIC_DBG_VIPORT );
	netpath_restartXmit( p_viport->p_netpath );
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

void
viport_recvPacket(
		IN		viport_t	*p_viport,
		IN		NDIS_PACKET	 **pp_pkt_arr,
		IN		uint32_t	num_packets )
{
	VNIC_ENTER( VNIC_DBG_VIPORT );
	netpath_recvPacket(p_viport->p_netpath, pp_pkt_arr, num_packets );
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}


void
viport_failure(
	IN		viport_t	*p_viport )
{
	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( !p_viport || !p_viport->p_netpath )
		return;

	InterlockedExchange( &p_viport->p_netpath->carrier, FALSE );
	
	if( InterlockedExchange( (volatile LONG*)&p_viport->errored,  TRUE ) == FALSE )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC[%d] %s instance %d FAILED\n", 
				p_viport->ioc_num,
				netpath_to_string( p_viport->p_netpath ),
				p_viport->p_netpath->instance ));
		viport_stopXmit( p_viport );
		viport_linkDown( p_viport );
	}

	VNIC_EXIT( VNIC_DBG_VIPORT );
}


void
viport_timeout(
		IN		void	*context )
{
	viport_t 	*p_viport = (viport_t *)context;
	CL_ASSERT( p_viport );

	if( InterlockedExchange( &p_viport->timerActive, FALSE ) == TRUE )
	{
		// did we send a query and got response ?
		if( p_viport->link_hb_state != LINK_HEARTBEATRSP &&
			!p_viport->hb_send_pending )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC[%d] instance %d HEARTBEAT TIMEOUT\n", 
				p_viport->ioc_num,
				p_viport->p_netpath->instance ));
			viport_failure( p_viport );
			return;
		}
		/* send heartbeat message again */
		else if( !p_viport->errored &&
			p_viport->state == VIPORT_CONNECTED )
		{
			viport_timer( p_viport, p_viport->port_config.hbInterval );
		}
	}
}


void
viport_timer(
		IN		 viport_t	*p_viport,
		IN		int			timeout )
{
	ib_api_status_t	ib_status;

	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( !p_viport->errored )
	{
		p_viport->link_hb_state = LINK_HEARTBEATREQ;
		InterlockedExchange( &p_viport->timerActive, TRUE );
		
		/* we can send now */
		if( !p_viport->control.reqOutstanding )
		{
			p_viport->hb_send_pending = FALSE;
			cl_timer_start( &p_viport->timer, (uint32_t)timeout );			

			ib_status = control_heartbeatReq( &p_viport->control,
								p_viport->port_config.hbTimeout );

			if( ib_status != IB_SUCCESS )
			{
				viport_timerStop( p_viport );
				VNIC_TRACE( VNIC_DBG_ERROR,
					("IOC[%d] instance %d HEARTBEAT send failed\n", 
							p_viport->ioc_num,
							p_viport->p_netpath->instance ));
				viport_failure( p_viport );
				return;
			}
		}
		/* schedule send on next timeout */
		else
		{
			p_viport->hb_send_pending = TRUE;
			cl_timer_start(&p_viport->timer, (uint32_t)timeout );
		}
	}

	VNIC_EXIT( VNIC_DBG_VIPORT );
}


void
viport_timerStop(
		IN		viport_t	*p_viport )
{
	VNIC_ENTER( VNIC_DBG_VIPORT );

	if( p_viport )
	{
		if( InterlockedExchange( &p_viport->timerActive, FALSE ) == TRUE )
		{
			cl_timer_stop( &p_viport->timer );
		}
	}

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return;
}

static ib_api_status_t
viport_initMacAddresses(
			IN		viport_t		*p_viport )
{
	int		i, size;
	NDIS_STATUS	status;
	VNIC_ENTER( VNIC_DBG_VIPORT );

	size = p_viport->numMacAddresses * sizeof(Inic_AddressOp_t);
	status = NdisAllocateMemoryWithTag( &p_viport->macAddresses, size , 'acam' );

	if ( status != NDIS_STATUS_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Failed allocating MAC address table size %d\n", size) );
		return IB_INSUFFICIENT_MEMORY;
	}

	NdisZeroMemory( p_viport->macAddresses, size );

	NdisAcquireSpinLock( &p_viport->lock );
	for( i = 0; i < p_viport->numMacAddresses; i++ )
	{
		p_viport->macAddresses[i].index = (uint16_t)i;
		p_viport->macAddresses[i].vlan  = p_viport->defaultVlan;
	}

	NdisFillMemory( p_viport->macAddresses[BROADCAST_ADDR].address,
	       MAC_ADDR_LEN, 0xFF );
	p_viport->macAddresses[BROADCAST_ADDR].valid = TRUE;

	NdisMoveMemory( p_viport->macAddresses[UNICAST_ADDR].address,
	       p_viport->hwMacAddress, MAC_ADDR_LEN );

	p_viport->macAddresses[UNICAST_ADDR].valid   = TRUE;
	
	if( !p_viport->p_adapter->macSet )
	{
		p_viport->p_adapter->macSet = TRUE;
	}

	NdisReleaseSpinLock( &p_viport->lock );

	VNIC_EXIT( VNIC_DBG_VIPORT );
	return IB_SUCCESS;
}


ib_api_status_t
viport_control_connect(
	IN		viport_t*	const	p_viport )
{
	ib_api_status_t	ib_status;
	cl_status_t		cl_status;

	VNIC_ENTER( VNIC_DBG_INIT );

	ib_status = control_init( &p_viport->control, p_viport,
		&p_viport->port_config.controlConfig, p_viport->portGuid );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_EXIT( VNIC_DBG_VIPORT );
		return ib_status;
	}

	ib_status = ibqp_connect( &p_viport->control.qp );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("control QP connect failed\n"));
		control_cleanup( &p_viport->control );
		return ib_status;
	}

	InterlockedExchange( (volatile LONG*)&p_viport->linkState,
		(LONG)LINK_INITINICREQ );

	ib_status = control_initInicReq( &p_viport->control );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR, 
			("CMD_INIT_INIC  REQ failed\n") );

		control_cleanup( &p_viport->control );
		return ib_status;
	}
	cl_status = cl_event_wait_on( &p_viport->sync_event,
						(p_viport->control.p_conf->rspTimeout << 11), FALSE );

	if( p_viport->linkState != LINK_INITINICRSP )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("CMD_INIT_INIC RSP failed: return linkstate: %d, cl_status: %d\n",
			p_viport->linkState, cl_status ));

		ib_status = IB_INSUFFICIENT_RESOURCES;
		control_cleanup( &p_viport->control );
		return ib_status;
	}

	vnic_resume_oids( p_viport->p_adapter );

	ib_status = viport_initMacAddresses( p_viport );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("Init MAC Addresses failed\n"));
		control_cleanup( &p_viport->control );
	}

	VNIC_EXIT( VNIC_DBG_INIT );
	return ib_status;
}

ib_api_status_t
viport_data_connect(
	IN		viport_t*	const	p_viport )
{
	NDIS_STATUS		status;
	ib_api_status_t	ib_status;

	VNIC_ENTER( VNIC_DBG_INIT );

	ib_status = data_init( &p_viport->data,
						 &p_viport->port_config.dataConfig,
						 p_viport->portGuid );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR, ("Data init returned %s\n",
			p_viport->p_adapter->ifc.get_err_str( ib_status )) );
		return ib_status;
	}
	InterlockedExchange( (volatile LONG*)&p_viport->linkState,
								(LONG)LINK_CONFIGDATAPATHREQ );

	ib_status = control_configDataPathReq( &p_viport->control,
		data_pathId(&p_viport->data ), data_hostPoolMax( &p_viport->data ),
		data_eiocPoolMax( &p_viport->data ) );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("command CONFIGDATAPATH REQ failed\n"));
		return ib_status;
	}
	cl_event_wait_on( &p_viport->sync_event,
					(p_viport->control.p_conf->rspTimeout << 11), TRUE );
	
	if( p_viport->linkState != LINK_CONFIGDATAPATHRSP )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
					("failed to get CONFIGDATAPATH RSP\n"));
		return IB_INSUFFICIENT_RESOURCES;
	}

	ib_status = data_connect( &p_viport->data );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_EXIT( VNIC_DBG_INIT );
		return ib_status;
	}
	cl_event_wait_on( &p_viport->sync_event,
					(p_viport->control.p_conf->rspTimeout << 11), TRUE );
	if( p_viport->data.qp.qpState != IB_ATTACHED )
	{
		VNIC_EXIT( VNIC_DBG_INIT );
		return IB_ERROR;
	}
	InterlockedExchange( (volatile LONG*)&p_viport->linkState,
									(LONG)LINK_XCHGPOOLREQ );
	ib_status = control_exchangePoolsReq( &p_viport->control,
		data_localPoolAddr(&p_viport->data),
		data_localPoolRkey(&p_viport->data) );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("command XCHGPOOL REQ failed\n"));
		return ib_status;
	}
	cl_event_wait_on( &p_viport->sync_event,
					(p_viport->control.p_conf->rspTimeout << 11), TRUE );

	if( p_viport->linkState != LINK_XCHGPOOLRSP )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("failed to get LINK_XCHGPOOL RSP\n"));
		return IB_ERROR;
	}

	InterlockedExchange( (volatile LONG*)&p_viport->linkState,
							(LONG)LINK_INITIALIZED );

	p_viport->state = VIPORT_CONNECTED;

	ib_status = data_connected( &p_viport->data );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
				("Alloc/Send Recv Buffers failed\n"));
		return ib_status;
	}

	p_viport->flags = 0;
	
	if( p_viport->p_netpath == p_viport->p_adapter->p_currentPath )
	{
		status = viport_setLink( p_viport, 	
							INIC_FLAG_ENABLE_NIC| INIC_FLAG_SET_MTU,
							(uint16_t)p_viport->p_adapter->params.MinMtu, TRUE );
		if( status != NDIS_STATUS_SUCCESS )
		{
			VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
					("failed to set Link flags\n"));
			return IB_ERROR;
		}

		/* start periodic heartbeat timer for active path */
		if( p_viport->port_config.hbInterval )
		{
			viport_timer( p_viport, p_viport->port_config.hbInterval );
		}
	}
	VNIC_EXIT( VNIC_DBG_INIT );
	return IB_SUCCESS;
}


NDIS_STATUS
_viport_process_query(
		IN		viport_t*	const	p_viport,
		IN		BOOLEAN				sync )
{
	NDIS_STATUS		status;
	ib_api_status_t	ib_status;
	LinkState_t		expected_state = 0;

	VNIC_ENTER( VNIC_DBG_VIPORT );
	
	if( p_viport->errored ||
		p_viport->p_adapter->pnp_state == IB_PNP_IOC_REMOVE )
	{
		VNIC_TRACE( VNIC_DBG_WARN,
			("IOC[%d] updates = %#x packet_filter = %#x, query: %#x(%d) set: %#x(%d)\n",
					p_viport->ioc_num,
					p_viport->updates,
					 p_viport->p_adapter->packet_filter,
					 p_viport->p_adapter->query_oid.oid,
					 p_viport->p_adapter->pending_query,
					 p_viport->p_adapter->set_oid.oid,
					 p_viport->p_adapter->pending_set ));

		VNIC_TRACE_EXIT( VNIC_DBG_WARN, ("IOC[%d] in REMOVE or invalid state.\n", 
				p_viport->ioc_num ));
		
		return NDIS_STATUS_NOT_ACCEPTED;
	}

	// Check for updates.  Note that unless sync is set to TRUE, this
	// is the only way for this function to return success.
	if( !InterlockedCompareExchange( &p_viport->updates, 0, 0 ) )
	{
		/*  now can restart heartbeats */
		if( !p_viport->timerActive &&
			p_viport->port_config.hbInterval )
		{
			viport_timer( p_viport, p_viport->port_config.hbInterval );
		}
		return NDIS_STATUS_SUCCESS;
	}
	
	else
	{
		/* stop heartbeat timer to serve another query */
		viport_timerStop( p_viport );
	}

	if( sync )
	{
		status = NDIS_STATUS_SUCCESS;
		InterlockedOr( &p_viport->updates, SYNC_QUERY );
	}
	else
	{
		status = NDIS_STATUS_PENDING;
	}
	
	// Handle update bits one at a time.
	if( p_viport->updates & NEED_ADDRESS_CONFIG )
	{
		VNIC_TRACE( VNIC_DBG_CONF,("QUERY NEED_ADDRESS_CONFIG\n"));
		
		NdisAcquireSpinLock(&p_viport->lock );

		p_viport->linkState = LINK_CONFIGADDRSREQ;
		ib_status = control_configAddrsReq(
			&p_viport->control, p_viport->macAddresses,
			p_viport->numMacAddresses, &p_viport->addrs_query_done );
		
		NdisReleaseSpinLock( &p_viport->lock );

		if ( ib_status != IB_SUCCESS )
		{
			InterlockedAnd( &p_viport->updates, ~NEED_ADDRESS_CONFIG );
			VNIC_EXIT( VNIC_DBG_VIPORT );
			return NDIS_STATUS_FAILURE;
		}
		expected_state = LINK_CONFIGADDRSRSP;
	}
	else if( p_viport->updates & NEED_LINK_CONFIG )
	{
		VNIC_TRACE( VNIC_DBG_CONF,
			("QUERY NEED_LINK_CONFIG\n"));

		NdisAcquireSpinLock(&p_viport->lock );

		p_viport->linkState = LINK_CONFIGLINKREQ;

		if( (InterlockedAnd(
			&p_viport->updates, ~MCAST_OVERFLOW ) & MCAST_OVERFLOW ) )
		{
			p_viport->newFlags |= INIC_FLAG_ENABLE_MCAST_ALL;
		}
		else
		{
			p_viport->newFlags &= ~INIC_FLAG_ENABLE_MCAST_ALL;
		}

		if ( p_viport->mtu != p_viport->newMtu )
		{
			p_viport->newFlags |= INIC_FLAG_SET_MTU;
			p_viport->mtu = p_viport->newMtu;
		}
		if( ( p_viport->newFlags & INIC_FLAG_ENABLE_NIC ) &&
			( p_viport->p_netpath != p_viport->p_adapter->p_currentPath ) )
		{
			ASSERT(0);
		}

		ib_status = control_configLinkReq( &p_viport->control,
			p_viport->newFlags, p_viport->newMtu );

		p_viport->newFlags &= ~INIC_FLAG_SET_MTU;

		NdisReleaseSpinLock( &p_viport->lock );

		if( ib_status != IB_SUCCESS )
		{
			InterlockedAnd( &p_viport->updates, ~NEED_LINK_CONFIG );
			VNIC_EXIT( VNIC_DBG_VIPORT );
			return NDIS_STATUS_FAILURE;
		}
		expected_state = LINK_CONFIGLINKRSP;
	}
	else if( p_viport->updates & NEED_STATS )
	{
		// TODO: 
		VNIC_TRACE( VNIC_DBG_CONF,
			("QUERY NEED_STATS\n"));

		NdisAcquireSpinLock( &p_viport->lock );

		p_viport->linkState = LINK_REPORTSTATREQ;

		ib_status = control_reportStatisticsReq( &p_viport->control );
		
		NdisReleaseSpinLock( &p_viport->lock );

		if( ib_status != IB_SUCCESS )
		{
			InterlockedAnd( &p_viport->updates, ~NEED_STATS );
			VNIC_EXIT( VNIC_DBG_VIPORT );
			return NDIS_STATUS_FAILURE;
		}
		expected_state = LINK_REPORTSTATRSP;
	}

	if( sync )
	{
		cl_event_wait_on( &p_viport->sync_event, EVENT_NO_TIMEOUT, TRUE );

		if( p_viport->linkState != expected_state )
		{
			status = NDIS_STATUS_FAILURE;
			VNIC_TRACE( VNIC_DBG_ERROR,
				("Link state error: expected %d but got %d\n",
				expected_state, p_viport->linkState));
		}
	}
	VNIC_EXIT( VNIC_DBG_VIPORT );
	return status;
}

BOOLEAN
viport_canTxCsum(
	IN		viport_t*	p_viport ) 
{
	if( !p_viport )
		return FALSE;

	return( BOOLEAN )( ( p_viport->featuresSupported &  
			( INIC_FEAT_IPV4_HEADERS | 
			  INIC_FEAT_IPV6_HEADERS |
			  INIC_FEAT_IPV4_CSUM_TX |
			  INIC_FEAT_TCP_CSUM_TX  | 
			  INIC_FEAT_UDP_CSUM_TX ) ) == 
			( INIC_FEAT_IPV4_HEADERS | 
			  INIC_FEAT_IPV6_HEADERS | 
			  INIC_FEAT_IPV4_CSUM_TX | 
			  INIC_FEAT_TCP_CSUM_TX  |
			  INIC_FEAT_UDP_CSUM_TX ) );
}

BOOLEAN
viport_canRxCsum(
	IN	viport_t*		p_viport )
{
	if( !p_viport )
		return FALSE;

	return( BOOLEAN )( ( p_viport->featuresSupported &  
			( INIC_FEAT_IPV4_HEADERS | 
			  INIC_FEAT_IPV6_HEADERS |
			  INIC_FEAT_IPV4_CSUM_RX |
			  INIC_FEAT_TCP_CSUM_RX  | 
			  INIC_FEAT_UDP_CSUM_RX ) ) == 
			( INIC_FEAT_IPV4_HEADERS | 
			  INIC_FEAT_IPV6_HEADERS | 
			  INIC_FEAT_IPV4_CSUM_RX | 
			  INIC_FEAT_TCP_CSUM_RX  |
			  INIC_FEAT_UDP_CSUM_RX ) );
}
