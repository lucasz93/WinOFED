/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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

#include <ib_verbs.h>
#include <ib_cache.h>
#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_av.tmh"
#endif


struct mthca_av {
	__be32 port_pd;
	u8     reserved1;
	u8     g_slid;
	__be16 dlid;
	u8     reserved2;
	u8     gid_index;
	u8     msg_sr;
	u8     hop_limit;
	__be32 sl_tclass_flowlabel;
	__be32 dgid[4];
};

int mthca_create_ah(struct mthca_dev *dev,
		    struct mthca_pd *pd,
		    struct ib_ah_attr *ah_attr,
		    struct mthca_ah *ah)
{
	u32 index = (u32)-1;
	struct mthca_av *av = NULL;

	ah->type = MTHCA_AH_PCI_POOL;

	if (mthca_is_memfree(dev)) {
		ah->av   = kmalloc(sizeof *ah->av, GFP_ATOMIC);
		if (!ah->av)
			return -ENOMEM;

		ah->type = MTHCA_AH_KMALLOC;
		av       = ah->av;
	} else if (!atomic_read(&pd->sqp_count) &&
		 !(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		index = mthca_alloc(&dev->av_table.alloc);

		/* fall back to allocate in host memory */
		if (index == -1)
			goto on_hca_fail;

		av = kmalloc(sizeof *av, GFP_ATOMIC);
		if (!av)
			goto on_hca_fail;

		ah->type = MTHCA_AH_ON_HCA;
		ah->avdma  = dev->av_table.ddr_av_base +
			index * MTHCA_AV_SIZE;
	}

on_hca_fail:
	if (ah->type == MTHCA_AH_PCI_POOL) {
		ah->av = pci_pool_alloc(dev->av_table.pool,
					SLAB_ATOMIC, &ah->avdma);
		if (!ah->av)
			return -ENOMEM;

		av = ah->av;
	}
		
	ah->key = pd->ntmr.ibmr.lkey;

	RtlZeroMemory(av, MTHCA_AV_SIZE);

	av->port_pd = cl_hton32(pd->pd_num | (ah_attr->port_num << 24));
	av->g_slid  = ah_attr->src_path_bits;
	av->dlid    = cl_hton16(ah_attr->dlid);
	av->msg_sr  = (3 << 4) | /* 2K message */
		ah_attr->static_rate;
	av->sl_tclass_flowlabel = cl_hton32(ah_attr->sl << 28);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		av->g_slid |= 0x80;
		av->gid_index = (u8)((ah_attr->port_num - 1) * dev->limits.gid_table_len +
			ah_attr->grh.sgid_index);
		av->hop_limit = ah_attr->grh.hop_limit;
		av->sl_tclass_flowlabel |=
			cl_hton32((ah_attr->grh.traffic_class << 20) |
				    ah_attr->grh.flow_label);
		memcpy(av->dgid, ah_attr->grh.dgid.raw, 16);
	} else {
		/* Arbel workaround -- low byte of GID must be 2 */
		av->dgid[3] = cl_hton32(2);
	}

	{ // debug print
		int j;

		HCA_PRINT(TRACE_LEVEL_VERBOSE,HCA_DBG_LOW,("Created UDAV at %p/%08lx:\n",
			  av, (unsigned long) ah->avdma));
		for (j = 0; j < 8; ++j)
			HCA_PRINT(TRACE_LEVEL_VERBOSE ,HCA_DBG_AV ,("  [%2x] %08x\n",
			       j * 4, cl_ntoh32(((__be32 *) av)[j])));
	}

	if (ah->type == MTHCA_AH_ON_HCA) {
		memcpy_toio((u8*)dev->av_table.av_map + index * MTHCA_AV_SIZE,
			    av, MTHCA_AV_SIZE);
		ah->av = (struct mthca_av *)( (u8*)( dev->av_table.av_map) + index *MTHCA_AV_SIZE );
		kfree(av);
	}
	return 0;
}

int mthca_destroy_ah(struct mthca_dev *dev, struct mthca_ah *ah)
{
	HCA_ENTER(HCA_DBG_AV);

	switch (ah->type) {
	case MTHCA_AH_ON_HCA:
		mthca_free(&dev->av_table.alloc,
 			(u32)( (ah->avdma - dev->av_table.ddr_av_base) /MTHCA_AV_SIZE));
		break;

	case MTHCA_AH_PCI_POOL:
		pci_pool_free(dev->av_table.pool, ah->av, ah->avdma);
		break;

	case MTHCA_AH_KMALLOC:
		kfree(ah->av);
		break;
	}

	HCA_EXIT(HCA_DBG_AV);
	return 0;
}

int mthca_ah_grh_present(struct mthca_ah *ah)
{
	return !!(ah->av->g_slid & 0x80);
}

int mthca_read_ah(struct mthca_dev *dev, struct mthca_ah *ah,
		  struct ib_ud_header *header)
{
	if (ah->type == MTHCA_AH_ON_HCA)
		return -ENOSYS;

	header->lrh.service_level   = (u8)(cl_ntoh32(ah->av->sl_tclass_flowlabel) >> 28);
	header->lrh.destination_lid = ah->av->dlid;
	header->lrh.source_lid      = cl_hton16(ah->av->g_slid & 0x7f);
	header->grh_present = mthca_ah_grh_present(ah);
	if (header->grh_present) {
		header->grh.traffic_class =
			(u8)((cl_ntoh32(ah->av->sl_tclass_flowlabel) >> 20) & 0xff);
		header->grh.flow_label    =
			(u8)(ah->av->sl_tclass_flowlabel & cl_hton32(0xfffff));
		ib_get_cached_gid(&dev->ib_dev,
				  (u8) (cl_ntoh32(ah->av->port_pd) >> 24),
			ah->av->gid_index % dev->limits.gid_table_len,
				  &header->grh.source_gid);
		memcpy(header->grh.destination_gid.raw,
		       ah->av->dgid, 16);
	}

	return 0;
}

int mthca_init_av_table(struct mthca_dev *dev)
{
	int err;

	if (mthca_is_memfree(dev))
		return 0;

	err = mthca_alloc_init(&dev->av_table.alloc,
			       dev->av_table.num_ddr_avs,
			       dev->av_table.num_ddr_avs - 1,
			       0);
	if (err)
		return err;

	dev->av_table.pool = pci_pool_create("mthca_av", dev,
					     MTHCA_AV_SIZE,
					     MTHCA_AV_SIZE, 0);
	if (!dev->av_table.pool)
		goto out_free_alloc;

	if (!(dev->mthca_flags & MTHCA_FLAG_DDR_HIDDEN)) {
		dev->av_table.av_map = ioremap(pci_resource_start(dev, HCA_BAR_TYPE_DDR) +
					       dev->av_table.ddr_av_base -
					       dev->ddr_start,
					       dev->av_table.num_ddr_avs *
					       MTHCA_AV_SIZE,
					       &dev->av_table.av_map_size);
		if (!dev->av_table.av_map)
			goto out_free_pool;
	} else
		dev->av_table.av_map = NULL;

	return 0;

 out_free_pool:
	pci_pool_destroy(dev->av_table.pool);

 out_free_alloc:
	mthca_alloc_cleanup(&dev->av_table.alloc);
	return -ENOMEM;
}

void mthca_cleanup_av_table(struct mthca_dev *dev)
{
	if (mthca_is_memfree(dev))
		return;

	if (dev->av_table.av_map)
		iounmap(dev->av_table.av_map, dev->av_table.av_map_size);
	pci_pool_destroy(dev->av_table.pool);
	mthca_alloc_cleanup(&dev->av_table.alloc);
}

//NB: temporary, for support of query_qp
void mthca_get_av_params(	struct mthca_ah *ah_p, u8 *port_num, __be16 *dlid, u8 *sr, u8 *path_bits )
{
	struct mthca_av *av_p = ah_p->av;
	*port_num	= (u8) (cl_ntoh32(av_p->port_pd) >> 24);
	*dlid				= av_p->dlid;
	*sr     				= av_p->msg_sr & 0x0f;
	*path_bits		= av_p->g_slid & 0x7f;
}

//NB: temporary, for support of modify_qp
void mthca_set_av_params(	struct mthca_dev *dev, struct mthca_ah *ah_p, struct ib_ah_attr *ah_attr )
{
	struct mthca_av *av	 = ah_p->av;
	struct ib_ah *ib_ah_p = (struct ib_ah *)ah_p;
	struct mthca_pd *pd = (struct mthca_pd *)ib_ah_p->pd;

	// taken from mthca_create_av
	av->port_pd = cl_hton32(pd->pd_num | (ah_attr->port_num << 24));
	av->g_slid	= ah_attr->src_path_bits;
	av->dlid		= cl_hton16(ah_attr->dlid);
	av->msg_sr	= (3 << 4) | /* 2K message */
		ah_attr->static_rate;
	av->sl_tclass_flowlabel = cl_hton32(ah_attr->sl << 28);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		av->g_slid |= 0x80;
		av->gid_index = (u8)((ah_attr->port_num - 1) * dev->limits.gid_table_len +
			ah_attr->grh.sgid_index);
		av->hop_limit = ah_attr->grh.hop_limit;
		av->sl_tclass_flowlabel |=
			cl_hton32((ah_attr->grh.traffic_class << 20) |
						ah_attr->grh.flow_label);
		memcpy(av->dgid, ah_attr->grh.dgid.raw, 16);
	} else {
		/* Arbel workaround -- low byte of GID must be 2 */
		av->dgid[3] = cl_hton32(2);
	}
}
	

