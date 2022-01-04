/*
 * Copyright (c) 2013 Intel Corporation. All rights reserved.
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

/**********************************************************************
 *
 * MODULE: dapl_nd_cm.c
 *
 * PURPOSE: DAPL over NetworkDirect Communications Manager
 *
 **********************************************************************/

#include "dapl.h"
#include "dapl_adapter_util.h"
#include "dapl_evd_util.h"
#include "dapl_sp_util.h"
#include "dapl_cr_util.h"
#include "dapl_name_service.h"
#include "dapl_nd.h"
#include "dapl_ep_util.h"
#include "dapl_vendor.h"
#include "dapl_osd.h"

static void dapli_addr_resolve(struct dapl_cm_id *conn);
static void dapli_route_resolve(struct dapl_cm_id *conn);
static void dapli_release_ND_resources(struct dapl_cm_id *conn);
static void dapli_remove_ND_listen( DAPL_CM_ID *pConn );
static void dapli_accept_complete( struct dapl_cm_id *pConn, HRESULT Error );
static void dapli_disconnect_complete( struct dapl_cm_id *pConn, HRESULT Error );

int g_dapl_loopback_connection = 0;

extern int gDAPL_rank;

/* NetworkDirect requires 16 bit SID/port, in network order */
#define IB_PORT_MOD 32001UL
#define IB_PORT_BASE (65535 - IB_PORT_MOD)

#define SID_TO_PORT(SID) (SID > 0xffff ? \
    htons((unsigned short)((SID % IB_PORT_MOD) + IB_PORT_BASE)) :\
    htons((unsigned short)SID))

#define PORT_TO_SID(p) ntohs(p)

/* private data header to validate consumer rejects versus abnormal events */
struct dapl_pdata_hdr {
	DAT_UINT32 version;
};

/*
 * Create a CM_ID struct.
 *
 * inputs:
 *	ep - DAPL ep ptr, can be NULL
 *
 * outputs:
 *	address of a CM_ID struct.
 */

DAPL_CM_ID *dapls_alloc_cm_id(DAPL_HCA *hca_ptr, HANDLE AdapterOvFileHandle)
{
	HRESULT hr;
	DAT_RETURN status;
	DAPL_CM_ID *pConn;

	/* Allocate CM ID struct + initialize lock */
	pConn = dapl_os_alloc(sizeof(DAPL_CM_ID));
	if (pConn == NULL)
		return pConn;
	dapl_os_memzero(pConn, sizeof(*pConn));
	pConn->hca = hca_ptr;
	pConn->pAdapter = hca_ptr->ib_hca_handle;
	dapl_os_assert( AdapterOvFileHandle );
	pConn->OvFileHandle = AdapterOvFileHandle;
	status = dapl_os_wait_object_init(&pConn->Event);
	dapl_os_assert( status == DAT_SUCCESS );
	if ( status != DAT_SUCCESS ) {
        	dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() pConn(%lx)->event failed to initialize @ line #%d\n",
			__FUNCTION__,pConn,__LINE__ );
		goto bail;
	}
	pConn->OverLap.Ov.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	pConn->serial.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	if( pConn->serial.hEvent == NULL )
	{
        	dapl_log(DAPL_DBG_TYPE_ERR, "%s() Failed to allocate "
			"pConn->serial.hEvent @ line #%d\n",
			__FUNCTION__,__LINE__ );
		goto bail1;
	}
	/* mark Overlap event as serial only - no IO Completion callback when used */
	pConn->serial.hEvent = (HANDLE) ((UINT32) pConn->serial.hEvent | 1);

	dapl_os_lock_init(&pConn->lock);
	pConn->ref_count = 1;
	dapl_os_atomic_inc( &hca_ptr->ib_trans.num_connections );

	/* setup timers for address and route resolution */
	pConn->arp_timeout = hca_ptr->ib_trans.arp_timeout;
	pConn->arp_retries = hca_ptr->ib_trans.arp_retries;
	pConn->route_timeout = hca_ptr->ib_trans.route_timeout;
	pConn->route_retries = hca_ptr->ib_trans.route_retries;

	pConn->ConnState = DCM_INIT;

	return pConn;

bail1:
	CloseHandle( pConn->OverLap.Ov.hEvent );
	pConn->OverLap.Ov.hEvent = INVALID_HANDLE_VALUE;
	CloseHandle( pConn->serial.hEvent );
	pConn->serial.hEvent = INVALID_HANDLE_VALUE;
bail:
	pConn->ConnState = DCM_FREE;
	dapl_os_lock_destroy(&pConn->lock);
	dapl_os_free(pConn, sizeof(*pConn));
	return NULL;
}


static void dapli_cm_dealloc(DAPL_CM_ID *pConn)
{
	HRESULT hr;

	dapl_log(DAPL_DBG_TYPE_CM, "%s(pConn %lx)\n",__FUNCTION__,pConn);
	dapl_os_assert(pConn->ref_count == 0);
	dapli_release_ND_resources( pConn );
	dapl_os_assert(pConn->hca);
	dapl_os_atomic_dec( &pConn->hca->ib_trans.num_connections );
	dapl_os_lock_destroy(&pConn->lock);
	pConn->ConnState = DCM_FREE;
	dapl_os_free(pConn, sizeof(*pConn));
}

void dapls_cm_acquire(dp_ib_cm_handle_t pConn)
{
	dapl_os_lock(&pConn->lock);
	pConn->ref_count++;
	dapl_os_unlock(&pConn->lock);
}

void dapls_cm_release(dp_ib_cm_handle_t pConn)
{
	dapl_os_lock(&pConn->lock);
	pConn->ref_count--;
	dapl_os_assert( pConn->ref_count >= 0);
	if (pConn->ref_count > 0) {
                dapl_os_unlock(&pConn->lock);
		return;
	}
	dapl_os_unlock(&pConn->lock);
	dapli_cm_dealloc(pConn);
}


/* BLOCKING: called from dapl_ep_free, EP link will be last ref */

void dapls_cm_free(dp_ib_cm_handle_t pConn)
{
	dapl_os_assert(pConn);
	dapl_os_assert(pConn->ep);
	dapl_os_assert(pConn->ConnState != DCM_FREE);

	//dapl_log(DAPL_DBG_TYPE_CM, "%s(pConn %lx) ep %lx refs=%d\n", 
	//	 __FUNCTION__, pConn, pConn->ep, pConn->ref_count);
	
	dapls_cm_release(pConn); /* release alloc ref */

	/* Destroy cm_id, wait until EP is last ref */
	dapl_os_lock(&pConn->lock);

	/* EP linking is last reference */
	while (pConn->ref_count != 1) {
		dapl_os_unlock(&pConn->lock);
		dapl_os_sleep_usec(10000);
		dapl_os_lock(&pConn->lock);
	}
	dapl_os_unlock(&pConn->lock);

	//dapl_log(DAPL_DBG_TYPE_ERR/*CM*/,"%s() ep_unlink_cm(ep %lx pConn %lx)\n", 
	//	 __FUNCTION__, pConn->ep, pConn);
	/* unlink, dequeue from EP. Final ref so release will destroy */
	dapl_ep_unlink_cm(pConn->ep, pConn);
}

/************************ DAPL provider entry points **********************/

/*
 * dapls_ib_connect
 *
 * Initiate a connection with a passive listener
 *
 * Input:
 *	ep_handle		EndPoint handle
 *	remote_ia_address	IPv4/6 address
 *	remote_conn_qual	aka port #
 *	prd_size		size of private data and structure
 *	prd_prt			pointer to private data structure
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *	DAT_INVALID_PARAMETER
 *
 */
DAT_RETURN dapls_ib_connect(IN DAT_EP_HANDLE ep_handle,
			    IN DAT_SOCK_ADDR *r_addr,
			    IN DAT_CONN_QUAL r_qual,
			    IN DAT_COUNT p_size,
			    IN void *p_data)
{
	DAPL_IA	*ia_ptr;
	DAPL_EP *ep_ptr;
	struct dapl_cm_id *pConn;
	DAT_RETURN dat_status = DAT_SUCCESS;
	HRESULT hr;
	IND2Adapter *pAdapter;
	IND2Connector *pConnector;
	int i, tries;
	ib_cm_events_t conn_event;
	SOCKADDR_INET sock_addr;
	struct sockaddr_in *pSain;
	SOCKADDR_INET LocalAddr;
	struct sockaddr_in *LA = (struct sockaddr_in*)&LocalAddr.Ipv4;
	SIZE_T Len, sa_len;
	ULONG InboundReadLimit, OutboundReadLimit;
	
	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		     "%s(): r_qual/rPort [%I64d 0x%I64x] private_data_size %d\n",
		     __FUNCTION__,r_qual, r_qual, p_size);

	ep_ptr = ep_handle;
	ia_ptr = ep_ptr->header.owner_ia;
	pAdapter = ia_ptr->hca_ptr->ib_hca_handle; 
	dapl_os_assert( ep_ptr->qp_handle );

	/* alloc a new cm_id struct to describe the connection request (REQ) */
	pConn = dapls_alloc_cm_id( ia_ptr->hca_ptr,
				    ia_ptr->hca_ptr->ib_trans.AdapterOvFileHandle );
	if ( pConn == NULL )
	{
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() ERR: unable to allocate DAPL_CM_ID?\n",__FUNCTION__);
		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_MEMORY);
	}

	pConn->ep = ep_ptr;
	pConn->h_qp = ep_ptr->qp_handle;

	/* link this connection into the EndPoint's list of connections so
	 * dapl_get_cm_from_ep() works.
	 */
	dapl_ep_link_cm(ep_ptr, pConn);

	if (DAPL_BAD_HANDLE(pConn->ep, DAPL_MAGIC_EP))
		return DAT_ERROR(DAT_INVALID_HANDLE,DAT_INVALID_HANDLE_EP);

	dapl_os_lock(&pConn->ep->header.lock);

	if (pConn->ep->param.ep_state == DAT_EP_STATE_DISCONNECTED)
	{
		dapl_os_unlock(&pConn->ep->header.lock);
		return DAT_ERROR(DAT_INVALID_STATE,DAT_INVALID_STATE_EP_DISCONNECTED);
	}
	dapl_os_unlock(&pConn->ep->header.lock);

	dapl_os_assert( pConn->h_qp );
	dapl_os_assert( pConn->h_qp->ND_QP );
	dapl_os_assert( ep_ptr->qp_handle == pConn->h_qp );
	dapl_os_assert( pConn->pAdapter );
	dapl_os_assert( pConn->OvFileHandle );
	dapl_os_assert( !pConn->pConnector );

	hr = pAdapter->lpVtbl->CreateConnector( pAdapter,
						&IID_IND2Connector,
						pConn->OvFileHandle,
						&pConn->pConnector );
	if( hr == ND_PENDING )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() PENDING: CreateConnector() @ line # %d\n",
					__FUNCTION__, __LINE__ );
		hr = pConn->pConnector->lpVtbl->GetOverlappedResult(
								pConn->pConnector,
							      	&pConn->serial,
							      	TRUE );
	}
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s(pConn %lx) CreateConnector() ERR: %s @ line#%d\n",
				__FUNCTION__,pConn,ND_error_str(hr),__LINE__);
		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_MEMORY);
	}
	pConnector = pConn->pConnector;

	if (p_size > 0)
	{
		dapl_os_memcpy(pConn->p_data, p_data, p_size);
		pConn->p_len = p_size;
	}
	else
	{
		pConn->p_len = p_size = 0;
		p_data = NULL;
	}

	/* copy in remote address, need a copy for retry attempts */
	memset( &pConn->remote_addr, 0, sizeof(pConn->remote_addr) );

	if( r_addr->sa_family == AF_INET )
	{
		dapl_os_memcpy( &pConn->remote_addr,
				r_addr,
				sizeof(*r_addr) );
		pSain = (struct sockaddr_in *)&pConn->remote_addr;
		pSain->sin_port = SID_TO_PORT(r_qual);
	}
	else
	{ //. AF_INET6 
		dapl_log(DAPL_DBG_TYPE_ERR,
		     "%s(%I64d) IPv6 address not supported\n",
			__FUNCTION__,dapl_os_time_stamp(),
			dapli_IPaddr_str((DAT_SOCK_ADDR*)r_addr,NULL));
#if 1 // XXX FIX ME FOR IPv6!
		return DAT_INVALID_ADDRESS;
#else
		pSain6 = (struct sockaddr_in6 *)&pConn->remote_addr;
		dapl_os_memcpy( &pConn->remote_addr,
				r_addr,
				sizeof(*r_addr) );
#endif
	}

	Len = sizeof(LocalAddr);

	hr = NdResolveAddress(	(const struct sockaddr*)pSain,
				sizeof(struct sockaddr_in),
				(SOCKADDR*)&LocalAddr,
				&Len );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
        	    "[%d] Connect: Remote %s port %d(%#x) Local: %s port %d(%#x)\n",
				(int)GetCurrentProcessId(),
				inet_ntoa(pSain->sin_addr),
				ntohs(pSain->sin_port),
				ntohs(pSain->sin_port),
				inet_ntoa(LA->sin_addr),
				ntohs(LA->sin_port),
				ntohs(LA->sin_port));
		dapl_log(DAPL_DBG_TYPE_ERR/* XXX CM*/,
        		"%s() NdResolveAddress(%s : %d) failed %s @ line #%d\n",
				__FUNCTION__, inet_ntoa(pSain->sin_addr),
				ND_error_str(hr), __LINE__ );
		dat_status = DAT_ERROR(DAT_INVALID_ADDRESS,
					DAT_INVALID_ADDRESS_UNREACHABLE);
		goto bail;
	}
	dapl_os_assert( LocalAddr.si_family == AF_INET );

	/* must bind to local address in order to achieve successful connect() */
	hr = pConnector->lpVtbl->Bind( pConnector,
					(struct sockaddr*)&LocalAddr,
					(ULONG) sizeof(LocalAddr) );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() Connect: Bind() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = dapl_cvt_ND_to_DAT_status(hr);
		conn_event = IB_CME_LOCAL_FAILURE; 
		goto bail;
	}

	Len = ( ((struct sockaddr *)pSain)->sa_family == AF_INET
			? sizeof(struct sockaddr_in)
			: sizeof(struct sockaddr_in6) );

	InboundReadLimit =
		ia_ptr->hca_ptr->ib_trans.nd_adapter_info.MaxInboundReadLimit; 

	OutboundReadLimit =
		ia_ptr->hca_ptr->ib_trans.nd_adapter_info.MaxOutboundReadLimit; 

	dapl_os_assert( pConn->h_qp->ND_QP );

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
       		"%s() Connect [%s] AddrLen %ld inBoundReadLim %ld "
		"OutBoundReadLim %ld Pdata_len %d\n", __FUNCTION__,
			dapli_IPaddr_str((DAT_SOCK_ADDR*)pSain,NULL),Len,
			InboundReadLimit, OutboundReadLimit, p_size);

	ep_ptr->qp_state = DAPL_QP_STATE_ACTIVE_CONNECTION_PENDING;

	pConn->OverLap.ior_type = IOR_CompleteConnect;
	pConn->OverLap.context = (void*) pConn;

	hr = pConnector->lpVtbl->Connect( pConnector,	// this
				  	(IUnknown*) pConn->h_qp->ND_QP,
				  	(const struct sockaddr *) pSain,
				  	(ULONG) Len,
				  	InboundReadLimit,
				  	OutboundReadLimit,
				  	(const void*) p_data,
				  	(ULONG) p_size,
				  	&pConn->OverLap.Ov );
	// ND_TIMEOUT considered FAILED()? XXX
	if( FAILED(hr) || hr == ND_TIMEOUT || hr == ND_IO_TIMEOUT )
	{
		if (hr == ND_TIMEOUT || hr == ND_IO_TIMEOUT) {
			dat_status = DAT_TIMEOUT_EXPIRED;
			conn_event = IB_CME_TIMEOUT; 
			dapl_log(DAPL_DBG_TYPE_CM+DAPL_DBG_TYPE_CM_WARN,
				"%s() TIMEOUT ERR Remote [%s] (%#x) Local: %s\n",
				__FUNCTION__,
				dapli_IPaddr_str((DAT_SOCK_ADDR*)pSain,NULL),
				ntohs(pSain->sin_port),
				inet_ntoa(LA->sin_addr));
			dapl_log(DAPL_DBG_TYPE_CM+DAPL_DBG_TYPE_CM_WARN,
				"\n%s() ND_Connect() ERR: '%s' @ line #%d\n",
					__FUNCTION__, ND_error_str(hr), __LINE__ );
		}
		else {
			dapl_log(DAPL_DBG_TYPE_ERR, "\nConnect ERR "
			    "Remote %s port %d(%#x) Local: %s\n",
				dapli_IPaddr_str((DAT_SOCK_ADDR*)pSain,NULL),
				ntohs(pSain->sin_port),
				ntohs(pSain->sin_port),
				inet_ntoa(LA->sin_addr));
			dapl_log(DAPL_DBG_TYPE_ERR/* XXX CM*/,
				"\n%s() ND_Connect() ERR: '%s' @ line #%d\n",
					__FUNCTION__, ND_error_str(hr), __LINE__ );
			dat_status = dapl_cvt_ND_to_DAT_status(hr);
			conn_event = IB_CME_DESTINATION_UNREACHABLE; 
		}
	}

	if( dat_status == DAT_SUCCESS )
		return dat_status;
bail:
	hr = pConn->pConnector->lpVtbl->Release(pConn->pConnector);
	pConn->pConnector = NULL;
	ep_ptr->qp_state = DAPL_QP_STATE_ERROR;
	pConn->h_qp = NULL;
	dapls_cm_release( pConn );

	return dat_status;
}


/*
 * NDConnect() request completed, this function called to complete the ND connection
 * sequence and notify DAPL CR machine.
 */

static void dapli_connect_complete( struct dapl_cm_id *pConn, HRESULT Error )
{
	IND2Connector *pConnector;
	DAPL_EP *ep_ptr;
	HRESULT hr;
	DAT_RETURN dat_status = DAT_SUCCESS;
	ib_cm_events_t conn_event;
	SOCKADDR_INET sock_addr;
	struct sockaddr_in *pSain;
	SIZE_T Len, sa_len;

	pConnector = pConn->pConnector;
	ep_ptr = pConn->ep;

	if( FAILED(Error) )
	{
		dapl_log(DAPL_DBG_TYPE_CM,
			"%s() ERR: %s @ line #%d, cleanup pConn %lx.\n",
				__FUNCTION__,ND_error_str(Error),__LINE__, pConn);
		conn_event = dapli_NDerror_2_CME_event(Error);
		goto fail;
	}

	if( ep_ptr->qp_state == DAPL_QP_STATE_ERROR )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() Invalid QP state %d?\n", __FUNCTION__,
				ep_ptr->qp_state);
		goto bail;
	}

	/* get read limits & private data */
	hr = pConnector->lpVtbl->GetReadLimits(
					pConnector, 	// this
					(ULONG*) &pConn->PeerInboundReadLimit,
					(ULONG*) &pConn->PeerOutboundReadLimit );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() GetReadLimits() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	Len = sizeof(pConn->p_data);
	hr = pConnector->lpVtbl->GetPrivateData( pConnector, 	// this
						 (VOID*) pConn->p_data,
						 &(ULONG)Len );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() GetPrivateData() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	/* setup local and remote ports for ep query.
	 * Note: port qual in network order,
	 * GetLocalAddress() & GetRemotePeer() only valid between NDConnect() and
	 * NDCompleteConnect().
	 */
	sa_len = sizeof(sock_addr);
	hr = pConn->pConnector->lpVtbl->GetLocalAddress( pConn->pConnector,
							 (SOCKADDR*)&sock_addr,
							 &(ULONG) sa_len );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() GetLocalAddress() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}
	else
	{
	   	pSain = (struct sockaddr_in*)&sock_addr;
		ep_ptr->param.local_port_qual = PORT_TO_SID(pSain->sin_port);
		dapl_os_memcpy( &pConn->local_addr, &sock_addr, sa_len );
	}

	sa_len = sizeof(sock_addr);
	hr = pConn->pConnector->lpVtbl->GetPeerAddress( pConn->pConnector,
							(SOCKADDR*)&sock_addr,
							&(ULONG) sa_len );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() GetPeerAddress() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}
	else {
	   	pSain = (struct sockaddr_in*)&sock_addr;
		ep_ptr->param.remote_port_qual = PORT_TO_SID(pSain->sin_port);
	}

	/* Complete the connection to the server */
	hr = pConnector->lpVtbl->CompleteConnect( pConnector, &pConn->serial );

	if( hr == ND_PENDING )
	{
		hr = pConnector->lpVtbl->GetOverlappedResult( pConnector,
							      &pConn->serial,
							      TRUE );
	}

	if( FAILED( hr ) || hr == ND_TIMEOUT )
	{
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() CompleteConnect %s line #%d\n",
			__FUNCTION__, ND_error_str(hr), __LINE__);
		dat_status = dapl_cvt_ND_to_DAT_status(hr);
		conn_event = IB_CME_BROKEN; 
		goto fail;
	}
	ep_ptr->qp_state = DAPL_QP_STATE_CONNECTED;
	dapl_os_lock(&pConn->lock);
	pConn->ConnState = DCM_CONNECTED;
	dapl_os_unlock(&pConn->lock);
	pConn->p_len = Len;

	/* setup active-side disconnect notifier */
	pConn->DisconnectNotify.ior_type = IOR_DisconnectNotify;
	pConn->DisconnectNotify.context = (void*) pConn;

	hr = pConnector->lpVtbl->NotifyDisconnect( pConnector,
						   &pConn->DisconnectNotify.Ov );
	if ( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR, 
			"%s() NotifyDisconnect() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}
	conn_event = IB_CME_CONNECTED;

	dapl_log(DAPL_DBG_TYPE_CM,
		"--> ACTIVE Conn --> Remote [%s] refs %d\n",
			dapli_IPaddr_str((DAT_SOCK_ADDR*)pSain,NULL),
			pConn->ref_count);

fail:
	dapl_evd_connection_callback( pConn,
				      conn_event,
				      pConn->p_data,
				      pConn->p_len,
				      ep_ptr );

	if( dat_status == DAT_SUCCESS )
		return;

bail:
	hr = pConn->pConnector->lpVtbl->Release(pConn->pConnector);
	pConn->pConnector = NULL;
	ep_ptr->qp_state = DAPL_QP_STATE_ERROR;
	pConn->h_qp = NULL;
	dapls_cm_release( pConn );
}


/*
 * dapls_ib_disconnect
 *
 * Disconnect an EP
 *
 * Input:
 *	ep_handle,
 *	close_flags
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *
 */
DAT_RETURN
dapls_ib_disconnect(IN DAPL_EP *ep_ptr, IN DAT_CLOSE_FLAGS close_flags)
{
	DAPL_CM_ID	*pConn;
	HRESULT		hr;
	DAPL_CM_STATE	prev_state;
	DWORD		start,elap;
	struct sockaddr_in *pSain;
	IND2Connector	*pConnector;

	if ( DAPL_BAD_HANDLE(ep_ptr, DAPL_MAGIC_EP) ) {
		dapl_dbg_log (DAPL_DBG_TYPE_ERR,
			"--> %s: BAD EP Magic EP=%lx\n", __FUNCTION__,ep_ptr); 
		return DAT_SUCCESS;
	}

	pConn = dapl_get_cm_from_ep(ep_ptr);
	if ( !pConn ) {
		dapl_dbg_log (DAPL_DBG_TYPE_ERR,
			"%s() EP->paran.ep_state %d || pConn.state %s\n",
			__FUNCTION__, ep_ptr->param.ep_state,
			(pConn ? dapli_cm_state_str(pConn->ConnState): "<null>"));
		return DAT_SUCCESS;
	}

	dapl_os_lock(&pConn->lock);

	prev_state = pConn->ConnState;

	if ( prev_state == DCM_DISCONNECTED || prev_state == DCM_DISCONNECTING ) {
		dapl_os_assert(!pConn->pConnector);
		dapl_dbg_log(DAPL_DBG_TYPE_CM,
			"%s(EP %lx) pConn %lx connState '%s' bail\n",
				__FUNCTION__, ep_ptr, pConn,
				dapli_cm_state_str(prev_state));
		// Disconnect Notify fired while conn is in process of disconnecting
		dapl_os_unlock(&pConn->lock);
		return DAT_SUCCESS;
	}
	pConn->ConnState = DCM_DISCONNECTED;
	pConnector = pConn->pConnector;
	pConn->pConnector = NULL;
	dapl_os_unlock(&pConn->lock);

	ep_ptr->qp_state = DAPL_QP_STATE_DISCONNECT_PENDING;

	if (pConn->sp)
		pSain = (struct sockaddr_in *)&pConn->local_addr;
	else
		pSain = (struct sockaddr_in *)&pConn->remote_addr;

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
			"DISCONNECT[%s] EP %lx pConn %lx port %d close-%s\n  "
			"connState '%s'\n",
			(pConn->sp ? "svr":"cli"), pConn, ep_ptr,
			SID_TO_PORT(pSain->sin_port),
			(close_flags == DAT_CLOSE_ABRUPT_FLAG ? "Abrupt":"Graceful"),
			dapli_cm_state_str(pConn->ConnState));

	dapl_os_assert( pConn->ep == ep_ptr );
	dapl_os_assert( pConn->ConnState != DCM_FREE );

	if ( pConnector ) {
		hr = pConnector->lpVtbl->CancelOverlappedRequests( pConnector );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s(EP %lx) pConn(%lx)->pConnector(%lx)->"
				"CancelOverlappedRequests %s @ line #%d\n",
 					__FUNCTION__,ep_ptr,pConn,pConnector,
					ND_error_str(hr), __LINE__);
		}

		if ( prev_state == DCM_CONNECTED ) {
			pConn->pConnector_dc = pConnector; // disconnect callback.
			pConn->OverLap.ior_type = IOR_Disconnect;
			pConn->OverLap.context = (void*) pConn;

			hr = pConnector->lpVtbl->Disconnect( pConnector,
						    	     &pConn->OverLap.Ov );
			if ( FAILED(hr) ) {
				dapl_log(DAPL_DBG_TYPE_ERR,
				    "%s() ERR Disconnect(pConn %lx) %s @ line#%d\n",
				       __FUNCTION__,pConn,ND_error_str(hr),__LINE__);
			}
		}
	}
	return DAT_SUCCESS;
}


static void dapli_disconnect_complete( struct dapl_cm_id *pConn, HRESULT Error )
{
	int		ret;
	HRESULT		hr;
	IND2Connector	*pConnector;
	DAPL_EP		*ep_ptr;

	ep_ptr = pConn->ep;
	pConnector = pConn->pConnector_dc;

	if ( pConn->ConnState != DCM_DISCONNECTED ) {
		dapl_log(DAPL_DBG_TYPE_CM, "%s() pConn %lx %s !DISCONNECTED\n",
		    	__FUNCTION__, pConn, dapli_cm_state_str(pConn->ConnState));
		pConnector = NULL;
	}

	if( FAILED(Error) ) {
		dapl_log(DAPL_DBG_TYPE_CM,
			"%s() ERR: %s @ line #%d, cleanup pConn %lx.\n",
				__FUNCTION__,ND_error_str(Error),__LINE__, pConn);
	}

	dapl_os_lock(&ep_ptr->header.lock);
	ep_ptr->qp_state = DAPL_QP_STATE_FREE;
	dapl_os_unlock(&ep_ptr->header.lock);

	if ( pConn->h_qp )
		pConn->h_qp->pConn = NULL;

	dapl_os_assert(!pConn->pListen);

	if ( pConnector ) {
		hr = pConnector->lpVtbl->Release(pConnector);
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Connector->release() ERR: %s @ line#%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
		}
	}

	if ( !pConnector ) {
		/* DAPL callback done from DisconnectNotify callback */
		return;
	}

	if ( pConn->sp ) {
		if ( pConn->sp->header.magic == DAPL_MAGIC_PSP ||
			     pConn->sp->header.magic == DAPL_MAGIC_RSP ) {
			dapl_dbg_log(DAPL_DBG_TYPE_CM,
			   "  cr_callback(ib_disconnect) EP %lx, pConn %lx sp %lx\n",
	     			__FUNCTION__, ep_ptr, pConn, pConn->sp);
			dapls_cr_callback( pConn, IB_CME_DISCONNECTED, NULL, 0,
						pConn->sp );
		}
	}
	else {
		if ( !DAPL_BAD_HANDLE(pConn->ep, DAPL_MAGIC_EP) ) {
			dapl_dbg_log(DAPL_DBG_TYPE_CM,
				"  evd_callback(disconnect) EP %lx, pConn %lx\n",
		     			__FUNCTION__, ep_ptr, pConn, pConn->sp);
			dapl_evd_connection_callback( pConn, IB_CME_DISCONNECTED,
							NULL, 0, ep_ptr );
		}
	}

	{
		struct sockaddr_in *pSain = (struct sockaddr_in *)&pConn->local_addr;

		dapl_dbg_log(DAPL_DBG_TYPE_CM,
			"%s() Exit: EP %lx, pConn %lx port %d\n",
				__FUNCTION__, ep_ptr, pConn,
				SID_TO_PORT(pSain->sin_port));
	}
}


static void dapli_release_ND_resources( DAPL_CM_ID *pConn )
{
	HRESULT hr;
	
	if ( pConn == NULL )
		return;

	// QP resource release properly handled in dapls_ib_qp_free().

	if ( pConn->pConnector ) {
		hr = pConn->pConnector->lpVtbl->Release(pConn->pConnector);
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Connector->release() ERR: %s @ line#%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
		}
		pConn->pConnector = NULL;
	}

	// CQ resource release properly handled in dapls_ib_cq_free().

	if ( pConn->serial.hEvent != INVALID_HANDLE_VALUE ) {
		CloseHandle( pConn->serial.hEvent );
		pConn->serial.hEvent = INVALID_HANDLE_VALUE;
	}

	if ( pConn->OverLap.Ov.hEvent != INVALID_HANDLE_VALUE ) {
		CloseHandle( pConn->OverLap.Ov.hEvent );
		pConn->OverLap.Ov.hEvent = INVALID_HANDLE_VALUE;
	}
	if ( pConn->Event != INVALID_HANDLE_VALUE ) {
		CloseHandle( pConn->Event );
		pConn->Event = INVALID_HANDLE_VALUE;
	}
}


/*
 * dapls_ib_disconnect_clean
 *
 * Clean up outstanding connection data. This routine is invoked
 * after the final disconnect callback has occurred. Only on the
 * ACTIVE side of a connection.
 *
 * Input:
 *	ep_ptr		DAPL_EP
 *	active		Indicates active side of connection
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	void
 *
 */
void
dapls_ib_disconnect_clean(IN DAPL_EP * ep_ptr,
			  IN DAT_BOOLEAN active,
			  IN const ib_cm_events_t cm_event)
{
	struct dapl_cm_id *pConn;
	struct sockaddr_in *pSain;
	
	dapl_os_assert( ep_ptr );
	pConn = dapl_get_cm_from_ep(ep_ptr);
	if ( !pConn ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,"%s() pConn NULL? ep %lx [%s] '%s'\n",
			__FUNCTION__, ep_ptr, (active ? "Active":"Passive"),
			dapli_ib_cm_event_str(cm_event));
		// XXX DebugBreak();
		return;
	}
	dapl_os_assert( pConn->ep == ep_ptr );

	if (pConn->sp)
		pSain = (struct sockaddr_in *)&pConn->local_addr;
	else
		pSain = (struct sockaddr_in *)&pConn->remote_addr;

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		"%s() pConn %lx port %d '%s' EP %lx [%s] '%s'\n",
			__FUNCTION__, pConn, SID_TO_PORT(pSain->sin_port),
			dapli_cm_state_str(pConn->ConnState),
			ep_ptr, (active ? "Active":"Passive"),
			dapli_ib_cm_event_str(cm_event));

	dapl_os_lock(&pConn->lock);

	if ( pConn->ConnState != DCM_DISCONNECTED ) {
		dapl_os_unlock(&pConn->lock);
		dapls_ib_disconnect( ep_ptr, DAT_CLOSE_GRACEFUL_FLAG );
	}
	else
		dapl_os_unlock(&pConn->lock);
}



/*
 * Received a Connection Request - build the passive connection side (cm_id).
 */

static void dapli_recv_conn_request( dp_ib_cm_handle_t pListenConn )
{
	dp_ib_cm_handle_t	pConn_REQ;
	struct dapl_hca		*hca_ptr;
	IND2Adapter		*pAdapter;
	SIZE_T			InboundReadLimit, OutboundReadLimit;
	HRESULT			hr;
	DAPL_SP			*sp_ptr;
	SOCKADDR_INET		sock_addr;
	struct sockaddr_in	*pSA;
	ULONG			sa_len;

	dapl_os_assert(pListenConn->hca);
	hca_ptr = pListenConn->hca;
	pAdapter = pListenConn->pAdapter;
	dapl_os_assert( pAdapter == hca_ptr->ib_trans.ib_ctx );

	sp_ptr = pListenConn->sp;
	if (sp_ptr == NULL) {
		dapl_log(DAPL_DBG_TYPE_ERR, " %s() Bad sp handle?\n");
		return;
	}

	/*
	 * The context pointer could have been cleaned up in a racing
	 * CM callback, check to see if we should just exit here
	 */
	if (sp_ptr->header.magic == DAPL_MAGIC_INVALID)
	{
		dapl_log (DAPL_DBG_TYPE_ERR/*XXXCM*/,
			"%s: BAD-Magic in SP %lx, racing CM callback?\n",
			__FUNCTION__, sp_ptr );
		return;
	}

	dapl_os_assert ( sp_ptr->header.magic == DAPL_MAGIC_PSP || 
			sp_ptr->header.magic == DAPL_MAGIC_RSP );

	/* alloc a new cm_id struct to describe the connection request (REQ) */
	dapl_os_assert( pListenConn->OvFileHandle ==
			hca_ptr->ib_trans.AdapterOvFileHandle );
	pConn_REQ = dapls_alloc_cm_id( hca_ptr, pListenConn->OvFileHandle );
	if ( pConn_REQ == NULL ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() ERR: unable to allocate DAPL_CM_ID?\n",__FUNCTION__);
		// XXX TODO - REJECT connection request.
		return;
	}

        /*
         * Save the cm_srvc_handle to avoid the race condition between
         * the return of the ib_cm_listen and the notification of a conn req
         */
        if (sp_ptr->cm_srvc_handle != pListenConn)
        {
		// XXX TYPE_ERR
            dapl_dbg_log (DAPL_DBG_TYPE_ERR | DAPL_DBG_TYPE_CM | DAPL_DBG_TYPE_CALLBACK, 
                               "--> DiCRqcb: cm_service_handle has changed?\n"); 
            sp_ptr->cm_srvc_handle = pListenConn;
        }

	/* use items from Listen()ing CM_ID, including the pConnector which fired */
	pConn_REQ->sp = sp_ptr;
	pConn_REQ->pAdapter = pAdapter;
	pConn_REQ->pConnector = pListenConn->pConnector;
	pConn_REQ->ConnState = DCM_ACCEPTING;
	/* alloc new ND Connector for Listen and future conenction requests(REQ).
	 * Re-issue GetConnRequest for more connections.
	 */
	hr = pAdapter->lpVtbl->CreateConnector( pAdapter,
						&IID_IND2Connector,
						pListenConn->OvFileHandle,
						&pListenConn->pConnector );
	if( hr == ND_PENDING ) {
		hr = pListenConn->pConnector->lpVtbl->GetOverlappedResult(
							pListenConn->pConnector,
							&pListenConn->serial,
							TRUE );
	}
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() CreateConnector() ERR: %s @ line#%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		goto bail;
	}

	/* Post request for next connection request */
	pListenConn->OverLap.ior_type = IOR_GetConnReq;
	pListenConn->OverLap.context = pListenConn;
	dapl_os_atomic_inc(&pListenConn->GetConnReq);

	hr = pListenConn->pListen->lpVtbl->GetConnectionRequest(
						pListenConn->pListen,
						(IUnknown *)pListenConn->pConnector,
						&pListenConn->OverLap.Ov );
	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&pListenConn->GetConnReq);
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() reissue GetConnectionRequest() "
				"ERR: %s @ line #%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	dapl_log(DAPL_DBG_TYPE_CM, "%s() POSTED new GetConnectionRequest"
		"(pConnector %lx) on pListenConn %lx\n",
			__FUNCTION__,pListenConn->pConnector,pListenConn);

	/* New Connection setup.
	 * Get Connection information: read limits + Private Data and size.
	 */

	/* get read limits & private data */
	hr = pConn_REQ->pConnector->lpVtbl->GetReadLimits(
					pConn_REQ->pConnector,
					(ULONG*) &pConn_REQ->PeerInboundReadLimit,
					(ULONG*) &pConn_REQ->PeerOutboundReadLimit );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() GetReadLimits() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	pConn_REQ->p_len = sizeof(pConn_REQ->p_data);
	hr = pConn_REQ->pConnector->lpVtbl->GetPrivateData(
						pConn_REQ->pConnector,
						(VOID*) pConn_REQ->p_data,
						&(ULONG)pConn_REQ->p_len );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() GetPrivateData() ERR: %s @ line#%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	/* setup local and remote ports for ep query.
	 * Note: port qual in network order.
	 * GetLocalAddress() & GetPeerAddress() only valid before Accept().
	 */

	sa_len = (ULONG) sizeof(sock_addr);
	hr = pConn_REQ->pConnector->lpVtbl->GetLocalAddress( pConn_REQ->pConnector,
							     (SOCKADDR*)&sock_addr,
							     &sa_len );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() GetLocalAddress() ERR: %s @ line #%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
		pSA = (struct sockaddr_in *)&pConn_REQ->local_addr;
		pSA ->sin_addr.S_un.S_addr = 0;
	}
	else
		dapl_os_memcpy( &pConn_REQ->local_addr, &sock_addr, sa_len );

	dapl_os_assert(sock_addr.si_family == AF_INET);
	dapl_os_assert(pConn_REQ->local_addr.sin6_family == AF_INET);

	sa_len = (ULONG) sizeof(sock_addr);
	hr = pConn_REQ->pConnector->lpVtbl->GetPeerAddress( pConn_REQ->pConnector,
							  (SOCKADDR*)&sock_addr,
							  &sa_len );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() GetPeerAddress() ERR: %s @ line #%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
		pSA = (struct sockaddr_in *)&pConn_REQ->remote_addr;
		pSA ->sin_addr.S_un.S_addr = 0;
		// XXX FIX this - return error.
	}
	else
		dapl_os_memcpy( &pConn_REQ->remote_addr, &sock_addr, sa_len );

	dapl_os_assert(pConn_REQ->remote_addr.sin6_family == AF_INET);
	dapl_os_assert(sock_addr.si_family == AF_INET);

	/* success */
	/* see ../common/dapl_cr_callback.c */
	dapl_log(DAPL_DBG_TYPE_CM,
	     "-->Passive Conn upcall dapls_cr_callback(pConn %lx)\n",pConn_REQ);

	dapls_cr_callback( pConn_REQ,
			   IB_CME_CONNECTION_REQUEST_PENDING,
			   (void* __ptr64) pConn_REQ->p_data,
			   pConn_REQ->p_len,
			   (void* __ptr64) pConn_REQ->sp );

	dapl_log(DAPL_DBG_TYPE_CM, "%s(pConn %lx) CONN REQ PENDING local [%s]\n",
		__FUNCTION__,pConn_REQ,
		dapli_IPaddr_str((DAT_SOCK_ADDR*) &pConn_REQ->local_addr,NULL) );

	return;

bail:
	dapl_os_lock_destroy(&pConn_REQ->lock);
	dapls_cm_release(pConn_REQ);
}



extern int _noise; // XXX see device instance close in device.c

void IOCompletion_cb(	__in	DWORD		ErrorCode,
			__in	DWORD 		BytesXfered,
			__inout	LPOVERLAPPED	pOverlap )
{
	dp_ib_cm_handle_t pConn, cm_ptr;
	DAPL_OVERLAPPED *pDOV;
	DAPL_ND_CQ *pCq;
	DAPL_CM_STATE prevState;
	HRESULT hr;
	DAPL_EP *ep_ptr;
	void *context;
	IND2Connector *pConnector;

	dapl_os_assert( pOverlap );
	pDOV = (DAPL_OVERLAPPED*)pOverlap;
#if DBG
	if ( _noise ) {
		DAPL_ND_IOR iType = IOR_MAX;
		void *context=NULL;

		if (pDOV) {
			context = pDOV->context;
			iType = pDOV->ior_type;
		}
		fprintf(stdout, "[%d] _noise %s() %s ctx %lx err %x\n", gDAPL_rank,
			__FUNCTION__, dapli_IOR_str(iType), context, ErrorCode);
		fflush(stdout);
	}
#endif
	if ( ErrorCode ) {
		switch (ErrorCode)
		{
		  case ND_CONNECTION_REFUSED:
				break;
		  case ND_CANCELED:
			switch( pDOV->ior_type ) {
			  case IOR_CQNotify:
			  case IOR_GetConnReq:
			  case IOR_CompleteConnect:
			  case IOR_Accept:
			  case IOR_Disconnect:
			  case IOR_DisconnectNotify:
				break;
			  default:
				fprintf(stdout,"%s() ND_CANCELED '%s'\n",
					__FUNCTION__,dapli_IOR_str(pDOV->ior_type));
				fflush(stdout);
				return;
			}
			break;

		  case ND_TIMEOUT: 
			fprintf(stdout,"%s() TIMEOUT %s ctx %lx\n", __FUNCTION__,
				dapli_IOR_str(pDOV->ior_type),pDOV->context);
			fflush(stdout);
			break;

		  case ND_DISCONNECTED: 
			//return;

		  default:
			fprintf(stdout,"%s() ND_error %#x\n",__FUNCTION__,ErrorCode);
			dapl_dbg_log(DAPL_DBG_TYPE_ERR,
				"%s() %s ND_error 0x%x\n",
					__FUNCTION__, dapli_IOR_str(pDOV->ior_type),
					ErrorCode);
			fflush(stdout);
			return;
		}
	}

	switch( pDOV->ior_type )
	{
	  case IOR_CQNotify:
		pCq = CONTAINING_RECORD(pDOV,struct dapl_nd_cq,OverLap);
		if (ErrorCode) {
			if (ErrorCode == ND_CANCELED) {
				if ( pCq->Event == NULL )
					return;
				if ( dapl_os_atomic_dec(&pCq->armed) <= 0 ) {
					if ( SetEvent(pCq->Event) == 0 ) {
						dapl_dbg_log(DAPL_DBG_TYPE_ERR,
					    		"%s() IOR_CQNotify "
							"ND_CANCELED SetEvent() "
							"err %#x\n",
							__FUNCTION__,GetLastError());
					}
				}
				return;
			}
			hr = ErrorCode;
		}
		else
			hr = pCq->ND_CQ->lpVtbl->GetOverlappedResult( pCq->ND_CQ,
								      pOverlap,
								      FALSE );
		dapl_os_atomic_dec(&pCq->armed);

		if ( FAILED(hr) ) {
			dapl_log( DAPL_DBG_TYPE_ERR,
			   "%s() IOR_CQNotify: OverLap.Status %s (%x)\n",
				__FUNCTION__,ND_error_str(hr),hr);
		}
		else {
			DAPL_EVD *evd_ptr = pCq->context;
			IND2Adapter *pAdapter;

			dapl_os_assert(evd_ptr != DAT_HANDLE_NULL);
			pAdapter = evd_ptr->header.owner_ia->hca_ptr->ib_hca_handle;
			dapl_os_assert(pAdapter);
			if ( evd_ptr->evd_enabled )
				dapl_evd_dto_callback(pAdapter, pCq, evd_ptr);
		}
		break;

	  case IOR_GetConnReq:

		pConn = CONTAINING_RECORD( pDOV, struct dapl_cm_id, OverLap );
		if (ErrorCode) {
			if (ErrorCode == ND_CANCELED) {
				if (pConn->Event == NULL)
					return;
				if ( dapl_os_atomic_dec(&pConn->GetConnReq) <= 0 ) {
					if ( SetEvent(pConn->Event) == 0 ) {
						dapl_dbg_log(DAPL_DBG_TYPE_ERR,
					    		"%s() IOR_GetConnReq "
							"ND_CANCELED SetEvent() "
							"err %#x\n",
							__FUNCTION__,GetLastError());
					}
				}
				return;
			}
			hr = ErrorCode;
		}
		else
			hr = pConn->pConnector->lpVtbl->GetOverlappedResult(
								pConn->pConnector,
								pOverlap,
								FALSE );
		dapl_os_atomic_dec(&pConn->GetConnReq);

		if ( FAILED(hr) || ErrorCode ) {
			if (hr != ND_CANCELED)
				dapl_log( DAPL_DBG_TYPE_ERR,
					"%s(pConn %lx) IOR_GetConnReq: OverLap."
					"Status %s\n",
						__FUNCTION__,pConn,ND_error_str(hr));
		}
 		else 
			dapli_recv_conn_request( (void*) pConn );
		break;

	  case IOR_CompleteConnect:

		pConn = CONTAINING_RECORD( pDOV, struct dapl_cm_id, OverLap );
		if (ErrorCode) {
			/* XXX consider the error cases */
			if (ErrorCode == ND_CANCELED) {
				return;
			}
			hr = ErrorCode;
		}
		else
			hr = pConn->pConnector->lpVtbl->GetOverlappedResult(
								pConn->pConnector,
								pOverlap,
								FALSE );
		if ( FAILED(hr) ) {
			if (hr != ND_CANCELED)
				dapl_log( DAPL_DBG_TYPE_CALLBACK,
					"%s(pConn %lx) IOR_CompleteConnect: OverLap."
					"Status %s\n",
						__FUNCTION__,pConn,ND_error_str(hr));
			else
				return;
		}
		dapli_connect_complete( pConn, hr );
		break;

	  case IOR_GetOverLapResult:
		break;

	  case IOR_CQArm:
		pDOV->ior_type = IOR_CQNotify;
		break;

	  case IOR_Accept:
		pConn = CONTAINING_RECORD( pDOV, struct dapl_cm_id, OverLap );
		if (ErrorCode) {
			/* XXX consider the error cases */
			if (ErrorCode == ND_CANCELED) {
				return;
			}
			hr = ErrorCode;
		}
		else
			hr = pConn->pConnector->lpVtbl->GetOverlappedResult(
								pConn->pConnector,
								pOverlap,
								FALSE );
		if ( FAILED(hr) ) {
			if (hr != ND_CANCELED)
				dapl_log( DAPL_DBG_TYPE_ERR/*XXX CALLBACK + _CM*/,
					"%s(pConn %lx) IOR_Accept: OverLap."
					"Status %s\n",
						__FUNCTION__,pConn,ND_error_str(hr));
			else
				return;
		}
		dapli_accept_complete( pConn, hr );
		break;

	  case IOR_Connect:
		break;

	 case IOR_RecvQ:
		break;

	 case IOR_SendQ:
		break;

	 case IOR_Disconnect:
		/* Async disconnect */
		pConn = (dp_ib_cm_handle_t)pDOV->context;
		if ( !pConn )
			return;

		if (ErrorCode)
			hr = ErrorCode;
		else if (pConn->pConnector)
			hr = pConn->pConnector->lpVtbl->GetOverlappedResult(
							pConn->pConnector,
							pOverlap,
							FALSE );
		
		if (hr != ND_SUCCESS && hr != ND_CANCELED)
			dapl_dbg_log( DAPL_DBG_TYPE_ERR,
				"\n%s() IOR_Disconnect: status %s pConn %lx\n",
					__FUNCTION__,ND_error_str(hr),pConn);

		if ( pConn->ConnState == DCM_FREE ) {
			if ( ErrorCode ) {
				fprintf(stdout, "IOR_Disconnect Err %x pConn %lx\n",
					ErrorCode, pConn);
				fflush(stdout);
			}
		}
		else
			dapli_disconnect_complete( pConn, hr );
		break;

	 case IOR_DisconnectNotify:
		/* Other side has disconnected - local cleanup */
		if ( ErrorCode && (ErrorCode != ND_CANCELED) ) {
			dapl_log( DAPL_DBG_TYPE_ERR,
				"IOR_DisconnectNotify Error %#lx\n",ErrorCode);
			return;
		}
		pConn = (dp_ib_cm_handle_t)pDOV->context;
		if ( !pConn )
			return;

		dapl_os_lock(&pConn->lock);
		prevState = pConn->ConnState;
		pConnector = pConn->pConnector;

		if ( prevState != DCM_CONNECTED || !pConnector ) {
			dapl_log( DAPL_DBG_TYPE_CM,
			"IOR_DisconnectNotify %s pConn %lx prevState '%s' "
				"pConnector %lx, bail\n",
				(pConn->sp ? "Server":"Client"), pConn,
				dapli_cm_state_str(prevState),pConnector);
			dapl_os_unlock(&pConn->lock);
        		return;
		}
		pConn->ConnState = DCM_DISCONNECTING;
		pConn->pConnector = NULL;
		pConn->DisconnectNotify.ior_type = IOR_MAX; 

		hr = pConnector->lpVtbl->GetOverlappedResult( pConnector,
							      pOverlap,
							      FALSE );
		dapl_os_unlock(&pConn->lock);

		if ( FAILED(hr) ) {
			if (hr != ND_CANCELED && hr != ND_DISCONNECTED)
				dapl_log( DAPL_DBG_TYPE_ERR,
					"%s() IOR_DisconnectNotify (pConn %lx) "
					"OverLap.Status(%x) %s\n",
					__FUNCTION__, pConn, hr, ND_error_str(hr));
			return;
		}
		ep_ptr = pConn->ep;

		hr = pConnector->lpVtbl->CancelOverlappedRequests( pConnector );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
			    "%s(EP %lx) pConn(%lx)->pConnector(%lx)->"
			    "CancelOverlappedRequests %s @ line #%d\n",
 				__FUNCTION__,ep_ptr,pConn,pConnector,
				ND_error_str(hr), __LINE__);
		}
		hr = pConnector->lpVtbl->Release(pConnector);
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Connector->release %s @ line#%d\n",
					__FUNCTION__,ND_error_str(hr),__LINE__);
		}
#if DBG
	{ struct sockaddr_in *pSain;
		if (pConn->sp)
			pSain = (struct sockaddr_in *)&pConn->local_addr;
		else
			pSain = (struct sockaddr_in *)&pConn->remote_addr;

		dapl_dbg_log( DAPL_DBG_TYPE_CM, "IOR_DisconnectNotify EP(%lx) "
			"%s-pConn %lx port %d prevState '%s'\n",
				ep_ptr, (pConn->sp ? "Svr":"Cli"), pConn,
				SID_TO_PORT(pSain->sin_port),
				dapli_cm_state_str(prevState));
	}
#endif
		if ( !ep_ptr )
			return;

		dapl_os_lock (&ep_ptr->header.lock);
		if ( DAPL_BAD_HANDLE(ep_ptr, DAPL_MAGIC_EP) ) {
        		dapl_dbg_log (DAPL_DBG_TYPE_EP, 
                      		"--> %s(): BAD Magic? EP(%lx)->header.magic %lx\n",
                      		__FUNCTION__, ep_ptr, ep_ptr->header.magic);
			dapl_os_unlock (&ep_ptr->header.lock);
			return;	// already disconnected.
		}
		if ( ep_ptr->param.ep_state == DAT_EP_STATE_DISCONNECTED ) {
        		dapl_dbg_log (DAPL_DBG_TYPE_EP, 
                      		"--> %s()IOR_DisconnectNotify EP(%lx)->QP %lx "
				"already Disconnected pConn(%lx '%s')\n",
                      		__FUNCTION__, ep_ptr, ep_ptr->qp_handle,pConn,
				dapli_cm_state_str(pConn->ConnState));
			dapl_os_unlock (&ep_ptr->header.lock);
        		return;
    		}
		ep_ptr->param.ep_state = DAT_EP_STATE_DISCONNECT_PENDING;
    		dapl_os_unlock (&ep_ptr->header.lock);

		ep_ptr->qp_state = DAPL_QP_STATE_DISCONNECT_PENDING;

		if ( pConn->sp ) {
			/* passive side */
			if ( !ep_ptr->cr_ptr )
				return;
			if (pConn->sp != (((DAPL_CR *)ep_ptr->cr_ptr)->sp_ptr) )
				return;
			if ( pConn->sp->header.magic == DAPL_MAGIC_PSP ||
			     pConn->sp->header.magic == DAPL_MAGIC_RSP ) {
				dapl_dbg_log( DAPL_DBG_TYPE_CM,
					"%s() IOR_DisconnectNotify cr_callback "
					"pConn %lx SP %lx @ line #%d\n",
					__FUNCTION__,pConn,pConn->sp,__LINE__);
				dapls_cr_callback( pConn, IB_CME_DISCONNECTED, 0, 0, 
						(void *) pConn->sp);
			}
		}
		else {
			dapl_dbg_log( DAPL_DBG_TYPE_CM,
				"%s() IOR_DisconnectNotify evd_cb pConn %lx "
				"EP %lx @ line #%d\n",
					__FUNCTION__,pConn,pConn->ep,__LINE__);
			dapl_evd_connection_callback( pConn, IB_CME_DISCONNECTED,
							0, 0, (void *) pConn->ep);
		}
		break;

	 case IOR_MemoryRegion:
		break;

	 default:
		dapl_dbg_log( DAPL_DBG_TYPE_ERR,
			" %s() unknown ND IO Request type %d?\n",
				__FUNCTION__, pDOV->ior_type );
		break;
	}
}


/*
 * dapl_ib_setup_conn_listener
 *
 * Request the Connection Manager (CM) set up a connection listener.
 *
 * Input:
 *	ia_ptr
 *	ServiceID	Service port
 *	sp_ptr		DAPL Service Provider
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *	DAT_INTERNAL_ERROR
 *	DAT_CONN_QUAL_UNAVAILBLE
 *	DAT_CONN_QUAL_IN_USE
 *
 */
DAT_RETURN
dapls_ib_setup_conn_listener( IN DAPL_IA	*ia_ptr,
			      IN DAT_UINT64	ServiceID,
			      IN DAPL_SP	*sp_ptr )
{
	DAT_RETURN dat_status = DAT_SUCCESS;
	HRESULT hr;
	ib_cm_srvc_handle_t pConn;
	DAT_SOCK_ADDR6 addr;	/* local binding address */
	struct sockaddr_in *pSain;
	IND2Adapter *pAdapter = ia_ptr->hca_ptr->ib_hca_handle;
	ib_hca_transport_t *pND_transport = &ia_ptr->hca_ptr->ib_trans;
	INT status;
	ULONG Len;

	dapl_os_assert(ia_ptr->hca_ptr);

	/* Allocate CM ID */
	pConn = dapls_alloc_cm_id( ia_ptr->hca_ptr,
				    ia_ptr->hca_ptr->ib_trans.AdapterOvFileHandle );
	if ( pConn == NULL )
		return DAT_INSUFFICIENT_RESOURCES;

	/* open identifies the local device; per DAT specification.
	 * Get family and address then set port to consumer's ServiceID.
	 */
	dapl_os_memcpy( &addr, &ia_ptr->hca_ptr->hca_address, sizeof(addr));
	pSain = (struct sockaddr_in *)&addr;
	pSain->sin_family = AF_INET;
	pSain->sin_port = SID_TO_PORT(ServiceID);

	dapl_log(DAPL_DBG_TYPE_CM+DAPL_DBG_TYPE_CM_WARN,
		"%s() SID %I64x  port %d [%#x] sp %lx pConn %lx\n", __FUNCTION__,
			ServiceID, ntohs(pSain->sin_port), ntohs(pSain->sin_port),
 			sp_ptr, pConn);

	sp_ptr->cm_srvc_handle = pConn;
	pConn->sp = sp_ptr;
	dapl_os_assert(pConn->hca == ia_ptr->hca_ptr);
	pConn->pAdapter = pAdapter;

	dapl_os_assert(pConn->OvFileHandle == pConn->hca->ib_trans.AdapterOvFileHandle);

	hr = pAdapter->lpVtbl->CreateListener(
					pAdapter,
					&IID_IND2Listener,
					pConn->OvFileHandle,
					&pConn->pListen );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,"%s() CreateListener %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
		goto bail;
	}

	hr = pConn->pListen->lpVtbl->Bind( pConn->pListen,
					   (struct sockaddr*)pSain,
					   (ULONG) sizeof(*pSain) );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,"%s() Bind() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
		goto bail1;
	}

	{
		int tries=2;

		do {
			// 0 == infinite back log.
			hr = pConn->pListen->lpVtbl->Listen( pConn->pListen, 0 );
			if ( FAILED(hr) ) {
				dapl_log(DAPL_DBG_TYPE_CM,
				  "%s() Listen() ERR: %s @ line #%d tries %d\n",
				  __FUNCTION__,ND_error_str(hr),__LINE__,tries);
				dapl_os_sleep_usec(10000);
			}
		} while( --tries > 0 && FAILED(hr) );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
			  "%s() Listen() ERR: %s @ line #%d pListen %lx\n",
			  __FUNCTION__,ND_error_str(hr),__LINE__,pConn->pListen);
			dat_status = DAT_INSUFFICIENT_RESOURCES;
			goto bail1;
		}
	}

	pConn->pConnector = NULL;
	hr = pAdapter->lpVtbl->CreateConnector( pAdapter,
						&IID_IND2Connector,
						pConn->OvFileHandle,
 						&pConn->pConnector );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() CreateConnector() ERR: 0x%08x @ line#%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		goto bail1;
	}

	pConn->OverLap.ior_type = IOR_GetConnReq;
	pConn->OverLap.context = pConn;
	dapl_os_atomic_inc(&pConn->GetConnReq);

	hr = pConn->pListen->lpVtbl->GetConnectionRequest(
						pConn->pListen,
						(IUnknown*) pConn->pConnector,
						&pConn->OverLap.Ov );
	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&pConn->GetConnReq);
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() GetConnectionRequest() ERR: 0x%08x @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
		goto bail1;
	}

	dapl_log(DAPL_DBG_TYPE_CM+DAPL_DBG_TYPE_CM_WARN,
		"%s(pConn(%lx) ND_Adapter '%s' Bound to [%s] Listening on port %04d "
		"(%#04x)\n", __FUNCTION__, pConn, pND_transport->dev_name,
			inet_ntoa(pSain->sin_addr),
			ntohs(pSain->sin_port),
			ntohs(pSain->sin_port) );

	return DAT_SUCCESS;

bail1:
	if ( pConn->pConnector ) {
		pConn->pConnector->lpVtbl->Release(pConn->pConnector);
		pConn->pConnector = NULL;
	}
	pConn->pListen->lpVtbl->Release(pConn->pListen);
	pConn->pListen = NULL;
bail:
	dapls_cm_release(pConn);	// release alloc ref, dealloc mem

	return dat_status;
}


static void dapli_remove_ND_listen( DAPL_CM_ID *pConn )
{
	HRESULT hr;

	dapl_dbg_log(DAPL_DBG_TYPE_CM, "%s(pConn %lx)\n", __FUNCTION__,pConn);

	if( pConn->pListen ) {
		hr = pConn->pListen->lpVtbl->CancelOverlappedRequests(pConn->pListen);
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR, 
			    "%s(pConn %lx) CancelOverlappedReq %s @ line #%d\n",
				__FUNCTION__,pConn,ND_error_str(hr),__LINE__);
		}

		hr = pConn->pListen->lpVtbl->Release(pConn->pListen);
		dapl_os_assert( hr != ND_PENDING );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR, 
			    "%s(pConn %lx) Release ERR: %s @ line #%d\n",
				__FUNCTION__,pConn,ND_error_str(hr),__LINE__);
		}
		pConn->pListen = NULL;
	}

	if ( pConn->pConnector ) {
		IND2Connector *pc = pConn->pConnector;
		int tries=10;
		DWORD result;

		pConn->pConnector = NULL;
		pc->lpVtbl->Release(pc);

		while((tries-- > 0) && (dapl_os_atomic_read(&pConn->GetConnReq) > 0))
		{
			result = WaitForSingleObject( pConn->Event, 250 );
			if ( result == WAIT_OBJECT_0 )
				continue;
			if ( result != WAIT_TIMEOUT ) {
				dapl_log(DAPL_DBG_TYPE_ERR,
					"%s() WaitForSingleObject()"
					" ERR %#x @ line #%d\n", __FUNCTION__,
						GetLastError(), __LINE__);
			}
		}
		if ((tries == 0) || (dapl_os_atomic_read(&pConn->GetConnReq) != 0))
  			printf("%s() *** pConn(%lx)->GetConnReq %ld tries %d\n",
				__FUNCTION__,pConn,
				dapl_os_atomic_read(&pConn->GetConnReq),(25-tries));
	}
}


/*
 * dapl_ib_remove_conn_listener
 *
 * Have the CM remove a connection listener.
 *
 * Input:
 *	ia_handle		IA handle
 *	ServiceID		IB Channel Service ID
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_STATE
 *
 */
DAT_RETURN
dapls_ib_remove_conn_listener(IN DAPL_IA *ia_ptr, IN DAPL_SP *sp_ptr)
{
	ib_cm_srvc_handle_t pConn;

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		"%s( ia_ptr %lx sp_ptr(%lx)->cm_srvc_handle %lx)\n",
			__FUNCTION__, ia_ptr, sp_ptr, sp_ptr->cm_srvc_handle);

	dapl_os_assert ( sp_ptr->header.magic == DAPL_MAGIC_PSP ||
					sp_ptr->header.magic == DAPL_MAGIC_RSP );

	if ( sp_ptr->cm_srvc_handle ) {
		pConn = sp_ptr->cm_srvc_handle;
		sp_ptr->cm_srvc_handle = NULL;
		if ( pConn && pConn->pListen ) {
			dapli_remove_ND_listen( pConn );
			dapls_cm_release(pConn);
		}
	}

	return DAT_SUCCESS;
}


struct queued_rx_op {
	struct dapl_llist_entry	Entry;
	DAPL_EP			*ep_ptr;
	DAPL_COOKIE		cookie;
	DAT_COUNT		segments;
	DAT_LMR_TRIPLET		local_iov[1]; // can be more if segments > 1
};

void dapli_post_queued_recvs( DAPL_ND_QP *h_qp )
{
	DAT_RETURN status;
	struct queued_rx_op *op;
	int posted=0;

	dapl_dbg_log(DAPL_DBG_TYPE_ERR/*CM*/,
		"%s(h_qp %lx) posting Queue Rx h_qp %lx\n", __FUNCTION__, h_qp);

	dapl_os_assert( h_qp );

	while ( !dapl_llist_is_empty(&h_qp->PrePostedRx) ) {
		op = (struct queued_rx_op *)
				dapl_llist_remove_head( &h_qp->PrePostedRx );
		status = dapls_ib_post_recv( op->ep_ptr,
					     &op->cookie,
					     op->segments,
					     &op->local_iov[0] );
		dapl_os_assert( status == DAT_SUCCESS );
		dapl_os_free(op, (sizeof(*op) +
				((op->segments - 1) * sizeof(DAT_LMR_TRIPLET))));
		posted++;
	}
	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		"\n%s() posted %d RX ops\n",__FUNCTION__,posted);
}

DAT_RETURN dapli_queue_recv(
		DAPL_EP		*ep_ptr,
		DAPL_COOKIE	*cookie,
		DAT_COUNT	segments,
		DAT_LMR_TRIPLET	*local_iov )
{
	DAT_RETURN status;
	struct queued_rx_op *op;

	dapl_os_assert(ep_ptr->qp_handle);

	op = (struct queued_rx_op *)dapl_os_alloc(sizeof(struct queued_rx_op) +
			((segments - 1) * sizeof(DAT_LMR_TRIPLET)));
	if ( !op )
		return DAT_INSUFFICIENT_RESOURCES; 

	op->ep_ptr = ep_ptr;
	op->cookie = *cookie;
	op->segments = segments;

	/* copy iov's */
	memcpy( op->local_iov, local_iov, segments  * sizeof(DAT_LMR_TRIPLET) );

	dapl_llist_add_tail(&ep_ptr->qp_handle->PrePostedRx, &op->Entry, (void*)op);

	dapl_dbg_log(DAPL_DBG_TYPE_CM, "%s(ep %lx) queued RX op %lx h_qp %lx\n",
		__FUNCTION__, ep_ptr, op, ep_ptr->qp_handle);

	return DAT_SUCCESS;
}

/*
 * dapls_ib_accept_connection
 *
 * Perform necessary steps to accept a connection
 *	conn_req (Connection Request cm_id) was constructed when the CM REP arrived,
 *	(dapli_recv_conn_request); point being it is not a fully populated cm_id.
 *	This routine merges useful info from conn_req --> ep_conn and then destroys
 *	conn_req after the Accept() completes successfully.
 *
 * Input:
 *	conn_req_handle - connection request handle
 *	ep_handle	- EndPoint handle
 *	private_data_size
 *	private_data
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *	DAT_INTERNAL_ERROR
 *
 */
DAT_RETURN
dapls_ib_accept_connection( IN DAT_CR_HANDLE	conn_req_handle,
			    IN DAT_EP_HANDLE	ep_handle,
			    IN DAT_COUNT	p_size,
			    IN const DAT_PVOID	p_data )
{
	int ret;
	DAT_RETURN dat_status = DAT_SUCCESS;
	HRESULT hr;
	DAPL_CR *cr_ptr = (DAPL_CR *) conn_req_handle;
	DAPL_EP *ep_ptr = (DAPL_EP *) ep_handle;
	DAPL_IA *ia_ptr = ep_ptr->header.owner_ia;
	DAPL_HCA *pHCA = ia_ptr->hca_ptr;
	DAPL_CM_ID *pConn;
	struct sockaddr_in *pSA_in;
	DAPL_ND_QP *h_qp;
	DAPL_ND_CQ *h_cq;
	char *reason=NULL;
	ULONG reason_len=0;

	dapl_os_assert(cr_ptr);
	pConn = cr_ptr->ib_cm_handle;
	dapl_os_assert(pConn);
	dapl_os_assert(ep_ptr);

	if( pConn->ConnState != DCM_ACCEPTING ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR/*CM*/,
	    	"-->ACCEPT: (pConn %lx) Invalid ConnState %s ? (EP %lx)\n",
			pConn, dapli_cm_state_str(pConn->ConnState), ep_ptr);

		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,
						DAT_INVALID_STATE_EP_NOTREADY);
	}
	dapl_dbg_log(DAPL_DBG_TYPE_CM,
	    "-->ACCEPT: (pConn %lx)->ConnState %s EP %lx\n",
		pConn, dapli_cm_state_str(pConn->ConnState), ep_ptr);

	dapl_os_assert ( ! DAPL_BAD_HANDLE(ep_ptr, DAPL_MAGIC_EP) );

	if (p_size > IB_MAX_REP_PDATA_SIZE) {
		dat_status = DAT_ERROR(DAT_LENGTH_ERROR, DAT_NO_SUBTYPE);
		sprintf(pConn->p_data,"ACCEPT: Private_Data size %d > max %d",
			p_size,IB_MAX_REP_PDATA_SIZE);
		reason = pConn->p_data;
		reason_len = strlen(reason) + 1;
		goto bail;
	}

	cr_ptr->param.local_ep_handle = ep_handle;

	/*
	 * use existing EP QueuePair with connector from which the
	 * Listen/GetConnectionResults fired on.
	 */
	if( ep_ptr->qp_state != DAPL_QP_STATE_UNCONNECTED ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR/*CM*/,
	    	"%s() pConn %lx (EP %lx)->qp_state %d?\n",
			__FUNCTION__, pConn, ep_ptr, ep_ptr->qp_state);

		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,
						DAT_INVALID_STATE_EP_NOTREADY);
	}
	ep_ptr->qp_state = DAPL_QP_STATE_PASSIVE_CONNECTION_PENDING;

	pConn->ep = ep_ptr;

	dapl_os_assert(ep_ptr->qp_handle);
	h_qp = pConn->h_qp = ep_ptr->qp_handle;
	dapl_os_assert( h_qp );
	h_qp->pConn = pConn;	// point back to this connection

	dapl_os_assert( h_qp->ND_QP );
	dapl_os_assert( h_qp->pRecvCQ );
	dapl_os_assert( h_qp->pRecvCQ->ND_CQ );
	dapl_os_assert( h_qp->pSendCQ );
	dapl_os_assert( h_qp->pSendCQ->ND_CQ );
	dapl_os_assert( pConn->serial.hEvent );

	/* setup local and remote ports for ep query.
	 * Note: port qual in network order.
	 *
	 * local address info.
	 */

   	pSA_in = (struct sockaddr_in*)&pConn->local_addr;
	ep_ptr->param.local_port_qual = PORT_TO_SID(pSA_in->sin_port);

	/* peer address info */

   	pSA_in = (struct sockaddr_in*)&pConn->remote_addr;
	ep_ptr->param.remote_port_qual = PORT_TO_SID(pSA_in->sin_port);
	dapl_os_memcpy( &ep_ptr->remote_ia_address,
			&pConn->remote_addr,
			sizeof(ep_ptr->remote_ia_address));

	dapl_os_memcpy( &cr_ptr->remote_ia_address, 
			&ep_ptr->remote_ia_address, 
			sizeof(cr_ptr->remote_ia_address));

	if ( !dapl_llist_is_empty(&h_qp->PrePostedRx) )
		dapli_post_queued_recvs( h_qp );

	dapl_os_lock(&pConn->lock);
	pConn->ConnState = DCM_CONNECTED;
	dapl_os_unlock(&pConn->lock);

	cr_ptr->ib_cm_handle = pConn;
	ep_ptr->cr_ptr = cr_ptr;
	dapl_os_assert(pConn->ep == ep_ptr);

	pConn->OverLap.ior_type = IOR_Accept;
	pConn->OverLap.context = (void*) pConn;

	hr = pConn->pConnector->lpVtbl->Accept(
					pConn->pConnector, // this
					(IUnknown*) h_qp->ND_QP,
					(ULONG)pConn->PeerInboundReadLimit,//XXX ?
					(ULONG)pConn->PeerOutboundReadLimit,
					(VOID*)p_data,
					(ULONG) p_size,
					&pConn->OverLap.Ov );
	if ( FAILED(hr) ) {
		pConn->ConnState = DCM_REJECTED;
		reason = ND_error_str(hr);
		reason_len = strlen(reason) + 1;
		dapl_log(DAPL_DBG_TYPE_ERR/*CM_ERR*/,
			"%s(pConn %lx) Accept ERR pConnector(%lx) reason %s "
			"(0x%x) len %d @ line #%d\n",
				__FUNCTION__, pConn, pConn->pConnector,
				reason, hr, reason_len, __LINE__);

		dat_status = DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,
						DAT_INVALID_STATE_EP_NOTREADY);
		goto bail;
	}

	return dat_status;

bail:
	/* REJECT connection request */
	hr = pConn->pConnector->lpVtbl->Reject( pConn->pConnector,
						  reason,
						  reason_len );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() ND_Reject() failed: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
	}
	else {
		dapl_log(DAPL_DBG_TYPE_ERR/*CM_ERRS*/,
			"%s() PASSIVE connection Reject()'ed reason '%s' len %d\n",
				__FUNCTION__,reason,reason_len);
	}

	return dat_status;
}


static void dapli_accept_complete( struct dapl_cm_id *pConn, HRESULT Error )
{
	int ret;
	DAT_RETURN dat_status=DAT_SUCCESS;
	HRESULT hr;
	DAPL_CR *cr_ptr;
	DAPL_EP *ep_ptr;
	DAPL_ND_QP *h_qp;
	DAPL_ND_CQ *h_cq;
	ib_cm_events_t conn_event;
	char *reason=NULL;
	ULONG reason_len=0;

	ep_ptr = pConn->ep;
	cr_ptr = ep_ptr->cr_ptr;
	h_qp = pConn->h_qp;

	if( FAILED(Error) ) {
		dapl_log(DAPL_DBG_TYPE_CM,
			"%s() ERR: %s @ line #%d, cleanup pConn %lx.\n",
				__FUNCTION__,ND_error_str(Error),__LINE__, pConn);
		conn_event = dapli_NDerror_2_CME_event(Error);
		reason = ND_error_str(Error);
		reason_len = strlen(reason) + 1;
		goto fail;
	}

	dapl_os_assert( pConn->ConnState != DCM_REJECTED );

	// dapl_os_lock(&pConn->lock);
	// pConn->ConnState = DCM_CONNECTED;
	// dapl_os_unlock(&pConn->lock);

	ep_ptr->qp_state = DAPL_QP_STATE_CONNECTED;

	/* setup passive side disconnect notifier, required by dtestcm.exe */
	pConn->DisconnectNotify.ior_type = IOR_DisconnectNotify;
	pConn->DisconnectNotify.context = (void*) pConn;
	hr = pConn->pConnector->lpVtbl->NotifyDisconnect(
						pConn->pConnector,
						&pConn->DisconnectNotify.Ov );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, 
			"%s() NotifyDisconnect(pConn %lx) ERR: %s @ line #%d\n",
				__FUNCTION__,pConn,ND_error_str(hr),__LINE__);
	}

	/* ARM Recv & Send CQs */
	dapl_os_assert(h_qp->pRecvCQ);
	h_cq = h_qp->pRecvCQ;
	dapl_os_atomic_inc(&h_cq->armed);
	h_cq->OverLap.ior_type = IOR_CQNotify;
	hr = h_cq->ND_CQ->lpVtbl->Notify( h_cq->ND_CQ,
					ND_CQ_NOTIFY_ANY,
					&h_cq->OverLap.Ov);
	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&h_cq->armed);
		dapl_log(DAPL_DBG_TYPE_ERR, 
			"%s() Recv CQ-Notify() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	dapl_os_assert(h_qp->pSendCQ);
	h_cq = h_qp->pSendCQ;
	dapl_os_atomic_inc(&h_cq->armed);
	h_cq->OverLap.ior_type = IOR_CQNotify;
	hr = h_cq->ND_CQ->lpVtbl->Notify( h_cq->ND_CQ,
					ND_CQ_NOTIFY_ANY,
					&h_cq->OverLap.Ov);
	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&h_cq->armed);
		dapl_log(DAPL_DBG_TYPE_ERR, 
			"%s() Send CQ-Notify() ERR: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}

	/* link this connection into the EndPoint's list of connections so
	 * dapl_get_cm_from_ep() works.
	 */
	dapl_ep_link_cm(ep_ptr, pConn);

	dapl_log(DAPL_DBG_TYPE_CM,
	    "%s(pConn %ls)-->CONNECTED[passive] pConnector %lx QP(%lx)->ND_QP %lx\n",
		__FUNCTION__,pConn,pConn->pConnector,h_qp,h_qp->ND_QP); 

	conn_event = IB_CME_CONNECTED;
	dapl_os_assert( ep_ptr->cr_ptr );
fail:
	dapls_cr_callback( pConn,
			   conn_event,
			   (void * __ptr64) pConn->p_data,
			   pConn->p_len,
			   pConn->sp );

	if ( reason == NULL )
		return;

bail:
	/* REJECT connection request */
	hr = pConn->pConnector->lpVtbl->Reject( pConn->pConnector,
						  reason,
						  reason_len );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() ND_Reject() failed: %s @ line #%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
	}
	else {
		dapl_log(DAPL_DBG_TYPE_ERR/*CM_ERRS*/,
			"%s() PASSIVE connection Reject()'ed reason '%s' len %d\n",
				__FUNCTION__,reason,reason_len);
	}
}

/*
 * dapls_ib_reject_connection
 *
 * Reject a connection
 *
 * Input:
 *	cr_handle
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INTERNAL_ERROR
 *
 */
DAT_RETURN
dapls_ib_reject_connection(IN dp_ib_cm_handle_t cm_handle,
			   IN int reason,
			   IN DAT_COUNT private_data_size,
			   IN const DAT_PVOID private_data)
{
	DAT_RETURN dat_status = DAT_SUCCESS;
	int offset = sizeof(struct dapl_pdata_hdr);
	struct dapl_pdata_hdr pdata_hdr;
	HRESULT hr;

	memset(&pdata_hdr, 0, sizeof pdata_hdr);
	pdata_hdr.version = htonl((DAT_VERSION_MAJOR << 24) |
				  (DAT_VERSION_MINOR << 16) |
				  (VN_PROVIDER_MAJOR << 8) |
				  (VN_PROVIDER_MINOR));

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		     " reject: handle %p reason %x, ver=%x, data %p, sz=%d\n",
		     cm_handle, reason, ntohl(pdata_hdr.version),
		     private_data, private_data_size);

	if (cm_handle == IB_INVALID_HANDLE) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			     " reject: invalid handle: reason %d\n", reason);
		return DAT_ERROR(DAT_INVALID_HANDLE, DAT_INVALID_HANDLE_CR);
	}

	/* setup pdata_hdr and users data, in CR pdata buffer */
	dapl_os_memcpy(cm_handle->p_data, &pdata_hdr, offset);
	if (private_data_size)
		dapl_os_memcpy(cm_handle->p_data + offset,
			       private_data, private_data_size);
	/*
	 * Always some private data with reject so active peer can
	 * determine real application reject from an abnormal 
	 * application termination
	 */
	hr = cm_handle->pConnector->lpVtbl->Reject(cm_handle->pConnector, 
			  cm_handle->p_data, (SIZE_T)(offset + private_data_size));
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() Reject() ERR: %s @ line #%d\n",
			__FUNCTION__,ND_error_str(hr),__LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
	}

	/* no EP linking, ok to destroy */
	dapls_cm_release(cm_handle);

	return dat_status;
}


/*
 * dapls_ib_cm_remote_addr
 *
 * Obtain the remote IP address given a connection
 *
 * Input:
 *	cr_handle
 *
 * Output:
 *	remote_ia_address: where to place the remote address
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_HANDLE
 *
 */
DAT_RETURN
dapls_ib_cm_remote_addr(IN DAT_HANDLE dat_handle, OUT DAT_SOCK_ADDR6 * raddr)
{
	DAPL_HEADER *header;
	dp_ib_cm_handle_t pConn;
	DAT_SOCK_ADDR6 *ipaddr;
	DAPL_EP *ep_ptr=NULL;

	dapl_dbg_log(DAPL_DBG_TYPE_EP,
		     " remote_addr(cm_handle=%p, r_addr=%p)\n",
		     dat_handle, raddr);

	header = (DAPL_HEADER *) dat_handle;

	if (header->magic == DAPL_MAGIC_EP) {
		pConn = dapl_get_cm_from_ep((DAPL_EP *) dat_handle);
		ep_ptr = (DAPL_EP *) dat_handle;
	}
	else if (header->magic == DAPL_MAGIC_CR)
		pConn = ((DAPL_CR *) dat_handle)->ib_cm_handle;
	else {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() Invalid handle?\n",__FUNCTION__);
		return DAT_INVALID_HANDLE;
	}

	/* get remote IP address from cm_id route */
	ipaddr = &pConn->remote_addr;
	dapl_os_assert(((struct sockaddr_in *)ipaddr)->sin_addr.S_un.S_addr != 0);

	dapl_os_memcpy( raddr, ipaddr, sizeof(DAT_SOCK_ADDR) );

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		"%s() pConn %p remtote_addr %s PORT %d\n",
			__FUNCTION__, pConn, 
			dapli_IPv4_addr_str(ipaddr,NULL),
			ntohs( ((struct sockaddr_in *)&ipaddr)->sin_port) );
	
	return DAT_SUCCESS;
}

/*
 * dapls_ib_private_data_size
 *
 * Return the size of max private data 
 *
 * Input:
 *      hca_ptr         hca pointer, needed for transport type
 *
 * Output:
 *	None
 *
 * Returns:
 * 	maximum private data supplied from transport.
 *
 */
int dapls_ib_private_data_size(IN DAPL_HCA * hca_ptr)
{
	return IB_MAX_REQ_PDATA_SIZE; // XXX REP REJ DREQ | DREP ? usage context
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */
