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

#if !defined(__IB_AL_CM_CONN_H__)
#define __IB_AL_CM_CONN_H__


#include <complib/cl_byteswap.h>
#include <iba/ib_al.h>
#include "al_common.h"


/*
 * Helper functions
 */
static inline void
__set_low24(
		OUT			ib_field32_t* const			p_field,
	IN		const	net32_t						val )
{
	const uint8_t* p8 = (const uint8_t*)&val;

	p_field->bytes[0] = p8[1];
	p_field->bytes[1] = p8[2];
	p_field->bytes[2] = p8[3];
}

/*
 * Returns a network byte ordered 32-bit quantity equal to the 24-bit value
 * stored in the lower 3-bytes of the supplied field
 */
static inline net32_t
__get_low24(
	IN		const	ib_field32_t				field )
{
	return cl_hton32( cl_ntoh32( field.val ) >> 8 );
}

static inline net32_t
__get_low20(
	IN		const	ib_field32_t				field )
{
	return cl_hton32( cl_ntoh32( field.val ) >> 12 );
}

/*
 * CM MAD definitions.
 */
#include <complib/cl_packon.h>

typedef struct _req_path_info
{
	ib_net16_t			local_lid;
	ib_net16_t			remote_lid;
	ib_gid_t			local_gid;
	ib_gid_t			remote_gid;

	/* Version 2: Flow Label:20, rsvd:6, Packet Rate:6 */
	ib_field32_t		offset36;

	uint8_t				traffic_class;
	uint8_t				hop_limit;
	/* SL:4, Subnet Local:1, rsvd:3 */
	uint8_t				offset42;
	/* Local ACK Timeout:5, rsvd:3 */
	uint8_t				offset43;

}	PACK_SUFFIX req_path_info_t;


#define CM_REQ_ATTR_ID	CL_HTON16(0x0010)
typedef struct _mad_cm_req
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			rsvd1;
	ib_net64_t			sid;
	ib_net64_t			local_ca_guid;
	ib_net32_t			rsvd2;
	ib_net32_t			local_qkey;

	/* Local QPN:24, Responder resources:8 */
	ib_field32_t		offset32;
	/* Local EECN:24, Initiator depth:8 */
	ib_field32_t		offset36;
	/*
	 * Remote EECN:24, Remote CM Response Timeout:5,
	 * Transport Service Type:2, End-to-End Flow Control:1
	 */
	ib_field32_t		offset40;
	/* Starting PSN:24, Local CM Response Timeout:5, Retry Count:3. */
	ib_field32_t		offset44;

	ib_net16_t			pkey;

	/* Path MTU:4, RDC Exists:1, RNR Retry Count:3. */
	uint8_t				offset50;
	/* Max CM Retries:4, rsvd:4 */
	uint8_t				offset51;

	req_path_info_t		primary_path;
	req_path_info_t		alternate_path;

	uint8_t				pdata[IB_REQ_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_req_t;
C_ASSERT( sizeof(mad_cm_req_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

/* REQ functions */

/* REQ offset32 accessors. */
static inline ib_net32_t
conn_req_get_lcl_qpn(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return __get_low24( p_req->offset32 );
}

static inline void
conn_req_set_lcl_qpn(
	IN		const	ib_net32_t					qpn,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	__set_low24( &p_req->offset32, qpn );
}

static inline uint8_t
conn_req_get_resp_res(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return p_req->offset32.bytes[3];
}

static inline void
conn_req_set_resp_res(
	IN		const	uint8_t						resp_res,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset32.bytes[3] = resp_res;
}

/* REQ offset36 accessors. */
static inline uint8_t
conn_req_get_init_depth(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return p_req->offset36.bytes[3];
}

static inline void
conn_req_set_init_depth(
	IN		const	uint8_t						init_depth,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset36.bytes[3] = init_depth;
}

static inline uint8_t
conn_req_get_resp_timeout(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (p_req->offset40.bytes[3] >> 3);
}

static inline void
conn_req_set_remote_resp_timeout(
	IN		const	uint8_t						resp_timeout,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( !(resp_timeout & 0xE0) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset40.bytes[3] &= 0x07;
	p_req->offset40.bytes[3] |= (resp_timeout << 3);
}

static inline ib_qp_type_t
conn_req_get_qp_type(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( ib_qp_type_t )( (p_req->offset40.bytes[3] >> 1) & 0x3 );
}

static inline void
conn_req_set_qp_type(
	IN		const	ib_qp_type_t				qp_type,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( !(qp_type & 0xFC) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset40.bytes[3] &= 0xF9;
	p_req->offset40.bytes[3] |= ( ((uint8_t)qp_type & 0x03) << 1 );
}

static inline boolean_t
conn_req_get_flow_ctrl(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT(p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( (p_req->offset40.bytes[3] & 0x01) != 0 );
}

static inline void
conn_req_set_flow_ctrl(
	IN		const	boolean_t					flow_ctrl,
	IN	OUT			mad_cm_req_t* const			p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	if( flow_ctrl )
		p_req->offset40.bytes[3] |= 0x01;
	else
		p_req->offset40.bytes[3] &= 0xFE;
}

/* REQ offset44 accessors. */
static inline ib_net32_t
conn_req_get_starting_psn(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return __get_low24( p_req->offset44 );
}

static inline void
conn_req_set_starting_psn(
	IN		const	ib_net32_t					starting_psn,
	IN				mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	__set_low24( &p_req->offset44, starting_psn );
}

static inline uint8_t
conn_req_get_retry_cnt(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return p_req->offset44.bytes[3] & 0x7;
}

static inline void
conn_req_set_retry_cnt(
	IN		const	uint8_t						retry_cnt,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset44.bytes[3] &= 0xF8;
	p_req->offset44.bytes[3] |= (retry_cnt & 0x7);
}

static inline uint8_t
conn_req_get_lcl_resp_timeout(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (p_req->offset44.bytes[3] >> 3);
}

static inline void
conn_req_set_lcl_resp_timeout(
	IN		const	uint8_t						resp_timeout,
	IN	OUT			mad_cm_req_t* const			p_req )
{
	CL_ASSERT( !(resp_timeout & 0xE0) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset44.bytes[3] &= 0x07;
	p_req->offset44.bytes[3] |= (resp_timeout << 3);
}

/* REQ offset50 accessors. */
static inline uint8_t
conn_req_get_mtu(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( p_req->offset50 >> 4);
}

static inline void
conn_req_set_mtu(
	IN		const	uint8_t						path_mtu,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( !(path_mtu & 0xF0) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset50 &= 0x0F;
	p_req->offset50 |= (path_mtu << 4);
}

static inline uint8_t
conn_req_get_rnr_retry_cnt(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( p_req->offset50 & 0x07 );
}

static inline void
conn_req_set_rnr_retry_cnt(
	IN		const	uint8_t						rnr_retry_cnt,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( !(rnr_retry_cnt & 0xF8) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset50 &= 0xF8;
	p_req->offset50 |= (rnr_retry_cnt & 0x07);
}

static inline uint8_t
conn_req_get_max_cm_retries(
	IN		const	mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (uint8_t)( p_req->offset51 >> 4 );
}

static inline void
conn_req_set_max_cm_retries(
	IN		const	uint8_t						retries,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( !(retries & 0xF0) );
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->offset51 = (retries << 4);
}

static inline ib_api_status_t
conn_req_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );

	if( p_data )
	{
		if( data_len > IB_REQ_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_req->pdata, p_data, data_len );
		cl_memclr( p_req->pdata + data_len, IB_REQ_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_req->pdata, IB_REQ_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_req_clr_rsvd_fields(
	IN	OUT			mad_cm_req_t*		const	p_req )
{
	CL_ASSERT( p_req->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_req->rsvd1 = 0;
	p_req->rsvd2 = 0;
	p_req->offset36.val &= CL_HTON32( 0x000000FF );
	p_req->offset40.val &= CL_HTON32( 0x000000FF );
	p_req->offset50 &= 0xF7;
	p_req->offset51 &= 0xF0;
}

/* REQ Path accessors. */
static inline net32_t
conn_req_path_get_flow_lbl(
	IN		const	req_path_info_t*	const	p_path )
{
	return __get_low20( p_path->offset36 );
}

static inline void
conn_req_path_set_flow_lbl(
	IN		const	net32_t						flow_lbl,
	IN	OUT			req_path_info_t*	const	p_path )
{
	CL_ASSERT( !(cl_ntoh32( flow_lbl ) & 0xFFF00000) );
	__set_low24( &p_path->offset36, (flow_lbl & CL_HTON32( 0x000FFFFF )) );
}

static inline uint8_t
conn_req_path_get_pkt_rate(
	IN		const	req_path_info_t*	const	p_path )
{
	return p_path->offset36.bytes[3] & 0x3F;
}

static inline void
conn_req_path_set_pkt_rate(
	IN		const	uint8_t						rate,
		OUT			req_path_info_t*	const	p_path )
{
	CL_ASSERT( !(rate & 0xC0) );
	p_path->offset36.bytes[3] = (rate & 0x3F);
}

static inline uint8_t
conn_req_path_get_svc_lvl(
	IN		const	req_path_info_t*	const	p_path )
{
	return ( p_path->offset42 >> 4 );
}

static inline void
conn_req_path_set_svc_lvl(
	IN		const	uint8_t						svc_lvl,
	IN	OUT			req_path_info_t*	const	p_path )
{
	CL_ASSERT( !(svc_lvl & 0xF0) );
	p_path->offset42 = ( ( p_path->offset42 & 0x08 ) | (svc_lvl << 4) );
}

static inline boolean_t
conn_req_path_get_subn_lcl(
	IN		const	req_path_info_t*	const	p_path )
{
	return (p_path->offset42 & 0x08) != 0;
}

static inline void
conn_req_path_set_subn_lcl(
	IN		const	boolean_t					subn_lcl,
	IN	OUT			req_path_info_t*	const	p_path )
{
	if( subn_lcl )
		p_path->offset42 = ((p_path->offset42 & 0xF0) | 0x08);
	else
		p_path->offset42 = (p_path->offset42 & 0xF0);
}

static inline uint8_t
conn_req_path_get_lcl_ack_timeout(
	IN		const	req_path_info_t*	const	p_path )
{
	return( p_path->offset43 >> 3 );
}

static inline void
conn_req_path_set_lcl_ack_timeout(
	IN		const	uint8_t						lcl_ack_timeout,
	IN	OUT			req_path_info_t*	const	p_path )
{
	CL_ASSERT( !(lcl_ack_timeout & 0xE0) );
	p_path->offset43 = (lcl_ack_timeout << 3);
}

static inline void
conn_req_path_clr_rsvd_fields(
	IN	OUT			req_path_info_t*	const	p_path )
{
	p_path->offset36.val &= CL_HTON32( 0xFFFFF03F );
	p_path->offset42 &= 0xF8;
	p_path->offset43 &= 0xF8;
}



/* MRA */
#include <complib/cl_packon.h>

#define CM_MRA_ATTR_ID	CL_HTON16(0x0011)
/* MRA is the same in ver1 and ver2. */
typedef struct _mad_cm_mra
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;
	/* Message MRAed:2, rsvd:6 */
	uint8_t				offset8;
	/* Service Timeout:5, rsvd:3 */
	uint8_t				offset9;

	uint8_t				pdata[IB_MRA_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_mra_t;
C_ASSERT( sizeof(mad_cm_mra_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline uint8_t
conn_mra_get_msg_mraed(
	IN		const	mad_cm_mra_t*		const	p_mra )
{
	return (p_mra->offset8 >> 6);
}

static inline void
conn_mra_set_msg_mraed(
	IN		const	uint8_t						msg,
		OUT			mad_cm_mra_t* const			p_mra )
{
	p_mra->offset8 = (msg << 6);
}

static inline uint8_t
conn_mra_get_svc_timeout(
	IN		const	mad_cm_mra_t*		const	p_mra )
{
	return (p_mra->offset9 >> 3);
}

static inline void
conn_mra_set_svc_timeout(
	IN		const	uint8_t						svc_timeout,
		OUT			mad_cm_mra_t* const			p_mra )
{
	p_mra->offset9 = (svc_timeout << 3);
}

static inline ib_api_status_t
conn_mra_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_mra_t*		const	p_mra )
{
	if( p_data )
	{
		if( data_len > IB_MRA_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_mra->pdata, p_data, data_len );
		cl_memclr( p_mra->pdata + data_len, IB_MRA_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_mra->pdata, IB_MRA_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_mra_clr_rsvd_fields(
	IN	OUT			mad_cm_mra_t*		const	p_mra )
{
	p_mra->offset8 &= 0xC0;
	p_mra->offset9 &= 0xF8;
}



/* REJ */

#include <complib/cl_packon.h>

#define CM_REJ_ATTR_ID	CL_HTON16(0x0012)
/* REJ is the same in ver1 and ver2. */
typedef struct _mad_cm_rej
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;

	/* Message REJected:2, rsvd:6 */
	uint8_t				offset8;

	/* Reject Info Length:7, rsvd:1. */
	uint8_t				offset9;

	ib_net16_t			reason;
	uint8_t				ari[IB_ARI_SIZE];
	uint8_t				pdata[IB_REJ_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_rej_t;
C_ASSERT( sizeof(mad_cm_rej_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline uint8_t
conn_rej_get_msg_rejected(
	IN		const	mad_cm_rej_t*		const	p_rej )
{
	return (p_rej->offset8 >> 6);
}

static inline void
conn_rej_set_msg_rejected(
	IN		const	uint8_t						msg,
	IN	OUT			mad_cm_rej_t*		const	p_rej )
{
	p_rej->offset8 = (msg << 6);
}

static inline uint8_t
conn_rej_get_ari_len(
	IN		const	mad_cm_rej_t*		const	p_rej )
{
	return (p_rej->offset9 >> 1);
}

static inline ib_api_status_t
conn_rej_set_ari(
	IN		const	uint8_t*			const	p_ari_info OPTIONAL,
	IN		const	uint8_t						info_len,
	IN	OUT			mad_cm_rej_t*		const	p_rej )
{
	if( p_ari_info && info_len > IB_ARI_SIZE )
		return IB_INVALID_PARAMETER;

	if( p_ari_info )
	{
		cl_memcpy( p_rej->ari, p_ari_info, info_len );
		cl_memclr( p_rej->ari + info_len, IB_ARI_SIZE - info_len );
	}
	else
	{
		cl_memclr( p_rej->ari, IB_ARI_SIZE );
	}
	p_rej->offset9 = (info_len << 1);
	return IB_SUCCESS;
}


static inline ib_api_status_t
conn_rej_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_rej_t*		const	p_rej )
{
	if( p_data )
	{
		if( data_len > IB_REJ_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_rej->pdata, p_data, data_len );
		cl_memclr( p_rej->pdata + data_len, IB_REJ_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_rej->pdata, IB_REJ_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_rej_clr_rsvd_fields(
	IN	OUT			mad_cm_rej_t*		const	p_rej )
{
	p_rej->offset8 &= 0xC0;
	p_rej->offset9 &= 0xFE;
}



/* REP */

#include <complib/cl_packon.h>

#define CM_REP_ATTR_ID	CL_HTON16(0x0013)
typedef struct _mad_cm_rep
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;

	ib_net32_t			local_qkey;
	/* Local QPN:24, rsvd:8 */
	ib_field32_t		offset12;
	/* Local EECN:24, rsvd:8 */
	ib_field32_t		offset16;
	/* Starting PSN:24 rsvd:8 */
	ib_field32_t		offset20;

	uint8_t				resp_resources;
	uint8_t				initiator_depth;
	/* Target ACK Delay:5, Failover Accepted:2, End-to-End Flow Control:1 */
	uint8_t				offset26;
	/* RNR Retry Count:3, rsvd:5 */
	uint8_t				offset27;

	ib_net64_t			local_ca_guid;
	uint8_t				pdata[IB_REP_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_rep_t;
C_ASSERT( sizeof(mad_cm_rep_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_net32_t
conn_rep_get_lcl_qpn(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return __get_low24( p_rep->offset12 );
}

static inline void
conn_rep_set_lcl_qpn(
	IN		const	ib_net32_t					qpn,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	__set_low24( &p_rep->offset12, qpn );
}

static inline ib_net32_t
conn_rep_get_starting_psn(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return __get_low24( p_rep->offset20 );
}

static inline void
conn_rep_set_starting_psn(
	IN		const	ib_net32_t					starting_psn,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	__set_low24( &p_rep->offset20, starting_psn );
}

static inline uint8_t
conn_rep_get_target_ack_delay(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (p_rep->offset26 >> 3);
}

static inline void
conn_rep_set_target_ack_delay(
	IN		const	uint8_t						target_ack_delay,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( !(target_ack_delay & 0xE0) );
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_rep->offset26 =
		((p_rep->offset26 & 0xE0) | (target_ack_delay << 3));
}

static inline uint8_t
conn_rep_get_failover(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( ( p_rep->offset26 >> 1 ) & 0x3 );
}

static inline void
conn_rep_set_failover(
	IN		const	uint8_t						failover,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_rep->offset26 =
		( (p_rep->offset26 & 0xF9) | (( failover & 0x03) << 1) );
}

static inline boolean_t
conn_rep_get_e2e_flow_ctl(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return ( p_rep->offset26 & 0x01 );
}

static inline void
conn_rep_set_e2e_flow_ctl(
	IN		const	boolean_t					e2e_flow_ctl,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	if( e2e_flow_ctl )
		p_rep->offset26 |= 0x01;
	else
		p_rep->offset26 &= 0xFE;
}

static inline uint8_t
conn_rep_get_rnr_retry_cnt(
	IN		const	mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (p_rep->offset27 >> 5);
}

static inline void
conn_rep_set_rnr_retry_cnt(
	IN		const	uint8_t						rnr_retry_cnt,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_rep->offset27 = (rnr_retry_cnt << 5);
}

static inline ib_api_status_t
conn_rep_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	CL_ASSERT( p_rep->hdr.class_ver == IB_MCLASS_CM_VER_2 );

	if( p_data )
	{
		if( data_len > IB_REP_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_rep->pdata, p_data, data_len );
		cl_memclr( p_rep->pdata + data_len, IB_REP_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_rep->pdata, IB_REP_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_rep_clr_rsvd_fields(
	IN	OUT			mad_cm_rep_t*		const	p_rep )
{
	p_rep->offset12.bytes[3] = 0;
	p_rep->offset16.val = 0;
	p_rep->offset20.bytes[3] = 0;
	p_rep->offset27 &= 0xE0;
}


/* RTU */

#include <complib/cl_packon.h>

#define CM_RTU_ATTR_ID	CL_HTON16(0x0014)
/* RTU is the same for ver1 and ver2. */
typedef struct _mad_cm_rtu
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;
	uint8_t				pdata[IB_RTU_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_rtu_t;
C_ASSERT( sizeof(mad_cm_rtu_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_api_status_t
conn_rtu_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_rtu_t*		const	p_rtu )
{
	if( p_data )
	{
		if( data_len > IB_RTU_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_rtu->pdata, p_data, data_len );
		cl_memclr( p_rtu->pdata + data_len, IB_RTU_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_rtu->pdata, IB_RTU_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

/* DREQ */

#include <complib/cl_packon.h>

#define CM_DREQ_ATTR_ID	CL_HTON16(0x0015)
/* DREQ is the same for ver1 and ver2. */
typedef struct _mad_cm_dreq
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;
	/* Remote QPN/EECN:24, rsvd:8 */
	ib_field32_t		offset8;
	uint8_t				pdata[IB_DREQ_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_dreq_t;
C_ASSERT( sizeof(mad_cm_dreq_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_net32_t
conn_dreq_get_remote_qpn(
	IN		const	mad_cm_dreq_t*		const	p_dreq )
{
	return __get_low24( p_dreq->offset8 );
}

static inline void
conn_dreq_set_remote_qpn(
	IN		const	ib_net32_t					qpn,
	IN	OUT			mad_cm_dreq_t*		const	p_dreq )
{
	__set_low24( &p_dreq->offset8, qpn );
}

static inline ib_api_status_t
conn_dreq_set_pdata(
	IN	const		uint8_t*			const	p_data OPTIONAL,
	IN	const		uint8_t						data_len,
	IN	OUT			mad_cm_dreq_t*		const	p_dreq )
{
	if( p_data )
	{
		if( data_len > IB_DREQ_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_dreq->pdata, p_data, data_len );
		cl_memclr( p_dreq->pdata + data_len, IB_DREQ_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_dreq->pdata, IB_DREQ_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_dreq_clr_rsvd_fields(
	IN	OUT			mad_cm_dreq_t*		const	p_dreq )
{
	p_dreq->offset8.bytes[3] = 0;
}



/* DREP */

#include <complib/cl_packon.h>

#define CM_DREP_ATTR_ID	CL_HTON16(0x0016)
/* DREP is the same for ver1 and ver2. */
typedef struct _mad_cm_drep
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;
	uint8_t				pdata[IB_DREP_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_drep_t;
C_ASSERT( sizeof(mad_cm_drep_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_api_status_t
conn_drep_set_pdata(
	IN		const		uint8_t*		const	p_data OPTIONAL,
	IN		const		uint8_t					data_len,
	IN	OUT				mad_cm_drep_t*	const	p_drep )
{
	if( p_data )
	{
		if( data_len > IB_DREP_PDATA_SIZE )
			return IB_INVALID_SETTING;

		cl_memcpy( p_drep->pdata, p_data, data_len );
		cl_memclr( p_drep->pdata + data_len, IB_DREP_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_drep->pdata, IB_DREP_PDATA_SIZE );
	}
	return IB_SUCCESS;
}


/* LAP */

#include <complib/cl_packon.h>

typedef struct _lap_path_info
{
	ib_net16_t			local_lid;
	ib_net16_t			remote_lid;
	ib_gid_t			local_gid;
	ib_gid_t			remote_gid;
	/* Flow Label:20, rsvd:4, Traffic Class:8 */
	ib_field32_t		offset36;
	uint8_t				hop_limit;

	/* rsvd:2, Packet Rate:6 */
	uint8_t				offset41;
	/* SL:4, Subnet Local:1, rsvd:3 */
	uint8_t				offset42;
	/* Local ACK Timeout:5, rsvd:3 */
	uint8_t				offset43;

}	PACK_SUFFIX lap_path_info_t;

#define CM_LAP_ATTR_ID	CL_HTON16(0x0019)
typedef struct _mad_cm_lap
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;

	ib_net32_t			rsvd1;
	/* Remote QPN/EECN:24, Remote CM Response Timeout:5, rsvd:3 */
	ib_field32_t		offset12;
	ib_net32_t			rsvd2;
	lap_path_info_t		alternate_path;
	uint8_t				pdata[IB_LAP_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_lap_t;
C_ASSERT( sizeof(mad_cm_lap_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_net32_t
conn_lap_get_remote_qpn(
	IN		const	mad_cm_lap_t*		const	p_lap )
{
	CL_ASSERT( p_lap->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return __get_low24( p_lap->offset12 );
}

static inline void
conn_lap_set_remote_qpn(
	IN		const	ib_net32_t					qpn,
	IN	OUT			mad_cm_lap_t*		const	p_lap )
{
	CL_ASSERT( p_lap->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	__set_low24( &p_lap->offset12, qpn );
}

static inline uint8_t
conn_lap_get_resp_timeout(
	IN		const	mad_cm_lap_t*		const	p_lap )
{
	CL_ASSERT( p_lap->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	return (p_lap->offset12.bytes[3] >> 3);
}

static inline void
conn_lap_set_resp_timeout(
	IN		const	uint8_t						resp_timeout,
	IN	OUT			mad_cm_lap_t*		const	p_lap )
{
	CL_ASSERT( !(resp_timeout & 0xE0) );
	CL_ASSERT( p_lap->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	p_lap->offset12.bytes[3] = (resp_timeout << 3);
}

static inline ib_api_status_t
conn_lap_set_pdata(
	IN	const			uint8_t*		const	p_data OPTIONAL,
	IN	const			uint8_t					data_len,
	IN	OUT				mad_cm_lap_t*	const	p_lap )
{
	CL_ASSERT( p_lap->hdr.class_ver == IB_MCLASS_CM_VER_2 );

	cl_memclr( p_lap->pdata, IB_LAP_PDATA_SIZE );
	if( p_data )
	{
		if( data_len > IB_LAP_PDATA_SIZE )
			return IB_INVALID_PARAMETER;

		cl_memcpy( p_lap->pdata, p_data, data_len );
		cl_memclr( p_lap->pdata + data_len,
			IB_LAP_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_lap->pdata, IB_LAP_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_lap_clr_rsvd_fields(
	IN	OUT			mad_cm_lap_t*		const	p_lap )
{
	p_lap->rsvd1 = 0;
	p_lap->offset12.val &= CL_HTON32( 0xFFFFFFF8 );
	p_lap->rsvd2 = 0;
}

static inline net32_t
conn_lap_path_get_flow_lbl(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return __get_low20( p_lap_path->offset36 );
}

static inline void
conn_lap_path_set_flow_lbl(
	IN		const	ib_net32_t					flow_lbl,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	__set_low24( &p_lap_path->offset36, (flow_lbl & CL_HTON32( 0x000FFFFF )) );
}

static inline uint8_t
conn_lap_path_get_tclass(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return p_lap_path->offset36.bytes[3];
}

static inline void
conn_lap_path_set_tclass(
	IN		const	uint8_t						tclass,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	p_lap_path->offset36.bytes[3] = tclass;
}

static inline uint8_t
conn_lap_path_get_pkt_rate(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return ( p_lap_path->offset41 & 0x7F );
}

static inline void
conn_lap_path_set_pkt_rate(
	IN		const	uint8_t						pkt_rate,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	CL_ASSERT( !( pkt_rate & 0xC0 ) );

	p_lap_path->offset41 = ( pkt_rate & 0x7F );
}

static inline const uint8_t
conn_lap_path_get_svc_lvl(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return ( p_lap_path->offset42 >> 4 );
}

static inline void
conn_lap_path_set_svc_lvl(
	IN		const	uint8_t						sl,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	CL_ASSERT( !( sl & 0xF0 ) );

	p_lap_path->offset42 = ( (p_lap_path->offset42 & 0x08) | (sl & 0xF0) );
}

static inline boolean_t
conn_lap_path_get_subn_lcl(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return (p_lap_path->offset42 & 0x08) != 0;
}

static inline void
conn_lap_path_set_subn_lcl(
	IN		const	boolean_t					subn_lcl,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	if( subn_lcl )
		p_lap_path->offset42 |= 0x08;
	else
		p_lap_path->offset42 &= 0xF0;
}

static inline const uint8_t
conn_lap_path_get_lcl_ack_timeout(
	IN		const	lap_path_info_t*	const	p_lap_path )
{
	return ( p_lap_path->offset43 >> 3 );
}

static inline void
conn_lap_path_set_lcl_ack_timeout(
	IN		const	uint8_t						timeout,
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	CL_ASSERT( !( timeout & 0xE0 ) );
	p_lap_path->offset43 = (timeout << 3);
}

static inline void
conn_lap_path_clr_rsvd_fields(
	IN	OUT			lap_path_info_t*	const	p_lap_path )
{
	p_lap_path->offset36.val &= CL_HTON32( 0xFFFFF0FF );
	p_lap_path->offset41 &= 0x3F;
	p_lap_path->offset42 &= 0xF8;
	p_lap_path->offset43 &= 0xF8;
}



/* APR */
#include <complib/cl_packon.h>

#define CM_APR_ATTR_ID	CL_HTON16(0x001A)
typedef struct _mad_cm_apr
{
	ib_mad_t			hdr;

	ib_net32_t			local_comm_id;
	ib_net32_t			remote_comm_id;

	uint8_t				info_len;
	uint8_t				status;
	ib_net16_t			rsvd;

	uint8_t				info[IB_APR_INFO_SIZE];
	uint8_t				pdata[IB_APR_PDATA_SIZE];

}	PACK_SUFFIX mad_cm_apr_t;
C_ASSERT( sizeof(mad_cm_apr_t) == MAD_BLOCK_SIZE );

#include <complib/cl_packoff.h>

static inline ib_api_status_t
conn_apr_set_apr_info(
	IN		const	uint8_t*			const	p_info OPTIONAL,
	IN		const	uint8_t						info_len,
	IN	OUT			mad_cm_apr_t*		const	p_apr )
{
	CL_ASSERT( p_apr->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	if( p_info && ( info_len > IB_APR_INFO_SIZE ) )
		return IB_INVALID_PARAMETER;

	if( p_info )
	{
		cl_memcpy( p_apr->info, p_info, info_len );
		cl_memclr( p_apr->info + info_len,
			IB_APR_INFO_SIZE - info_len );
		p_apr->info_len = info_len;
	}
	else
	{
		cl_memclr( p_apr->info, IB_APR_INFO_SIZE );
		p_apr->info_len = 0;
	}
	return IB_SUCCESS;
}

static inline ib_api_status_t
conn_apr_set_pdata(
	IN		const	uint8_t*			const	p_data OPTIONAL,
	IN		const	uint8_t						data_len,
	IN	OUT			mad_cm_apr_t*		const	p_apr )
{
	CL_ASSERT( p_apr->hdr.class_ver == IB_MCLASS_CM_VER_2 );
	if( p_data )
	{
		if( data_len > IB_APR_PDATA_SIZE )
			return IB_INVALID_PARAMETER;

		cl_memcpy( p_apr->pdata, p_data, data_len );
		cl_memclr( p_apr->pdata + data_len,
			IB_APR_PDATA_SIZE - data_len );
	}
	else
	{
		cl_memclr( p_apr->pdata, IB_APR_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
conn_apr_clr_rsvd_fields(
	IN	OUT			mad_cm_apr_t*		const	p_apr )
{
	p_apr->rsvd = 0;
}

#endif /* __IB_AL_CM_CONN_H__ */
