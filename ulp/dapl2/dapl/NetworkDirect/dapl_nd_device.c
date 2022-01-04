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

/**********************************************************************
 * 
 * MODULE: dapl_nd_device.c
 *
 * PURPOSE: OFED DAPL over NetworkDirect provider
 *	init, open, close, utilities
 *
 **********************************************************************/

#include "dapl.h"
#include "dapl_adapter_util.h"
#include "dapl_ep_util.h"
#include "dapl_osd.h"
#include "dapl_nd.h"

#include <stdlib.h>

#ifdef DAT_IB_COLLECTIVES
#include <collectives/ib_collectives.h>
#endif


DAT_UINT32 g_parent = 0;
DAPL_OS_LOCK g_hca_lock;
struct dapl_llist_entry *g_hca_list;

int gDAPL_rank = -2;

static int dapls_os_init(void)
{
	/* WSAStartup() & WSACleanup() done in NdStartup/NdCleanup() */

  { // XXX - get Intel MPI rank.
	char *s;
	s = getenv("PMI_RANK");
	if ( s  && (s[0] != '\0') ) {
		gDAPL_rank = atoi(s);
	}
  }
	return 0;
}

static void dapls_os_release(void)
{
}


enum ibv_mtu dapl_ib_mtu(int mtu)
{
	switch (mtu) {
	case 256:
		return IBV_MTU_256;
	case 512:
		return IBV_MTU_512;
	case 1024:
		return IBV_MTU_1024;
	case 2048:
		return IBV_MTU_2048;
	case 4096:
		return IBV_MTU_4096;
	default:
		return IBV_MTU_1024;
	}
}


char *dapl_ib_mtu_str(enum ibv_mtu mtu)
{
	switch (mtu) {
	case IBV_MTU_256:
		return "256";
	case IBV_MTU_512:
		return "512";
	case IBV_MTU_1024:
		return "1024";
	case IBV_MTU_2048:
		return "2048";
	case IBV_MTU_4096:
		return "4096";
	default:
		return "1024";
	}
}

#define DMP_ND_ADDRS 0

#if DMP_ND_ADDRS
static void dump_ND_addr_list( SOCKET_ADDRESS_LIST *pSAL )
{
	SOCKET_ADDRESS *pSA;
	struct sockaddr_in *sa4;
	struct sockaddr_in6 *sa6;
	int i;

	printf("%s() %d local ND devices\n",__FUNCTION__,pSAL->iAddressCount);

	for(i=0,pSA=pSAL->Address; i < pSAL->iAddressCount; i++,pSA++) {
		switch ( pSA->lpSockaddr->sa_family )
		{
		  case AF_INET:
			sa4 = (struct sockaddr_in *)pSA->lpSockaddr; 
			dapl_os_assert(sa4->sin_family == AF_INET);
			printf("  ND IF[%d] AF_INET: [%s]\n",
					i, dapli_IPaddr_str(pSA->lpSockaddr,0));
			break;

		  case AF_INET6:
			sa6 = (struct sockaddr_in6 *)pSA->lpSockaddr; 
                        printf("  ND IF[%d] AF_INET6: %s port %d\n",
				i,dapli_IPaddr_str(pSA->lpSockaddr,0), 
				ntohs(sa6->sin6_port) );
			break;

		  default: 
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Unknown SA_Family %d @ line#%d\n",
				__FUNCTION__, pSA->lpSockaddr->sa_family, __LINE__);
			break;
		}
        }
}
#endif

/* Get IP address based on the IP address list of NetworkDirect interfaces.
 *  hca_name		ND0, ND1 ala dat.conf
 *  *V6			sockaddr for the current adapter
 *  IF_match_idx	use the 'nth' IPv4 ND interface
 *  *pV4		sockaddr_in4 of ND IF selected.
 *
 * dat.conf specifies a config line using an ia_device_params name such as 'ND0'
 * 'ND' is the name, where '0' is the '0'th NetworkDirect device in the system.
 * For now just match the nth RDMA IP address we get from QueryAddressList.
 * Such that ND1 returns the QueryAddressList[1] sockaddr.
 *
 * Return:
 *	0 == SUCCESS, otherwise failure.
 */
static int
dapli_GetIPaddr( char			*hca_name,
		 struct sockaddr	*pTarget_sa,
	   	 ULONG			*IF_match_idx )
{
	SOCKET_ADDRESS_LIST	*pSAL;
	SOCKET_ADDRESS		*pSA;
	struct sockaddr_in	*sa4;
	struct sockaddr_in6	*sa6;
        HRESULT			hr;
	int			i;
	int			ipv4_if_cnt = -1;
	int			ipv6_if_cnt = -1;
	ULONG			BufSize=0;

	/* parse hca_name for device instance (ND0 == 0th ND device)
	 * Such that ND0 returns QueryAddressList[0] sockaddr pointer.
	 * Parse hca_name back to front to isolate the device index.
	 */
	i = strcspn(hca_name,"0123456789");
	if ( i == strlen(hca_name) ) // no device number, use QueryAddressList[0].
		*IF_match_idx = 0;
	else
		*IF_match_idx = atoi(&hca_name[i]);

	/* Query to get required bufsize, BufSize updated in failure */
	hr = NdQueryAddressList( 0, NULL, &BufSize );

	pSAL = (SOCKET_ADDRESS_LIST*) dapl_os_alloc( BufSize );
	if ( !pSAL ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() dapl_os_alloc(%d) failed?\n",
			__FUNCTION__, BufSize);
		return 1;
	}
	hr = NdQueryAddressList( 0, pSAL, &BufSize );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() NdQueryAddressList() ERR: %s @ line#%d\n",
				__FUNCTION__,ND_error_str(hr),__LINE__);
		dapl_os_free(pSAL,BufSize);
		return 2;
	}
#if DMP_ND_ADDRS
	dump_ND_addr_list(pSAL);
#endif
	for(i=0,pSA=pSAL->Address; i < pSAL->iAddressCount; i++,pSA++) {
		switch ( pSA->lpSockaddr->sa_family ) {
		  case AF_INET:
			sa4 = (struct sockaddr_in *)pSA->lpSockaddr; 
			/* HACK-alert: reject local or MS default IPv4 addrs */
			if ( sa4->sin_addr.S_un.S_un_b.s_b1 == 169 ||
			     sa4->sin_addr.S_un.S_un_b.s_b1 == 127 )
#if DBG
				dapl_log(DAPL_DBG_TYPE_CM,
					"%s() hca_name '%s' IF_match_idx %d\n",
					__FUNCTION__, hca_name, *IF_match_idx);
#endif
				break;
			if ( *IF_match_idx == ++ipv4_if_cnt ) {
				memcpy( pTarget_sa, sa4, sizeof(*sa4) );
				dapl_os_free(pSAL,BufSize);
        			return 0;
			}
			break;

		  case AF_INET6:
			sa6 = (struct sockaddr_in6 *)pSA->lpSockaddr; 
                        dapl_log(DAPL_DBG_TYPE_CM, "  Local AF_INET6: %s port %d\n",
				dapli_IPaddr_str(pSA->lpSockaddr,0), 
				ntohs(sa6->sin6_port) );
			if ( *IF_match_idx == ++ipv6_if_cnt ) {
				memcpy( pTarget_sa, sa6, sizeof(*sa6) );
				dapl_os_free(pSAL,BufSize);
        			return 0;
			}
			break;

		  default: 
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Unknown SA_Familyi %#x @ line#%d\n",
				__FUNCTION__, pSA->lpSockaddr->sa_family, __LINE__);
			break;
		}
        }
	dapl_log(DAPL_DBG_TYPE_ERR, "%s() Did not select an ND IF? wanted IF %d\n",
				__FUNCTION__, *IF_match_idx );
	dapl_os_free(pSAL,BufSize);
        return 3;
}

/*
 * dapls_ib_init & dapls_ib_release
 *
 * Initialize Verb related items for device open
 *
 * Input:
 * 	none
 *
 * Output:
 *	none
 *
 * Returns:
 * 	0 success, -1 error
 *
 */

int32_t dapls_ib_init(void)
{
	HRESULT hr;

	g_parent = dapl_os_getpid();

	/* initialize hca_list lock */
	dapl_os_lock_init(&g_hca_lock);

	/* initialize hca list for CQ events */
	dapl_llist_init_head(&g_hca_list);

	if (dapls_os_init())
		return 1;

	hr = NdStartup();
	if( FAILED(hr) )
	{
		fprintf(stderr, "%s() NdStartup failed with %s\n",
				__FUNCTION__, ND_error_str(hr) );
		exit( __LINE__ );
	}
	return 0;
}


int32_t dapls_ib_release(void)
{
	HRESULT hr;
	int32_t rc=12;

	/* only parent will cleanup */
	if (dapl_os_getpid() != g_parent)
		return 0;

	hr = NdCleanup();
	if( FAILED(hr) )
	{
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() NdCleanup failed %s\n",
			__FUNCTION__, ND_error_str(hr));
		rc = -1;
	}
	dapls_os_release();

	if ( ! dapl_llist_is_empty(&g_hca_list) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() g_hca_list NOT Empty?\n", __FUNCTION__);
		rc = -2;
	}
	dapl_dbg_log(DAPL_DBG_TYPE_UTIL, "%s() EXIT %d\n",__FUNCTION__,rc);

	return rc;
}


/*
 * dapls_ib_open_hca
 *
 * Open HCA
 *
 * Input:
 *      *hca_name	pointer to provider device name
 *      *hca_ptr  	pointer to provide HCA handle
 *
 * Output:
 *      none
 *
 * Return:
 *      DAT_SUCCESS
 *      DAT_INSUFFICIENT_RESOURCES
 *
 */

DAT_RETURN dapls_ib_open_hca(IN IB_HCA_NAME hca_name, IN DAPL_HCA *hca_ptr)
{
	DAT_RETURN dat_status;
	int ret;
	HRESULT hr;
	IND2Adapter *pAdapter;
	SIZE_T sa_len, Len, QueueDepth;
	struct sockaddr *sa = (struct sockaddr *)&hca_ptr->hca_address;
	ib_hca_transport_t *pND_transport = &hca_ptr->ib_trans;
	ULONG max_inline_send;

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL, "%s: %s hca_ptr %lx\n",
			__FUNCTION__, hca_name, hca_ptr);

	/* HCA name is NetworkDirect device name such as ND0, ND1 ala dat.conf */
	ret = dapli_GetIPaddr( (char *)hca_name, sa, &hca_ptr->port_num );
	if ( ret )
		return DAT_INVALID_ADDRESS;

	ret = strlen(hca_name)+1;
	if ( ret >= DAPL_ND_MAX_DEV_NAME ) {
		ret = DAPL_ND_MAX_DEV_NAME - 1;
		pND_transport->dev_name[ret] = '\0';
	}
	strncpy( pND_transport->dev_name, hca_name, ret );

	/* Open NetworkDirect adapter */
	sa_len = (sa->sa_family == AF_INET
			? sizeof(struct sockaddr_in)
			: sizeof(struct sockaddr_in6));

	dapl_dbg_log(DAPL_DBG_TYPE_CM,
		"-->opening_hca: %s, %s %d.%d.%d.%d\n",
		     hca_name,
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_family == AF_INET ?
		     "AF_INET" : "AF_INET6", 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 0 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 8 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 16 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 24 & 0xff );

	hr = NdOpenAdapter( &IID_IND2Adapter, sa, sa_len, &pAdapter);
	if( FAILED(hr) ) {

		dapl_log(DAPL_DBG_TYPE_ERR,
			" %s(): NdOpenIAdapter [%s] ERR: %s line #%d\n",
			__FUNCTION__, dapli_IPaddr_str(sa,NULL), ND_error_str(hr),
			__LINE__);
		return DAT_INTERNAL_ERROR;
	}

	/* get adapter info V2 */
	pND_transport->nd_adapter_info.InfoVersion = ND_VERSION_2;
	Len = sizeof(pND_transport->nd_adapter_info);
	hr = pAdapter->lpVtbl->Query( pAdapter,
				      &pND_transport->nd_adapter_info,
				      &(ULONG)Len ); 
	if( FAILED( hr ) ) 
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() IND2Adapter::Query failed %s line #%d\n",
			__FUNCTION__, ND_error_str(hr), __LINE__ );
		return DAT_INTERNAL_ERROR;
	}

	/* Get the file handle for overlapped operations on this adapter.  */
	hr = pAdapter->lpVtbl->CreateOverlappedFile(
					pAdapter,
					&pND_transport->AdapterOvFileHandle );
	if( FAILED( hr ) ) 
	{
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() IND2Adapter::CreateOverlappedFile ERR %s line #%d\n",
			__FUNCTION__, ND_error_str(hr), __LINE__ );
		return DAT_INTERNAL_ERROR;
	}

	ret = BindIoCompletionCallback( pND_transport->AdapterOvFileHandle,
					IOCompletion_cb,
					0 ); 
	if ( ret == 0 ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() BindIoCompletionCallback() ERR: 0x%08x @ "
			"line #%d\n",
				__FUNCTION__,GetLastError(),__LINE__);
		return DAT_INSUFFICIENT_RESOURCES;
	}

	pND_transport->ib_ctx = hca_ptr->ib_hca_handle = pAdapter;

	max_inline_send = dapl_os_get_env_val( "DAPL_MAX_INLINE",
				pND_transport->nd_adapter_info.MaxInlineDataSize);
	if (max_inline_send > 0 &&
	    max_inline_send < pND_transport->nd_adapter_info.MaxInlineDataSize)
		pND_transport->nd_adapter_info.MaxInlineDataSize = max_inline_send;

	/* set CM timer defaults */
	pND_transport->max_cm_timeout = dapl_os_get_env_val(
				"DAPL_MAX_CM_RESPONSE_TIME",IB_CM_RESPONSE_TIMEOUT);
	pND_transport->max_cm_retries = dapl_os_get_env_val(
				"DAPL_MAX_CM_RETRIES", IB_CM_RETRIES);
	
	pND_transport->arp_timeout = dapl_os_get_env_val(
					"DAPL_CM_ARP_TIMEOUT_MS",IB_ARP_TIMEOUT);
	pND_transport->arp_retries = dapl_os_get_env_val(
				"DAPL_CM_ARP_RETRY_COUNT",IB_ARP_RETRY_COUNT);
	pND_transport->route_timeout = dapl_os_get_env_val(
					"DAPL_CM_ROUTE_TIMEOUT_MS",IB_ROUTE_TIMEOUT);
	pND_transport->route_retries = dapl_os_get_env_val(
				"DAPL_CM_ROUTE_RETRY_COUNT",IB_ROUTE_RETRY_COUNT);

	/* set default IB MTU */
	pND_transport->mtu = dapl_ib_mtu(2048);

	dapl_os_atomic_set( &pND_transport->num_connections, 0 );
	/* 
	 * Put new hca_transport on list for async and CQ event processing 
	 * Wakeup work thread to add to polling list
	 */
	dapl_llist_init_entry((DAPL_LLIST_ENTRY *) &pND_transport->entry);
	dapl_os_lock(&g_hca_lock);
	dapl_llist_add_tail(&g_hca_list,
			    (DAPL_LLIST_ENTRY *) &pND_transport->entry,
			    &pND_transport->entry);

	dapl_os_unlock(&g_hca_lock);

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		"-->opened adapter %s, %s %d.%d.%d.%d INLINE_MAX=%d(threshold %d)\n",
		     hca_name,
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_family == AF_INET ?
		     "AF_INET" : "AF_INET6", 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 0 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 8 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 16 & 0xff, 
		     ((struct sockaddr_in *)
		     &hca_ptr->hca_address)->sin_addr.s_addr >> 24 & 0xff, 
		     pND_transport->nd_adapter_info.MaxInlineDataSize,
		     pND_transport->nd_adapter_info.InlineRequestThreshold);

#ifdef DAT_IB_COLLECTIVES
	if (dapli_create_collective_service(hca_ptr))
		return DAT_INTERNAL_ERROR;
#endif

	return DAT_SUCCESS;
}


/*
 * dapls_ib_close_hca
 *
 * Open HCA
 *
 * Input:
 *      ib_hca_handle   provide HCA handle
 *
 * Output:
 *      none
 *
 * Return:
 *      DAT_SUCCESS
 *      DAT_INSUFFICIENT_RESOURCES
 *
 */

int _noise=0;

DAT_RETURN dapls_ib_close_hca(IN DAPL_HCA * hca_ptr)
{
	HRESULT hr;
	ib_hca_transport_t *pND = &hca_ptr->ib_trans;

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		"%s(hca %lx)->ib_hca_handle %lx num_connections %ld\n",
			__FUNCTION__, hca_ptr, hca_ptr->ib_hca_handle,
			pND->num_connections);
#ifdef DAT_IB_COLLECTIVES
	dapli_free_collective_service(hca_ptr);
#endif

	_noise = 1;

	CloseHandle(hca_ptr->ib_trans.AdapterOvFileHandle);
	hca_ptr->ib_trans.AdapterOvFileHandle = IB_INVALID_HANDLE;

	if (hca_ptr->ib_hca_handle != IB_INVALID_HANDLE) {
		hca_ptr->ib_hca_handle->lpVtbl->Release(hca_ptr->ib_hca_handle);
		hca_ptr->ib_hca_handle = IB_INVALID_HANDLE;
	}

	dapl_os_lock(&g_hca_lock);
	dapl_llist_remove_entry(&g_hca_list, (DAPL_LLIST_ENTRY *) &pND->entry);
	dapl_os_unlock(&g_hca_lock);

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		"%s(HCA %lx) CLOSED.\n", __FUNCTION__, hca_ptr);

	return DAT_SUCCESS;
}


/*
 * dapls_ib_cq_alloc
 *
 * Alloc a CQ
 *
 * Input:
 *	ia_handle		IA handle
 *	evd_ptr			pointer to EVD struct
 *	cqlen			minimum QLen
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_cq_alloc(IN DAPL_IA * ia_ptr,
		  IN DAPL_EVD * evd_ptr, IN DAT_COUNT * cqlen)
{
	DAT_RETURN ret;
	DAPL_ND_CQ *h_CQ;
	HRESULT hr;
	SIZE_T Len;
	IND2Adapter *pAdapter = ia_ptr->hca_ptr->ib_hca_handle;

	dapl_dbg_log(DAPL_DBG_TYPE_EP,
		     "dapls_ib_cq_alloc: evd %lx cqlen=%ld \n", evd_ptr, *cqlen);

	evd_ptr->ib_cq_handle = NULL;
	h_CQ = dapl_os_alloc(sizeof(DAPL_ND_CQ));
	if (!h_CQ )
		return DAT_INSUFFICIENT_RESOURCES;

	memset( (void*)h_CQ, 0, sizeof(DAPL_ND_CQ) );

	h_CQ->Event = CreateEvent( NULL, FALSE, FALSE, TEXT("CQ_Event") );
	if (h_CQ->Event == NULL ) {
		dapl_os_free( (void*)h_CQ, sizeof(DAPL_ND_CQ) );
		return DAT_INSUFFICIENT_RESOURCES;
	}

	Len = (SIZE_T)(*cqlen);
	hr = pAdapter->lpVtbl->CreateCompletionQueue(
				pAdapter,
				&IID_IND2CompletionQueue,
				ia_ptr->hca_ptr->ib_trans.AdapterOvFileHandle,
				Len,
				0,	// group 
				0,	// affinity
				&h_CQ->ND_CQ );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() CreateCompletionQueue(depth %d)"
			" ERR %s @ line #%d\n",
				 __FUNCTION__, *cqlen, ND_error_str(hr), __LINE__);
		dapl_os_free((void*)h_CQ, sizeof(*h_CQ));
		return DAT_INSUFFICIENT_RESOURCES;
	}

	h_CQ->Magic = DAPL_CQ_MAGIC;
	h_CQ->OverLap.ior_type = IOR_CQNotify;
	h_CQ->OverLap.context = evd_ptr;
	h_CQ->context = evd_ptr;
	h_CQ->depth = *cqlen;
	evd_ptr->ib_cq_handle = h_CQ;

	/* Arm CQ for events */
	ret = dapls_set_cq_notify(ia_ptr, evd_ptr);
	if ( ret != DAT_SUCCESS ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s: daps_set_cq_notify(CQ %lx) failed"
			" DAT_ERR %#x @ line #%d\n",
				 __FUNCTION__, h_CQ, ret, __LINE__);
	}
	else
		dapl_dbg_log(DAPL_DBG_TYPE_EP,
			"CQ_ALLOC(%lx) len %d context %lx\n",
		     		evd_ptr->ib_cq_handle, *cqlen,h_CQ->context);

	return ret;
}


/*
 * dapls_ib_cq_free
 *
 * destroy a CQ
 *
 * Input:
 *	ia_handle		IA handle
 *	evd_ptr			pointer to EVD struct
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_PARAMETER
 *
 */
DAT_RETURN dapls_ib_cq_free(IN DAPL_IA * ia_ptr, IN DAPL_EVD * evd_ptr)
{
	DAT_EVENT event;
	DAT_RETURN dat_status = DAT_SUCCESS;
	HRESULT hr;
	ib_cq_handle_t h_CQ;
	DAPL_ND_IO_RESULT Result;
	DAPL_HCA *pHCA;
	int cnt=0;

	if ( ia_ptr == NULL || ia_ptr->header.magic != DAPL_MAGIC_IA )
		return DAT_INVALID_HANDLE;

	pHCA = ia_ptr->hca_ptr;
	h_CQ = evd_ptr->ib_cq_handle;
	if ( !h_CQ )
		return dat_status;

	/* pull off CQ and EVD entries and toss */

	while (dapls_ib_completion_poll(pHCA, evd_ptr, &Result) == DAT_SUCCESS)
		cnt++;

	if ( cnt > 0 ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() Lingering CQE %d\n",
		 	__FUNCTION__, cnt);
	}

	while (dapl_evd_dequeue(evd_ptr, &event) == DAT_SUCCESS) ;

	hr = h_CQ->ND_CQ->lpVtbl->CancelOverlappedRequests(h_CQ->ND_CQ);
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() CQ->CancelOverlappedRequests"
			" ERR %s @ line #%d\n",
			 	__FUNCTION__, ND_error_str(hr), __LINE__);
	}

	/* Wait for Canceled CQ notify callbacks. */
	{
		DWORD result;
		int tries, start;

		start = tries = 16;

		while( (tries-- > 0) && (dapl_os_atomic_read(&h_CQ->armed) > 0) ) {
			result = WaitForSingleObject( h_CQ->Event, 250 );
			if ( result == WAIT_OBJECT_0 )
				continue;
			if ( result != WAIT_TIMEOUT ) {
				dapl_log(DAPL_DBG_TYPE_ERR,
					"%s() WaitForSingleObject() ERR %#x @ "
					"line #%d\n",
			 		__FUNCTION__, GetLastError(), __LINE__);
			}
		}
		if ((tries == 0) || (dapl_os_atomic_read(&h_CQ->armed) != 0))
			dapl_log(DAPL_DBG_TYPE_ERR,
  			    "%s() ***Timeout CQ(%lx)->armed %ld tries %d start %d\n",
    				__FUNCTION__,h_CQ,dapl_os_atomic_read(&h_CQ->armed),
				(start-tries),start);
	}

	hr = h_CQ->ND_CQ->lpVtbl->Release(h_CQ->ND_CQ);
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s(%lx) CQ->Release()"
			" ERR %s @ line #%d\n",
			 	__FUNCTION__, h_CQ, ND_error_str(hr), __LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
	}

	h_CQ->ND_CQ = NULL;
	CloseHandle( h_CQ->Event );
	h_CQ->Event = NULL;

	dapl_os_free((void*)h_CQ, sizeof(*h_CQ));
	evd_ptr->ib_cq_handle = NULL;

	return dat_status;
}



/*
 * dapl_ib_cq_resize
 *
 * Alloc a CQ
 *
 * Input:
 *	ia_handle		IA handle
 *	evd_ptr			pointer to EVD struct
 *	cqlen			minimum QLen
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_PARAMETER
 *
 */
DAT_RETURN
dapls_ib_cq_resize(IN DAPL_IA * ia_ptr, IN DAPL_EVD * evd_ptr, IN DAT_COUNT * cqlen)
{
	DAT_RETURN dat_status;
	HRESULT hr;

	if ( evd_ptr->ib_cq_handle == NULL ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() Bad CQ Handle @ line #%d\n",
				 __FUNCTION__, __LINE__);
		return DAT_INVALID_HANDLE;
	}

	hr = evd_ptr->ib_cq_handle->ND_CQ->lpVtbl->Resize(
						evd_ptr->ib_cq_handle->ND_CQ,
						(SIZE_T)(*cqlen) );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() ERR %s @ line #%d\n",
				 __FUNCTION__, ND_error_str(hr), __LINE__);
		dat_status = DAT_INSUFFICIENT_RESOURCES;
	}
	else
		evd_ptr->ib_cq_handle->depth = *cqlen;

	return dat_status;
}

/*
 * dapls_set_cq_notify
 *
 * Set the CQ notification for next
 *
 * Input:
 *	hca_handl		hca handle
 *	DAPL_EVD		evd handle
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	dapl_convert_errno 
 */
DAT_RETURN dapls_set_cq_notify(IN DAPL_IA * ia_ptr, IN DAPL_EVD * evd_ptr)
{
	HRESULT hr;
	DAT_RETURN ret = DAT_SUCCESS;
	ib_cq_handle_t h_CQ = evd_ptr->ib_cq_handle;

	dapl_os_assert(h_CQ);
	dapl_os_assert(h_CQ->ND_CQ);

	h_CQ->OverLap.ior_type = IOR_CQNotify;
	h_CQ->OverLap.context = evd_ptr;
	// assumes ior_context is set.

	dapl_os_atomic_inc(&h_CQ->armed);
	hr = h_CQ->ND_CQ->lpVtbl->Notify( h_CQ->ND_CQ,
					  ND_CQ_NOTIFY_ANY,
					  &h_CQ->OverLap.Ov );
	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&h_CQ->armed);
		dapl_log(DAPL_DBG_TYPE_ERR, "%s() h_CQ %lx Nofity(ANY)"
			" ERR %s @ line #%d\n",
				 __FUNCTION__, h_CQ, ND_error_str(hr), __LINE__);
		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_TEVD);
	}

	dapl_log(DAPL_DBG_TYPE_EP, "--> CQ %lx Armed\n",h_CQ);

	return DAT_SUCCESS;
}


/*
 * dapls_ib_qp_alloc
 *
 * Alloc a QP
 *
 * Input:
 *	*ia_ptr		pointer to IA info.
 *	*ep_ptr		pointer to EP INFO
 *	ep_ctx_ptr	ep context.
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
dapls_ib_qp_alloc(IN DAPL_IA * ia_ptr,
		  IN DAPL_EP * ep_ptr, IN DAPL_EP * ep_ctx_ptr)
{
	DAT_RETURN dat_status;
	DAT_EP_ATTR *attr;
	DAPL_EVD *recv_evd_ptr, *request_evd_ptr;
	ib_cq_handle_t cq_recv=NULL, cq_send=NULL;
	DAPL_HCA *pHCA;
	IND2Adapter *pAdapter;
	DAPL_ND_QP *h_qp;
	HRESULT hr;
	int i, j, h_qp_alloc;

	//int i, j, num_rx_results, num_tx_results, h_qp_alloc;
	//DAPL_ND_IO_RESULT *pResults;

	dapl_dbg_log(DAPL_DBG_TYPE_EP,
		" %s() ia_ptr %lx ep_ptr %lx ep_ctx_ptr %lx\n",
			__FUNCTION__, ia_ptr, ep_ptr, ep_ctx_ptr);

	pAdapter = ia_ptr->hca_ptr->ib_hca_handle;
	dapl_os_assert( pAdapter );
	pHCA = ia_ptr->hca_ptr;
	attr = &ep_ptr->param.ep_attr;
	dapl_os_assert( attr );

	dapl_os_assert(ep_ptr->qp_handle == NULL);

	recv_evd_ptr    = (DAPL_EVD *) ep_ptr->param.recv_evd_handle;
	request_evd_ptr = (DAPL_EVD *) ep_ptr->param.request_evd_handle;

	h_qp_alloc = sizeof(DAPL_ND_QP); 
	h_qp = (DAPL_ND_QP *) dapl_os_alloc(h_qp_alloc);
	if (!h_qp ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() ERR: alloc(%d bytes) h_qp + rx results @ line #%d\n",
				__FUNCTION__, h_qp_alloc,  __LINE__);
		return DAT_INSUFFICIENT_RESOURCES;
	}
	memset((void *)h_qp, 0, h_qp_alloc);

	dapl_llist_init_head( &h_qp->PrePostedRx );

	h_qp->pAdapter = pAdapter;

	dapl_os_assert ( recv_evd_ptr != DAT_HANDLE_NULL );

        cq_recv = (ib_cq_handle_t) recv_evd_ptr->ib_cq_handle;
        
        if ((cq_recv == IB_INVALID_HANDLE) && 
            ( 0 != (recv_evd_ptr->evd_flags & ~DAT_EVD_SOFTWARE_FLAG) ))
        {
            dat_status = dapls_ib_cq_alloc( ia_ptr,
					    recv_evd_ptr,
					    &recv_evd_ptr->qlen );
            if (dat_status != DAT_SUCCESS)
            {
                dapl_dbg_log (DAPL_DBG_TYPE_ERR,
			"--> %s() ERR rcv CQ create dat_status %#x @ line #%d\n",
				__FUNCTION__,dat_status,__LINE__);
		goto bail;
            }
            cq_recv = (ib_cq_handle_t) recv_evd_ptr->ib_cq_handle;
            dapl_dbg_log (DAPL_DBG_TYPE_ERR, "--> alloc_recv_CQ %lx len %d\n",
			cq_recv, recv_evd_ptr->qlen );
        }
	else {
            dapl_dbg_log (DAPL_DBG_TYPE_EP, 
                          "--> use EXISTING recv_CQ %lx len %d\n", cq_recv,
					    recv_evd_ptr->qlen );
	}

	dapl_os_assert ( request_evd_ptr != DAT_HANDLE_NULL );
        cq_send = (ib_cq_handle_t) request_evd_ptr->ib_cq_handle;
        
        if ((cq_send == IB_INVALID_HANDLE) && 
            ( 0 != (request_evd_ptr->evd_flags & ~DAT_EVD_SOFTWARE_FLAG) ))
        {
            dat_status = dapls_ib_cq_alloc( ia_ptr,
					    request_evd_ptr,
					    &request_evd_ptr->qlen );
            if (dat_status != DAT_SUCCESS)
            {
                dapl_dbg_log (DAPL_DBG_TYPE_ERR,
			"--> %s() ERR req CQ create dat_status %#x @ line #%d\n",
				__FUNCTION__,dat_status,__LINE__);
		goto bail2;
            }
            cq_send = (ib_cq_handle_t) request_evd_ptr->ib_cq_handle;
            dapl_dbg_log (DAPL_DBG_TYPE_ERR, 
                          "--> alloc_send_CQ = %lx\n", cq_send); 
        }
	else {
            dapl_dbg_log (DAPL_DBG_TYPE_EP, 
                          "--> use EXISTING Send_CQ %lx len %d\n", cq_send,
					    request_evd_ptr->qlen );
	}

	h_qp->recv_queue_depth = cq_recv->depth;
	h_qp->send_queue_depth = cq_send->depth;
	h_qp->max_inline_send = pHCA->ib_trans.nd_adapter_info.MaxInlineDataSize;

	h_qp->recv_max_sge = DAPL_MIN(attr->max_recv_iov,
				pHCA->ib_trans.nd_adapter_info.MaxReceiveSge);
	if (h_qp->recv_max_sge == 0)
		h_qp->recv_max_sge = MAX_SCATTER_GATHER_ENTRIES_ALLOC;

	h_qp->send_max_sge = DAPL_MIN(attr->max_request_iov,
				pHCA->ib_trans.nd_adapter_info.MaxInitiatorSge);
	if (h_qp->send_max_sge == 0)
		h_qp->send_max_sge = MAX_SCATTER_GATHER_ENTRIES_ALLOC;

        dapl_dbg_log (DAPL_DBG_TYPE_EP,
		"\n%s() CQ_depth(s:%d r:%d) max_SGE(s:%d r:%d) max_inline_send %d\n",
				__FUNCTION__,
				cq_send->depth,
				cq_recv->depth,
				h_qp->send_max_sge,
				h_qp->recv_max_sge,
				h_qp->max_inline_send );

	hr = pAdapter->lpVtbl->CreateQueuePair( pAdapter, 
						&IID_IND2QueuePair,
						(IUnknown*)cq_recv->ND_CQ,
						(IUnknown*)cq_send->ND_CQ,
						//(VOID*) h_qp,
						(VOID*) ep_ptr,
				 		cq_recv->depth,
				 		cq_send->depth,
				 		h_qp->recv_max_sge,
				 		h_qp->send_max_sge,
						h_qp->max_inline_send,
						&h_qp->ND_QP );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() CreateQueuePair(h_qp %lx) ERR %s @ line #%d\n",
				 __FUNCTION__, h_qp, ND_error_str(hr), __LINE__);
		goto bail2;
	}

	h_qp->qp_type = IB_QPT_RC;
	h_qp->pRecvCQ = cq_recv;
	h_qp->pSendCQ = cq_send;
	if ( cq_recv )
		cq_recv->h_qp = h_qp;
	if ( cq_send )
		cq_send->h_qp = h_qp;
	ep_ptr->qp_handle = h_qp;
	ep_ptr->qp_state = DAPL_QP_STATE_UNCONNECTED;

	dapl_dbg_log(DAPL_DBG_TYPE_EP,
		"%s(h_qp %lx)->ND_QP %lx sendCQ %lx recvCQ %lx\n",
			__FUNCTION__,h_qp,h_qp->ND_QP,h_qp->pSendCQ,h_qp->pRecvCQ);

	return DAT_SUCCESS;

bail2:
	cq_send->ND_CQ->lpVtbl->Release(cq_send->ND_CQ);
bail1:
	cq_recv->ND_CQ->lpVtbl->Release(cq_recv->ND_CQ);
bail:
	dapl_os_free((void*)h_qp, h_qp_alloc);

	return dat_status;
}


/*
 * dapl_ib_qp_free
 *
 * Free a QP
 *
 * Input:
 *	ia_handle	IA handle
 *	*ep_ptr		pointer to EP INFO
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *  dapl_convert_errno
 *
 */
DAT_RETURN dapls_ib_qp_free(IN DAPL_IA *ia_ptr, IN DAPL_EP *ep_ptr)
{
	dp_ib_cm_handle_t pConn;
	HRESULT hr;
	DAPL_ND_QP *h_qp;

	dapl_os_assert(ep_ptr->qp_handle);

	pConn = dapl_get_cm_from_ep(ep_ptr);	// can be <null>

	dapl_log(DAPL_DBG_TYPE_EP, "%s(EP %lx) pConn %lx h_QP %lx\n",
		 __FUNCTION__, ep_ptr, pConn, ep_ptr->qp_handle );

	dapl_os_lock(&ep_ptr->header.lock);

	if ( !ep_ptr->qp_handle ) {
		dapl_os_unlock(&ep_ptr->header.lock);
		return DAT_SUCCESS;
	}
	h_qp = ep_ptr->qp_handle;	// local copy
	ep_ptr->qp_state = DAPL_QP_STATE_ERROR;
	dapl_os_unlock(&ep_ptr->header.lock);

	dapls_ep_flush_cqs(ep_ptr);
	ep_ptr->qp_handle = NULL;

	if (h_qp->ND_QP) {
		hr = h_qp->ND_QP->lpVtbl->Flush( h_qp->ND_QP );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() QP Flush() ERR %s @ line #%d\n",
				__FUNCTION__, ND_error_str(hr), __LINE__);
		}
		hr = h_qp->ND_QP->lpVtbl->Release( h_qp->ND_QP );
		if ( FAILED(hr) ) {
			dapl_log(DAPL_DBG_TYPE_ERR,
				"%s() Release->Endpoint ERR %s @ line #%d\n",
				__FUNCTION__, ND_error_str(hr), __LINE__);
		}
		h_qp->ND_QP = NULL;
	}

#if TRACK_POSTED_RECVS
	//dapl_os_assert(dapl_os_atomic_read(&h_qp->posted_recvs) >= 0);
	if (dapl_os_atomic_read(&h_qp->posted_recvs) > 0) {
		ULONG pr=dapl_os_atomic_read(&h_qp->posted_recvs);

		fprintf(stderr,"%s(%lx) posted recvs %d?\n",__FUNCTION__,h_qp,pr);
		fflush(stderr);
		//dapl_os_assert(dapl_os_atomic_read(&h_qp->posted_recvs) == 0);
	}
#endif
	h_qp->pConn = NULL;

	if (pConn)
		pConn->h_qp = NULL;

	/* erase back links from bound CQ to this QP */
	h_qp->pSendCQ = NULL;
	h_qp->pRecvCQ = NULL;

	dapl_os_free((void*)h_qp, sizeof(*h_qp));

	return DAT_SUCCESS;
}

/*
 * dapl_ib_qp_modify
 *
 * Set the QP to the parameters specified in an EP_PARAM
 *
 * The EP_PARAM structure that is provided has been
 * sanitized such that only non-zero values are valid.
 *
 * Input:
 *	ib_hca_handle		HCA handle
 *	qp_handle		QP handle
 *	ep_attr		        Sanitized EP Params
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
DAT_RETURN
dapls_ib_qp_modify(IN DAPL_IA * ia_ptr, IN DAPL_EP * ep_ptr, IN DAT_EP_ATTR * attr)
{
	/* 
	 * EP state, qp_handle state should be an indication
	 * of current state but the only way to be sure is with
	 * a user mode ibv_query_qp call which is NOT available 
	 */

	/* Adjust to current EP attributes */

	dapl_dbg_log(DAPL_DBG_TYPE_ERR/*EP*/, "%s() qp %p sq %d,%d, rq %d,%d\n",
			__FUNCTION__,
			ep_ptr->qp_handle,
			attr->max_request_dtos, attr->max_request_iov,
			attr->max_recv_dtos, attr->max_recv_iov);

	return DAT_SUCCESS;
}


/*
 * dapls_ib_reinit_ep
 *
 * Move the QP to INIT state again.
 *
 * Input:
 *	ep_ptr		DAPL_EP
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	void
 *
 */
void
dapls_ib_reinit_ep ( IN DAPL_EP  *ep_ptr )
{
	HRESULT hr;
	DAPL_ND_QP *h_qp;

	dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		"%s(EP=%lx)->ib_qp_handle(%lx) qp_state %d\n",
			__FUNCTION__, ep_ptr->qp_handle, ep_ptr->qp_state); 

	dapl_os_assert( ep_ptr->qp_handle );

	h_qp = ep_ptr->qp_handle;

	/* NetworkDirect does not allow QP reuse. Delete the current QP and
	 * create a new QP.
	 */
	dapl_os_assert( h_qp->ND_QP );

	hr = h_qp->ND_QP->lpVtbl->Release( h_qp->ND_QP ); 

	hr = h_qp->pAdapter->lpVtbl->CreateQueuePair(h_qp->pAdapter, 
						     &IID_IND2QueuePair,
						     (IUnknown*)h_qp->pRecvCQ->ND_CQ,
						     (IUnknown*)h_qp->pSendCQ->ND_CQ,
						     (VOID*) h_qp,
						     h_qp->pRecvCQ->depth,
						     h_qp->pSendCQ->depth,
						     h_qp->recv_max_sge,
						     h_qp->send_max_sge,
						     h_qp->max_inline_send,
						     &h_qp->ND_QP );
	if ( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s(EP %lx) CreateQueuePair(h_qp %lx) ERR %s @ line #%d\n",
				__FUNCTION__, ep_ptr, h_qp,
				ND_error_str(hr), __LINE__);
	}

	dapl_os_lock(&ep_ptr->header.lock);
	ep_ptr->qp_state = DAPL_QP_STATE_UNCONNECTED; 
	dapl_os_unlock(&ep_ptr->header.lock);
}


/*
 * dapl_ib_pd_alloc
 *
 * Alloc a PD
 *
 * Input:
 *	ia_handle		IA handle
 *	PZ_ptr			pointer to PZEVD struct
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_pd_alloc (
        IN  DAPL_IA                 *ia,
        IN  DAPL_PZ                 *pz)
{
	pz->pd_handle = (ib_pd_handle_t) dapl_os_alloc( sizeof(void*) );
	return DAT_SUCCESS;
}


/*
 * dapl_ib_pd_free
 *
 * Free a PD
 *
 * Input:
 *	PZ_ptr			pointer to PZ struct
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_pd_free (
        IN  DAPL_PZ                 *pz)
{
	dapl_os_assert( pz->pd_handle != IB_INVALID_HANDLE );
	dapl_os_free( pz->pd_handle, sizeof(void*) );
	pz->pd_handle = IB_INVALID_HANDLE;

	return DAT_SUCCESS;
}


/*
 * dapl_ib_mr_register
 *
 * Register a virtual memory region
 *
 * Input:
 *	ia_handle		IA handle
 *	lmr			pointer to dapl_lmr struct
 *	virt_addr		virtual address of beginning of mem region
 *	length			length of memory region
 *	privileges		access flags
 *	va_type			?
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_mr_register(
        IN  DAPL_IA                 *ia_ptr,
        IN  DAPL_LMR                *lmr,
        IN  DAT_PVOID               virt_addr,
        IN  DAT_VLEN                length,
        IN  DAT_MEM_PRIV_FLAGS      privileges,
        IN  DAT_VA_TYPE             va_type)
{
	HRESULT hr;
	DAPL_ND_MR *h_mr;
	IND2Adapter *pAdapter;
	IND2MemoryRegion *pND_mr;
	SIZE_T BytesRet, Len = (SIZE_T)length;
	ULONG flags=0;
	ib_hca_transport_t *pND_transport;

	if ( ia_ptr == NULL || ia_ptr->header.magic != DAPL_MAGIC_IA )
		return DAT_INVALID_HANDLE;

	/* TODO: shared memory */
	if (lmr->param.mem_type == DAT_MEM_TYPE_SHARED_VIRTUAL) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() mr_register_shared: NOT IMPLEMENTED\n", __FUNCTION__);
		return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
	}

	/* IBAL does not support */
	if (va_type == DAT_VA_TYPE_ZB) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() va_type == DAT_VA_TYPE_ZB: NOT SUPPORTED\n",
				__FUNCTION__);
		return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);  
	}

	if ( lmr->mr_handle ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() lmr_handle ! NULL\n", __FUNCTION__);
		return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);  
	}

	pAdapter = ((DAPL_IA*)lmr->param.ia_handle)->hca_ptr->ib_hca_handle;
	dapl_os_assert( pAdapter );

	pND_transport = &((DAPL_IA*)lmr->param.ia_handle)->hca_ptr->ib_trans;
	dapl_os_assert( pND_transport );

	if ( Len >= pND_transport->nd_adapter_info.MaxRegistrationSize ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() mr reg length %ld > Reg Max %ld\n", __FUNCTION__,
			Len, pND_transport->nd_adapter_info.MaxRegistrationSize);
				
		return DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_MEMORY_REGION);  
	}

    	h_mr = dapl_os_alloc(sizeof(DAPL_ND_MR));
	if ( !h_mr )
		return DAT_INSUFFICIENT_RESOURCES;
	memset( h_mr, 0, sizeof(DAPL_ND_MR) );

	h_mr->OverLap.Ov.hEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	if( h_mr->OverLap.Ov.hEvent == NULL )
	{
        	dapl_log(DAPL_DBG_TYPE_ERR, "%s() Failed to allocate "
			"h_mr->OverLap.hEvent @ line #%d\n",
				__FUNCTION__,__LINE__ );
		dapl_os_free( h_mr, sizeof(*h_mr) );
		return DAT_INSUFFICIENT_RESOURCES;
	}
	h_mr->OverLap.ior_type = IOR_MemoryRegion;
	h_mr->OverLap.context = (void*) h_mr;
	/* mark Overlap event as serial only - no IOCompletion callback when used */
	h_mr->OverLap.Ov.hEvent = (HANDLE) ((ULONG)h_mr->OverLap.Ov.hEvent | 1);


	if (privileges & DAT_MEM_PRIV_ALL_FLAG)
		flags = ND_MR_FLAG_ALLOW_LOCAL_WRITE |
			ND_MR_FLAG_ALLOW_REMOTE_READ |
			ND_MR_FLAG_ALLOW_REMOTE_WRITE;
	else {

		flags |= ((privileges & DAT_MEM_PRIV_LOCAL_WRITE_FLAG)
				? ND_MR_FLAG_ALLOW_LOCAL_WRITE : 0);

		flags |= ((privileges & DAT_MEM_PRIV_REMOTE_READ_FLAG)
				? ND_MR_FLAG_ALLOW_REMOTE_READ : 0);

		flags |= ((privileges & DAT_MEM_PRIV_REMOTE_WRITE_FLAG)
				? ND_MR_FLAG_ALLOW_REMOTE_WRITE : 0);
	}

	hr = pAdapter->lpVtbl->CreateMemoryRegion( pAdapter,
						   &IID_IND2MemoryRegion,
						  pND_transport->AdapterOvFileHandle,
						   &h_mr->ND_MR );
	if( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() CreateMemoryRegion %s @ line # %d\n",
				__FUNCTION__, ND_error_str(hr), __LINE__ );
		
		h_mr->OverLap.Ov.hEvent = (HANDLE)
					((ULONG)h_mr->OverLap.Ov.hEvent & ~1);
		CloseHandle( h_mr->OverLap.Ov.hEvent );
		dapl_os_free( h_mr, sizeof(*h_mr) );
		return dapl_cvt_ND_to_DAT_status(hr);
	}

	pND_mr = h_mr->ND_MR;
	hr = pND_mr->lpVtbl->Register(  pND_mr,
					virt_addr,
					Len,
					flags,
					&h_mr->OverLap.Ov );
	if( hr == ND_PENDING ) {
		hr = pND_mr->lpVtbl->GetOverlappedResult( pND_mr,
							  &h_mr->OverLap.Ov,
							  TRUE );
	}
	if( FAILED(hr) ) {
		HRESULT hr1=hr;
		DAT_RETURN status = dapl_cvt_ND_to_DAT_status(hr);

		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() NDRegister: %s (Dat_Status type %#x subtype %#x)"
			"\n  @ line %d flags %#x len %d vaddr %p\n",
				__FUNCTION__, ND_error_str(hr), DAT_GET_TYPE(status),
				DAT_GET_SUBTYPE(status), __LINE__,
				flags, length, virt_addr );
		
		hr = pND_mr->lpVtbl->Release( pND_mr );
		if( hr == ND_PENDING ) {
			hr = pND_mr->lpVtbl->GetOverlappedResult( pND_mr,
							  	  &h_mr->OverLap.Ov,
							  	  TRUE );
		}
		h_mr->OverLap.Ov.hEvent = (HANDLE)
					((ULONG)h_mr->OverLap.Ov.hEvent & ~1);
		CloseHandle( h_mr->OverLap.Ov.hEvent );
		dapl_os_free( h_mr, sizeof(*h_mr) );
		return dapl_cvt_ND_to_DAT_status(hr1);
	}

	// lkey
	lmr->param.lmr_context = (DAT_LMR_CONTEXT)
					pND_mr->lpVtbl->GetLocalToken(pND_mr);
	dapl_os_assert( lmr->param.lmr_context );
	// rkey
	lmr->param.rmr_context = (DAT_RMR_CONTEXT)
					pND_mr->lpVtbl->GetRemoteToken(pND_mr);
	dapl_os_assert( lmr->param.rmr_context );

	lmr->param.registered_size = length;
	lmr->param.registered_address = (DAT_VADDR) (uintptr_t) virt_addr;
	lmr->mr_handle = h_mr;

	dapl_dbg_log (DAPL_DBG_TYPE_UTIL,
		"%s(lmr %lx)\n   VA %p len %d lmr_context %#x rmr_context %#x\n",
			__FUNCTION__, lmr, virt_addr, length,
			lmr->param.lmr_context,
			lmr->param.rmr_context);

	return DAT_SUCCESS;
}


/*
 * dapl_ib_mr_deregister
 *
 * Free a memory region
 *
 * Input:
 *	lmr			pointer to dapl_lmr struct
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_mr_deregister( IN DAPL_LMR *lmr )
{
	DAT_RETURN dat_status=DAT_SUCCESS;
	HRESULT hr;
	IND2Adapter *pAdapter;
	DAPL_ND_MR *h_mr;
	ND2_RESULT nd_result;
	SIZE_T BytesRet;
	INDEndpoint *pEndpoint;

	if ( lmr->mr_handle == NULL ) {
		dapl_dbg_log (DAPL_DBG_TYPE_UTIL,
		"%s() deregister NULL lmr ?\n", __FUNCTION__);
		return DAT_SUCCESS;
	}

	dapl_dbg_log (DAPL_DBG_TYPE_UTIL,
		"%s() lmr_context = %#x\n", __FUNCTION__, lmr->param.lmr_context);

	h_mr = lmr->mr_handle;
	pAdapter = ((DAPL_IA*)lmr->param.ia_handle)->hca_ptr->ib_hca_handle;
	dapl_os_assert( pAdapter );

	dapl_os_assert( h_mr->ND_MR );

	hr = h_mr->ND_MR->lpVtbl->Deregister( h_mr->ND_MR, &h_mr->OverLap.Ov );

	if( hr == ND_PENDING ) {
		hr = h_mr->ND_MR->lpVtbl->GetOverlappedResult( h_mr->ND_MR,
							       &h_mr->OverLap.Ov,
							       TRUE );
	}
	if( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() MR DeregisterMemory %s @ line # %d\n",
				__FUNCTION__, ND_error_str(hr), __LINE__ );
		
		h_mr->OverLap.Ov.hEvent = (HANDLE)
					((ULONG)h_mr->OverLap.Ov.hEvent & ~1);
		CloseHandle( h_mr->OverLap.Ov.hEvent );
		dapl_os_free( h_mr, sizeof(*h_mr) );
		return dapl_cvt_ND_to_DAT_status(hr);
	}

	hr = h_mr->ND_MR->lpVtbl->Release( h_mr->ND_MR );
	if( FAILED(hr) ) {
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() MR Release %s @ line # %d\n",
				__FUNCTION__, ND_error_str(hr), __LINE__ );
	}

	h_mr->OverLap.Ov.hEvent = (HANDLE) ((ULONG)h_mr->OverLap.Ov.hEvent & ~1);
	CloseHandle( h_mr->OverLap.Ov.hEvent );

	dapl_os_free( h_mr, sizeof(*h_mr) );

	lmr->param.lmr_context = 0;
	lmr->mr_handle = NULL;

	dapl_dbg_log (DAPL_DBG_TYPE_UTIL, "%s(lmr %lx) VA %lx len %d\n",__FUNCTION__,
		lmr, lmr->param.registered_address, lmr->param.registered_size);

	return DAT_SUCCESS;
}


/*
 * dapl_ib_mr_register_shared
 *
 * Register a virtual memory region
 *
 * Input:
 *	ia_handle		IA handle
 *	lmr			pointer to dapl_lmr struct
 *	virt_addr		virtual address of beginning of mem region
 *	length			length of memory region
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_mr_register_shared (
        IN  DAPL_IA                  *ia,
        IN  DAPL_LMR                 *lmr,
        IN  DAT_MEM_PRIV_FLAGS       privileges,
        IN  DAT_VA_TYPE              va_type )
{
	dapl_dbg_log(DAPL_DBG_TYPE_ERR, "%s() NOT IMPLEMENTED\n", __FUNCTION__);

	return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
}


/*
 * dapls_ib_mw_alloc
 *
 * Bind a protection domain to a memory window
 *
 * Input:
 *	rmr	Initialized rmr to hold binding handles
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN dapls_ib_mw_alloc(IN DAPL_RMR * rmr)
{

	dapl_dbg_log(DAPL_DBG_TYPE_ERR, " mw_alloc: NOT IMPLEMENTED\n");

	return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
}

/*
 * dapls_ib_mw_free
 *
 * Release bindings of a protection domain to a memory window
 *
 * Input:
 *	rmr	Initialized rmr to hold binding handles
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_STATE
 *
 */
DAT_RETURN dapls_ib_mw_free(IN DAPL_RMR * rmr)
{
	dapl_dbg_log(DAPL_DBG_TYPE_ERR, " mw_free: NOT IMPLEMENTED\n");

	return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
}

/*
 * dapls_ib_mw_bind
 *
 * Bind a protection domain to a memory window
 *
 * Input:
 *	rmr	Initialized rmr to hold binding handles
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_PARAMETER;
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_mw_bind(IN DAPL_RMR * rmr,
		 IN DAPL_LMR * lmr,
		 IN DAPL_EP * ep,
		 IN DAPL_COOKIE * cookie,
		 IN DAT_VADDR virtual_address,
		 IN DAT_VLEN length,
		 IN DAT_MEM_PRIV_FLAGS mem_priv, IN DAT_BOOLEAN is_signaled)
{
	dapl_dbg_log(DAPL_DBG_TYPE_ERR, " mw_bind: NOT IMPLEMENTED\n");

	return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
}

/*
 * dapls_ib_mw_unbind
 *
 * Unbind a protection domain from a memory window
 *
 * Input:
 *	rmr	Initialized rmr to hold binding handles
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *    	DAT_INVALID_PARAMETER;
 *   	DAT_INVALID_STATE;
 *	DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_mw_unbind(IN DAPL_RMR * rmr,
		   IN DAPL_EP * ep,
		   IN DAPL_COOKIE * cookie, IN DAT_BOOLEAN is_signaled)
{
	dapl_dbg_log(DAPL_DBG_TYPE_ERR, " mw_unbind: NOT IMPLEMENTED\n");

	return DAT_ERROR(DAT_NOT_IMPLEMENTED, DAT_NO_SUBTYPE);
}


/*
 * dapls_ib_setup_async_callback
 *
 * Set up an asynchronous callbacks of various kinds
 *
 * Input:
 *	ia_handle		IA handle
 *	handler_type		type of handler to set up
 *	callback_handle		handle param for completion callbacks
 *	callback		callback routine pointer
 *	context			argument for callback routine
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
DAT_RETURN
dapls_ib_setup_async_callback (
        IN  DAPL_IA                     *ia_ptr,
        IN  DAPL_ASYNC_HANDLER_TYPE     handler_type,
        IN  DAPL_EVD                    *evd_ptr,
        IN  ib_async_handler_t          callback,
        IN  void                        *context )
{
#if 0	// NetworkDirect does not support async handlers.
	static char *hdl_type_str[4] = {"DAPL_ASYNC_UNAFILIATED",
					"DAPL_ASYNC_CQ_ERROR",
					"DAPL_ASYNC_CQ_COMPLETION",
					"DAPL_ASYNC_QP_ERROR"};
	dapl_dbg_log (DAPL_DBG_TYPE_ERR/*UTIL*/,
                  "\n%s(): ia %lx type %s evd %lx ctx %p\n",
			__FUNCTION__, ia_ptr, hdl_type_str[handler_type],
			evd_ptr, context);
#endif
	return DAT_SUCCESS;

#if 0
    dapl_ibal_ca_t     *p_ca;
    dapl_ibal_evd_cb_t *evd_cb;

    dapl_dbg_log (DAPL_DBG_TYPE_UTIL,
                  " setup_async_cb: ia %p type %d hdl %p cb %p ctx %p\n",
                  ia_ptr, handler_type, evd_ptr, callback, context);

    p_ca = (dapl_ibal_ca_t *) ia_ptr->hca_ptr->ib_hca_handle;

    if (p_ca == NULL)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR, "--> DsISAC: can't find %s HCA\n", 
                       (ia_ptr->header.provider)->device_name);
        return (DAT_INVALID_HANDLE);
    }
    return DAT_SUCCESS;
#endif   
}


#if 0 // XXX
/*
 * dapls_ib_query_gid
 *
 * Query the hca for the gid of the 1st active port.
 *
 * Input:
 *	hca_handl		hca handle	
 *	ep_attr			attribute of the ep
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_HANDLE
 *	DAT_INVALID_PARAMETER
 */

DAT_RETURN
dapls_ib_query_gid( IN  DAPL_HCA	*hca_ptr,
		    IN  GID		*gid )
{
    dapl_ibal_ca_t    *p_ca;
    ib_ca_attr_t      *p_hca_attr;
    ib_api_status_t   ib_status;
    ib_hca_port_t     port_num;

    p_ca = (dapl_ibal_ca_t *) hca_ptr->ib_hca_handle;

    if (p_ca == NULL)
    {
        dapl_dbg_log ( DAPL_DBG_TYPE_ERR,
			"%s() invalid hca_ptr %p", __FUNCTION__, hca_ptr);
        return DAT_INVALID_HANDLE;
    }

    gid->gid_prefix = p_hca_attr->p_port_attr[port_num].p_gid_table->unicast.prefix;
    gid->guid = p_hca_attr->p_port_attr[port_num].p_gid_table->unicast.interface_id;
    return DAT_SUCCESS;
}
#endif

#if 0

void dapli_print_ep_attrs(DAT_EP_ATTR *ep_attr )
{
	if (ep_attr == NULL)
		return;

	printf("DAT_ATTR ep %p  \n",ep_attr);
	printf("  service_type %d\n", ep_attr->service_type);
	printf("  max_mtu/max_message_size %u\n", ep_attr->max_message_size);
	printf("  max_rdma_size %u\n", ep_attr->max_rdma_size);
	printf("  qos %d\n", ep_attr->qos);
	printf("  recv_completion_flags %#x\n", ep_attr->recv_completion_flags);
	printf("  request_completion_flags %#x\n",ep_attr->request_completion_flags);
	printf("  max_recv_dtos %d\n", ep_attr->max_recv_dtos);
	printf("  max_request_dtos %d\n", ep_attr->max_request_dtos);
	printf("  max_recv_iov %d\n", ep_attr->max_recv_iov);
	printf("  max_request_iov %d\n", ep_attr->max_request_iov);
	printf("  max_rdma_read_in %d\n", ep_attr->max_rdma_read_in);
	printf("  max_rdma_read_out %d\n", ep_attr->max_rdma_read_out);
	printf("  ep_transport_specific_count %d\n",
		ep_attr->ep_transport_specific_count);
	printf("  ep_provider_specific_count %d\n",
		ep_attr->ep_provider_specific_count);
}
#endif

#if 0
ep_sane(DAPL_EP *ep_ptr, DAT_EP_ATTR *ep_attr)
{
	extern DAT_RETURN dapl_ep_check_recv_completion_flags (
    						DAT_COMPLETION_FLAGS        flags );
		// sanity checking per dat_ep_create()
		dapli_print_ep_attrs(ep_attr);

		if (ep_attr != NULL && (
#ifndef DAT_EXTENSIONS
		       ep_attr->service_type != DAT_SERVICE_TYPE_RC ||
#endif
	       (ep_ptr->param.recv_evd_handle == DAT_HANDLE_NULL
			   && ep_attr->max_recv_dtos != 0)
	       || (ep_ptr->param.recv_evd_handle != DAT_HANDLE_NULL
			   && ep_attr->max_recv_dtos == 0)
	       || (ep_ptr->param.request_evd_handle == DAT_HANDLE_NULL
		   	   && ep_attr->max_request_dtos != 0)
	       || (ep_ptr->param.request_evd_handle != DAT_HANDLE_NULL
			   && ep_attr->max_request_dtos == 0)
	       || (ep_ptr->param.recv_evd_handle != DAT_HANDLE_NULL
			   && ep_attr->max_recv_iov == 0)
	       || (ep_ptr->param.request_evd_handle == DAT_HANDLE_NULL
			   && ep_attr->max_request_iov != 0)
	       || (DAT_SUCCESS != dapl_ep_check_recv_completion_flags
					   (ep_attr->recv_completion_flags))))
		{
			dapl_os_assert( 0 );
		}		
}
#endif

/*
 * dapls_ib_query_hca
 *
 * Query the hca attribute
 *
 * Input:
 *	hca_handl		hca handle	
 *	ep_attr			attributes of the ep
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_INVALID_PARAMETER
 */

DAT_RETURN dapls_ib_query_hca (
	IN  DAPL_HCA                       *hca_ptr,
        OUT DAT_IA_ATTR                    *ia_attr,
        OUT DAT_EP_ATTR                    *ep_attr,
	OUT DAT_SOCK_ADDR6                 *ip_addr)
{
	ND2_ADAPTER_INFO *nd = &hca_ptr->ib_trans.nd_adapter_info;

	if (hca_ptr->ib_hca_handle == NULL) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR, "%s: query_hca: BAD hca handle\n",
				__FUNCTION__);
		return DAT_INVALID_HANDLE;
	}

	/* local IP address of device, set during ia_open */
	if ( ip_addr )
		memcpy(ip_addr, &hca_ptr->hca_address, sizeof(DAT_SOCK_ADDR6));

	if (ia_attr != NULL) {
		(void)dapl_os_memzero(ia_attr, sizeof(*ia_attr));
		strncpy( ia_attr->adapter_name, hca_ptr->ib_trans.dev_name,
			 DAT_NAME_MAX_LENGTH - 1);
		strncpy( ia_attr->vendor_name, "Microsoft NetworkDirect",
			 DAT_NAME_MAX_LENGTH - 1);

		ia_attr->ia_address_ptr = (DAT_IA_ADDRESS_PTR) &hca_ptr->hca_address;

		dapl_dbg_log(DAPL_DBG_TYPE_UTIL, " query_hca: %s @ %s \n",
			     hca_ptr->ib_trans.dev_name,
			     inet_ntoa(((struct sockaddr_in *)
					&hca_ptr->hca_address)->sin_addr));

		ia_attr->hardware_version_major = nd->VendorId;
		ia_attr->hardware_version_minor = nd->DeviceId;
		// fantasy vs. reality of ND device info?

		/*dev_attr.max_qp*/
		ia_attr->max_eps = nd->MaxInitiatorQueueDepth;

		/*dev_attr.max_qp_wr*/
		ia_attr->max_dto_per_ep = nd->MaxInitiatorQueueDepth;

		/*dev_attr.max_qp_rd_atom*/
		ia_attr->max_rdma_read_in = nd->MaxInboundReadLimit;

		/*dev_attr.max_qp_init_rd_atom*/
		ia_attr->max_rdma_read_out = nd->MaxOutboundReadLimit;

		/*dev_attr.max_qp_rd_atom*/
		ia_attr->max_rdma_read_per_ep_in = nd->MaxInboundReadLimit;

		/*dev_attr.max_qp_init_rd_atom*/;
		ia_attr->max_rdma_read_per_ep_out = nd->MaxOutboundReadLimit;

		ia_attr->max_rdma_read_per_ep_in_guaranteed = DAT_TRUE;
		ia_attr->max_rdma_read_per_ep_out_guaranteed = DAT_TRUE;

		/*dev_attr.max_cq*/
		ia_attr->max_evds = nd->MaxInitiatorQueueDepth;

		/*dev_attr.max_cqe*/
		ia_attr->max_evd_qlen = nd->MaxInitiatorQueueDepth;

		/*dev_attr.max_sge*/;
		ia_attr->max_iov_segments_per_dto = nd->MaxInitiatorSge;

		/*dev_attr.max_mr*/
		ia_attr->max_lmrs =
			(DAT_COUNT) nd->MaxInboundReadLimit/*MaxInboundRequests*/;

		/* 32bit attribute from 64bit, 4G-1 limit, DAT v2 needs fix */
		/*(dev_attr.max_mr_size >> 32) ? ~0 : dev_attr.max_mr_size*/
		ia_attr->max_lmr_block_size = nd->MaxRegistrationSize;

		/*dev_attr.max_mw - match other providers */
		ia_attr->max_rmrs = (DAT_COUNT) 0 /*MaxOutboundRequests?*/;

		/*dev_attr.max_mr_size*/
		ia_attr->max_lmr_virtual_address = nd->MaxRegistrationSize;

		/*dev_attr.max_mr_size*/
		ia_attr->max_rmr_target_address = nd->MaxRegistrationSize;

		/*dev_attr.max_pd*/
		ia_attr->max_pzs = nd->MaxInitiatorQueueDepth;

		/*port_attr.max_msg_sz*/
		ia_attr->max_message_size = nd->MaxTransferLength;

		/*port_attr.max_msg_sz*/
		ia_attr->max_rdma_size = nd->MaxTransferLength;

		/*dev_attr.max_sge*/
		ia_attr->max_iov_segments_per_rdma_read = nd->MaxReadSge;

		/*dev_attr.max_sge*/
		ia_attr->max_iov_segments_per_rdma_write = nd->MaxInitiatorSge;

		ia_attr->num_transport_attr = 0;
		ia_attr->transport_attr = NULL;
		ia_attr->num_vendor_attr = 0;
		ia_attr->vendor_attr = NULL;
#ifdef DAT_EXTENSIONS
		ia_attr->extension_supported = DAT_EXTENSION_IB;
		ia_attr->extension_version = DAT_IB_EXTENSION_VERSION;
#endif
		hca_ptr->ib_trans.mtu = IBV_MTU_4096; // guessing

		/* set MTU in transport specific named attribute */
		hca_ptr->ib_trans.named_attr.name = "DAT_IB_TRANSPORT_MTU";
		hca_ptr->ib_trans.named_attr.value =
					dapl_ib_mtu_str(hca_ptr->ib_trans.mtu);

		dapl_log(DAPL_DBG_TYPE_UTIL, " query_hca: ib_trans.global %d\n",
				hca_ptr->ib_trans.global);

		dapl_log(DAPL_DBG_TYPE_UTIL,
			     " query_hca: (%x.%x) eps %d, sz %d evds %d,"
			     " max_evd_qlen %d mtu %d\n",
			     ia_attr->hardware_version_major,
			     ia_attr->hardware_version_minor,
			     ia_attr->max_eps, ia_attr->max_dto_per_ep,
			     ia_attr->max_evds, ia_attr->max_evd_qlen,
			     128 << hca_ptr->ib_trans.mtu);

		dapl_log(DAPL_DBG_TYPE_UTIL,
			     " query_hca: msg %llu rdma %llu iov %d lmr %d rmr %d"
			     " mr %u\n",
			     ia_attr->max_message_size, ia_attr->max_rdma_size,
			     ia_attr->max_iov_segments_per_dto,
			     ia_attr->max_lmrs, ia_attr->max_rmrs,
			     ia_attr->max_lmr_block_size);
	}

	if (ep_attr != NULL) {
		(void)dapl_os_memzero(ep_attr, sizeof(*ep_attr));

		ep_attr->service_type = DAT_SERVICE_TYPE_RC;

		/*port_attr.max_msg_sz*/
		ep_attr->max_message_size = nd->MaxTransferLength;

		/*port_attr.max_msg_sz*/
		ep_attr->max_rdma_size = nd->MaxTransferLength;

		/*dev_attr.max_qp_wr*/
		ep_attr->max_recv_dtos = DAPL_MIN(MAXINT, nd->MaxInitiatorQueueDepth);

		/*dev_attr.max_qp_wr*/
		ep_attr->max_request_dtos = DAPL_MIN(MAXINT,nd->MaxInitiatorQueueDepth);

		/*dev_attr.max_sge*/
		ep_attr->max_recv_iov = DAPL_MIN(MAXINT,nd->MaxReceiveSge);

		/*dev_attr.max_sge*/
		ep_attr->max_request_iov = DAPL_MIN(MAXINT,nd->MaxInitiatorSge);

		/*dev_attr.max_qp_rd_atom*/
		ep_attr->max_rdma_read_in = DAPL_MIN(MAXINT,nd->MaxInboundReadLimit);

		/*dev_attr.max_qp_init_rd_atom*/
		ep_attr->max_rdma_read_out = DAPL_MIN(MAXINT,nd->MaxInboundReadLimit);

		ep_attr->srq_soft_hw = 0; // XXX ?

		/*dev_attr.max_sge*/
		ep_attr->max_rdma_read_iov = DAPL_MIN(MAXINT,nd->MaxReceiveSge);

		/*dev_attr.max_sge*/
		ep_attr->max_rdma_write_iov = DAPL_MIN(MAXINT,nd->MaxInitiatorSge);

		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
			     " query_hca: MAX msg %u mtu %d qsz %d iov %d"
			     " rdma i%d,o%d\n",
			     ep_attr->max_message_size,
			     128 << hca_ptr->ib_trans.mtu,
			     ep_attr->max_recv_dtos, 
			     ep_attr->max_recv_iov,
			     ep_attr->max_rdma_read_in,
			     ep_attr->max_rdma_read_out);
	}
	return DAT_SUCCESS;
}


static _INLINE_ uint32_t dapli_ND_to_cqe_opcode(ND2_REQUEST_TYPE req_type)
{
	switch( req_type )
	{
	  case Nd2RequestTypeReceive:
		return OP_RECEIVE;
		
	  case Nd2RequestTypeSend:
		return OP_SEND;
		
	  case Nd2RequestTypeBind:
		return OP_BIND_MW;
		
	  case Nd2RequestTypeRead:
		return OP_RDMA_READ;
		
	  case Nd2RequestTypeWrite:
		return OP_RDMA_WRITE;
		
	  case Nd2RequestTypeInvalidate: 
	  default:
		break;
	}
	return OP_INVALID;
}


/*
 * FIXME - Vu
 *     Now we only poll for one cqe. We can poll for more than
 *     one completions later for better. However, this requires
 *     to change the logic in dapl_evd_dto_callback function
 *     to process more than one completion.
 *
 * Returns:
 *	DAT_SUCCESS
 *	DAT_QUEUE_EMPTY
 */

DAT_RETURN
dapls_ib_completion_poll ( IN DAPL_HCA                *p_hca,
                           IN DAPL_EVD                *p_evd,
                           IN ib_work_completion_t    *cqe_ptr )
{
	SIZE_T nResults;
	DAPL_ND_CQ *h_Cq;
	ND2_RESULT Result;

	if ( !p_hca->ib_hca_handle ) {
		return DAT_ERROR(DAT_INVALID_HANDLE,DAT_INVALID_HANDLE_IA);
	}

	if ( !p_evd )
		return DAT_ERROR(DAT_INVALID_HANDLE,DAT_INVALID_HANDLE_EVD_RECV);

	if (DAPL_BAD_HANDLE(p_evd, DAPL_MAGIC_EVD))
		return DAT_ERROR(DAT_INVALID_HANDLE,0);

	h_Cq = p_evd->ib_cq_handle;
	dapl_os_assert( h_Cq );
	dapl_os_assert( cqe_ptr );

	nResults = h_Cq->ND_CQ->lpVtbl->GetResults( h_Cq->ND_CQ, &Result, 1 );

	if ( nResults == 0 )
		return DAT_QUEUE_EMPTY;

	if ( nResults != 1 ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s(%lx) Results(%ld) != 0|1?\n",
					__FUNCTION__,h_Cq,nResults);
		return DAT_QUEUE_EMPTY;
	}

	cqe_ptr->wr_id = (DAT_UINT64) Result.RequestContext;
	cqe_ptr->opcode = dapli_ND_to_cqe_opcode(Result.RequestType);

#if TRACK_RDMA_WRITES
	if (cqe_ptr->opcode == OP_SEND) {
		dapl_os_atomic_dec(&h_Cq->outstanding_sends);
	}
#endif
#if TRACK_POSTED_RECVS
	if (Result.RequestType == Nd2RequestTypeReceive) {
		dapl_os_assert( h_Cq->h_qp );
		dapl_os_atomic_dec(&h_Cq->h_qp->posted_recvs);
	}
#endif
	cqe_ptr->byte_len = (uint32_t) Result.BytesTransferred;
	cqe_ptr->status = (int) dapl_cvt_ND_to_DAT_status(Result.Status);
	cqe_ptr->vendor_err = (int) Result.Status;
#if DBG
	if (Result.Status != ND_SUCCESS && Result.Status != ND_CANCELED) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "%s(%lx) DAT_Status(%#x) ND_Status %s OP %s\n", __FUNCTION__,
			h_Cq, cqe_ptr->status,
			ND_error_str(cqe_ptr->vendor_err),
			 ND_RequestType_str(Result.RequestType));
	}
#endif
	cqe_ptr->wc_flags = 0;

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		"%s(CQ %lx) nResults %d %s len %d cookie " F64x "\n",
			__FUNCTION__, h_Cq, nResults,
			DTO_error_str(dapls_ib_get_dto_status(cqe_ptr)),
			cqe_ptr->byte_len, (void*)cqe_ptr->wr_id);

	// skip reporting err msg for flushed RX buffers
	if (cqe_ptr->status != DAT_SUCCESS && Result.Status != ND_CANCELED)
		dapl_dbg_log(DAPL_DBG_TYPE_ERR, "%s(CQ %lx) %s\n",
			__FUNCTION__, h_Cq, ND_error_str(Result.Status));

	//dump_sg_list(cqe_ptr->sg_list,cqe_ptr->numSGE);

	return DAT_SUCCESS;
}


DAT_RETURN
dapls_ib_completion_notify( IN ib_hca_handle_t         hca_handle,
                            IN DAPL_EVD                *p_evd,
                            IN ib_notification_type_t  type )
{
	HRESULT hr;
	DAT_RETURN dat_status;
	DWORD solic_notify;
	ib_cq_handle_t h_CQ = p_evd->ib_cq_handle;

    	if  ( !h_CQ )
        	return DAT_INVALID_HANDLE;
	dapl_os_assert( h_CQ->ND_CQ );

    	// solic_notify = (type == IB_NOTIFY_ON_SOLIC_COMP) ? DAT_TRUE : DAT_FALSE; 
	solic_notify = (type == IB_NOTIFY_ON_SOLIC_COMP)
				? ND_CQ_NOTIFY_SOLICITED
				: ND_CQ_NOTIFY_ANY; 

	h_CQ->OverLap.ior_type = IOR_CQNotify;
	h_CQ->OverLap.context = p_evd;
	dapl_os_atomic_inc(&h_CQ->armed);

	hr = h_CQ->ND_CQ->lpVtbl->Notify( h_CQ->ND_CQ, solic_notify, &h_CQ->OverLap.Ov );

	if ( FAILED(hr) ) {
		dapl_os_atomic_dec(&h_CQ->armed);
		dapl_log(DAPL_DBG_TYPE_ERR,
			"%s() CQ-Notify(%x) %s @ line #%d\n",
				 __FUNCTION__, solic_notify, ND_error_str(hr),
				__LINE__);
		dat_status = DAT_ERROR(DAT_INSUFFICIENT_RESOURCES,DAT_RESOURCE_TEVD);
	}
	else {
		dat_status=DAT_SUCCESS;
		dapl_log(DAPL_DBG_TYPE_UTIL, "%s(%lx) %s\n",
			__FUNCTION__, h_CQ,
			(solic_notify == ND_CQ_NOTIFY_SOLICITED ?
				"NOTIFY_SOLICITED": "NOTIFY_ANY"));
	}

	return dat_status;

#if 0 // XXX OFED example
    ib_api_status_t        ib_status;
    DAT_BOOLEAN            solic_notify;

    if  ( !hca_handle )
    {
        return DAT_INVALID_HANDLE;
    }
    solic_notify = (type == IB_NOTIFY_ON_SOLIC_COMP) ? DAT_TRUE : DAT_FALSE; 
    ib_status = ib_rearm_cq ( p_evd->ib_cq_handle, solic_notify );

    return dapl_ib_status_convert (ib_status);
#endif
}


DAT_RETURN
dapls_evd_dto_wakeup( IN DAPL_EVD * evd_ptr)
{
#if 0 // XXX DBG
	DAT_RETURN dr = DAT_SUCCESS;

    	fprintf(stdout,"-->%s(%lx) Enter TO %u cno_ptr %#lx\n",
			__FUNCTION__,evd_ptr,evd_ptr->cno_ptr); fflush(stdout);

	//if( evd_ptr->cno_ptr ) be like IBAL XXX?
		dr = dapl_os_wait_object_wakeup(&evd_ptr->wait_object);

    	fprintf(stdout, "-->%s(%lx) Exit rc %x\n", __FUNCTION__,evd_ptr,dr);
	fflush(stdout);

	return dr;
#else
	return dapl_os_wait_object_wakeup(&evd_ptr->wait_object);
#endif
}

DAT_RETURN
dapls_evd_dto_wait (
	IN DAPL_EVD			*evd_ptr,
	IN uint32_t 			timeout)
{
#if 0 // XXX remove ASAP
	DAT_RETURN dr;
    	dapl_dbg_log (DAPL_DBG_TYPE_ERR, "-->%s(%lx) Enter TO %u\n",
			__FUNCTION__,evd_ptr);
	dr = dapl_os_wait_object_wait(&evd_ptr->wait_object, timeout);
    	dapl_dbg_log (DAPL_DBG_TYPE_ERR,
		"-->%s(%lx) Exit rc %x\n",__FUNCTION__,evd_ptr,dr);
	return dr;
#else
	return dapl_os_wait_object_wait(&evd_ptr->wait_object, timeout);
#endif
}


/*
 * dapls_ib_get_async_event
 *
 * Translate an asynchronous event type to the DAT event.
 * Note that different providers have different sets of errors.
 *
 * Input:
 *	cause_ptr		provider event cause
 *
 * Output:
 * 	async_event		DAT mapping of error
 *
 * Returns:
 * 	DAT_SUCCESS
 *	DAT_NOT_IMPLEMENTED	Caller is not interested this event
 */

DAT_RETURN dapls_ib_get_async_event(
	IN  ib_error_record_t		*cause_ptr,
	OUT DAT_EVENT_NUMBER		*async_event)
{
    DAT_RETURN			dat_status = DAT_NOT_IMPLEMENTED;

    return DAT_NOT_IMPLEMENTED;
}

/*
 * dapls_ib_get_dto_status
 *
 * Return the DAT status of a DTO operation
 *
 * Input:
 *	cqe_ptr			pointer to completion queue entry
 *
 * Output:
 * 	none
 *
 * Returns:
 *
 */

#define DTO_STAT(a,b)				\
	case a:					\
	ds = (DAT_DTO_COMPLETION_STATUS)(b);	\
	break

DAT_DTO_COMPLETION_STATUS
dapls_ib_get_dto_status( IN ib_work_completion_t *cqe_ptr )
{
	DAT_RETURN ds;

	switch( cqe_ptr->vendor_err )
	{
	    DTO_STAT(ND_SUCCESS, DAT_DTO_SUCCESS);
	    DTO_STAT(ND_FLUSHED, DAT_DTO_ERR_FLUSHED);
	    DTO_STAT(ND_TIMEOUT, DAT_DTO_ERR_RECEIVER_NOT_READY);
	    DTO_STAT(ND_PENDING, DAT_DTO_SUCCESS);
	    DTO_STAT(ND_BUFFER_OVERFLOW, DAT_DTO_ERR_LOCAL_LENGTH);
	    DTO_STAT(ND_DEVICE_BUSY, DAT_PORT_IN_USE);
	    DTO_STAT(ND_NO_MORE_ENTRIES, DAT_QUEUE_FULL);
	    DTO_STAT(ND_UNSUCCESSFUL, DAT_DTO_ERR_BAD_RESPONSE);
	    DTO_STAT(ND_ACCESS_VIOLATION, DAT_DTO_ERR_LOCAL_PROTECTION);
	    DTO_STAT(ND_INVALID_HANDLE, DAT_INVALID_HANDLE);
	    DTO_STAT(ND_INVALID_PARAMETER, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_NO_MEMORY, DAT_INSUFFICIENT_RESOURCES);
	    DTO_STAT(ND_INVALID_PARAMETER_MIX, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_DATA_OVERRUN, DAT_DTO_ERR_REMOTE_ACCESS);
	    DTO_STAT(ND_INSUFFICIENT_RESOURCES, DAT_INSUFFICIENT_RESOURCES);
	    DTO_STAT(ND_DEVICE_NOT_READY, DAT_INVALID_STATE);
	    DTO_STAT(ND_IO_TIMEOUT, DAT_TIMEOUT_EXPIRED);
	    DTO_STAT(ND_NOT_SUPPORTED, DAT_DTO_ERR_TRANSPORT);
	    DTO_STAT(ND_INTERNAL_ERROR, DAT_INTERNAL_ERROR);
	    DTO_STAT(ND_INVALID_PARAMETER_1, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_2, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_3, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_4, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_5, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_6, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_7, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_8, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_9, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_INVALID_PARAMETER_10, DAT_INVALID_PARAMETER);
	    DTO_STAT(ND_CANCELED, DAT_DTO_ERR_FLUSHED);
	    DTO_STAT(ND_REMOTE_ERROR,DAT_DTO_ERR_REMOTE_RESPONDER);
	    DTO_STAT(ND_INVALID_ADDRESS, DAT_DTO_ERR_LOCAL_PROTECTION);
	    DTO_STAT(ND_INVALID_DEVICE_STATE, DAT_DTO_ERR_TRANSPORT);
	    DTO_STAT(ND_INVALID_BUFFER_SIZE, DAT_DTO_ERR_LOCAL_LENGTH);
	    DTO_STAT(ND_TOO_MANY_ADDRESSES, DAT_DTO_ERR_TRANSPORT);
	    DTO_STAT(ND_ADDRESS_ALREADY_EXISTS, DAT_DTO_ERR_TRANSPORT);
	    DTO_STAT(ND_CONNECTION_REFUSED, DAT_DTO_ERR_TRANSPORT);
	    DTO_STAT(ND_CONNECTION_INVALID, DAT_INVALID_ADDRESS);
	    DTO_STAT(ND_CONNECTION_ACTIVE, DAT_CONN_QUAL_IN_USE);
	    DTO_STAT(ND_NETWORK_UNREACHABLE, DAT_INVALID_ADDRESS_UNREACHABLE);
	    DTO_STAT(ND_HOST_UNREACHABLE, DAT_INVALID_ADDRESS_UNREACHABLE);
	    DTO_STAT(ND_CONNECTION_ABORTED, DAT_ABORT);
	    DTO_STAT(ND_DEVICE_REMOVED, DAT_ABORT);
	    DTO_STAT(ND_INVALIDATION_ERROR, DAT_DTO_ERR_LOCAL_MM_ERROR);
	  default:
		fprintf(stderr,"%s() Unknown NT Error %#x? ret DAT_INTERNAL_ERR\n",
			__FUNCTION__, cqe_ptr->vendor_err);
		fflush(stderr);
		ds = DAT_INTERNAL_ERROR;
		break;
	}
	return ds;
}
#undef DTO_Stat


/*
 * Map all IBAPI DTO completion codes to the DAT equivelent.
 *
 * dapls_ib_get_dat_event
 *
 * Return a DAT connection event given a provider CM event.
 *
 * N.B.	Some architectures combine async and CM events into a
 *	generic async event. In that case, dapls_ib_get_dat_event()
 *	and dapls_ib_get_async_event() should be entry points that
 *	call into a common routine.
 *
 * Input:
 *	ib_cm_event	event provided to the dapl callback routine
 *	active		switch indicating active or passive connection
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	DAT_EVENT_NUMBER of translated provider value
 */

DAT_EVENT_NUMBER
dapls_ib_get_dat_event (
	IN    const ib_cm_events_t	ib_cm_event,
	IN    DAT_BOOLEAN		active)
{
    DAT_EVENT_NUMBER	dat_event_num = 0;
    UNREFERENCED_PARAMETER (active);

    switch ( ib_cm_event)
    {
      case IB_CME_CONNECTED:
          dat_event_num = DAT_CONNECTION_EVENT_ESTABLISHED;
          break;
      case IB_CME_DISCONNECTED:
           dat_event_num = DAT_CONNECTION_EVENT_DISCONNECTED;
           break;
      case IB_CME_DISCONNECTED_ON_LINK_DOWN:
           dat_event_num = DAT_CONNECTION_EVENT_DISCONNECTED;
           break;
      case IB_CME_CONNECTION_REQUEST_PENDING:
           dat_event_num = DAT_CONNECTION_REQUEST_EVENT;
           break;
      case IB_CME_CONNECTION_REQUEST_PENDING_PRIVATE_DATA:
           dat_event_num = DAT_CONNECTION_REQUEST_EVENT;
           break;
      case IB_CME_CONNECTION_REQUEST_ACKED:
           dat_event_num = DAT_CONNECTION_EVENT_ESTABLISHED;
           break;
      case IB_CME_DESTINATION_REJECT:
           dat_event_num = DAT_CONNECTION_EVENT_NON_PEER_REJECTED;
           break;
      case IB_CME_DESTINATION_REJECT_PRIVATE_DATA:
           dat_event_num = DAT_CONNECTION_EVENT_PEER_REJECTED;
           break;
      case IB_CME_DESTINATION_UNREACHABLE:
           dat_event_num = DAT_CONNECTION_EVENT_UNREACHABLE;
           break;
      case IB_CME_TOO_MANY_CONNECTION_REQUESTS:
           dat_event_num = DAT_CONNECTION_EVENT_NON_PEER_REJECTED;
           break;
      case IB_CME_LOCAL_FAILURE:
      	   dat_event_num = DAT_CONNECTION_EVENT_BROKEN;
           break;
      case IB_CME_BROKEN:
      	   dat_event_num = DAT_CONNECTION_EVENT_BROKEN;
           break;
      case IB_CME_TIMEOUT:
      	   dat_event_num = DAT_CONNECTION_EVENT_TIMED_OUT;
           break;
      case IB_CME_REPLY_RECEIVED:
      case IB_CME_REPLY_RECEIVED_PRIVATE_DATA:
           dat_event_num = DAT_CONNECTION_REQUEST_EVENT;
           break;
      default:
    	   dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		  "dapls_ib_get_dat_event[%s] ib_event %s (%d)\n",
		  active ? "active" : "passive",
		  dapli_ib_cm_event_str(ib_cm_event),ib_cm_event);
	   dapl_os_assert(0);
           break;
    }

    dapl_dbg_log (DAPL_DBG_TYPE_CM,
		  " dapls_ib_get_dat_event[%s] ib_event %s dat_event 0x%x\n",
		  active ? "active" : "passive",
		  dapli_ib_cm_event_str(ib_cm_event),
		  dat_event_num);

    return dat_event_num;
}


/*
 * dapls_ib_get_dat_event
 *
 * Return a DAT connection event given a provider CM event.
 *
 * N.B.	Some architectures combine async and CM events into a
 *	generic async event. In that case, dapls_ib_get_cm_event()
 *	and dapls_ib_get_async_event() should be entry points that
 *	call into a common routine.
 *
 *	WARNING: In this implementation, there are multiple CM
 *	events that map to a single DAT event. Be very careful
 *	with provider routines that depend on this reverse mapping,
 *	they may have to accomodate more CM events than they
 *	'naturally' would.
 *
 * Input:
 *	dat_event_num	DAT event we need an equivelent CM event for
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	ib_cm_event of translated DAPL value
 */
ib_cm_events_t
dapls_ib_get_cm_event (
	IN    DAT_EVENT_NUMBER		dat_event_num)
{
    ib_cm_events_t	ib_cm_event = 0;

    switch (dat_event_num)
    {
        case DAT_CONNECTION_EVENT_ESTABLISHED:
             ib_cm_event = IB_CME_CONNECTED;
             break;
        case DAT_CONNECTION_EVENT_DISCONNECTED:
             ib_cm_event = IB_CME_DISCONNECTED;
             break;
        case DAT_CONNECTION_REQUEST_EVENT:
             ib_cm_event =  IB_CME_CONNECTION_REQUEST_PENDING;
             break;
        case DAT_CONNECTION_EVENT_NON_PEER_REJECTED:
             ib_cm_event = IB_CME_DESTINATION_REJECT;
             break;
        case DAT_CONNECTION_EVENT_PEER_REJECTED:
             ib_cm_event = IB_CME_DESTINATION_REJECT_PRIVATE_DATA;
             break;
        case DAT_CONNECTION_EVENT_UNREACHABLE:
             ib_cm_event = IB_CME_DESTINATION_UNREACHABLE;
             break;
        case DAT_CONNECTION_EVENT_BROKEN:
             ib_cm_event = IB_CME_LOCAL_FAILURE;
             break;
	case DAT_CONNECTION_EVENT_TIMED_OUT:
             ib_cm_event = IB_CME_TIMEOUT;
             break;
        default:
	     dapl_os_assert(0);
             break;
    }

    return ib_cm_event;
}



/*
 * dapls_set_provider_specific_attr
 *
 * Input:
 *	attr_ptr	Pointer provider specific attributes
 *
 * Output:
 * 	none
 *
 * Returns:
 * 	void
 */
DAT_NAMED_ATTR ib_attrs[] = {
	{
	 "DAT_IB_TRANSPORT_MTU", "4096"}
	,
//#ifdef DAT_EXTENSIONS
	{
	 "DAT_EXTENSION_INTERFACE", "FALSE"}
	,
	{
	 DAT_IB_ATTR_FETCH_AND_ADD, "FALSE"}
	,
	{
	 DAT_IB_ATTR_CMP_AND_SWAP, "FALSE"}
	,
	{
	 DAT_IB_ATTR_IMMED_DATA, "FALSE"}
	,
//#ifdef DAT_IB_COLLECTIVES
	{
	 DAT_IB_COLL_BARRIER, "FALSE"}
	,
	{
	 DAT_IB_COLL_BROADCAST, "FALSE"}
	,
	{
	 DAT_IB_COLL_REDUCE, "FALSE"}
	,
	{
	 DAT_IB_COLL_ALLREDUCE, "FALSE"}
	,
	{
	 DAT_IB_COLL_ALLGATHER, "FALSE"}
	,
	{
	 DAT_IB_COLL_ALLGATHERV, "FALSE"}
	,
//#endif /* DAT_IB_COLLECTIVES */
	{
	 DAT_IB_ATTR_UD, "FALSE"}
	,
#ifdef DAPL_COUNTERS
	{
	 DAT_ATTR_COUNTERS, "FALSE"}
	,
#endif	/* DAPL_COUNTERS */
//#endif	/* DAT EXTENSIONS */
};
#define SPEC_ATTR_SIZE( x )	(sizeof( x ) / sizeof( DAT_NAMED_ATTR))

void dapls_query_provider_specific_attr(
	IN	DAPL_IA			*ia_ptr,
	IN	DAT_PROVIDER_ATTR	*attr_ptr )
{
    attr_ptr->num_provider_specific_attr = SPEC_ATTR_SIZE(ib_attrs);
    attr_ptr->provider_specific_attr     = ib_attrs;

    /* set MTU to actual settings */
    ib_attrs[0].value = ia_ptr->hca_ptr->ib_trans.named_attr.value;
}


#ifdef NOT_USED

DAT_RETURN dapls_ns_map_gid (
	IN  DAPL_HCA		*hca_ptr,
	IN  DAT_IA_ADDRESS_PTR	remote_ia_address,
	OUT GID			*gid)
{
	dapl_os_assert( 0 );
    //return (dapls_ib_ns_map_gid (hca_ptr, remote_ia_address, gid));
    return DAT_SUCCESS;
}

DAT_RETURN dapls_ns_map_ipaddr (
	IN  DAPL_HCA		*hca_ptr,
	IN  GID			gid,
	OUT DAT_IA_ADDRESS_PTR	remote_ia_address)
{
	dapl_os_assert( 0 );
    //return (dapls_ib_ns_map_ipaddr (hca_ptr, gid, remote_ia_address));
    return DAT_SUCCESS;
}


/*
 * dapls_ib_post_recv - defered.until QP ! in init state.
 *
 * Provider specific Post RECV function
 */

DAT_RETURN 
dapls_ib_post_recv_defered (
	IN  DAPL_EP		   	*ep_ptr,
	IN  DAPL_COOKIE			*cookie,
	IN  DAT_COUNT	   		num_segments,
	IN  DAT_LMR_TRIPLET	   	*local_iov)
{
    ib_api_status_t     ib_status;
    ib_recv_wr_t	*recv_wr, *rwr;
    ib_local_ds_t       *ds_array_p;
    DAT_COUNT           i, total_len;

    if (ep_ptr->qp_state != DAPL_QP_STATE_UNCONNECTED)
    {
        dapl_dbg_log (DAPL_DBG_TYPE_ERR/*EP*/, "--> DsPR: BAD QP state(%s), not init? "
                      "EP %p QP %p cookie %p, num_seg %d\n", 
                      ib_get_port_state_str(ep_ptr->qp_state), ep_ptr,
                      ep_ptr->qp_handle, cookie, num_segments);
	return (DAT_INSUFFICIENT_RESOURCES);
    }

    recv_wr = dapl_os_alloc (sizeof(ib_recv_wr_t)
                             + (num_segments*sizeof(ib_local_ds_t)));
    if (NULL == recv_wr)
    {
	return (DAT_INSUFFICIENT_RESOURCES);
    }

    dapl_os_memzero(recv_wr, sizeof(ib_recv_wr_t));
    recv_wr->wr_id        = (DAT_UINT64) cookie;
    recv_wr->num_ds       = num_segments;

    ds_array_p = (ib_local_ds_t*)(recv_wr+1);

    recv_wr->ds_array     = ds_array_p;

    //total_len = 0;

    for (total_len = i = 0; i < num_segments; i++, ds_array_p++)
    {
        ds_array_p->length = (uint32_t)local_iov[i].segment_length;
        ds_array_p->lkey  = cl_hton32(local_iov[i].lmr_context);
        ds_array_p->vaddr = local_iov[i].virtual_address;
        total_len        += ds_array_p->length;
    }

    if (cookie != NULL)
    {
	cookie->val.dto.size = total_len;

        dapl_dbg_log (DAPL_DBG_TYPE_EP,
                      "--> DsPR: EP = %p QP = %p cookie= %p, num_seg= %d\n", 
                      ep_ptr, ep_ptr->qp_handle, cookie, num_segments);
    }

    recv_wr->p_next = NULL;

    /* find last defered recv work request, link new on the end */
    rwr=ep_ptr->cm_post;
    if (rwr == NULL)
    {
        ep_ptr->cm_post = (void*)recv_wr;
        i = 1;
    }
    else
    {
        for(i=2; rwr->p_next; rwr=rwr->p_next) i++;
        rwr->p_next = recv_wr;
    }

    dapl_dbg_log (DAPL_DBG_TYPE_EP, "--> DsPR: %s() EP %p h_QP %p cookie %p "
                  "num_seg %d Tdefered %d\n", 
                  __FUNCTION__, ep_ptr, ep_ptr->qp_handle, cookie, num_segments,
                  i);

    return DAT_SUCCESS;
}
#endif

