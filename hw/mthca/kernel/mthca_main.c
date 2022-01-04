/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_main.tmh"
#endif
#include "mthca_config_reg.h"
#include "mthca_cmd.h"
#include "mthca_profile.h"
#include "mthca_memfree.h"

static const char mthca_version[] =
	DRV_NAME ": HCA Driver v"
	DRV_VERSION " (" DRV_RELDATE ")";

static struct mthca_profile default_profile = {
	1 << 16,		// num_qp
	4,					// rdb_per_qp
	0, 				// num_srq
	1 << 16,		// num_cq
	1 << 13,		// num_mcg
	1 << 17,		// num_mpt
	1 << 20,		// num_mtt
	1 << 15,		// num_udav (Tavor only)
	0,					// num_uar
	1 << 18,		// uarc_size (Arbel only)
	1 << 18,		// fmr_reserved_mtts (Tavor only)
};

/* Types of supported HCA */
enum __hca_type {
	TAVOR,			/* MT23108                        */
	ARBEL_COMPAT,		/* MT25208 in Tavor compat mode   */
	ARBEL_NATIVE,		/* MT25218 with extended features */
	SINAI			/* MT25204 */
};

#define MTHCA_FW_VER(major, minor, subminor) \
	(((u64) (major) << 32) | ((u64) (minor) << 16) | (u64) (subminor))

static struct {
	u64 max_unsupported_fw;
	u64 min_supported_fw;
	int is_memfree;
	int is_pcie;
} mthca_hca_table[] = {
	{ MTHCA_FW_VER(3, 3, 2), MTHCA_FW_VER(3, 5, 0), 0, 0 },	/* TAVOR */
	{ MTHCA_FW_VER(4, 7, 0), MTHCA_FW_VER(4, 8, 200), 0, 1 },	/* ARBEL_COMPAT */
	{ MTHCA_FW_VER(5, 1, 0), MTHCA_FW_VER(5, 3, 0), 1, 1 },	/* ARBEL_NATIVE */
	{ MTHCA_FW_VER(1, 0, 800), MTHCA_FW_VER(1, 2, 0), 1, 1 }	/* SINAI */
};


#define HCA(v, d, t) \
	{ PCI_VENDOR_ID_##v,	PCI_DEVICE_ID_MELLANOX_##d, t }

static struct pci_device_id {
	unsigned		vendor;
	unsigned		device;
	enum __hca_type	driver_data;
} mthca_pci_table[] = {
	HCA(MELLANOX, TAVOR,	    TAVOR),
	HCA(MELLANOX, ARBEL_COMPAT, ARBEL_COMPAT),
	HCA(MELLANOX, ARBEL,	    ARBEL_NATIVE),
	HCA(MELLANOX, SINAI_OLD,    SINAI),
	HCA(MELLANOX, SINAI,	    SINAI),
	HCA(TOPSPIN,  TAVOR,	    TAVOR),
	HCA(TOPSPIN,  ARBEL_COMPAT, TAVOR),
	HCA(TOPSPIN,  ARBEL,	    ARBEL_NATIVE),
	HCA(TOPSPIN,  SINAI_OLD,    SINAI),
	HCA(TOPSPIN,  SINAI,	    SINAI),
};
#define MTHCA_PCI_TABLE_SIZE (sizeof(mthca_pci_table)/sizeof(struct pci_device_id))

// wrapper to driver's hca_tune_pci
static NTSTATUS mthca_tune_pci(struct mthca_dev *mdev)
{
	PDEVICE_OBJECT pdo = mdev->ext->cl_ext.p_self_do;
	return hca_tune_pci(pdo, &mdev->uplink_info);
}

int mthca_get_dev_info(struct mthca_dev *mdev, __be64 *node_guid, u32 *hw_id)
{
	struct ib_device_attr props;
	struct ib_device *ib_dev = &mdev->ib_dev;
	int err = (ib_dev->query_device )(ib_dev, &props );

	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("can't get guid - mthca_query_port() failed (%08X)\n", err ));
		return err;
	}

	//TODO: do we need to convert GUID to LE by cl_ntoh64(x) ?
	*node_guid = ib_dev->node_guid;
	*hw_id = props.hw_ver;
	return 0;
}

static struct pci_device_id * mthca_find_pci_dev(unsigned ven_id, unsigned dev_id)
{
	struct pci_device_id *p_id = mthca_pci_table;
	int i;

	// find p_id (appropriate line in mthca_pci_table)
	for (i = 0; i < MTHCA_PCI_TABLE_SIZE; ++i, ++p_id) {
		if (p_id->device == dev_id && p_id->vendor ==  ven_id)
			return p_id;
	}
	return NULL;
}


static int  mthca_dev_lim(struct mthca_dev *mdev, struct mthca_dev_lim *dev_lim)
{
	int err;
	u8 status;

	err = mthca_QUERY_DEV_LIM(mdev, dev_lim, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_DEV_LIM command failed, aborting.\n"));
		return err;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("QUERY_DEV_LIM returned status 0x%02x, "
			  "aborting.\n", status));
		return -EINVAL;
	}
	if (dev_lim->min_page_sz > PAGE_SIZE) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("HCA minimum page size of %d bigger than "
			  "kernel PAGE_SIZE of %ld, aborting.\n",
			  dev_lim->min_page_sz, PAGE_SIZE));
		return -ENODEV;
	}
	if (dev_lim->num_ports > MTHCA_MAX_PORTS) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("HCA has %d ports, but we only support %d, "
			  "aborting.\n",
			  dev_lim->num_ports, MTHCA_MAX_PORTS));
		return -ENODEV;
	}

	if (dev_lim->uar_size > (int)pci_resource_len(mdev, HCA_BAR_TYPE_UAR)) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW , ("HCA reported UAR size of 0x%x bigger than "
			  "Bar%d size of 0x%lx, aborting.\n",
			  dev_lim->uar_size, HCA_BAR_TYPE_UAR, 
			  (unsigned long)pci_resource_len(mdev, HCA_BAR_TYPE_UAR)));
		return -ENODEV;
	}
	

	mdev->limits.num_ports      	= dev_lim->num_ports;
	mdev->limits.vl_cap             = dev_lim->max_vl;
	mdev->limits.mtu_cap            = dev_lim->max_mtu;
	mdev->limits.gid_table_len  	= dev_lim->max_gids;
	mdev->limits.pkey_table_len 	= dev_lim->max_pkeys;
	mdev->limits.local_ca_ack_delay = dev_lim->local_ca_ack_delay;
	mdev->limits.max_sg             = dev_lim->max_sg;
	mdev->limits.max_wqes           = dev_lim->max_qp_sz;
	mdev->limits.max_qp_init_rdma   = dev_lim->max_requester_per_qp;
	mdev->limits.reserved_qps       = dev_lim->reserved_qps;
	mdev->limits.max_srq_wqes       = dev_lim->max_srq_sz;
	mdev->limits.reserved_srqs      = dev_lim->reserved_srqs;
	mdev->limits.reserved_eecs      = dev_lim->reserved_eecs;
	mdev->limits.max_desc_sz      = dev_lim->max_desc_sz;
	mdev->limits.max_srq_sge	= mthca_max_srq_sge(mdev);
	/*
	 * Subtract 1 from the limit because we need to allocate a
	 * spare CQE so the HCA HW can tell the difference between an
	 * empty CQ and a full CQ.
	 */
	mdev->limits.max_cqes           = dev_lim->max_cq_sz - 1;
	mdev->limits.reserved_cqs       = dev_lim->reserved_cqs;
	mdev->limits.reserved_eqs       = dev_lim->reserved_eqs;
	mdev->limits.reserved_mtts      = dev_lim->reserved_mtts;
	mdev->limits.reserved_mrws      = dev_lim->reserved_mrws;
	mdev->limits.reserved_uars      = dev_lim->reserved_uars;
	mdev->limits.reserved_pds       = dev_lim->reserved_pds;
	mdev->limits.port_width_cap     = (u8)dev_lim->max_port_width;
	mdev->limits.page_size_cap     	= !(u32)(dev_lim->min_page_sz - 1);
	mdev->limits.flags     				= dev_lim->flags;
	mdev->limits.num_avs = mthca_is_memfree(mdev) ? 0 : dev_lim->hca.tavor.max_avs;

	/* IB_DEVICE_RESIZE_MAX_WR not supported by driver.
	   May be doable since hardware supports it for SRQ.

	   IB_DEVICE_N_NOTIFY_CQ is supported by hardware but not by driver.

	   IB_DEVICE_SRQ_RESIZE is supported by hardware but SRQ is not
	   supported by driver. */
	mdev->device_cap_flags = IB_DEVICE_CHANGE_PHY_PORT |
		IB_DEVICE_PORT_ACTIVE_EVENT |
		IB_DEVICE_SYS_IMAGE_GUID |
		IB_DEVICE_RC_RNR_NAK_GEN;

	if (dev_lim->flags & DEV_LIM_FLAG_BAD_PKEY_CNTR)
		mdev->device_cap_flags |= IB_DEVICE_BAD_PKEY_CNTR;

	if (dev_lim->flags & DEV_LIM_FLAG_BAD_QKEY_CNTR)
		mdev->device_cap_flags |= IB_DEVICE_BAD_QKEY_CNTR;

	if (dev_lim->flags & DEV_LIM_FLAG_RAW_MULTI)
		mdev->device_cap_flags |= IB_DEVICE_RAW_MULTI;

	if (dev_lim->flags & DEV_LIM_FLAG_AUTO_PATH_MIG)
		mdev->device_cap_flags |= IB_DEVICE_AUTO_PATH_MIG;

	if (dev_lim->flags & DEV_LIM_FLAG_UD_AV_PORT_ENFORCE)
		mdev->device_cap_flags |= IB_DEVICE_UD_AV_PORT_ENFORCE;

	if (dev_lim->flags & DEV_LIM_FLAG_SRQ)
		mdev->mthca_flags |= MTHCA_FLAG_SRQ;
	
	if (mthca_is_memfree(mdev)) {
		if (dev_lim->flags & DEV_LIM_FLAG_IPOIB_CSUM)
			mdev->device_cap_flags |= IB_DEVICE_IPOIB_CSUM; //IB_DEVICE_UD_IP_CSUM;
	}

	return 0;
}

static int  mthca_init_tavor(struct mthca_dev *mdev)
{
	u8 status;
	int err;
	struct mthca_dev_lim        dev_lim;
	struct mthca_profile        profile;
	struct mthca_init_hca_param init_hca;

	err = mthca_SYS_EN(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("SYS_EN command failed, aborting.\n"));
		return err;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("SYS_EN returned status 0x%02x, "
			  "aborting.\n", status));
		return -EINVAL;
	}

	err = mthca_QUERY_FW(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_FW command failed, aborting.\n"));
		goto err_disable;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("QUERY_FW returned status 0x%02x, "
			  "aborting.\n", status));
		err = -EINVAL;
		goto err_disable;
	}
	err = mthca_QUERY_DDR(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_DDR command failed, aborting.\n"));
		goto err_disable;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,( "QUERY_DDR returned status 0x%02x, "
			  "aborting.\n", status));
		err = -EINVAL;
		goto err_disable;
	}

	err = mthca_dev_lim(mdev, &dev_lim);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,( "QUERY_DEV_LIM command failed, aborting.\n"));
		goto err_disable;
	}

	profile = default_profile;
	profile.num_uar   = dev_lim.uar_size / PAGE_SIZE;
	profile.uarc_size = 0;

	/* correct default profile */
	if ( g_profile_qp_num != 0 ) 
		profile.num_qp = g_profile_qp_num;
		
	if ( g_profile_rd_out != 0xffffffff )
		profile.rdb_per_qp = g_profile_rd_out;

	if (mdev->mthca_flags & MTHCA_FLAG_SRQ)
		profile.num_srq = dev_lim.max_srqs;

	err = (int)mthca_make_profile(mdev, &profile, &dev_lim, &init_hca);
	if (err < 0)
		goto err_disable;

	err = (int)mthca_INIT_HCA(mdev, &init_hca, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("INIT_HCA command failed, aborting.\n"));
		goto err_disable;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("INIT_HCA returned status 0x%02x, "
			  "aborting.\n", status));
		err = -EINVAL;
		goto err_disable;
	}

	return 0;

err_disable:
	mthca_SYS_DIS(mdev, &status);

	return err;
}

static int  mthca_load_fw(struct mthca_dev *mdev)
{
	u8 status;
	int err;

	/* FIXME: use HCA-attached memory for FW if present */

	mdev->fw.arbel.fw_icm =
		mthca_alloc_icm(mdev, mdev->fw.arbel.fw_pages,
				GFP_HIGHUSER | __GFP_NOWARN);
	if (!mdev->fw.arbel.fw_icm) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Couldn't allocate FW area, aborting.\n"));
		return -ENOMEM;
	}

	err = mthca_MAP_FA(mdev, mdev->fw.arbel.fw_icm, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MAP_FA command failed, aborting.\n"));
		goto err_free;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MAP_FA returned status 0x%02x, aborting.\n", status));
		err = -EINVAL;
		goto err_free;
	}
	err = mthca_RUN_FW(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("RUN_FW command failed, aborting.\n"));
		goto err_unmap_fa;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("RUN_FW returned status 0x%02x, aborting.\n", status));
		err = -EINVAL;
		goto err_unmap_fa;
	}

	return 0;

err_unmap_fa:
	mthca_UNMAP_FA(mdev, &status);

err_free:
	mthca_free_icm(mdev, mdev->fw.arbel.fw_icm);
	return err;
}

static int  mthca_init_icm(struct mthca_dev *mdev,
				    struct mthca_dev_lim *dev_lim,
				    struct mthca_init_hca_param *init_hca,
				    u64 icm_size)
{
	u64 aux_pages;
	u8 status;
	int err;

	err = mthca_SET_ICM_SIZE(mdev, icm_size, &aux_pages, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("SET_ICM_SIZE command failed, aborting.\n"));
		return err;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("SET_ICM_SIZE returned status 0x%02x, "
			  "aborting.\n", status));
		return -EINVAL;
	}

	HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_LOW , ("%I64d KB of HCA context requires %I64d KB aux memory.\n",
		  (u64) icm_size >> 10,
		  (u64) aux_pages << 2));

	mdev->fw.arbel.aux_icm = mthca_alloc_icm(mdev, (int)aux_pages,
						 GFP_HIGHUSER | __GFP_NOWARN);
	if (!mdev->fw.arbel.aux_icm) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Couldn't allocate aux memory, aborting.\n"));
		return -ENOMEM;
	}

	err = mthca_MAP_ICM_AUX(mdev, mdev->fw.arbel.aux_icm, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MAP_ICM_AUX command failed, aborting.\n"));
		goto err_free_aux;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("MAP_ICM_AUX returned status 0x%02x, aborting.\n", status));
		err = -EINVAL;
		goto err_free_aux;
	}

	err = mthca_map_eq_icm(mdev, init_hca->eqc_base);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map EQ context memory, aborting.\n"));
		goto err_unmap_aux;
	}

	/* CPU writes to non-reserved MTTs, while HCA might DMA to reserved mtts */
	mdev->limits.reserved_mtts = ALIGN(mdev->limits.reserved_mtts * MTHCA_MTT_SEG_SIZE,
							 dma_get_cache_alignment()) / MTHCA_MTT_SEG_SIZE;
	
	mdev->mr_table.mtt_table = mthca_alloc_icm_table(mdev, init_hca->mtt_base,
							 MTHCA_MTT_SEG_SIZE,
							 mdev->limits.num_mtt_segs,
							 mdev->limits.reserved_mtts, 1);
	if (!mdev->mr_table.mtt_table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map MTT context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_eq;
	}

	mdev->mr_table.mpt_table = mthca_alloc_icm_table(mdev, init_hca->mpt_base,
							 dev_lim->mpt_entry_sz,
							 mdev->limits.num_mpts,
							 mdev->limits.reserved_mrws, 1);
	if (!mdev->mr_table.mpt_table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map MPT context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_mtt;
	}

	mdev->qp_table.qp_table = mthca_alloc_icm_table(mdev, init_hca->qpc_base,
							dev_lim->qpc_entry_sz,
							mdev->limits.num_qps,
							mdev->limits.reserved_qps, 0);
	if (!mdev->qp_table.qp_table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map QP context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_mpt;
	}

	mdev->qp_table.eqp_table = mthca_alloc_icm_table(mdev, init_hca->eqpc_base,
							 dev_lim->eqpc_entry_sz,
							 mdev->limits.num_qps,
							 mdev->limits.reserved_qps, 0);
	if (!mdev->qp_table.eqp_table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map EQP context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_qp;
	}

	mdev->qp_table.rdb_table = mthca_alloc_icm_table(mdev, init_hca->rdb_base,
							 MTHCA_RDB_ENTRY_SIZE,
							 mdev->limits.num_qps <<
							 mdev->qp_table.rdb_shift,
							 0, 0);
	if (!mdev->qp_table.rdb_table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map RDB context memory, aborting\n"));
		err = -ENOMEM;
		goto err_unmap_eqp;
	}

       mdev->cq_table.table = mthca_alloc_icm_table(mdev, init_hca->cqc_base,
						    dev_lim->cqc_entry_sz,
						    mdev->limits.num_cqs,
						    mdev->limits.reserved_cqs, 0);
	if (!mdev->cq_table.table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map CQ context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_rdb;
	}

	if (mdev->mthca_flags & MTHCA_FLAG_SRQ) {
		mdev->srq_table.table =
			mthca_alloc_icm_table(mdev, init_hca->srqc_base,
					      dev_lim->srq_entry_sz,
					      mdev->limits.num_srqs,
					      mdev->limits.reserved_srqs, 0);
		if (!mdev->srq_table.table) {
			HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("Failed to map SRQ context memory, "
				  "aborting.\n"));
			err = -ENOMEM;
			goto err_unmap_cq;
		}
	}

	/*
	 * It's not strictly required, but for simplicity just map the
	 * whole multicast group table now.  The table isn't very big
	 * and it's a lot easier than trying to track ref counts.
	 */
	mdev->mcg_table.table = mthca_alloc_icm_table(mdev, init_hca->mc_base,
						      MTHCA_MGM_ENTRY_SIZE,
						      mdev->limits.num_mgms +
						      mdev->limits.num_amgms,
						      mdev->limits.num_mgms +
						      mdev->limits.num_amgms,
						      0);
	if (!mdev->mcg_table.table) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to map MCG context memory, aborting.\n"));
		err = -ENOMEM;
		goto err_unmap_srq;
	}

	return 0;

err_unmap_srq:
	if (mdev->mthca_flags & MTHCA_FLAG_SRQ)
		mthca_free_icm_table(mdev, mdev->srq_table.table);

err_unmap_cq:
	mthca_free_icm_table(mdev, mdev->cq_table.table);

err_unmap_rdb:
	mthca_free_icm_table(mdev, mdev->qp_table.rdb_table);

err_unmap_eqp:
	mthca_free_icm_table(mdev, mdev->qp_table.eqp_table);

err_unmap_qp:
	mthca_free_icm_table(mdev, mdev->qp_table.qp_table);

err_unmap_mpt:
	mthca_free_icm_table(mdev, mdev->mr_table.mpt_table);

err_unmap_mtt:
	mthca_free_icm_table(mdev, mdev->mr_table.mtt_table);

err_unmap_eq:
	mthca_unmap_eq_icm(mdev);

err_unmap_aux:
	mthca_UNMAP_ICM_AUX(mdev, &status);

err_free_aux:
	mthca_free_icm(mdev, mdev->fw.arbel.aux_icm);

	return err;
}

static int  mthca_init_arbel(struct mthca_dev *mdev)
{
	struct mthca_dev_lim        dev_lim;
	struct mthca_profile        profile;
	struct mthca_init_hca_param init_hca;
	u64 icm_size;
	u8 status;
	int err;

	err = mthca_QUERY_FW(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_FW command failed, aborting.\n"));
		return err;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("QUERY_FW returned status 0x%02x, "
			  "aborting.\n", status));
		return -EINVAL;
	}

	err = mthca_ENABLE_LAM(mdev, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("ENABLE_LAM command failed, aborting.\n"));
		return err;
	}
	if (status == MTHCA_CMD_STAT_LAM_NOT_PRE) {
		HCA_PRINT(TRACE_LEVEL_INFORMATION   ,HCA_DBG_LOW   ,("No HCA-attached memory (running in MemFree mode)\n"));
		mdev->mthca_flags |= MTHCA_FLAG_NO_LAM;
	} else if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("ENABLE_LAM returned status 0x%02x, "
			  "aborting.\n", status));
		return -EINVAL;
	}

	err = mthca_load_fw(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to start FW, aborting.\n"));
		goto err_disable;
	}

	err = mthca_dev_lim(mdev, &dev_lim);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_DEV_LIM command failed, aborting.\n"));
		goto err_stop_fw;
	}

	profile = default_profile;
	profile.num_uar  = dev_lim.uar_size / PAGE_SIZE;
	profile.num_udav = 0;
	if (mdev->mthca_flags & MTHCA_FLAG_SRQ)
		profile.num_srq = dev_lim.max_srqs;

	/* correct default profile */
	if ( g_profile_qp_num != 0 ) 
		profile.num_qp = g_profile_qp_num;
		
	if ( g_profile_rd_out != 0xffffffff )
		profile.rdb_per_qp = g_profile_rd_out;

	RtlZeroMemory( &init_hca, sizeof(init_hca));
	icm_size = mthca_make_profile(mdev, &profile, &dev_lim, &init_hca);
	if ((int) icm_size < 0) {
		err = (int)icm_size;
		goto err_stop_fw;
	}

	err = mthca_init_icm(mdev, &dev_lim, &init_hca, icm_size);
	if (err)
		goto err_stop_fw;

	err = mthca_INIT_HCA(mdev, &init_hca, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("INIT_HCA command failed, aborting.\n"));
		goto err_free_icm;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("INIT_HCA returned status 0x%02x, "
			  "aborting.\n", status));
		err = -EINVAL;
		goto err_free_icm;
	}

	return 0;

err_free_icm:
	if (mdev->mthca_flags & MTHCA_FLAG_SRQ)
		mthca_free_icm_table(mdev, mdev->srq_table.table);
	mthca_free_icm_table(mdev, mdev->cq_table.table);
	mthca_free_icm_table(mdev, mdev->qp_table.rdb_table);
	mthca_free_icm_table(mdev, mdev->qp_table.eqp_table);
	mthca_free_icm_table(mdev, mdev->qp_table.qp_table);
	mthca_free_icm_table(mdev, mdev->mr_table.mpt_table);
	mthca_free_icm_table(mdev, mdev->mr_table.mtt_table);
	mthca_unmap_eq_icm(mdev);

	mthca_UNMAP_ICM_AUX(mdev, &status);
	mthca_free_icm(mdev, mdev->fw.arbel.aux_icm);

err_stop_fw:
	mthca_UNMAP_FA(mdev, &status);
	mthca_free_icm(mdev, mdev->fw.arbel.fw_icm);

err_disable:
	if (!(mdev->mthca_flags & MTHCA_FLAG_NO_LAM))
		mthca_DISABLE_LAM(mdev, &status);

	return err;
}

static void mthca_close_hca(struct mthca_dev *mdev)
{
	u8 status;

	mthca_CLOSE_HCA(mdev, 0, &status);

	if (mthca_is_memfree(mdev)) {
		if (mdev->mthca_flags & MTHCA_FLAG_SRQ)
			mthca_free_icm_table(mdev, mdev->srq_table.table);
		mthca_free_icm_table(mdev, mdev->cq_table.table);
		mthca_free_icm_table(mdev, mdev->qp_table.rdb_table);
		mthca_free_icm_table(mdev, mdev->qp_table.eqp_table);
		mthca_free_icm_table(mdev, mdev->qp_table.qp_table);
		mthca_free_icm_table(mdev, mdev->mr_table.mpt_table);
		mthca_free_icm_table(mdev, mdev->mr_table.mtt_table);
		mthca_free_icm_table(mdev, mdev->mcg_table.table);
		mthca_unmap_eq_icm(mdev);

		mthca_UNMAP_ICM_AUX(mdev, &status);
		mthca_free_icm(mdev, mdev->fw.arbel.aux_icm);

		mthca_UNMAP_FA(mdev, &status);
		mthca_free_icm(mdev, mdev->fw.arbel.fw_icm);

		if (!(mdev->mthca_flags & MTHCA_FLAG_NO_LAM))
			mthca_DISABLE_LAM(mdev, &status);
	} else
		mthca_SYS_DIS(mdev, &status);
}

static int  mthca_init_hca(struct mthca_dev *mdev)
{
	u8 status;
	int err;
	struct mthca_adapter adapter;

	if (mthca_is_memfree(mdev))
		err = mthca_init_arbel(mdev);
	else
		err = mthca_init_tavor(mdev);

	if (err)
		return err;

	err = mthca_QUERY_ADAPTER(mdev, &adapter, &status);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("QUERY_ADAPTER command failed, aborting.\n"));
		goto err_close;
	}
	if (status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("QUERY_ADAPTER returned status 0x%02x, "
			  "aborting.\n", status));
		err = -EINVAL;
		goto err_close;
	}

	mdev->eq_table.inta_pin = adapter.inta_pin;
	mdev->rev_id            = adapter.revision_id;
	memcpy(mdev->board_id, adapter.board_id, sizeof mdev->board_id);

	return 0;

err_close:
	mthca_close_hca(mdev);
	return err;
}

static int  mthca_setup_hca(struct mthca_dev *mdev)
{
	int err;
	u8 status;

	MTHCA_INIT_DOORBELL_LOCK(&mdev->doorbell_lock);

	err = mthca_init_uar_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "user access region table, aborting.\n"));
		return err;
	}

	err = mthca_uar_alloc(mdev, &mdev->driver_uar);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to allocate driver access region, "
			  "aborting.\n"));
		goto err_uar_table_free;
	}

	mdev->kar = ioremap((io_addr_t)mdev->driver_uar.pfn << PAGE_SHIFT, PAGE_SIZE,&mdev->kar_size);
	if (!mdev->kar) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Couldn't map kernel access region, "
			  "aborting.\n"));
		err = -ENOMEM;
		goto err_uar_free;
	}

	err = mthca_init_pd_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "protection domain table, aborting.\n"));
		goto err_kar_unmap;
	}

	err = mthca_init_mr_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "memory region table, aborting.\n"));
		goto err_pd_table_free;
	}

	err = mthca_pd_alloc(mdev, 1, &mdev->driver_pd);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to create driver PD, "
			  "aborting.\n"));
		goto err_mr_table_free;
	}

	err = mthca_init_eq_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW, ("Failed to initialize "
			  "event queue table, aborting.\n"));
		goto err_pd_free;
	}

	err = mthca_cmd_use_events(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to switch to event-driven "
			  "firmware commands, aborting.\n"));
		goto err_eq_table_free;
	}

	err = mthca_NOP(mdev, &status);
	if (err || status) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("NOP command failed to generate interrupt, aborting.\n"));
		if (mdev->mthca_flags & (MTHCA_FLAG_MSI | MTHCA_FLAG_MSI_X)){
			HCA_PRINT_EV(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("Try again with MSI/MSI-X disabled.\n"));
		}else{
			HCA_PRINT_EV(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("BIOS or ACPI interrupt routing problem?\n"));
		}

		goto err_cmd_poll;
	}

	HCA_PRINT(TRACE_LEVEL_VERBOSE  ,HCA_DBG_LOW  ,("NOP command IRQ test passed\n"));

	err = mthca_init_cq_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "completion queue table, aborting.\n"));
		goto err_cmd_poll;
	}

	err = mthca_init_srq_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "shared receive queue table, aborting.\n"));
		goto err_cq_table_free;
	}

	err = mthca_init_qp_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW, ("Failed to initialize "
			  "queue pair table, aborting.\n"));
		goto err_srq_table_free;
	}

	err = mthca_init_av_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "address vector table, aborting.\n"));
		goto err_qp_table_free;
	}

	err = mthca_init_mcg_table(mdev);
	if (err) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("Failed to initialize "
			  "multicast group table, aborting.\n"));
		goto err_av_table_free;
	}

	return 0;

err_av_table_free:
	mthca_cleanup_av_table(mdev);

err_qp_table_free:
	mthca_cleanup_qp_table(mdev);

err_srq_table_free:
	mthca_cleanup_srq_table(mdev);

err_cq_table_free:
	mthca_cleanup_cq_table(mdev);

err_cmd_poll:
	mthca_cmd_use_polling(mdev);

err_eq_table_free:
	mthca_cleanup_eq_table(mdev);

err_pd_free:
	mthca_pd_free(mdev, &mdev->driver_pd);

err_mr_table_free:
	mthca_cleanup_mr_table(mdev);

err_pd_table_free:
	mthca_cleanup_pd_table(mdev);

err_kar_unmap:
	iounmap(mdev->kar, mdev->kar_size);

err_uar_free:
	mthca_uar_free(mdev, &mdev->driver_uar);

err_uar_table_free:
	mthca_cleanup_uar_table(mdev);
	return err;
}


static int 	mthca_check_fw(struct mthca_dev *mdev, struct pci_device_id *p_id)
{
	int err = 0;
	
	if (mdev->fw_ver < mthca_hca_table[p_id->driver_data].max_unsupported_fw) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("HCA FW version %d.%d.%d is not supported. Use %d.%d.%d or higher.\n",
			   (int) (mdev->fw_ver >> 32), (int) (mdev->fw_ver >> 16) & 0xffff,
			   (int) (mdev->fw_ver & 0xffff),
			   (int) (mthca_hca_table[p_id->driver_data].max_unsupported_fw >> 32),
			   (int) (mthca_hca_table[p_id->driver_data].max_unsupported_fw >> 16) & 0xffff,
			   (int) (mthca_hca_table[p_id->driver_data].max_unsupported_fw & 0xffff)));
		err = -ENODEV;
	}
	else 
	if (mdev->fw_ver < mthca_hca_table[p_id->driver_data].min_supported_fw) {
		HCA_PRINT_EV(TRACE_LEVEL_WARNING ,HCA_DBG_LOW ,
			("The HCA FW version is %d.%d.%d, which is not the latest one. \n"
			"If you meet any issues with the HCA please first try to upgrade the FW to version %d.%d.%d or higher.\n",
				 (int) (mdev->fw_ver >> 32), (int) (mdev->fw_ver >> 16) & 0xffff,
				 (int) (mdev->fw_ver & 0xffff),
				 (int) (mthca_hca_table[p_id->driver_data].min_supported_fw >> 32),
				 (int) (mthca_hca_table[p_id->driver_data].min_supported_fw >> 16) & 0xffff,
				 (int) (mthca_hca_table[p_id->driver_data].min_supported_fw & 0xffff)));
	}
	else {
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_LOW ,("Current HCA FW version is %d.%d.%d. \n",
				 (int) (mdev->fw_ver >> 32), (int) (mdev->fw_ver >> 16) & 0xffff,
				 (int) (mdev->fw_ver & 0xffff)));
	}

	return err;
}

NTSTATUS mthca_init_one(hca_dev_ext_t *ext)
{
	static int mthca_version_printed = 0;
	int err;
	NTSTATUS status;
	struct mthca_dev *mdev;
	struct pci_device_id *p_id;

	/* print version */
	if (!mthca_version_printed) {
		HCA_PRINT(TRACE_LEVEL_INFORMATION ,HCA_DBG_LOW ,("%s\n", mthca_version));
		++mthca_version_printed;
	}

	/* find the type of device */
	p_id = mthca_find_pci_dev(
		(unsigned)ext->hcaConfig.VendorID,
		(unsigned)ext->hcaConfig.DeviceID);
	if (p_id == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto end;
	}

	InitializeListHead(&ext->hca.hob.event_list);
	KeInitializeSpinLock(&ext->hca.hob.event_list_lock);
	ext->hca.hob.mark = E_MARK_CA;

	/* allocate mdev structure */
	mdev = kzalloc(sizeof *mdev, GFP_KERNEL);
	if (!mdev) {
		// can't use HCA_PRINT_EV here !
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_LOW ,("Device struct alloc failed, "
			"aborting.\n"));
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto end;
	}
	 
	/* set some fields */
	mdev->ext = ext;		/* pointer to DEVICE OBJECT extension */
	mdev->hca_type = p_id->driver_data;
	mdev->ib_dev.mdev = mdev;
	if (ext->hca_hidden)
		mdev->mthca_flags |= MTHCA_FLAG_DDR_HIDDEN;
	if (mthca_hca_table[p_id->driver_data].is_memfree)
		mdev->mthca_flags |= MTHCA_FLAG_MEMFREE;
	if (mthca_hca_table[p_id->driver_data].is_pcie)
		mdev->mthca_flags |= MTHCA_FLAG_PCIE;
	
//TODO: after we have a FW, capable of reset, 
// write a routine, that only presses the button

	/*
	 * Now reset the HCA before we touch the PCI capabilities or
	 * attempt a firmware command, since a boot ROM may have left
	 * the HCA in an undefined state.
	 */
	status = hca_reset( mdev->ext->cl_ext.p_self_do, p_id->driver_data == TAVOR );
	if ( !NT_SUCCESS( status ) ) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to reset HCA, aborting.\n"));
		goto err_free_dev;
	}

	if (mthca_cmd_init(mdev)) {
		HCA_PRINT_EV(TRACE_LEVEL_ERROR   ,HCA_DBG_LOW   ,("Failed to init command interface, aborting.\n"));
		status = STATUS_DEVICE_DATA_ERROR;
		goto err_free_dev;
	}

	status = mthca_tune_pci(mdev);
	if ( !NT_SUCCESS( status ) ) {
		goto err_cmd;
	}

	err = mthca_init_hca(mdev); 
	if (err) {
		status = STATUS_UNSUCCESSFUL;
		goto err_cmd;
	}

	err = mthca_check_fw(mdev, p_id);
	if (err) {
		status = STATUS_UNSUCCESSFUL;
		goto err_close;
	}

	err = mthca_setup_hca(mdev);
	if (err) {
		status = STATUS_UNSUCCESSFUL;
		goto err_close;
	}

	err = mthca_register_device(mdev);
	if (err) {
		status = STATUS_UNSUCCESSFUL;
		goto err_cleanup;
	}

	ext->hca.mdev = mdev;
	mdev->state = MTHCA_DEV_INITIALIZED;
	return 0;

err_cleanup:
	mthca_cleanup_mcg_table(mdev);
	mthca_cleanup_av_table(mdev);
	mthca_cleanup_qp_table(mdev);
	mthca_cleanup_srq_table(mdev);
	mthca_cleanup_cq_table(mdev);
	mthca_cmd_use_polling(mdev);
	mthca_cleanup_eq_table(mdev);

	mthca_pd_free(mdev, &mdev->driver_pd);

	mthca_cleanup_mr_table(mdev);
	mthca_cleanup_pd_table(mdev);
	mthca_cleanup_uar_table(mdev);

err_close:
	mthca_close_hca(mdev);

err_cmd:
	mthca_cmd_cleanup(mdev);

err_free_dev:
	kfree(mdev);

end:
	return status;
}

void mthca_remove_one(hca_dev_ext_t *ext)
{
	struct mthca_dev *mdev = ext->hca.mdev;
	u8 status;
	int p;

	ext->hca.mdev = NULL;
	if (mdev) {
		mdev->state = MTHCA_DEV_UNINITIALIZED;
		mthca_unregister_device(mdev);

		for (p = 1; p <= mdev->limits.num_ports; ++p)
			mthca_CLOSE_IB(mdev, p, &status);

		mthca_cleanup_mcg_table(mdev);
		mthca_cleanup_av_table(mdev);
		mthca_cleanup_qp_table(mdev);
		mthca_cleanup_srq_table(mdev);
		mthca_cleanup_cq_table(mdev);
		mthca_cmd_use_polling(mdev);
		mthca_cleanup_eq_table(mdev);
		mthca_pd_free(mdev, &mdev->driver_pd);
		mthca_cleanup_mr_table(mdev);
		mthca_cleanup_pd_table(mdev);
		iounmap(mdev->kar, mdev->kar_size);
		mthca_uar_free(mdev, &mdev->driver_uar);
		mthca_cleanup_uar_table(mdev);
		mthca_close_hca(mdev);
		mthca_cmd_cleanup(mdev);
		kfree(mdev);
	}
}



