/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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


/*
 * Abstract:
 *	Defines string to decode ib_api_status_t return values.
 *
 * Environment:
 *	All
 */


#include <iba/ib_types.h>


/* ib_api_status_t values above converted to text for easier printing. */
static const char* const __ib_error_str[] =
{
	"IB_SUCCESS",
	"IB_INSUFFICIENT_RESOURCES",
	"IB_INSUFFICIENT_MEMORY",
	"IB_INVALID_PARAMETER",
	"IB_INVALID_SETTING",
	"IB_NOT_FOUND",
	"IB_TIMEOUT",
	"IB_CANCELED",
	"IB_INTERRUPTED",
	"IB_INVALID_PERMISSION",
	"IB_UNSUPPORTED",
	"IB_OVERFLOW",
	"IB_MAX_MCAST_QPS_REACHED",
	"IB_INVALID_QP_STATE",
	"IB_INVALID_APM_STATE",
	"IB_INVALID_PORT_STATE",
	"IB_INVALID_STATE",
	"IB_RESOURCE_BUSY",
	"IB_INVALID_PKEY",
	"IB_INVALID_LKEY",
	"IB_INVALID_RKEY",
	"IB_INVALID_MAX_WRS",
	"IB_INVALID_MAX_SGE",
	"IB_INVALID_CQ_SIZE",
	"IB_INVALID_SRQ_SIZE",
	"IB_INVALID_SERVICE_TYPE",
	"IB_INVALID_GID",
	"IB_INVALID_LID",
	"IB_INVALID_GUID",
	"IB_INVALID_GUID_MASK",
	"IB_INVALID_CA_HANDLE",
	"IB_INVALID_AV_HANDLE",
	"IB_INVALID_CQ_HANDLE",
	"IB_INVALID_QP_HANDLE",
	"IB_INVALID_SRQ_HANDLE",
	"IB_INVALID_PD_HANDLE",
	"IB_INVALID_MR_HANDLE",
	"IB_INVALID_FMR_HANDLE",
	"IB_INVALID_MW_HANDLE",
	"IB_INVALID_MCAST_HANDLE",
	"IB_INVALID_CALLBACK",
	"IB_INVALID_AL_HANDLE",
	"IB_INVALID_HANDLE",
	"IB_ERROR",
	"IB_REMOTE_ERROR",
	"IB_VERBS_PROCESSING_DONE",
	"IB_INVALID_WR_TYPE",
	"IB_QP_IN_TIMEWAIT",
	"IB_EE_IN_TIMEWAIT",
	"IB_INVALID_PORT",
	"IB_NOT_DONE",
	"IB_INVALID_INDEX",
	"IB_NO_MATCH",
	"IB_PENDING",
	"IB_UNKNOWN_ERROR"
};


const char*
ib_get_err_str(
	IN				ib_api_status_t				status )
{
	if( status > IB_UNKNOWN_ERROR )
		status = IB_UNKNOWN_ERROR;
	return( __ib_error_str[status] );
}


/* ib_async_event_t values above converted to text for easier printing. */
static const char* const __ib_async_event_str[] =
{
	"IB_AE_DUMMY",		/*place holder*/
	"IB_AE_SQ_ERROR",
	"IB_AE_SQ_DRAINED",
	"IB_AE_RQ_ERROR",
	"IB_AE_CQ_ERROR",
	"IB_AE_QP_FATAL",
	"IB_AE_QP_COMM",
	"IB_AE_QP_APM",
	"IB_AE_LOCAL_FATAL",
	"IB_AE_PKEY_TRAP",
	"IB_AE_QKEY_TRAP",
	"IB_AE_MKEY_TRAP",
	"IB_AE_PORT_TRAP",
	"IB_AE_SYSIMG_GUID_TRAP",
	"IB_AE_BUF_OVERRUN",
	"IB_AE_LINK_INTEGRITY",
	"IB_AE_FLOW_CTRL_ERROR",
	"IB_AE_BKEY_TRAP",
	"IB_AE_QP_APM_ERROR",
	"IB_AE_WQ_REQ_ERROR",
	"IB_AE_WQ_ACCESS_ERROR",
	"IB_AE_PORT_ACTIVE",		/* ACTIVE STATE */
	"IB_AE_PORT_DOWN",			/* INIT", ARMED", DOWN */
	"IB_AE_CLIENT_REREGISTER",
	"IB_AE_SRQ_LIMIT_REACHED",
	"IB_AE_SRQ_CATAS_ERROR",
	"IB_AE_SRQ_QP_LAST_WQE_REACHED",
	"IB_AE_RESET_DRIVER",
	"IB_AE_RESET_CLIENT",
	"IB_AE_RESET_END",
	"IB_AE_RESET_FAILED",
	"IB_AE_LID_CHANGE",
	"IB_AE_PKEY_CHANGE",
	"IB_AE_SM_CHANGE",
	"IB_AE_GID_CHANGE",
	"IB_AE_RESET_4_RMV",
    "IB_AE_CQ_OVERFLOW",
	"IB_AE_UNKNOWN"
};


const char*
ib_get_async_event_str(
	IN				ib_async_event_t			event )
{
	if( event > IB_AE_UNKNOWN )
		event = IB_AE_UNKNOWN;
	return( __ib_async_event_str[event] );
}


static const char* const __ib_wc_status_str[] =
{
	"IB_WCS_SUCCESS",
	"IB_WCS_LOCAL_LEN_ERR",
	"IB_WCS_LOCAL_OP_ERR",
	"IB_WCS_LOCAL_PROTECTION_ERR",
	"IB_WCS_WR_FLUSHED_ERR",
	"IB_WCS_MEM_WINDOW_BIND_ERR",
	"IB_WCS_REM_ACCESS_ERR",
	"IB_WCS_REM_OP_ERR",
	"IB_WCS_RNR_RETRY_ERR",
	"IB_WCS_TIMEOUT_RETRY_ERR",
	"IB_WCS_REM_INVALID_REQ_ERR",
	"IB_WCS_BAD_RESP_ERR",
	"IB_WCS_LOCAL_ACCESS_ERR",
	"IB_WCS_GENERAL_ERR",
	"IB_WCS_UNMATCHED_RESPONSE",			/* InfiniBand Access Layer */
	"IB_WCS_CANCELED",						/* InfiniBand Access Layer */
	"IB_WCS_REM_ABORT_ERR",
	"IB_WCS_UNKNOWN"
};


const char*
ib_get_wc_status_str(
	IN				ib_wc_status_t				wc_status )
{
	if( wc_status > IB_WCS_UNKNOWN )
		wc_status = IB_WCS_UNKNOWN;
	return( __ib_wc_status_str[wc_status] );
}


static const char* const __ib_wc_send_type_str[] =
{
	"IB_WC_SEND",
	"IB_WC_RDMA_WRITE",
	"IB_WC_RDMA_READ",
	"IB_WC_COMPARE_SWAP",
	"IB_WC_FETCH_ADD",
	"IB_WC_MW_BIND",
    "IB_WC_NOP"
};

static const char* const __ib_wc_recv_type_str[] =
{
	"IB_WC_RECV",
	"IB_WC_RECV_RDMA_WRITE"
};

const char*
ib_get_wc_type_str(
	IN				ib_wc_type_t				wc_type )
{
	if ( wc_type & IB_WC_RECV )
		if ( wc_type - IB_WC_RECV >= IB_WC_UNKNOWN2)
			return "IB_WC_UNKNOWN";
		else
			return __ib_wc_recv_type_str[wc_type - IB_WC_RECV];
	else
		if ( wc_type >= IB_WC_UNKNOWN1 )
			return "IB_WC_UNKNOWN";
		else
			return __ib_wc_send_type_str[wc_type];
}


static const char* const __ib_wr_type_str[] =
{
	"WR_SEND",
	"WR_RDMA_WRITE",
	"WR_RDMA_READ",
	"WR_COMPARE_SWAP",
	"WR_FETCH_ADD",
	"WR_LSO"
	"WR_NOP",
	"WR_LOCAL_INV",
	"WR_FAST_REG_MR",
};


const char* 
ib_get_wr_type_str(
	IN				uint8_t						wr_type )
{
	if( wr_type >= WR_UNKNOWN )
		return "WR_UNKNOWN";
	return( __ib_wr_type_str[wr_type] );
}

static const char* const __ib_qp_type_str[] =
{
	"IB_QPT_RELIABLE_CONN",
	"IB_QPT_UNRELIABLE_CONN",
	"IB_QPT_UNKNOWN",
	"IB_QPT_UNRELIABLE_DGRM",
	"IB_QPT_QP0",
	"IB_QPT_QP1",
	"IB_QPT_RAW_IPV6",
	"IB_QPT_RAW_ETHER",
	"IB_QPT_MAD",
	"IB_QPT_QP0_ALIAS",
	"IB_QPT_QP1_ALIAS",
	"IB_QPT_UNKNOWN"

};


const char* 
ib_get_qp_type_str(
	IN				uint8_t						qp_type )
{
	if( qp_type > IB_QPT_UNKNOWN )
		qp_type = IB_QPT_UNKNOWN;
	return( __ib_qp_type_str[qp_type] );
}



