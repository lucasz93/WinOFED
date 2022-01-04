/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2004-2005 Mellanox Technologies, Inc. All rights reserved. 
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

#pragma once

#include <iba/ib_ci.h>
#include <complib/comp_lib.h>

extern char				mlnx_uvp_lib_name[];


#define MLNX_MAX_HCA   4
#define MLNX_NUM_HOBKL MLNX_MAX_HCA
#define MLNX_NUM_CB_THR     1
#define MLNX_SIZE_CB_POOL 256
#define MLNX_UAL_ALLOC_HCA_UL_RES 1
#define MLNX_UAL_FREE_HCA_UL_RES 2


// Defines for QP ops
#define	MLNX_MAX_NUM_SGE 8
#define	MLNX_MAX_WRS_PER_CHAIN 4

#define MLNX_NUM_RESERVED_QPS 16

/*
 * Completion model.
 *	0: No DPC processor assignment
 *	1: DPCs per-CQ, processor affinity set at CQ initialization time.
 *	2: DPCs per-CQ, processor affinity set at runtime.
 *	3: DPCs per-CQ, no processor affinity set.
 */
#define MLNX_COMP_MODEL		3

#if DBG
#define VALIDATE_INDEX(index, limit, error, label) \
	{                  \
		if (index >= limit) \
		{                   \
			status = error;   \
			HCA_PRINT(TRACE_LEVEL_ERROR  , g_mlnx_dbg_lvl  ,("file %s line %d\n", __FILE__, __LINE__)));\
			goto label;       \
		}                   \
	}
#else
#define VALIDATE_INDEX(index, limit, error, label)
#endif



// Typedefs

typedef enum {
	E_EV_CA=1,
	E_EV_QP,
	E_EV_CQ,
	E_EV_LAST
} ENUM_EVENT_CLASS;

typedef enum {
	E_MR_PHYS=1,
	E_MR_SHARED,
	E_MR_ANY,
	E_MR_INVALID
} ENUM_MR_TYPE;

/*
 * Attribute cache for port info saved to expedite local MAD processing.
 * Note that the cache accounts for the worst case GID and PKEY table size
 * but is allocated from paged pool, so it's nothing to worry about.
 */

typedef struct _guid_block
{
	boolean_t				valid;
	ib_guid_info_t			tbl;

}	mlnx_guid_block_t;

typedef struct _port_info_cache
{
	boolean_t				valid;
	ib_port_info_t			info;

}	mlnx_port_info_cache_t;

typedef struct _pkey_block
{
	boolean_t				valid;
	ib_pkey_table_t	tbl;

}	mlnx_pkey_block_t;

typedef struct _sl_vl_cache
{
	boolean_t				valid;
	ib_slvl_table_t			tbl;

}	mlnx_sl_vl_cache_t;

typedef struct _vl_arb_block
{
	boolean_t				valid;
	ib_vl_arb_table_t		tbl;

}	mlnx_vl_arb_block_t;

typedef struct _attr_cache
{
	mlnx_guid_block_t		guid_block[32];
	mlnx_port_info_cache_t	port_info;
	mlnx_pkey_block_t		pkey_tbl[2048];
	mlnx_sl_vl_cache_t		sl_vl;
	mlnx_vl_arb_block_t		vl_arb[4];

}	mlnx_cache_t;

typedef struct _ib_mcast {
	ib_gid_t         mcast_gid;
	struct ib_qp    *p_ib_qp;
	uint16_t         mcast_lid;
} mlnx_mcast_t;

typedef struct _mlnx_hca_t {
	cl_list_item_t	list_item;			// to include in the HCA chain
	net64_t			guid;					// HCA node Guid
	uint32_t			hw_ver;				// HCA HW version
	// HOB
	KSPIN_LOCK			event_list_lock;
	LIST_ENTRY			event_list;
	ci_async_event_cb_t async_cb_p;
	const void          *ca_context;
	void                *cl_device_h;
	uint32_t           index;
	cl_async_proc_t     *async_proc_mgr_p;
	struct ib_event_handler ib_event_handler;
} mlnx_hca_t;

// Functions
void
setup_ci_interface(
	IN		const	ib_net64_t					ca_guid,
		OUT			ci_interface_t				*p_interface );

void
mlnx_hca_insert(
	IN				mlnx_hca_t					*p_hca );

void
mlnx_hca_remove(
	IN				mlnx_hca_t					*p_hca );

mlnx_hca_t*
mlnx_hca_from_guid(
	IN				ib_net64_t					guid );

/*
void
mlnx_names_from_guid(
	IN				ib_net64_t					guid,
		OUT			char						**hca_name_p,
		OUT			char						**dev_name_p);
*/

cl_status_t
mlnx_hcas_init( void );

ib_api_status_t
mlnx_set_cb(
	IN				mlnx_hca_t				*	p_hca, 
	IN				ci_async_event_cb_t			async_cb_p,
	IN		const	void* const					ib_context);

void
mlnx_reset_cb(
	IN				mlnx_hca_t				*	p_hca);

void
from_hca_cap(
	IN				struct ib_device *ib_dev,
	IN				struct ib_device_attr *hca_info_p,
	IN				struct ib_port_attr  *hca_ports,
	OUT			ib_ca_attr_t 				*ca_attr_p);

ib_api_status_t
mlnx_local_mad (
	IN		const	ib_ca_handle_t				h_ca,
	IN		const	uint8_t						port_num,
	IN		const	ib_av_attr_t				*p_src_av_attr,
	IN		const	ib_mad_t					*p_mad_in,
		OUT			ib_mad_t					*p_mad_out );

ib_api_status_t
fw_access_ctrl(
	IN		const	void*						context,
	IN		const	void** const				handle_array	OPTIONAL,
	IN				uint32_t					num_handles,
	IN				ib_ci_op_t* const			p_ci_op,
	IN	OUT			ci_umv_buf_t				*p_umv_buf		OPTIONAL);

void unmap_crspace_for_all( struct ib_ucontext *p_context );

void ca_event_handler(struct ib_event *ev, void *context);

ib_api_status_t
to_qp_attr(
	IN	 const	struct ib_qp *ib_qp_p,
	IN				ib_qp_type_t	qp_type,
	IN	 const	ib_qp_mod_t *modify_attr_p,		
	OUT 	struct ib_qp_attr *qp_attr_p,
	OUT 	int *qp_attr_mask_p
	);

ib_api_status_t
from_qp_attr(
	IN	 const	struct ib_qp 	*p_ib_qp,
	IN 	struct ib_qp_attr 		*p_ib_qp_attr,
	OUT 	ib_qp_attr_t		*p_qp_attr
	);

enum ib_qp_type to_qp_type(ib_qp_type_t qp_type);

ib_qp_type_t from_qp_type(enum ib_qp_type ib_qp_type);

int
to_av(
	IN		const	struct ib_device 	*p_ib_dev,
	IN		const	ib_av_attr_t		*p_ib_av_attr,
	OUT			struct ib_ah_attr		*p_ib_ah_attr);

int from_av(
	IN		const	struct ib_device 	*p_ib_dev,
	IN 			struct ib_qp_attr 		*p_ib_qp_attr,
	IN			struct ib_ah_attr		*p_ib_ah_attr,
	OUT				ib_av_attr_t		*p_ib_av_attr);

enum ib_access_flags
to_qp_acl(
	IN				ib_access_t					ibal_acl);

static inline int from_umv_buf(void *dest, ci_umv_buf_t* const p_umv_buf, size_t len)
{
	RtlCopyMemory(dest, (void*)(ULONG_PTR)p_umv_buf->p_inout_buf,  len);
	return 0;
}

static inline int to_umv_buf(ci_umv_buf_t* const p_umv_buf, void *src, size_t len)
{
	if (p_umv_buf->output_size < len) {
		p_umv_buf->status = IB_INSUFFICIENT_MEMORY;
		p_umv_buf->output_size = 0;
		return -EFAULT;
	}
	RtlCopyMemory( (void*)(ULONG_PTR)p_umv_buf->p_inout_buf,  src, len);
	p_umv_buf->status = IB_SUCCESS;
	p_umv_buf->output_size = (uint32_t)len;
	return 0;
}


/* interface */

void
mlnx_ca_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_pd_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_av_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_cq_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_qp_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_srq_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_mr_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_direct_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_mcast_if(
	IN	OUT			ci_interface_t				*p_interface );

void
mlnx_mr_if_livefish(
	IN	OUT			ci_interface_t				*p_interface );


