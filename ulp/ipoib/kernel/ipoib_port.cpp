/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2011 Intel Corporation.  All rights reserved.
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

#include "precompile.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_port.tmh"
#endif
#include <offload.h>

#include "wdm.h"
#include <ntddk.h>

#include <kernel\ip_packet.h>
#include <netiodef.h>

extern ULONG g_ipoib_send;
extern ULONG g_ipoib_send_ack;
extern ULONG g_ipoib_send_SW;
extern ULONG g_ipoib_send_SG;
extern ULONG g_ipoib_send_SW_in_loop;
extern ULONG g_ipoib_send_SG_pending;
extern ULONG g_ipoib_send_SG_real;
extern ULONG g_ipoib_send_SG_failed;
extern ULONG g_ipoib_send_reset;

ib_gid_t	bcast_mgid_template = {
	0xff,								/* multicast field */
	0x12,								/* scope (to be filled in) */
	0x40, 0x1b,							/* IPv4 signature */
	0xff, 0xff,							/* 16 bits of P_Key (to be filled in) */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 48 bits of zeros */
	0xff, 0xff, 0xff, 0xff,				/* 32 bit IPv4 broadcast address */
};


#ifdef _DEBUG_
/* Handy pointer for debug use. */
ipoib_port_t	*gp_ipoib_port;
#endif

//static KDEFERRED_ROUTINE __port_mcast_garbage_dpc;

static void
__port_mcast_garbage_dpc(KDPC *p_gc_dpc, void *context, void *s_arg1, void *s_arg2);

static void
__port_do_mcast_garbage(ipoib_port_t* const	p_port );

static ipoib_endpt_t *
ipoib_mac_to_endpt(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t*					mac );

#if IPOIB_CM && DBG

#ifndef _IPOIB_DEBUG_NDIS6
#define _IPOIB_DEBUG_NDIS6

CL_INLINE void CL_API
cl_qlist_check_validity(
	IN	cl_qlist_t* const	p_list )
{
	cl_list_item_t	*p_item;
	size_t cnt = 0;

	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT( p_list );
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT( p_list->state == CL_INITIALIZED );
	p_item = cl_qlist_head(p_list);
	while(p_item != cl_qlist_end(p_list)) {
		++cnt;
		CL_ASSERT(p_item->p_list == p_list);
		p_item = p_item->p_next;
		CL_ASSERT (cnt <= p_list->count);
	}
	CL_ASSERT (cnt == p_list->count);
	return;
}
#endif
#endif

/******************************************************************************
*
* Declarations
*
******************************************************************************/
static void
__port_construct(
	IN				ipoib_port_t* const			p_port,
	IN				int32_t						alloc_size);

static ib_api_status_t
__port_init(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec );

static void
__port_destroying(
	IN				cl_obj_t* const				p_obj );

static void
__port_cleanup(
	IN				cl_obj_t* const				p_obj );

static void
__port_free(
	IN				cl_obj_t* const				p_obj );

static ib_api_status_t
__port_query_ca_attrs( 
	IN		ipoib_port_t* const					p_port,
	IN		ib_ca_attr_t**						pp_ca_attrs );


/******************************************************************************
*
* IB resource manager operations
*
******************************************************************************/
static void
__ib_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__ib_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__ib_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static void
__qp_event(
	IN				ib_async_event_rec_t		*p_event_rec );

static void
__cq_event(
	IN				ib_async_event_rec_t		*p_event_rec );

static ib_api_status_t
__ib_mgr_activate(
	IN				ipoib_port_t* const			p_port );

/******************************************************************************
*
* Buffer manager operations.
*
******************************************************************************/
static void
__buf_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__buf_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__buf_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static cl_status_t
__recv_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item );

#if !IPOIB_INLINE_RECV || UD_NBL_IN_DESC
static void
__recv_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context );
#endif

static inline ipoib_send_desc_t*
__buf_mgr_get_send(
	IN				ipoib_port_t* const			p_port );

static inline void
__buf_mgr_put_send(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc );

static inline ipoib_recv_desc_t*
__buf_mgr_get_recv(
	IN				ipoib_port_t* const			p_port );

static inline void
__buf_mgr_put_recv(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				NET_BUFFER_LIST* const		p_net_buffer_list OPTIONAL );

static inline void
__buf_mgr_put_recv_list(
	IN				ipoib_port_t* const			p_port,
	IN				cl_qlist_t* const			p_list );

//NDIS60
static inline NET_BUFFER_LIST*
__buf_mgr_get_NBL(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc );


/******************************************************************************
*
* Receive manager operations.
*
******************************************************************************/
static void
__recv_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__recv_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__recv_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

/* Posts receive buffers to the receive queue. */
static int32_t
__recv_mgr_repost(
	IN				ipoib_port_t* const			p_port );

static void
__recv_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

static void
__recv_get_endpts(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				ib_wc_t* const				p_wc,
		OUT			ipoib_endpt_t** const		pp_src,
		OUT			ipoib_endpt_t** const		pp_dst );

static int32_t
__recv_mgr_filter(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_done_wc_list,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			cl_qlist_t* const			p_bad_list );

static ib_api_status_t
__recv_gen(
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_arp(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		p_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_icmpv6(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN				ipoib_pkt_t* const			p_ipoib,
	OUT				eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		pp_src,
	IN				ipoib_endpt_t* const		p_dst );

static ib_api_status_t
__recv_mgr_prepare_NBL(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
		OUT			NET_BUFFER_LIST** const		pp_net_buffer_list );

static uint32_t
__recv_mgr_build_NBL_array(
	IN				ipoib_port_t* const			p_port,
		OUT			cl_qlist_t*					p_done_list,
		OUT			int32_t* const				p_discarded );

/******************************************************************************
*
* Send manager operations.
*
******************************************************************************/
static void
__send_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__send_mgr_init(
	IN				ipoib_port_t* const			p_port );


static void
__send_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

static NDIS_STATUS
__send_gen(
	IN				ipoib_send_NB_SG *			s_buf,
	IN				UINT						lso_header_size OPTIONAL);

static NDIS_STATUS
__send_mgr_filter_ip(
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG* const		s_buf);

static NDIS_STATUS
__send_mgr_filter_igmp_v2(
	IN 				ipoib_port_t* const			p_port,
    IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				size_t						iph_options_size,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len );

static NDIS_STATUS
__send_mgr_filter_udp(
	IN		const	void* const					p_ip_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN				net16_t 					ethertype,
	IN	OUT			ipoib_send_NB_SG* const		s_buf);

static NDIS_STATUS
__send_mgr_filter_dhcp(
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN		const	udp_hdr_t* const			p_udp_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN				net16_t 					ethertype,
	IN	OUT			ipoib_send_NB_SG* const		s_buf );

static NDIS_STATUS
__send_mgr_filter_arp(
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG* const		s_buf );

static NDIS_STATUS
__send_mgr_filter_icmpv6(
	IN		const	ipv6_hdr_t* const			p_ipv6_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG*			s_buf);

static inline void 
__send_complete_net_buffer(
	IN	ipoib_send_NB_SG	*s_buf, 
	IN	ULONG				compl_flags);

static inline NDIS_STATUS
__send_mgr_queue(
	IN				ipoib_port_t* const			p_port,
	IN				eth_hdr_t* const			p_eth_hdr,
		OUT			ipoib_endpt_t** const		pp_endpt );

static NDIS_STATUS
__build_send_desc(
	IN				eth_hdr_t* const			p_eth_hdr,
	IN				MDL* const					p_mdl,
	IN		const	size_t						mdl_len,
	IN				ipoib_send_NB_SG			*s_buf );


static void
__send_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context );

static NDIS_STATUS 
GetLsoHeaderSize(
	IN OUT	PNET_BUFFER		pNetBuffer,
	IN		LsoData 		*pLsoData,
	IN		ipoib_hdr_t 	*ipoib_hdr,
	ULONG 					TcpHeaderOffset );


static NDIS_STATUS
__build_lso_desc(
	IN				ipoib_port_t* const			p_port,
	IN				ULONG						mss,
	IN				int32_t						hdr_idx,
	IN PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO p_lso_info,
	IN				NET_BUFFER					*p_netbuf);

static unsigned short ipchksum(unsigned short *ip, int len);

static inline void
ipoib_print_ip_hdr( 
	IN				ip_hdr_t*	const 			p_ip_hdr )
{
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("IP Header:\n ver_hl=%d\tsvc_type=%d\n "
			 "length=%04X\tid=%d\n"
			 "offset=%d\tttl=%d\n "
			 "prot=%d\tchksum=%d\n "
			 "src_ip=%d.%d.%d.%d\tdst_ip=%d.%d.%d.%d\n",
				p_ip_hdr->ver_hl,
				p_ip_hdr->svc_type,
				cl_ntoh16( p_ip_hdr->length ),
				cl_ntoh16( p_ip_hdr->id ),
				cl_ntoh16( p_ip_hdr->offset_flags ),
				p_ip_hdr->ttl,
				p_ip_hdr->prot,
				cl_ntoh16( p_ip_hdr->chksum ),
				((UCHAR*) (void*) & (p_ip_hdr->src_ip))[0],
				((UCHAR*) (void*) & (p_ip_hdr->src_ip))[1],
				((UCHAR*) (void*) & (p_ip_hdr->src_ip))[2],
				((UCHAR*) (void*) & (p_ip_hdr->src_ip))[3],
				((UCHAR*) (void*) & (p_ip_hdr->dst_ip))[0],
				((UCHAR*) (void*) & (p_ip_hdr->dst_ip))[1],
				((UCHAR*) (void*) & (p_ip_hdr->dst_ip))[2],
				((UCHAR*) (void*) & (p_ip_hdr->dst_ip))[3]) );
}

static const char src_hw_str[] = "src_hw_addr";
static const char dest_hw_str[] = "dest_hw_addr";

static inline void
ipoib_print_ib_hw_addr(
	IN				UCHAR*		const	p_hw_addr,
	IN				const char*	const	hw_addr_str ) 
{
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
		("%s =%02x %02x %02x %02x %02x %02x %02x %02x %02x "
		 "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		hw_addr_str,
		p_hw_addr[0], p_hw_addr[1], p_hw_addr[2], p_hw_addr[3],
		p_hw_addr[4], p_hw_addr[5], p_hw_addr[6], p_hw_addr[7],
		p_hw_addr[8], p_hw_addr[9], p_hw_addr[10], p_hw_addr[11],
		p_hw_addr[12], 	p_hw_addr[13], p_hw_addr[14], p_hw_addr[15],
		p_hw_addr[15], 	p_hw_addr[17], p_hw_addr[18], p_hw_addr[19]) 
	);
}

static inline void
ipoib_print_arp_hdr(
	IN				ipoib_arp_pkt_t *	const			p_ib_arp )
{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("ARP packet:\n"
					"hw_type=%d\tprot_type=%d\thw_size=%d\n"
					"prot_size=%d\top=%d\t"
					"src_ip=%d.%d.%d.%d\tdst_ip=%d.%d.%d.%d\n",
						p_ib_arp->hw_type,
					p_ib_arp->prot_type,
					p_ib_arp->hw_size,
					p_ib_arp->prot_size,
					p_ib_arp->op,
					((UCHAR*) (void*)(&p_ib_arp->src_ip))[0],
					((UCHAR*) (void*)(&p_ib_arp->src_ip))[1],
					((UCHAR*) (void*)(&p_ib_arp->src_ip))[2],
					((UCHAR*) (void*)(&p_ib_arp->src_ip))[3],
					((UCHAR*) (void*)(&p_ib_arp->dst_ip))[0],
					((UCHAR*) (void*)(&p_ib_arp->dst_ip))[1],
					((UCHAR*) (void*)(&p_ib_arp->dst_ip))[2],
					((UCHAR*) (void*)(&p_ib_arp->dst_ip))[3])
					);
		ipoib_print_ib_hw_addr( (UCHAR*) (void*) &p_ib_arp->src_hw, src_hw_str );
		ipoib_print_ib_hw_addr( (UCHAR*) (void*) &p_ib_arp->dst_hw , dest_hw_str );
		
}

#if IPOIB_CM

static uint32_t dump_sgl( IN  PSCATTER_GATHER_LIST	p_sgl, int verbose );

static NDIS_STATUS
__build_ipv4_fragments(
	IN		ipoib_send_NB_SG*			s_buf,
	IN		ip_hdr_t* const				p_ip_hdr,
	IN		uint32_t					buf_len,
	IN		uint32_t					ip_packet_len,
	IN		MDL*						p_mdl );

static void
__update_fragment_ip_hdr(
	IN		ip_hdr_t* const		p_ip_hdr,
	IN		uint16_t			fragment_size, 
	IN		uint16_t			fragment_offset, 
	IN		BOOLEAN				more_fragments );

static void
__copy_ip_options(
	IN		uint8_t*			p_buf,
	IN		uint8_t*			p_options,
	IN		uint32_t			options_len,
	IN		BOOLEAN				copy_all );
#endif

/******************************************************************************
*
* Endpoint manager operations
*
******************************************************************************/
static void
__endpt_mgr_construct(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__endpt_mgr_init(
	IN				ipoib_port_t* const			p_port );

static void
__endpt_mgr_destroy(
	IN				ipoib_port_t* const			p_port );

/****f* IPoIB/__endpt_mgr_remove_all
* NAME
*	__endpt_mgr_remove_all
*
* DESCRIPTION
*	Removes all enpoints from the port, dereferencing them to initiate
*	destruction.
*
* SYNOPSIS
*/
static void
__endpt_mgr_remove_all(
	IN				ipoib_port_t* const			p_port );
/*
********/

static void
__endpt_mgr_remove(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t* const		p_endpt );

static void
__endpt_mgr_reset_all(
	IN				ipoib_port_t* const			p_port );

static inline NDIS_STATUS
__endpt_mgr_ref(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ipoib_endpt_t** const		pp_endpt );

static inline NDIS_STATUS
__endpt_mgr_get_gid_qpn(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t* const				p_gid,
		OUT			UNALIGNED net32_t* const	p_qpn );

static inline ipoib_endpt_t*
__endpt_mgr_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid );

static inline ipoib_endpt_t*
__endpt_mgr_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid );

static inline ib_api_status_t
__endpt_mgr_insert_locked(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt );

static inline ib_api_status_t
__endpt_mgr_insert(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt );

static ib_api_status_t
__endpt_mgr_add_local(
	IN				ipoib_port_t* const			p_port,
	IN				ib_port_info_t* const		p_port_info );

static ib_api_status_t
__endpt_mgr_add_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_mcast_rec_t				*p_mcast_rec );

/******************************************************************************
*
* MCast operations.
*
******************************************************************************/
static ib_api_status_t
__port_get_bcast(
	IN				ipoib_port_t* const			p_port );

static ib_api_status_t
__port_join_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_member_rec_t* const		p_member_rec );

static ib_api_status_t
__port_create_bcast(
	IN				ipoib_port_t* const			p_port );



static void
__bcast_get_cb(
	IN				ib_query_rec_t				*p_query_rec );


static void
__bcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec );


static void
__mcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec );

void
__leave_error_mcast_cb(
	IN				void						*context );

static ib_api_status_t
__endpt_update(	
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t** const		pp_src,
	IN				const ipoib_hw_addr_t*		p_new_hw_addr,
	IN				ib_wc_t* const				p_wc
);

#if DBG
char *ref_cnt_str(int type);
#endif

static int
__gid_cmp(
	IN		const	void* const					p_key1,
	IN		const	void* const					p_key2 )
{
	return memcmp( p_key1, p_key2, sizeof(ib_gid_t) );
}

inline void ipoib_port_ref( ipoib_port_t * p_port, int type )
{
	cl_obj_ref( &p_port->obj );
#if DBG
	int32_t	r = cl_atomic_inc( &p_port->ref[type % ref_mask] );

	if( ((p_port->obj.ref_cnt % 20) == 0) || p_port->obj.ref_cnt < 10 )
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
			("ref type %d '%s' refs %d port refs %d\n",
				type, ref_cnt_str(type), r, p_port->obj.ref_cnt) );
#else
	UNREFERENCED_PARAMETER(type);
#endif
}


inline void ipoib_port_deref(ipoib_port_t * p_port, int type)
{
	cl_obj_deref( &p_port->obj );
#if DBG
	int32_t r = cl_atomic_dec( &p_port->ref[type % ref_mask] );

	if( ((p_port->obj.ref_cnt % 20) == 0) || p_port->obj.ref_cnt < 10 )
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_OBJ,
			("deref type %d '%s' refs %d port refs %d\n",
				type, ref_cnt_str(type), r, p_port->obj.ref_cnt) );
#else
	UNREFERENCED_PARAMETER(type);
#endif
}

/* function returns pointer to payload that is going after IP header.
*  asssuming that payload and IP header are in the same buffer
*/
static inline void* GetIpPayloadPtr(const	ip_hdr_t* const	p_ip_hdr)
{
	return (void*)((uint8_t*)p_ip_hdr + IP_HEADER_LENGTH(p_ip_hdr));
}

static inline void* GetIpv6PayloadPtr(const	ipv6_hdr_t* const	p_ipv6_hdr)
{
	// BUGBUG: need to add support for extension headers
	return (void*)((uint8_t*)p_ipv6_hdr + sizeof(ipv6_hdr_t));
}


/******************************************************************************
*
* Implementation
*
******************************************************************************/
ib_api_status_t
ipoib_create_port(
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec,
		OUT			ipoib_port_t** const		pp_port )
{
	ib_api_status_t		status;
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( !p_adapter->p_port );

	// Allocate PORT object along with "send queue depth" - 1 IPoIB headers 
	p_port = (ipoib_port_t *) cl_zalloc( sizeof(ipoib_port_t) +
		(sizeof(ipoib_prealloc_hdr_t) * (p_adapter->params.sq_depth - 1)) );
	if( !p_port )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate ipoib_port_t (%d bytes)\n",
			sizeof(ipoib_port_t)) );
		return IB_INSUFFICIENT_MEMORY;
	}

#ifdef _DEBUG_
	gp_ipoib_port = p_port;
#endif

	__port_construct( p_port, p_adapter->params.sq_depth );

	status = __port_init( p_port, p_adapter, p_pnp_rec );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_port_init returned %s.\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		__port_cleanup( &p_port->obj );
		__port_free( &p_port->obj );
		return status;
	}

	*pp_port = p_port;
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


void
ipoib_port_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_port );
	CL_ASSERT( p_port->p_adapter );
	CL_ASSERT( !p_port->p_adapter->p_port );

#if DBG
	if( p_port->obj.ref_cnt > 0 )
	{
		dmp_ipoib_port_refs( p_port, "port_destroy()" );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ALL,
			("*** port[%d] ref cnt %d > 0\n",
				p_port->port_num, p_port->obj.ref_cnt) );
	}
#endif
	cl_obj_destroy( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_construct(
	IN				ipoib_port_t* const			p_port,
	IN				int32_t						alloc_size)
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port->state = IB_QPS_RESET;

	cl_obj_construct( &p_port->obj, IPOIB_OBJ_PORT );
	cl_spinlock_construct( &p_port->send_lock );
	cl_spinlock_construct( &p_port->recv_lock );
	__ib_mgr_construct( p_port );
	__buf_mgr_construct( p_port );

	__recv_mgr_construct( p_port );
	__send_mgr_construct( p_port );

	__endpt_mgr_construct( p_port );

	p_port->pPoWorkItem = NULL;
	p_port->pPoWorkItemCM = NULL;

	KeInitializeEvent( &p_port->sa_event, NotificationEvent, TRUE );
	KeInitializeEvent( &p_port->leave_mcast_event, NotificationEvent, TRUE );

	for ( int i = 0; i < alloc_size; ++i )
	{
		p_port->hdr[i].phys_addr = cl_get_physaddr( &p_port->hdr[i] );
		p_port->hdr[i].resv = 0;
	}
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__port_init(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_adapter_t* const		p_adapter,
	IN				ib_pnp_port_rec_t* const	p_pnp_rec )
{
	cl_status_t			cl_status;
	ib_api_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port->port_num = p_pnp_rec->p_port_attr->port_num;
	p_port->p_adapter = p_adapter;

	p_port->pPoWorkItem = IoAllocateWorkItem(p_adapter->pdo);

	if( p_port->pPoWorkItem == NULL )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("IoAllocateWorkItem returned NULL\n") );
		return IB_ERROR;
	}
#if IPOIB_CM
	p_port->pPoWorkItemCM = IoAllocateWorkItem(p_adapter->pdo);
	if( p_port->pPoWorkItemCM == NULL )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("IoAllocateWorkItem returned NULL for CM?\n") );
		return IB_ERROR;
	}
#endif
	cl_status = cl_spinlock_init( &p_port->send_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_spinlock_init( &p_port->recv_lock );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_spinlock_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	/* Initialize the IB resource manager. */
	status = __ib_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__ib_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the buffer manager. */
	status = __buf_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__buf_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the receive manager. */
	status = __recv_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	
	status =__send_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__send_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Initialize the endpoint manager. */
	status = __endpt_mgr_init( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_init returned %s\n", 
			p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	 /* Initialize multicast garbage collector timer and DPC object */
	KeInitializeDpc(&p_port->gc_dpc,
					(PKDEFERRED_ROUTINE)__port_mcast_garbage_dpc,p_port);
	KeInitializeTimerEx(&p_port->gc_timer,SynchronizationTimer);

	/* We only ever destroy from the PnP callback thread. */
	cl_status = cl_obj_init( &p_port->obj, CL_DESTROY_SYNC, __port_destroying,
								__port_cleanup, __port_free );

	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_obj_init returned %#x\n", cl_status) );
		return IB_ERROR;
	}

	cl_status = cl_obj_insert_rel( &p_port->rel, &p_adapter->obj, &p_port->obj );
	if( cl_status != CL_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_obj_insert_rel returned %#x\n", cl_status) );
		cl_obj_destroy( &p_port->obj );
		return IB_ERROR;
	}

	/* The steps of the initialization are as depicted below:
	 I.	adapter_init() calls shutter_init(), the shutter counter is set to 0
	II.	ipoib_pnp_cb() calls to ipoib_port_init() that calls to __port_init() that SHOULD set shutter counter to -MAX_OPERATIONS
		That is, the code below should call to shutter_shut()
	III.NDIS calls to ipoib_restart() that calls to shutter_alive. Now, shutter_counter is again 0.
	IV.	NDIS call to ipoib_pause() that sets the adapter to pause state, calls to shutter_shut. 
		Now, the counter is again equals to -MAX_OPERATIONS
	V.	NDIS calls to ipoib_restart that calls to shutter_alive. Shutter counter is 0 and we can start working
	*/
	
	if ( p_adapter->ipoib_state == IPOIB_UNINIT ) 
	{
		p_adapter->ipoib_state = IPOIB_INIT;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SHUTTER,
			("Shutter shut, state = %d\n", p_adapter->ipoib_state));
		shutter_shut ( &p_adapter->recv_shutter );
	} 
	else 
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SHUTTER,
			("Shutter wasn't shut, state = %d\n", p_adapter->ipoib_state));
	}
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__port_destroying(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;
	cl_qlist_t	complete_list;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );
	cl_qlist_init(&complete_list);

	ipoib_port_down( p_port );

	__endpt_mgr_remove_all( p_port );

	if( p_port->p_adapter->params.cm_enabled )
	{
		endpt_cm_buf_mgr_destroy( p_port );
		ipoib_port_srq_destroy( p_port );
		p_port->endpt_mgr.thread_is_done = 1;
		cl_event_signal( &p_port->endpt_mgr.event );
	}

	cl_spinlock_acquire(&p_port->send_lock);
	ipoib_port_resume( p_port, FALSE, &complete_list );
	cl_spinlock_release(&p_port->send_lock);
	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());

	dmp_ipoib_port_refs( p_port, "port_destroying()" );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_cleanup(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );

	/* Wait for all sends and receives to get flushed. */
	while( p_port->send_mgr.depth || p_port->recv_mgr.depth )
		cl_thread_suspend( 0 );

	/* Destroy the send and receive managers before closing the CA. */
	__ib_mgr_destroy( p_port );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__port_free(
	IN				cl_obj_t* const				p_obj )
{
	ipoib_port_t	*p_port;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( p_obj );

	p_port = PARENT_STRUCT( p_obj, ipoib_port_t, obj );

	KeCancelTimer(&p_port->gc_timer);
	KeFlushQueuedDpcs();
	__endpt_mgr_destroy( p_port );
	__recv_mgr_destroy( p_port );
	__send_mgr_destroy( p_port );
	__buf_mgr_destroy( p_port );

	cl_spinlock_destroy( &p_port->send_lock );
	cl_spinlock_destroy( &p_port->recv_lock );

	cl_obj_deinit( p_obj );
	if( p_port->p_ca_attrs )
	{
		cl_free ( p_port->p_ca_attrs );
	}

	IoFreeWorkItem( p_port->pPoWorkItem );
	if( p_port->pPoWorkItemCM )
		IoFreeWorkItem( p_port->pPoWorkItemCM );

	cl_free( p_port );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}



/******************************************************************************
*
* IB resource manager implementation.
*
******************************************************************************/
static void
__ib_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	memset( &p_port->ib_mgr, 0, sizeof(ipoib_ib_mgr_t) );
}


static ib_api_status_t
__ib_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_cq_create_t		cq_create;
	ib_qp_create_t		qp_create;
	ib_phys_create_t	phys_create;
	ib_phys_range_t		phys_range;
	uint64_t			vaddr;
	net32_t				rkey;
	ib_qp_attr_t		qp_attr;
	boolean_t			cm_enabled = p_port->p_adapter->params.cm_enabled;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Open the CA. */
	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("__ib_mgr_init: open  CA\n"));
	status = p_port->p_adapter->p_ifc->open_ca( p_port->p_adapter->h_al,
												p_port->p_adapter->guids.ca_guid,
												NULL,
												p_port,
												&p_port->ib_mgr.h_ca );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_OPEN_CA, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_open_ca(port %p) returns %s\n", 
			p_port, p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	status = __port_query_ca_attrs( p_port, &p_port->p_ca_attrs );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Query CA attributes failed\n" ) );
		return status;
	}

#if IPOIB_USE_DMA
	/* init DMA only once while running MiniportInitialize */
	if ( !p_port->p_adapter->reset )
	{
		ULONG max_phys_mapping;

		if( p_port->p_adapter->params.cm_enabled )
		{
			max_phys_mapping = p_port->p_adapter->params.cm_xfer_block_size;
		}
		else if( p_port->p_adapter->params.lso )
		{
			max_phys_mapping = MAX_LSO_SIZE;
		}
		else
		{
			max_phys_mapping = p_port->p_adapter->params.xfer_block_size;
		}
		/*if( NdisMInitializeScatterGatherDma( p_port->p_adapter->h_adapter,
							TRUE, max_phys_mapping )!= NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("NdisMInitializeScatterGatherDma failed\n" ) );
			return IB_INSUFFICIENT_RESOURCES;
		}*/
	}
#endif

	/* Allocate the PD. */
	status = p_port->p_adapter->p_ifc->alloc_pd( p_port->ib_mgr.h_ca,
												 IB_PDT_UD,
												 p_port,
												 &p_port->ib_mgr.h_pd );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_ALLOC_PD, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_alloc_pd returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Allocate receive CQ. */
	cq_create.size = p_port->p_adapter->params.rq_depth;
	cq_create.pfn_comp_cb = __recv_cb;
	cq_create.h_wait_obj = NULL;

	status = p_port->p_adapter->p_ifc->create_cq( p_port->ib_mgr.h_ca,
												  &cq_create,
												  p_port,
												  __cq_event,
												  &p_port->ib_mgr.h_recv_cq );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_RECV_CQ, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_cq returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Allocate send CQ. */
	cq_create.size = p_port->p_adapter->params.sq_depth;
	cq_create.pfn_comp_cb = __send_cb;

	status = p_port->p_adapter->p_ifc->create_cq( p_port->ib_mgr.h_ca,
												  &cq_create,
												  p_port,
												  __cq_event,
												  &p_port->ib_mgr.h_send_cq );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_SEND_CQ, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_cq returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	
	/* Allocate the QP. */
	memset( &qp_create, 0, sizeof(qp_create) );
	qp_create.qp_type = IB_QPT_UNRELIABLE_DGRM;
	qp_create.rq_depth = p_port->p_adapter->params.rq_depth;
	qp_create.rq_sge = 2;	/* To support buffers spanning pages. */
	qp_create.h_rq_cq = p_port->ib_mgr.h_recv_cq;
	qp_create.sq_depth = p_port->p_adapter->params.sq_depth;
	
	// p_ca_attrs->max_sges contains the  maximum number of SG elements 
	// available by HW. 3 of the them are reserved for an internal use
	// Thus, the maximum of SGE for UD QP is limited by (p_ca_attrs->max_sges - 3)
#define UD_QP_USED_SGE 3
	if ( p_port->p_ca_attrs->max_sges > MAX_SEND_SGE )
	{
		p_port->max_sq_sge_supported = MAX_SEND_SGE - UD_QP_USED_SGE;
	}
	else 
	{
		p_port->max_sq_sge_supported = p_port->p_ca_attrs->max_sges - UD_QP_USED_SGE;
	}
	
	p_port->p_ca_attrs->max_sges -= UD_QP_USED_SGE;
	qp_create.sq_sge = p_port->max_sq_sge_supported;
	
	if ( !p_port->p_ca_attrs->ipoib_csum ) 
	{ 
		/* checksum is not supported by device
		user must specify BYPASS to explicitly cancel checksum calculation */
		if (p_port->p_adapter->params.send_chksum_offload == CSUM_ENABLED)
			p_port->p_adapter->params.send_chksum_offload = CSUM_DISABLED;
		if (p_port->p_adapter->params.recv_chksum_offload == CSUM_ENABLED)
			p_port->p_adapter->params.recv_chksum_offload = CSUM_DISABLED;
	}

	// Now, params struct contains the intersection between the user definition
	// and actual HW capabilites
	// Remember these values for NDIS OID requests
	p_port->p_adapter->offload_cap.lso = !!(p_port->p_adapter->params.lso);
	p_port->p_adapter->offload_cap.send_chksum_offload = 
		!! (p_port->p_adapter->params.send_chksum_offload);
	p_port->p_adapter->offload_cap.recv_chksum_offload = 
		!! (p_port->p_adapter->params.recv_chksum_offload);

	qp_create.h_sq_cq = p_port->ib_mgr.h_send_cq;
	qp_create.sq_signaled = FALSE;

	status = p_port->p_adapter->p_ifc->create_qp( p_port->ib_mgr.h_pd,
												  &qp_create,
												  p_port,
												  __qp_event,
												  &p_port->ib_mgr.h_qp );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_QP, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	/* Query the QP so we can get our QPN. */
	status = p_port->p_adapter->p_ifc->query_qp( p_port->ib_mgr.h_qp, &qp_attr );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_QUERY_QP, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	
	ASSERT( qp_attr.sq_sge >= qp_create.sq_sge);
	p_port->ib_mgr.qpn = qp_attr.num;

	/* Register all of physical memory */
	phys_create.length = MEM_REG_SIZE;
	phys_create.num_ranges = 1;
	phys_create.range_array = &phys_range;
	phys_create.buf_offset = 0;
	phys_create.hca_page_size = PAGE_SIZE;
	phys_create.access_ctrl = IB_AC_LOCAL_WRITE;
	phys_range.base_addr = 0;
	phys_range.size = MEM_REG_SIZE;
	vaddr = 0;
	status = p_port->p_adapter->p_ifc->reg_phys( p_port->ib_mgr.h_pd,
												 &phys_create,
												 &vaddr,
												 &p_port->ib_mgr.lkey,
												 &rkey,
												 &p_port->ib_mgr.h_mr );
	if( status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_REG_PHYS, 1, status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_reg_phys returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	if( p_port->p_adapter->params.cm_enabled )
	{
		/* Create a Shared Recv Queue for CM */
		status = ipoib_port_srq_init( p_port );
		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_port_srq_init failed %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* disable further CM initialization */
			p_port->p_adapter->params.cm_enabled = FALSE;

			NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
				EVENT_IPOIB_CONNECTED_MODE_ERR, 1, 0xbadc0de1 );

			p_port->ib_mgr.h_srq = NULL;
			goto cm_xit;
		}

		status = endpt_cm_buf_mgr_init( p_port );
		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("CM Init buf mgr failed status %#x\n", status ) );
			ipoib_port_srq_destroy( p_port );
			p_port->p_adapter->params.cm_enabled = FALSE;

			NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
				EVENT_IPOIB_CONNECTED_MODE_ERR, 1, 0xbadc0de2 );
			p_port->p_adapter->params.cm_enabled  = 0; // disable CM mode.
		}
		else 
		{
			if ( p_port->p_adapter->params.send_chksum_offload )
				p_port->p_adapter->params.send_chksum_offload = CSUM_DISABLED;
		}
cm_xit:
		if(	cm_enabled && p_port->p_adapter->params.cm_enabled == 0 )
		{	/* problems in CM resource allocation - release CM resouces */
			endpt_cm_buf_mgr_destroy( p_port );
			ipoib_port_srq_destroy( p_port );
			status = IB_SUCCESS;	// good to go in UD mode.
		}
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}

static void
__srq_async_event_cb(
	IN		ib_async_event_rec_t		*p_event_rec )
{
	ipoib_port_t* p_port = 
		(ipoib_port_t *)p_event_rec->context;

	switch( p_event_rec->code )
	{
	case IB_AE_SRQ_LIMIT_REACHED:
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("SRQ ASYNC EVENT CODE %d: %s\n", 
			p_event_rec->code, "IB_AE_SRQ_LIMIT_REACHED" ) );
			break;
	case IB_AE_SRQ_CATAS_ERROR:
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("SRQ ASYNC EVENT CODE %d: %s\n", 
				p_event_rec->code, "IB_AE_SRQ_CATAS_ERROR" ) );
			/*SRQ is in err state, must reinitialize */
			p_port->p_adapter->hung = TRUE;
			break;
	case IB_AE_SRQ_QP_LAST_WQE_REACHED:
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("SRQ ASYNC EVENT CODE %d: %s\n", 
				p_event_rec->code, "IB_AE_SRQ_QP_LAST_WQE_REACHED" ) );
			/*SRQ is in err state, must reinitialize */
			p_port->p_adapter->hung = TRUE;
			break;
	default:
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ASYNC EVENT CODE ARRIVED %s vendor code %#I64d\n", 
					ib_get_async_event_str( p_event_rec->code ),
					p_event_rec->vendor_specific) );
	}
}

ib_api_status_t
ipoib_port_srq_init(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		ib_status;
	ib_srq_handle_t		h_srq;
	ib_srq_attr_t		srq_attr;

	IPOIB_ENTER( IPOIB_DBG_CM );
	
	if( !p_port->p_adapter->params.cm_enabled )
		return IB_SUCCESS;

	srq_attr.max_sge = MAX_CM_RECV_SGE;

	// if below threshold, then hardware fires async event
	srq_attr.srq_limit = SRQ_LOW_WATER;
	srq_attr.max_wr =
		min( ((uint32_t)p_port->p_adapter->params.rq_depth * 8),
				(p_port->p_ca_attrs->max_srq_wrs/2) );

	DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM_CONN,
		("SRQ max_WR %u = MIN( rq_depth x8 %u, ca.max_srq_wrs/2 %u)\n",
			srq_attr.max_wr,
			(uint32_t)p_port->p_adapter->params.rq_depth * 8,
			(p_port->p_ca_attrs->max_srq_wrs/2)) );

	p_port->ib_mgr.srq_qp_cnt = 0; 

	ib_status = p_port->p_adapter->p_ifc->create_srq( p_port->ib_mgr.h_pd, 
													  &srq_attr, 
													  p_port, 
													  __srq_async_event_cb, 
													  &h_srq );
	if( ib_status != IB_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_CREATE_QP, 1, ib_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_srq failed status %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
		return ib_status;
	}
	p_port->ib_mgr.h_srq = h_srq;

	IPOIB_EXIT( IPOIB_DBG_CM );

	return ib_status;
}

/*  __port_query_ca_attrs() 
 *  returns a pointer to allocated memory.
 *  must be released by caller.
 */
static ib_api_status_t
__port_query_ca_attrs( 
	IN		ipoib_port_t* const		p_port,
	IN		ib_ca_attr_t**			pp_ca_attrs )
{
	ib_api_status_t		ib_status;
	uint32_t			attr_size;
	ib_ca_attr_t*		p_ca_attrs;

	*pp_ca_attrs = NULL;

	ib_status = 
		p_port->p_adapter->p_ifc->query_ca( p_port->ib_mgr.h_ca, NULL , &attr_size );
	if( ib_status != IB_INSUFFICIENT_MEMORY )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query_ca failed status %s\n",
			p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
		goto done;
	}
	CL_ASSERT( attr_size );

	p_ca_attrs = (ib_ca_attr_t *) cl_zalloc( attr_size );
	if ( p_ca_attrs == NULL )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Allocate %d bytes failed for CA Attributes\n", attr_size ));
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto done;
	}

	ib_status = 
		p_port->p_adapter->p_ifc->query_ca( p_port->ib_mgr.h_ca, p_ca_attrs , &attr_size );
	if ( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("CA attributes query failed\n") );
		cl_free ( p_ca_attrs );
		goto done;
	}

	*pp_ca_attrs = p_ca_attrs;
done:
	return ib_status;
}

void
ipoib_port_srq_destroy( 
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t	status;

	if( p_port->ib_mgr.h_srq )
	{
		int loops=1000;
		BOOLEAN dispatch = (KeGetCurrentIrql() == DISPATCH_LEVEL);

		/* wait for SRQ bound QPs to destroy */
		for(; loops > 0 && p_port->ib_mgr.srq_qp_cnt > 0; loops-- ) 
		{
			if( !dispatch )
				cl_thread_suspend(2);
		}

		status = p_port->p_adapter->p_ifc->destroy_srq( p_port->ib_mgr.h_srq, NULL );
		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Port[%d] destroy_srq() %s\n",
					p_port->port_num,
					p_port->p_adapter->p_ifc->get_err_str( status )) );
		}
		p_port->ib_mgr.h_srq = NULL;
		p_port->ib_mgr.srq_qp_cnt = 0;
	}
}

static void
__ib_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	if( p_port->ib_mgr.h_ca )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("__ib_mgr_destroy: close	CA\n"));
		status = p_port->p_adapter->p_ifc->close_ca( p_port->ib_mgr.h_ca, NULL );
		CL_ASSERT( status == IB_SUCCESS );
		p_port->ib_mgr.h_ca = NULL;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}



/******************************************************************************
*
* Buffer manager implementation.
*
******************************************************************************/
static void
__buf_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	cl_qpool_construct( &p_port->buf_mgr.recv_pool );

	p_port->buf_mgr.h_packet_pool = NULL;

	NdisInitializeNPagedLookasideList( &p_port->buf_mgr.send_buf_list,
		NULL, NULL, 0, MAX_LSO_PAYLOAD_MTU, 'bipi', 0 );

	p_port->buf_mgr.send_buf_len = MAX_LSO_PAYLOAD_MTU;

	p_port->buf_mgr.h_send_pkt_pool = NULL;
}


static ib_api_status_t
__buf_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	cl_status_t		cl_status;
#if UD_NBL_IN_DESC
	ib_api_status_t	ib_status;
#endif
	ipoib_params_t	*p_params;
	NET_BUFFER_LIST_POOL_PARAMETERS pool_parameters;

	IPOIB_ENTER(IPOIB_DBG_INIT );

	CL_ASSERT( p_port );
	CL_ASSERT( p_port->p_adapter );

	p_params = &p_port->p_adapter->params;

#if	UD_NBL_IN_DESC

	/* Allocate the NET BUFFER list pools for receive indication. */
	memset(&pool_parameters, 0, sizeof(NET_BUFFER_LIST_POOL_PARAMETERS));
    pool_parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pool_parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    pool_parameters.Header.Size = sizeof(pool_parameters);
    pool_parameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    pool_parameters.ContextSize = 0;
    pool_parameters.fAllocateNetBuffer = TRUE;
    pool_parameters.PoolTag = 'CRPI';
	pool_parameters.DataSize = 0;

    p_port->buf_mgr.h_packet_pool = NdisAllocateNetBufferListPool(
												p_port->p_adapter->h_adapter,
												&pool_parameters ); 

	if( !p_port->buf_mgr.h_packet_pool )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_POOL, 1, NDIS_STATUS_RESOURCES  );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool returned %08X\n", (UINT)NDIS_STATUS_RESOURCES) );
		return IB_INSUFFICIENT_RESOURCES;
	}

	/* Allocate the receive descriptor pool */
	cl_status = cl_qpool_init( &p_port->buf_mgr.recv_pool,
							   p_params->rq_depth * p_params->recv_pool_ratio,
							   0,
							   0,
							   sizeof(ipoib_recv_desc_t),
							   __recv_ctor,
							   __recv_dtor,
							   p_port );

	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_POOL, 1, cl_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init for recvs returned %#x\n",
			cl_status) );
		ib_status = IB_INSUFFICIENT_MEMORY;
		goto pkt_pool_failed;
	}

#else	!UD_NBL_IN_DESC

	/* Allocate the receive descriptor pool */
	cl_status = cl_qpool_init( &p_port->buf_mgr.recv_pool,
							   p_params->rq_depth * p_params->recv_pool_ratio,
							   0,
							   0,
							   sizeof(ipoib_recv_desc_t),
							   __recv_ctor,
#if IPOIB_INLINE_RECV
							   NULL,
#else
							   __recv_dtor,
#endif
							   p_port );

	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_POOL, 1, cl_status );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init for recvs returned %#x\n",
			cl_status) );
		return IB_INSUFFICIENT_MEMORY;
	}

	/* Allocate the NET BUFFER list pools for receive indication. */
	memset(&pool_parameters, 0, sizeof(NET_BUFFER_LIST_POOL_PARAMETERS));
    pool_parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    pool_parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    pool_parameters.Header.Size = sizeof(pool_parameters);
    pool_parameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    pool_parameters.ContextSize = 0;
    pool_parameters.fAllocateNetBuffer = TRUE;
    pool_parameters.PoolTag = 'CRPI';
	pool_parameters.DataSize = 0;

    p_port->buf_mgr.h_packet_pool = NdisAllocateNetBufferListPool(
												p_port->p_adapter->h_adapter,
												&pool_parameters ); 

	if( !p_port->buf_mgr.h_packet_pool )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_POOL, 1, NDIS_STATUS_RESOURCES  );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool returned %08X\n", (UINT)NDIS_STATUS_RESOURCES) );
		return IB_INSUFFICIENT_RESOURCES;
	}
#endif
	
	/* Allocate the NET buffer list pool for send formatting. */
    pool_parameters.PoolTag = 'XTPI';

    p_port->buf_mgr.h_send_pkt_pool = NdisAllocateNetBufferListPool(
												p_port->p_adapter->h_adapter,
												&pool_parameters ); 
	if( !p_port->buf_mgr.h_send_pkt_pool)
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_SEND_PKT_POOL, 1, NDIS_STATUS_RESOURCES );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisAllocatePacketPool returned %08X\n",
			(UINT)NDIS_STATUS_RESOURCES) );
#if UD_NBL_IN_DESC
		ib_status = IB_INSUFFICIENT_RESOURCES;
		goto pkt_pool_failed;
#else
		return IB_INSUFFICIENT_RESOURCES;
#endif
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;

#if UD_NBL_IN_DESC
pkt_pool_failed:
	NdisFreeNetBufferListPool( p_port->buf_mgr.h_packet_pool );
	p_port->buf_mgr.h_packet_pool = NULL;
	cl_qpool_destroy( &p_port->buf_mgr.recv_pool );
	if( p_port->buf_mgr.h_send_pkt_pool)
	{
		NdisFreeNetBufferListPool( p_port->buf_mgr.h_send_pkt_pool );
		p_port->buf_mgr.h_send_pkt_pool = NULL;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return ib_status;
#endif
}


static void
__buf_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER(IPOIB_DBG_INIT );

	CL_ASSERT( p_port );

	/* Destroy the send packet and buffer pools. 
	if( p_port->buf_mgr.h_send_buf_pool )
		NdisFreeBufferPool( p_port->buf_mgr.h_send_buf_pool );*/
	if( p_port->buf_mgr.h_send_pkt_pool )
		NdisFreeNetBufferListPool ( p_port->buf_mgr.h_send_pkt_pool );

	/* Destroy the receive packet and buffer pools. */
#if UD_NBL_IN_DESC
	/* Free the receive and send descriptors. */
	cl_qpool_destroy( &p_port->buf_mgr.recv_pool );

	if( p_port->buf_mgr.h_packet_pool )
		NdisFreeNetBufferListPool ( p_port->buf_mgr.h_packet_pool );
#else
	if( p_port->buf_mgr.h_packet_pool )
		NdisFreeNetBufferListPool ( p_port->buf_mgr.h_packet_pool );

	/* Free the receive and send descriptors. */
	cl_qpool_destroy( &p_port->buf_mgr.recv_pool );
#endif

	/* Free the lookaside list of scratch buffers. */
	NdisDeleteNPagedLookasideList( &p_port->buf_mgr.send_buf_list );

	IPOIB_EXIT(  IPOIB_DBG_INIT );
}


static cl_status_t
__recv_ctor(
	IN				void* const					p_object,
	IN				void*						context,
		OUT			cl_pool_item_t** const		pp_pool_item )
{
	ipoib_recv_desc_t	*p_desc;
	ipoib_port_t		*p_port;

#if IPOIB_INLINE_RECV
	uint32_t			ds0_len;
#endif

	IPOIB_ENTER( IPOIB_DBG_ALLOC );

	CL_ASSERT( p_object );
	CL_ASSERT( context );

	p_desc = (ipoib_recv_desc_t*)p_object;
	p_port = (ipoib_port_t*)context;

	/* Setup the work request. */
	p_desc->wr.ds_array = p_desc->local_ds;
	p_desc->wr.wr_id = (uintn_t)p_desc;

#if IPOIB_INLINE_RECV
	/* Sanity check on the receive buffer layout */
	CL_ASSERT( (void*)&p_desc->buf.eth.pkt.type ==
		(void*)&p_desc->buf.ib.pkt.type );
	CL_ASSERT( sizeof(recv_buf_t) == sizeof(ipoib_pkt_t) + sizeof(ib_grh_t) );

	/* Setup the local data segment. */
	p_desc->local_ds[0].vaddr = cl_get_physaddr( &p_desc->buf );
	p_desc->local_ds[0].lkey = p_port->ib_mgr.lkey;
	ds0_len =
		PAGE_SIZE - ((uint32_t)p_desc->local_ds[0].vaddr & (PAGE_SIZE - 1));
	if( ds0_len >= sizeof(recv_buf_t) )
	{
		/* The whole buffer is within a page. */
		p_desc->local_ds[0].length = ds0_len;
		p_desc->wr.num_ds = 1;
	}
	else
	{
		/* The buffer crosses page boundaries. */
		p_desc->local_ds[0].length = ds0_len;
		p_desc->local_ds[1].vaddr =
							cl_get_physaddr( ((uint8_t*)&p_desc->buf) + ds0_len );
		p_desc->local_ds[1].lkey = p_port->ib_mgr.lkey;
		p_desc->local_ds[1].length = sizeof(recv_buf_t) - ds0_len;
		p_desc->wr.num_ds = 2;
	}
#else	/* ! IPOIB_INLINE_RECV */
	/* Allocate the receive buffer. */
	p_desc->p_buf = (recv_buf_t*)cl_zalloc( sizeof(recv_buf_t) );
	if( !p_desc->p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate receive buffer.\n") );
		return CL_INSUFFICIENT_MEMORY;
	}

	/* Sanity check on the receive buffer layout */
	CL_ASSERT( (void*)&p_desc->p_buf->eth.pkt.type ==
		(void*)&p_desc->p_buf->ib.pkt.type );

	/* Setup the local data segment. */
	p_desc->local_ds[0].vaddr = cl_get_physaddr( p_desc->p_buf );
	p_desc->local_ds[0].length = sizeof(ipoib_pkt_t) + sizeof(ib_grh_t);
	p_desc->local_ds[0].lkey = p_port->ib_mgr.lkey;
	p_desc->wr.num_ds = 1;
#endif	/* IPOIB_INLINE_RECV */

	p_desc->type = PKT_TYPE_UCAST;
	p_desc->recv_mode = RECV_UD;

#if	UD_NBL_IN_DESC
	/* setup NDIS NetworkBufferList and MemoryDescriptorList for this Recv desc */
	p_desc->p_mdl = NdisAllocateMdl(p_port->p_adapter->h_adapter,
#if IPOIB_INLINE_RECV
									&p_desc->buf.eth.pkt,
#else
									p_desc->p_buf,
#endif
									sizeof(ipoib_pkt_t) + sizeof(ib_grh_t) );
	if( !p_desc->p_mdl )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate MDL\n") );
		goto err1;
	}

	p_desc->p_NBL = NdisAllocateNetBufferAndNetBufferList(
						p_port->buf_mgr.h_packet_pool,
						0,
						0,
						p_desc->p_mdl,
						0,
						0);

	if( !p_desc->p_NBL )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate NET_BUFFER_LIST\n") );
		goto err2;
	}

	NET_BUFFER_LIST_NEXT_NBL(p_desc->p_NBL) = NULL;
	IPOIB_PORT_FROM_NBL( p_desc->p_NBL ) = p_port;
	IPOIB_RECV_FROM_NBL( p_desc->p_NBL ) = p_desc;
	p_desc->p_NBL->SourceHandle = p_port->p_adapter->h_adapter;
#endif

	*pp_pool_item = &p_desc->item;

	IPOIB_EXIT( IPOIB_DBG_ALLOC );
	return CL_SUCCESS;

#if UD_NBL_IN_DESC
err2:
	NdisFreeMdl( p_desc->p_mdl );
	p_desc->p_mdl = NULL;

err1:
#if !IPOIB_INLINE_RECV
	cl_free( p_desc->p_buf );
	p_desc->p_buf = NULL;
#endif
	IPOIB_EXIT( IPOIB_DBG_ALLOC );
	return CL_INSUFFICIENT_MEMORY;
#endif
}


#if !IPOIB_INLINE_RECV || UD_NBL_IN_DESC
static void
__recv_dtor(
	IN		const	cl_pool_item_t* const		p_pool_item,
	IN				void						*context )
{
	ipoib_recv_desc_t	*p_desc;

	IPOIB_ENTER(  IPOIB_DBG_ALLOC );

	UNUSED_PARAM( context );

	p_desc = PARENT_STRUCT( p_pool_item, ipoib_recv_desc_t, item );

#if UD_NBL_IN_DESC
	if( p_desc->p_mdl )
	{
		NdisFreeMdl( p_desc->p_mdl );
		p_desc->p_mdl = NULL;
	}
	if( p_desc->p_NBL)
	{
		NdisFreeNetBufferList(p_desc->p_NBL);
		p_desc->p_NBL = NULL;
	}
#endif

#if !IPOIB_INLINE_RECV
	if( p_desc->p_buf )
		cl_free( p_desc->p_buf );
#endif
	IPOIB_EXIT( IPOIB_DBG_ALLOC );
}
#endif


static inline ipoib_recv_desc_t*
__buf_mgr_get_recv(
	IN				ipoib_port_t* const			p_port )
{
	ipoib_recv_desc_t	*p_desc;

	XIPOIB_ENTER( IPOIB_DBG_BUF );

	p_desc = (ipoib_recv_desc_t*)cl_qpool_get( &p_port->buf_mgr.recv_pool );

	/* Reference the port object for the recv. */
	if( p_desc )
	{
		ipoib_port_ref( p_port, ref_get_recv );
		CL_ASSERT( p_desc->recv_mode == RECV_UD );
		CL_ASSERT( p_desc->wr.wr_id == (uintn_t)p_desc );
#if IPOIB_INLINE_RECV
		CL_ASSERT( p_desc->local_ds[0].vaddr == cl_get_physaddr( &p_desc->buf ) );
#else
		CL_ASSERT( p_desc->local_ds[0].vaddr == cl_get_physaddr( p_desc->p_buf ) );
		CL_ASSERT( p_desc->local_ds[0].length ==
										(sizeof(ipoib_pkt_t) + sizeof(ib_grh_t)) );
#endif
		CL_ASSERT( p_desc->local_ds[0].lkey == p_port->ib_mgr.lkey );
	}
	XIPOIB_EXIT( IPOIB_DBG_BUF );
	return p_desc;
}


//NDIS60
static inline void
__buf_mgr_put_recv(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				NET_BUFFER_LIST* const		p_net_buffer_list OPTIONAL )
{
	IPOIB_ENTER(IPOIB_DBG_BUF );

	if( p_net_buffer_list )
	{
		NET_BUFFER	*p_buf = NULL;
		MDL			*p_mdl = NULL;
#if UD_NBL_IN_DESC
		ASSERT( p_desc->p_NBL );
		ASSERT( p_desc->p_mdl );
		ASSERT( p_net_buffer_list == p_desc->p_NBL );

		NET_BUFFER_LIST_NEXT_NBL(p_net_buffer_list) = NULL;

		p_buf = NET_BUFFER_LIST_FIRST_NB( p_net_buffer_list );
		p_mdl = NET_BUFFER_FIRST_MDL( p_buf );	

		ASSERT( p_mdl == p_desc->p_mdl );
		ASSERT( NET_BUFFER_CURRENT_MDL( p_buf ) == p_mdl );
#else
		NET_BUFFER_LIST_NEXT_NBL(p_net_buffer_list) = NULL;
		p_buf = NET_BUFFER_LIST_FIRST_NB(p_net_buffer_list);
		CL_ASSERT( p_buf );
		p_mdl = NET_BUFFER_FIRST_MDL(p_buf);
		CL_ASSERT( p_mdl );
		NdisFreeMdl(p_mdl);
		NdisFreeNetBufferList(p_net_buffer_list);
#endif
	}
#if DBG
	if (p_desc->recv_mode != RECV_UD )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("!RECV_UD? p_desc %p pkt_type %s cmode %s\n",
				p_desc,
				get_ipoib_pkt_type_str(p_desc->type),
				get_ib_recv_mode_str(p_desc->recv_mode)) );
	}
#endif
	CL_ASSERT( p_desc->recv_mode == RECV_UD );

	/* Return the descriptor to its pools. */
	cl_qpool_put( &p_port->buf_mgr.recv_pool, &p_desc->item );

	/*
	 * Dereference the port object since the receive is no longer outstanding.
	 */
	ipoib_port_deref( p_port, ref_get_recv );
	IPOIB_EXIT( IPOIB_DBG_BUF );
}


static inline void
__buf_mgr_put_recv_list(
	IN				ipoib_port_t* const			p_port,
	IN				cl_qlist_t* const			p_list )
{
	cl_qpool_put_list( &p_port->buf_mgr.recv_pool, p_list );
}


static inline NET_BUFFER_LIST*
__buf_mgr_get_NBL(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc )
{
	NET_BUFFER_LIST			*p_net_buffer_list;
	MDL						*p_mdl;
#if UD_NBL_IN_DESC
	PNET_BUFFER			NetBuffer;
	UNREFERENCED_PARAMETER(p_port);
	
	IPOIB_ENTER( IPOIB_DBG_BUF );
	ASSERT( p_desc->p_NBL );
	ASSERT( p_desc->p_mdl );
	p_net_buffer_list = p_desc->p_NBL;

	NetBuffer = NET_BUFFER_LIST_FIRST_NB( p_net_buffer_list );
	p_mdl = NET_BUFFER_FIRST_MDL( NetBuffer );	

	ASSERT( p_mdl == p_desc->p_mdl );
	ASSERT( NET_BUFFER_CURRENT_MDL( NetBuffer ) == p_mdl );

	NET_BUFFER_DATA_LENGTH( NetBuffer ) = p_desc->len;
	NdisAdjustMdlLength( p_mdl, p_desc->len );

#else	// !UD_NBL_IN_DESC
	IPOIB_ENTER( IPOIB_DBG_BUF );

	p_mdl = NdisAllocateMdl(p_port->p_adapter->h_adapter,
							&p_desc->buf.eth.pkt,
							p_desc->len );
	if( !p_mdl )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate MDL\n") );
		return NULL;
	}

	p_net_buffer_list = NdisAllocateNetBufferAndNetBufferList(
						p_port->buf_mgr.h_packet_pool,
						0,
						0,
						p_mdl,
						0,
						0);

	if( !p_net_buffer_list )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate NET_BUFFER_LIST\n") );
		NdisFreeMdl(p_mdl);
		return NULL;
	}

	NET_BUFFER_LIST_NEXT_NBL(p_net_buffer_list) = NULL;
	IPOIB_PORT_FROM_NBL( p_net_buffer_list ) = p_port;
	IPOIB_RECV_FROM_NBL( p_net_buffer_list ) = p_desc;
	p_net_buffer_list->SourceHandle = p_port->p_adapter->h_adapter;
#endif	// !UD_NBL_IN_DESC

	IPOIB_EXIT( IPOIB_DBG_BUF );
	return p_net_buffer_list;
}


/******************************************************************************
*
* Receive manager implementation.
*
******************************************************************************/
static void
__recv_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	cl_qlist_init( &p_port->recv_mgr.done_list );
	p_port->recv_mgr.recv_NBL_array = NULL;
}


static ib_api_status_t
__recv_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Allocate the NDIS_PACKET pointer array for indicating receives. */
	p_port->recv_mgr.recv_NBL_array = (NET_BUFFER_LIST **)cl_malloc(
		sizeof(NET_BUFFER_LIST*) * p_port->p_adapter->params.rq_depth );
	if( !p_port->recv_mgr.recv_NBL_array )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_PKT_ARRAY, 0 );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_malloc for PNDIS_PACKET array failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


static void
__recv_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	CL_ASSERT( cl_is_qlist_empty( &p_port->recv_mgr.done_list ) );
	CL_ASSERT( !p_port->recv_mgr.depth );

	if( p_port->recv_mgr.recv_NBL_array )
		cl_free( p_port->recv_mgr.recv_NBL_array );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


/*
 * Posts receive buffers to the receive queue and returns the number
 * of receives needed to bring the RQ to its low water mark.  Note
 * that the value is signed, and can go negative.  All tests must
 * be for > 0.
 */
static int32_t
__recv_mgr_repost(
	IN			ipoib_port_t* const			p_port )
{
	ipoib_recv_desc_t	*p_head = NULL, *p_tail = NULL, *p_next;
	ib_api_status_t		status;
	ib_recv_wr_t		*p_failed;
	size_t				rx_cnt=0;
	int					rx_wanted;
	PERF_DECLARE( GetRecv );
	PERF_DECLARE( PostRecv );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	CL_ASSERT( p_port );
	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
			("Port[%d] in invalid state; Not reposting.\n", p_port->port_num) );

		return 0;
	}
	ipoib_port_ref( p_port, ref_repost );
	cl_obj_unlock( &p_port->obj );

	rx_wanted = p_port->p_adapter->params.rq_depth - p_port->recv_mgr.depth;

	while( p_port->recv_mgr.depth < p_port->p_adapter->params.rq_depth )
	{
		/* Pull receives out of the pool and chain them up. */
		cl_perf_start( GetRecv );
		p_next = __buf_mgr_get_recv( p_port );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecv );
		if( !p_next )
		{
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
				("Out of UD receive descriptors! cur recv Q depth %d Max %d\n",
					p_port->recv_mgr.depth,p_port->p_adapter->params.rq_depth) );
			break;
		}

		if( !p_tail )
		{
			p_tail = p_next;
			p_next->wr.p_next = NULL;
		}
		else
		{
			p_next->wr.p_next = &p_head->wr;
		}

		p_head = p_next;

		p_port->recv_mgr.depth++;
		rx_cnt++;
	}

	if( p_head )
	{
		cl_perf_start( PostRecv );
		status = p_port->p_adapter->p_ifc->post_recv( p_port->ib_mgr.h_qp,
													  &p_head->wr,
													  &p_failed );
		cl_perf_stop( &p_port->p_adapter->perf, PostRecv );

		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ip_post_recv returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* return the descriptors to the pool */
			while( p_failed )
			{
				p_head = PARENT_STRUCT( p_failed, ipoib_recv_desc_t, wr );
				p_failed = p_failed->p_next;

				__buf_mgr_put_recv( p_port, p_head, NULL );
				p_port->recv_mgr.depth--;
			}
		}
#if DBG
		else
		{
			if( (size_t)rx_wanted != rx_cnt )
			{
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_BUF,
					("UD RX bufs: wanted %d posted %d\n",rx_wanted,rx_cnt) ); 
			}
		}
#endif
	}

	ipoib_port_deref( p_port, ref_repost );
	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return p_port->p_adapter->params.rq_low_watermark - p_port->recv_mgr.depth;
}

// 	p_port->recv_lock held by caller.

static inline ULONG __free_received_NBL(
	IN		ipoib_port_t			*p_port,
	IN		NET_BUFFER_LIST 		*p_net_buffer_lists ) 
{
	ipoib_recv_desc_t	*p_desc;
	NET_BUFFER_LIST		*cur_net_buffer_list, *next_net_buffer_list;
	LONG				NBL_cnt = 0;

	PERF_DECLARE( ReturnPutRecv );

	for (cur_net_buffer_list = p_net_buffer_lists;
		 cur_net_buffer_list != NULL;
		 cur_net_buffer_list = next_net_buffer_list)
	{
		++NBL_cnt;
		next_net_buffer_list = NET_BUFFER_LIST_NEXT_NBL(cur_net_buffer_list);

		/* Get the port and descriptor from the NET_BUFFER_LIST. */
		CL_ASSERT(p_port == IPOIB_PORT_FROM_NBL( cur_net_buffer_list ));
		p_desc = IPOIB_RECV_FROM_NBL( cur_net_buffer_list );
		
		CL_ASSERT(p_desc->recv_mode == RECV_UD || p_desc->recv_mode == RECV_RC);

		cl_perf_start( ReturnPutRecv );
		if( p_desc->recv_mode == RECV_RC )
		{
			ipoib_cm_recv_desc_t *p_cm_desc = (ipoib_cm_recv_desc_t*)p_desc;
#if DBG
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_BUF,
				("RC NBL %p pkt_type %s recv_mode %s\n",
					cur_net_buffer_list,
					get_ipoib_pkt_type_str(p_desc->type),
					get_ib_recv_mode_str(p_desc->recv_mode)) );
#endif
			ipoib_cm_buf_mgr_put_recv( p_port, p_cm_desc, cur_net_buffer_list );
		}
		else
		{
			__buf_mgr_put_recv( p_port, p_desc, cur_net_buffer_list );
		}
		cl_perf_stop( &p_port->p_adapter->perf, ReturnPutRecv );
	}
	return NBL_cnt;
}


ULONG ipoib_free_received_NBL(
	IN		ipoib_port_t			*p_port,
	IN		NET_BUFFER_LIST 		*p_net_buffer_lists ) 
{
	return __free_received_NBL( p_port, p_net_buffer_lists );
}


/* Called by MiniPortDriver->MiniportReturnNetBufferLists()),
 * hence NBL can come from UD or RC send/recv.
 * see ipoib_driver.cpp for reference.
 */

void
ipoib_return_net_buffer_list(
	IN				NDIS_HANDLE					adapter_context,
	IN				NET_BUFFER_LIST				*p_net_buffer_lists,
	IN				ULONG						return_flags)
{
	ipoib_port_t		*p_port;
	int32_t				shortage;
	LONG				NBL_cnt = 0;
	
	PERF_DECLARE( ReturnPacket );
	PERF_DECLARE( ReturnRepostRecv );

	IPOIB_ENTER( IPOIB_DBG_BUF );

	UNUSED_PARAM( return_flags );
	
	p_port = ((ipoib_adapter_t*)adapter_context)->p_port;
	CL_ASSERT( p_net_buffer_lists );
	if ( !p_port ) {
		ASSERT(p_port);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("<NULL> port pointer; already cleared?\n") );
		return;
	}

	cl_perf_start( ReturnPacket );
	cl_spinlock_acquire( &p_port->recv_lock );

	NBL_cnt = __free_received_NBL( p_port, p_net_buffer_lists );

	shutter_sub( &p_port->p_adapter->recv_shutter, -NBL_cnt );

	/* Repost buffers to HW */
	cl_perf_start( ReturnRepostRecv );
	shortage = __recv_mgr_repost( p_port );
	cl_perf_stop( &p_port->p_adapter->perf, ReturnRepostRecv );
	cl_spinlock_release( &p_port->recv_lock );
	cl_perf_stop( &p_port->p_adapter->perf, ReturnPacket );

	IPOIB_EXIT( IPOIB_DBG_BUF );
}

static BOOLEAN
__recv_cb_internal(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context,
    IN              uint32_t*                    p_recv_cnt );


static IO_WORKITEM_ROUTINE __iopoib_WorkItem;

static void
__iopoib_WorkItem(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				void*						context )
{
	ipoib_port_t *p_port = ( ipoib_port_t* ) context;
	BOOLEAN WorkToDo = TRUE;
	KIRQL irql;
	uint32_t recv_cnt = 0;
	uint32_t total_recv_cnt = 0;

	UNREFERENCED_PARAMETER(p_dev_obj);

	while (WorkToDo && total_recv_cnt < 512) {
		irql = KeRaiseIrqlToDpcLevel();
		WorkToDo = __recv_cb_internal(NULL, p_port, &recv_cnt);
		KeLowerIrql(irql);
		total_recv_cnt += recv_cnt;
	}

	if (WorkToDo)
	{
		IoQueueWorkItem( p_port->pPoWorkItem,
						 (PIO_WORKITEM_ROUTINE) __iopoib_WorkItem,
						 DelayedWorkQueue,
						 p_port );
	}
	else
	{
		// Release the reference count that was incremented when queued the work item.
		ipoib_port_deref( p_port, ref_recv_cb );
	}
}

static BOOLEAN
__recv_cb_internal(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context,
	IN				uint32_t					*p_recv_cnt )
{
	ipoib_port_t		*p_port;
	ib_api_status_t		status;
	ib_wc_t				wc[MAX_RECV_WC], *p_free, *p_wc;
	int32_t				NBL_cnt, recv_cnt = 0, shortage, discarded;
	cl_qlist_t			done_list, bad_list;
	ULONG				recv_complete_flags = 0;
	BOOLEAN				res;
	BOOLEAN 			WorkToDo = FALSE;

	PERF_DECLARE( RecvCompBundle );
	PERF_DECLARE( RecvCb );
	PERF_DECLARE( PollRecv );
	PERF_DECLARE( RepostRecv );
	PERF_DECLARE( FilterRecv );
	PERF_DECLARE( BuildNBLArray );
	PERF_DECLARE( RecvNdisIndicate );
	PERF_DECLARE( RearmRecv );
	PERF_DECLARE( PutRecvList );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	cl_perf_clr( RecvCompBundle );

	cl_perf_start( RecvCb );

	NDIS_SET_SEND_COMPLETE_FLAG( recv_complete_flags,
								 NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL );

	p_port = (ipoib_port_t*)cq_context;

#if DBG
	if( h_cq ) {ASSERT( h_cq == p_port->ib_mgr.h_recv_cq );}
#endif

	cl_qlist_init( &done_list );
	cl_qlist_init( &bad_list );

	ipoib_port_ref( p_port, ref_recv_cb );

	for( p_free=wc; p_free < &wc[MAX_RECV_WC - 1]; p_free++ )
		p_free->p_next = p_free + 1;
	p_free->p_next = NULL;

	/*
	 * We'll be accessing the endpoint map so take a reference
	 * on it to prevent modifications.
	 */
	cl_obj_lock( &p_port->obj );
	cl_atomic_inc( &p_port->endpt_rdr );
	cl_obj_unlock( &p_port->obj );

	do
	{
		/* If we get here, then the list of WCs is intact. */
		p_free = wc;

		cl_perf_start( PollRecv );
		status = p_port->p_adapter->p_ifc->poll_cq( p_port->ib_mgr.h_recv_cq,
													&p_free,
													&p_wc );
		cl_perf_stop( &p_port->p_adapter->perf, PollRecv );
		CL_ASSERT( status == IB_SUCCESS || status == IB_NOT_FOUND );

		/* Look at the payload now and filter ARP and DHCP packets. */
		cl_perf_start( FilterRecv );
		recv_cnt += __recv_mgr_filter( p_port, p_wc, &done_list, &bad_list );
		cl_perf_stop( &p_port->p_adapter->perf, FilterRecv );

	} while( ( !p_free ) && ( recv_cnt < 128 )); 

	*p_recv_cnt = (uint32_t)recv_cnt;

	/* We're done looking at the endpoint map, release the reference. */
	cl_atomic_dec( &p_port->endpt_rdr );

	cl_perf_log( &p_port->p_adapter->perf, RecvCompBundle, recv_cnt );

	cl_spinlock_acquire( &p_port->recv_lock );

	/* Update our posted depth. */
	p_port->recv_mgr.depth -= recv_cnt;

	/* Return any discarded receives to the pool */
	cl_perf_start( PutRecvList );
	__buf_mgr_put_recv_list( p_port, &bad_list );
	cl_perf_stop( &p_port->p_adapter->perf, PutRecvList );

	do
	{
		/* Repost ASAP so we don't starve the RQ. */
		cl_perf_start( RepostRecv );
		shortage = __recv_mgr_repost( p_port );
		
		cl_perf_stop( &p_port->p_adapter->perf, RepostRecv );

		cl_perf_start( BuildNBLArray );
		/* Notify NDIS of any and all possible receive buffers. */
		NBL_cnt = __recv_mgr_build_NBL_array( p_port, &done_list, &discarded);
		cl_perf_stop( &p_port->p_adapter->perf, BuildNBLArray );

		/* Only indicate receives if we actually had any. */
		if( discarded && shortage > 0 )
		{
			/* We may have thrown away packets, and have a shortage */
			cl_perf_start( RepostRecv );
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ALL,
					("Out of recv descriptors, reposting again\n") );
			shortage = __recv_mgr_repost( p_port );
			cl_perf_stop( &p_port->p_adapter->perf, RepostRecv );
		}

		if( !NBL_cnt ) {
			break;
		}

		cl_spinlock_release( &p_port->recv_lock );

		cl_perf_start( RecvNdisIndicate );
		
		if (shortage <= 0) 
		{
			res = shutter_add( &p_port->p_adapter->recv_shutter, NBL_cnt );
			if (res)
			{
				NdisMIndicateReceiveNetBufferLists(
											p_port->p_adapter->h_adapter,
											p_port->recv_mgr.recv_NBL_array[0],
											NDIS_DEFAULT_PORT_NUMBER,
											NBL_cnt,
											recv_complete_flags );
			}
			else {
				cl_spinlock_acquire( &p_port->recv_lock );
				__free_received_NBL( p_port, p_port->recv_mgr.recv_NBL_array[0] );
				cl_spinlock_release( &p_port->recv_lock );
			}
			cl_perf_stop( &p_port->p_adapter->perf, RecvNdisIndicate );
		}
		else
		{
			/* If shortage >0,  IPoIB driver should regain
			   ownership of the NET_BUFFER_LIST structures immediately.
			 */
			res = shutter_add( &p_port->p_adapter->recv_shutter, 1 );
			if (res) {
				recv_complete_flags |= NDIS_RECEIVE_FLAGS_RESOURCES;
				NdisMIndicateReceiveNetBufferLists(
												p_port->p_adapter->h_adapter,
												p_port->recv_mgr.recv_NBL_array[0],
												NDIS_DEFAULT_PORT_NUMBER,
												NBL_cnt,
												recv_complete_flags );
				shutter_sub( &p_port->p_adapter->recv_shutter, -1 );
			}
			cl_perf_stop( &p_port->p_adapter->perf, RecvNdisIndicate );

			/*
			 * Cap the number of receives to put back to what we just indicated
			 * with NDIS_STATUS_RESOURCES.
			 */
			
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ALL,
				("Out of recv descriptors, SHORTAGE=%d\n",shortage) );
			
			/* Return all but the last packet to the pool. */
			cl_spinlock_acquire( &p_port->recv_lock );
			while ( NBL_cnt-- > 0)
			{
				__buf_mgr_put_recv(
					p_port,
					(ipoib_recv_desc_t *)
					IPOIB_RECV_FROM_NBL( p_port->recv_mgr.recv_NBL_array[NBL_cnt] ),
					p_port->recv_mgr.recv_NBL_array[NBL_cnt] );
			}
			__recv_mgr_repost( p_port );
			cl_spinlock_release( &p_port->recv_lock );
		}
		cl_spinlock_acquire( &p_port->recv_lock );

	} while( NBL_cnt );
	cl_spinlock_release( &p_port->recv_lock );

	if (p_free )
	{
		/*
		 * Rearm after filtering to prevent contention on the endpoint maps
		 * and eliminate the possibility of having a call to
		 * __endpt_mgr_insert find a duplicate.
		 */
		ASSERT(WorkToDo == FALSE);
		cl_perf_start( RearmRecv );

		status =
			p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_recv_cq, FALSE );

		cl_perf_stop( &p_port->p_adapter->perf, RearmRecv );
		CL_ASSERT( status == IB_SUCCESS );
	} else {
		if(h_cq) {
			// increment reference to ensure no one release the object while work
			// item is queued
			ipoib_port_ref( p_port, ref_recv_cb );
			IoQueueWorkItem( p_port->pPoWorkItem,
							 (PIO_WORKITEM_ROUTINE) __iopoib_WorkItem,
							 DelayedWorkQueue,
							 p_port );
			WorkToDo = FALSE;
		} else {
			WorkToDo = TRUE;
		}
	}
	ipoib_port_deref( p_port, ref_recv_cb );
	cl_perf_stop( &p_port->p_adapter->perf, RecvCb );

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return WorkToDo;
}
	
	
static void
__recv_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	uint32_t recv_cnt;
	
	__recv_cb_internal(h_cq, cq_context, &recv_cnt);
}


static void
__recv_get_endpts(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t* const	p_desc,
	IN				ib_wc_t* const				p_wc,
		OUT			ipoib_endpt_t** const		pp_src,
		OUT			ipoib_endpt_t** const		pp_dst )
{
	ib_api_status_t		status;
	mac_addr_t			mac;
	PERF_DECLARE( GetEndptByGid );
	PERF_DECLARE( GetEndptByLid );
	PERF_DECLARE( EndptInsert );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	/* Setup our shortcut pointers based on whether GRH is valid. */
	if( p_wc->recv.ud.recv_opt & IB_RECV_OPT_GRH_VALID )
	{
		/* Lookup the source endpoints based on GID. */
		cl_perf_start( GetEndptByGid );
		*pp_src = __endpt_mgr_get_by_gid( p_port,
#if IPOIB_INLINE_RECV
										  &p_desc->buf.ib.grh.src_gid
#else
										  &p_desc->p_buf->ib.grh.src_gid
#endif
										  );

		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByGid );

		/*
		 * Lookup the destination endpoint based on GID.
		 * This is used along with the packet filter to determine
		 * whether to report this to NDIS.
		 */
		cl_perf_start( GetEndptByGid );

		*pp_dst = __endpt_mgr_get_by_gid( p_port,
#if IPOIB_INLINE_RECV
										  &p_desc->buf.ib.grh.dest_gid
#else
										  &p_desc->p_buf->ib.grh.dest_gid
#endif
										);

		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByGid );

		/*
		 * Create the source endpoint if it does not exist.  Note that we
		 * can only do this for globally routed traffic since we need the
		 * information from the GRH to generate the MAC.
		 */
		if( !*pp_src )
		{
			status = ipoib_mac_from_guid(
#if IPOIB_INLINE_RECV
								p_desc->buf.ib.grh.src_gid.unicast.interface_id,
#else
								p_desc->p_buf->ib.grh.src_gid.unicast.interface_id,
#endif
								p_port->p_adapter->params.guid_mask,
								&mac );
								
			if( status != IB_SUCCESS )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ipoib_mac_from_guid returned %s\n",
					p_port->p_adapter->p_ifc->get_err_str( status )) );
				return;
			}

			/* Create the endpoint. */
			*pp_src = ipoib_endpt_create(
										p_port,
#if IPOIB_INLINE_RECV
										&p_desc->buf.ib.grh.src_gid,
#else
										&p_desc->p_buf->ib.grh.src_gid,
#endif
										p_wc->recv.ud.remote_lid,
										p_wc->recv.ud.remote_qp );
			if( !*pp_src )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ipoib_endpt_create failed\n") );
				return;
			}
#if DBG_ENDPT
			ipoib_port_ref( p_port, ref_endpt_track );
#endif
			cl_perf_start( EndptInsert );
			cl_obj_lock( &p_port->obj );
			status = __endpt_mgr_insert( p_port, mac, *pp_src );
			cl_obj_unlock( &p_port->obj );
			if( status != IB_SUCCESS )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("__endpt_mgr_insert returned %s\n",
					p_port->p_adapter->p_ifc->get_err_str( status )) );
				*pp_src = NULL;
				return;
			}
			cl_perf_stop( &p_port->p_adapter->perf, EndptInsert );
		}
	}
	else
	{
		/*
		 * Lookup the remote endpoint based on LID.  Note that only
		 * unicast traffic can be LID routed.
		 */
		cl_perf_start( GetEndptByLid );
		*pp_src = __endpt_mgr_get_by_lid( p_port, p_wc->recv.ud.remote_lid );
		cl_perf_stop( &p_port->p_adapter->perf, GetEndptByLid );
		*pp_dst = p_port->p_local_endpt;
		CL_ASSERT( *pp_dst );
	}

	if( *pp_src && !ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) &&
		(*pp_src)->qpn != p_wc->recv.ud.remote_qp )
	{
		/* Update the QPN for the endpoint. */
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_RECV,
			("Updating QPN for MAC: %s\n", mk_mac_str(&(*pp_src)->mac)) );
		(*pp_src)->qpn = p_wc->recv.ud.remote_qp;
	}

	if( *pp_src && *pp_dst )
	{
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
			("\n\tsrc-EP %s MAC %s dst-EP %s MAC %s \n",
				(*pp_src)->tag, mk_mac_str(&(*pp_src)->mac),
				(*pp_dst)->tag, mk_mac_str2(&(*pp_dst)->mac)) );
	}

	XIPOIB_EXIT( IPOIB_DBG_RECV );
}


static int32_t
__recv_mgr_filter(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_done_wc_list,
		OUT			cl_qlist_t* const			p_done_list,
		OUT			cl_qlist_t* const			p_bad_list )
{
	ipoib_recv_desc_t		*p_desc;
	ib_wc_t					*p_wc;
	ipoib_pkt_t				*p_ipoib;
	eth_pkt_t				*p_eth;
	ipoib_endpt_t			*p_src, *p_dst;
	ib_api_status_t			status;
	uint32_t				len;
	int32_t					recv_cnt = 0;
	PERF_DECLARE( GetRecvEndpts );
	PERF_DECLARE( RecvGen );
	PERF_DECLARE( RecvTcp );
	PERF_DECLARE( RecvUdp );
	PERF_DECLARE( RecvDhcp );
	PERF_DECLARE( RecvArp );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	for( p_wc = p_done_wc_list; p_wc; p_wc = p_wc->p_next )
	{
		CL_ASSERT( p_wc->status != IB_WCS_SUCCESS || p_wc->wc_type == IB_WC_RECV );
		p_desc = (ipoib_recv_desc_t*)(uintn_t)p_wc->wr_id;
		recv_cnt++;

		if( p_wc->status != IB_WCS_SUCCESS )
		{
			if( p_wc->status != IB_WCS_WR_FLUSHED_ERR )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed completion %s  (vendor specific %#x)\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
				ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			}
			else
			{
				IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
					("Flushed completion %s\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status )) );
				ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_DROPPED, 0, 0 );
			}
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			/* Dereference the port object on behalf of the failed receive. */
			ipoib_port_deref( p_port, ref_failed_recv_wc );
			continue;
		}

		len = p_wc->length - sizeof(ib_grh_t);

		if( len < sizeof(ipoib_hdr_t) )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet < min size\n") );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			ipoib_port_deref( p_port, ref_recv_inv_len );
			continue;
		}

		if((len - sizeof(ipoib_hdr_t)) > p_port->p_adapter->params.payload_mtu)
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received ETH packet len %d > payload MTU (%d)\n",
				(len - sizeof(ipoib_hdr_t)),
				p_port->p_adapter->params.payload_mtu) );
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			ipoib_port_deref( p_port, ref_recv_inv_len );
			continue;
		}

		/* Successful completion.  Get the receive information. */
		p_desc->ndis_csum.Value =
					( ( p_wc->recv.ud.recv_opt & IB_RECV_OPT_CSUM_MASK ) >> 8 );
		p_desc->len = len + 14 - 4 ;
		cl_perf_start( GetRecvEndpts );
		__recv_get_endpts( p_port, p_desc, p_wc, &p_src, &p_dst );
		cl_perf_stop( &p_port->p_adapter->perf, GetRecvEndpts );

#if IPOIB_INLINE_RECV
		p_ipoib = &p_desc->buf.ib.pkt;
		p_eth = &p_desc->buf.eth.pkt;
#else
		p_ipoib = &p_desc->p_buf->ib.pkt;
		p_eth = &p_desc->p_buf->eth.pkt;
#endif

		if( p_src )
		{
			/* Don't report loopback traffic - we requested SW loopback. */
			if( !memcmp( &p_port->p_adapter->params.conf_mac,
				&p_src->mac, sizeof(p_port->p_adapter->params.conf_mac) ) )
			{
				/*
				 * "This is not the packet you're looking for" - don't update
				 * receive statistics, the packet never happened.
				 */
				cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
				/* Dereference the port object on behalf of the failed recv. */
				ipoib_port_deref( p_port, ref_recv_loopback );
				continue;
			}
		}

		switch( p_ipoib->hdr.type )
		{
		case ETH_PROT_TYPE_IPV6:
			if( len < (sizeof(ipoib_hdr_t) + sizeof(ipv6_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received IP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			if( p_ipoib->type.ipv6.hdr.next_header == IP_PROT_ICMPV6 )
			{
				status = __recv_icmpv6( p_port, p_wc, p_ipoib, p_eth, &p_src, p_dst );
				break;
			}
			
			if( p_ipoib->type.ipv6.hdr.next_header != IP_PROT_UDP )
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvTcp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvTcp );
				break;
			}
			//ASSERT( p_ipoib->type.ipv6.hdr.payload_length == sizeof(ipv6_hdr_t) );

			/* First packet of a UDP transfer. */
			if( len <
				(sizeof(ipoib_hdr_t) + sizeof(ipv6_hdr_t) + sizeof(udp_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received UDP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			/* Check if DHCP conversion is required. */
			if( (p_ipoib->type.ipv6.prot.udp.hdr.dst_port == DHCP_IPV6_PORT_SERVER_OR_AGENT&&
				p_ipoib->type.ipv6.prot.udp.hdr.src_port == DHCP_IPV6_PORT_CLIENT) ||
				(p_ipoib->type.ipv6.prot.udp.hdr.dst_port == DHCP_IPV6_PORT_CLIENT &&
				p_ipoib->type.ipv6.prot.udp.hdr.src_port == DHCP_IPV6_PORT_SERVER_OR_AGENT))
			{
				//TODO should be DHCP IPv6
				if( len < (sizeof(ipoib_hdr_t) + sizeof(ipv6_hdr_t) +
					sizeof(udp_hdr_t) /*+ DHCP_MIN_SIZE*/) )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Received DHCP < min size\n") );
					status = IB_INVALID_SETTING;
					break;
				}
				
				/* UDP packet with BOOTP ports in src/dst port numbers. */
				cl_perf_start( RecvDhcp );
				//TODO implement this function
				//status = __recv_dhcp_ipv6( p_port, p_ipoib, p_eth, p_src, p_dst );
				status = IB_INVALID_SETTING;
				cl_perf_stop( &p_port->p_adapter->perf, RecvDhcp );
			}
			else
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvUdp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvUdp );
			}
			break;
			
		case ETH_PROT_TYPE_IP:
			if( len < (sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received IP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			if( IP_FRAGMENT_OFFSET(&p_ipoib->type.ip.hdr) ||
				p_ipoib->type.ip.hdr.prot != IP_PROT_UDP )
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvTcp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvTcp );
				break;
			}

			/* First packet of a UDP transfer. */
			if( len <
				(sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) + sizeof(udp_hdr_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received UDP packet < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}

			/* Check if DHCP conversion is required. */
			if( (p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_SERVER &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_CLIENT) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_CLIENT &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_SERVER)||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_PROXY_SERVER &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_CLIENT) ||
				(p_ipoib->type.ip.prot.udp.hdr.dst_port == DHCP_PORT_CLIENT &&
				p_ipoib->type.ip.prot.udp.hdr.src_port == DHCP_PORT_PROXY_SERVER))
			{
				if( len < (sizeof(ipoib_hdr_t) + sizeof(ip_hdr_t) +
					sizeof(udp_hdr_t) + DHCP_MIN_SIZE) )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Received DHCP < min size\n") );
					status = IB_INVALID_SETTING;
					break;
				}
				if ((p_ipoib->type.ip.hdr.ver_hl & 0x0f) != 5 ) {
					// If there are IP options in this message, we are in
					// trouble in any case
					status = IB_INVALID_SETTING;
					break;					
				}
				/* UDP packet with BOOTP ports in src/dst port numbers. */
				cl_perf_start( RecvDhcp );
				status = __recv_dhcp( p_port, p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvDhcp );
			}
			else
			{
				/* Unfiltered.  Setup the ethernet header and report. */
				cl_perf_start( RecvUdp );
				status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
				cl_perf_stop( &p_port->p_adapter->perf, RecvUdp );
			}
			break;

		case ETH_PROT_TYPE_ARP:
			if( len < (sizeof(ipoib_hdr_t) + sizeof(ipoib_arp_pkt_t)) )
			{
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Received ARP < min size\n") );
				status = IB_INVALID_SETTING;
				break;
			}
			cl_perf_start( RecvArp );
			status = __recv_arp( p_port, p_wc, p_ipoib, p_eth, &p_src, p_dst );
			cl_perf_stop( &p_port->p_adapter->perf, RecvArp );
			len = sizeof(ipoib_hdr_t) + sizeof(arp_pkt_t);
			break;

		default:
			/* Unfiltered.  Setup the ethernet header and report. */
			cl_perf_start( RecvGen );
			status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
			cl_perf_stop( &p_port->p_adapter->perf, RecvGen );
		}

		if( status != IB_SUCCESS )
		{
			/* Update stats. */
			ipoib_inc_recv_stat( p_port->p_adapter, IP_STAT_ERROR, 0, 0 );
			cl_qlist_insert_tail( p_bad_list, &p_desc->item.list_item );
			/* Dereference the port object on behalf of the failed receive. */
			ipoib_port_deref( p_port, ref_recv_filter );
		}
		else
		{
			ip_stat_sel_t		    ip_stat;
			p_desc->len = len + sizeof(eth_hdr_t) - sizeof(ipoib_hdr_t);
			if( p_dst->h_mcast)
			{
				if( p_dst->dgid.multicast.raw_group_id[10] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[11] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[12] == 0xFF &&
					p_dst->dgid.multicast.raw_group_id[13] == 0xFF )
				{
					p_desc->type = PKT_TYPE_BCAST;
					ip_stat = IP_STAT_BCAST_BYTES;
				}
				else
				{
					p_desc->type = PKT_TYPE_MCAST;
					ip_stat = IP_STAT_MCAST_BYTES;
				}
			}
			else
			{
				p_desc->type = PKT_TYPE_UCAST;
				ip_stat = IP_STAT_UCAST_BYTES;
				
			}
			cl_qlist_insert_tail( p_done_list, &p_desc->item.list_item );
			ipoib_inc_recv_stat( p_port->p_adapter, ip_stat, p_desc->len, 1 );  
		}
	}

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return recv_cnt;
}


static ib_api_status_t
__recv_gen(
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	XIPOIB_ENTER( IPOIB_DBG_RECV );

	if( !p_src || !p_dst )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Received packet with no matching endpoints.\n") );
		return IB_NOT_DONE;
	}

	/*
	 * Fill in the ethernet header.  Note that doing so will overwrite
	 * the IPoIB header, so start by moving the information from the IPoIB
	 * header.
	 */
	p_eth->hdr.type = p_ipoib->hdr.type;
	p_eth->hdr.src = p_src->mac;
	p_eth->hdr.dst = p_dst->mac;

	if ( p_eth->hdr.dst.addr[0] == 1 && 
		 p_eth->hdr.type == ETH_PROT_TYPE_IP &&
		 p_eth->hdr.dst.addr[2] == 0x5E)  
	{
		p_eth->hdr.dst.addr[1] = 0;
		p_eth->hdr.dst.addr[3] = p_eth->hdr.dst.addr[3] & 0x7f;
	}
	if (p_dst->h_mcast)
		p_dst->is_in_use = TRUE;

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


uint8_t *FindString(uint8_t *src, uint16_t src_len,uint8_t* dst, uint16_t dest_len )
{
	int i;

	if (src_len < dest_len) 
	{
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,("string to look for is too big ...\n"));
		return NULL;
	}
	for(i=0; i <= src_len - dest_len; i++) 
	{
		if (RtlCompareMemory(src+i, dst, dest_len) == dest_len) 
		{
			// string was found
			return src+i;
		}
	}
	return NULL;
}


static ib_api_status_t
__recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	ib_api_status_t		status;
	dhcp_pkt_t			*p_dhcp;
	uint8_t				*p_option;
	uint8_t				*p_cid = NULL;
	uint8_t				*p_opt160 = NULL;
	uint16_t			p_opt160_len = 0;
	uint8_t				*p_opt160_guid = NULL;
	uint8_t				msg = 0;

	IPOIB_ENTER( IPOIB_DBG_RECV );

	UNUSED_PARAM( p_port );

	/* Create the ethernet header. */
	status = __recv_gen( p_ipoib, p_eth, p_src, p_dst );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_gen returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Fixup the payload. */
	p_dhcp = &p_eth->type.ip.prot.udp.dhcp;
	if( p_dhcp->op != DHCP_REQUEST && p_dhcp->op != DHCP_REPLY )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid DHCP op code.\n") );
		return IB_INVALID_SETTING;
	}

	/*
	 * Find the client identifier option, making sure to skip
	 * the "magic cookie".
	 */
	p_option = &p_dhcp->options[0];
	if ( *(uint32_t *)p_option != DHCP_COOKIE )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("DHCP cookie corrupted.\n") );
		return IB_INVALID_PARAMETER;
	}

	p_option = &p_dhcp->options[DHCP_COOKIE_SIZE];
	while( *p_option != DHCP_OPT_END && p_option < &p_dhcp->options[312] )
	{
		switch( *p_option )
		{
		case DHCP_OPT_PAD:
			p_option++;
			break;

		case DHCP_OPT_MSG:
			msg = p_option[2];
			p_option += 3;
			break;

		case DHCP_OPT_CLIENT_ID:
			p_cid = p_option;
			p_option += (p_option[1] + 2);
			break;

		case 160:
			p_opt160 = p_option;
			p_opt160_len = p_option[1];
			/* Fall through. */

		default:
			/*
			 * All other options have a length byte following the option code.
			 * Offset by the length to get to the next option.
			 */
			p_option += (p_option[1] + 2);
		}
	}

	switch( msg )
	{
	/* message from client */
	case DHCPDISCOVER:
	case DHCPREQUEST:
	case DHCPDECLINE:
	case DHCPRELEASE:
	case DHCPINFORM:
		if( !p_cid )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to find required Client-identifier option.\n") );
			return IB_INVALID_SETTING;
		}
		if( p_dhcp->htype != DHCP_HW_TYPE_IB )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("msg %d Invalid hardware address type %d\n",msg,p_dhcp->htype) );
			return IB_INVALID_SETTING;
		}
		break;
	/* message from DHCP server */
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		break;

	default:
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalide message type.\n") );
		return IB_INVALID_PARAMETER;
	}

	if (p_opt160) 
	{
		// On the altiris system, the system sends a packet with option 160 that has a guid in it. we need to replace it with the mac        

		// I know want to find the guid and replace it

		// make sure we don't get out of the buffer:
		p_opt160_len = (uint16_t)min(p_opt160_len, DHCP_OPTIONS_SIZE - (p_opt160 - &p_dhcp->options[0]));


		p_opt160_guid = FindString(p_opt160, p_opt160_len, p_src->dgid.raw+8, 6);
		if (p_opt160_guid != NULL) 
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,("option 160 found, guid is replaced with mac\n"));
			RtlCopyMemory( p_opt160_guid, &p_src->mac, sizeof(p_src->mac) );
		} 
		else 
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,("option 160 found but guid inside it not\n"));
		}        
	}

	
	
	p_eth->type.ip.prot.udp.hdr.chksum = 0;
	p_dhcp->htype = DHCP_HW_TYPE_ETH;
	p_dhcp->hlen = HW_ADDR_LEN;

	if( p_cid ) /* from client */
	{
		/* Validate that the length and type of the option is as required. */
		IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
					("DHCP CID received is:"));
		for (int i=0; i < coIPoIB_CID_TotalLen; ++i) {
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
				("[%d] 0x%x: \n",i, p_cid[i]));
		}
		if( p_cid[1] != coIPoIB_CID_Len )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Client-identifier length(%u) is not equal to %d as required.\n",
					p_cid[1], coIPoIB_CID_Len) );
			return IB_INVALID_SETTING;
		}
		if( p_cid[2] != coIPoIB_HwTypeIB)
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Client-identifier type is %d <> %d and wrong\n",
					p_cid[2], coIPoIB_HwTypeIB) );
			return IB_INVALID_SETTING;
		}
		/*
		 * Copy the GID value from the option so that we can make aligned
		 * accesses to the contents.
		 * Recover CID to standard type.
		 */
		p_cid[1] =  sizeof (ib_net64_t) + 1;// CID length 
		p_cid[2] =  DHCP_HW_TYPE_ETH;// CID type
		//Copy the GUID to the 3-d byte of CID
		RtlMoveMemory( &p_cid[3], &p_cid[coIPoIB_CID_TotalLen - sizeof (ib_net64_t)],
						sizeof (ib_net64_t) );
		// Clear the rest
		RtlFillMemory( &p_cid[3+sizeof (ib_net64_t)],
						coIPoIB_CID_TotalLen - 3 -sizeof (ib_net64_t), 0 );

		RtlCopyMemory( p_dhcp->chaddr, &p_src->mac, sizeof(p_src->mac) );
		RtlFillMemory( &p_dhcp->chaddr[sizeof(p_src->mac)],
						( sizeof(p_dhcp->chaddr) - sizeof(p_src->mac) ), 0 );
	}
	IPOIB_EXIT( IPOIB_DBG_RECV );
	return status;
}


static ib_api_status_t
__recv_arp(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		pp_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	ib_api_status_t			status;
	arp_pkt_t				*p_arp;
	const ipoib_arp_pkt_t	*p_ib_arp;
	ipoib_hw_addr_t			null_hw = {0};
	uint8_t					cm_capable = 0;
	boolean_t				queue_rc_conn = FALSE;

	IPOIB_ENTER( IPOIB_DBG_ARP );

	if( !p_dst )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Unknown destination endpoint\n") );
		return IB_INVALID_SETTING;
	}

	p_ib_arp = &p_ipoib->type.arp;
	p_arp = &p_eth->type.arp;

	if( p_ib_arp->hw_type != ARP_HW_TYPE_IB )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP hardware type is not IB\n") );
		return IB_INVALID_SETTING;
	}

	if( p_ib_arp->hw_size != sizeof(ipoib_hw_addr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP hardware address size is not sizeof(ipoib_hw_addr_t)\n") );
		return IB_INVALID_SETTING;
	}

	if( p_ib_arp->prot_type != ETH_PROT_TYPE_IP )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ARP protocal type not IP?\n") );
		return IB_INVALID_SETTING;
	}

	cm_capable = ipoib_addr_get_flags( &p_ib_arp->src_hw );

	/*
	 * If we don't have a source, lookup the endpoint specified in the payload.
	 */
	if( !*pp_src )
		*pp_src = __endpt_mgr_get_by_gid( p_port, &p_ib_arp->src_hw.gid );

	status = __endpt_update(p_port, pp_src, &p_ib_arp->src_hw, p_wc);

	if(status != IB_SUCCESS)
	{
		return status;
	}
	
	(*pp_src)->cm_flag = cm_capable;

	CL_ASSERT( !memcmp(
		&(*pp_src)->dgid, &p_ib_arp->src_hw.gid, sizeof(ib_gid_t) ) );

	if( p_port->p_adapter->params.cm_enabled &&
		p_ib_arp->op == ARP_OP_REP &&
		cm_capable == IPOIB_CM_FLAG_RC )
	{
		/* ARP sender is CM enabled, RC connect */
		ipoib_addr_set_sid( &(*pp_src)->conn.service_id,
							ipoib_addr_get_qpn( &p_ib_arp->src_hw ) );
		queue_rc_conn = TRUE;
	}
#if DBG
	{
		char ip_src[16];
		char ip_dst[16];

		RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->src_ip, ip_src );
		RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->dst_ip, ip_dst );

		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
			("Rx ARP-%s Src[%s] EP %s CM_cap[%s] %s SID %I64x Dst[%s] %s\n",
				(p_ib_arp->op == ARP_OP_REQ ? "REQ":"REPL"),
				ip_src,
				(*pp_src)->tag,
				((*pp_src)->cm_flag == IPOIB_CM_FLAG_RC ? "1" : "0"),
				cm_get_state_str(endpt_cm_get_state((*pp_src))),
				(*pp_src)->conn.service_id,
				ip_dst,
				p_dst->tag) );
	}
#endif

	/* Now swizzle the data. */
	p_arp->hw_type = ARP_HW_TYPE_ETH;
	p_arp->hw_size = sizeof(mac_addr_t);
	p_arp->src_hw = (*pp_src)->mac;
	p_arp->src_ip = p_ib_arp->src_ip;

	if( memcmp( &p_ib_arp->dst_hw, &null_hw, sizeof(ipoib_hw_addr_t) ) )
	{
		if( memcmp( &p_dst->dgid, &p_ib_arp->dst_hw.gid, sizeof(ib_gid_t) ) )
		{
			/*
			 * We received bcast ARP packet that means the remote port
			 * lets everyone know it was changed IP/MAC or just activated
			 */

			/* Guy: TODO: Check why this check fails in case of Voltaire IPR */

			if ( !ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) &&
				 !ib_gid_is_multicast( (const ib_gid_t*)&p_dst->dgid ) )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("ARP: is not ARP MCAST\n") );
				return IB_INVALID_SETTING;
			}

			p_arp->dst_hw = p_port->p_local_endpt->mac;
			p_dst->mac = p_port->p_local_endpt->mac;
			/*
			 * we don't care what receiver ip addr is,
			 * as long as OS' ARP table is global  ???
			 */
			p_arp->dst_ip = (net32_t)0;
		}
		else /* we have a reply to our ARP request */
		{
			p_arp->dst_hw = p_dst->mac;
			p_arp->dst_ip = p_ib_arp->dst_ip;
			CL_ASSERT( p_dst->qpn == ipoib_addr_get_qpn( &p_ib_arp->dst_hw ) );
#if IPOIB_CM
#if DBG
			{
				char ip_src[16];
				char ip_dst[16];

				RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->src_ip, ip_src );
				RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->dst_ip, ip_dst );

				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
					("Recv'ed BC? ARP-%s Src[%s] EP %s CM_cap[%s] %s Dst[%s] %s\n",
						(p_ib_arp->op == ARP_OP_REQ ? "REQ":"REPL"),
						ip_src,
						(*pp_src)->tag,
						((*pp_src)->cm_flag == IPOIB_CM_FLAG_RC ? "1" : "0"),
						cm_get_state_str(endpt_cm_get_state((*pp_src))),
						ip_dst,
						p_dst->tag) );
			}
#endif
			if( queue_rc_conn )
			{
				/* Received our ARP reply and the remote RC flag is set, 
	 	 		 * Queue an active RC connection to the remote EP.
	 	 		 */
				if( endpt_cm_get_state( (*pp_src) ) == IPOIB_CM_DISCONNECTED )
				{
#if DBG
					{
						char ip_src[16];

						RtlIpv4AddressToStringA((IN_ADDR*)&p_ib_arp->src_ip, ip_src);
						IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
							("Queue RC CONNECT EP %s [%s] MAC %s SID %#I64x\n",
								(*pp_src)->tag, ip_src,
								mk_mac_str(&(*pp_src)->mac),
								(*pp_src)->conn.service_id) );
					}
#endif
					endpt_queue_cm_connection( p_port, *pp_src );
				}
			}
#endif	// IPOIB_CM
		}
	}
	else /* we got ARP request */
	{
		memset( &p_arp->dst_hw, 0, sizeof(mac_addr_t) );
		p_arp->dst_ip = p_ib_arp->dst_ip;
#if IPOIB_CM && DBG && 0
		{
			char ip_src[16];
			char ip_dst[16];

			RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->src_ip, ip_src );
			RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->dst_ip, ip_dst );

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
				("Recv'ed BC ARP-%s Src[%s] EP %s CM_cap[%s] %s Dst[%s] %s\n",
					(p_ib_arp->op == ARP_OP_REQ ? "REQ":"REPL"),
					ip_src,
					(*pp_src)->tag,
					(cm_capable == IPOIB_CM_FLAG_RC ? "1" : "0"),
					cm_get_state_str(endpt_cm_get_state((*pp_src))),
					ip_dst,
					p_dst->tag) );
		}
#endif
	}

	/*
	 * Create the ethernet header.  Note that this is done last so that
	 * we have a chance to create a new endpoint.
	 */
	status = __recv_gen( p_ipoib, p_eth, *pp_src, p_dst );

	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_gen returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_ARP );
	return IB_SUCCESS;
}

static ib_api_status_t
__recv_icmpv6(
	IN				ipoib_port_t* const			p_port,
	IN				ib_wc_t* const				p_wc,
	IN				ipoib_pkt_t* const			p_ipoib,
	OUT				eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t** const		pp_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	ib_api_status_t			status;
	ipv6_hdr_t				*p_ipv6_hdr;
	icmpv6_pkt_t            *p_icmpv6_pkt;
	icmpv6_option_t			*p_icmpv6_opt;
	IPOIB_ENTER( IPOIB_DBG_RECV );

	if( !p_dst )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Unknown destination endpoint\n") );
		return IB_INVALID_SETTING;
	}

	p_icmpv6_pkt = (icmpv6_pkt_t *) (&p_ipoib->type.ipv6.hdr + 1);

	if(p_icmpv6_pkt->hdr.type != ICMPV6_MSG_TYPE_NBR_ADV &&
	   p_icmpv6_pkt->hdr.type != ICMPV6_MSG_TYPE_NBR_SOL)
	{
		goto recv_gen;
	}

	p_ipv6_hdr = &p_ipoib->type.ipv6.hdr;

	if(CL_NTOH16(p_ipv6_hdr->payload_length) != sizeof(icmpv6_pkt_t) +
		sizeof(icmpv6_option_t))
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Malformed ICMPv6 neighbor discovery packet\n") );
		return IB_INVALID_SETTING;
	}

	p_icmpv6_opt = (icmpv6_option_t *) (p_icmpv6_pkt + 1);

	if(p_icmpv6_opt->option_type != ICMPV6_OPTION_SRC_LINK_ADDR &&
	   p_icmpv6_opt->option_type != ICMPV6_OPTION_TARGET_LINK_ADDR)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid ICMPv6 option type\n") );
		return IB_INVALID_SETTING;
	}

	if(p_icmpv6_opt->option_length != ICMPV6_IPOIB_LINK_ADDR_OPTION_LENGTH / 8)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid ICMPv6 option length\n") );
		return IB_INVALID_SETTING;
	}
		
	status = __endpt_update(p_port, pp_src, (ipoib_hw_addr_t *) p_icmpv6_opt->u.saddr.mac_addr, p_wc);

	if(status != IB_SUCCESS)
	{
		return status;
	}

	p_ipv6_hdr->payload_length = cl_hton16(CL_NTOH16(p_ipv6_hdr->payload_length) + 
		ICMPV6_ETH_LINK_ADDR_OPTION_LENGTH - ICMPV6_IPOIB_LINK_ADDR_OPTION_LENGTH);
	p_icmpv6_opt->option_length = ICMPV6_ETH_LINK_ADDR_OPTION_LENGTH / 8;
	cl_memcpy(p_icmpv6_opt->u.saddr.mac_addr, &(*pp_src)->mac, sizeof(mac_addr_t));

recv_gen:	
	status = __recv_gen( p_ipoib, p_eth, *pp_src, p_dst );

	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__recv_gen returned %s.\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}

static ib_api_status_t
__recv_mgr_prepare_NBL(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_recv_desc_t*	const	p_desc,
		OUT			NET_BUFFER_LIST** const		pp_net_buffer_list )
{
	NDIS_STATUS							status;
	uint32_t							pkt_filter;
	NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO 	chksum;

	PERF_DECLARE( GetNdisPkt );

	IPOIB_ENTER( IPOIB_DBG_RECV );

	pkt_filter = p_port->p_adapter->packet_filter;
	/* Check the packet filter. */
	switch( p_desc->type )
	{
	default:
	case PKT_TYPE_UCAST:
		
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_ALL_FUNCTIONAL ||
			pkt_filter & NDIS_PACKET_TYPE_SOURCE_ROUTING ||
			pkt_filter & NDIS_PACKET_TYPE_DIRECTED )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
			("Received UCAST PKT.\n"));
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Received UCAST PKT with ERROR !!!!\n"));
		}
		break;
	case PKT_TYPE_BCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_BROADCAST )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
			("Received BCAST PKT.\n"));
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Received BCAST PKT with ERROR!!!! pkt_filter 0x%x\n",pkt_filter));
		}
		break;
	case PKT_TYPE_MCAST:
		if( pkt_filter & NDIS_PACKET_TYPE_PROMISCUOUS ||
			pkt_filter & NDIS_PACKET_TYPE_ALL_MULTICAST ||
			pkt_filter & NDIS_PACKET_TYPE_MULTICAST )
		{
			/* OK to report. */
			status = NDIS_STATUS_SUCCESS;
			IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_RECV,
			("Received UCAST PKT.\n"));
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Received MCAST PKT with ERROR !!!!\n"));
		}
		break;
	}

	if( status != NDIS_STATUS_SUCCESS )
	{
		/* Return the receive descriptor to the pool. */
		__buf_mgr_put_recv( p_port, p_desc, NULL );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Packet filter doesn't match receive.  Dropping.\n") );
		/*
		 * Return IB_NOT_DONE since the packet has been completed,
		 * but has not consumed an array entry.
		 */
		return IB_NOT_DONE;
	}

	cl_perf_start( GetNdisPkt );
	*pp_net_buffer_list = __buf_mgr_get_NBL( p_port, p_desc );
	cl_perf_stop( &p_port->p_adapter->perf, GetNdisPkt );
	if( !*pp_net_buffer_list )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__buf_mgr_get_NBL failed\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}

	chksum.Value = 0;

	PNET_BUFFER NetBuffer = NET_BUFFER_LIST_FIRST_NB(*pp_net_buffer_list);
	NET_BUFFER_DATA_LENGTH(NetBuffer) = p_desc->len;
	
	switch( p_port->p_adapter->params.recv_chksum_offload )
	{
	  default:
		CL_ASSERT( FALSE );
	  case CSUM_DISABLED:
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) = 
		(void*)(uintn_t)chksum.Value;
		break;
	  case CSUM_ENABLED:
		/* Get the checksums directly from packet information.
		 * In this case, no one checksum can get false value
		 * If hardware checksum failed or wasn't calculated, NDIS will recalculate
		 * it again
		 */
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) =
			(void*)(uintn_t)(p_desc->ndis_csum.Value);
		break;
	  case CSUM_BYPASS:
		/* Flag the checksums as having been calculated. */
		chksum.Receive.TcpChecksumSucceeded = 
		chksum.Receive.UdpChecksumSucceeded = 
		chksum.Receive.IpChecksumSucceeded = TRUE;
		
		NET_BUFFER_LIST_INFO(*pp_net_buffer_list, TcpIpChecksumNetBufferListInfo) =
		(void*)(uintn_t)chksum.Value;
		break;
	}

	IPOIB_EXIT( IPOIB_DBG_RECV );
	return IB_SUCCESS;
}


static uint32_t
__recv_mgr_build_NBL_array(
	IN		ipoib_port_t* const		p_port,
	OUT		cl_qlist_t*     		p_done_list OPTIONAL,
	OUT		int32_t* const			p_discarded )
{
	cl_list_item_t			*p_item;
	ipoib_recv_desc_t		*p_desc;
	uint32_t				i = 0;
	ib_api_status_t			status;
	
	PERF_DECLARE( PreparePkt );

	XIPOIB_ENTER( IPOIB_DBG_RECV );

	*p_discarded = 0;

	/* to preserve ordering move existing receives to the head of p_done_list */
	if ( p_done_list ) {
		cl_qlist_insert_list_head( p_done_list, &p_port->recv_mgr.done_list );
	} else {
		p_done_list = &p_port->recv_mgr.done_list;
	}
	p_item = cl_qlist_remove_head( p_done_list );
	while( p_item != cl_qlist_end( p_done_list ) )
	{
		p_desc = (ipoib_recv_desc_t*)p_item;

		cl_perf_start( PreparePkt );
		status = __recv_mgr_prepare_NBL( p_port, p_desc,
			&p_port->recv_mgr.recv_NBL_array[i] );
		cl_perf_stop( &p_port->p_adapter->perf, PreparePkt );
		if( status == IB_SUCCESS )
		{
			CL_ASSERT( p_port->recv_mgr.recv_NBL_array[i] );

			if (i)
			{	// Link NBLs together
				NET_BUFFER_LIST_NEXT_NBL(p_port->recv_mgr.recv_NBL_array[i-1]) = 
					p_port->recv_mgr.recv_NBL_array[i];
			}
			i++;
		}
		else if( status == IB_NOT_DONE )
		{
			(*p_discarded)++;
		}
		else
		{
			IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__recv_mgr_prepare_NBL returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			/* Put all completed receives on the port's done list. */
			if ( p_done_list != &p_port->recv_mgr.done_list) {
				cl_qlist_insert_tail( &p_port->recv_mgr.done_list, p_item );
				cl_qlist_insert_list_tail( &p_port->recv_mgr.done_list, p_done_list );
			} else {
				cl_qlist_insert_head( &p_port->recv_mgr.done_list, p_item );
			}
			break;
		}

		p_item = cl_qlist_remove_head( p_done_list );
	}

	XIPOIB_EXIT( IPOIB_DBG_RECV );
	return i;
}




/******************************************************************************
*
* Send manager implementation.
*
******************************************************************************/
static void
__send_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	p_port->send_mgr.depth = 0;
	cl_qlist_init( &p_port->send_mgr.pending_list );
	cl_qpool_construct(&p_port->send_mgr.sg_pool);
	cl_qpool_construct( &p_port->send_mgr.send_pool );
	cl_spinlock_construct(&p_port->send_mgr.send_pool_lock);
   
	p_port->p_desc = NULL;
}

static ib_api_status_t
__send_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	cl_status_t		cl_status;
	
	IPOIB_ENTER(IPOIB_DBG_SEND );

	CL_ASSERT( p_port );

	static const size_t cPoolDeltaSize(1024);
	static const ULONG 	MaxNumBuffers(16384);
	
	/* Allocate the pool for async NETBUF flow (process_sg_list) */
	cl_status = cl_qpool_init(
					&p_port->send_mgr.sg_pool,
					MaxNumBuffers,
					0,
					cPoolDeltaSize,
					sizeof(cl_pool_item_t) + p_port->p_adapter->sg_list_size,
					NULL,
					NULL,
					p_port );
	
	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_POOL, 1, cl_status ); //TODO EVENT_IPOIB_SEND_POOL
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init for sends returned %#x\n",
			cl_status) );
		IPOIB_EXIT(IPOIB_DBG_SEND );
		return  IB_INSUFFICIENT_MEMORY;
	}

	cl_status = cl_qpool_init( &p_port->send_mgr.send_pool,
							   MaxNumBuffers,
							   0,
							   cPoolDeltaSize,
							   sizeof(ipoib_send_NB_SG),
							   NULL,
							   NULL,
							   p_port );
	
	if( cl_status != CL_SUCCESS )
	{
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_RECV_POOL, 1, cl_status ); //TODO EVENT_IPOIB_SEND_POOL
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("cl_qpool_init for sends returned %#x\n", cl_status) );
		cl_qpool_destroy(&p_port->send_mgr.sg_pool);
		return IB_INSUFFICIENT_MEMORY;
	}
    
	cl_spinlock_init(&p_port->send_mgr.send_pool_lock);

	//This send descriptor can't be allocated on the stack because of boundary
	// violation !!!
	p_port->p_desc = (ipoib_send_desc_t *)
						ExAllocatePoolWithTag( NonPagedPool,
											   sizeof(ipoib_send_desc_t),
											   'XMXA');
	if (!p_port->p_desc) {
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
								EVENT_IPOIB_RECV_POOL,
								1,
								CL_INSUFFICIENT_MEMORY); //TODO EVENT_IPOIB_SEND_POOL
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Allocation of port[%d] send descriptor failed\n", p_port->port_num) );
		cl_qpool_destroy(&p_port->send_mgr.send_pool);
		
		cl_spinlock_destroy(&p_port->send_mgr.send_pool_lock);
		cl_qpool_destroy(&p_port->send_mgr.sg_pool);
		return IB_INSUFFICIENT_MEMORY;
	}
	return IB_SUCCESS;
}

void
__send_complete_add_to_list(
	IN				cl_qlist_t					*p_complete_list,
	IN				ipoib_send_NB_SG					*s_buf,
	IN	NDIS_STATUS 		status)
{

	NET_BUFFER_LIST_STATUS(s_buf->p_nbl) = status;
	cl_qlist_insert_tail(p_complete_list, &s_buf->p_complete_list_item);
}

// Complete the entire list
void 
send_complete_list_complete(
	IN				cl_qlist_t					*p_complete_list,
	IN				ULONG						compl_flags)
{
	cl_list_item_t		*p_item;
	ipoib_send_NB_SG	*s_buf;

	KIRQL oldIrql = DISPATCH_LEVEL;
	if ( (compl_flags & NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL) == 0 )
	{
	   NDIS_RAISE_IRQL_TO_DISPATCH(&oldIrql);
	}
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	for( p_item = cl_qlist_remove_head( p_complete_list );
		p_item != cl_qlist_end( p_complete_list );
		p_item = cl_qlist_remove_head( p_complete_list ) )
	{
		s_buf = CONTAINING_RECORD(p_item, ipoib_send_NB_SG, p_complete_list_item);
		__send_complete_net_buffer(s_buf, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
	}
	NDIS_LOWER_IRQL(oldIrql, DISPATCH_LEVEL);

}
static void 
__pending_list_destroy(
	IN				ipoib_port_t* const			p_port,
	IN				cl_qlist_t					*p_complete_list)
{
	cl_list_item_t		*p_item;
	ipoib_send_NB_SG 	*s_buf;

	/* Complete any pending packets. */
	for( p_item = cl_qlist_remove_head( &p_port->send_mgr.pending_list );
		p_item != cl_qlist_end( &p_port->send_mgr.pending_list );
		p_item = cl_qlist_remove_head( &p_port->send_mgr.pending_list ) )
	{
		s_buf = (ipoib_send_NB_SG*) (PVOID) p_item; // TODO: Check this casting
		ASSERT(s_buf->p_port == p_port);
		ASSERT(s_buf->p_nbl);

		__send_complete_add_to_list(p_complete_list  ,s_buf, NDIS_STATUS_FAILURE);
	}
}

static void
__send_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	cl_qlist_t	complete_list;

	IPOIB_ENTER( IPOIB_DBG_SEND );
	//Destroy pending list and put all the send buffers back to pool
	//The list should be already destroyed at this point
	ASSERT(p_port->send_mgr.pending_list.count == 0);
	cl_qlist_init(&complete_list);
	cl_spinlock_acquire( &p_port->send_lock );
	__pending_list_destroy(p_port, &complete_list);
	cl_spinlock_release( &p_port->send_lock );
	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());

	// Now, destroy the send pool
	cl_qpool_destroy(&p_port->send_mgr.send_pool);
	cl_spinlock_destroy(&p_port->send_mgr.send_pool_lock);
	cl_qpool_destroy(&p_port->send_mgr.sg_pool);

	//Now, free port send descriptor
	if( p_port->p_desc )
	{
		ExFreePoolWithTag(p_port->p_desc, 'XMXA');
		p_port->p_desc = NULL;
	}

	//Lookaside list will be destroyed in __buf_mgr_destroy
	
	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static NDIS_STATUS
__send_mgr_filter(
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				MDL* const					p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG			*s_buf )
{
	NDIS_STATUS		status;
	//IPV6_HEADER		*p_ip6_hdr;

	PERF_DECLARE( FilterIp );
	PERF_DECLARE( FilterIpV6 );
	PERF_DECLARE( FilterArp );
	PERF_DECLARE( SendGen );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ASSERT( s_buf->p_send_desc );
	/*
	 * We already checked the ethernet header length, so we know it's safe
	 * to decrement the buf_len without underflowing.
	 */
	buf_len -= sizeof(eth_hdr_t);

	switch( p_eth_hdr->type )
	{
	case ETH_PROT_TYPE_IPV6:
		cl_perf_start( FilterIpV6 );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Current header type is IPv6\n") );
		status = __send_mgr_filter_ip( p_eth_hdr, p_mdl, buf_len, s_buf );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, FilterIpV6 );
		break;

	case ETH_PROT_TYPE_IP:
		cl_perf_start( FilterIp );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Current header type is IPv4\n") );
		status = __send_mgr_filter_ip( p_eth_hdr, p_mdl, buf_len, s_buf );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, FilterIp );
		break;

	case ETH_PROT_TYPE_ARP:
		cl_perf_start( FilterArp );
		// UD only
		CL_ASSERT( s_buf->p_send_desc->send_qp == s_buf->p_port->ib_mgr.h_qp );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Current header type is ARP\n") );
		status = __send_mgr_filter_arp( p_eth_hdr, p_mdl, buf_len, s_buf );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, FilterArp );
		break;

	default:
		/*
		 * The IPoIB spec doesn't define how to send non IP or ARP packets.
		 * Just send the payload UD and hope for the best.
		 */
		IPOIB_PRINT_EXIT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("UD Send non ETH IP/ARP packet type 0x%X\n",
				cl_ntoh16(p_eth_hdr->type)));

		cl_perf_start( SendGen );
		status = __send_gen( s_buf, 0 );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, SendGen );
		break;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}


ULONG 
CopyNetBuffer(
    IN	PNET_BUFFER		NetBuffer,
    IN	PUCHAR			pDest ) 
{
    ULONG  BytesCopied = 0;
    
    IPOIB_ENTER(IPOIB_DBG_SEND);

    PUCHAR pSrc = NULL;
    
    PMDL CurrentMdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    ULONG DataLength = NET_BUFFER_DATA_LENGTH(NetBuffer);

	if (DataLength > MAX_LSO_PAYLOAD_MTU) {
		ASSERT(FALSE);
		IPOIB_PRINT_EXIT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Can't copy buffer of %d because of 64K limitation\n", DataLength));
		return 0;
	}

    while (CurrentMdl && DataLength > 0)
    {
        ULONG  CurrLength;
        NdisQueryMdl(CurrentMdl, &pSrc, &CurrLength, NormalPagePriority);
        if (pSrc == NULL)
        {
			IPOIB_PRINT_EXIT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("NdisQueryMdl failed\n"));
            return 0;
        }
        // 
        //  Current buffer length is greater than the offset to the buffer
        //  
        if (CurrLength > Offset)
        { 
            pSrc += Offset;
            CurrLength -= Offset;

            if (CurrLength > DataLength)
            {
                CurrLength = DataLength;
            }
            DataLength -= CurrLength;
            memcpy( pDest, pSrc, CurrLength );
            BytesCopied += CurrLength;

            pDest += CurrLength;
            pSrc += CurrLength;
            Offset = 0;
        }
        else
        {
            ASSERT(FALSE);
            Offset -= CurrLength;
        }
        NdisGetNextMdl(CurrentMdl, &CurrentMdl);
    }

    if (DataLength > 0)
    {   
        //
        // In some cases the size in MDL isn't equal to the buffer size. In such 
        // a case we need to copy the rest of packet
        //
#ifdef _WIN64
        ASSERT((((uint64_t)pSrc % PAGE_SIZE) + DataLength) <= PAGE_SIZE);
#else
        ASSERT((((uint32_t)pSrc % PAGE_SIZE) + DataLength) <= PAGE_SIZE);
#endif
        memcpy( pDest, pSrc, DataLength );
        BytesCopied += DataLength;
    }
    
#if 0
    if ((BytesCopied != 0) && (BytesCopied < NIC_MIN_PACKET_SIZE))
    {
        memset(pDest, 0, NIC_MIN_PACKET_SIZE - BytesCopied);
    }
#endif
    
   // NdisAdjustMdlLength(pMpTxBuf->Mdl, BytesCopied);
   // NdisFlushBuffer(pMpTxBuf->Mdl, TRUE);
    
    //ASSERT(BytesCopied <= pMpTxBuf->BufferSize);

    IPOIB_EXIT(IPOIB_DBG_SEND);
    return BytesCopied;
}


static NDIS_STATUS
__send_copy(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_NB_SG *			s_buf,
	IN				UINT						total_offset)
{
	ULONG			tot_len = 0;

	// first DS does not contain IPoIB header in the case of LSO, so we set it back to 0
	int seg_index = ( total_offset == EthHeaderOffset ? 1 : 0 );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ipoib_send_desc_t *p_desc = p_port->p_desc;

	ASSERT(s_buf->p_send_buf == NULL);
	s_buf->p_send_buf = 
		(send_buf_t *) NdisAllocateFromNPagedLookasideList( &p_port->buf_mgr.send_buf_list );
	if( !s_buf->p_send_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate buffer for packet copy.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	tot_len = CopyNetBuffer(s_buf->p_curr_nb, (PUCHAR) s_buf->p_send_buf);
	if ( !tot_len ) {
		ASSERT( tot_len );
		return NDIS_STATUS_FAILURE;
	}
	
	/* Setup the work request. */
	p_desc->send_wr[0].local_ds[seg_index].vaddr = cl_get_physaddr(
		((uint8_t*)s_buf->p_send_buf) + total_offset );
	p_desc->send_wr[0].local_ds[seg_index].length = tot_len - total_offset;
	p_desc->send_wr[0].local_ds[seg_index].lkey = p_port->ib_mgr.lkey;
	p_desc->send_wr[0].wr.num_ds = seg_index+1;


	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static inline NDIS_STATUS
__send_mgr_get_eth_hdr(
	IN				PNET_BUFFER					p_net_buffer,
		OUT			MDL** const					pp_mdl,
		OUT			eth_hdr_t** const			pp_eth_hdr,
		OUT			UINT*						p_mdl_len)
{
	PUCHAR	p_head = NULL;
	IPOIB_ENTER( IPOIB_DBG_SEND );

	*pp_mdl	= NET_BUFFER_FIRST_MDL(p_net_buffer);

	NdisQueryMdl( *pp_mdl, &p_head, p_mdl_len, NormalPagePriority );
	if( ! p_head )
	{
		/* Failed to get first buffer. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("NdisQueryMdl failed.\n") );
		return NDIS_STATUS_FAILURE;
	}

	ULONG MdlDataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(p_net_buffer);
	*p_mdl_len -= MdlDataOffset;
 
	if( *p_mdl_len < sizeof(eth_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("First buffer in packet smaller than eth_hdr_t: %d.\n", *p_mdl_len) );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	*pp_eth_hdr = (eth_hdr_t*)(p_head + MdlDataOffset);

#if EXTRA_DBG
	IPOIB_PRINT_EXIT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
		("Ethernet header:\n\tsrc MAC: %s\n\tdst MAC: %s\n\tprotocol: %s\n",
		mk_mac_str(&(*pp_eth_hdr)->src),
		mk_mac_str2(&(*pp_eth_hdr)->dst),
		get_eth_packet_type_str((*pp_eth_hdr)->type)) );
#endif

	return NDIS_STATUS_SUCCESS;
}

#if !IPOIB_USE_DMA

/* Send using the MDL's page information rather than the SGL. */
static ib_api_status_t
__send_gen(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_send_desc_t* const	p_desc )
{
	uint32_t				i, j = 1;
	ULONG					offset;
	MDL						*p_mdl;
	UINT					num_pages, tot_len;
	ULONG					buf_len;
	PPFN_NUMBER				page_array;
	boolean_t				hdr_done = FALSE;
	ib_api_status_t			status;
	PNET_BUFFER				p_net_buf;

	IPOIB_ENTER( IPOIB_DBG_SEND );
	p_net_buf = NET_BUFFER_LIST_FIRST_NB(p_desc->p_netbuf_list);
	NdisQueryBuffer( p_net_buf, &num_pages, NULL, &p_mdl, &tot_len );

	if( !p_mdl )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("No buffers associated with packet.\n") );
		return IB_ERROR;
	}

	/* Remember that one of the DS entries is reserved for the IPoIB header. */
	if( num_pages >=  p_port->max_sq_sge_supported )
	{
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
			("Too many buffers(%d) to fit in WR ds_array(%u); Copying data.\n",
				num_pages, p_port->max_sq_sge_supported) );
		status = __send_copy( p_port, p_desc );
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	CL_ASSERT( tot_len > sizeof(eth_hdr_t) );
	CL_ASSERT( tot_len <= p_port->p_adapter->params.xfer_block_size );
	/*
	 * Assume that the ethernet header is always fully contained
	 * in the first page of the first MDL.  This makes for much
	 * simpler code.
	 */
	offset = MmGetMdlByteOffset( p_mdl ) + sizeof(eth_hdr_t);
	CL_ASSERT( offset <= PAGE_SIZE );

	while( tot_len )
	{
		buf_len = MmGetMdlByteCount( p_mdl );
		page_array = MmGetMdlPfnArray( p_mdl );
		CL_ASSERT( page_array );
		i = 0;
		if( !hdr_done )
		{
			CL_ASSERT( buf_len >= sizeof(eth_hdr_t) );
			/* Skip the ethernet header. */
			buf_len -= sizeof(eth_hdr_t);
			CL_ASSERT( buf_len <= p_port->p_adapter->params.payload_mtu );
			if( buf_len )
			{
				/* The ethernet header is a subset of this MDL. */
				CL_ASSERT( i == 0 );
				if( offset < PAGE_SIZE )
				{
					p_desc->send_wr[0].local_ds[j].lkey = p_port->ib_mgr.lkey;
					p_desc->send_wr[0].local_ds[j].vaddr = (page_array[i] << PAGE_SHIFT);
					/* Add the byte offset since we're on the 1st page. */
					p_desc->send_wr[0].local_ds[j].vaddr += offset;
					if( offset + buf_len > PAGE_SIZE )
					{
						p_desc->send_wr[0].local_ds[j].length = PAGE_SIZE - offset;
						buf_len -= p_desc->send_wr[0].local_ds[j].length;
					}
					else
					{
						p_desc->send_wr[0].local_ds[j].length = buf_len;
						buf_len = 0;
					}
					/* This data segment is done.  Move to the next. */
					j++;
				}
				/* This page is done.  Move to the next. */
				i++;
			}
			/* Done handling the ethernet header. */
			hdr_done = TRUE;
		}

		/* Finish this MDL */
		while( buf_len )
		{
			p_desc->send_wr[0].local_ds[j].lkey = p_port->ib_mgr.lkey;
			p_desc->send_wr[0].local_ds[j].vaddr = (page_array[i] << PAGE_SHIFT);
			/* Add the first page's offset if we're on the first page. */
			if( i == 0 )
				p_desc->send_wr[0].local_ds[j].vaddr += MmGetMdlByteOffset( p_mdl );

			if( i == 0 && (MmGetMdlByteOffset( p_mdl ) + buf_len) > PAGE_SIZE )
			{
				/* Buffers spans pages. */
				p_desc->send_wr[0].local_ds[j].length =
					PAGE_SIZE - MmGetMdlByteOffset( p_mdl );
				buf_len -= p_desc->send_wr[0].local_ds[j].length;
				/* This page is done.  Move to the next. */
				i++;
			}
			else
			{
				/* Last page of the buffer. */
				p_desc->send_wr[0].local_ds[j].length = buf_len;
				buf_len = 0;
			}
			/* This data segment is done.  Move to the next. */
			j++;
		}

		tot_len -= MmGetMdlByteCount( p_mdl );
		if( !tot_len )
			break;

		NdisGetNextBuffer( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get next buffer.\n") );
			return IB_ERROR;
		}
	}

	/* Set the number of data segments. */
	p_desc->send_wr[0].wr.num_ds = j;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return IB_SUCCESS;
}

#else


ULONG g_ipoib_send_mcast = 0;

// returns true if buffer was sent

bool 
ipoib_process_sg_list_real(
	IN	PDEVICE_OBJECT			pDO,
	IN	PVOID					pIrp,
	IN	PSCATTER_GATHER_LIST	p_sgl,
	IN	PVOID					context,
	IN	cl_qlist_t				*p_complete_list
	)
{
	NDIS_STATUS				status;
	ipoib_port_t			*p_port;
	MDL						*p_mdl;
	eth_hdr_t				*p_eth_hdr;
	UINT					mdl_len;
	bool					ret = false;
	bool					dst_is_MULTICAST, dst_is_BROADCAST;

	ib_send_wr_t			*p_wr_failed;
	NET_BUFFER_LIST			*p_net_buffer_list;
	NET_BUFFER				*p_netbuf;
	boolean_t				from_queue;
	ib_api_status_t			ib_status;
	ULONG					complete_flags = 0;
	ipoib_send_NB_SG		*s_buf;
	ipoib_send_desc_t		*p_desc;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ASSERT(p_sgl != NULL);

	UNREFERENCED_PARAMETER(pDO);
	UNREFERENCED_PARAMETER(pIrp);

	PERF_DECLARE( BuildSendDesc );
	PERF_DECLARE( GetEthHdr );
	PERF_DECLARE( QueuePacket );
	PERF_DECLARE( SendMgrQueue );
	PERF_DECLARE( PostSend );
	PERF_DECLARE( ProcessFailedSends );
	PERF_DECLARE( GetEndpt );

	++g_ipoib_send_SG_real;

	//Read Data from the buffer passed as a context
	s_buf				= (ipoib_send_NB_SG *)context;
	p_net_buffer_list	= s_buf->p_nbl;
	p_netbuf 			= s_buf->p_curr_nb;
	p_port 				= s_buf->p_port;
	p_desc				= s_buf->p_send_desc;
	p_desc->send_qp		= s_buf->p_port->ib_mgr.h_qp; // assume UD send.

	XIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_BUF,
		("Processing netbuffer list %p s_buf %p\n", p_net_buffer_list, s_buf) );

	//TODO Define this function as void if we are not in DBG mode
	//cl_qlist_check_validity(&p_port->send_mgr.pending_list);

	NDIS_SET_SEND_COMPLETE_FLAG(complete_flags,
								NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);

	CL_ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	
	p_desc->p_netbuf_list = p_net_buffer_list;
	s_buf->p_send_buf = NULL;
	p_desc->num_wrs = 1;

	DIPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
			("\nRECEIVED NB= %p with SG= %p\n********\n", p_netbuf, p_sgl) );

	/* Get the ethernet header so we can find the endpoint. */
	cl_perf_start( GetEthHdr );

	status = __send_mgr_get_eth_hdr( p_netbuf, &p_mdl, &p_eth_hdr, &mdl_len );

	cl_perf_stop( &p_port->p_adapter->perf, GetEthHdr );

	if( status != NDIS_STATUS_SUCCESS )
	{
		cl_perf_start( ProcessFailedSends );
		/* fail  net buffer */
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed to get Eth hdr - send inside process SG list\n"));
		__send_complete_add_to_list(p_complete_list  ,s_buf, status);

		
		cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
		ret = true;
		goto send_end;
	}

	dst_is_MULTICAST = ETH_IS_MULTICAST( p_eth_hdr->dst.addr );
	dst_is_BROADCAST = ETH_IS_BROADCAST( p_eth_hdr->dst.addr );

	from_queue = (boolean_t)(s_buf->p_sgl != NULL);

	if (from_queue)
	{
		cl_perf_start( GetEndpt );
		status = __endpt_mgr_ref( p_port,
								  p_eth_hdr->dst,
								  &(s_buf->p_endpt) );

		XIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
				("__endpt_mgr_ref called on EP %p\n", s_buf->p_endpt));

		cl_perf_stop( &p_port->p_adapter->perf, GetEndpt );
		if( status == NDIS_STATUS_PENDING )
		{
			s_buf->p_sgl = p_sgl;
			cl_qlist_insert_head( &p_port->send_mgr.pending_list,
				(cl_list_item_t*)s_buf  );
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
				("Insert item back to the pending list: %p \n",
				p_net_buffer_list));
			ret = false;
			goto send_end;
		}
		else if( status != NDIS_STATUS_SUCCESS )
		{
			ASSERT( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION );

			if( dst_is_MULTICAST )
			{
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
					("recived a mc packet (from the queue) %p\n",
						p_net_buffer_list));
				ib_status = ipoib_port_join_mcast( p_port,
												   p_eth_hdr->dst,
												   IB_MC_REC_STATE_FULL_MEMBER);
				if( ib_status == IB_SUCCESS )
				{
					s_buf->p_sgl = p_sgl;
					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
						("Insert MULTICAST item back to the pending list %p\n",
						 	p_net_buffer_list));					
					cl_qlist_insert_head( &p_port->send_mgr.pending_list,
										  (cl_list_item_t*) s_buf );
					ret = false;
					++g_ipoib_send_SG_pending;
					goto send_end;
				}
			}

			/*
			 * Complete the send as if we sent it - WHQL tests don't like the
			 * sends to fail.
			 */
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Bad status: packet has not been sent\n"));
			cl_perf_start( ProcessFailedSends );
			__send_complete_add_to_list(p_complete_list  ,s_buf, NDIS_STATUS_SUCCESS);

			ret = true;
			goto send_end;
		}
	}
	else //We got this Net Buffer and its SG list directly from NDIS
	{
		ASSERT(s_buf->p_sgl == NULL);
		s_buf->p_sgl = p_sgl;
		
		cl_perf_start( SendMgrQueue );
		if( dst_is_MULTICAST
			&& p_eth_hdr->type == ETH_PROT_TYPE_IP
			&& !dst_is_BROADCAST ) 
		{
			ip_hdr_t			*p_ip_hdr;
			uint8_t				*p_tmp;
			MDL					*p_ip_hdr_mdl;
			UINT				ip_hdr_mdl_len;
			
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
				("Send a Multicast NBL=%p\n", p_net_buffer_list) );
		
			g_ipoib_send_mcast++;
			
			if(mdl_len >= sizeof(ip_hdr_t) + sizeof(eth_hdr_t))
			{
				p_ip_hdr = (ip_hdr_t*)(p_eth_hdr + 1);
			}
			else
			{
				NdisGetNextMdl(p_mdl,&p_ip_hdr_mdl);
				// Extract the ip hdr 
				if( !p_ip_hdr_mdl )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Failed to get IP header buffer.\n") );
					goto mc_end;
				}	
				NdisQueryMdl( p_ip_hdr_mdl, &p_tmp, &ip_hdr_mdl_len, NormalPagePriority );
				if( !p_tmp )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Failed to get IP header.\n") );
					goto mc_end;
				}					
				if( ip_hdr_mdl_len < sizeof(ip_hdr_t) )
				{
					/* This buffer is done for.  Get the next buffer. */
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("ip_hdr_mdl_len(%d) < sizeof(ip_hdr_t)(%d) @ line #%d\n",
						ip_hdr_mdl_len,(int)sizeof(ip_hdr_t),__LINE__) );
					goto mc_end;
				}
				p_ip_hdr = (ip_hdr_t*)(p_tmp + NET_BUFFER_CURRENT_MDL_OFFSET(p_netbuf));
				
			}
			p_eth_hdr->dst.addr[1] = ((unsigned char*)&p_ip_hdr->dst_ip)[0] & 0x0f;
			p_eth_hdr->dst.addr[3] = ((unsigned char*)&p_ip_hdr->dst_ip)[1];
		}

mc_end:
		ASSERT(s_buf->p_sgl);

		status = __send_mgr_queue( p_port, p_eth_hdr, &s_buf->p_endpt );

		cl_perf_stop( &p_port->p_adapter->perf, SendMgrQueue );
		if( status == NDIS_STATUS_PENDING )
		{
			/* Queue net buffer list. */
			cl_perf_start( QueuePacket );

			cl_qlist_insert_tail( &p_port->send_mgr.pending_list,
								  (cl_list_item_t*)s_buf );
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
				("Inserting NB %p first time to the pending list\n", p_netbuf));
			
			cl_perf_stop( &p_port->p_adapter->perf, QueuePacket );
			++g_ipoib_send_SG_pending;
			ret = false;
			goto send_end;
		}

		if( status != NDIS_STATUS_SUCCESS )
		{
			ASSERT( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION );
			/*
			 * Complete the send as if we sent it - WHQL tests don't like the
			 * sends to fail.
			 */
			 //TODO - check previous comment !
			__send_complete_add_to_list(p_complete_list  ,s_buf,NDIS_STATUS_SUCCESS );

			ret = true;
			goto send_end;
		}
		// endpt ref held
	}
	cl_perf_start( BuildSendDesc );
	status = __build_send_desc( p_eth_hdr,
								p_mdl,
								mdl_len,
								s_buf );

	cl_perf_stop( &p_port->p_adapter->perf, BuildSendDesc );

	if( status != NDIS_STATUS_SUCCESS )
	{
		cl_perf_start( ProcessFailedSends );
		__send_complete_add_to_list(p_complete_list  ,s_buf, NDIS_STATUS_FAILURE);
		cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
		ret = true;
		goto send_end;
	}

	if( p_desc->send_qp != p_port->ib_mgr.h_qp &&
			p_desc->send_qp != s_buf->p_endpt->conn.h_send_qp )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
			("EP %s RESET send_qp to UD\n", s_buf->p_endpt->tag) );
		p_desc->send_qp = p_port->ib_mgr.h_qp;
	}

 // XXX+
#if DBG && 0
 if( p_desc->send_qp != p_port->ib_mgr.h_qp || p_desc->num_wrs > 1 )
 {
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
		("post_send[%s] EP %s num_wrs %d num_ds %u sgl_size %u\n",
			(p_desc->send_qp == p_port->ib_mgr.h_qp ? "UD":"RC"),
			s_buf->p_endpt->tag, p_desc->num_wrs, 
			p_desc->send_wr[0].wr.num_ds,
			get_sgl_size(p_sgl)) );
 }
#endif
// XXX-

	/* Post the WR. */
	cl_perf_start( PostSend );
	ib_status = p_port->p_adapter->p_ifc->post_send( p_desc->send_qp,
													 &(p_desc->send_wr[0].wr),
													 &p_wr_failed );
	p_port->n_no_progress = 0; // IPoIB can send, reset the failure counter
	ret = true;
	cl_perf_stop( &p_port->p_adapter->perf, PostSend );
	if( ib_status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] %s ib_post_send returned %s\n", 
				p_port->port_num,
				(p_desc->send_qp == p_port->ib_mgr.h_qp ? "UD":"RC"),
				p_port->p_adapter->p_ifc->get_err_str( ib_status )) );
		cl_perf_start( ProcessFailedSends );
		__send_complete_add_to_list(p_complete_list  ,s_buf, NDIS_STATUS_FAILURE);
		cl_perf_stop( &p_port->p_adapter->perf, ProcessFailedSends );
		if( p_desc->send_qp == p_port->ib_mgr.h_qp )
		{
			/* Flag the adapter as hung since posting is busted. */
			p_port->p_adapter->hung = TRUE;
		}
		else
		{
			/* revert to UD only send */
			endpt_cm_set_state( s_buf->p_endpt, IPOIB_CM_DISCONNECT_CLEANUP );
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Port[%d] Release CM Tx resources on EP %s\n",
					p_port->port_num, s_buf->p_endpt->tag) );
			cm_release_resources( p_port, s_buf->p_endpt, 1 );
			endpt_cm_set_state( s_buf->p_endpt, IPOIB_CM_DISCONNECTED );
		}
	}
	else
	{
		++g_ipoib_send;
		cl_atomic_inc( &p_port->send_mgr.depth );
	}

send_end:		
			
	IPOIB_EXIT( IPOIB_DBG_SEND );

	return ret;
}


// This routine is called (aka callout) from within the execution of the Windows
// routine NdisMAllocateNetBufferSGList().

void 
ipoib_process_sg_list(
    IN  PDEVICE_OBJECT          pDO,
    IN  PVOID                   pIrp,
    IN  PSCATTER_GATHER_LIST    p_sgl,
    IN  PVOID                   context )
{
	ipoib_send_NB_SG *  s_buf = (ipoib_send_NB_SG *)context;
	ipoib_port_t*		p_port = s_buf->p_port;
	cl_qlist_t	complete_list;
	cl_qlist_init(&complete_list);

	cl_spinlock_acquire( &p_port->send_lock );

	++g_ipoib_send_SG;
	if (g_ipoib_send_SG > 2) {
#if 0
		ASSERT( g_ipoib_send_SG-2 <= g_ipoib_send
									+ g_ipoib_send_mcast
									+ p_port->send_mgr.pending_list.count
									+ g_ipoib_send_SG_failed );
#endif
	}

	ipoib_process_sg_list_real( pDO, pIrp, p_sgl, context, &complete_list );

	if (g_ipoib_send_SG > 1) {
#if 0
		ASSERT( g_ipoib_send_SG-1 <= g_ipoib_send 
										+ g_ipoib_send_mcast
										+ p_port->send_mgr.pending_list.count
										+ g_ipoib_send_SG_failed );
#endif
	}
	cl_spinlock_release( &p_port->send_lock );
	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());
}


static NDIS_STATUS
__send_gen(
	IN				ipoib_send_NB_SG *			s_buf,
	IN				UINT						lso_header_size OPTIONAL)
{
	NDIS_STATUS		status;
	uint32_t		i = 0; 	//Index of SG element
	uint32_t		j;  	//Index of DS elements;
	ULONG 			DataOffset	= 0; 	
	UINT			total_offset;

	PSCATTER_GATHER_LIST p_sgl = s_buf->p_sgl;
	ipoib_send_desc_t *p_desc = s_buf->p_send_desc;

	PERF_DECLARE( SendCopy );

	IPOIB_ENTER( IPOIB_DBG_SEND );
	
	/* We calculate the amount of bytes to skip over ETH header in a case of normal send or
	  * LSO header in a case of LSO.
	  * But in the case of LSO we replace last 4 bytes of ETH header by IPoIB header
	  * 
	  * Thus, the calulation should be:
	  * Normal send: offset = sizeof ETH header
	  * LSO		  : offset = sizeof ETH header+sizeof IP header+ sizeof TCP header 
  					== sizeof LSO header + (sizeof ETH header-sizeof IPoIB header)
	  */
	if ( lso_header_size ) 
	{
		total_offset = lso_header_size + EthIPoIBHeaderOffset;
		j = 0;
	}
	else
	{
		total_offset =  EthHeaderOffset;
		j = 1; //Skip on the first DS, because it alredy contain IPoIB header
	}
	
	if( !p_sgl )
	{
		ASSERT( p_sgl );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to get SGL from packet.\n") );
		return NDIS_STATUS_FAILURE;
	}

	/* TODO: Copy only essential data
	   That is, copy only 2 chunks of ETH header if it contained in several
	   first SG elements.
	   Copy only N+1-MAX_SEND_SGE, where N is a lenght of SG List
	   Remember that one of the DS entries is reserved for the IPoIB header.
	*/
	if( ( p_sgl->NumberOfElements >=  s_buf->p_port->max_sq_sge_supported ||
		p_sgl->Elements[0].Length < sizeof(eth_hdr_t)) )
	{
		IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Too many buffers %d to fit in WR ds_array[%d] \
			 Or buffer[0] length %d < Eth header. Copying data.\n",
			p_sgl->NumberOfElements,  s_buf->p_port->max_sq_sge_supported , 
			p_sgl->Elements[0].Length ) );

		if( !s_buf->p_port->p_adapter->params.cm_enabled )
		{
			cl_perf_start( SendCopy );
			status = __send_copy( s_buf->p_port, s_buf, total_offset );
			cl_perf_stop( &s_buf->p_port->p_adapter->perf, SendCopy );
		}
		else 
		{
			status = NDIS_STATUS_RESOURCES;
		}
		IPOIB_EXIT( IPOIB_DBG_SEND );
		return status;
	}

	DataOffset= (ULONG)(NET_BUFFER_CURRENT_MDL_OFFSET(s_buf->p_curr_nb)); 
	
	/*
	 * Skip the Ethernet or LSO header. It is contained at N+1 first elements (N>=0),
	 * while (N+1) element may contain only part of it
	 */
	 	
	while( total_offset >= p_sgl->Elements[i].Length - DataOffset )
	{
		// skip the current element and increment the index
		total_offset -= ( p_sgl->Elements[i].Length - DataOffset);
		DataOffset = 0;
		i++;
	}
	
	if (total_offset > 0 )
	{
		//Handle the (N+1) element that can probably contain both Header and Data
		p_desc->send_wr[0].local_ds[j].vaddr =
			p_sgl->Elements[i].Address.QuadPart + total_offset + DataOffset;
		p_desc->send_wr[0].local_ds[j].length =
			p_sgl->Elements[i].Length - total_offset - DataOffset;
		p_desc->send_wr[0].local_ds[j].lkey = s_buf->p_port->ib_mgr.lkey;
		j++;          
		i++;
	}
			
	/* Now fill in the rest of the local data segments. */
	while( i < p_sgl->NumberOfElements )
	{
		p_desc->send_wr[0].local_ds[j].vaddr = p_sgl->Elements[i].Address.QuadPart;
		p_desc->send_wr[0].local_ds[j].length = p_sgl->Elements[i].Length;
		CL_ASSERT( p_desc->send_wr[0].local_ds[j].length > 0 );
		p_desc->send_wr[0].local_ds[j].lkey = s_buf->p_port->ib_mgr.lkey;
		i++;
		j++;
	}

	/* Set the number of data segments. */
	p_desc->send_wr[0].wr.num_ds = j;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}
#endif


static NDIS_STATUS
__send_mgr_filter_ip(
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN				ipoib_send_NB_SG*			s_buf )
{
	NDIS_STATUS		status;
	PVOID			p_ip_hdr;
	uint32_t		ip_packet_len;
	size_t			iph_size_in_bytes;
	size_t			iph_options_size;
	uint8_t			prot;
	size_t			hdr_size;
	ipoib_send_desc_t *p_desc = s_buf->p_send_desc;
	boolean_t		dst_is_multicast;
	boolean_t		dst_is_broadcast;
	boolean_t		cm_enabled = s_buf->p_port->p_adapter->params.cm_enabled;
	
	PERF_DECLARE( QueryIp );
	PERF_DECLARE( SendTcp );
	PERF_DECLARE( FilterUdp );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	CL_ASSERT( p_desc->send_qp == s_buf->p_port->ib_mgr.h_qp );	// start with UD Tx

	if( !buf_len )
	{
		cl_perf_start( QueryIp );
		NdisGetNextMdl ( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get IP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}

		NdisQueryMdl(p_mdl, &p_ip_hdr, &buf_len, NormalPagePriority);
		if( !p_ip_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query IP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, QueryIp );
	}
	else
	{
		p_ip_hdr = (PVOID) (p_eth_hdr + 1);
	}

	dst_is_multicast = ETH_IS_MULTICAST( p_eth_hdr->dst.addr );
	dst_is_broadcast = ETH_IS_BROADCAST( p_eth_hdr->dst.addr );

	if ( p_eth_hdr->type == ETH_PROT_TYPE_IPV6 ) 
	{
		// BUGBUG: need to add support for extension headers
		PIPV6_HEADER p_ip6_hdr = (PIPV6_HEADER)p_ip_hdr;

		prot = ((ipv6_hdr_t *) p_ip_hdr)->next_header;
		hdr_size = sizeof(ipv6_hdr_t);
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,("Got IPV6 Header\n") );
		ip_packet_len = cl_ntoh16( p_ip6_hdr->PayloadLength );
	}
	else //IPv4
	{
		prot = ((ip_hdr_t *) p_ip_hdr)->prot;
		hdr_size = sizeof(ip_hdr_t);
		ip_packet_len = cl_ntoh16( ((ip_hdr_t*)p_ip_hdr)->length );
		ipoib_print_ip_hdr( (ip_hdr_t *) p_ip_hdr );
	}

	if( buf_len < hdr_size )
	{
		/* This buffer is done for.  Get the next buffer. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("buf_len(%d) < hdr_size(%d) @ Line #%d\n",
				(int)buf_len,(int)hdr_size,__LINE__) );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	switch( prot )
	{
	case IP_PROT_UDP:
		cl_perf_start( FilterUdp );
		status = __send_mgr_filter_udp( p_ip_hdr, p_mdl,
										(buf_len - hdr_size), p_eth_hdr->type, s_buf );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, FilterUdp );

		if( status != NDIS_STATUS_PENDING ) {
			return status;
		}
		/* not DHCP packet, keep going */
		if( !dst_is_multicast && cm_enabled && s_buf->p_endpt->conn.h_send_qp )
			p_desc->send_qp = s_buf->p_endpt->conn.h_send_qp;

		break;	
	case IP_PROT_TCP:
		if( !cm_enabled )
			break;

		if( s_buf->p_endpt->conn.h_send_qp &&
			ip_packet_len <= s_buf->p_endpt->tx_mtu )
		{
			p_desc->send_qp = s_buf->p_endpt->conn.h_send_qp;	// RC Tx
		}
		break;
	case IP_PROT_IGMP:
		/*
		In igmp packet I saw that iph arrive in 2 NDIS_BUFFERs:
		1. iph
		2. ip options
			So to get the IGMP packet we need to skip the ip options NDIS_BUFFER
		*/
			iph_size_in_bytes = (((ip_hdr_t*)p_ip_hdr)->ver_hl & 0xf) * 4;
			iph_options_size = iph_size_in_bytes - buf_len;
			buf_len -= sizeof(ip_hdr_t);//without ipheader

		/*
		Could be a case that arrived igmp packet not from type IGMPv2 ,
		but IGMPv1 or IGMPv3.
		We anyway pass it to __send_mgr_filter_igmp_v2().
		*/
		status = __send_mgr_filter_igmp_v2( s_buf->p_port,
											(ip_hdr_t*) p_ip_hdr,
											iph_options_size,
											p_mdl,
											buf_len );
		if( status != NDIS_STATUS_SUCCESS )
			return status;
		break;
		
	case IP_PROT_ICMP:
		break;

	case IPPROTO_HOPOPTS:
		break;	

	case IP_PROT_ICMPV6:
		status = __send_mgr_filter_icmpv6( (ipv6_hdr_t*) p_ip_hdr,
											p_mdl,
											(buf_len - hdr_size), s_buf);
		
		if( status != NDIS_STATUS_PENDING ) {
			return status;
		}
		break;
		
	default:
		break;
	}
	
	CL_ASSERT( s_buf->p_endpt );

	if( p_desc->send_qp == s_buf->p_port->ib_mgr.h_qp ) // UD Tx
	{
		if( ip_packet_len > s_buf->p_endpt->tx_mtu )
		{
			DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
				("SEND_UD needs IP fragmentation ip_pkt_len %d mtu %u\n",
					ip_packet_len, s_buf->p_endpt->tx_mtu) );

			if ( p_eth_hdr->type == ETH_PROT_TYPE_IPV6 ) 
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("IPv6 packet (len %d) wants IP fragmentation, unsupported.\n",
						ip_packet_len) );
				return NDIS_STATUS_FAILURE;
			}
			status = __build_ipv4_fragments( s_buf,
											 (ip_hdr_t* const)p_ip_hdr,
											 (uint32_t)buf_len,
											 ip_packet_len,
											 p_mdl );

			/* no need for send_gen(,0,0) as wr's & ds have already been setup. */
			IPOIB_EXIT( IPOIB_DBG_SEND );
			return status;
		}
	}
	else
	{
		/* want to send RC, can we? */
		if( ip_packet_len > s_buf->p_endpt->tx_mtu )
		{
			/* problems? */
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ERR: want RC send, IP packet Len %d > Tx payload MTU %u\n",
					ip_packet_len, s_buf->p_endpt->tx_mtu) );
			return NDIS_STATUS_INVALID_LENGTH;
		}
	}

	cl_perf_start( SendTcp );
	status = __send_gen( s_buf, 0 );
	cl_perf_stop( &s_buf->p_port->p_adapter->perf, SendTcp );

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}

static NDIS_STATUS
__send_mgr_filter_icmpv6(
	IN		const	ipv6_hdr_t* const			p_ipv6_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG*			s_buf)
{
	icmpv6_pkt_t    *p_icmpv6_orig_pkt;
	icmpv6_pkt_t    *p_icmpv6_new_pkt;

	ipoib_send_desc_t *p_desc = s_buf->p_port->p_desc;

	IPOIB_ENTER( IPOIB_DBG_SEND );
	if( !buf_len )
	{
		NdisGetNextMdl( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryMdl( p_mdl, &p_icmpv6_orig_pkt, &buf_len, NormalPagePriority );
		if( !p_icmpv6_orig_pkt )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		p_icmpv6_orig_pkt = (icmpv6_pkt_t*)(p_ipv6_hdr + 1);
	}

	if( buf_len < sizeof(icmpv6_hdr_t) )
	{
		/* This buffer is done for.  Get the next buffer. */
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer too small for ICMPv6 packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}	

	if(p_icmpv6_orig_pkt->hdr.type != ICMPV6_MSG_TYPE_NBR_ADV &&
	   p_icmpv6_orig_pkt->hdr.type != ICMPV6_MSG_TYPE_NBR_SOL)
	{
		return NDIS_STATUS_PENDING;
	}

	/* Allocate our scratch buffer. */
	ASSERT(s_buf->p_send_buf == NULL);
	s_buf->p_send_buf = (send_buf_t*)
							NdisAllocateFromNPagedLookasideList(
										&s_buf->p_port->buf_mgr.send_buf_list );
	if( !s_buf->p_send_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to query ARP packet buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}

	ipv6_hdr_t *p_new_ipv6 = &s_buf->p_send_buf->ipv6.hdr;

	cl_memcpy(p_new_ipv6, p_ipv6_hdr, sizeof(ipv6_hdr_t));

	p_icmpv6_new_pkt = (icmpv6_pkt_t *) (p_new_ipv6 + 1);

	cl_memcpy(p_icmpv6_new_pkt, p_icmpv6_orig_pkt, buf_len);

	icmpv6_option_t *p_new_option = (icmpv6_option_t *) (p_icmpv6_new_pkt + 1);
	p_new_option->option_type = (p_icmpv6_orig_pkt->hdr.type == ICMPV6_MSG_TYPE_NBR_SOL) ?
								ICMPV6_OPTION_SRC_LINK_ADDR : ICMPV6_OPTION_TARGET_LINK_ADDR;
	p_new_option->option_length = ICMPV6_IPOIB_LINK_ADDR_OPTION_LENGTH / 8;

	ipoib_hw_addr_t* link_addr = (ipoib_hw_addr_t *) p_new_option->u.saddr.mac_addr;

	ipoib_addr_set_qpn( link_addr, s_buf->p_port->ib_mgr.qpn );

	ib_gid_set_default( &link_addr->gid,
						s_buf->p_port->p_adapter->guids.port_guid.guid );

	p_new_ipv6->payload_length = cl_hton16(sizeof(icmpv6_pkt_t) + sizeof(icmpv6_option_t));

	p_desc->send_wr[0].local_ds[1].vaddr = cl_get_physaddr( s_buf->p_send_buf );
	p_desc->send_wr[0].local_ds[1].length = sizeof(ipv6_hdr_t) + sizeof(icmpv6_pkt_t) + sizeof(icmpv6_option_t);
	p_desc->send_wr[0].local_ds[1].lkey = s_buf->p_port->ib_mgr.lkey;
	p_desc->send_wr[0].wr.num_ds = 2;
	p_desc->send_wr[0].wr.p_next = NULL;
	
	IPOIB_EXIT( IPOIB_DBG_SEND );
	return STATUS_SUCCESS;
}

static NDIS_STATUS
__send_mgr_filter_igmp_v2(
	IN				ipoib_port_t* const			p_port,
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN				size_t						iph_options_size,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len )
{
	igmp_v2_hdr_t		*p_igmp_v2_hdr = NULL;
	NDIS_STATUS			endpt_status;
	ipoib_endpt_t* 		p_endpt = NULL;
	mac_addr_t			fake_mcast_mac;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
		("buf_len = %d,iph_options_size = %d\n",
			(int)buf_len, (int)iph_options_size) );

	if( !buf_len )
	{
		// To get the IGMP packet we need to skip the ip options NDIS_BUFFER
		// (if exists)
		while ( iph_options_size )
		{
			NdisGetNextMdl( p_mdl, &p_mdl );
			if( !p_mdl )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed to get IGMPv2 header buffer.\n") );
				return NDIS_STATUS_FAILURE;
			}
			NdisQueryMdl( p_mdl, &p_igmp_v2_hdr, &buf_len, NormalPagePriority );
			if( !p_igmp_v2_hdr )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Failed to query IGMPv2 header buffer.\n") );
				return NDIS_STATUS_FAILURE;
			}
			iph_options_size-=buf_len;
		}
        
		NdisGetNextMdl( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get IGMPv2 header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryMdl( p_mdl, &p_igmp_v2_hdr, &buf_len, NormalPagePriority );
		if( !p_igmp_v2_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query IGMPv2 header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		/* assuming ip header and options are in the same packet */
		p_igmp_v2_hdr = (igmp_v2_hdr_t *) GetIpPayloadPtr(p_ip_hdr);
	}
	/* Get the IGMP header length. */
	if( buf_len < sizeof(igmp_v2_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for IGMPv2 packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	// build fake mac from igmp packet group address
	fake_mcast_mac.addr[0] = 1;
	fake_mcast_mac.addr[1] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[0] & 0x0f;
	fake_mcast_mac.addr[2] = 0x5E;
	fake_mcast_mac.addr[3] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[1];
	fake_mcast_mac.addr[4] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[2];
	fake_mcast_mac.addr[5] = ((unsigned char*)&p_igmp_v2_hdr->group_address)[3];

	switch ( p_igmp_v2_hdr->type )
	{
	case IGMP_V2_MEMBERSHIP_REPORT:
		/* 
			This mean that some body open listener on this group 
			Change type of mcast endpt to SEND_RECV endpt. So mcast garbage
			collector will not delete this mcast endpt.
		*/
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Received IGMP_V2_MEMBERSHIP_REPORT message\n") );
		endpt_status = __endpt_mgr_ref( p_port, fake_mcast_mac, &p_endpt );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
				("__endpt_mgr_ref called for %p\n", p_endpt));
		if ( p_endpt )
		{
			cl_obj_lock( &p_port->obj );
			p_endpt->is_mcast_listener = TRUE;
			cl_obj_unlock( &p_port->obj );
            ipoib_endpt_deref( p_endpt );
		}
		break;

	case IGMP_V2_LEAVE_GROUP:
		/* 
			This mean that somebody CLOSE listener on this group .
		    Change type of mcast endpt to SEND_ONLY endpt. So mcast 
			garbage collector will delete this mcast endpt next time.
		*/
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			     ("Received IGMP_V2_LEAVE_GROUP message\n") );
		endpt_status = __endpt_mgr_ref( p_port, fake_mcast_mac, &p_endpt );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("__endpt_mgr_ref called for %p\n", p_endpt));
		if ( p_endpt )
		{
			cl_obj_lock( &p_port->obj );
			p_endpt->is_mcast_listener = FALSE;
			p_endpt->is_in_use = FALSE;
			cl_obj_unlock( &p_port->obj );
			ipoib_endpt_deref( p_endpt );
		}

		__port_do_mcast_garbage(p_port);

		break;

	case IGMP_VERSION3_REPORT_TYPE:
		XIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
		     ("IGMP_VERSION3_REPORT_TYPE: 0x%x, unsupported\n",
				p_igmp_v2_hdr->type ) );
		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			     ("Send Unknown IGMP message: 0x%x \n", p_igmp_v2_hdr->type ) );
		break;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
__send_mgr_filter_udp(
	IN		const	void* const				p_ip_hdr,
	IN				MDL*					p_mdl,
	IN				size_t					buf_len,
	IN				net16_t					ethertype,
	IN	OUT			ipoib_send_NB_SG*		s_buf )
{
	NDIS_STATUS			status;
	udp_hdr_t			*p_udp_hdr;
	PERF_DECLARE( QueryUdp );
	PERF_DECLARE( FilterDhcp );

	XIPOIB_ENTER( IPOIB_DBG_SEND );
	if ( (ethertype == ETH_PROT_TYPE_IP) &&
		(IP_FRAGMENT_OFFSET((ip_hdr_t*)p_ip_hdr) > 0) ) 
	{
		/* This is a fragmented part of UDP packet
		 * Only first packet will contain UDP header in such case
		 * So, return if offset > 0
		 */
		 return NDIS_STATUS_PENDING;
	}

	if( !buf_len )
	{
		cl_perf_start( QueryUdp );
		NdisGetNextMdl( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get UDP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryMdl( p_mdl, &p_udp_hdr, &buf_len, NormalPagePriority );
		if( !p_udp_hdr )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query UDP header buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, QueryUdp );
	}
	else
	{
		if ( ethertype == ETH_PROT_TYPE_IPV6 ) 
		{
			p_udp_hdr = (udp_hdr_t*)GetIpv6PayloadPtr((ipv6_hdr_t*)p_ip_hdr);
		}
		else //IPv4
		{
			p_udp_hdr = (udp_hdr_t*)GetIpPayloadPtr((ip_hdr_t*)p_ip_hdr);
			
		}
	}
	/* Get the UDP header and check the destination port numbers. */
		 
	if( buf_len < sizeof(udp_hdr_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for UDP packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	if ( ethertype == ETH_PROT_TYPE_IP ) {
		if( (p_udp_hdr->src_port != DHCP_PORT_CLIENT ||
			p_udp_hdr->dst_port != DHCP_PORT_SERVER) &&
			(p_udp_hdr->src_port != DHCP_PORT_SERVER ||
			p_udp_hdr->dst_port != DHCP_PORT_CLIENT) )
		{
			/* Not a DHCP packet. */
			return NDIS_STATUS_PENDING;
		}
	}
	else //IPv6
	{
		if( (p_udp_hdr->src_port != DHCP_IPV6_PORT_CLIENT||
			p_udp_hdr->dst_port != DHCP_IPV6_PORT_SERVER_OR_AGENT) &&
			(p_udp_hdr->src_port != DHCP_IPV6_PORT_SERVER_OR_AGENT ||
			p_udp_hdr->dst_port != DHCP_IPV6_PORT_CLIENT) )
		{
			/* Not a DHCP packet. */
			return NDIS_STATUS_PENDING;
		}
	}

	buf_len -= sizeof(udp_hdr_t);

	/* Allocate our scratch buffer. */
	s_buf->p_send_buf = (send_buf_t*)
		ExAllocateFromNPagedLookasideList( &s_buf->p_port->buf_mgr.send_buf_list );
	if( !s_buf->p_send_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to query DHCP packet buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	memset(s_buf->p_send_buf, 0, s_buf->p_port->buf_mgr.send_buf_len);
		
	/* Copy the IP and UDP headers. */
	//TODO: in this case we limited IP size to 20, but it can be bigger, according
	// to GetIpPayloadPtr
	if ( ethertype == ETH_PROT_TYPE_IPV6 ) 
	{
		memcpy( &s_buf->p_send_buf->ipv6.hdr, p_ip_hdr , sizeof(ipv6_hdr_t) );
	}
	else
	{
		memcpy( &s_buf->p_send_buf->ip.hdr, p_ip_hdr , sizeof(ip_hdr_t) );
	}

	memcpy( &s_buf->p_send_buf->ip.prot.udp.hdr, p_udp_hdr, sizeof(udp_hdr_t) );

	cl_perf_start( FilterDhcp );
	status = __send_mgr_filter_dhcp( (ip_hdr_t *) p_ip_hdr, p_udp_hdr, p_mdl, buf_len, ethertype, s_buf );
	cl_perf_stop( &s_buf->p_port->p_adapter->perf, FilterDhcp );

	XIPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}

static unsigned short ipchksum( unsigned short *ip, int len )
{
	unsigned long sum = 0;

	len >>= 1;
	while (len--) {
		sum += *(ip++);
		if (sum > 0xFFFF)
		{
			sum -= 0xFFFF;
		}
	}
	return (unsigned short)((~sum) & 0x0000FFFF);
}

static NDIS_STATUS
__send_mgr_filter_dhcp(
	IN		const	ip_hdr_t* const				p_ip_hdr,
	IN		const	udp_hdr_t* const			p_udp_hdr,
	IN				NDIS_BUFFER*				p_mdl,
	IN				size_t						buf_len,
	IN				net16_t						ethertype,
	IN	OUT			ipoib_send_NB_SG*			s_buf )
{
	dhcp_pkt_t			*p_dhcp;
	dhcp_pkt_t			*p_ib_dhcp;
	uint8_t				*p_option, *p_cid = NULL;
	uint8_t				msg = 0;
	size_t				len;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ipoib_send_desc_t *p_desc = s_buf->p_send_desc;
	
	if( !buf_len )
	{
		NdisGetNextMdl( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryMdl( p_mdl, &p_dhcp, &buf_len, NormalPagePriority );
		if( !p_dhcp )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to query DHCP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
	{
		p_dhcp = (dhcp_pkt_t*)(p_udp_hdr + 1);
	}

	if( ethertype == ETH_PROT_TYPE_IPV6 )
	{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("DHCPv6 packet - not supported.\n") );
			return NDIS_STATUS_FAILURE;
	}
	
	if( buf_len < DHCP_MIN_SIZE )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer not large enough for DHCP packet.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	p_ib_dhcp = &s_buf->p_send_buf->ip.prot.udp.dhcp;
	memcpy( p_ib_dhcp, p_dhcp, buf_len );

	/* Now scan through the options looking for the client identifier. */
	p_option = &p_ib_dhcp->options[4];
	while( *p_option != DHCP_OPT_END && p_option < &p_ib_dhcp->options[312] )
	{
		switch( *p_option )
		{
		case DHCP_OPT_PAD:
			p_option++;
			break;

		case DHCP_OPT_MSG:
			msg = p_option[2];
			p_option += 3;
			break;

		case DHCP_OPT_CLIENT_ID:
			p_cid = p_option;
			/* Fall through. */

		default:
			/*
			 * All other options have a length byte following the option code.
			 * Offset by the length to get to the next option.
			 */
			p_option += (p_option[1] + 2);
		}
	}

	switch( msg )
	{
	/* Client messages */
	case DHCPDISCOVER:
	case DHCPREQUEST:
		if(p_ip_hdr->dst_ip == IP_BROADCAST_ADDRESS)
			p_ib_dhcp->flags |= DHCP_FLAGS_BROADCAST;
		/* Fall through */
	case DHCPDECLINE:
	case DHCPRELEASE:
	case DHCPINFORM:
		/* Fix up the client identifier option */
		if( p_cid )
		{
			/* do we need to replace it ?  len eq ETH MAC sz 'and' MAC is mine */
			if( p_cid[1] == HW_ADDR_LEN+1 && !memcmp( &p_cid[3],
				&s_buf->p_port->p_adapter->params.conf_mac.addr, HW_ADDR_LEN ) )
			{
				/* Make sure there's room to extend it.  22 is the size of
				 * the CID option for IPoIB. (20 is the length, one byte for type
				 * and the second for length field) 
				 */
				if( buf_len + coIPoIB_CID_TotalLen - p_cid[1] > sizeof(dhcp_pkt_t) )
				{
					IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Can't convert CID to IPoIB format.\n") );
					return NDIS_STATUS_RESOURCES;
				}
				/* Move the existing options down, and add a new CID option */
				len = p_option - ( p_cid + p_cid[1] + 2 );
				p_option = p_cid + p_cid[1] + 2;
				RtlMoveMemory( p_cid, p_option, len );
				
				p_cid += len;
				p_cid[0] = DHCP_OPT_CLIENT_ID;
				p_cid[1] = coIPoIB_CID_Len;
			}
		}
		else
		{
			/*
			 * Make sure there's room to extend it.  22 is the size of
			 * the CID option for IPoIB.
			 */
			if( buf_len + coIPoIB_CID_TotalLen > sizeof(dhcp_pkt_t) )
			{
				IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Can't convert CID to IPoIB format.\n") );
				return NDIS_STATUS_RESOURCES;
			}

			p_cid = p_option;
			p_cid[0] = DHCP_OPT_CLIENT_ID;
			p_cid[1] = coIPoIB_CID_Len;
		}

		CL_ASSERT( p_cid[1] == coIPoIB_CID_Len);
		p_cid[coIPoIB_CID_TotalLen]= DHCP_OPT_END;
		
		// Copy the default prefix for ALL DHCP messages
		memcpy( &p_cid[2], &coIBDefaultDHCPPrefix[0], sizeof coIBDefaultDHCPPrefix );
		// Copy the GUID into the last 8 bytes of the CID field
		memcpy( &p_cid[2+sizeof(coIBDefaultDHCPPrefix)],
				&s_buf->p_port->p_adapter->guids.port_guid.guid,
				sizeof(s_buf->p_port->p_adapter->guids.port_guid.guid) );
		
		p_ib_dhcp->htype = DHCP_HW_TYPE_IB;

		break;

	/* Server messages. */
	case DHCPOFFER:
		p_ib_dhcp->htype = 0x20;
		p_ib_dhcp->hlen = 0x8;
	case DHCPACK:
	case DHCPNAK:
		/* don't touch server messages */
		break;

	default:
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalide message type.\n") );
		return NDIS_STATUS_INVALID_DATA;
	}

	s_buf->p_send_buf->ip.hdr.length = cl_ntoh16( sizeof(ip_hdr_t) + sizeof(udp_hdr_t) + sizeof(dhcp_pkt_t) );
	s_buf->p_send_buf->ip.prot.udp.hdr.length = cl_ntoh16( sizeof(udp_hdr_t) + sizeof(dhcp_pkt_t) );
	s_buf->p_send_buf->ip.hdr.chksum = 0;
	s_buf->p_send_buf->ip.hdr.chksum = ipchksum((unsigned short*) &s_buf->p_send_buf->ip.hdr, sizeof(ip_hdr_t));

	
	/* no chksum for udp, in a case when HW does not support checksum offload */
	s_buf->p_send_buf->ip.prot.udp.hdr.chksum = 0;
	p_desc->send_wr[0].local_ds[1].vaddr = cl_get_physaddr( s_buf->p_send_buf );
	p_desc->send_wr[0].local_ds[1].length = sizeof(ip_hdr_t)
											+ sizeof(udp_hdr_t)
											+ sizeof(dhcp_pkt_t);
	p_desc->send_wr[0].local_ds[1].lkey = s_buf->p_port->ib_mgr.lkey;
	p_desc->send_wr[0].wr.num_ds = 2;
	p_desc->send_qp = s_buf->p_port->ib_mgr.h_qp;	// UD Tx
	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS
__send_mgr_filter_arp(
	IN		const	eth_hdr_t* const			p_eth_hdr,
	IN				MDL*						p_mdl,
	IN				size_t						buf_len,
	IN	OUT			ipoib_send_NB_SG*			s_buf )
{
	arp_pkt_t			*p_arp;
	ipoib_arp_pkt_t		*p_ib_arp;
	NDIS_STATUS			status;
	mac_addr_t			null_hw = {0};
	ipoib_send_desc_t	*p_desc = s_buf->p_send_desc;
	ipoib_port_t		*p_port = s_buf->p_port;

	IPOIB_ENTER( IPOIB_DBG_ARP );

	if( !buf_len )
	{
		NdisGetNextMdl( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get ARP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
		NdisQueryMdl( p_mdl, &p_arp, &buf_len, NormalPagePriority );
		if( !p_arp )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get query ARP buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}
	}
	else
		p_arp = (arp_pkt_t*)(p_eth_hdr + 1);

	/* Single buffer ARP packet. */
	if( buf_len < sizeof(arp_pkt_t) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Buffer too short for ARP.\n") );
		return NDIS_STATUS_BUFFER_TOO_SHORT;
	}

	if( p_arp->prot_type != ETH_PROT_TYPE_IP )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Unsupported protocol type.\n") );
		return NDIS_STATUS_INVALID_DATA;
	}

	/* Allocate our scratch buffer. */
	ASSERT(s_buf->p_send_buf == NULL);
	s_buf->p_send_buf = (send_buf_t*)
							NdisAllocateFromNPagedLookasideList(
										&s_buf->p_port->buf_mgr.send_buf_list );
	if( !s_buf->p_send_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to query ARP packet buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	p_ib_arp = (ipoib_arp_pkt_t*)s_buf->p_send_buf;

	/* Convert the ARP payload. */
	p_ib_arp->hw_type = ARP_HW_TYPE_IB;
	p_ib_arp->prot_type = p_arp->prot_type;
	p_ib_arp->hw_size = sizeof(ipoib_hw_addr_t);
	p_ib_arp->prot_size = p_arp->prot_size;
	p_ib_arp->op = p_arp->op;
	
	ipoib_addr_set_qpn( &p_ib_arp->src_hw, s_buf->p_port->ib_mgr.qpn ); /* UD QPN */
	ipoib_addr_set_flags( &p_ib_arp->src_hw,
			(s_buf->p_port->p_adapter->params.cm_enabled ? IPOIB_CM_FLAG_RC : 0) );

#if DBG
	if( s_buf->p_port->p_adapter->params.cm_enabled )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
			("Set CM_FLAG_RC ib_arp.src_hw ARP %s\n",
				(p_ib_arp->op == ARP_OP_REQ ? "REQ":"REPL")) );
	}
#endif

	ib_gid_set_default( &p_ib_arp->src_hw.gid,
						s_buf->p_port->p_adapter->guids.port_guid.guid );

	p_ib_arp->src_ip = p_arp->src_ip;
	ipoib_print_arp_hdr( p_ib_arp ); 

	if( memcmp( &p_arp->dst_hw, &null_hw, sizeof(mac_addr_t) ) )
	{
		/* Get the endpoint referenced by the dst_hw address. */
		net32_t	qpn = 0;
		status = __endpt_mgr_get_gid_qpn( s_buf->p_port,
										  p_arp->dst_hw,
										  &p_ib_arp->dst_hw.gid,
										  &qpn );
		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed lookup of destination HW address\n") );
			return status;
		}
		ipoib_addr_set_qpn( &p_ib_arp->dst_hw, qpn );

#if IPOIB_CM
#if DBG
		{
		char ipa[16];
		bool req = (p_arp->op == ARP_OP_REQ);

		RtlIpv4AddressToStringA((IN_ADDR*)&p_ib_arp->dst_ip, ipa);

		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
			("Sending UD ARP-%s to [%s] EP %s CM_cap %s\n",
				(req ? "REQ" : "REP"),
				ipa,
				s_buf->p_endpt->tag,
				(s_buf->p_endpt->cm_flag == IPOIB_CM_FLAG_RC ? "1" : "0")) );
		}
#endif

#if 0 // SKIP conn establishment in favor of recv ARP-REPLY conn setup

		/* ARP Reply is not over RC per IPOIB CM spec. */
		if( p_arp->op == ARP_OP_REP && 
			s_buf->p_port->p_adapter->params.cm_enabled && 
			s_buf->p_endpt->cm_flag == IPOIB_CM_FLAG_RC )
		{
			cm_state_t	cm_state = endpt_cm_get_state(s_buf->p_endpt);

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
				("CM state %s\n",cm_get_state_str(cm_state)) );

			switch( cm_state )
			{
			case IPOIB_CM_DISCONNECTED:
				cm_state = (cm_state_t)InterlockedCompareExchange(
								(volatile LONG *)&s_buf->p_endpt->conn.state,
								IPOIB_CM_QUEUED_TO_CONNECT, IPOIB_CM_DISCONNECTED );
				{
					char ipa[16];

					RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->src_ip, ipa );

					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
				 	("Queue RC connect[%s], send ARP REPLY 2 EP %s\n",
						ipa, s_buf->p_endpt->tag) );
				}
				ipoib_addr_set_sid( &s_buf->p_endpt->conn.service_id, qpn);
				endpt_queue_cm_connection( p_port, s_buf->p_endpt );
				break;
			
			case IPOIB_CM_CONNECTING:
				break;

			default:
				break;
			}
		}
#endif // SKIP connect - see recv_cb for conn establishment
#endif	// IPOIB_CM
	}
	else
	{
		memset( &p_ib_arp->dst_hw, 0, sizeof(ipoib_hw_addr_t) );
	}
	
#if IPOIB_CM
	if( p_port->p_adapter->params.cm_enabled )
	{
		char ip_dst[16];

		RtlIpv4AddressToStringA( (IN_ADDR*)&p_ib_arp->dst_ip, ip_dst );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ARP,
			("Send ARP-%s to EP %s [%s] %s RCM_cap %d %s MAC %s\n",
			(p_ib_arp->op == ARP_OP_REP ? "REP": "REQ"),
			s_buf->p_endpt->tag,
			ip_dst,
			cm_get_state_str(endpt_cm_get_state(s_buf->p_endpt)),
			(s_buf->p_endpt->cm_flag == IPOIB_CM_FLAG_RC ? 1:0),
			(p_desc->send_qp == s_buf->p_port->ib_mgr.h_qp ? "UD":"RC"),
			mk_mac_str(&s_buf->p_endpt->mac)) );
	}
#endif

	p_ib_arp->dst_ip = p_arp->dst_ip;

	p_desc->send_wr[0].local_ds[1].vaddr = cl_get_physaddr( p_ib_arp );
	p_desc->send_wr[0].local_ds[1].length = sizeof(ipoib_arp_pkt_t);
	p_desc->send_wr[0].local_ds[1].lkey = s_buf->p_port->ib_mgr.lkey;
	p_desc->send_wr[0].wr.num_ds = 2;
	p_desc->send_wr[0].wr.p_next = NULL;

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}

static inline NDIS_STATUS
__send_mgr_queue(
	IN				ipoib_port_t* const			p_port,
	IN				eth_hdr_t* const			p_eth_hdr,
		OUT			ipoib_endpt_t** const		pp_endpt )
{
	NDIS_STATUS		status;

	PERF_DECLARE( GetEndpt );

	IPOIB_ENTER( IPOIB_DBG_SEND );

	/* Check the send queue and pend the request if not empty. */
	//No need in spinlock, this function is already under lock 
	if( cl_qlist_count( &p_port->send_mgr.pending_list ) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
			("Pending list not empty p_eth_hdr %p\n",p_eth_hdr) );
		return NDIS_STATUS_PENDING;
	}

	/* Check the send queue and pend the request if not empty. */
	if( p_port->send_mgr.depth == p_port->p_adapter->params.sq_depth )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ALL,
			("HW SQ is full No available WQEs (sq_depth %d)\n",
				p_port->send_mgr.depth) );
		return NDIS_STATUS_PENDING;
	}

	cl_perf_start( GetEndpt );
	status = __endpt_mgr_ref( p_port, p_eth_hdr->dst, pp_endpt );
	XIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("__endpt_mgr_ref called for %p\n", *pp_endpt));
	cl_perf_stop( &p_port->p_adapter->perf, GetEndpt );

	if( status == NDIS_STATUS_NO_ROUTE_TO_DESTINATION &&
		ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
				("Calling join mcast from send_mgr_queue\n"));
		if( ipoib_port_join_mcast( p_port, p_eth_hdr->dst, 
			IB_MC_REC_STATE_FULL_MEMBER) == IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
				("Multicast Mac - trying to join.\n") );
			return NDIS_STATUS_PENDING;
		}
	}
	else if ( status == NDIS_STATUS_SUCCESS && 
			  ETH_IS_MULTICAST( p_eth_hdr->dst.addr ) &&  
			  !ETH_IS_BROADCAST( p_eth_hdr->dst.addr ) )
	{
		CL_ASSERT( (*pp_endpt) );
		CL_ASSERT((*pp_endpt)->h_mcast != NULL);
		(*pp_endpt)->is_in_use = TRUE;
	}

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}


static NDIS_STATUS
__build_send_desc(
	IN				eth_hdr_t* const			p_eth_hdr,
	IN				MDL* const					p_mdl,
	IN		const	size_t						mdl_len,
	IN				ipoib_send_NB_SG			*s_buf )
{
	NDIS_STATUS			status;
	int32_t				hdr_idx;
	ULONG 				mss = 0;
	PVOID				*ppTemp;
	ipoib_send_desc_t 	*p_desc= s_buf->p_port->p_desc;
	ipoib_endpt_t		*p_endpt = s_buf->p_endpt;

	PNDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO 			p_checksum_list_info = NULL;
	PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO	p_lso_info = NULL;
	PERF_DECLARE( SendMgrFilter );
	
	IPOIB_ENTER( IPOIB_DBG_SEND );
	
	/* Store context in our reserved area of the packet. */

	ASSERT(s_buf == (ipoib_send_NB_SG *) IPOIB_INFO_FROM_NB(s_buf->p_curr_nb));

	//TODO IMPORTANT: Send buffer should not be allocated within global struct !!!
	// Otherwise, the next send may override its content
	//s_buf->p_send_buf= p_desc->p_buf;

	/* Format the send descriptor. */
	ppTemp = &NET_BUFFER_LIST_INFO( s_buf->p_nbl, TcpIpChecksumNetBufferListInfo );
    p_checksum_list_info =
				(PNDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO) ((PULONG)ppTemp);
	// Calculate LSO - no LSO if CM enabled.
	if( s_buf->p_port->p_adapter->params.lso )
	{
		ASSERT( !s_buf->p_port->p_adapter->params.cm_enabled );
		p_lso_info = (PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO)
					(PULONG) &NET_BUFFER_LIST_INFO(s_buf->p_nbl,
												TcpLargeSendNetBufferListInfo);
		ASSERT(p_lso_info);
    	ULONG LsoType = p_lso_info->Transmit.Type;

	    mss = p_lso_info->LsoV1Transmit.MSS;
		ULONG PacketLength = NET_BUFFER_DATA_LENGTH(s_buf->p_curr_nb);
		if  (PacketLength < mss)
		{
			ASSERT(FALSE);
			return NDIS_STATUS_INVALID_PACKET;
		}
	    if(LsoType == NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE)
		{
	        ASSERT(p_lso_info->LsoV2Transmit.Type ==
										NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE);
	        ASSERT(mss == p_lso_info->LsoV2Transmit.MSS);
	        ASSERT(p_lso_info->LsoV1Transmit.TcpHeaderOffset ==
									p_lso_info->LsoV2Transmit.TcpHeaderOffset);
	    }
	}

	/* Format the send descriptor. */
	hdr_idx = cl_atomic_inc( &s_buf->p_port->hdr_idx );
	hdr_idx &= (s_buf->p_port->p_adapter->params.sq_depth - 1);
	ASSERT( hdr_idx < s_buf->p_port->p_adapter->params.sq_depth );

	/* Set up IPoIB Header */
	s_buf->p_port->hdr[hdr_idx].type = p_eth_hdr->type;		

	//Init send buffer to 0
	s_buf->p_send_buf = NULL;

	if (mss && (p_lso_info->LsoV1Transmit.TcpHeaderOffset != 0))
	{ //We have LSO packet
		ASSERT( mss == (p_lso_info->LsoV1Transmit.MSS &
												p_lso_info->LsoV2Transmit.MSS));
		//ASSERT ( (mss & (1<<20)) == mss);
		status = __build_lso_desc( s_buf->p_port,
								   mss,
								   hdr_idx,
								   p_lso_info,
								   s_buf->p_curr_nb );

		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__build_lso_desc returned 0x%08X.\n", status) );
			return status;
		}
	}
	else
	{
		uint32_t	i;

		cl_perf_start( SendMgrFilter );

		/* Put first DS to be IPoIB Header */
		p_desc->send_wr[0].local_ds[0].vaddr = s_buf->p_port->hdr[hdr_idx].phys_addr;
		p_desc->send_wr[0].local_ds[0].length = sizeof(ipoib_hdr_t);
		p_desc->send_wr[0].local_ds[0].lkey = s_buf->p_port->ib_mgr.lkey;
		
		status = __send_mgr_filter( p_eth_hdr, p_mdl, mdl_len, s_buf );
		cl_perf_stop( &p_port->p_adapter->perf, SendMgrFilter );
		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__send_mgr_filter returned 0x%08X.\n", status) );
			return status;
		}

		/* want to Transmit over RC connection - is RC connection available & ready?
		 * if endpt RC connection state is OK, then, set
		 * p_desc->send_qp to be RC not UD QP. Otherwise reset send_QP to UD QP.
		 */

		if( s_buf->p_port->p_adapter->params.cm_enabled 
		    && p_desc->send_qp == p_endpt->conn.h_send_qp )	// RC Tx
		{
			cm_state_t	cstate = endpt_cm_get_state( p_endpt );

			switch( cstate )
			{
			  case IPOIB_CM_CONNECTED:

				for( i = 0; i < p_desc->num_wrs; i++ )
					p_desc->send_wr[i].wr.send_opt = 0;
				goto wr_setup;

			  case IPOIB_CM_CONNECTING:
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("Revert RC to UD, EP %s CONNECTING\n", p_endpt->tag) );
				break;

			  case IPOIB_CM_DISCONNECTED:
				break;

			  default:	
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("Revert RC to UD, RC not CONNECTED  cstate %s EP %s\n",
						cm_get_state_str(cstate), p_endpt->tag) );
				break;
			}
			// Not yet, set UD Tx.
			p_desc->send_qp = s_buf->p_port->ib_mgr.h_qp;
		}

		if( p_desc->send_qp == s_buf->p_port->ib_mgr.h_qp ) // UD Tx ?
		{
			for( i = 0; i < p_desc->num_wrs; i++ )
			{
				p_desc->send_wr[i].wr.dgrm.ud.remote_qp = p_endpt->qpn;
				p_desc->send_wr[i].wr.dgrm.ud.remote_qkey = s_buf->p_port->ib_mgr.bcast_rec.qkey;
				p_desc->send_wr[i].wr.dgrm.ud.h_av = p_endpt->h_av;
				p_desc->send_wr[i].wr.dgrm.ud.pkey_index = s_buf->p_port->pkey_index;
				p_desc->send_wr[i].wr.dgrm.ud.rsvd = NULL;
				p_desc->send_wr[i].wr.send_opt = 0;

				if( s_buf->p_port->p_adapter->params.send_chksum_offload &&
					p_checksum_list_info &&
					(p_checksum_list_info->Transmit.IsIPv4 || 
					p_checksum_list_info->Transmit.IsIPv6) )
				{
					// Set transmit checksum offloading
					if( p_checksum_list_info->Transmit.IpHeaderChecksum )
					{
						p_desc->send_wr[i].wr.send_opt |= IB_SEND_OPT_TX_IP_CSUM;
					}
					if( p_checksum_list_info->Transmit.TcpChecksum ||
						p_checksum_list_info->Transmit.UdpChecksum )
					{
						p_desc->send_wr[i].wr.send_opt |= IB_SEND_OPT_TX_TCP_UDP_CSUM;
					}
				}
			}
		}

wr_setup:
		for( i = 0; i < p_desc->num_wrs; i++ )
		{
			p_desc->send_wr[i].wr.wr_type = WR_SEND;
			p_desc->send_wr[i].wr.wr_id = 0;
			p_desc->send_wr[i].wr.ds_array = &p_desc->send_wr[i].local_ds[0];
			if( i )
			{
				p_desc->send_wr[i-1].wr.p_next = &p_desc->send_wr[i].wr;
			}
		}
		
		p_desc->send_wr[p_desc->num_wrs - 1].wr.wr_id = (uintn_t)s_buf;
		p_desc->send_wr[p_desc->num_wrs - 1].wr.send_opt |= IB_SEND_OPT_SIGNALED;

		if( p_desc->send_qp != s_buf->p_port->ib_mgr.h_qp )	// RC Tx
			p_desc->send_wr[p_desc->num_wrs - 1].wr.send_opt |=IB_SEND_OPT_SOLICITED;

		p_desc->send_wr[p_desc->num_wrs - 1].wr.p_next = NULL;
	}
	
	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}


static NDIS_STATUS
__build_lso_desc(
	IN				ipoib_port_t* const			p_port,
	IN				ULONG						mss,
	IN				int32_t						hdr_idx, 
	IN PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO p_lso_info,
	IN				NET_BUFFER					*p_netbuf)
{
	NDIS_STATUS			status;
	LsoData								TheLsoData;

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ipoib_send_NB_SG  *s_buf = IPOIB_INFO_FROM_NB(p_netbuf);
	ipoib_send_desc_t *p_desc = s_buf->p_send_desc;

	//TODO What if first NB was inserted to pending list ????
	PNET_BUFFER 	FirstBuffer  = NET_BUFFER_LIST_FIRST_NB (s_buf->p_nbl);
	ULONG 			PacketLength = NET_BUFFER_DATA_LENGTH(FirstBuffer);

	memset(&TheLsoData, 0, sizeof TheLsoData );
	status = GetLsoHeaderSize(
		FirstBuffer, 
		&TheLsoData, 
		&p_port->hdr[hdr_idx],
		p_lso_info->LsoV1Transmit.TcpHeaderOffset );

	if ( (status != NDIS_STATUS_SUCCESS ) ) 
	{
		ASSERT( status == NDIS_STATUS_SUCCESS );

		IPOIB_PRINT(TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("<-- Throwing this packet\n"));

		if( status == NDIS_STATUS_SUCCESS )
		{
			status = NDIS_STATUS_INVALID_PACKET;
		}
		return status;
	}
	ASSERT( TheLsoData.LsoHeaderSize > 0 );
		
	// Tell NDIS how much we will send.
	if( p_lso_info->LsoV1Transmit.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE )
	{
			s_buf->tcp_payload = PacketLength-TheLsoData.LsoHeaderSize;
	}

	p_desc->send_wr[0].wr.dgrm.ud.mss = mss;
	p_desc->send_wr[0].wr.dgrm.ud.header = TheLsoData.LsoBuffers.pData;
	p_desc->send_wr[0].wr.dgrm.ud.hlen = TheLsoData.LsoHeaderSize ;
	p_desc->send_wr[0].wr.dgrm.ud.remote_qp = s_buf->p_endpt->qpn;
	p_desc->send_wr[0].wr.dgrm.ud.remote_qkey = p_port->ib_mgr.bcast_rec.qkey;
	p_desc->send_wr[0].wr.dgrm.ud.h_av = s_buf->p_endpt->h_av;
	p_desc->send_wr[0].wr.dgrm.ud.pkey_index = p_port->pkey_index;
	p_desc->send_wr[0].wr.dgrm.ud.rsvd = NULL;

	p_desc->send_wr[0].wr.wr_id = (uintn_t)s_buf;
	p_desc->send_wr[0].wr.ds_array = p_desc->send_wr[0].local_ds;
	p_desc->send_wr[0].wr.wr_type = WR_LSO;
	p_desc->send_wr[0].wr.send_opt = 
		(IB_SEND_OPT_TX_IP_CSUM | IB_SEND_OPT_TX_TCP_UDP_CSUM) | IB_SEND_OPT_SIGNALED;
	
	p_desc->send_wr[0].wr.p_next = NULL;
	p_desc->send_qp = p_port->ib_mgr.h_qp;
	status = __send_gen( s_buf, TheLsoData.LsoHeaderSize );

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return status;
}


// max number of physical fragmented buffers 
#define MAX_PHYS_BUF_FRAG_ELEMENTS      0x29
#define MP_FRAG_ELEMENT SCATTER_GATHER_ELEMENT 
#define PMP_FRAG_ELEMENT PSCATTER_GATHER_ELEMENT 


typedef struct _MP_FRAG_LIST {
    ULONG NumberOfElements;
    ULONG_PTR Reserved;
    SCATTER_GATHER_ELEMENT Elements[MAX_PHYS_BUF_FRAG_ELEMENTS];
} MP_FRAG_LIST, *PMP_FRAG_LIST;


void 
CreateFragList(
    ULONG PhysBufCount,
    PNET_BUFFER NetBuff,
    ULONG PacketLength,
    PMP_FRAG_LIST pFragList )
{
    ULONG i = 0;
	int j=0;

    UINT  buf_len = NET_BUFFER_DATA_LENGTH(NetBuff);
    PMDL pMdl = NET_BUFFER_CURRENT_MDL(NetBuff);

    ULONG CurrentMdlDataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuff);
    ASSERT(MmGetMdlByteCount(pMdl) >= CurrentMdlDataOffset);

    ASSERT(NetBuff != NULL);
#if DBG
    ASSERT(PhysBufCount <= MAX_PHYS_BUF_FRAG_ELEMENTS);
#else
	UNREFERENCED_PARAMETER(PhysBufCount);
#endif
    
    ASSERT(buf_len > 0);
    UNREFERENCED_PARAMETER(PacketLength);


    IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
		("CreateFragList: NetBuff %p, Length =0x%x\n", NetBuff, buf_len));

    while ( (pMdl != NULL) && (buf_len != 0) )
    {
        PPFN_NUMBER page_array = MmGetMdlPfnArray(pMdl);
        int MdlBufCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(pMdl), MmGetMdlByteCount(pMdl));
    
        ULONG offset = MmGetMdlByteOffset(pMdl) + CurrentMdlDataOffset ;        
        ULONG MdlBytesCount = MmGetMdlByteCount(pMdl) - CurrentMdlDataOffset;
        CurrentMdlDataOffset = 0;
        
        if( MdlBytesCount == 0 )
        {
            pMdl = pMdl->Next;
            continue;
        }

        ASSERT( (buf_len > 0) && (MdlBytesCount > 0) );
 
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
			("CreateFragList: pMdl=%p, MdlBytesCount=x%x, MdlBufCount=0x%x\n",
				pMdl, MdlBytesCount, MdlBufCount));

        if (MdlBytesCount > 0)
        {
            if( buf_len > MdlBytesCount)
            {
                buf_len -= MdlBytesCount;    
            }
            else
            {
                MdlBytesCount = buf_len;
                buf_len = 0;                
            }                        
            //
            // In some cases the mdlcount is greater than needed and in the last
			// page there is 0 bytes
            //
            for (j=0; ((j< MdlBufCount) && (MdlBytesCount > 0)); j++) 
            {
                ASSERT(MdlBytesCount > 0);
                if (j ==0 ) 
                {
                    // First page
                    //
                    ULONG64 ul64PageNum = page_array[j];
                    pFragList->Elements[i].Address.QuadPart =
											(ul64PageNum << PAGE_SHIFT)+ offset;
                    if( offset + MdlBytesCount > PAGE_SIZE )
                    {
                        // the data slides behind the page boundry
                        //
                        ASSERT(PAGE_SIZE > offset);
                        pFragList->Elements[i].Length = PAGE_SIZE - offset;
                        MdlBytesCount -= pFragList->Elements[i].Length;
                    }
                    else
                    {
                        // All the data is hold in one page
                        //    
                        pFragList->Elements[i].Length = MdlBytesCount;
                        MdlBytesCount = 0;
                    }

                   IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
						("CreateFragList: j == 0, MdlBytesCount=x%x, i = %d, "
						"element.length=0x%x \n",
							MdlBytesCount, i, pFragList->Elements[i].Length));
                } 
                else 
                {
                    if (page_array[j] == (page_array[j-1] + 1))
                    {
                        
                        ULONG size = min(PAGE_SIZE, MdlBytesCount);
						i -= 1;
                        pFragList->Elements[i].Length += size;
                        MdlBytesCount -= size;
                    }
                    else 
                    {
                        // Not first page. so the data always start at the
						// begining of the page
                        //
                        ULONG64 ul64PageNum = page_array[j];
                        pFragList->Elements[i].Address.QuadPart = (ul64PageNum << PAGE_SHIFT);
                        pFragList->Elements[i].Length = min(PAGE_SIZE, MdlBytesCount);
                        MdlBytesCount -= pFragList->Elements[i].Length;
                    }

                    IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
						( "CreateFragList: j != 0, MdlBytesCount=x%x, i = %d, "
						"element.length=0x%x \n",
							MdlBytesCount, i, pFragList->Elements[i].Length));
                }                
                i++;
                ASSERT(i <= MAX_PHYS_BUF_FRAG_ELEMENTS);
            }
        }
        pMdl = pMdl->Next;
    }
        
    if (buf_len != 0)
    {
        // In some cases the size in MDL isn't equal to the buffer size.
		// In such a case we need to add the rest of packet to last chunk
        //
        ASSERT(i > 0); // To prevent array underflow
        pFragList->Elements[i-1].Length += buf_len;
        IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
			( "CreateFragList: buf_len != 0, i = %d, element.length=0x%x \n",
				i-1, pFragList->Elements[i-1].Length));
    }

    ASSERT(i <= PhysBufCount);
    pFragList->NumberOfElements = i;

#if DBG
	{
    	ULONG size = 0;
    	for (i  = 0; i <  pFragList->NumberOfElements; ++i)
    	{
        	size += pFragList->Elements[i].Length;
    	}
    	ASSERT(size == PacketLength);
	}
#endif
}


void
ipoib_port_send(
	IN	ipoib_port_t* const		p_port,
	IN	NET_BUFFER_LIST			*p_net_buffer_list,
	IN	ULONG					send_flags )
{
	NDIS_STATUS			status;
	PNET_BUFFER			p_netbuf, p_next_netbuf = NULL;
	UINT 				buf_cnt = 0;
	ULONG				send_complete_flags = 0;
	
	KIRQL				old_irql;
	
	XIPOIB_ENTER( IPOIB_DBG_SEND );
	
	if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(send_flags))
	{
		//TODO Tzachid: make an assert here to validate your IRQL
		ASSERT (KeGetCurrentIrql() == DISPATCH_LEVEL);
		old_irql = DISPATCH_LEVEL;
	} else {
		NDIS_RAISE_IRQL_TO_DISPATCH(&old_irql);
		//ASSERT (KeGetCurrentIrql() == PASSIVE_LEVEL); // Happens
	}
	NDIS_SET_SEND_COMPLETE_FLAG( send_complete_flags,
								 NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL );
	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
			("Invalid QP state: not RTS, exiting from port_send\n"));
		NET_BUFFER_LIST_STATUS(p_net_buffer_list) = NDIS_STATUS_FAILURE;
		ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
			
		NdisMSendNetBufferListsCompleteX( p_port->p_adapter,
										  p_net_buffer_list,
										  send_complete_flags );  

		NDIS_LOWER_IRQL(old_irql, DISPATCH_LEVEL);

		IPOIB_EXIT( IPOIB_DBG_SEND );
		return;
	}
	cl_obj_unlock( &p_port->obj );

	cl_spinlock_acquire( &p_port->send_lock );
	// You are already here at dispatch

	// We need to init the status here
	// When completing the send, we will set the status NDIS_STATUS_FAILURE if
	// AT LEAST one of NBs will fail.
	// That is, status can't be updated back to SUCCESS if it previosly was set
	// to FAILURE.
	NET_BUFFER_LIST_STATUS(p_net_buffer_list) = NDIS_STATUS_SUCCESS;

	for (p_netbuf = NET_BUFFER_LIST_FIRST_NB(p_net_buffer_list);
		 p_netbuf != NULL; 
		 p_netbuf = NET_BUFFER_NEXT_NB(p_netbuf))
	{
		++g_ipoib_send_SW;
		++buf_cnt;
	}

	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
		("Processing netbuffer list: %p buf_cnt %d\n", p_net_buffer_list, buf_cnt));

	ASSERT(buf_cnt);

	// Raise reference count of the NBL to the number of its NBs
	IPOIB_GET_NET_BUFFER_LIST_REF_COUNT(p_net_buffer_list) =
													(PVOID)(ULONG_PTR)buf_cnt;

	for (p_netbuf = NET_BUFFER_LIST_FIRST_NB(p_net_buffer_list);
		 p_netbuf != NULL; 
		 p_netbuf = p_next_netbuf)
	{
		p_next_netbuf = NET_BUFFER_NEXT_NB(p_netbuf);

		cl_spinlock_acquire(&p_port->send_mgr.send_pool_lock);
		ipoib_send_NB_SG * s_buf = (ipoib_send_NB_SG*) (PVOID)
									(cl_qpool_get(&p_port->send_mgr.send_pool));
		cl_spinlock_release(&p_port->send_mgr.send_pool_lock);
		if (s_buf == NULL)
		{
			ASSERT(FALSE);
			NET_BUFFER_LIST_STATUS(p_net_buffer_list) = NDIS_STATUS_RESOURCES;
			NdisMSendNetBufferListsCompleteX( p_port->p_adapter,
											  p_net_buffer_list,
											  send_complete_flags );
			break;
		}
			
		//Set all the data needed for process_sg_list
		s_buf->p_port = p_port;
		s_buf->p_sgl = NULL;
		s_buf->p_send_desc = p_port->p_desc; 
		s_buf->p_endpt = NULL;
		s_buf->p_nbl = p_net_buffer_list;
		s_buf->p_curr_nb = p_netbuf;
		//TODO remove this line from process_sg_real
		s_buf->p_send_buf = NULL;
		
		//We can also define p_sg_buf as a static member of send_buf,
		// But the problem is that we don't know it's size
		if (s_buf->p_sg_buf == NULL)
		{
			s_buf->p_sg_buf = (PVOID) cl_qpool_get(&p_port->send_mgr.sg_pool);
		}
		
		IPOIB_INFO_FROM_NB(p_netbuf) = s_buf;
		
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_BUF,
				("Netbuf to send = %p\n", p_netbuf) );

		//cl_qlist_check_validity(&p_port->send_mgr.pending_list);

		cl_spinlock_release( &p_port->send_lock );

		++g_ipoib_send_SW_in_loop;

		status = NdisMAllocateNetBufferSGList(
							p_port->p_adapter->NdisMiniportDmaHandle,
							p_netbuf,
							s_buf,
							NDIS_SG_LIST_WRITE_TO_DEVICE,
							(PUCHAR )s_buf->p_sg_buf + sizeof(cl_pool_item_t),
							p_port->p_adapter->sg_list_size);
			
		if( status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("Partial Send @ line #%d\n  %s\n",__LINE__,__FILE__) );
			ASSERT(FALSE);
			// TODO: There is a bug here if we have succeeded in sending some
			// and failed with the others.
		
			/* fail net buffer list */
			NET_BUFFER_LIST_STATUS(p_net_buffer_list) = status;
			NdisMSendNetBufferListsCompleteX( p_port->p_adapter,
											  p_net_buffer_list,
											  send_complete_flags );
			cl_spinlock_acquire( &p_port->send_lock );
			break;
		}
		cl_spinlock_acquire( &p_port->send_lock );
	}
		
	cl_spinlock_release( &p_port->send_lock );

	NDIS_LOWER_IRQL(old_irql, DISPATCH_LEVEL);

	XIPOIB_EXIT( IPOIB_DBG_SEND );
}


static inline void 
__send_complete_net_buffer(
	IN	ipoib_send_NB_SG	*s_buf, 
	IN	ULONG				compl_flags ) 
{
	PERF_DECLARE( FreeSendBuf );
	PERF_DECLARE( SendComp );

	CL_ASSERT( s_buf );

	
	IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_BUF,
		("Processing send completion for NBL %p s_buf %p\n",
			s_buf->p_nbl, s_buf));
	
	cl_perf_start( SendComp );

	// Free SGL element allocated by NDIS
	// We should do it before freeing the whole NBL
	NdisMFreeNetBufferSGList( s_buf->p_port->p_adapter->NdisMiniportDmaHandle,
							  s_buf->p_sgl,
							  s_buf->p_curr_nb );
	
	// No need to delete p_sg_buf at this state, we will destroy the whole list
	// at the end of the execution
	//NET_BUFFER_LIST_NEXT_NBL(p_desc->p_netbuf_list) = NULL;
#if 0
	if (NET_BUFFER_LIST_STATUS(s_buf->p_nbl) != NDIS_STATUS_FAILURE)
	{
		//TODO what about other statuses ?????
		NET_BUFFER_LIST_STATUS(s_buf->p_nbl) = status;
	}
#endif

	switch (NET_BUFFER_LIST_STATUS(s_buf->p_nbl)) {
		case NDIS_STATUS_SUCCESS:
			++g_ipoib_send_ack;
			break;
		case NDIS_STATUS_RESET_IN_PROGRESS:
			++g_ipoib_send_reset;
			break;
		case NDIS_STATUS_FAILURE:
			++g_ipoib_send_SG_failed;
			break;
		case NDIS_STATUS_INVALID_LENGTH:
		case NDIS_STATUS_PAUSED:
		case NDIS_STATUS_SEND_ABORTED:
		case NDIS_STATUS_RESOURCES:
		default:
			ASSERT(FALSE);
	}
		
	IPOIB_DEC_NET_BUFFER_LIST_REF_COUNT(s_buf->p_nbl);
	/* Complete the NBL */
	if (IPOIB_GET_NET_BUFFER_LIST_REF_COUNT(s_buf->p_nbl) == 0)
	{
		PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO pLsoInfo = 	
			(PNDIS_TCP_LARGE_SEND_OFFLOAD_NET_BUFFER_LIST_INFO)
				(PULONG)&NET_BUFFER_LIST_INFO(s_buf->p_nbl, TcpLargeSendNetBufferListInfo);
        if(pLsoInfo->Transmit.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V1_TYPE)
		{            
			UINT TcpPayLoad = 0;
			//TODO optimize this code during MSS/LSO building
			for (PNET_BUFFER NetBuffer = NET_BUFFER_LIST_FIRST_NB(s_buf->p_nbl);
			     NetBuffer != NULL ; 
			     NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
			{
			
				TcpPayLoad += s_buf->tcp_payload;
			}
			pLsoInfo->LsoV1TransmitComplete.TcpPayload = TcpPayLoad;            
		}
	    else if (pLsoInfo->Transmit.Type == NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE)
	    {
	        pLsoInfo->LsoV2TransmitComplete.Reserved = 0;
	        pLsoInfo->LsoV2TransmitComplete.Type = NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE;
	    }
		
		NdisMSendNetBufferListsCompleteX( s_buf->p_port->p_adapter,	s_buf->p_nbl, compl_flags );
	} 

	if( s_buf->p_send_buf )
	{
		cl_perf_start( FreeSendBuf );
		NdisFreeToNPagedLookasideList( &s_buf->p_port->buf_mgr.send_buf_list,
			s_buf->p_send_buf );
		cl_perf_stop( &s_buf->p_port->p_adapter->perf, FreeSendBuf );
	}	
	
	/* Dereference the enpoint used for the transfer. */
	if( s_buf->p_endpt )
	{
		ipoib_endpt_deref( s_buf->p_endpt );
	}
#if 0	
	if (status == NDIS_STATUS_SUCCESS)
	{
		//++g_ipoib_send_SG_real;
		ipoib_inc_send_stat( s_buf->p_port->p_adapter, IP_STAT_SUCCESS, 0 );
	} else {
		++g_ipoib_send_SG_failed;
		ipoib_inc_send_stat( s_buf->p_port->p_adapter, IP_STAT_ERROR, 0 );
	}
#endif
	//Put back into the pool list structure allocated for the NB
	cl_spinlock_acquire(&s_buf->p_port->send_mgr.send_pool_lock);
	cl_qpool_put(&s_buf->p_port->send_mgr.send_pool, (cl_pool_item_t* )s_buf);
	cl_spinlock_release(&s_buf->p_port->send_mgr.send_pool_lock);
	
	cl_perf_stop( &s_buf->p_port->p_adapter->perf, SendComp );
}

void 
ipoib_send_complete_net_buffer(
	IN	ipoib_send_NB_SG	*s_buf, 
	IN	ULONG				compl_flags ) 
{
	IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_BUF,
		("RC send completion for NBL %p s_buf %p\n", s_buf->p_nbl, s_buf));

	__send_complete_net_buffer( s_buf, compl_flags );
}

void
ipoib_port_resume(
	IN				ipoib_port_t* const			p_port,
	IN boolean_t								b_pending,
	IN	cl_qlist_t								*p_complete_list)
{
	cl_list_item_t		*p_item;
	ipoib_send_NB_SG	*s_buf = NULL;
	boolean_t			b_good_port_state = TRUE;
	bool				continue_sending;
	KIRQL				cur_irql = DISPATCH_LEVEL;

	UNUSED_PARAM(b_pending);

	XIPOIB_ENTER( IPOIB_DBG_SEND );

	if( KeGetCurrentIrql() != DISPATCH_LEVEL )
	{
		NDIS_RAISE_IRQL_TO_DISPATCH(&cur_irql);
	}

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	
	cl_obj_lock( &p_port->obj );
	if( p_port->state != IB_QPS_RTS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
			("Port[%d] Invalid state !IB_QPS_RTS(%d) - Flush pending list\n", 
				p_port->port_num, p_port->p_adapter->state) );
		b_good_port_state = FALSE;
	}
	cl_obj_unlock( &p_port->obj );

	if (p_port->send_mgr.pending_list.count <= 0)
		goto Cleanup;

	p_item =  cl_qlist_remove_head( &p_port->send_mgr.pending_list );
	while (p_item != cl_qlist_end(&p_port->send_mgr.pending_list))
	{
		s_buf = (ipoib_send_NB_SG*) (PVOID) p_item; // TODO: Check this casting

		if (!b_good_port_state)
		{
			// Port is in error state, flush the list
			ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
			__send_complete_add_to_list(p_complete_list  ,s_buf, NDIS_STATUS_FAILURE);
		} else {
			/* Check the send queue and pend the request if not empty. */
			if( p_port->send_mgr.depth == p_port->p_adapter->params.sq_depth )
			{
				IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_SEND,
					("No available WQEs.\n") );
				cl_qlist_insert_head( &p_port->send_mgr.pending_list, p_item  );
				break;
			}
			
			continue_sending = ipoib_process_sg_list_real(
											NULL,
											NULL,
											(PSCATTER_GATHER_LIST) s_buf->p_sgl,
											s_buf,
											p_complete_list );

			//cl_qlist_check_validity(&p_port->send_mgr.pending_list);

			if (!continue_sending)
			{
				ASSERT( cl_is_item_in_qlist(&p_port->send_mgr.pending_list,
											(cl_list_item_t*)(PVOID)s_buf) );
				goto Cleanup;
			}
		}
		p_item =  cl_qlist_remove_head( &p_port->send_mgr.pending_list );
	}

Cleanup:

	if (cur_irql != DISPATCH_LEVEL)
	{
		NDIS_LOWER_IRQL(cur_irql, DISPATCH_LEVEL);
	}

	XIPOIB_EXIT( IPOIB_DBG_SEND );
}


//TODO: use s_buf-><data_member> directly, instead of useless copies

static void
__send_cb(
	IN		const	ib_cq_handle_t				h_cq,
	IN				void						*cq_context )
{
	ipoib_port_t		*p_port;
	ib_api_status_t		status;
	ib_wc_t				wc[MAX_SEND_WC], *p_wc, *p_free;
	cl_qlist_t			done_list;
	ipoib_endpt_t		*p_endpt;
	ip_stat_sel_t		type;
	NET_BUFFER			*p_netbuffer = NULL;
	ipoib_send_NB_SG	*s_buf;
	cl_qlist_t			complete_list;

	PERF_DECLARE( SendCompBundle );
	PERF_DECLARE( SendCb );
	PERF_DECLARE( PollSend );
	PERF_DECLARE( SendComp );
	PERF_DECLARE( RearmSend );
	PERF_DECLARE( PortResume );
 
	IPOIB_ENTER( IPOIB_DBG_SEND );

	cl_qlist_init( &complete_list );
	ASSERT( KeGetCurrentIrql() == DISPATCH_LEVEL );

	cl_perf_clr( SendCompBundle );

	cl_perf_start( SendCb );

	UNUSED_PARAM( h_cq );

	cl_qlist_init( &done_list );

	p_port = (ipoib_port_t*)cq_context;
	cl_spinlock_acquire( &p_port->send_lock );
	//cl_qlist_check_validity(&p_port->send_mgr.pending_list);
	ipoib_port_ref( p_port, ref_send_cb );

	for( p_free=wc; p_free < &wc[MAX_SEND_WC - 1]; p_free++ )
		p_free->p_next = p_free + 1;
	p_free->p_next = NULL;

	do
	{
		p_free = wc;
		cl_perf_start( PollSend );
		status = p_port->p_adapter->p_ifc->poll_cq( p_port->ib_mgr.h_send_cq,
													&p_free,
													&p_wc );
		cl_perf_stop( &p_port->p_adapter->perf, PollSend );
		CL_ASSERT( status == IB_SUCCESS || status == IB_NOT_FOUND );

		while( p_wc )
		{
			cl_perf_start( SendComp );
			CL_ASSERT( p_wc->status != IB_WCS_SUCCESS ||
						p_wc->wc_type == IB_WC_SEND || p_wc->wc_type == IB_WC_LSO);

			s_buf = (ipoib_send_NB_SG*)(uintn_t)p_wc->wr_id;
			CL_ASSERT( s_buf );
			IPOIB_PRINT(TRACE_LEVEL_VERBOSE, IPOIB_DBG_SEND,
				("UD send completion NBL %p s_buf %p\n", s_buf->p_nbl, s_buf) );

			p_endpt = s_buf->p_endpt;

			NDIS_STATUS status = NDIS_STATUS_FAILURE;
			switch( p_wc->status )
			{
			
			case IB_WCS_SUCCESS:
				if( p_endpt->h_mcast )
				{
					if( p_endpt->dgid.multicast.raw_group_id[11] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[10] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[12] == 0xFF &&
						p_endpt->dgid.multicast.raw_group_id[13] == 0xFF )
					{
						type = IP_STAT_BCAST_BYTES;
					}
					else
					{
						type = IP_STAT_MCAST_BYTES;
					}
				}
				else
				{
					type = IP_STAT_UCAST_BYTES;
				}

				p_netbuffer = s_buf->p_curr_nb;
				ipoib_inc_send_stat( p_port->p_adapter, type,
										NET_BUFFER_DATA_LENGTH(p_netbuffer) );	
				status = NDIS_STATUS_SUCCESS;
				break;

			case IB_WCS_WR_FLUSHED_ERR:
				IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_SEND,
					("Flushed send completion.\n") );
				ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_DROPPED, 0 );
				status = NDIS_STATUS_RESET_IN_PROGRESS;
				
				break;

			default:
				IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
					("Send failed with %s (vendor specific %#x)\n",
					p_port->p_adapter->p_ifc->get_wc_status_str( p_wc->status ),
					(int)p_wc->vendor_specific) );
				ipoib_inc_send_stat( p_port->p_adapter, IP_STAT_ERROR, 0 );
				status = NDIS_STATUS_FAILURE;
			
				break;
			}
			__send_complete_add_to_list( &complete_list ,s_buf, status );
			
			cl_atomic_dec( &p_port->send_mgr.depth );

			p_wc = p_wc->p_next;
			cl_perf_inc( SendCompBundle );
		}
		/* If we didn't use up every WC, break out. */
	} while( !p_free );


	/* Resume any sends awaiting resources. */
	cl_perf_start( PortResume );
	ipoib_port_resume( p_port, TRUE, &complete_list ); 
	cl_perf_stop( &p_port->p_adapter->perf, PortResume );

	/* Rearm the CQ. */
	cl_perf_start( RearmSend );
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_send_cq, FALSE );
	cl_perf_stop( &p_port->p_adapter->perf, RearmSend );
	CL_ASSERT( status == IB_SUCCESS );

	ipoib_port_deref( p_port, ref_send_cb );

	cl_perf_stop( &p_port->p_adapter->perf, SendCb );
	cl_perf_update_ctr( &p_port->p_adapter->perf, SendCompBundle );
	//cl_qlist_check_validity(&p_port->send_mgr.pending_list);
	cl_spinlock_release( &p_port->send_lock );
	send_complete_list_complete( &complete_list, 
		NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL );
	IPOIB_EXIT( IPOIB_DBG_SEND );
}


/******************************************************************************
*
* Endpoint manager implementation
*
******************************************************************************/

static void
__endpt_mgr_construct(
	IN				ipoib_port_t* const			p_port )
{
	cl_qmap_init( &p_port->endpt_mgr.mac_endpts );
	cl_qmap_init( &p_port->endpt_mgr.lid_endpts );
	cl_fmap_init( &p_port->endpt_mgr.gid_endpts, __gid_cmp );
}


static ib_api_status_t
__endpt_mgr_init(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );

	if( p_port->p_adapter->params.cm_enabled )
	{
		cl_fmap_init( &p_port->endpt_mgr.conn_endpts, __gid_cmp );

		NdisInitializeListHead( &p_port->endpt_mgr.pending_conns );
		NdisAllocateSpinLock( &p_port->endpt_mgr.conn_lock );
		cl_event_init( &p_port->endpt_mgr.event, FALSE );
	
		NdisInitializeListHead( &p_port->endpt_mgr.remove_conns );
		NdisAllocateSpinLock( &p_port->endpt_mgr.remove_lock );

		cl_thread_init( &p_port->endpt_mgr.h_thread, 
						ipoib_endpt_cm_mgr_thread,
						( const void *)p_port, 
						"CmEndPtMgr" );
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}

static void
__endpt_mgr_destroy(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_INIT );
	CL_ASSERT( cl_is_qmap_empty( &p_port->endpt_mgr.mac_endpts ) );
	CL_ASSERT( cl_is_qmap_empty( &p_port->endpt_mgr.lid_endpts ) );
	CL_ASSERT( cl_is_fmap_empty( &p_port->endpt_mgr.gid_endpts ) );

	if( p_port->p_adapter->params.cm_enabled )
	{
		// make sure once CM connected EPs are removed.
		if( !cl_is_fmap_empty( &p_port->endpt_mgr.conn_endpts ) )
		{
			cl_fmap_item_t			*p_fmap_item;

			p_fmap_item = cl_fmap_head( &p_port->endpt_mgr.conn_endpts );
			while( p_fmap_item != cl_fmap_end( &p_port->endpt_mgr.conn_endpts ) )
			{
				ipoib_endpt_t	*p_endpt;

				p_endpt = PARENT_STRUCT( p_fmap_item, ipoib_endpt_t, conn_item );
				DIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_CM,
					("conn_endpts: Lingering EP: %s state %s destroy %s flush %s\n",
						p_endpt->tag,
						cm_get_state_str( endpt_cm_get_state( p_endpt ) ),
						(p_endpt->cm_ep_destroy ? "True":"False"),
						(p_endpt->cm_rx_flushing ? "True":"False")) );
				cl_fmap_remove_item( &p_port->endpt_mgr.conn_endpts, 
									 &p_endpt->conn_item );
				p_fmap_item = cl_fmap_head( &p_port->endpt_mgr.conn_endpts );
			}
		}
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
}

static void
__endpt_mgr_remove_all(
	IN				ipoib_port_t* const			p_port )
{
	IPOIB_ENTER( IPOIB_DBG_ENDPT );
	
	cl_obj_lock( &p_port->obj );

	/* Wait for all readers to complete. */
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	/*
	 * We don't need to initiate destruction - this is called only
	 * from the __port_destroying function, and destruction cascades
	 * to all child objects.  Just clear all the maps.
	 */
	cl_qmap_remove_all( &p_port->endpt_mgr.mac_endpts );
	cl_qmap_remove_all( &p_port->endpt_mgr.lid_endpts );
	cl_fmap_remove_all( &p_port->endpt_mgr.gid_endpts );
	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


static void
__endpt_mgr_reset_all(
	IN			ipoib_port_t* const			p_port )
{
	cl_map_item_t			*p_item;
	ipoib_endpt_t			*p_endpt;
	cl_qlist_t				mc_list;
	cl_qlist_t				conn_list;
	uint32_t				local_exist = 0;
	NDIS_LINK_STATE			link_state;
	NDIS_STATUS_INDICATION	status_indication;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_qlist_init( &mc_list );
	cl_qlist_init( &conn_list );

	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to complete. */
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}

#if 0
	__endpt_mgr_remove_all(p_port);
#else
	link_state.Header.Revision = NDIS_LINK_STATE_REVISION_1;
	link_state.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
	link_state.Header.Size = sizeof(NDIS_LINK_STATE);
	link_state.MediaConnectState = MediaConnectStateDisconnected;
	link_state.MediaDuplexState = MediaDuplexStateFull;

	link_state.XmitLinkSpeed =
	link_state.RcvLinkSpeed  = SET_PORT_RATE_BPS( p_port->p_adapter->port_rate );

	IPOIB_INIT_NDIS_STATUS_INDICATION( &status_indication,
									   p_port->p_adapter->h_adapter,
									   NDIS_STATUS_LINK_STATE,
									   (PVOID)&link_state,
									   sizeof(link_state) );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
					("Indicate DISCONNECT!\n") );

	NdisMIndicateStatusEx( p_port->p_adapter->h_adapter, &status_indication );
			
	link_state.MediaConnectState = MediaConnectStateConnected;
	IPOIB_INIT_NDIS_STATUS_INDICATION( &status_indication,
									   p_port->p_adapter->h_adapter,
									   NDIS_STATUS_LINK_STATE,
									   (PVOID)&link_state,
									   sizeof(link_state) );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT, ("Indicate Connect\n") );

	NdisMIndicateStatusEx( p_port->p_adapter->h_adapter, &status_indication );
		
	// IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT, ("Link DOWN!\n") );

	if( p_port->p_local_endpt )
	{
		if( p_port->p_adapter->params.cm_enabled )
			ipoib_port_cancel_listen( p_port->p_local_endpt );

		cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts,
							 &p_port->p_local_endpt->gid_item );
		cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts,
							 &p_port->p_local_endpt->mac_item );
		if( p_port->p_local_endpt->dlid )
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
								 &p_port->p_local_endpt->lid_item );
			p_port->p_local_endpt->dlid = 0;
		}
		
		cl_qlist_insert_head( &mc_list,
							  &p_port->p_local_endpt->mac_item.pool_item.list_item );
		local_exist = 1;
		p_port->p_local_endpt = NULL;
	}

	p_item = cl_qmap_head( &p_port->endpt_mgr.mac_endpts );
	while( p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		p_item = cl_qmap_next( p_item );

		if( p_endpt->h_mcast )
		{
			/*
			 * We destroy MC endpoints since they will get recreated
			 * when the port comes back up and we rejoin the MC groups.
			 */
			cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts, &p_endpt->mac_item );
			cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );

			cl_qlist_insert_tail( &mc_list, &p_endpt->mac_item.pool_item.list_item );
		}
		/* destroy endpoints CM resources if any */
		if( p_port->p_adapter->params.cm_enabled &&
			(p_endpt->conn.h_send_cq || p_endpt->conn.h_recv_cq) )
		{
			endpt_unmap_conn_dgid( p_port, p_endpt );
			cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts, &p_endpt->mac_item );
			cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );

			cl_qlist_insert_tail( &conn_list, &p_endpt->mac_item.pool_item.list_item );
		}

		if( p_endpt->h_av )
		{
			/* Destroy the AV for all other endpoints. */
			p_endpt->p_ifc->destroy_av( p_endpt->h_av );
			p_endpt->h_av = NULL;
		}
		
		if( p_endpt->dlid )
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts, &p_endpt->lid_item );
			IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_ENDPT,
				("<__endptr_mgr_reset_all: setting p_endpt->dlid to 0\n"));
			p_endpt->dlid = 0;
		}
	}
#endif
	cl_obj_unlock( &p_port->obj );

	if( p_port->p_adapter->params.cm_enabled )
	{
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("Port[%d] conn_list entries %d\n",
				p_port->port_num, (int)cl_qlist_count( &conn_list )) );

		while( cl_qlist_count( &conn_list ) )
		{
			BOOLEAN	destroy_now;

			p_endpt = PARENT_STRUCT( cl_qlist_remove_head(&conn_list),
									 ipoib_endpt_t,
									 mac_item.pool_item.list_item );
			destroy_now = cm_destroy_conn( p_port, p_endpt );
			if( destroy_now )
				cl_obj_destroy( &p_endpt->obj );
		}
	}

	if( cl_qlist_count( &mc_list ) - local_exist )
	{
		p_port->mcast_cnt =  (uint32_t)cl_qlist_count( &mc_list ) - local_exist;
	}
	else
	{
		p_port->mcast_cnt = 0;
		KeSetEvent( &p_port->leave_mcast_event, EVENT_INCREMENT, FALSE );
	}	

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
				("p_port->mcast_cnt %d\n", p_port->mcast_cnt - local_exist));

	/* Destroy all multicast endpoints now that we have released the lock. */
	while( cl_qlist_count( &mc_list ) )
	{
		cl_list_item_t	*p_item;
		p_item = cl_qlist_remove_head( &mc_list );
		p_endpt = PARENT_STRUCT(p_item, ipoib_endpt_t, mac_item.pool_item.list_item);
		cl_obj_destroy( &p_endpt->obj);
	}

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


/*
 * Called when updating an endpoint entry in response to an ARP.
 * Because receive processing is serialized, and holds a reference
 * on the endpoint reader, we wait for all *other* readers to exit before
 * removing the item.
 */
static void
__endpt_mgr_remove(
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t* const		p_endpt )
{
	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	/* This function must be called from the receive path */
	CL_ASSERT(p_port->endpt_rdr > 0);

	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to complete. */    
	while( p_port->endpt_rdr > 1 )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}

	/* Remove the endpoint from the maps so further requests don't find it. */
	cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts, &p_endpt->mac_item );
	/*
	 * The enpoints are *ALWAYS* in both the MAC and GID maps.  They are only
	 * in the LID map if the GID has the same subnet prefix as us.
	 */
	cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );
#if IPOIB_CM
	endpt_unmap_conn_dgid( p_port, p_endpt );
#endif 
	if( p_endpt->dlid )
	{
		cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts, &p_endpt->lid_item );
		p_endpt->dlid = 0;
	}

	cl_obj_unlock( &p_port->obj );

#if IPOIB_CM
	if( p_port->p_adapter->params.cm_enabled )
		cm_destroy_conn( p_port, p_endpt );

	if( !p_endpt->cm_ep_destroy )
		cl_obj_destroy( &p_endpt->obj );
#else
	cl_obj_destroy( &p_endpt->obj );
#endif

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}


NTSTATUS
ipoib_mac_to_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t*					p_gid )
{
	ipoib_endpt_t*	p_endpt;
	cl_map_item_t	*p_item;
	uint64_t		key = 0;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	memcpy( &key, &mac, sizeof(mac_addr_t) );

	cl_obj_lock( &p_port->obj );

	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed endpoint lookup.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	*p_gid = p_endpt->dgid;

	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return STATUS_SUCCESS;
}


NTSTATUS
ipoib_mac_to_path(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_path_rec_t*				p_path )
{
	ipoib_endpt_t*	p_endpt;
	cl_map_item_t	*p_item;
	uint64_t		key = 0;
	uint8_t			sl;
	net32_t			flow_lbl;
	uint8_t			hop_limit;
	uint8_t			pkt_life;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	memcpy( &key, &mac, sizeof(mac_addr_t) );

	cl_obj_lock( &p_port->obj );

	if( p_port->p_local_endpt == NULL )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("No local endpoint.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	if( mac.addr[0] == 0 && mac.addr[1] == 0 && mac.addr[2] == 0 &&
		mac.addr[3] == 0 && mac.addr[4] == 0 && mac.addr[5] == 0 )
	{
		p_endpt = p_port->p_local_endpt;
	}
	else
	{
		p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
		if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
		{
			cl_obj_unlock( &p_port->obj );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Port[%d] Failed endpoint lookup MAC %s\n",
					p_port->port_num, mk_mac_str(&mac)) );
			return STATUS_INVALID_PARAMETER;
		}

		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	}

	p_path->service_id = 0;
	p_path->dgid = p_endpt->dgid;
	p_path->sgid = p_port->p_local_endpt->dgid;
	p_path->dlid = p_endpt->dlid;
	p_path->slid = p_port->p_local_endpt->dlid;

	ib_member_get_sl_flow_hop(
		p_port->ib_mgr.bcast_rec.sl_flow_hop,
		&sl,
		&flow_lbl,
		&hop_limit
		);
	
	if( p_path->slid == p_path->dlid )
		pkt_life = 0;
	else
		pkt_life = p_port->ib_mgr.bcast_rec.pkt_life;

	ib_path_rec_init_local(
		p_path,
		&p_endpt->dgid,
		&p_port->p_local_endpt->dgid,
		p_endpt->dlid,
		p_port->p_local_endpt->dlid,
		1,
		p_port->ib_mgr.bcast_rec.pkey,
		sl, 0,
		IB_PATH_SELECTOR_EXACTLY, p_port->ib_mgr.bcast_rec.mtu,
		IB_PATH_SELECTOR_EXACTLY, p_port->ib_mgr.bcast_rec.rate,
		IB_PATH_SELECTOR_EXACTLY, pkt_life,
		0 );

	/* Set global routing information. */
	ib_path_rec_set_hop_flow_raw( p_path, hop_limit, flow_lbl, FALSE );
	p_path->tclass = p_port->ib_mgr.bcast_rec.tclass;
 
	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return STATUS_SUCCESS;
}


static inline NDIS_STATUS
__endpt_mgr_ref(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ipoib_endpt_t** const		pp_endpt )
{
	NDIS_STATUS		status;
	cl_map_item_t	*p_item;
	uint64_t		key;

	PERF_DECLARE( EndptQueue );

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	if( !memcmp( &mac, &p_port->p_adapter->params.conf_mac, sizeof(mac) ) )
	{
		/* Discard loopback traffic. */
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_ENDPT,
			("Discarding loopback traffic\n") );
		IPOIB_EXIT( IPOIB_DBG_ENDPT );
		return NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
	}
	key = 0;
	memcpy( &key, &mac, sizeof(mac_addr_t) );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("Check MAC %s\n",mk_mac_str(&mac)) );

	cl_obj_lock( &p_port->obj );
	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_ENDPT,
			("Port[%d] Failed endpoint lookup MAC %s\n",
				p_port->port_num, mk_mac_str(&mac)) );
		return NDIS_STATUS_NO_ROUTE_TO_DESTINATION;
	}

	*pp_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
	ipoib_endpt_ref( *pp_endpt );

	cl_obj_unlock( &p_port->obj );

	cl_perf_start( EndptQueue );
	status = ipoib_endpt_queue( p_port, *pp_endpt );
	cl_perf_stop( &p_port->p_adapter->perf, EndptQueue );
	if( status != NDIS_STATUS_SUCCESS )
		*pp_endpt = NULL;

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return status;
}


static inline NDIS_STATUS
__endpt_mgr_get_gid_qpn(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
		OUT			ib_gid_t* const				p_gid,
		OUT			UNALIGNED net32_t* const	p_qpn )
{
	UNALIGNED
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	uint64_t		key;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	cl_obj_lock( &p_port->obj );

	key = 0;
	memcpy( &key, &mac, sizeof(mac_addr_t) );
	p_item = cl_qmap_get( &p_port->endpt_mgr.mac_endpts, key );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("Failed endpoint lookup.\n") );
		return NDIS_STATUS_FAILURE;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );

	*p_gid = p_endpt->dgid;
	*p_qpn = p_endpt->qpn;

	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return NDIS_STATUS_SUCCESS;
}


static inline ipoib_endpt_t*
__endpt_mgr_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid )
{
	cl_fmap_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_item = cl_fmap_get( &p_port->endpt_mgr.gid_endpts, p_gid );
	if( p_item == cl_fmap_end( &p_port->endpt_mgr.gid_endpts ) )
		p_endpt = NULL;
	else
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, gid_item );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return p_endpt;
}


static ipoib_endpt_t*
__endpt_mgr_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid )
{
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	p_item = cl_qmap_get( &p_port->endpt_mgr.lid_endpts, lid );
	if( p_item == cl_qmap_end( &p_port->endpt_mgr.lid_endpts ) )
		p_endpt = NULL;
	else
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, lid_item );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return p_endpt;
}


inline ib_api_status_t
__endpt_mgr_insert_locked(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt )
{
	ib_api_status_t	status;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("insert  :\t  MAC: %s\n", mk_mac_str(&mac)) );

	cl_obj_lock( &p_port->obj );
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	/* __endpt_mgr_insert expects *1* reference to be held when being called */
	cl_atomic_inc( &p_port->endpt_rdr );
	status= __endpt_mgr_insert( p_port, mac, p_endpt );
	cl_atomic_dec( &p_port->endpt_rdr );
	cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return status;
}


inline ib_api_status_t
__endpt_mgr_insert(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac,
	IN				ipoib_endpt_t* const		p_endpt )
{
	uint64_t		key;
	cl_status_t		cl_status;
	cl_map_item_t	*p_qitem;
	cl_fmap_item_t	*p_fitem;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	/* Wait for all accesses to the map to complete. */
	while( p_port->endpt_rdr > 1 )
		;

	/* Link the endpoint to the port. */
	cl_status = cl_obj_insert_rel_parent_locked( &p_endpt->rel,
												 &p_port->obj,
												 &p_endpt->obj );
	if( cl_status != CL_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		return IB_INVALID_STATE;
	}

	p_endpt->mac = mac;
	key = 0;
	memcpy( &key, &mac, sizeof(mac_addr_t) );
	p_qitem = cl_qmap_insert( &p_port->endpt_mgr.mac_endpts,
							  key,
							  &p_endpt->mac_item );
	CL_ASSERT( p_qitem == &p_endpt->mac_item );

	p_fitem = cl_fmap_insert( &p_port->endpt_mgr.gid_endpts,
							  &p_endpt->dgid,
							  &p_endpt->gid_item );
	CL_ASSERT( p_fitem == &p_endpt->gid_item );

	if( p_endpt->dlid )
	{
		p_qitem = cl_qmap_insert( &p_port->endpt_mgr.lid_endpts,
								  p_endpt->dlid,
								  &p_endpt->lid_item );
		CL_ASSERT( p_qitem == &p_endpt->lid_item );
		if (p_qitem != &p_endpt->lid_item)
		{
			// Since we failed to insert into the list, make sure it is not removed
			p_endpt->dlid =0;
		}
	}

    p_port->p_adapter->p_ifc->ibat_update_route(
        p_port->p_adapter->ibatRouter,
        key,
        &p_endpt->dgid
        );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
	return IB_SUCCESS;
}


static ib_api_status_t
__endpt_mgr_add_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ib_api_status_t	status;
	ipoib_endpt_t	*p_endpt;
	mac_addr_t		bcast_mac;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/*
	 * Cache the broadcast group properties for creating future mcast groups.
	 */
	p_port->ib_mgr.bcast_rec = *p_mcast_rec->p_member_rec;

	/* Allocate the broadcast endpoint. */
	p_endpt = ipoib_endpt_create( p_port,
								  &p_mcast_rec->p_member_rec->mgid,
								  0,
								  CL_HTON32(0x00FFFFFF) );
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_endpt_create failed.\n") );
		return IB_INSUFFICIENT_RESOURCES;
	}
#if DBG_ENDPT
	/* set reference to transport to be used while is not attached to the port */
	ipoib_port_ref( p_port, ref_endpt_track );
#endif
	StringCbCopy(p_endpt->tag,sizeof(p_endpt->tag),"<BCast>");
	p_endpt->is_mcast_listener = TRUE;
	p_endpt->p_ifc = p_port->p_adapter->p_ifc;
	status = ipoib_endpt_set_mcast( p_endpt,
									p_port->ib_mgr.h_pd,
									p_port->port_num,
									p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_create_mcast_endpt returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Add the broadcast endpoint to the endpoint map. */
	memset( &bcast_mac, 0xFF, sizeof(bcast_mac) );
	status = __endpt_mgr_insert_locked( p_port, bcast_mac, p_endpt );

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


void
ipoib_port_remove_endpt(
	IN				ipoib_port_t* const			p_port,
	IN		const	mac_addr_t					mac )
{
	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	uint64_t		key;
	BOOLEAN			destroy_now = TRUE;

	IPOIB_ENTER( IPOIB_DBG_ENDPT );

	key = 0;
	memcpy( &key, &mac, sizeof(mac_addr_t) );

	/* Remove the endpoint from the maps so further requests don't find it. */
	cl_obj_lock( &p_port->obj );

	/* Wait for all readers to finish */
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	p_item = cl_qmap_remove( &p_port->endpt_mgr.mac_endpts, key );
	/*
	 * Dereference the endpoint.  If the ref count goes to zero, it
	 * will get freed.
	 */
	if( p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts ) )
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		/*
		 * The enpoints are *ALWAYS* in both the MAC and GID maps. They are only
		 * in the LID map if the GID has the same subnet prefix as us.
		 */
		cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts, &p_endpt->gid_item );
#if IPOIB_CM
		endpt_unmap_conn_dgid( p_port, p_endpt );
#endif

		if( p_endpt->dlid )
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts, &p_endpt->lid_item );
			p_endpt->dlid = 0;
		}

		cl_obj_unlock( &p_port->obj );

		if( p_port->p_adapter->params.cm_enabled )
			destroy_now = cm_destroy_conn( p_port, p_endpt );

		if( destroy_now )
			cl_obj_destroy( &p_endpt->obj );
	}
	else
		cl_obj_unlock( &p_port->obj );

	IPOIB_EXIT( IPOIB_DBG_ENDPT );
}

/*
 * The sequence for port up is as follows:
 *	1. The port goes active.  This allows the adapter to send SA queries
 *	and join the broadcast group (and other groups).
 *
 *	2. The adapter sends an SA query for the broadcast group.
 *
 *	3. Upon completion of the query, the adapter joins the broadcast group.
 */


/*
 * Query the SA for the broadcast group.
 */
void
ipoib_port_up(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_pnp_port_rec_t* const	p_pnp_rec )
{
	ib_port_info_t		*p_port_info;
	ib_mad_t			*mad_in 	= NULL;
	ib_mad_t			*mad_out = NULL;
	ib_api_status_t		status 	= IB_INSUFFICIENT_MEMORY;
	static int 			cnt 	= 0;
	
	IPOIB_ENTER( IPOIB_DBG_INIT );
	IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_INIT,
			("[%d] Entering port_up.\n", ++cnt) ); 
	p_port->n_no_progress = 0; // Init the send failure counter
	
	cl_obj_lock( &p_port->obj );
	if ( p_port->state == IB_QPS_INIT ) 
	{
		cl_obj_unlock( &p_port->obj );
		status = IB_SUCCESS;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("p_port->state = %d - Aborting.\n", p_port->state) );        
		goto up_done;
	}
	else if ( p_port->state == IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->p_adapter->obj );
		if( p_port->p_adapter->state == IB_PNP_PORT_INIT )
		{
			p_port->p_adapter->state = IB_PNP_PORT_ACTIVE;
		}
		cl_obj_unlock( &p_port->p_adapter->obj );
		status = IB_SUCCESS;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Port init is done. p_port->state = %d.\n", p_port->state ) );
		goto up_done;
	}
	p_port->state = IB_QPS_INIT;
	cl_obj_unlock( &p_port->obj );  

	/* Wait for all work requests to get flushed. */
	while( p_port->recv_mgr.depth || p_port->send_mgr.depth )
		cl_thread_suspend( 0 );

	KeResetEvent( &p_port->sa_event );

	mad_out = (ib_mad_t *) (ib_mad_t*)cl_zalloc(256);
	if(! mad_out)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed to allocate mad mad_out\n")); 
		goto up_done;
	}
	mad_in = (ib_mad_t *) cl_zalloc(256);
	if(! mad_in)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("failed to allocate mad mad_in\n")); 
		goto up_done;
	}

	mad_in->attr_id = IB_MAD_ATTR_PORT_INFO;
	mad_in->method = IB_MAD_METHOD_GET;
	mad_in->base_ver = 1;
	mad_in->class_ver =1;
	mad_in->mgmt_class = IB_MCLASS_SUBN_LID;
	
	status = p_port->p_adapter->p_ifc->local_mad( p_port->ib_mgr.h_ca,
												  p_port->port_num,
												  mad_in,
												  mad_out );
	if( status != IB_SUCCESS )
	{
		ipoib_set_inactive( p_port->p_adapter );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_local_mad returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		goto up_done;
	}

	p_port_info = (ib_port_info_t*)(((ib_smp_t*)mad_out)->data);
	p_port->base_lid = p_pnp_rec->p_port_attr->lid;
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
		("Received port info: link width = %d.\n",
			p_port_info->link_width_active) );
	p_port->ib_mgr.rate = ib_port_info_compute_rate( p_port_info,
							p_port_info->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS );
	ipoib_set_rate( p_port->p_adapter,
					p_port_info->link_width_active,
					ib_port_info_get_link_speed_active( p_port_info ) );

	status = __port_get_bcast( p_port );
	if (status != IB_SUCCESS)
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			(" __port_get_bcast returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )));
	}

up_done:
	if( status != IB_SUCCESS )
	{
		if( status != IB_CANCELED )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
		}
		ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
		p_port->state = IB_QPS_ERROR;
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	}

	if(mad_out)
		cl_free(mad_out);
	if(mad_in)
		cl_free(mad_in);

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__endpt_mgr_add_local(
	IN				ipoib_port_t* const			p_port,
	IN				ib_port_info_t* const		p_port_info )
{
	ib_api_status_t			status;
	ib_gid_t				gid;
	ipoib_endpt_t			*p_endpt;
	ib_av_attr_t			av_attr;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	ib_gid_set_default( &gid, p_port->p_adapter->guids.port_guid.guid );
	p_endpt = ipoib_endpt_create( p_port,
								  &gid,
								  p_port_info->base_lid,
								  p_port->ib_mgr.qpn );
	if( !p_endpt )
	{
		p_port->p_adapter->hung = TRUE;
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to create local endpt\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
#if DBG_ENDPT
	ipoib_port_ref( p_port, ref_endpt_track );

	StringCchPrintf( p_endpt->tag,
					 sizeof(p_endpt->tag),
					 "Local_EP.lid-%#x",
					 cl_ntoh16(p_port_info->base_lid) );
#endif


	memset( &av_attr, 0, sizeof(ib_av_attr_t) );
	av_attr.port_num = p_port->port_num;
	av_attr.sl = 0;
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		(" av_attr.dlid = p_port_info->base_lid = %x\n",
			cl_ntoh16( p_port_info->base_lid ) ));
	av_attr.dlid = p_port_info->base_lid;
	av_attr.static_rate = p_port->ib_mgr.rate;
	av_attr.path_bits = 0;
	status = p_port->p_adapter->p_ifc->create_av( p_port->ib_mgr.h_pd,
												  &av_attr,
												  &p_endpt->h_av );
	if( status != IB_SUCCESS )
	{
		cl_obj_destroy( &p_endpt->obj );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_create_av for local endpoint returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* __endpt_mgr_insert expects *one* reference to be held. */
	cl_atomic_inc( &p_port->endpt_rdr );
	status = __endpt_mgr_insert( p_port,
								 p_port->p_adapter->params.conf_mac,
								 p_endpt );
	cl_atomic_dec( &p_port->endpt_rdr );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_insert for local endpoint returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	p_port->p_local_endpt = p_endpt;

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static ib_api_status_t
__port_get_bcast(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_query_req_t		query;
	ib_user_query_t		info;
	ib_member_rec_t		member_rec;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	info.method = IB_MAD_METHOD_GETTABLE;
	info.attr_id = IB_MAD_ATTR_MCMEMBER_RECORD;
	info.attr_size = sizeof(ib_member_rec_t);
	info.comp_mask = IB_MCR_COMPMASK_MGID;
	info.p_attr = &member_rec;

	/* Query requires only the MGID. */
	memset( &member_rec, 0, sizeof(ib_member_rec_t) );
	member_rec.mgid = bcast_mgid_template;

    member_rec.mgid.raw[4] = (uint8_t) (p_port->p_adapter->guids.port_guid.pkey >> 8) ;
	member_rec.mgid.raw[5] = (uint8_t) p_port->p_adapter->guids.port_guid.pkey;
	member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);
	memset( &query, 0, sizeof(ib_query_req_t) );
	query.query_type = IB_QUERY_USER_DEFINED;
	query.p_query_input = &info;
	query.port_guid = p_port->p_adapter->guids.port_guid.guid;
	query.timeout_ms = p_port->p_adapter->params.sa_timeout;
	query.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	query.query_context = p_port;
	query.pfn_query_cb = __bcast_get_cb;

	/* reference the object for the multicast query. */
	ipoib_port_ref( p_port, ref_get_bcast );

	status = p_port->p_adapter->p_ifc->query( p_port->p_adapter->h_al,
											  &query,
											  &p_port->ib_mgr.h_query );
	if( status != IB_SUCCESS )
	{
		// done in bcast_get_cb() ipoib_port_deref( ref_bcast_get_cb );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_query returned %s\n", 
				p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	ipoib_port_deref( p_port, ref_get_bcast );
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


/* Callback for the MCMemberRecord Get query for the IPv4 broadcast group. */
static void
__bcast_get_cb(
	IN				ib_query_rec_t				*p_query_rec )
{
	ipoib_port_t		*p_port;
	ib_member_rec_t		*p_mc_req;
	ib_api_status_t		status;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port = (ipoib_port_t*)p_query_rec->query_context;

	cl_obj_lock( &p_port->obj );
	p_port->ib_mgr.h_query = NULL;

	CL_ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if( p_port->state != IB_QPS_INIT )
	{
		status = IB_CANCELED;
		goto done;
	}
	
	status = p_query_rec->status;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
		("status of request %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );

	switch( status )
	{
	case IB_SUCCESS:
		if( p_query_rec->result_cnt )
		{
			p_mc_req = (ib_member_rec_t*)
				ib_get_query_result( p_query_rec->p_result_mad, 0 );

			/* Join the broadcast group. */
			status = __port_join_bcast( p_port, p_mc_req );
			break;
		}
		/* Fall through. */

	case IB_REMOTE_ERROR:
		/* SA failed the query.  Broadcast group doesn't exist, create it. */
		status = __port_create_bcast( p_port );
		break;

	case IB_CANCELED:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Instance destroying - Aborting.\n") );
		break;

	default:
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_BCAST_GET, 1, p_query_rec->status );
	}
done:
	cl_obj_unlock( &p_port->obj );

	if( status != IB_SUCCESS )
	{
		if( status != IB_CANCELED )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
		}
		p_port->state = IB_QPS_ERROR;
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	}

	/* Return the response MAD to AL. */
	if( p_query_rec->p_result_mad )
		p_port->p_adapter->p_ifc->put_mad( __FILE__, __LINE__, p_query_rec->p_result_mad );

	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static ib_api_status_t
__port_join_bcast(
	IN				ipoib_port_t* const			p_port,
	IN				ib_member_rec_t* const		p_member_rec )
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;
	ib_ca_attr_t * 		p_ca_attrs;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Check that the rate is realizable for our port. */
	if( p_port->ib_mgr.rate < (p_member_rec->rate & 0x3F) &&
		(g_ipoib.bypass_check_bcast_rate == 0))
	{
		/*
		 * The MC group rate is higher than our port's rate.  Log an error
		 * and stop.  A port transition will drive the retry.
		 */
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT,
			("Unrealizable join due to rate mismatch.\n") );
		NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
			EVENT_IPOIB_BCAST_RATE, 2,
			(uint32_t)(p_member_rec->rate & 0x3F),
			(uint32_t)p_port->ib_mgr.rate );
		return IB_ERROR;
	}

	/* Join the broadcast group. */
	memset( &mcast_req, 0, sizeof(mcast_req) );
	/* Copy the results of the Get to use as parameters. */
	mcast_req.member_rec = *p_member_rec;
	
	/* We specify our port GID for the join operation. */
	if( p_port->p_ca_attrs )
	{
		mcast_req.member_rec.port_gid.unicast.prefix = 
					p_port->p_ca_attrs->p_port_attr->p_gid_table[0].unicast.prefix;
	}
	else
	{
		status =__port_query_ca_attrs( p_port, &p_ca_attrs);
		if ( status == IB_SUCCESS ) 
		{
			mcast_req.member_rec.port_gid.unicast.prefix = 
							p_ca_attrs->p_port_attr->p_gid_table[0].unicast.prefix;
			cl_free ( p_ca_attrs );
		}
		else 
		{	
			ASSERT ( status != IB_SUCCESS );
			mcast_req.member_rec.port_gid.unicast.prefix = IB_DEFAULT_SUBNET_PREFIX;
		}
	}
	
	mcast_req.member_rec.port_gid.unicast.interface_id =
		p_port->p_adapter->guids.port_guid.guid;

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __bcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;

	if( ib_member_get_state( mcast_req.member_rec.scope_state ) !=
		IB_MC_REC_STATE_FULL_MEMBER )
	{
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_INIT,
			("Incorrect MC member rec join state in query response.\n") );
		ib_member_set_state( &mcast_req.member_rec.scope_state,
			IB_MC_REC_STATE_FULL_MEMBER );
	}

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_bcast );

	status = p_port->p_adapter->p_ifc->join_mcast( p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_bcast_join_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


static ib_api_status_t
__port_create_bcast(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	/* Join the broadcast group. */
	memset( &mcast_req, 0, sizeof(mcast_req) );
	mcast_req.create = TRUE;
	/*
	 * Create requires pkey, qkey, SL, flow label, traffic class, joing state
	 * and port GID.
	 *
	 * We specify the MGID since we don't want the SA to generate it for us.
	 */
	mcast_req.member_rec.mgid = bcast_mgid_template;
	mcast_req.member_rec.mgid.raw[4] = (uint8_t) (p_port->p_adapter->guids.port_guid.pkey >> 8); 
	mcast_req.member_rec.mgid.raw[5] = (uint8_t) p_port->p_adapter->guids.port_guid.pkey;
	ib_gid_set_default( &mcast_req.member_rec.port_gid,
						p_port->p_adapter->guids.port_guid.guid );
	/*
	 * IPOIB spec requires that the QKEY have the MSb set so that the QKEY
	 * from the QP is used rather than the QKEY in the send WR.
	 */
	mcast_req.member_rec.qkey =
		(uint32_t)(uintn_t)p_port | IB_QP_PRIVILEGED_Q_KEY;
	mcast_req.member_rec.mtu =
		(IB_PATH_SELECTOR_EXACTLY << 6) | IB_MTU_LEN_2048;

	mcast_req.member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);

	mcast_req.member_rec.sl_flow_hop = ib_member_set_sl_flow_hop( 0, 0, 0 );
	mcast_req.member_rec.scope_state =
		ib_member_set_scope_state( 2, IB_MC_REC_STATE_FULL_MEMBER );

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __bcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_bcast );

	status = p_port->p_adapter->p_ifc->join_mcast( p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_bcast_create_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	IPOIB_EXIT( IPOIB_DBG_INIT );
	return status;
}


void
ipoib_port_down(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t		status;
	ib_qp_mod_t			qp_mod;
	cl_qlist_t	complete_list;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	
	cl_qlist_init(&complete_list);

	/*
	 * Mark our state.  This causes all callbacks to abort.
	 * Note that we hold the receive lock so that we synchronize
	 * with reposting.  We must take the receive lock before the
	 * object lock since that is the order taken when reposting.
	 */
	cl_spinlock_acquire( &p_port->recv_lock );
	cl_spinlock_acquire( &p_port->send_lock );
	cl_obj_lock( &p_port->obj );

	if( p_port->state == IB_QPS_ERROR )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Port[%d] already down(state == IB_QPS_ERROR)? PNP state %s\n",
				p_port->port_num, ib_get_pnp_event_str(p_port->p_adapter->state)) );
	}

	p_port->state = IB_QPS_ERROR;

	__pending_list_destroy(p_port, &complete_list);

	NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter, EVENT_IPOIB_PORT_DOWN, 0 );

	if( p_port->ib_mgr.h_query )
	{
		p_port->p_adapter->p_ifc->cancel_query( p_port->p_adapter->h_al,
												p_port->ib_mgr.h_query );
		p_port->ib_mgr.h_query = NULL;
	}
	cl_obj_unlock( &p_port->obj );
	cl_spinlock_release( &p_port->send_lock );
	cl_spinlock_release( &p_port->recv_lock );
	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());

	KeWaitForSingleObject( &p_port->sa_event,
						   Executive,
						   KernelMode,
						   FALSE,
						   NULL );

	/* garbage collector timer is not needed when link is down */
	KeCancelTimer(&p_port->gc_timer);
	KeFlushQueuedDpcs();

	/*
	 * Put the QP in the error state.  This removes the need to
	 * synchronize with send/receive callbacks.
	 */
	CL_ASSERT( p_port->ib_mgr.h_qp );
	memset( &qp_mod, 0, sizeof(ib_qp_mod_t) );
	qp_mod.req_state = IB_QPS_ERROR;
	status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_modify_qp to error state returned %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		p_port->p_adapter->hung = TRUE;
		return;
	}
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("IB QP was put into IB_QPS_ERROR state\n") );

	KeResetEvent(&p_port->leave_mcast_event);

	/* Reset all endpoints so we don't flush our ARP cache. */
	__endpt_mgr_reset_all( p_port );

	KeWaitForSingleObject( &p_port->leave_mcast_event,
						   Executive,
						   KernelMode,
						   FALSE,
						   NULL );
	
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__bcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ipoib_port_t	*p_port;
	ib_api_status_t	status;
	LARGE_INTEGER  	gc_due_time;

	IPOIB_ENTER( IPOIB_DBG_INIT );

	p_port = (ipoib_port_t*)p_mcast_rec->mcast_context;

	cl_obj_lock( &p_port->obj );

	ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if( p_port->state != IB_QPS_INIT )
	{
		cl_obj_unlock( &p_port->obj );
		if( p_mcast_rec->status == IB_SUCCESS )
		{
			ipoib_port_ref(p_port, ref_leave_mcast);
			p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast,
												   __leave_error_mcast_cb );
		}
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		ipoib_port_deref( p_port, ref_bcast_inv_state );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Invalid state - Aborting.\n") );
		return;
	}
	status = p_mcast_rec->status;
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Multicast join for broadcast group returned %s.\n",
			p_port->p_adapter->p_ifc->get_err_str( p_mcast_rec->status )) );
		if( status == IB_REMOTE_ERROR )
		{
			/*
			 * Either:
			 *	- the join failed because the group no longer exists
			 *	- the create failed because the group already exists
			 *
			 * Kick off a new Get query to the SA to restart the join process
			 * from the top.  Note that as an optimization, it would be
			 * possible to distinguish between the join and the create.
			 * If the join fails, try the create.  If the create fails, start
			 * over with the Get.
			 */
			/* TODO: Assert is a place holder.  Can we ever get here if the
			state isn't IB_PNP_PORT_ADD or PORT_DOWN or PORT_INIT? */
			CL_ASSERT( p_port->p_adapter->state == IB_PNP_PORT_ADD ||
				p_port->p_adapter->state == IB_PNP_PORT_DOWN ||
				p_port->p_adapter->state == IB_PNP_PORT_INIT );
			if(++p_port->bc_join_retry_cnt < p_port->p_adapter->params.bc_join_retry)
			{
				status = __port_get_bcast( p_port );
			}
			else
			{
				NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
					EVENT_IPOIB_BCAST_JOIN, 1, p_mcast_rec->status );
				p_port->bc_join_retry_cnt = 0;
			}
		}
		else
		{
			NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
				EVENT_IPOIB_BCAST_JOIN, 1, p_mcast_rec->status );
		}

		cl_obj_unlock( &p_port->obj );
		if( status != IB_SUCCESS )
		{
			ipoib_set_inactive( p_port->p_adapter );
			__endpt_mgr_reset_all( p_port );
			ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
			p_port->state = IB_QPS_ERROR;
			KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		}
		ipoib_port_deref( p_port, ref_bcast_req_failed );
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return;
	}
	p_port->bc_join_retry_cnt = 0;

	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}

	if( !p_port->p_local_endpt )
	{
		ib_port_info_t	port_info;
		memset(&port_info, 0, sizeof(port_info));
		port_info.base_lid = p_port->base_lid;
		status = __endpt_mgr_add_local( p_port, &port_info );
		if( status != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__endpt_mgr_add_local returned %s\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			cl_obj_unlock( &p_port->obj );
			goto err;
		}
	}

	cl_obj_unlock( &p_port->obj );

	status = __endpt_mgr_add_bcast( p_port, p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_add_bcast returned %s\n",
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		ipoib_port_ref(p_port, ref_leave_mcast);
		status = p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast,
														__leave_error_mcast_cb );
		CL_ASSERT( status == IB_SUCCESS );
		goto err;
	}

	/* Get the QP ready for action. */
	status = __ib_mgr_activate( p_port );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__ib_mgr_activate returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );

err:
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung = TRUE;
		ASSERT(p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
		p_port->state = IB_QPS_ERROR;        
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		ipoib_port_deref( p_port, ref_bcast_error );
		IPOIB_EXIT( IPOIB_DBG_INIT );
		return;
	}

	cl_obj_lock( &p_port->obj );
	/* Only change the state if we're still in INIT. */
	ASSERT( p_port->state == IB_QPS_INIT || p_port->state == IB_QPS_ERROR);
	if (p_port->state == IB_QPS_INIT)
	{
		p_port->state = IB_QPS_RTS;
	}
	cl_obj_unlock( &p_port->obj );

	/* Prepost receives. */
	cl_spinlock_acquire( &p_port->recv_lock );
	__recv_mgr_repost( p_port );
	cl_spinlock_release( &p_port->recv_lock );

	/* Notify the adapter that we now have an active connection. */
	status = ipoib_set_active( p_port->p_adapter );
	if( status != IB_SUCCESS )
	{
		ib_qp_mod_t			qp_mod;
		ipoib_set_inactive( p_port->p_adapter );
		KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("ipoib_set_active returned %s.\n",
					p_port->p_adapter->p_ifc->get_err_str( status )));
		cl_spinlock_acquire( &p_port->recv_lock );
		cl_obj_lock( &p_port->obj );
		p_port->state = IB_QPS_ERROR;
		if( p_port->ib_mgr.h_query )
		{
			p_port->p_adapter->p_ifc->cancel_query(
				p_port->p_adapter->h_al, p_port->ib_mgr.h_query );
			p_port->ib_mgr.h_query = NULL;
		}
		cl_obj_unlock( &p_port->obj );
		cl_spinlock_release( &p_port->recv_lock );

		CL_ASSERT( p_port->ib_mgr.h_qp );
		memset( &qp_mod, 0, sizeof(ib_qp_mod_t) );
		qp_mod.req_state = IB_QPS_ERROR;
		status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("IB QP was put into IB_QPS_ERROR state\n") );
		__endpt_mgr_reset_all( p_port );

		ipoib_port_deref( p_port, ref_join_bcast );
		return;
	}
#if IPOIB_CM
	if( p_port->p_adapter->params.cm_enabled &&
		!p_port->p_local_endpt->conn.h_cm_listen )
	{
		if( ipoib_port_listen( p_port ) != IB_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Port CM Listen failed\n" ) );
			/*keep going with UD only */
			p_port->p_adapter->params.cm_enabled = FALSE;

			NdisWriteErrorLogEntry( p_port->p_adapter->h_adapter,
				EVENT_IPOIB_CONNECTED_MODE_ERR, 1, 0xbadc0de3 );
		}
	}
#endif
	/* garbage collector timer is needed when link is active */
	gc_due_time.QuadPart = -(int64_t)(((uint64_t)p_port->p_adapter->params.mc_leave_rescan * 2000000) * 10);
	KeSetTimerEx(&p_port->gc_timer,gc_due_time,
			    (LONG)p_port->p_adapter->params.mc_leave_rescan*1000,&p_port->gc_dpc);

	KeSetEvent( &p_port->sa_event, EVENT_INCREMENT, FALSE );
	ipoib_port_deref( p_port, ref_join_bcast );
	IPOIB_EXIT( IPOIB_DBG_INIT );
}


static void
__qp_event(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
	CL_ASSERT( p_event_rec->context );
	((ipoib_port_t*)p_event_rec->context)->p_adapter->hung = TRUE;
}


static void
__cq_event(
	IN				ib_async_event_rec_t		*p_event_rec )
{
	UNUSED_PARAM( p_event_rec );
	CL_ASSERT( p_event_rec->context );
	((ipoib_port_t*)p_event_rec->context)->p_adapter->hung = TRUE;
}


static ib_api_status_t
__ib_mgr_activate(
	IN				ipoib_port_t* const			p_port )
{
	ib_api_status_t	status;
	ib_dgrm_info_t	dgrm_info;
	ib_qp_mod_t		qp_mod;

	IPOIB_ENTER( IPOIB_DBG_INIT );
	/*
	 * Move the QP to RESET.  This allows us to reclaim any
	 * unflushed receives.
	 */
	memset( &qp_mod, 0, sizeof(ib_qp_mod_t) );
	qp_mod.req_state = IB_QPS_RESET;
	status = p_port->p_adapter->p_ifc->modify_qp( p_port->ib_mgr.h_qp, &qp_mod );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_modify_qp returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("IB QP was put into IB_QPS_ERROR state\n") );

	/* Move the QP to RTS. */
	dgrm_info.port_guid = p_port->p_adapter->guids.port_guid.guid;
	dgrm_info.qkey = p_port->ib_mgr.bcast_rec.qkey;
	dgrm_info.pkey_index = p_port->pkey_index;
	status = p_port->p_adapter->p_ifc->init_dgrm_svc( p_port->ib_mgr.h_qp, &dgrm_info );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_init_dgrm_svc returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* Rearm the CQs. */
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_recv_cq, FALSE );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_rearm_cq for recv returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}
	status = p_port->p_adapter->p_ifc->rearm_cq( p_port->ib_mgr.h_send_cq, FALSE );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_rearm_cq for send returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	IPOIB_EXIT( IPOIB_DBG_INIT );
	return IB_SUCCESS;
}


/* Transition to a passive level thread. */

ib_api_status_t
ipoib_port_join_mcast(
	IN				ipoib_port_t* const		p_port,
	IN		const	mac_addr_t				mac,
	IN		const	uint8_t					state)
{
	ib_api_status_t		status;
	ib_mcast_req_t		mcast_req;
	ipoib_endpt_t		*p_endpt;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
		("Join Multicast request: src MAC: %s\n", mk_mac_str(&mac)) );

	switch( __endpt_mgr_ref( p_port, mac, &p_endpt ) )
	{
	case NDIS_STATUS_NO_ROUTE_TO_DESTINATION:
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
				("NDIS_STATUS_NO_ROUTE_TO_DESTINATION\n") );
		break;

	case NDIS_STATUS_SUCCESS:
		ipoib_endpt_deref( p_endpt );
		/* Fall through */

	case NDIS_STATUS_PENDING:
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("<ipoib_port_join_mcast> PENDING\n") );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("__endpt_mgr_ref on EP %s\n", p_endpt->tag));
		return IB_SUCCESS;
	}
	XIPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
		("__endpt_mgr_ref called for %p\n", p_endpt));
	/*
	 * Issue the mcast request, using the parameters of the broadcast group.
	 * This allows us to do a create request that should always succeed since
	 * the required parameters are known.
	 */
	memset( &mcast_req, 0, sizeof(mcast_req) );
	mcast_req.create = TRUE;

	/* Copy the settings from the broadcast group. */
	mcast_req.member_rec = p_port->ib_mgr.bcast_rec;
	/* Clear fields that aren't specified in the join */
	mcast_req.member_rec.mlid = 0;
	ib_member_set_state( &mcast_req.member_rec.scope_state,state);

	if( (mac.addr[0] == 1) && (mac.addr[2] == 0x5E ))
	{
		/*
		 * Update the address portion of the MGID with the 28 lower bits of the
		 * IP address.  Since we're given a MAC address, we are using 
		 * 24 lower bits of that network-byte-ordered value (assuming MSb
		 * is zero) and 4 lsb bits of the first byte of IP address.
		 */
		mcast_req.member_rec.mgid.raw[12] = mac.addr[1];
		mcast_req.member_rec.mgid.raw[13] = mac.addr[3];
		mcast_req.member_rec.mgid.raw[14] = mac.addr[4];
		mcast_req.member_rec.mgid.raw[15] = mac.addr[5];
	}
	else
	{
		/* Handle non IP multicast MAC addresses. */
		/* Update the signature to use the lower 2 bytes of the OpenIB OUI. */
		mcast_req.member_rec.mgid.raw[2] = 0x14;
		mcast_req.member_rec.mgid.raw[3] = 0x05;
		/* Now copy the MAC address into the last 6 bytes of the GID. */
		memcpy( &mcast_req.member_rec.mgid.raw[10], mac.addr, 6 );
	}

	mcast_req.mcast_context = p_port;
	mcast_req.pfn_mcast_cb = __mcast_cb;
	mcast_req.timeout_ms = p_port->p_adapter->params.sa_timeout;
	mcast_req.retry_cnt = p_port->p_adapter->params.sa_retry_cnt;
	mcast_req.port_guid = p_port->p_adapter->guids.port_guid.guid;
	mcast_req.pkey_index = p_port->pkey_index;
	mcast_req.member_rec.pkey = cl_hton16(p_port->p_adapter->guids.port_guid.pkey);
	/*
	 * Create the endpoint and insert it in the port.  Since we don't wait for
	 * the mcast SA operations to complete before returning from the multicast
	 * list set OID asynchronously, it is possible for the mcast entry to be
	 * cleared before the SA interaction completes.  In this case, when the
	 * mcast callback is invoked, it would not find the corresponding endpoint
	 * and would be undone.
	 */
	p_endpt = ipoib_endpt_create( p_port,
								  &mcast_req.member_rec.mgid,
								  0,
								  CL_HTON32(0x00FFFFFF) );
	if( !p_endpt )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ipoib_endpt_create failed.\n") );
		return IB_INSUFFICIENT_MEMORY;
	}
#if DBG_ENDPT
	ipoib_port_ref( p_port, ref_endpt_track );
#endif

	status = __endpt_mgr_insert_locked( p_port, mac, p_endpt );
	if( status != IB_SUCCESS )
	{
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("__endpt_mgr_insert_locked returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
		return status;
	}

	/* reference the object for the multicast join request. */
	ipoib_port_ref( p_port, ref_join_mcast );

	status = p_port->p_adapter->p_ifc->join_mcast( p_port->ib_mgr.h_qp, &mcast_req, NULL );
	if( status != IB_SUCCESS )
	{
		ipoib_port_deref( p_port, ref_mcast_join_failed );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("ib_join_mcast returned %s\n", 
			p_port->p_adapter->p_ifc->get_err_str( status )) );
	}
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
	("Joined MCAST group with MGID[10:15]= %0x %0x %0x %0x %0x %0x\n",
		mcast_req.member_rec.mgid.raw[10],
		mcast_req.member_rec.mgid.raw[11],
		mcast_req.member_rec.mgid.raw[12],
		mcast_req.member_rec.mgid.raw[13],
		mcast_req.member_rec.mgid.raw[14],
		mcast_req.member_rec.mgid.raw[15]) );

	IPOIB_EXIT( IPOIB_DBG_MCAST );
	return status;
}


static void
__mcast_cb(
	IN				ib_mcast_rec_t				*p_mcast_rec )
{
	ib_api_status_t		status;
	ipoib_port_t		*p_port;
	cl_fmap_item_t		*p_item;
	ipoib_endpt_t		*p_endpt;
	cl_qlist_t	complete_list;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)p_mcast_rec->mcast_context;
	cl_qlist_init(&complete_list);

	cl_obj_lock( &p_port->obj );
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	if( p_port->state != IB_QPS_RTS )
	{
		cl_obj_unlock( &p_port->obj );
		if( p_mcast_rec->status == IB_SUCCESS )

		{
			ipoib_port_ref(p_port, ref_leave_mcast);
			p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast,
												   __leave_error_mcast_cb );
		}
		ipoib_port_deref( p_port, ref_mcast_inv_state );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Invalid state - Aborting.\n") );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_INIT,
			("Invalid state - Aborting.\n") );
		cl_spinlock_acquire(&p_port->send_lock);
		//ipoib_port_resume(p_port , FALSE);
		__pending_list_destroy( p_port, &complete_list );
		cl_spinlock_release(&p_port->send_lock);
		send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());
		return;
	}

	if( p_mcast_rec->status != IB_SUCCESS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Multicast join request failed with status %s.\n",
				p_port->p_adapter->p_ifc->get_err_str( p_mcast_rec->status )) );
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung =TRUE;
		ipoib_port_deref( p_port, ref_mcast_req_failed );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	p_item = cl_fmap_get( &p_port->endpt_mgr.gid_endpts,
						  &p_mcast_rec->p_member_rec->mgid );
	if( p_item == cl_fmap_end( &p_port->endpt_mgr.gid_endpts ) )
	{
		/*
		 * The endpoint must have been flushed while the join request
		 * was outstanding.  Just leave the group and return.  This
		 * is not an error.
		 */
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT(TRACE_LEVEL_WARNING, IPOIB_DBG_ERROR,
			("Failed to find endpoint for update.\n") );

		ipoib_port_ref(p_port, ref_leave_mcast);
		p_port->p_adapter->p_ifc->leave_mcast( p_mcast_rec->h_mcast,
											   __leave_error_mcast_cb );
		ipoib_port_deref( p_port, ref_mcast_no_endpt );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, gid_item );
	p_endpt->p_ifc = p_port->p_adapter->p_ifc;

	/* Setup the endpoint for use. */
	status = ipoib_endpt_set_mcast( p_endpt,
									p_port->ib_mgr.h_pd,
									p_port->port_num,
									p_mcast_rec );
	if( status != IB_SUCCESS )
	{
		cl_obj_unlock( &p_port->obj );
		IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_MCAST,
			("ipoib_endpt_set_mcast returned %s.\n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
		/* Flag the adapter as hung. */
		p_port->p_adapter->hung = TRUE;
		ipoib_port_deref( p_port, ref_mcast_av_failed );
		IPOIB_EXIT( IPOIB_DBG_MCAST );
		return;
	}

	/*
	 * The endpoint is already in the GID and MAC maps.
	 * mast endpoint are not used in the LID map.
	 */
	CL_ASSERT(p_endpt->dlid == 0);
	/* set flag that endpoint is use */
	p_endpt->is_in_use = TRUE;
	cl_obj_unlock( &p_port->obj );
	
	/* Try to send all pending sends. */
	cl_spinlock_acquire( &p_port->send_lock );
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
		("Calling ipoib_port_resume from mcast_cb, xmit pending sends\n"));
	ipoib_port_resume(p_port , FALSE, &complete_list);
	cl_spinlock_release( &p_port->send_lock );
	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());

	ipoib_port_deref( p_port, ref_join_mcast );

	IPOIB_EXIT( IPOIB_DBG_MCAST );
}

void
ipoib_leave_mcast_cb(
	IN				void				*context )
{
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)context;

	IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_MCAST,
		("port[%d] mcast_cnt %d\n", p_port->port_num, p_port->mcast_cnt));
	
	ipoib_port_deref( p_port, ref_leave_mcast);
	//It happens
	//ASSERT(p_port->mcast_cnt > 0);
	cl_atomic_dec( &p_port->mcast_cnt);
	
	if(0 == p_port->mcast_cnt)
	{
		KeSetEvent( &p_port->leave_mcast_event, EVENT_INCREMENT, FALSE );
	}
	
	IPOIB_PRINT_EXIT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_MCAST,
		("Leave mcast callback deref ipoib_port[%d] (ref_leave_mcast)\n",
			p_port->port_num) );
	
	IPOIB_EXIT( IPOIB_DBG_MCAST );
}

void
__leave_error_mcast_cb(
	IN				void				*context )
{
	ipoib_port_t		*p_port;

	IPOIB_ENTER( IPOIB_DBG_MCAST );

	p_port = (ipoib_port_t*)context;

	ipoib_port_deref( p_port, ref_leave_mcast);
	IPOIB_PRINT_EXIT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_MCAST,
			("Leave mcast callback deref ipoib_port \n") );
	
	IPOIB_EXIT( IPOIB_DBG_MCAST );
}

/*++
Routine Description:
    The routine process the packet and returns LSO information
    
Arguments:
	pNetBuffer - a pointer to the first net buffer object of the packet
	pLsoData - pointer to LsoData object in which the routine returns the LSO information
	ipoib_hdr - pointer to preallocated IPoIB hdr
	TcpHeaderOffset - offset to the begining of the TCP header in the packet
			 
Return Value:
     NDIS_STATUS   

NOTE:
    called at DISPATCH level
--*/


NDIS_STATUS 
GetLsoHeaderSize(
	IN OUT	PNET_BUFFER		pNetBuffer,
	IN		LsoData 		*pLsoData,
	IN		ipoib_hdr_t 	*ipoib_hdr,
	ULONG 					TcpHeaderOffset
	)
{
	static const uint16_t coEthHeaderSize(14); 

	IPOIB_ENTER( IPOIB_DBG_SEND );

	ASSERT(TcpHeaderOffset > 0);
	ASSERT(pNetBuffer != NULL);

	PUCHAR pSrc = NULL;
	bool fAlreadyCopied = false;
	PUCHAR pCopiedData = pLsoData->coppied_data;
	UINT CurrLength = 0;
	ULONG DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET( pNetBuffer );

	PMDL pMDL = NET_BUFFER_CURRENT_MDL( pNetBuffer );
	NdisQueryMdl( pMDL, &pSrc, &CurrLength, NormalPagePriority );
	if (pSrc == NULL) 
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("NdisQueryMdl failed\n") );
		return NDIS_STATUS_INVALID_PACKET;
	}

	pSrc += DataOffset;
	CurrLength -= DataOffset;

	if( CurrLength < TcpHeaderOffset )
	{
		//
		// We assume that the ETH and IP header exist in first segment
		//
		ASSERT( CurrLength >= TcpHeaderOffset );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Error processing packets."
			"The ETH & IP headers are divided into multiple segments\n") );
		return NDIS_STATUS_INVALID_PACKET;
	}

	if ( CurrLength == TcpHeaderOffset )
	{
		ASSERT( CurrLength > TcpHeaderOffset );
		memcpy( pCopiedData, pSrc , TcpHeaderOffset );
		pCopiedData += TcpHeaderOffset;

		fAlreadyCopied = true;
		pNetBuffer = NET_BUFFER_NEXT_NB( pNetBuffer );
		NdisQueryMdl( NET_BUFFER_CURRENT_MDL(pNetBuffer), &pSrc, &CurrLength, NormalPagePriority);

		if ( pSrc == NULL ) 
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, ("NdisQueryMdl failed\n") );
			return NDIS_STATUS_INVALID_PACKET;
	    }
		// BUGBUG: If we do reach here, make sure that  pLsoData->LsoBuffers.pData will be set correctly.
	}
	else
	{
		//IMPORTANT: we de-facto replace ETH header by IPoIB header here
		//TODO: This is not good practice to change data we got from NDIS
		pLsoData->LsoBuffers.pData = pSrc + (coEthHeaderSize - sizeof (ipoib_hdr_t));
		memcpy ( pLsoData->LsoBuffers.pData, ipoib_hdr, sizeof (ipoib_hdr_t) );
		pLsoData->LsoHeaderSize = TcpHeaderOffset - (coEthHeaderSize - sizeof (ipoib_hdr_t));
		
		CurrLength -= TcpHeaderOffset;
		pSrc += TcpHeaderOffset;
	}

	if ( CurrLength < sizeof(tcp_hdr_t) ) 
	{
		ASSERT( CurrLength >= sizeof(tcp_hdr_t) );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Error processing packets"
			"The buffer is too small to contain TCP header\n") );
		return NDIS_STATUS_INVALID_PACKET;
	}

	//
	// We found the TCP header
	//
	tcp_hdr_t UNALIGNED* TcpHdr = (tcp_hdr_t UNALIGNED *)pSrc;
	uint16_t TcpHeaderLen = TCP_HEADER_LENGTH( TcpHdr );
	if (CurrLength < TcpHeaderLen)
	{
		ASSERT( CurrLength >= sizeof(tcp_hdr_t) );
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Error processing packets"
			"The buffer is too small to contain TCP data\n") );
		return NDIS_STATUS_INVALID_PACKET;
	}

	pLsoData->LsoHeaderSize += TcpHeaderLen;
	if ( pLsoData->LsoHeaderSize > LSO_MAX_HEADER )
	{
		ASSERT(pLsoData->LsoBuffers.Len <= LSO_MAX_HEADER);
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
			("Error processing packets.LsoHeaderSize > LSO_MAX_HEADER\n") );
		return NDIS_STATUS_DEVICE_FAILED;
	}

	if( fAlreadyCopied )
	{
		memcpy(pCopiedData, pSrc, TcpHeaderLen);
		pCopiedData += TcpHeaderLen;
		pLsoData->LsoBuffers.pData = pLsoData->coppied_data;
	}
#if DBG	
	pLsoData->LsoBuffers.Len = 
		TcpHeaderOffset + TcpHeaderLen - (coEthHeaderSize - sizeof (ipoib_hdr_t));
	ASSERT( pLsoData->LsoBuffers.Len <= MAX_LSO_SIZE );
#endif

	IPOIB_EXIT( IPOIB_DBG_SEND );
	return NDIS_STATUS_SUCCESS;
}

static void __port_do_mcast_garbage(ipoib_port_t* const	p_port)
{
    const mac_addr_t DEFAULT_MCAST_GROUP = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
	/* Do garbage collecting... */

	cl_map_item_t	*p_item;
	ipoib_endpt_t	*p_endpt;
	cl_qlist_t		destroy_mc_list;
	uint8_t			cnt;
	const static GC_MAX_LEAVE_NUM = 80;

	cl_qlist_init( &destroy_mc_list );

	cl_obj_lock( &p_port->obj );
	/* Wait for all readers to finish */
	while( p_port->endpt_rdr )
	{
		cl_obj_unlock( &p_port->obj );
		cl_obj_lock( &p_port->obj );
	}
	cnt = 0;
	p_item = cl_qmap_head( &p_port->endpt_mgr.mac_endpts );
	while( (p_item != cl_qmap_end( &p_port->endpt_mgr.mac_endpts )) && (cnt < GC_MAX_LEAVE_NUM))
	{
		p_endpt = PARENT_STRUCT( p_item, ipoib_endpt_t, mac_item );
		p_item = cl_qmap_next( p_item );

		/* Check if the current endpoint is not a multicast listener */

		if( p_endpt->h_mcast && 
			(!p_endpt->is_mcast_listener) &&
			( memcmp( &p_endpt->mac, &DEFAULT_MCAST_GROUP, sizeof(mac_addr_t) ) &&
			 (!p_endpt->is_in_use) ))
		{
			cl_qmap_remove_item( &p_port->endpt_mgr.mac_endpts,
				&p_endpt->mac_item );
			cl_fmap_remove_item( &p_port->endpt_mgr.gid_endpts,
				&p_endpt->gid_item );

			if( p_endpt->dlid )
			{
				cl_qmap_remove_item( &p_port->endpt_mgr.lid_endpts,
					&p_endpt->lid_item );
				p_endpt->dlid = 0;
			}

			cl_qlist_insert_tail(
				&destroy_mc_list, &p_endpt->mac_item.pool_item.list_item );
			cnt++;
		}
		else
			p_endpt->is_in_use = FALSE;
	}
	cl_obj_unlock( &p_port->obj );

	/* Destroy all multicast endpoints now that we have released the lock. */
	while( cl_qlist_count( &destroy_mc_list ) )
	{
		p_endpt = PARENT_STRUCT( cl_qlist_remove_head( &destroy_mc_list ),
								 ipoib_endpt_t, mac_item.pool_item.list_item );
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_ENDPT,
			("mcast garbage collector: destroying EP %p %s %s\n", 
				p_endpt, p_endpt->tag, mk_mac_str(&p_endpt->mac)) );

		cl_obj_destroy( &p_endpt->obj );
	}
}


static void
__port_mcast_garbage_dpc(KDPC *p_gc_dpc,void *context,void *s_arg1, void *s_arg2)
{
	ipoib_port_t *p_port = (ipoib_port_t *) context;

	UNREFERENCED_PARAMETER(p_gc_dpc);
	UNREFERENCED_PARAMETER(s_arg1);
	UNREFERENCED_PARAMETER(s_arg2);

	__port_do_mcast_garbage(p_port);
}

ipoib_endpt_t*
ipoib_endpt_get_by_gid(
	IN				ipoib_port_t* const			p_port,
	IN		const	ib_gid_t* const				p_gid )
{
	return __endpt_mgr_get_by_gid( p_port, p_gid );
}

ipoib_endpt_t*
ipoib_endpt_get_by_lid(
	IN				ipoib_port_t* const			p_port,
	IN		const	net16_t						lid )
{
	return __endpt_mgr_get_by_lid( p_port, lid );
}

ib_api_status_t
ipoib_recv_dhcp(
	IN				ipoib_port_t* const			p_port,
	IN		const	ipoib_pkt_t* const			p_ipoib,
		OUT			eth_pkt_t* const			p_eth,
	IN				ipoib_endpt_t* const		p_src,
	IN				ipoib_endpt_t* const		p_dst )
{
	return __recv_dhcp(
		p_port,	p_ipoib, p_eth,	p_src,p_dst );
}


void
ipoib_port_cancel_xmit(
	IN				ipoib_port_t* const		p_port,
	IN				PVOID					 cancel_id )
{
	cl_list_item_t		*p_item, *p_next;
	ipoib_send_NB_SG	*s_buf;
	PVOID			nbl_id;
	cl_qlist_t		complete_list;
	IPOIB_ENTER( IPOIB_DBG_SEND );

	cl_qlist_init( &complete_list );
	
	ASSERT(FALSE); //TODO ???????????????? Do we reach here ????????????

	cl_spinlock_acquire( &p_port->send_lock );

	

	for( p_item = cl_qlist_head( &p_port->send_mgr.pending_list );
		p_item != cl_qlist_end( &p_port->send_mgr.pending_list );
		p_item = p_next )
	{
		p_next = cl_qlist_next( p_item );
		s_buf = (ipoib_send_NB_SG*) (PVOID) p_item; // TODO: Check this casting
		nbl_id = NDIS_GET_NET_BUFFER_LIST_CANCEL_ID( s_buf->p_nbl );
		if( nbl_id == cancel_id )
		{
			cl_qlist_remove_item( &p_port->send_mgr.pending_list, p_item );
			__send_complete_add_to_list(&complete_list	,s_buf, NDIS_STATUS_REQUEST_ABORTED);
		}
	}
	cl_spinlock_release( &p_port->send_lock );

	send_complete_list_complete(&complete_list, IRQL_TO_COMPLETE_FLAG());
	IPOIB_EXIT( IPOIB_DBG_SEND );
}


static ib_api_status_t
__endpt_update(	
	IN				ipoib_port_t* const			p_port,
	IN				ipoib_endpt_t** const		pp_src,
	IN				const ipoib_hw_addr_t*		p_new_hw_addr,
	IN				ib_wc_t* const				p_wc
)
{
	ib_api_status_t status = IB_ERROR;
	ib_gid_t gid;
	mac_addr_t mac;
	
	/*
	 * If the endpoint exists for the GID, make sure
	 * the dlid and qpn match the arp.
	 */
	if( *pp_src )
	{
		if( cl_memcmp( &(*pp_src)->dgid, &p_new_hw_addr->gid, sizeof(ib_gid_t) ) )
		{
			/*
			 * GIDs for the endpoint are different.  The ARP must
			 * have been proxied.  Dereference it.
			 */
			*pp_src = NULL;
		}
		else if( (*pp_src)->dlid && (*pp_src)->dlid != p_wc->recv.ud.remote_lid )
		{
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
		else if ( ! ((*pp_src)->dlid))
		{
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
		else if( ipoib_is_voltaire_router_gid( &(*pp_src)->dgid ) )
		{
			if( (*pp_src)->qpn != ipoib_addr_get_qpn( p_new_hw_addr ) &&
				 p_wc->recv.ud.remote_qp != ipoib_addr_get_qpn( p_new_hw_addr ) )
			{
				/* Out of date!  Destroy the endpoint and replace it. */
				__endpt_mgr_remove( p_port, *pp_src );
				*pp_src = NULL;
			}
		}
		else if( (*pp_src)->qpn != p_wc->recv.ud.remote_qp )
		{
			/* Out of date!  Destroy the endpoint and replace it. */
			__endpt_mgr_remove( p_port, *pp_src );
			*pp_src = NULL;
		}
	}

	/* Do we need to create an endpoint for this GID? */
	if( !*pp_src )
	{
		/* Copy the src GID to allow aligned access */
		cl_memcpy( &gid, &p_new_hw_addr->gid, sizeof(ib_gid_t) );
		status = ipoib_mac_from_guid( gid.unicast.interface_id,
									  p_port->p_adapter->params.guid_mask,
									  &mac );
		if (status == IB_INVALID_GUID_MASK)
		{
			IPOIB_PRINT( TRACE_LEVEL_WARNING, IPOIB_DBG_ERROR,
				("Invalid GUID mask received, rejecting it") );
			ipoib_create_log( p_port->p_adapter->h_adapter,
							  GUID_MASK_LOG_INDEX,
							  EVENT_IPOIB_WRONG_PARAMETER_WRN );
			return status;
		}
		else if( status != IB_SUCCESS )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_mac_from_guid returned %s\n",
					p_port->p_adapter->p_ifc->get_err_str( status )) );
			return status;
		}

		/*
		 * Create the endpoint.
		 */
		*pp_src = ipoib_endpt_create( p_port,
									  &p_new_hw_addr->gid,
									  p_wc->recv.ud.remote_lid,
									  ipoib_addr_get_qpn( p_new_hw_addr ) );
		
		if( !*pp_src )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ipoib_endpt_create failed\n") );
			status = IB_INSUFFICIENT_MEMORY;
			return status;
		}

		cl_obj_lock( &p_port->obj );
		status = __endpt_mgr_insert( p_port, mac, *pp_src );
		if( status != IB_SUCCESS )
		{
			cl_obj_unlock( &p_port->obj );
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("__endpt_mgr_insert return %s \n",
				p_port->p_adapter->p_ifc->get_err_str( status )) );
			return status;
		}
		cl_obj_unlock( &p_port->obj );
	}

	return IB_SUCCESS;
}

/* 
 *  Put all IP fragments into separate WRs and chain together.
 *  The last WR will be set to generate CQ Event.
 *  lookaside buffer is used for ip headers attached to each WR.
 *  Lookaside buffer will be released on last WR send completion.
 *
 * IPoIB header is pre-built (by build_send_desc) in
 * p_desc->send_wr[0].local_ds[0].length/vaddr
 */

static NDIS_STATUS
__build_ipv4_fragments(
	IN		ipoib_send_NB_SG*			s_buf,
	IN		ip_hdr_t* const				p_ip_hdr,
	IN		uint32_t					buf_len,
	IN		uint32_t					total_ip_len,
	IN		MDL*						p_mdl )
{
	uint32_t	ds_idx = 1;		// ds[0] is ipoib header in send_desc[0]
	uint32_t	wr_idx = 0;
	uint32_t	sgl_idx = 0;
	uint32_t	sgl_offset;
	uint32_t	options_len = 0;
	uint8_t*	p_options = NULL;
	uint8_t*	p_buf;
	uint32_t	frag_offset = 0;
	uint32_t	last_frag = 0;
	uint32_t	cur_sge;
	uint32_t	wr_size = 0;
	uint32_t	ip_hdr_len = IP_HEADER_LENGTH( p_ip_hdr );
	uint32_t	tx_mtu = s_buf->p_endpt->tx_mtu;
	uint32_t	seg_len;
	uint64_t	next_sgl_addr;
	ULONG		DataOffset;
	uint32_t	need, mtu_avail;
	int			mtu_data;
	uint32_t	frag_cnt=0;

	ipoib_port_t* const			p_port = s_buf->p_port;
	ipoib_send_desc_t* const	p_desc = s_buf->p_send_desc;
	SCATTER_GATHER_LIST*		p_sgl = s_buf->p_sgl;

	IPOIB_ENTER( IPOIB_DBG_FRAG );

	if( IP_DONT_FRAGMENT(p_ip_hdr) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Err: IP hdr: Don't Fragment SET? IP len %u\n",total_ip_len) );
		return NDIS_STATUS_INVALID_PACKET;
	}
	
	ASSERT( p_sgl );

	if( ( p_sgl->NumberOfElements > MAX_SEND_SGE ||
		p_sgl->Elements[0].Length < sizeof(eth_hdr_t)) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Too many SG Elements(%d) in packet, sgl[0].Len %d\n",
				p_sgl->NumberOfElements,
				p_sgl->Elements[0].Length) );
		return NDIS_STATUS_FAILURE;
	}

	CL_ASSERT( s_buf->p_send_buf == NULL );
	p_buf = (uint8_t *)
		ExAllocateFromNPagedLookasideList( &p_port->buf_mgr.send_buf_list );
	if( !p_buf )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Failed to allocate lookaside buffer.\n") );
		return NDIS_STATUS_RESOURCES;
	}
	s_buf->p_send_buf = (send_buf_t*)p_buf;

	DataOffset= (ULONG)(NET_BUFFER_CURRENT_MDL_OFFSET(s_buf->p_curr_nb)); 

	if( buf_len < ip_hdr_len )
	{ 	/* ip options in a separate buffer */
		CL_ASSERT( buf_len == sizeof( ip_hdr_t ) );
		NdisGetNextBuffer( p_mdl, &p_mdl );
		if( !p_mdl )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to get IP options buffer.\n") );
			return NDIS_STATUS_FAILURE;
		}

		NdisQueryMdl(p_mdl, &p_options, &options_len, NormalPagePriority);

		if( !p_options )
		{
			IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("Failed to QueryMdl IP options buffer address.\n") );
			return NDIS_STATUS_FAILURE;
		}

        memcpy( p_buf, p_ip_hdr, sizeof( ip_hdr_t ) );
		if( p_options && options_len )
		{ 
			__copy_ip_options( &p_buf[sizeof(ip_hdr_t)],
							   p_options,
							   options_len,
							   TRUE );
		}
		wr_size = buf_len + options_len;
		// sgl_idx++; ??
	}
	else
	{ 	/* options, if any, are in the same buffer */
        memcpy( p_buf, p_ip_hdr, buf_len );
		options_len = ip_hdr_len - sizeof( ip_hdr_t );
		if( options_len )
		{
			p_options = p_buf + sizeof( ip_hdr_t );
		}
		//frag_offset += ( buf_len - ip_hdr_len );
		wr_size = buf_len;
	}
	
	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
		("IP.length %u Opt[%s%s] buf_len %u opt_len %u frag_off %u DataOff %u\n",
			total_ip_len,
			(options_len ? "Yes":"No"),
			(options_len ?
				(p_buf[sizeof(ip_hdr_t)] & 0x80 ? " copy: Yes":" copy: No") : ""),
			buf_len, options_len, frag_offset, DataOffset) );

	/* local_ds[0] preset to ipoib_hdr_t in port->hdr[x] */
	CL_ASSERT(p_desc->send_wr[0].local_ds[0].length == sizeof( ipoib_hdr_t ) );

	CL_ASSERT( ds_idx == 1 );
	p_desc->send_wr[wr_idx].local_ds[1].vaddr = cl_get_physaddr( p_buf );
	p_desc->send_wr[wr_idx].local_ds[1].lkey = p_port->ib_mgr.lkey;
	p_desc->send_wr[wr_idx].local_ds[1].length = wr_size;
	
	/* Ethernet header starts @ sgl[0] + DataOffset.
	 * skip Eth hdr + IP hdr + IP options to IP packet data beyond buf_len.
	 */
	sgl_offset = DataOffset + sizeof(eth_hdr_t) + wr_size;
	next_sgl_addr = p_sgl->Elements[sgl_idx].Address.QuadPart + sgl_offset;
	cur_sge = p_sgl->Elements[sgl_idx].Length - sgl_offset;

	if( cur_sge == 0 )
	{
		cur_sge = p_sgl->Elements[++sgl_idx].Length;
		next_sgl_addr = p_sgl->Elements[sgl_idx].Address.QuadPart;
		IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
			("cur_sge == 0 Next sge[%u] cur_sge %u\n", sgl_idx, cur_sge) );
	}

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
		("sgl[%u].Len %u cur_sge %u  wr_size %u mtu_left %u\n\n",
			sgl_idx, p_sgl->Elements[sgl_idx].Length, cur_sge, wr_size,
			(tx_mtu - wr_size)) );

	total_ip_len -= wr_size;
	ds_idx++;
	ASSERT( ds_idx == 2 );
	frag_cnt = 0;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
		("main:\n  wr[%u] ds_idx %u cur_sge %u tot_ip_len %u wr_size %u "
		 "mtu_avail %u frag_offset %u\n\n",
			wr_idx, ds_idx, cur_sge, total_ip_len, wr_size, (tx_mtu - wr_size),
			frag_offset) );

	for( ; sgl_idx < p_sgl->NumberOfElements; sgl_idx++ )
	{
		if( wr_idx >= ( MAX_WRS_PER_MSG - 1 ) )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
				("ResourceErr: wr_idx %d >= MAX_WRS_PER_MSG-1 %d\n",
					wr_idx,( MAX_WRS_PER_MSG - 1 )) );
			return NDIS_STATUS_RESOURCES;
		}
		
		if( cur_sge == 0 )
		{
			cur_sge = p_sgl->Elements[sgl_idx].Length;
			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
				("NEW sge[%u] cur_sge %u wr_size %u total_ip_len %u\n",
					sgl_idx, cur_sge, (tx_mtu - wr_size), total_ip_len) );
			next_sgl_addr = p_sgl->Elements[sgl_idx].Address.QuadPart;
		}

		while( cur_sge )
		{
			if( ds_idx == 0 )
			{	/* ipoib header preset in send_wr[0] */
				p_desc->send_wr[wr_idx].local_ds[0] = p_desc->send_wr[0].local_ds[0];

				++ds_idx;

				/* set IP header */
        		memcpy( p_buf, p_ip_hdr, sizeof( ip_hdr_t ) );
				if( p_options && options_len )
				{
					/* copy ip options if needed */
					__copy_ip_options( &p_buf[sizeof(ip_hdr_t)], 
									   p_options,
									   options_len,
									   FALSE );
				}
				wr_size = ip_hdr_len;

				/* ds_idx == 1, setup IP header */
				p_desc->send_wr[wr_idx].local_ds[1].length = ip_hdr_len;
				p_desc->send_wr[wr_idx].local_ds[1].vaddr = cl_get_physaddr( p_buf );
				p_desc->send_wr[wr_idx].local_ds[1].lkey = p_port->ib_mgr.lkey;
				++ds_idx;
			}

			mtu_avail = tx_mtu - wr_size;
			mtu_data = (int) (mtu_avail  & ~7);

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
				("tot_ip_len %u mtu_avail %u wr_size %u mtu_data %d cur_sge %u\n",
					total_ip_len, mtu_avail, wr_size, mtu_data, cur_sge) );

			/* IP Packet data must be in 8-byte chunks, except for the last frag. */ 

			if( total_ip_len <= mtu_avail && cur_sge <= mtu_avail)
			{
				IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
					("Last Frag(%u)\n", (frag_cnt+1)) );
				seg_len = cur_sge;
			}
			else
				seg_len = cur_sge & (~7);

			if( seg_len == 0 )
			{
				if( last_frag )
				{
					IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
						("seg_len == 0 && last_frag > 0: last_frag %u need %u\n",
							last_frag,cur_sge) );
					last_frag += cur_sge;
					seg_len = cur_sge;
					CL_ASSERT(0);
				}
				else
				{
					last_frag = seg_len = cur_sge;
					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
						("last_frag(0): last_frag/seg_len %u\n", seg_len) );
				}
				seg_len = (seg_len > (uint32_t)mtu_data
								? (uint32_t)mtu_data : seg_len );
			}
			else
			{
				if( last_frag )
				{	 // frag unaligned
					need = 8 - last_frag;
					seg_len = (seg_len - 8) + need;

					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
						("last_frag_ADJ: last %u need %u cur_sge %u "
						 "adj_seg_len %u\n", last_frag, need, cur_sge, seg_len) );
					last_frag = 0;
				}
				else
					need = 0;

				seg_len = (seg_len > (uint32_t)mtu_data
								? ((uint32_t)mtu_data + need) : seg_len);
			}

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
				("Set wr[%d].ds[%d] seg_len %u\n", wr_idx, ds_idx, seg_len) );

			p_desc->send_wr[wr_idx].local_ds[ds_idx].vaddr = next_sgl_addr;
			p_desc->send_wr[wr_idx].local_ds[ds_idx].length = seg_len;
			p_desc->send_wr[wr_idx].local_ds[ds_idx].lkey = p_port->ib_mgr.lkey;
			++ds_idx;
			
			wr_size += seg_len;
			total_ip_len -= seg_len;
			mtu_data -= seg_len;

			IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
				("mtu_data %d wr_size %u tx_mtu %u total_ip_len %u\n",
					mtu_data, wr_size, tx_mtu, total_ip_len) );

			if( (int)mtu_data <= 0 || wr_size >= tx_mtu || total_ip_len == 0 )
			{	/* fix ip hdr for current fragment */
				if( frag_cnt == 0 )
				{
					/* fix ip hdr for the first fragment and continue */
					__update_fragment_ip_hdr( (ip_hdr_t* const)p_buf,
											  (uint16_t)wr_size,
											  IP_FRAGMENT_OFFSET(p_ip_hdr),
											  TRUE );
					p_buf += ip_hdr_len;
					p_buf += ((buf_len > ip_hdr_len) ? ( buf_len - ip_hdr_len ): 0);
					frag_offset += (wr_size - ip_hdr_len);
					p_desc->send_wr[wr_idx].wr.num_ds = ds_idx;

					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
						("Finalize frag-0 wr[%d] total_ip_len %u frag_offset %u\n",
							wr_idx, total_ip_len, frag_offset) );
				}
				else
				{
					__update_fragment_ip_hdr( (ip_hdr_t* const)p_buf,
											  (uint16_t)wr_size,
											  ((uint16_t)(frag_offset >> 3 )), 
											  (BOOLEAN)(( total_ip_len > 0 ) ||
													IP_MORE_FRAGMENTS(p_ip_hdr)) );
					p_buf += ip_hdr_len;
					p_desc->send_wr[wr_idx].wr.num_ds = ds_idx;
					frag_offset += (wr_size - ip_hdr_len);
#if DBG
					if( total_ip_len > 0 )
					{
						CL_ASSERT( (frag_offset & 7) == 0 );
					}
#endif
					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
						("Finalize frag-%d wr[%d] total_ip_len %u\n",
							frag_cnt+1, wr_idx, total_ip_len) );
				}
				frag_cnt++;

				if( total_ip_len > 0 )
				{
					++wr_idx;
					wr_size = 0;
					ds_idx = 0;
					IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
						("New wr[%d] sgl[%d] < Max %d, total_ip_len %u cur_sge %u "
						 "seg_len %u\n",
							wr_idx,sgl_idx,p_sgl->NumberOfElements,
							total_ip_len, (cur_sge-seg_len), seg_len) );
				}
				else
				{
					CL_ASSERT( (cur_sge - seg_len) == 0 );
					DIPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_FRAG,
						("IP bytes remaining %d\n", (cur_sge - seg_len)) );
				}
			}
			cur_sge -= seg_len;
			if( cur_sge > 0 )
				next_sgl_addr += seg_len;
		}
	}
	p_desc->num_wrs = wr_idx + 1;

	IPOIB_PRINT( TRACE_LEVEL_INFORMATION, IPOIB_DBG_FRAG,
		("Exit - num_wrs %d frag_cnt %u\n",p_desc->num_wrs,frag_cnt) );

	IPOIB_EXIT( IPOIB_DBG_FRAG );
	return NDIS_STATUS_SUCCESS;
}


static void
__update_fragment_ip_hdr(
	IN		ip_hdr_t* const		p_ip_hdr,
	IN		uint16_t			fragment_size, 
	IN		uint16_t			fragment_offset, 
	IN		BOOLEAN				more_fragments )
{
	uint16_t*	p_hdr = (uint16_t*)p_ip_hdr;

	p_ip_hdr->length = cl_hton16( fragment_size ); // bytes
	p_ip_hdr->offset_flags = cl_hton16( fragment_offset ); // 8-byte units

	if( more_fragments )
	{
		IP_SET_MORE_FRAGMENTS( p_ip_hdr );
	}
	else
	{
		IP_SET_LAST_FRAGMENT( p_ip_hdr );
	}
	p_ip_hdr->chksum = 0;
	p_ip_hdr->chksum = ipchksum( p_hdr, IP_HEADER_LENGTH(p_ip_hdr) );
}

static void
__copy_ip_options(
	IN		uint8_t*	p_buf,
	IN		uint8_t*	p_options,
	IN		uint32_t	options_len,
	IN		BOOLEAN		copy_all )
{
	uint32_t	option_length;
	uint32_t	total_length = 0;
	uint32_t	copied_length = 0;
	uint8_t*	p_src = p_options;
	uint8_t*	p_dst = p_buf;

	if( p_options == NULL || options_len == 0 )
		return;
	if( copy_all )
	{
		memcpy( p_dst, p_src, options_len );
		return;
	}
	do
	{
		if( ( *p_src ) == 0 ) // end of options list
		{
			total_length++;
			break;
		}
		if( ( *p_src ) == 0x1 ) // no op
		{
			p_src++;
			total_length++;
			continue;
		}
		/*from RFC791: 
		* This option may be used between options, for example, to align
        * the beginning of a subsequent option on a 32 bit boundary.
		*/
		if( copied_length && (copied_length % 4) )
		{
			uint32_t align = 4 - (copied_length % 4);
			memset( p_dst, 0x1, (size_t)align );
			p_dst += align;
			copied_length += align;
		}
		option_length = *(p_src + 1);

		if( *p_src & 0x80 )
		{
			memcpy( p_dst, p_src, option_length );
			p_dst += option_length;
			copied_length += option_length;
		}
		total_length += option_length;
		p_src += option_length;

	}while( total_length < options_len );

	CL_ASSERT( total_length == options_len );
	CL_ASSERT( copied_length <= 40 );

	/* padding the rest */
	if( options_len > copied_length )
	{
		memset( p_dst, 0, ( options_len - copied_length ) );
	}
	return;
}
