/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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


#ifndef _IPOIB_ENDPOINT_H_
#define _IPOIB_ENDPOINT_H_


#include <iba/ib_al.h>
#include <complib/cl_qlist.h>
#include <complib/cl_qmap.h>
#include <complib/cl_fleximap.h>
#include <complib/cl_obj.h>
#include "iba/ipoib_ifc.h"
#include <ip_packet.h>
#include "ipoib_debug.h"


typedef struct _ipoib_endpt
{
	cl_obj_t				obj;
	cl_obj_rel_t			rel;
	cl_map_item_t			mac_item;
	cl_fmap_item_t			gid_item;
	cl_map_item_t			lid_item;
	ib_query_handle_t		h_query;
	ib_mcast_handle_t		h_mcast;
	mac_addr_t				mac;
	ib_gid_t				dgid;
	net16_t					dlid;
	net32_t					qpn;
	ib_av_handle_t			h_av;
	ib_al_ifc_t				*p_ifc;
	boolean_t    			is_in_use;
	boolean_t				is_mcast_listener;
}	ipoib_endpt_t;
/*
* FIELDS
*	mac_item
*		Map item for storing the endpoint in a map.  The key is the
*		destination MAC address.
*
*	lid_item
*		Map item for storing the endpoint in a map.  The key is the
*		destination LID.
*
*	gid_item
*		Map item for storing the endpoint in a map.  The key is the
*		destination GID.
*
*	h_query
*		Query handle for cancelling SA queries.
*
*	h_mcast
*		For multicast endpoints, the multicast handle.
*
*	mac
*		MAC address.
*
*	dgid
*		Destination GID.
*
*	dlid
*		Destination LID.  The destination LID is only set for endpoints
*		that are on the same subnet.  It is used as key in the LID map.
*
*	qpn
*		Destination queue pair number.
*
*	h_av
*		Address vector for sending data.
*
*	expired
*		Flag to indicate that the endpoint should be flushed.
*
*	p_ifc
*		Reference to transport functions, can be used
*		while endpoint is not attached to port yet.
*
* NOTES
*	If the h_mcast member is set, the endpoint is never expired.
*********/


ipoib_endpt_t*
ipoib_endpt_create(
	IN		const	ib_gid_t* const				p_dgid,
	IN		const	net16_t						dlid,
	IN		const	net32_t						qpn );


ib_api_status_t
ipoib_endpt_set_mcast(
	IN				ipoib_endpt_t* const		p_endpt,
	IN				ib_pd_handle_t				h_pd,
	IN				uint8_t						port_num,
	IN				ib_mcast_rec_t* const		p_mcast_rec );


static inline void
ipoib_endpt_ref(
	IN				ipoib_endpt_t* const		p_endpt )
{
	CL_ASSERT( p_endpt );

	cl_obj_ref( &p_endpt->obj );
	/*
	 * Anytime we reference the endpoint, we're either receiving data
	 * or trying to send data to that endpoint.  Clear the expired flag
	 * to prevent the AV from being flushed.
	 */
}


static inline void
ipoib_endpt_deref(
	IN				ipoib_endpt_t* const		p_endpt )
{
	cl_obj_deref( &p_endpt->obj );
}


NDIS_STATUS
ipoib_endpt_queue(
	IN				ipoib_endpt_t* const		p_endpt );


#endif	/* _IPOIB_ENDPOINT_H_ */
