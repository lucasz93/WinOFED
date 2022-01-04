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

#if !defined(__IB_AL_CM_SIDR_H__)
#define __IB_AL_CM_SIDR_H__


#include <complib/cl_byteswap.h>
#include <iba/ib_al.h>
#include "al_common.h"

/*
 * CM SIDR MAD definitions.
 * Helper functions to handle version specific SIDR MADs
 */


/* SIDR REQ */
#include <complib/cl_packon.h>

#define CM_SIDR_REQ_ATTR_ID	CL_HTON16(0x0017)
typedef struct _mad_cm_sidr_req
{
	ib_mad_t			hdr;

	ib_net32_t			req_id;

	ib_net16_t			pkey;
	ib_net16_t			rsvd;

	ib_net64_t			sid;

	uint8_t				pdata[IB_SIDR_REQ_PDATA_SIZE];

}	PACK_SUFFIX	mad_cm_sidr_req_t;

#include <complib/cl_packoff.h>

static inline ib_api_status_t
sidr_req_set_pdata(
	IN		const	uint8_t*					p_data OPTIONAL,
	IN		const	uint8_t						rep_length,
	IN OUT			mad_cm_sidr_req_t*	const	p_sidr_req )
{
	if( p_data && rep_length > IB_SIDR_REQ_PDATA_SIZE )
		return IB_INVALID_SETTING;

	if( p_data )
	{
		cl_memcpy( p_sidr_req->pdata, p_data, rep_length );
		cl_memclr( p_sidr_req->pdata + rep_length,
			IB_SIDR_REQ_PDATA_SIZE - rep_length );
	}
	else
	{
		cl_memclr( p_sidr_req->pdata, IB_SIDR_REQ_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
sidr_req_clr_rsvd_fields(
	IN	OUT			mad_cm_sidr_req_t*	const	p_sidr_req )
{
	p_sidr_req->rsvd = 0;
}



/* SIDR REP */
#include <complib/cl_packon.h>

#define	CM_SIDR_REP_ATTR_ID	CL_HTON16(0x0018)
typedef struct _mad_cm_sidr_rep
{
	ib_mad_t			hdr;

	ib_net32_t			req_id;

	uint8_t				status;
	uint8_t				info_len;
	ib_net16_t			rsvd;

	/* QPN 24; rsvd 8 */
	ib_field32_t		offset8;

	ib_net64_t			sid;
	ib_net32_t			qkey;

	ib_class_port_info_t	class_info;

	uint8_t				pdata[IB_SIDR_REP_PDATA_SIZE];

}	PACK_SUFFIX	mad_cm_sidr_rep_t;

#include <complib/cl_packoff.h>

static inline ib_net32_t
sidr_rep_get_qpn(
	IN		const	mad_cm_sidr_rep_t*	const	p_sidr_rep )
{
	return __get_low24( p_sidr_rep->offset8 );
}

static inline void
sidr_rep_set_qpn(
	IN		const	ib_net32_t					qpn,
	IN	OUT			mad_cm_sidr_rep_t*	const	p_sidr_rep )
{
	CL_ASSERT( !( cl_ntoh32( qpn ) & 0xFF000000 ) );
	__set_low24( &p_sidr_rep->offset8, qpn );
}

static inline ib_api_status_t
sidr_rep_set_pdata(
	IN		const	uint8_t*					p_data OPTIONAL,
	IN		const	uint8_t						rep_length,
	IN	OUT			mad_cm_sidr_rep_t*	const	p_sidr_rep )
{
	if( p_data && rep_length > IB_SIDR_REP_PDATA_SIZE )
		return IB_INVALID_SETTING;

	if( p_data )
	{
		cl_memcpy( p_sidr_rep->pdata, p_data, rep_length );
		cl_memclr( p_sidr_rep->pdata + rep_length,
			IB_SIDR_REP_PDATA_SIZE - rep_length );
	}
	else
	{
		cl_memclr( p_sidr_rep->pdata, IB_SIDR_REP_PDATA_SIZE );
	}
	return IB_SUCCESS;
}

static inline void
sidr_rep_clr_rsvd_fields(
	IN	OUT			mad_cm_sidr_rep_t*	const	p_sidr_rep )
{
	p_sidr_rep->rsvd = 0;
	p_sidr_rep->offset8.bytes[3] = 0;
}

#endif /* __IB_AL_CM_SIDR_H__ */
