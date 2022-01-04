/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved. 
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

#if !defined(__AL_PROXY_IOCTL_H__)
#define __AL_PROXY_IOCTL_H__



/*
 * IOCTL structures for passing the callback contexts to user mode.
 */

typedef enum _cm_cb_rec_type
{
	CM_REQ_REC,
	CM_REP_REC,
	CM_RTU_REC,
	CM_REJ_REC,
	CM_MRA_REC,
	CM_LAP_REC,
	CM_APR_REC,
	CM_DREQ_REC,
	CM_DREP_REC

}	cm_cb_rec_type;



typedef enum _misc_cb_rec_type
{
	CA_ERROR_REC,
	QP_ERROR_REC,
	SRQ_ERROR_REC,
	CQ_ERROR_REC,
	MCAST_REC,
	MAD_SEND_REC,
	MAD_RECV_REC,
	SVC_REG_REC,
	QUERY_REC,
	PNP_REC

}	misc_cb_rec_type;



/*
 * Information for most callbacks.  This does not include callbacks for
 * the CM or completions. 
 */
typedef union _misc_cb_ioctl_rec
{
	uint64_t						context;

	/* Asynchronous event records */
	ib_async_event_rec_t			event_rec;

	/* Multicast record */
	struct _mcast_cb_ioctl_rec
	{
		uint64_t					mcast_context;
		ib_api_status_t				status;
		ib_net16_t					error_status;
		uint64_t					h_mcast;
		ib_member_rec_t				member_rec;

	}	mcast_cb_ioctl_rec;


	/* Mad send */
	struct _mad_send_cb_ioctl_rec
	{
		uint64_t					p_um_mad;
		ib_wc_status_t				wc_status;
		uint64_t					mad_svc_context;

	}	mad_send_cb_ioctl_rec;


	/* Mad receive */
	struct _mad_recv_cb_ioctl_rec
	{
		uint64_t					h_mad;
		uint32_t					elem_size;
		uint64_t					mad_svc_context;
		uint64_t					p_send_mad;

	}	mad_recv_cb_ioctl_rec;


	/* PNP Record as defined here is for UAL's consumption alone */
	struct _pnp_cb_ioctl_rec
	{
		ib_pnp_event_t				pnp_event;

		union _pnp_info
		{
			/* pnp_ca is valid only for CA events
			 * UAL can query based on the ca_guid for more info
			 */
			struct _pnp_ca
			{
				ib_net64_t			ca_guid;

			}	ca;

		}	pnp_info;

	}	pnp_cb_ioctl_rec;

}	misc_cb_ioctl_rec_t;



typedef struct _comp_cb_ioctl_info
{
	uint64_t						cq_context;

}	comp_cb_ioctl_info_t;



typedef struct _misc_cb_ioctl_info
{
	misc_cb_rec_type				rec_type;
	misc_cb_ioctl_rec_t				ioctl_rec;

}	misc_cb_ioctl_info_t;


#endif /* __AL_PROXY_IOCTL_H__ */
