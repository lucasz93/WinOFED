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
#include "vnic_adapter.h"

extern vnic_globals_t	g_vnic;


void
netpath_init(
		IN			Netpath_t			*pNetpath,
		IN			vnic_adapter_t		*p_adapter )
{
	VNIC_ENTER( VNIC_DBG_FUNC );

	pNetpath->p_adapter    = p_adapter;
	pNetpath->carrier    = 0;
	pNetpath->pViport    = NULL;
	pNetpath->p_path_rec = NULL;
	pNetpath->timerState = NETPATH_TS_IDLE;

	VNIC_EXIT( VNIC_DBG_FUNC );
	return;
}

BOOLEAN
netpath_addPath (
				 Netpath_t		*pNetpath,
				 viport_t		*pViport )
{

	if( pNetpath->pViport )
	{
		return FALSE;
	}
	else
	{
		pNetpath->pViport = pViport;
		viport_setParent( pViport, pNetpath );
		return TRUE;
	}
}

BOOLEAN
netpath_setUnicast(
		IN		Netpath_t*		p_netpath,
		IN		uint8_t*		p_address )
{
	if( p_netpath->pViport )
	{
		return( viport_setUnicast( p_netpath->pViport, p_address ) );
	}
	return FALSE;
}

NDIS_STATUS
netpath_setMulticast(
		IN		Netpath_t*		p_netpath )
{
	if( netpath_is_connected( p_netpath ) )
	{
		return viport_setMulticast( p_netpath->pViport );
	}
	return NDIS_STATUS_NOT_ACCEPTED;
}

BOOLEAN
netpath_removePath(
		IN		Netpath_t		*p_netpath,
		IN		viport_t		*p_viport )

{
	if( p_netpath->pViport != p_viport )
	{
		return FALSE;
	}
	else
	{
		viport_timerStop( p_netpath->pViport );
		viport_unsetParent( p_netpath->pViport );
		viport_cleanup( p_viport );
	}

	return TRUE;
}

void
netpath_free(
	IN	Netpath_t	*pNetpath )
{
	if( netpath_is_valid( pNetpath ) )
	{
		netpath_removePath(	pNetpath, pNetpath->pViport	);
	}
	return;
}

BOOLEAN
netpath_is_valid(
		 IN		Netpath_t*	const	p_netpath )
{
	if( p_netpath == NULL )
		return FALSE;
	
	if( p_netpath->pViport == NULL )
		return FALSE;

	return TRUE;
}

BOOLEAN
netpath_is_connected(
 		 IN		Netpath_t*	const	p_netpath )
{
	if( !netpath_is_valid( p_netpath ) )
		return FALSE;

	if( p_netpath->pViport->errored )
		return FALSE;

	if( p_netpath->p_path_rec == NULL )
		return FALSE;

	if( p_netpath->pViport->state != VIPORT_CONNECTED )
		return FALSE;

	
	return TRUE;
}

BOOLEAN
netpath_is_primary(
	 IN		Netpath_t*	const	p_netpath )
{
	return (BOOLEAN)( p_netpath->instance == NETPATH_PRIMARY );
}
BOOLEAN 
netpath_linkUp(
	IN		Netpath_t*		p_netpath )
{
	if( p_netpath != p_netpath->p_adapter->p_currentPath )
		return FALSE;

	if( p_netpath->carrier == TRUE && 
		InterlockedExchange( &p_netpath->p_adapter->carrier, TRUE ) == FALSE )
	{
		NdisMIndicateStatus( p_netpath->p_adapter->h_handle,
							NDIS_STATUS_MEDIA_CONNECT, NULL, 0 );
		NdisMIndicateStatusComplete( p_netpath->p_adapter->h_handle );
		VNIC_TRACE( VNIC_DBG_INIT,
			("IOC[%d] %s Link UP\n", 
				p_netpath->p_adapter->ioc_num,
				netpath_to_string( p_netpath ) ) );
	}
	return TRUE;
}

BOOLEAN
netpath_linkDown(
	IN		Netpath_t*		p_netpath )
{
	if( p_netpath != p_netpath->p_adapter->p_currentPath )
		return FALSE;

	if ( InterlockedExchange( &p_netpath->p_adapter->carrier, FALSE ) == TRUE )
	{
		NdisMIndicateStatus( p_netpath->p_adapter->h_handle,
						NDIS_STATUS_MEDIA_DISCONNECT, NULL, 0 );
		NdisMIndicateStatusComplete( p_netpath->p_adapter->h_handle );
		VNIC_TRACE( VNIC_DBG_INIT,
			("IOC[%d] %s Link DOWN\n", 
				p_netpath->p_adapter->ioc_num,
				netpath_to_string( p_netpath ) ) );
	}

	return TRUE;
}

int netpath_maxMtu(Netpath_t *pNetpath)
{
	int ret = MAX_PARAM_VALUE;

	if (pNetpath->pViport) {
		ret = viport_maxMtu(pNetpath->pViport);
	}
	return ret;
}

BOOLEAN
netpath_xmitPacket(
		IN		Netpath_t			*pNetpath,
		IN		NDIS_PACKET	* const p_packet )
{

	if ( pNetpath->pViport &&
		pNetpath->p_adapter->xmitStarted )
	{
		return ( viport_xmitPacket(pNetpath->pViport, p_packet ) );
	}
	else
	{
		NdisInterlockedInsertTailList( 
			&pNetpath->p_adapter->send_pending_list,
			VNIC_LIST_ITEM_FROM_PACKET( p_packet ),
			&pNetpath->p_adapter->pending_list_lock );
		return TRUE;
	}
}

void
netpath_cancel_xmit( 
	IN		Netpath_t		*p_netpath, 
	IN		PVOID			cancel_id )
{
	if( p_netpath->pViport )
	{
		viport_cancel_xmit( p_netpath->pViport, cancel_id );
	}
}

void
netpath_stopXmit(
	IN		Netpath_t		*pNetpath )

{

	VNIC_ENTER( VNIC_DBG_NETPATH );

	if( pNetpath == pNetpath->p_adapter->p_currentPath )
	{
		InterlockedCompareExchange( &pNetpath->p_adapter->xmitStarted, 0, 1 );
	}
#ifdef INIC_STATISTICS
		if ( pNetpath->p_adapter->statistics.xmitRef == 0)
		{
			pNetpath->p_adapter->statistics.xmitRef = get_time_stamp_ms();
		}
#endif /* INIC_STATISTICS */
	return;
}

void netpath_restartXmit(
		IN		Netpath_t		*pNetpath )
{
	VNIC_ENTER( VNIC_DBG_NETPATH );

	if( ( netpath_is_connected( pNetpath ) ) &&
		pNetpath == pNetpath->p_adapter->p_currentPath )
	{
		InterlockedCompareExchange( &pNetpath->p_adapter->xmitStarted, 1, 0 );
		VNIC_TRACE( VNIC_DBG_NETPATH,
			("IOC[%d] instance %d Restart TRANSMIT\n", 
			pNetpath->pViport->ioc_num, 
			pNetpath->instance ));

	}
#ifdef INIC_STATISTICS
	if (pNetpath->p_adapter->statistics.xmitRef != 0)
	{
		pNetpath->p_adapter->statistics.xmitOffTime +=
			get_time_stamp_ms() - pNetpath->p_adapter->statistics.xmitRef;
		pNetpath->p_adapter->statistics.xmitOffNum++;
		pNetpath->p_adapter->statistics.xmitRef = 0;
	}
#endif /* INIC_STATISTICS */
	return;
}

// Viport on input calls this
void netpath_recvPacket(
		IN		Netpath_t		*pNetpath,
		IN		NDIS_PACKET**	pp_packet_array,
		IN		uint32_t		num_packets )
{

#ifdef INIC_STATISTICS
	extern uin64_t recvRef;
#endif /* INIC_STATISTICS */

	VNIC_ENTER( VNIC_DBG_NETPATH );

#ifdef INIC_STATISTICS
	pNetpath->p_adapter->statistics.recvTime += cl_get_tick_count() - recvRef;
	pNetpath->p_adapter->statistics.recvNum++;
#endif /* INIC_STATISTICS */

	NdisMIndicateReceivePacket( pNetpath->p_adapter->h_handle,
								pp_packet_array, 
								num_packets );
	return;
}

void netpath_tx_timeout(
		IN		Netpath_t		*pNetpath )
{
	if ( pNetpath->pViport )
	{
		viport_failure( pNetpath->pViport );
	}
}

const char *
netpath_to_string(
		IN		Netpath_t			*p_netpath )
{
	if ( !netpath_is_valid( p_netpath ) )
	{
		return "NULL";
	}
	else if ( p_netpath == p_netpath->p_adapter->p_currentPath )
	{
		return "CURRENT";
	}
	else
	{
		return "STANDBY";
	}
}
