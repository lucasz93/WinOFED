/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corp.  All rights reserved.
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


#ifndef SRP_DESCRIPTORS_H_INCLUDED
#define SRP_DESCRIPTORS_H_INCLUDED


#include <iba/ib_al.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>

#include "srp_connection.h"
#include "srp_data.h"
#include "srp_debug.h"

/*
 * Number of data segments.
 */
#define SRP_NUM_SGE     1

typedef struct _srp_session   *p_srp_session_t;
typedef struct _srp_conn_info *p_srp_conn_info_t;

/* SRP SCSI request block extension. */
typedef struct _srp_send_descriptor
{
	/* Leave this as first member variable */
	cl_list_item_t				list_item;
	ib_send_wr_t				wr;
	uint64_t					tag;
	SCSI_REQUEST_BLOCK		*p_srb;
	mlnx_fmr_pool_el_t		p_fmr_el;
	ib_local_ds_t				ds[SRP_NUM_SGE];
	/* must be the last*/
	uint8_t					data_segment[SRP_MAX_IU_SIZE];
}srp_send_descriptor_t;

typedef struct _srp_recv_descriptor
{
	ib_recv_wr_t			wr;
	ib_local_ds_t			ds[SRP_NUM_SGE];
	uint8_t				*p_data_segment;
}srp_recv_descriptor_t;

typedef struct _srp_descriptors
{
	BOOLEAN                 initialized;

	cl_spinlock_t           sent_list_lock;
	cl_qlist_t              sent_descriptors;
	cl_spinlock_t           pending_list_lock;
	cl_qlist_t              pending_descriptors;

	uint32_t                recv_descriptor_count;
	srp_recv_descriptor_t   *p_recv_descriptors_array;

	uint32_t                recv_data_segment_size;
	void                    *p_recv_data_segments_array;


} srp_descriptors_t;

ib_api_status_t
srp_init_descriptors(
	IN	OUT			srp_descriptors_t			*p_descriptors,
	IN				uint32_t					recv_descriptor_count,
	IN				uint32_t					recv_data_segment_size,
	IN				ib_al_ifc_t* const			p_ifc,
	IN				ib_pd_handle_t				h_pd,
	IN				ib_qp_handle_t				h_qp,
	IN				net32_t						lkey);

ib_api_status_t
srp_destroy_descriptors(
	IN OUT  srp_descriptors_t   *p_descriptors );

srp_send_descriptor_t*
srp_remove_lun_head_send_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      UCHAR                   lun );

srp_send_descriptor_t*
srp_remove_lun_head_pending_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      UCHAR                   lun );

void
srp_add_pending_descriptor(
	IN  srp_descriptors_t       *p_descriptors,
	IN  srp_send_descriptor_t   *p_descriptor );

ib_api_status_t
srp_post_send_descriptor(
	IN				srp_descriptors_t			*p_descriptors,
	IN				srp_send_descriptor_t		*p_descriptor,
	IN				struct _srp_session			*p_session );

srp_send_descriptor_t*
srp_find_matching_send_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      uint64_t                tag ) ;

srp_send_descriptor_t*
srp_find_matching_pending_descriptor(
	IN      srp_descriptors_t       *p_descriptors,
	IN      uint64_t                tag ) ;

void
srp_build_send_descriptor(
	IN      PVOID               p_dev_ext,
	IN OUT  PSCSI_REQUEST_BLOCK p_srb,
	IN      p_srp_conn_info_t   p_srp_conn_info );

#endif /* SRP_DESCRIPTORS_H_INCLUDED */
