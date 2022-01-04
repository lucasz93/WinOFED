/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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


#ifndef _IPOIB_DRIVER_H_
#define _IPOIB_DRIVER_H_


#include "ipoib_log.h"
#include "ipoib_adapter.h"
#include <complib/cl_spinlock.h>
#include <complib/cl_qlist.h>
#include "ipoib_debug.h"


/*
 * Definitions
 */
#define MAX_BUNDLE_ID_LENGTH	32

/* The maximum number of send packets the MiniportSendPackets function can accept */
#define MINIPORT_MAX_SEND_PACKETS	200

/* MLX4 supports 4K MTU */
#define IB_MTU			4096
/*
 * Header length as defined by IPoIB spec:
 * http://www.ietf.org/internet-drafts/draft-ietf-ipoib-ip-over-infiniband-04.txt
 */
 
#define MAX_PAYLOAD_MTU		(IB_MTU - sizeof(ipoib_hdr_t))

/*
 * Only the protocol type is sent as part of the UD payload
 * since the rest of the Ethernet header is encapsulated in the
 * various IB headers.  We report out buffer space as if we
 * transmit the ethernet headers.
 */
#define MAX_XFER_BLOCK_SIZE		(sizeof(eth_hdr_t) + MAX_PAYLOAD_MTU)


typedef struct _ipoib_globals
{
	KSPIN_LOCK		lock;
	cl_qlist_t		adapter_list;
	cl_qlist_t		bundle_list;

	atomic32_t		laa_idx;

	NDIS_HANDLE		h_ndis_wrapper;
	NDIS_HANDLE		h_ibat_dev;
	volatile LONG	ibat_ref;
	uint32_t		bypass_check_bcast_rate;

}	ipoib_globals_t;
/*
* FIELDS
*	lock
*		Spinlock to protect list access.
*
*	adapter_list
*		List of all adapter instances.  Used for address translation support.
*
*	bundle_list
*		List of all adapter bundles.
*
*	laa_idx
*		Global counter for generating LAA MACs
*
*	h_ibat_dev
*		Device handle returned by NdisMRegisterDevice.
*********/

extern ipoib_globals_t	g_ipoib;


typedef struct _ipoib_bundle
{
	cl_list_item_t	list_item;
	char			bundle_id[MAX_BUNDLE_ID_LENGTH];
	cl_qlist_t		adapter_list;

}	ipoib_bundle_t;
/*
* FIELDS
*	list_item
*		List item for storing the bundle in a quick list.
*
*	bundle_id
*		Bundle identifier.
*
*	adapter_list
*		List of adapters in the bundle.  The adapter at the head is the
*		primary adapter of the bundle.
*********/
void
ipoib_create_log(
	NDIS_HANDLE h_adapter,
	UINT ind,
	ULONG eventLogMsgId);

#define GUID_MASK_LOG_INDEX 0

void
ipoib_resume_oids(
	IN				ipoib_adapter_t* const		p_adapter );

#define IPOIB_OFFSET(field)   ((UINT)FIELD_OFFSET(ipoib_params_t,field))
#define IPOIB_SIZE(field)     sizeof(((ipoib_params_t*)0)->field)
#define IPOIB_INIT_NDIS_STRING(str)                        \
    (str)->Length = 0;                                  \
    (str)->MaximumLength = 0;                           \
    (str)->Buffer = NULL;



#endif	/* _IPOIB_DRIVER_H_ */
