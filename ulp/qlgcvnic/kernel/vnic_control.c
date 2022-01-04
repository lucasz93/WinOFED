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

#include "vnic_adapter.h"

static void
control_recv( Control_t *pControl, RecvIo_t *pRecvIo );

static void
control_recvComplete(
		IN		Io_t		*pIo );

static ib_api_status_t
control_send( Control_t *pControl );

static void
control_sendComplete( Io_t *pIo );
static void
control_timeout( void * context );

static void
control_timer( Control_t *pControl, int timeout );

static void
control_timerStop( Control_t *pControl );

static void
control_initHdr( Control_t *pControl, uint8_t cmd );

static RecvIo_t *
control_getRsp( Control_t *pControl );

static void
copyRecvPoolConfig( Inic_RecvPoolConfig_t *pSrc,
					Inic_RecvPoolConfig_t *pDst );

static BOOLEAN
checkRecvPoolConfigValue(
		IN		void		*pSrc,
		IN		void		*pDst,
		IN		void		*pMax,
		IN		void		*pMin,
		IN		char		*name );

static BOOLEAN  checkRecvPoolConfig(
					Inic_RecvPoolConfig_t *pSrc,
					Inic_RecvPoolConfig_t *pDst,
					Inic_RecvPoolConfig_t *pMax,
					Inic_RecvPoolConfig_t *pMin);

static void
__control_logControlPacket(
					Inic_ControlPacket_t *pPkt );

void
control_construct(
	IN		Control_t			*pControl,
	IN		viport_t			*pViport )
{
	VNIC_ENTER( VNIC_DBG_CTRL );

	cl_memclr(pControl, sizeof(Control_t));

	pControl->p_viport = pViport;

	pControl->reqOutstanding = FALSE;
	pControl->seqNum         = 0;

	pControl->pResponse      = NULL;
	pControl->pInfo          = NULL;

	pControl->p_viport->addrs_query_done = TRUE;

	InitializeListHead(&pControl->failureList);
	KeInitializeSpinLock(&pControl->ioLock);

	cl_timer_construct( &pControl->timer );

	ibqp_construct( &pControl->qp, pViport );

	VNIC_EXIT( VNIC_DBG_CTRL );
}


ib_api_status_t
control_init(
	IN		Control_t			*pControl,
	IN		viport_t			*pViport,
	IN		ControlConfig_t		*pConfig,
	IN		uint64_t			guid )
{
	ib_api_status_t			ib_status;
	ib_pd_handle_t			hPd;
	Inic_ControlPacket_t	*pkt;
	Io_t					*pIo;
	int						sz;
	unsigned int			i;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pControl->p_conf  = pConfig;

	hPd = pViport->p_adapter->ca.hPd;

	cl_timer_init( &pControl->timer, control_timeout, pControl );

	ib_status = ibqp_init(
		&pControl->qp, guid, &pConfig->ibConfig );
	if( ib_status != IB_SUCCESS )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR, ("ibqp_init returned %s\n",
			pViport->p_adapter->ifc.get_err_str( ib_status )) );
		goto failure;
	}

	sz = (sizeof(RecvIo_t) * pConfig->numRecvs ) +
		(sizeof(Inic_ControlPacket_t) * (pConfig->numRecvs + 1));

	pControl->pLocalStorage = cl_zalloc( sz );

	if ( pControl->pLocalStorage == NULL )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("Failed allocating space for local storage\n" ));

		ibqp_cleanup(&pControl->qp);
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto failure;
	}

	pControl->pRecvIos = (RecvIo_t *)pControl->pLocalStorage;

	pkt = (Inic_ControlPacket_t *)(pControl->pLocalStorage +
		             sizeof(SendIo_t) * pConfig->numRecvs);

	sz = sizeof(Inic_ControlPacket_t) * (pConfig->numRecvs + 1);

	ib_status = ibregion_init( pViport, &pControl->region, hPd,
		pkt, sz, IB_AC_LOCAL_WRITE );
	if ( ib_status != IB_SUCCESS )
	{
		/* NOTE: I'm allowing recvs into the send buffer as well
		 * as the receive buffers. I'm doing this to combine them
		 * into a single region, and conserve a region.
		 */
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR ,
					(" Failed setting up control space region\n" ));

		ibqp_cleanup( &pControl->qp );
		cl_free( pControl->pLocalStorage );
		pControl->pLocalStorage = NULL;
		goto failure;
	}
	pIo						= &pControl->sendIo.io;
	pIo->pViport			= pViport;
	pIo->pRoutine			= control_sendComplete;

	pIo->wrq.p_next			= NULL;
	pIo->wrq.wr_type		= WR_SEND;
	pIo->wrq.send_opt		= IB_SEND_OPT_SIGNALED;
	pIo->wrq.wr_id			= (ULONG_PTR)pIo;
	pIo->wrq.num_ds			= 1;
	pIo->wrq.ds_array		= &pControl->sendIo.dsList;
	pIo->wrq.ds_array[0].length	= sizeof(Inic_ControlPacket_t);
	pIo->wrq.ds_array[0].lkey  = pControl->region.lkey;
	pIo->wrq.ds_array[0].vaddr = (ULONG_PTR)pkt++;

	for (i = 0; i < pConfig->numRecvs; i++ )
	{
		pIo						= &pControl->pRecvIos[i].io;
		pIo->pViport			= pViport;
		pIo->pRoutine			= control_recvComplete;

		pIo->r_wrq.wr_id				= (ULONG_PTR)pIo;
		pIo->r_wrq.p_next				= NULL;
		pIo->r_wrq.num_ds				= 1;
		pIo->r_wrq.ds_array				= &pControl->pRecvIos[i].dsList;
		pIo->r_wrq.ds_array[0].length	= sizeof(Inic_ControlPacket_t);
		pIo->r_wrq.ds_array[0].vaddr	= (ULONG_PTR)pkt++;
		pIo->r_wrq.ds_array[0].lkey		= pControl->region.lkey;
	
		if ( ibqp_postRecv( &pControl->qp, pIo ) != IB_SUCCESS )
		{
			control_cleanup( pControl );
			ib_status = IB_ERROR;
			break;
		}
	}

failure:
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


void
control_cleanup(
	IN			Control_t	*pControl )
{
	VNIC_ENTER( VNIC_DBG_CTRL );

	control_timerStop( pControl );
	ibqp_detach( &pControl->qp );
	ibregion_cleanup( pControl->p_viport, &pControl->region );

	if ( pControl->pLocalStorage )
	{
		cl_free( pControl->pLocalStorage );
		pControl->pLocalStorage = NULL;
	}

	cl_timer_destroy( &pControl->timer );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return;
}

void
control_processAsync(
		IN			Control_t		*pControl )
{
	RecvIo_t				*pRecvIo;
	Inic_ControlPacket_t	*pPkt;
	LIST_ENTRY				*p_list_entry;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = InterlockedExchangePointer( &pControl->pInfo, NULL );

	if ( pRecvIo != NULL )
	{
		VNIC_TRACE( VNIC_DBG_CTRL,
				("IOC %d:  processing info packet\n",
						pControl->p_viport->ioc_num ) );
	
		pPkt = control_packet( pRecvIo );

		if ( pPkt->hdr.pktCmd == CMD_REPORT_STATUS )
		{
			switch( ntoh32(pPkt->cmd.reportStatus.statusNumber) )
			{
			case INIC_STATUS_LINK_UP:
				VNIC_TRACE( VNIC_DBG_CTRL,
					("IOC %d: STATUS LINK UP\n", pControl->p_viport->ioc_num ) );
				/* renew link speed info since it can change */
				pControl->p_viport->p_adapter->link_speed =
							ntoh32( pPkt->cmd.reportStatus.statusInfo );
				viport_linkUp( pControl->p_viport );
				break;

			case INIC_STATUS_LINK_DOWN:
				VNIC_TRACE( VNIC_DBG_CTRL,
					("IOC %d: STATUS LINK DOWN\n", pControl->p_viport->ioc_num ) );

				viport_linkDown( pControl->p_viport );
				break;
			case INIC_STATUS_EIOC_ERROR:
			case INIC_STATUS_EIOC_SHUTDOWN:
				viport_failure( pControl->p_viport );
				VNIC_TRACE( VNIC_DBG_ERROR,
						("IOC %d: STATUS EIOC ERROR\n",
									pControl->p_viport->ioc_num ) );
				break;
			default:
				VNIC_TRACE( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: Asynchronous status %#x received\n",
									 pControl->p_viport->ioc_num,
									 ntoh32(pPkt->cmd.reportStatus.statusNumber)) );
				__control_logControlPacket( pPkt );
				break;
			}
		}

		if (  pPkt->hdr.pktCmd != CMD_REPORT_STATUS ||
			  pPkt->cmd.reportStatus.isFatal )
		{
			viport_failure( pControl->p_viport );
		}

		control_recv( pControl, pRecvIo );
	}

	while ( !IsListEmpty( &pControl->failureList ) )
	{

		VNIC_TRACE( VNIC_DBG_CTRL,
					("IOC %d: processing error packet\n",
								 pControl->p_viport->ioc_num ) );

		p_list_entry = ExInterlockedRemoveHeadList( &pControl->failureList, &pControl->ioLock );
		pRecvIo = (RecvIo_t *)p_list_entry;
		pPkt = control_packet( pRecvIo );

		VNIC_TRACE( VNIC_DBG_CTRL,
					("IOC %d: Asynchronous error received from EIOC\n",
								pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( pPkt );

		if ( ( pPkt->hdr.pktType != TYPE_ERR ) ||
			 ( pPkt->hdr.pktCmd != CMD_REPORT_STATUS ) ||
		     ( pPkt->cmd.reportStatus.isFatal ) )
		{
			viport_failure( pControl->p_viport );
			break;
		}

		control_recv( pControl, pRecvIo );
	}

	VNIC_EXIT( VNIC_DBG_CTRL );
}


ib_api_status_t
control_initInicReq(
	IN			Control_t		 *pControl )
{
	ControlConfig_t			*p_conf = pControl->p_conf;
	Inic_ControlPacket_t	*pPkt;
	Inic_CmdInitInicReq_t	*pInitInicReq;
	ib_api_status_t			ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );
	
	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr( pControl, CMD_INIT_INIC );

	pPkt                           = control_packet( &pControl->sendIo );
	pInitInicReq                   = &pPkt->cmd.initInicReq;
	pInitInicReq->inicMajorVersion = hton16( INIC_MAJORVERSION );
	pInitInicReq->inicMinorVersion = hton16( INIC_MINORVERSION );
	pInitInicReq->inicInstance = p_conf->inicInstance;
	pInitInicReq->numDataPaths = 1;
	pInitInicReq->numAddressEntries = hton16( p_conf->maxAddressEntries );

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_initInicRsp(
		IN		Control_t		*pControl,
		IN		uint32_t		*pFeatures,
		IN		uint8_t			*pMacAddress,
		IN		uint16_t		*pNumAddrs,
		IN		uint16_t		*pVlan )
{
	RecvIo_t              *pRecvIo;
	ControlConfig_t       *p_conf = pControl->p_conf;
	Inic_ControlPacket_t  *pPkt;
	Inic_CmdInitInicRsp_t *pInitInicRsp;
	uint8_t               numDataPaths,
	                      numLanSwitches;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if (!pRecvIo)
		return FALSE;

	pPkt = control_packet( pRecvIo );

	if ( pPkt->hdr.pktCmd != CMD_INIT_INIC )
	{
	
		VNIC_TRACE( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
						pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );

		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: Received control response:\n",
							pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( pPkt );
		goto failure;
	}
#ifdef _DEBUG_
		VNIC_TRACE( VNIC_DBG_CTRL_PKT,
				("IOC %d: instance %d Sent initINIC request:\n",
					pControl->p_viport->ioc_num,
					pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE(VNIC_DBG_CTRL_PKT,
			("IOC %d: instance %d Received initInic response:\n",
				pControl->p_viport->ioc_num,
				pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( pPkt );
#endif

	pInitInicRsp     = &pPkt->cmd.initInicRsp;
	pControl->majVer = ntoh16( pInitInicRsp->inicMajorVersion );
	pControl->minVer = ntoh16( pInitInicRsp->inicMinorVersion );
	numDataPaths     = pInitInicRsp->numDataPaths;
	numLanSwitches   = pInitInicRsp->numLanSwitches;
	*pFeatures       = ntoh32( pInitInicRsp->featuresSupported );
	*pNumAddrs       = ntoh16( pInitInicRsp->numAddressEntries );

	if ( ( pControl->majVer > INIC_MAJORVERSION ) ||
		 (( pControl->majVer == INIC_MAJORVERSION ) &&
	      ( pControl->minVer > INIC_MINORVERSION )) )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
				("IOC %d: Unsupported version\n",
						pControl->p_viport->ioc_num ) );
		goto failure;
	}

	if ( numDataPaths != 1 )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: EIOC returned too many datapaths\n",
							pControl->p_viport->ioc_num ) );
		goto failure;
	}

	if ( *pNumAddrs > p_conf->maxAddressEntries )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
				("IOC %d: EIOC returned more Address entries than requested\n",
						pControl->p_viport->ioc_num) );
		goto failure;
	}
	if ( *pNumAddrs < p_conf->minAddressEntries )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: Not enough address entries\n",
							pControl->p_viport->ioc_num ) );
		goto failure;
	}
	if ( numLanSwitches < 1 )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: EIOC returned no lan switches\n",
							pControl->p_viport->ioc_num ) );
		goto failure;
	}
	if ( numLanSwitches > 1 )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_CTRL | VNIC_DBG_ERROR,
					("IOC %d: EIOC returned multiple lan switches\n",
							pControl->p_viport->ioc_num ) );
		goto failure;
	}

	pControl->lanSwitch.lanSwitchNum =
		pInitInicRsp->lanSwitch[0].lanSwitchNum ;
	pControl->lanSwitch.numEnetPorts =
		pInitInicRsp->lanSwitch[0].numEnetPorts ;
	pControl->lanSwitch.defaultVlan =
		ntoh16( pInitInicRsp->lanSwitch[0].defaultVlan );
	*pVlan = pControl->lanSwitch.defaultVlan;
	cl_memcpy( pControl->lanSwitch.hwMacAddress,
				pInitInicRsp->lanSwitch[0].hwMacAddress, MAC_ADDR_LEN );
	cl_memcpy( pMacAddress,
				pInitInicRsp->lanSwitch[0].hwMacAddress, MAC_ADDR_LEN);

	control_recv( pControl, pRecvIo );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;
failure:
	viport_failure( pControl->p_viport );
	return FALSE;
}


ib_api_status_t
control_configDataPathReq(
		IN		Control_t				*pControl,
		IN		uint64_t				pathId,
		IN		Inic_RecvPoolConfig_t	*pHost,
		IN		Inic_RecvPoolConfig_t	*pEioc )
{
	Inic_ControlPacket_t		*pPkt;
	Inic_CmdConfigDataPath_t	*pConfigDataPath;
	ib_api_status_t				ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );
	
	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr( pControl, CMD_CONFIG_DATA_PATH );

	pPkt                            = control_packet( &pControl->sendIo );
	pConfigDataPath                 = &pPkt->cmd.configDataPathReq;
	NdisZeroMemory( pConfigDataPath, sizeof( Inic_CmdConfigDataPath_t ) );
	pConfigDataPath->dataPath       = 0;
	pConfigDataPath->pathIdentifier = pathId;
	copyRecvPoolConfig( pHost, &pConfigDataPath->hostRecvPoolConfig );
	copyRecvPoolConfig( pEioc, &pConfigDataPath->eiocRecvPoolConfig );

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_configDataPathRsp(
		IN		Control_t				*pControl,
		IN		Inic_RecvPoolConfig_t	*pHost,
		IN		Inic_RecvPoolConfig_t	*pEioc,
		IN		Inic_RecvPoolConfig_t	*pMaxHost,
		IN		Inic_RecvPoolConfig_t	*pMaxEioc,
		IN		Inic_RecvPoolConfig_t	*pMinHost,
		IN		Inic_RecvPoolConfig_t	*pMinEioc )
{
	RecvIo_t                 *pRecvIo;
	Inic_ControlPacket_t     *pPkt;
	Inic_CmdConfigDataPath_t *pConfigDataPath;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if ( !pRecvIo )
		return FALSE;

	pPkt = control_packet( pRecvIo );

	if ( pPkt->hdr.pktCmd != CMD_CONFIG_DATA_PATH )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
					pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );

		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
						pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( pPkt );
		goto failure;
	}
#ifdef _DEBUG_
		VNIC_TRACE( VNIC_DBG_CTRL_PKT,
				("IOC %d: instance %d Sent configDATAPATH request:\n",
					pControl->p_viport->ioc_num,
					pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE(VNIC_DBG_CTRL_PKT,
			("IOC %d: instance %d Received configDATAPATH response:\n",
				pControl->p_viport->ioc_num,
				pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( pPkt );
#endif
	pConfigDataPath = &pPkt->cmd.configDataPathRsp;

	if ( pConfigDataPath->dataPath  != 0 )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received CMD_CONFIG_DATA_PATH response for wrong data path: %u\n",
					pControl->p_viport->ioc_num , pConfigDataPath->dataPath) );
		goto failure;
	}

	if ( !checkRecvPoolConfig(&pConfigDataPath->hostRecvPoolConfig,
				 pHost, pMaxHost, pMinHost )
		|| !checkRecvPoolConfig(&pConfigDataPath->eiocRecvPoolConfig,
 				pEioc, pMaxEioc, pMinEioc))
	{
		goto failure;
	}

	control_recv( pControl, pRecvIo );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;
failure:
	viport_failure( pControl->p_viport );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return FALSE;
}


ib_api_status_t
control_exchangePoolsReq(
		IN			Control_t		*pControl,
		IN			uint64_t		 addr,
		IN			uint32_t		rkey )
{
	Inic_CmdExchangePools_t	*pExchangePools;
	Inic_ControlPacket_t	*pPkt;
	ib_api_status_t			ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );

	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr(pControl, CMD_EXCHANGE_POOLS );

	pPkt                     = control_packet( &pControl->sendIo );
	pExchangePools           = &pPkt->cmd.exchangePoolsReq;
	NdisZeroMemory( pExchangePools, sizeof( Inic_CmdExchangePools_t ) );
	pExchangePools->dataPath =	0;
	pExchangePools->poolRKey = rkey;
	pExchangePools->poolAddr = hton64( addr );

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_exchangePoolsRsp(
		IN			Control_t			*pControl,
		IN	OUT		uint64_t			*pAddr,
		IN	OUT		uint32_t			*pRkey )
{
	RecvIo_t                *pRecvIo;
	Inic_ControlPacket_t    *pPkt;
	Inic_CmdExchangePools_t *pExchangePools;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if ( !pRecvIo )
		return FALSE;

	pPkt = control_packet( pRecvIo );

	if ( pPkt->hdr.pktCmd != CMD_EXCHANGE_POOLS )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
					pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq(pControl ) );

		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
					pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( pPkt );
		goto failure;
	}

#ifdef _DEBUG_
		VNIC_TRACE( VNIC_DBG_CTRL_PKT,
				("IOC %d: instance %d Sent exchangePOOLS request:\n",
					pControl->p_viport->ioc_num,
					pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE(VNIC_DBG_CTRL_PKT,
			("IOC %d: instance %d Received exchangePOOLS response:\n",
				pControl->p_viport->ioc_num,
				pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( pPkt );
#endif
	pExchangePools = &pPkt->cmd.exchangePoolsRsp;
	*pRkey         = pExchangePools->poolRKey;
	*pAddr         = ntoh64( pExchangePools->poolAddr );

	if ( hton32( pExchangePools->dataPath ) != 0 )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("IOC %d: Received CMD_EXCHANGE_POOLS response for wrong data path: %u\n",
					pControl->p_viport->ioc_num , pExchangePools->dataPath ) );
		goto failure;
	}

	control_recv( pControl, pRecvIo );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;

failure:
	viport_failure( pControl->p_viport );
	return FALSE;
}


ib_api_status_t
control_configLinkReq(
		IN		Control_t		*pControl,
		IN		uint8_t			flags,
		IN		uint16_t		 mtu )
{
	Inic_CmdConfigLink_t	*pConfigLinkReq;
	Inic_ControlPacket_t	*pPkt;
	ib_api_status_t			ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );
	
	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr( pControl, CMD_CONFIG_LINK );

	pPkt                         = control_packet( &pControl->sendIo );
	pConfigLinkReq               = &pPkt->cmd.configLinkReq;
	NdisZeroMemory( pConfigLinkReq, sizeof( Inic_CmdConfigLink_t ) );
	pConfigLinkReq->lanSwitchNum = pControl->lanSwitch.lanSwitchNum;

	if ( flags & INIC_FLAG_ENABLE_NIC )
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_ENABLE_NIC;
	}
	else
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_DISABLE_NIC;
	}

	if (flags & INIC_FLAG_ENABLE_MCAST_ALL )
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_ENABLE_MCAST_ALL;
	}
	else
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_DISABLE_MCAST_ALL;
	}
	if (flags & INIC_FLAG_ENABLE_PROMISC )
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_ENABLE_PROMISC;
		/* The EIOU doesn't really do PROMISC mode.
		 * If PROMISC is set, it only receives unicast packets
		 * I also have to set MCAST_ALL if I want real
		 * PROMISC mode.
		 */
		pConfigLinkReq->cmdFlags &= ~INIC_FLAG_DISABLE_MCAST_ALL;
		pConfigLinkReq->cmdFlags |= INIC_FLAG_ENABLE_MCAST_ALL;
	}
	else
	{
		pConfigLinkReq->cmdFlags |= INIC_FLAG_DISABLE_PROMISC;
	}

	if( flags & INIC_FLAG_SET_MTU )
	{
		pConfigLinkReq->cmdFlags     |= INIC_FLAG_SET_MTU;
		pConfigLinkReq->mtuSize      = hton16( mtu );
	}

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_configLinkRsp(
		IN			Control_t		*pControl,
		IN	OUT		uint8_t		*pFlags,
		IN	OUT		uint16_t		*pMtu )
{
	RecvIo_t             *pRecvIo;
	Inic_ControlPacket_t *pPkt;
	Inic_CmdConfigLink_t *pConfigLinkRsp;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if ( !pRecvIo )
		return FALSE;

	pPkt = control_packet( pRecvIo );
	if ( pPkt->hdr.pktCmd != CMD_CONFIG_LINK )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
				pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );

		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
					pControl->p_viport->ioc_num )  );
	
		__control_logControlPacket( pPkt );
		goto failure;
	}
#ifdef _DEBUG_
		VNIC_TRACE( VNIC_DBG_CTRL_PKT,
				("IOC %d: instance %d Sent configLINK request:\n",
					pControl->p_viport->ioc_num,
					pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE(VNIC_DBG_CTRL_PKT,
			("IOC %d: instance %d Received configLINK response:\n",
				pControl->p_viport->ioc_num,
				pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( pPkt );
#endif
	pConfigLinkRsp = &pPkt->cmd.configLinkRsp;

	*pFlags = pConfigLinkRsp->cmdFlags;
	*pMtu   = ntoh16( pConfigLinkRsp->mtuSize );

	control_recv( pControl, pRecvIo );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;

failure:
	viport_failure( pControl->p_viport );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return FALSE;
}

/* control_configAddrsReq:
 * Return values:
 *          -1: failure
 *           0: incomplete (successful operation, but more address
 *              table entries to be updated)
 *           1: complete
 */
ib_api_status_t
control_configAddrsReq(
	IN				Control_t			*pControl,
	IN				Inic_AddressOp_t	*pAddrs,
	IN				uint16_t			num,
		OUT			int32_t				*pAddrQueryDone )
{
	Inic_CmdConfigAddresses_t	*pConfigAddrsReq;
	Inic_ControlPacket_t		*pPkt;
	uint16_t					i;
	uint8_t						j;
	ib_api_status_t				ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );

	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr( pControl, CMD_CONFIG_ADDRESSES );

	pPkt                          = control_packet( &pControl->sendIo );
	pConfigAddrsReq               = &pPkt->cmd.configAddressesReq;
	NdisZeroMemory( pConfigAddrsReq, sizeof( Inic_CmdConfigAddresses_t ) );
	pConfigAddrsReq->lanSwitchNum = pControl->lanSwitch.lanSwitchNum;

	for ( i=0, j = 0; ( i < num ) && ( j < 16 ); i++ )
	{
		if ( !pAddrs[i].operation )
			continue;

		pConfigAddrsReq->listAddressOps[j].index     = hton16(i);
		pConfigAddrsReq->listAddressOps[j].operation = INIC_OP_SET_ENTRY;
		pConfigAddrsReq->listAddressOps[j].valid = pAddrs[i].valid;
	
		cl_memcpy( pConfigAddrsReq->listAddressOps[j].address,
			pAddrs[i].address, MAC_ADDR_LEN );
		pConfigAddrsReq->listAddressOps[j].vlan = hton16( pAddrs[i].vlan );
		pAddrs[i].operation = 0;
		j++;
	}
	pConfigAddrsReq->numAddressOps = j;

	for ( ; i < num; i++ )
	{
		if ( pAddrs[i].operation )
			break;
	}
	*pAddrQueryDone = (i == num);

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_configAddrsRsp(
		IN		Control_t		*pControl )
{
	RecvIo_t                  *pRecvIo;
	Inic_ControlPacket_t      *pPkt;
	Inic_CmdConfigAddresses_t *pConfigAddrsRsp;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if ( !pRecvIo )
		return FALSE;

	pPkt = control_packet( pRecvIo );
	if ( pPkt->hdr.pktCmd != CMD_CONFIG_ADDRESSES )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
				pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );

		VNIC_TRACE_EXIT(VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
				pControl->p_viport->ioc_num ) );

		__control_logControlPacket( pPkt );
	
		goto failure;
	}

#ifdef _DEBUG_
		VNIC_TRACE( VNIC_DBG_CTRL_PKT,
				("IOC %d: instance %d Sent configADDRS request:\n",
					pControl->p_viport->ioc_num,
					pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE(VNIC_DBG_CTRL_PKT,
			("IOC %d: instance %d Received configADDRS response:\n",
				pControl->p_viport->ioc_num,
				pControl->p_viport->p_netpath->instance ) );
		__control_logControlPacket( pPkt );
#endif

	pConfigAddrsRsp = &pPkt->cmd.configAddressesRsp;

	control_recv( pControl, pRecvIo );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;

failure:
	viport_failure( pControl->p_viport );
	return FALSE;
}


ib_api_status_t
control_reportStatisticsReq(
		IN		Control_t		*pControl )
{
	Inic_ControlPacket_t			*pPkt;
	Inic_CmdReportStatisticsReq_t	*pReportStatisticsReq;
	ib_api_status_t					ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );

	control_initHdr( pControl, CMD_REPORT_STATISTICS );

	pPkt                               = control_packet( &pControl->sendIo );
	pReportStatisticsReq               = &pPkt->cmd.reportStatisticsReq;
	pReportStatisticsReq->lanSwitchNum = pControl->lanSwitch.lanSwitchNum;

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}


BOOLEAN
control_reportStatisticsRsp(
		IN		Control_t						*pControl,
		IN		Inic_CmdReportStatisticsRsp_t	*pStats )
{
	RecvIo_t                      *pRecvIo;
	Inic_ControlPacket_t          *pPkt;
	Inic_CmdReportStatisticsRsp_t *pRepStatRsp;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if (!pRecvIo)
		return FALSE;

	pPkt = control_packet( pRecvIo );
	if ( pPkt->hdr.pktCmd != CMD_REPORT_STATISTICS )
	{
		VNIC_TRACE(VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
					pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );

		VNIC_TRACE_EXIT(VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
				pControl->p_viport->ioc_num  ) );

		__control_logControlPacket( pPkt );
	
		goto failure;
	}

	pRepStatRsp                 = &pPkt->cmd.reportStatisticsRsp;
	pStats->ifInBroadcastPkts   = ntoh64(pRepStatRsp->ifInBroadcastPkts);
	pStats->ifInMulticastPkts   = ntoh64(pRepStatRsp->ifInMulticastPkts);
	pStats->ifInOctets          = ntoh64(pRepStatRsp->ifInOctets);
	pStats->ifInUcastPkts       = ntoh64(pRepStatRsp->ifInUcastPkts);
	pStats->ifInNUcastPkts      = ntoh64(pRepStatRsp->ifInNUcastPkts);
	pStats->ifInUnderrun        = ntoh64(pRepStatRsp->ifInUnderrun);
	pStats->ifInErrors          = ntoh64(pRepStatRsp->ifInErrors);
	pStats->ifOutErrors         = ntoh64(pRepStatRsp->ifOutErrors);
	pStats->ifOutOctets         = ntoh64(pRepStatRsp->ifOutOctets);
	pStats->ifOutUcastPkts      = ntoh64(pRepStatRsp->ifOutUcastPkts);
	pStats->ifOutMulticastPkts  = ntoh64(pRepStatRsp->ifOutMulticastPkts);
	pStats->ifOutBroadcastPkts  = ntoh64(pRepStatRsp->ifOutBroadcastPkts);
	pStats->ifOutNUcastPkts     = ntoh64(pRepStatRsp->ifOutNUcastPkts);
	pStats->ifOutOk             = ntoh64(pRepStatRsp->ifOutOk);
	pStats->ifInOk              = ntoh64(pRepStatRsp->ifInOk);
	pStats->ifOutUcastBytes     = ntoh64(pRepStatRsp->ifOutUcastBytes);
	pStats->ifOutMulticastBytes = ntoh64(pRepStatRsp->ifOutMulticastBytes);
	pStats->ifOutBroadcastBytes = ntoh64(pRepStatRsp->ifOutBroadcastBytes);
	pStats->ifInUcastBytes      = ntoh64(pRepStatRsp->ifInUcastBytes);
	pStats->ifInMulticastBytes  = ntoh64(pRepStatRsp->ifInMulticastBytes);
	pStats->ifInBroadcastBytes  = ntoh64(pRepStatRsp->ifInBroadcastBytes);
	pStats->ethernetStatus      = ntoh64(pRepStatRsp->ethernetStatus);

	control_recv(pControl, pRecvIo);

	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;
failure:
	viport_failure( pControl->p_viport );
	return FALSE;
}


ib_api_status_t
control_resetReq(
	IN		Control_t		*pControl )
{
	ib_api_status_t		ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );

	control_initHdr( pControl, CMD_RESET );
	ib_status = control_send( pControl );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}

BOOLEAN
control_resetRsp(
		IN		Control_t		*pControl )
{
	RecvIo_t                      *pRecvIo;
	Inic_ControlPacket_t          *pPkt;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );
	if ( !pRecvIo ) return FALSE;

	pPkt = control_packet( pRecvIo );

	if ( pPkt->hdr.pktCmd != CMD_RESET )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Sent control request:\n",
				pControl->p_viport->ioc_num ) );

		__control_logControlPacket( control_lastReq( pControl ) );
	
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
				pControl->p_viport->ioc_num ) );

		__control_logControlPacket( pPkt );

		goto failure;
	}

	control_recv( pControl, pRecvIo );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return TRUE;
failure:
	viport_failure( pControl->p_viport );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return FALSE;
}


ib_api_status_t
control_heartbeatReq(
		IN		Control_t	*pControl,
		IN		uint32_t	hbInterval )
{
	Inic_ControlPacket_t	*pPkt;
	Inic_CmdHeartbeat_t		*pHeartbeatReq;
	ib_api_status_t			ib_status;

	VNIC_ENTER( VNIC_DBG_CTRL );
	if( pControl->p_viport->errored )
	{
		return IB_ERROR;
	}
	control_initHdr(pControl, CMD_HEARTBEAT);

	pPkt                      = control_packet(&pControl->sendIo);
	pHeartbeatReq             = &pPkt->cmd.heartbeatReq;

	/* pass timeout for the target in microseconds */
	pHeartbeatReq->hbInterval = hton32( hbInterval*1000 );

	ib_status = control_send( pControl );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return ib_status;
}

BOOLEAN
control_heartbeatRsp(
		IN		Control_t		*pControl )
{
	RecvIo_t             *pRecvIo;
	Inic_ControlPacket_t *pPkt;
	Inic_CmdHeartbeat_t  *pHeartbeatRsp;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pRecvIo = control_getRsp( pControl );

	if (!pRecvIo)
		return FALSE;

	pPkt = control_packet(pRecvIo);

	if ( pPkt->hdr.pktCmd != CMD_HEARTBEAT )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC %d: Sent control request:\n",
					pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( control_lastReq(pControl) );
	
		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Received control response:\n",
					pControl->p_viport->ioc_num ) );
	
		__control_logControlPacket( pPkt );
		goto failure;
	}

	pHeartbeatRsp = &pPkt->cmd.heartbeatRsp;

	control_recv( pControl, pRecvIo );
	VNIC_EXIT ( VNIC_DBG_CTRL );
	return TRUE;

failure:
	viport_failure( pControl->p_viport );
	VNIC_EXIT ( VNIC_DBG_CTRL );
	return FALSE;
}

static void
control_recv(
		IN		Control_t		*pControl,
		IN		RecvIo_t		*pRecvIo )
{
	VNIC_ENTER( VNIC_DBG_CTRL );

	if ( ibqp_postRecv( &pControl->qp, &pRecvIo->io ) != IB_SUCCESS )
		viport_failure( pControl->p_viport );

	VNIC_EXIT ( VNIC_DBG_CTRL );
}


static void
control_recvComplete(
		IN		Io_t		*pIo )
{
	RecvIo_t             *pRecvIo = (RecvIo_t *)pIo;
	RecvIo_t             *pLastRecvIo;
	BOOLEAN				status = FALSE;
	Control_t            *pControl = &pIo->pViport->control;
	viport_t			*p_viport = pIo->pViport;
	Inic_ControlPacket_t *pPkt = control_packet(pRecvIo);
	Inic_ControlHeader_t *pCHdr = &pPkt->hdr;

	if( p_viport->errored )
	{
		return;
	}
	switch ( pCHdr->pktType )
	{
		case TYPE_INFO:
			pLastRecvIo = InterlockedExchangePointer( &pControl->pInfo, pRecvIo );

			control_processAsync( pControl );

			if ( pLastRecvIo )
			{
				control_recv( pControl, pLastRecvIo );
			}
			return;

		case TYPE_RSP:
			break;

		default:
			//TODO: Should we ever reach this?  Who processes the list entries?
			ASSERT( pCHdr->pktType == TYPE_INFO || pCHdr->pktType == TYPE_RSP );
			ExInterlockedInsertTailList( &pRecvIo->io.listPtrs,
										 &pControl->failureList,
										 &pControl->ioLock );
			return;
	}

	if( !InterlockedExchange( (volatile LONG*)&pControl->rspExpected, FALSE ) )
	{
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("UNEXPECTED RSP Packet CMD: %d\n",	pCHdr->pktCmd ) );
		return;
	}

	InterlockedExchangePointer( &pControl->pResponse, pRecvIo );

	switch ( pCHdr->pktCmd )
	{
	case CMD_INIT_INIC:
		status = control_initInicRsp( pControl,
			&p_viport->featuresSupported,
			p_viport->hwMacAddress,
			&p_viport->numMacAddresses,
			&p_viport->defaultVlan );
		if( status )
		{
			InterlockedExchange(
				(volatile LONG*)&p_viport->linkState,
				(LONG)LINK_INITINICRSP );
		}
		InterlockedOr( &p_viport->updates, SYNC_QUERY );
		break;

	case CMD_CONFIG_DATA_PATH:
		status = control_configDataPathRsp( pControl,
			data_hostPool( &p_viport->data ),
			data_eiocPool( &p_viport->data ),
			data_hostPoolMax( &p_viport->data ),
			data_eiocPoolMax( &p_viport->data ),
			data_hostPoolMin( &p_viport->data ),
			data_eiocPoolMin( &p_viport->data ));
		if( status )
		{
			InterlockedExchange(
				(volatile LONG*)&p_viport->linkState,
				(LONG)LINK_CONFIGDATAPATHRSP );
		}
		InterlockedOr( &p_viport->updates, SYNC_QUERY );
		break;

	case CMD_EXCHANGE_POOLS:
		status = control_exchangePoolsRsp( &p_viport->control,
			data_remotePoolAddr( &p_viport->data ),
			data_remotePoolRkey( &p_viport->data ) );
		if( status )
		{
			InterlockedExchange(
				(volatile LONG*)&p_viport->linkState,
				(LONG)LINK_XCHGPOOLRSP );
		}
		InterlockedOr( &p_viport->updates, SYNC_QUERY );
		break;

		/* process other responses */
	case CMD_CONFIG_LINK:
		status = control_configLinkRsp( &p_viport->control,
			&p_viport->flags,
			&p_viport->mtu );
		if( status )
		{
			InterlockedExchange(
				(volatile LONG*)&p_viport->linkState,
				(LONG)LINK_CONFIGLINKRSP );

			if( p_viport->flags & INIC_FLAG_ENABLE_NIC )
			{
				/* don't indicate media state yet if in sync query */
				if( ( InterlockedExchange( &p_viport->p_netpath->carrier, TRUE ) == FALSE ) &&
					!( p_viport->updates & SYNC_QUERY ) )
				{	
					viport_restartXmit( p_viport );
					//viport_linkUp( p_viport );
				}
			}
			else if( p_viport->flags & INIC_FLAG_DISABLE_NIC )
			{
				if( InterlockedExchange( &p_viport->p_netpath->carrier, FALSE ) == TRUE )
				{
					viport_stopXmit( p_viport );
					viport_linkDown( p_viport );
				}
			}
			InterlockedAnd( &p_viport->updates, ~NEED_LINK_CONFIG );
		}
		break;

	case CMD_HEARTBEAT:
		status = control_heartbeatRsp( pControl );
		if( status &&
			!p_viport->errored &&
			!p_viport->disconnect )
		{
			InterlockedExchange(
				(volatile LONG*)&p_viport->link_hb_state,
				(LONG)LINK_HEARTBEATRSP );
		}
		// Don't signal any waiting thread or start processing other updates.
		return;

	case CMD_CONFIG_ADDRESSES:
		status = control_configAddrsRsp( pControl );
		if( status == TRUE )
		{	// need more entries to send?
			if(	p_viport->addrs_query_done == 0 )
			{
				if( !p_viport->errored )
				{
					if( control_configAddrsReq( pControl,
							p_viport->macAddresses,
							p_viport->numMacAddresses,
							&p_viport->addrs_query_done ) != IB_SUCCESS )
					{
						viport_failure( p_viport );
					}
				}
				// Don't signal any waiting thread or start processing other updates.
				return;
			}

			InterlockedAnd( &p_viport->updates, ~NEED_ADDRESS_CONFIG );
			InterlockedExchange( (volatile LONG*)&p_viport->linkState,
				(LONG)LINK_CONFIGADDRSRSP );
		}
		break;

	case CMD_REPORT_STATISTICS:
		status = control_reportStatisticsRsp( pControl, &p_viport->stats );
		if ( status )
		{
			if( p_viport->stats.ethernetStatus > 0 &&
				!p_viport->errored )
			{
				viport_linkUp( p_viport );
			}
			else
			{
				viport_linkDown( p_viport );
			}
		}
		InterlockedAnd( &p_viport->updates, ~NEED_STATS );
		break;

	case CMD_RESET:
		status = control_resetRsp( pControl );
		if( status )
		{
			status = FALSE;
			InterlockedExchange(
				(volatile LONG*)&p_viport->linkState,
				(LONG)LINK_RESETRSP );
		}
		break;

	default:
		break;
	}
	
	if( _viport_process_query( p_viport, FALSE ) != NDIS_STATUS_PENDING )
	{
		/* Complete any pending set OID. */
		vnic_resume_set_oids( p_viport->p_adapter );
	}

	if(	InterlockedAnd( &p_viport->updates, ~SYNC_QUERY ) & SYNC_QUERY )
		cl_event_signal( &p_viport->sync_event );
}


static ib_api_status_t
control_send(
	IN		Control_t		*pControl )
{
	Inic_ControlPacket_t *pPkt = control_packet(&pControl->sendIo);

	VNIC_ENTER ( VNIC_DBG_CTRL );

	if ( InterlockedCompareExchange( (volatile LONG*)&pControl->reqOutstanding,
									TRUE, FALSE ) == TRUE )
	{
		/* IF WE HIT THIS WE ARE HOSED!!!
		 * by the time we detect this error, the send buffer has been
		 * overwritten, and if we retry we will send garbage data.
		 */
		VNIC_TRACE_EXIT( VNIC_DBG_ERROR,
			("IB Send never completed\n" ) );
		goto failure;
	}

#ifdef _DEBUG_
	//__control_logControlPacket( pPkt );
#endif
	InterlockedExchange( (volatile LONG*)&pControl->rspExpected,
										(LONG)pPkt->hdr.pktCmd );

	control_timer( pControl, pControl->p_conf->rspTimeout );

#ifdef VNIC_STATISTIC
	pControl->statistics.requestTime = cl_get_time_stamp();
#endif /* VNIC_STATISTIC */

	if( ( ibqp_postSend( &pControl->qp, &pControl->sendIo.io )) != IB_SUCCESS )
	{
		InterlockedExchange((volatile LONG*)&pControl->reqOutstanding, FALSE );

		VNIC_TRACE( VNIC_DBG_ERROR,
			("IOC %d: Failed to post send\n", pControl->p_viport->ioc_num ) );
		goto failure;
	}
	
	VNIC_EXIT( VNIC_DBG_CTRL );
	return IB_SUCCESS;

failure:
	pControl->p_viport->p_adapter->hung++;
	viport_failure( pControl->p_viport );
	VNIC_EXIT( VNIC_DBG_CTRL );
	return IB_ERROR;
}


static void
control_sendComplete(
		IN		Io_t		*pIo )
{
	Control_t *pControl = &pIo->pViport->control;

	VNIC_ENTER( VNIC_DBG_CTRL );

	InterlockedExchange((volatile LONG*)&pControl->reqOutstanding, FALSE );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return;
}

static void
control_timeout(
		IN		void	*p_context )
{
	Control_t *pControl;

	VNIC_ENTER( VNIC_DBG_CTRL );

	pControl = (Control_t *)p_context;

	InterlockedExchange( (LONG *)&pControl->timerstate, (LONG)TIMER_EXPIRED );

	InterlockedExchange( (volatile LONG*)&pControl->rspExpected, FALSE );

	VNIC_EXIT( VNIC_DBG_CTRL );
	return;
}

static void
control_timer(
		IN		Control_t	*pControl,
		IN		int			timeout )
{
	VNIC_ENTER( VNIC_DBG_CTRL );

	InterlockedExchange( (LONG *)&pControl->timerstate, (LONG)TIMER_ACTIVE );

	cl_timer_start(&pControl->timer, timeout);

	VNIC_EXIT( VNIC_DBG_CTRL );
	return;
}

static void
control_timerStop(
		IN			Control_t	*pControl )
{
	VNIC_ENTER( VNIC_DBG_CTRL );


	if ( ( InterlockedExchange( (LONG *)&pControl->timerstate,
										(LONG)TIMER_IDLE )) == TIMER_ACTIVE )
	{
		cl_timer_stop( &pControl->timer );
	}

	VNIC_EXIT( VNIC_DBG_CTRL );
	return;
}

static void
control_initHdr(
		IN		Control_t *		pControl,
		IN		uint8_t			cmd )
{
	ControlConfig_t      *p_conf;
	Inic_ControlPacket_t *pPkt;
	Inic_ControlHeader_t *pHdr;

	VNIC_ENTER( VNIC_DBG_CTRL );

	p_conf = pControl->p_conf;

	pPkt = control_packet( &pControl->sendIo );
	pHdr = &pPkt->hdr;

	pHdr->pktType = TYPE_REQ;
	pHdr->pktCmd = cmd;
	pHdr->pktSeqNum = ++pControl->seqNum;
	pControl->reqRetryCounter = 0;
	pHdr->pktRetryCount = 0;

	VNIC_EXIT( VNIC_DBG_CTRL );
}

static RecvIo_t*
control_getRsp(
		IN			Control_t		*pControl )
{
	RecvIo_t		*pRecvIo;

	VNIC_ENTER ( VNIC_DBG_CTRL );

	pRecvIo = InterlockedExchangePointer( &pControl->pResponse, NULL );

	if ( pRecvIo != NULL )
	{
		control_timerStop(pControl);
		return pRecvIo;
	}

	if ( ( pControl->timerstate =
				InterlockedCompareExchange( (LONG *)&pControl->timerstate,
				(LONG)TIMER_IDLE, (LONG)TIMER_EXPIRED )) == TIMER_EXPIRED )
	{
		Inic_ControlPacket_t *pPkt = control_packet( &pControl->sendIo );
		Inic_ControlHeader_t *pHdr = &pPkt->hdr;

		VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC %d: No response received from EIOC\n",
						pControl->p_viport->ioc_num ) );
#ifdef VNIC_STATISTIC
		pControl->statistics.timeoutNum++;
#endif /* VNIC_STATISTIC */

		pControl->reqRetryCounter++;

		if ( pControl->reqRetryCounter >= pControl->p_conf->reqRetryCount )
		{
			VNIC_TRACE( VNIC_DBG_ERROR,
				("IOC %d: Control packet retry exceeded\n",
						pControl->p_viport->ioc_num ) );
		}
		else
		{
			pHdr->pktRetryCount = pControl->reqRetryCounter;
			control_send( pControl );
		}
	}
	return NULL;
}

static void
copyRecvPoolConfig(
		IN			Inic_RecvPoolConfig_t		*pSrc,
		IN OUT		Inic_RecvPoolConfig_t		*pDst )
{

	pDst->sizeRecvPoolEntry            = hton32(pSrc->sizeRecvPoolEntry);
	pDst->numRecvPoolEntries           = hton32(pSrc->numRecvPoolEntries);
	pDst->timeoutBeforeKick            = hton32(pSrc->timeoutBeforeKick);
	pDst->numRecvPoolEntriesBeforeKick = hton32(pSrc->numRecvPoolEntriesBeforeKick);
	pDst->numRecvPoolBytesBeforeKick   = hton32(pSrc->numRecvPoolBytesBeforeKick);
	pDst->freeRecvPoolEntriesPerUpdate = hton32(pSrc->freeRecvPoolEntriesPerUpdate);
	return;
}

static BOOLEAN
checkRecvPoolConfigValue(
		IN		void		*pSrc,
		IN		void		*pDst,
		IN		void		*pMax,
		IN		void		*pMin,
		IN		char		*name )
{
	uint32_t value;
	uint32_t *p_src = (uint32_t *)pSrc;
	uint32_t *p_dst = (uint32_t *)pDst;
	uint32_t *p_min = (uint32_t *)pMin;
	uint32_t *p_max = (uint32_t *)pMax;

	UNREFERENCED_PARAMETER( name );

	value = ntoh32( *p_src );

	if (value > *p_max )
	{
		VNIC_TRACE(  VNIC_DBG_ERROR,
					("Value %s too large\n", name) );
		return FALSE;
	}
	else if (value < *p_min )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
				("Value %s too small\n", name) );
		return FALSE;
	}

	*p_dst = value;
	return TRUE;
}

static BOOLEAN
checkRecvPoolConfig(
		IN		Inic_RecvPoolConfig_t		*pSrc,
		IN		Inic_RecvPoolConfig_t		*pDst,
		IN		Inic_RecvPoolConfig_t		*pMax,
		IN		Inic_RecvPoolConfig_t		*pMin )
{
	if (!checkRecvPoolConfigValue(&pSrc->sizeRecvPoolEntry, &pDst->sizeRecvPoolEntry,
	    &pMax->sizeRecvPoolEntry, &pMin->sizeRecvPoolEntry, "sizeRecvPoolEntry")
	|| !checkRecvPoolConfigValue(&pSrc->numRecvPoolEntries, &pDst->numRecvPoolEntries,
	    &pMax->numRecvPoolEntries, &pMin->numRecvPoolEntries, "numRecvPoolEntries")
	|| !checkRecvPoolConfigValue(&pSrc->timeoutBeforeKick, &pDst->timeoutBeforeKick,
	    &pMax->timeoutBeforeKick, &pMin->timeoutBeforeKick, "timeoutBeforeKick")
	|| !checkRecvPoolConfigValue(&pSrc->numRecvPoolEntriesBeforeKick,
	    &pDst->numRecvPoolEntriesBeforeKick, &pMax->numRecvPoolEntriesBeforeKick,
	    &pMin->numRecvPoolEntriesBeforeKick, "numRecvPoolEntriesBeforeKick")
	|| !checkRecvPoolConfigValue(&pSrc->numRecvPoolBytesBeforeKick,
	    &pDst->numRecvPoolBytesBeforeKick, &pMax->numRecvPoolBytesBeforeKick,
	    &pMin->numRecvPoolBytesBeforeKick, "numRecvPoolBytesBeforeKick")
	|| !checkRecvPoolConfigValue(&pSrc->freeRecvPoolEntriesPerUpdate,
	    &pDst->freeRecvPoolEntriesPerUpdate, &pMax->freeRecvPoolEntriesPerUpdate,
	    &pMin->freeRecvPoolEntriesPerUpdate, "freeRecvPoolEntriesPerUpdate"))
		return FALSE;

	if ( !IsPowerOf2( pDst->numRecvPoolEntries ) )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("numRecvPoolEntries (%d) must be power of 2\n",
					pDst->numRecvPoolEntries) );
		return FALSE;
	}
	if ( !IsPowerOf2( pDst->freeRecvPoolEntriesPerUpdate ) )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("freeRecvPoolEntriesPerUpdate (%d) must be power of 2\n",
					pDst->freeRecvPoolEntriesPerUpdate) );
		return FALSE;
	}
	if ( pDst->freeRecvPoolEntriesPerUpdate >= pDst->numRecvPoolEntries )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("freeRecvPoolEntriesPerUpdate (%d) must be less than numRecvPoolEntries (%d)\n",
			pDst->freeRecvPoolEntriesPerUpdate, pDst->numRecvPoolEntries) );
			return FALSE;
	}
	if ( pDst->numRecvPoolEntriesBeforeKick >= pDst->numRecvPoolEntries )
	{
		VNIC_TRACE( VNIC_DBG_ERROR,
			("numRecvPoolEntriesBeforeKick (%d) must be less than numRecvPoolEntries (%d)\n",
				pDst->numRecvPoolEntriesBeforeKick, pDst->numRecvPoolEntries) );
			return FALSE;
	}

	return TRUE;
}

static void
__control_logControlPacket(
			IN		Inic_ControlPacket_t		*pPkt )
{
	char *type;
	int i;

	switch( pPkt->hdr.pktType  )
	{
	case TYPE_INFO:
		type = "TYPE_INFO";
		break;
	case TYPE_REQ:
		type = "TYPE_REQ";
		break;
	case TYPE_RSP:
		type = "TYPE_RSP";
		break;
	case TYPE_ERR:
		type = "TYPE_ERR";
		break;
	default:
		type = "UNKNOWN";
	}
	switch( pPkt->hdr.pktCmd  )
	{
	case CMD_INIT_INIC:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_INIT_INIC\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
								pPkt->hdr.pktSeqNum,
								pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               inicMajorVersion = %u, inicMinorVersion = %u\n",
								ntoh16(pPkt->cmd.initInicReq.inicMajorVersion),
								ntoh16(pPkt->cmd.initInicReq.inicMinorVersion)) );
		if (pPkt->hdr.pktType == TYPE_REQ)
		{
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               inicInstance = %u, numDataPaths = %u\n",
			       pPkt->cmd.initInicReq.inicInstance,
			       pPkt->cmd.initInicReq.numDataPaths) );
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               numAddressEntries = %u\n",
								ntoh16(pPkt->cmd.initInicReq.numAddressEntries)) );
		}
		else
		{
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               numLanSwitches = %u, numDataPaths = %u\n",
								pPkt->cmd.initInicRsp.numLanSwitches,
								pPkt->cmd.initInicRsp.numDataPaths) );
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               numAddressEntries = %u, featuresSupported = %08x\n",
								ntoh16(pPkt->cmd.initInicRsp.numAddressEntries),
								ntoh32(pPkt->cmd.initInicRsp.featuresSupported)) );
			if (pPkt->cmd.initInicRsp.numLanSwitches != 0)
			{
				VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("lanSwitch[0]  lanSwitchNum = %u, numEnetPorts = %08x\n",
				       pPkt->cmd.initInicRsp.lanSwitch[0].lanSwitchNum,
				       pPkt->cmd.initInicRsp.lanSwitch[0].numEnetPorts) );
				VNIC_PRINT( VNIC_DBG_CTRL_PKT,
					("               defaultVlan = %u, hwMacAddress = %02x:%02x:%02x:%02x:%02x:%02x\n",
								ntoh16(pPkt->cmd.initInicRsp.lanSwitch[0].defaultVlan),
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[0],
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[1],
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[2],
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[3],
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[4],
								pPkt->cmd.initInicRsp.lanSwitch[0].hwMacAddress[5]) );
			}
		}
		break;
	case CMD_CONFIG_DATA_PATH:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			( "ControlPacket: pktType = %s, pktCmd = CMD_CONFIG_DATA_PATH\n", type) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
								pPkt->hdr.pktSeqNum,
								pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pathIdentifier = %"PRIx64", dataPath = %u\n",
							pPkt->cmd.configDataPathReq.pathIdentifier,
							pPkt->cmd.configDataPathReq.dataPath) );
	
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("Host Config    sizeRecvPoolEntry = %u, numRecvPoolEntries = %u\n",
					ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.sizeRecvPoolEntry),
					ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.numRecvPoolEntries)) );
	
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               timeoutBeforeKick = %u, numRecvPoolEntriesBeforeKick = %u\n",
					ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.timeoutBeforeKick),
					ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.numRecvPoolEntriesBeforeKick)) );
	
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               numRecvPoolBytesBeforeKick = %u, freeRecvPoolEntriesPerUpdate = %u\n",
		       ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.numRecvPoolBytesBeforeKick),
		       ntoh32(pPkt->cmd.configDataPathReq.hostRecvPoolConfig.freeRecvPoolEntriesPerUpdate)) );

		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("Eioc Config    sizeRecvPoolEntry = %u, numRecvPoolEntries = %u\n",
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.sizeRecvPoolEntry),
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.numRecvPoolEntries)) );

		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               timeoutBeforeKick = %u, numRecvPoolEntriesBeforeKick = %u\n",
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.timeoutBeforeKick),
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.numRecvPoolEntriesBeforeKick)) );

		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               numRecvPoolBytesBeforeKick = %u, freeRecvPoolEntriesPerUpdate = %u\n",
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.numRecvPoolBytesBeforeKick),
		       ntoh32(pPkt->cmd.configDataPathReq.eiocRecvPoolConfig.freeRecvPoolEntriesPerUpdate)) );
		break;
	case CMD_EXCHANGE_POOLS:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_EXCHANGE_POOLS\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
							pPkt->hdr.pktSeqNum,
							pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               datapath = %u\n",
						pPkt->cmd.exchangePoolsReq.dataPath) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               poolRKey = %08x poolAddr = %"PRIx64"\n",
						ntoh32(pPkt->cmd.exchangePoolsReq.poolRKey),
						ntoh64(pPkt->cmd.exchangePoolsReq.poolAddr)) );
		break;
	case CMD_CONFIG_ADDRESSES:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			( "ControlPacket: pktType = %s, pktCmd = CMD_CONFIG_ADDRESSES\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
							pPkt->hdr.pktSeqNum,
							pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               numAddressOps = %x, lanSwitchNum = %d\n",
							pPkt->cmd.configAddressesReq.numAddressOps,
							pPkt->cmd.configAddressesReq.lanSwitchNum) );
		for (i = 0; ( i < pPkt->cmd.configAddressesReq.numAddressOps) && (i < 16); i++)
		{
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               listAddressOps[%u].index = %u\n",
					i, ntoh16(pPkt->cmd.configAddressesReq.listAddressOps[i].index)) );
		       
			switch(pPkt->cmd.configAddressesReq.listAddressOps[i].operation)
			{
			case INIC_OP_GET_ENTRY:
				VNIC_PRINT( VNIC_DBG_CTRL_PKT,
					("               listAddressOps[%u].operation = INIC_OP_GET_ENTRY\n", i) );
				break;
			case INIC_OP_SET_ENTRY:
				VNIC_PRINT( VNIC_DBG_CTRL_PKT,
					("               listAddressOps[%u].operation = INIC_OP_SET_ENTRY\n", i) );
				break;
			default:
				VNIC_PRINT( VNIC_DBG_CTRL_PKT,
					("               listAddressOps[%u].operation = UNKNOWN(%d)\n",
		        	       i, pPkt->cmd.configAddressesReq.listAddressOps[i].operation) );
				break;
			}
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               listAddressOps[%u].valid = %u\n",
		               i, pPkt->cmd.configAddressesReq.listAddressOps[i].valid) );
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               listAddressOps[%u].address = %02x:%02x:%02x:%02x:%02x:%02x\n", i,
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[0],
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[1],
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[2],
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[3],
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[4],
					pPkt->cmd.configAddressesReq.listAddressOps[i].address[5]) );

			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               listAddressOps[%u].vlan = %u\n",
		               i, ntoh16(pPkt->cmd.configAddressesReq.listAddressOps[i].vlan)) );
		}
		break;
	case CMD_CONFIG_LINK:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_CONFIG_LINK\n", type) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
		       pPkt->hdr.pktSeqNum,
		       pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               cmdFlags = %x\n",
		       pPkt->cmd.configLinkReq.cmdFlags) );

		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_ENABLE_NIC )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_ENABLE_NIC\n") );

		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_DISABLE_NIC )
	
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_DISABLE_NIC\n") );

		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_ENABLE_MCAST_ALL )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_ENABLE_MCAST_ALL\n") );

		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_DISABLE_MCAST_ALL )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_DISABLE_MCAST_ALL\n") );

		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_ENABLE_PROMISC )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_ENABLE_PROMISC\n") );
	
		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_DISABLE_PROMISC )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_DISABLE_PROMISC\n") );
		if ( pPkt->cmd.configLinkReq.cmdFlags & INIC_FLAG_SET_MTU )
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("                       INIC_FLAG_SET_MTU\n") );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               lanSwitchNum = %x, mtuSize = %d\n",
						pPkt->cmd.configLinkReq.lanSwitchNum,
						ntoh16(pPkt->cmd.configLinkReq.mtuSize)) );
		if ( pPkt->hdr.pktType == TYPE_RSP )
		{
			VNIC_PRINT( VNIC_DBG_CTRL_PKT,
				("               defaultVlan = %u, hwMacAddress = %02x:%02x:%02x:%02x:%02x:%02x\n",
				ntoh16(pPkt->cmd.configLinkReq.defaultVlan),
				pPkt->cmd.configLinkReq.hwMacAddress[0],
				pPkt->cmd.configLinkReq.hwMacAddress[1],
				pPkt->cmd.configLinkReq.hwMacAddress[2],
				pPkt->cmd.configLinkReq.hwMacAddress[3],
				pPkt->cmd.configLinkReq.hwMacAddress[4],
				pPkt->cmd.configLinkReq.hwMacAddress[5]) );
		}
		break;
	case CMD_REPORT_STATISTICS:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_REPORT_STATISTICS\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               lanSwitchNum = %u\n",
			pPkt->cmd.reportStatisticsReq.lanSwitchNum) );

		if (pPkt->hdr.pktType == TYPE_REQ)
			break;
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInBroadcastPkts = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInBroadcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifInMulticastPkts = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInMulticastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInOctets = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInOctets)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifInUcastPkts = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInUcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInNUcastPkts = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInNUcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifInUnderrun = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInUnderrun)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInErrors = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInErrors)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutErrors = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutErrors)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifOutOctets = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutOctets)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutUcastPkts = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutUcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifOutMulticastPkts = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutMulticastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutBroadcastPkts = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutBroadcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifOutNUcastPkts = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutNUcastPkts)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutOk = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutOk)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInOk = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInOk)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutUcastBytes = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutUcastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifOutMulticastBytes = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutMulticastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifOutBroadcastBytes = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifOutBroadcastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInUcastBytes = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInUcastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ifInMulticastBytes = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInMulticastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               ifInBroadcastBytes = %"PRIu64,
			ntoh64(pPkt->cmd.reportStatisticsRsp.ifInBroadcastBytes)) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			(" ethernetStatus = %"PRIu64"\n",
			ntoh64(pPkt->cmd.reportStatisticsRsp.ethernetStatus)) );
		break;
	case CMD_CLEAR_STATISTICS:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_CLEAR_STATISTICS\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		break;
	case CMD_REPORT_STATUS:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_REPORT_STATUS\n",
			type) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               lanSwitchNum = %u, isFatal = %u\n",
			pPkt->cmd.reportStatus.lanSwitchNum,
			pPkt->cmd.reportStatus.isFatal) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               statusNumber = %u, statusInfo = %u\n",
			ntoh32(pPkt->cmd.reportStatus.statusNumber),
			ntoh32(pPkt->cmd.reportStatus.statusInfo)) );
		pPkt->cmd.reportStatus.fileName[31] = '\0';
		pPkt->cmd.reportStatus.routine[31] = '\0';
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               filename = %s, routine = %s\n",
			pPkt->cmd.reportStatus.fileName,
			pPkt->cmd.reportStatus.routine) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               lineNum = %u, errorParameter = %u\n",
			ntoh32(pPkt->cmd.reportStatus.lineNum),
			ntoh32(pPkt->cmd.reportStatus.errorParameter)) );
		pPkt->cmd.reportStatus.descText[127] = '\0';
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               descText = %s\n",
			pPkt->cmd.reportStatus.descText) );
		break;
	case CMD_RESET:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_RESET\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		break;
	case CMD_HEARTBEAT:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = CMD_HEARTBEAT\n", type ) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               hbInterval = %d\n",
			ntoh32(pPkt->cmd.heartbeatReq.hbInterval)) );
		break;
	default:
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("ControlPacket: pktType = %s, pktCmd = UNKNOWN (%u)\n",
			type,pPkt->hdr.pktCmd) );
		VNIC_PRINT( VNIC_DBG_CTRL_PKT,
			("               pktSeqNum = %u, pktRetryCount = %u\n",
			pPkt->hdr.pktSeqNum,
			pPkt->hdr.pktRetryCount) );
		break;
	}
	return;
}
