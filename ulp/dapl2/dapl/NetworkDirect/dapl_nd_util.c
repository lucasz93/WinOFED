/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
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
 * MODULE: dapl_nd_util.c
 *
 * PURPOSE: DAPL internal routines providing access to the  Microsoft
 * NetworkDirect (ND) APIs
 *
 **********************************************************************/

#include "dapl.h"
#include "dapl_adapter_util.h"
#include "dapl_evd_util.h"
#include "dapl_cr_util.h"
#include "dapl_lmr_util.h"
#include "dapl_rmr_util.h"
#include "dapl_cookie.h"
#include "dapl_ring_buffer_util.h"
#include "dapl_nd.h"
#include "dapl_nd_util.h"

#ifdef DAT_EXTENSIONS
#include <dat2\dat_ib_extensions.h>
#endif

#ifdef DAT_IB_COLLECTIVES
#include <collectives/ib_collectives.h>
#endif

#define DAT_ADAPTER_NAME "NetworkDirect"
#define DAT_VENDOR_NAME  "Microsoft NetworkDirect"


#define NDIOR(a) 	\
	case a:		\
	  type = #a ;	\
	  break

char *
dapli_IOR_str( DAPL_ND_IOR ior )
{
	static char lerr[42];
	char *type;

	switch( ior )
	{
	    NDIOR(IOR_GetOverLapResult);
	    NDIOR(IOR_GetConnReq);
	    NDIOR(IOR_Accept);
	    NDIOR(IOR_Connect);
	    NDIOR(IOR_CompleteConnect);
	    NDIOR(IOR_SendQ);
	    NDIOR(IOR_RecvQ);
	    NDIOR(IOR_Disconnect);
	    NDIOR(IOR_DisconnectNotify);
	    NDIOR(IOR_CQNotify);
	    NDIOR(IOR_CQArm);
	    NDIOR(IOR_Send);
	    NDIOR(IOR_Recv);
	    NDIOR(IOR_RDMA_Read);
	    NDIOR(IOR_RDMA_Write);
	    NDIOR(IOR_MemoryRegion);

	  case IOR_MAX:
	  default:
		_snprintf(lerr,sizeof(lerr),"Unknown IO request type %#08x\n", ior);
		type = lerr;
		break;
	}
	return type;
}
#undef NDIOR



// Retrieve the system error message for the last-error code

char *
GetLastError_str( HRESULT hr, char *errmsg, SIZE_T max_msg_len )
{

	LPVOID lpMsgBuf;
	//DWORD dw = GetLastError(); 
	DWORD dw = (DWORD)hr;
	errno_t rc;

	FormatMessage(
        	FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        	FORMAT_MESSAGE_FROM_SYSTEM |
        	FORMAT_MESSAGE_IGNORE_INSERTS,
        	NULL,
        	dw,
        	MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        	(LPTSTR) &lpMsgBuf,
        	0, NULL );

	strcpy_s(errmsg,max_msg_len,"NTStatus: ");
	rc = strncat_s( errmsg, max_msg_len-strlen(errmsg), lpMsgBuf, _TRUNCATE);

	LocalFree(lpMsgBuf);

	return errmsg;
}


#define CASE_STR(a) 	\
	case a:		\
	  em = #a ;	\
	  break

char *
ND_error_str( HRESULT hr )
{
	static char lerr[128];
	char *em=NULL;

	switch( hr )
	{
	    CASE_STR(ND_SUCCESS);
	    CASE_STR(ND_FLUSHED);
	    CASE_STR(ND_TIMEOUT);
	    CASE_STR(ND_PENDING);
	    CASE_STR(ND_BUFFER_OVERFLOW);
	    CASE_STR(ND_DEVICE_BUSY);
	    CASE_STR(ND_NO_MORE_ENTRIES);
	    CASE_STR(ND_UNSUCCESSFUL);
	    CASE_STR(ND_ACCESS_VIOLATION);
	    CASE_STR(ND_INVALID_HANDLE);
	    CASE_STR(ND_INVALID_DEVICE_REQUEST);
	    CASE_STR(ND_INVALID_PARAMETER);
	    CASE_STR(ND_NO_MEMORY);
	    CASE_STR(ND_INVALID_PARAMETER_MIX);
	    CASE_STR(ND_DATA_OVERRUN);
	    CASE_STR(ND_SHARING_VIOLATION);
	    CASE_STR(ND_INSUFFICIENT_RESOURCES);
	    CASE_STR(ND_DEVICE_NOT_READY);
	    CASE_STR(ND_IO_TIMEOUT);
	    CASE_STR(ND_NOT_SUPPORTED);
	    CASE_STR(ND_INTERNAL_ERROR);
	    CASE_STR(ND_INVALID_PARAMETER_1);
	    CASE_STR(ND_INVALID_PARAMETER_2);
	    CASE_STR(ND_INVALID_PARAMETER_3);
	    CASE_STR(ND_INVALID_PARAMETER_4);
	    CASE_STR(ND_INVALID_PARAMETER_5);
	    CASE_STR(ND_INVALID_PARAMETER_6);
	    CASE_STR(ND_INVALID_PARAMETER_7);
	    CASE_STR(ND_INVALID_PARAMETER_8);
	    CASE_STR(ND_INVALID_PARAMETER_9);
	    CASE_STR(ND_INVALID_PARAMETER_10);
	    CASE_STR(ND_CANCELED);
	    CASE_STR(ND_REMOTE_ERROR);
	    CASE_STR(ND_INVALID_ADDRESS);
	    CASE_STR(ND_INVALID_DEVICE_STATE);
	    CASE_STR(ND_INVALID_BUFFER_SIZE);
	    CASE_STR(ND_TOO_MANY_ADDRESSES);
	    CASE_STR(ND_ADDRESS_ALREADY_EXISTS);
	    CASE_STR(ND_CONNECTION_REFUSED);
	    CASE_STR(ND_CONNECTION_INVALID);
	    CASE_STR(ND_CONNECTION_ACTIVE);
	    CASE_STR(ND_HOST_UNREACHABLE);
	    CASE_STR(ND_CONNECTION_ABORTED);
	    CASE_STR(ND_DEVICE_REMOVED);
	    CASE_STR(ND_DISCONNECTED);

	  default:
		em = GetLastError_str( hr, lerr, sizeof(lerr) );
		if ( em == NULL ) {
			_snprintf(lerr,sizeof(lerr),"ND?? Unknown ND error %#08x",hr);
			em = lerr;
		}
		break;
	}
	return em;
}

char *
ND_RequestType_str( ND2_REQUEST_TYPE rq )
{
	static char lerr[64];
	char *em=NULL;

	switch( rq )
	{
	  CASE_STR(Nd2RequestTypeReceive);
	  CASE_STR(Nd2RequestTypeSend);
	  CASE_STR(Nd2RequestTypeBind);
	  CASE_STR(Nd2RequestTypeRead);
	  CASE_STR(Nd2RequestTypeWrite);
	  CASE_STR(Nd2RequestTypeInvalidate);
	  default:
		_snprintf(lerr,sizeof(lerr),
			"%s() Unknown ND2_REQUEST_TYPE %d\n",__FUNCTION__,rq);
		em = lerr;
	}
	return em;
}

#undef CASE_STR


#define NDStat(a,b)		\
	case a:			\
	ds = (DAT_RETURN)(b);	\
	break


DAT_RETURN
dapl_cvt_ND_to_DAT_status(HRESULT hr)
{
	DAT_RETURN ds;

	switch( hr )
	{
	    NDStat(ND_SUCCESS, DAT_SUCCESS);
	    NDStat(ND_FLUSHED, DAT_SUCCESS);
	    NDStat(ND_TIMEOUT, DAT_TIMEOUT_EXPIRED);
	    NDStat(ND_PENDING, DAT_SUCCESS);
	    NDStat(ND_BUFFER_OVERFLOW, DAT_LENGTH_ERROR);
	    NDStat(ND_DEVICE_BUSY, DAT_PORT_IN_USE);
	    NDStat(ND_NO_MORE_ENTRIES, DAT_QUEUE_FULL);

	    NDStat(ND_UNSUCCESSFUL, DAT_ABORT);
	    NDStat(ND_ACCESS_VIOLATION, DAT_PRIVILEGES_VIOLATION);
	    NDStat(ND_INVALID_HANDLE, DAT_INVALID_HANDLE);
	    NDStat(ND_INVALID_DEVICE_REQUEST, DAT_NOT_IMPLEMENTED);
	    NDStat(ND_INVALID_PARAMETER, DAT_INVALID_PARAMETER);
	    NDStat(ND_NO_MEMORY, DAT_INSUFFICIENT_RESOURCES);
	    NDStat(ND_INVALID_PARAMETER_MIX, DAT_INVALID_PARAMETER);
	    NDStat(ND_DATA_OVERRUN, DAT_QUEUE_FULL);
	    NDStat(ND_SHARING_VIOLATION, DAT_PRIVILEGES_VIOLATION);
	    NDStat(ND_INSUFFICIENT_RESOURCES, DAT_INSUFFICIENT_RESOURCES);
	    NDStat(ND_DEVICE_NOT_READY, DAT_INVALID_STATE);
	    NDStat(ND_IO_TIMEOUT, DAT_TIMEOUT_EXPIRED);
	    NDStat(ND_NOT_SUPPORTED, DAT_COMM_NOT_SUPPORTED);
	    NDStat(ND_INTERNAL_ERROR, DAT_INTERNAL_ERROR);
	    NDStat(ND_INVALID_PARAMETER_1, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_2, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_3, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_4, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_5, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_6, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_7, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_8, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_9, DAT_INVALID_PARAMETER);
	    NDStat(ND_INVALID_PARAMETER_10, DAT_INVALID_PARAMETER);
	    NDStat(ND_CANCELED, DAT_ABORT);
	    NDStat(ND_REMOTE_ERROR, DAT_PRIVILEGES_VIOLATION);
	    NDStat(ND_INVALID_ADDRESS, DAT_INVALID_ADDRESS);
	    NDStat(ND_INVALID_DEVICE_STATE, DAT_INVALID_STATE);
	    NDStat(ND_INVALID_BUFFER_SIZE, DAT_LENGTH_ERROR);
	    NDStat(ND_TOO_MANY_ADDRESSES, DAT_INVALID_ADDRESS);
	    NDStat(ND_ADDRESS_ALREADY_EXISTS, DAT_PROVIDER_ALREADY_REGISTERED);
	    NDStat(ND_CONNECTION_REFUSED,
				(DAT_INVALID_STATE | DAT_INVALID_STATE_EP_NOTREADY));
	    NDStat(ND_CONNECTION_INVALID, DAT_INVALID_ADDRESS);
	    NDStat(ND_CONNECTION_ACTIVE, DAT_CONN_QUAL_IN_USE);
	    NDStat(ND_NETWORK_UNREACHABLE,
			(DAT_INVALID_ADDRESS | DAT_INVALID_ADDRESS_UNREACHABLE));
	    NDStat(ND_HOST_UNREACHABLE,
			(DAT_INVALID_ADDRESS | DAT_INVALID_ADDRESS_UNREACHABLE));
	    NDStat(ND_CONNECTION_ABORTED, DAT_ABORT);
	    NDStat(ND_DEVICE_REMOVED, DAT_ABORT);
	  default:
		ds = DAT_INTERNAL_ERROR;
		break;
	}

//	dapl_dbg_log(DAPL_DBG_TYPE_ERR, "%s() hr %x ds %x\n",
//			__FUNCTION__,hr,ds);fflush(stdout);
	return ds;
}
#undef NDStat


#define DTO_ERR(a) 	\
	case a:		\
	  em = #a ;	\
	  break

char *
DTO_error_str( DAT_DTO_COMPLETION_STATUS ds )
{
	char *em=NULL;

	switch( ds )
	{
	  case DAT_DTO_SUCCESS:
		em = "DAT_DTO_SUCCESS";
    	  case DAT_DTO_ERR_FLUSHED:
		em = "DAT_DTO_ERR_FLUSHED";
    	  case DAT_DTO_ERR_LOCAL_LENGTH:
		em = "DAT_DTO_ERR_LOCAL_LENGTH";
    	  case DAT_DTO_ERR_LOCAL_EP:
		em = "DAT_DTO_ERR_LOCAL_EP";
    	  case DAT_DTO_ERR_LOCAL_PROTECTION:
		em = "DAT_DTO_ERR_LOCAL_PROTECTION";
    	  case DAT_DTO_ERR_BAD_RESPONSE:
		em = "DAT_DTO_ERR_BAD_RESPONSE";
    	  case DAT_DTO_ERR_REMOTE_ACCESS:
		em = "DAT_DTO_ERR_REMOTE_ACCESS";
    	  case DAT_DTO_ERR_REMOTE_RESPONDER:
		em = "DAT_DTO_ERR_REMOTE_RESPONDER";
    	  case DAT_DTO_ERR_TRANSPORT:
		em = "DAT_DTO_ERR_TRANSPORT";
    	  case DAT_DTO_ERR_RECEIVER_NOT_READY:
		em = "DAT_DTO_ERR_RECEIVER_NOT_READY";
    	  case DAT_DTO_ERR_PARTIAL_PACKET:
		em = "DAT_DTO_ERR_PARTIAL_PACKET";
    	  case DAT_RMR_OPERATION_FAILED:
		em = "DAT_RMR_OPERATION_FAILED";
    	  case DAT_DTO_ERR_LOCAL_MM_ERROR:
		em = "DAT_DTO_ERR_LOCAL_MM_ERROR";
	  default:
	    break;
	}
	if (em)
		return em;

	return ND_error_str( (HRESULT) ds );
}
#undef DTO_ERR

char *
dapli_IPv4_addr_str(DAT_SOCK_ADDR6 *ipa, OPTIONAL char *buf)
{
	int rval;
	static char lbuf[28];
	char *cp, *str = (buf ? buf : lbuf);

	cp = inet_ntoa( ((struct sockaddr_in *)ipa)->sin_addr );
	strcpy(str,cp);

	return str;
}

char *
dapli_IPaddr_str(DAT_SOCK_ADDR *sa, OPTIONAL char *buf)
{
	int rc;
	static char lbuf[52];
	char *cp, *str = (buf ? buf : lbuf);
	DWORD sa_len, sa_buflen;

	sa_len = (sa->sa_family == AF_INET6
			? sizeof(struct sockaddr_in6)
			: sizeof(struct sockaddr_in));
	sa_buflen = sizeof(lbuf);
	rc = WSAAddressToString( sa, sa_len, NULL, str, &sa_buflen );
	if ( rc ) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			"%s() ERR: WSAAddressToString %#x sa_len %d\n",
				__FUNCTION__, WSAGetLastError(), sa_len);
	}
	return str;
}


char * dapli_cm_state_str(IN enum dapl_cm_state st)
{
	static char *state[] = {
		"DCM_INIT",
		"DCM_LISTEN",
		"DCM_CONN_PENDING",
		"DCM_REP_PENDING",
		"DCM_ACCEPTING",
		"DCM_ACCEPTING_DATA",
		"DCM_ACCEPTED",
		"DCM_REJECTING",
		"DCM_REJECTED",
		"DCM_CONNECTED",
		"DCM_RELEASE",
		"DCM_DISCONNECTING",
		"DCM_DISCONNECTED",
		"DCM_DESTROY",
		"DCM_RTU_PENDING",
		"DCM_DISC_RECV",
		"DCM_FREE"
        };
        return ((st < 0 || st > DCM_FREE) ? "Invalid CM state?" : state[st]);
}

 
char * dapli_ib_cm_event_str(IN ib_cm_events_t st)
{
	static char *events[] = {	/* must match ib_cm_events_t in dapl_nd.h */
		"IB_CME_CONNECTED",
		"IB_CME_DISCONNECTED",
		"IB_CME_DISCONNECTED_ON_LINK_DOWN",
		"IB_CME_CONNECTION_REQUEST_PENDING",
		"IB_CME_CONNECTION_REQUEST_PENDING_PRIVATE_DATA",
		"IB_CME_CONNECTION_REQUEST_ACKED",
		"IB_CME_DESTINATION_REJECT",
		"IB_CME_DESTINATION_REJECT_PRIVATE_DATA",
		"IB_CME_DESTINATION_UNREACHABLE",
		"IB_CME_TOO_MANY_CONNECTION_REQUESTS",
		"IB_CME_LOCAL_FAILURE",
		"IB_CME_BROKEN",
		"IB_CME_TIMEOUT",
		"IB_CME_REPLY_RECEIVED",
		"IB_CME_REPLY_RECEIVED_PRIVATE_DATA"
        };
        return (st < 0 || st >IB_CME_REPLY_RECEIVED_PRIVATE_DATA)
				? "Invalid IB CM event?" : events[st];
}

ib_cm_events_t
dapli_NDerror_2_CME_event(DWORD err)
{
	ib_cm_events_t cme;

	switch( err )
	{
	  case ND_CONNECTION_REFUSED: 
		cme = IB_CME_DESTINATION_REJECT;
		break;

	  case ND_REMOTE_ERROR: 
	  case ND_CONNECTION_INVALID: 
		cme = IB_CME_DESTINATION_REJECT;
		break;

	  case ND_TOO_MANY_ADDRESSES: 
	  case ND_ADDRESS_ALREADY_EXISTS: 
	  case ND_CONNECTION_ACTIVE: 
		cme = IB_CME_TOO_MANY_CONNECTION_REQUESTS;
		break;

	  case ND_NETWORK_UNREACHABLE: 
	  case ND_HOST_UNREACHABLE: 
		cme = IB_CME_DESTINATION_UNREACHABLE;
		break;

	  case ND_CANCELED: 
	  case ND_CONNECTION_ABORTED: 
	  case ND_DEVICE_REMOVED: 
		cme = IB_CME_LOCAL_FAILURE;
		break;

	  default:
		cme = IB_CME_LOCAL_FAILURE;
		break;
	}
	return cme;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 * End:
 */

