/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4_ib.h"
#include "mlx4_debug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ah.tmh"
#endif

static inline void rdma_get_mcast_mac(const union ib_gid *gid, u8 *mac)
{
	int i;

	mac[0] = 0x33;
	mac[1] = 0x33;
	for (i = 2; i < 6; ++i)
		mac[i] = gid->raw[i + 10];

}

int mlx4_ib_resolve_grh(struct mlx4_ib_dev *dev, const struct ib_ah_attr *ah_attr,
			u8 *mac, int *is_mcast)
{
	UNREFERENCED_PARAMETER(dev);

	if (IN6_IS_ADDR_LINKLOCAL((IN6_ADDR*)ah_attr->grh.dgid.raw)) {
		rdma_get_ll_mac(&ah_attr->grh.dgid, mac);
	*is_mcast = 0;
	} else if (IN6_IS_ADDR_MULTICAST((IN6_ADDR*)ah_attr->grh.dgid.raw)) {
		rdma_get_mcast_mac(&ah_attr->grh.dgid, mac);
		*is_mcast = 1;
	} else {
		ASSERT(FALSE);
		return -EINVAL; //jyang:todo
	}
	return 0;
}

static struct ib_ah *create_ib_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
				  struct mlx4_ib_ah *ah)
{
	struct mlx4_dev *dev = to_mdev(pd->device)->dev;

	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_ah *)ERR_PTR(-EFAULT);


	memset(&ah->av, 0, sizeof ah->av);
	ah->ex = NULL;	

	/* By default, when operating in multi-function mode, the demux function's	
	 * GSI mads are also looped back to the tunnel QP for canonical processing	
	 * with other functions
	 */
	ah->gsi_demux_lb = 1;

	ah->av.ib.port_pd = cpu_to_be32(to_mpd(pd)->pdn | (ah_attr->port_num << 24));
	ah->av.ib.g_slid  = ah_attr->src_path_bits;
	ah->av.ib.dlid    = cpu_to_be16(ah_attr->dlid);
	if (ah_attr->static_rate) {
		ah->av.ib.stat_rate = (u8)(ah_attr->static_rate + MLX4_STAT_RATE_OFFSET);
		while (ah->av.ib.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.ib.stat_rate & dev->caps.stat_rate_support))
			--ah->av.ib.stat_rate;
	}
	ah->av.ib.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		ah->av.ib.g_slid   |= 0x80;
		if (mlx4_is_mfunc(dev)) {
			if (ah_attr->grh.sgid_index) {
				/* XXX currently deny access to non-0 gid when
				 * operating in multi-function mode */
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: cannot create ah with "
					     "non-0 gid in mfunc mode\n", 
					     pd->device->dma_device->pdev->name));
                
				return (ib_ah *)ERR_PTR(-EINVAL);
			}
			/* Map to function-specific gid */
			ah->av.ib.gid_index = (u8)slave_gid_index(to_mdev(pd->device));
		} else
			ah->av.ib.gid_index = ah_attr->grh.sgid_index;
		ah->av.ib.hop_limit = ah_attr->grh.hop_limit;
		ah->av.ib.sl_tclass_flowlabel |=
			cpu_to_be32((ah_attr->grh.traffic_class << 20) |
				    ah_attr->grh.flow_label);
		memcpy(ah->av.ib.dgid, ah_attr->grh.dgid.raw, 16);
	}

	ah->av.ib.dlid    = cpu_to_be16(ah_attr->dlid);
	if (ah_attr->static_rate) {
		ah->av.ib.stat_rate = (u8)(ah_attr->static_rate + MLX4_STAT_RATE_OFFSET);
		while (ah->av.ib.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.ib.stat_rate & dev->caps.stat_rate_support))
			--ah->av.ib.stat_rate;
	}
	ah->av.ib.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);

	return &ah->ibah;
}

struct ib_ah *create_rdmaoe_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr,
				   struct mlx4_ib_ah *ah)
{
	struct mlx4_ib_dev *ibdev = to_mdev(pd->device);
	struct mlx4_dev *dev = ibdev->dev;
	u8 mac[6];
	int err;
	int is_mcast;
	// Support is only for vlan id=0
	u16 vlan_tag = 0;
	
	if (mlx4_is_barred(pd->device->dma_device))
		return (ib_ah *)ERR_PTR(-EFAULT);

	err = mlx4_ib_resolve_grh(ibdev, ah_attr, mac, &is_mcast);
	if (err)
		return (ib_ah *)ERR_PTR(err);

	memcpy(ah->av.eth.mac_0_1, mac, 2);
	memcpy(ah->av.eth.mac_2_5, mac + 2, 4);
	ah->av.ib.port_pd = cpu_to_be32(to_mpd(pd)->pdn | (ah_attr->port_num << 24));
	ah->av.ib.g_slid = 0x80;

	vlan_tag |= (ah_attr->sl & 7) << 13;
	ah->av.eth.vlan = cpu_to_be16(vlan_tag);
	
	if (ah_attr->static_rate) {
		ah->av.ib.stat_rate = (u8)(ah_attr->static_rate + MLX4_STAT_RATE_OFFSET);
		while (ah->av.ib.stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
		       !(1 << ah->av.ib.stat_rate & dev->caps.stat_rate_support))
			--ah->av.ib.stat_rate;
	}

	/*
	 * HW requires multicast LID so we just choose one.
	 */
	if (is_mcast)
		ah->av.ib.dlid = cpu_to_be16(0xc000);

	memcpy(ah->av.ib.dgid, ah_attr->grh.dgid.raw, 16);
	ah->av.ib.sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);

	return &ah->ibah;
}


struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah;
	enum rdma_transport_type transport;

	struct ib_ah *ret;

	ah = (mlx4_ib_ah *)kzalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah)
		return (ib_ah *)ERR_PTR(-ENOMEM);

	transport = rdma_port_get_transport(pd->device, ah_attr->port_num);
	if (transport == RDMA_TRANSPORT_RDMAOE) {
		if (!(ah_attr->ah_flags & IB_AH_GRH)) {
			ret = (ib_ah *)ERR_PTR(-EINVAL);
			goto out;
		} else {
			/* TBD: need to handle the case when we get called
			in an atomic context and there we might sleep. We
			don't expect this currently since we're working with
			link local addresses which we can translate without
			going to sleep */
			ret = create_rdmaoe_ah(pd, ah_attr, ah);
			if (IS_ERR(ret))
				goto out;
			else
				return ret;
		}
	} else
		return create_ib_ah(pd, ah_attr, ah); /* never fails */

out:
	kfree(ah);
	return ret;
}


int mlx4_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct mlx4_ib_ah *ah = to_mah(ibah);
	enum rdma_transport_type transport;

	transport = rdma_port_get_transport(ibah->device, ah_attr->port_num);

	if (mlx4_is_barred(ibah->device->dma_device))
		return -EFAULT;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->dlid	       = transport == RDMA_TRANSPORT_IB ? be16_to_cpu(ah->av.ib.dlid) : 0;
	ah_attr->sl	       = (u8)(be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 28);
	ah_attr->port_num      = (u8)(be32_to_cpu(ah->av.ib.port_pd) >> 24);
	if (ah->av.ib.stat_rate)
		ah_attr->static_rate = (u8)(ah->av.ib.stat_rate - MLX4_STAT_RATE_OFFSET);
	ah_attr->src_path_bits = ah->av.ib.g_slid & 0x7F;

	if (mlx4_ib_ah_grh_present(ah)) {
		ah_attr->ah_flags = IB_AH_GRH;

		ah_attr->grh.traffic_class =
			(u8)(be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) >> 20);
		ah_attr->grh.flow_label =
			be32_to_cpu(ah->av.ib.sl_tclass_flowlabel) & 0xfffff;
		ah_attr->grh.hop_limit  = ah->av.ib.hop_limit;
		ah_attr->grh.sgid_index = ah->av.ib.gid_index;
		memcpy(ah_attr->grh.dgid.raw, ah->av.ib.dgid, 16);
	}

	return 0;
}

int mlx4_ib_destroy_ah(struct ib_ah *ah)
{
	if (to_mah(ah)->ex)
		kfree(to_mah(ah)->ex);
	kfree(to_mah(ah));
	return 0;
}

// Leo: temporary 
int mlx4_ib_modify_ah( struct ib_ah *ibah, struct ib_ah_attr *ah_attr )
{
	struct mlx4_av *av	 = &to_mah(ibah)->av.ib;
	struct mlx4_dev *dev = to_mdev(ibah->pd->device)->dev;

	if (mlx4_is_barred(dev))
		return -EFAULT;

	// taken from mthca_create_av
	av->port_pd = cpu_to_be32(to_mpd(ibah->pd)->pdn | (ah_attr->port_num << 24));
	av->g_slid	= ah_attr->src_path_bits;
	av->dlid		= cpu_to_be16(ah_attr->dlid);
	if (ah_attr->static_rate) {
		av->stat_rate = (u8)(ah_attr->static_rate + MLX4_STAT_RATE_OFFSET);
		while (av->stat_rate > IB_RATE_2_5_GBPS + MLX4_STAT_RATE_OFFSET &&
			   !(1 << av->stat_rate & dev->caps.stat_rate_support))
			--av->stat_rate;
	}
	av->sl_tclass_flowlabel = cpu_to_be32(ah_attr->sl << 28);
	if (ah_attr->ah_flags & IB_AH_GRH) {
		av->g_slid |= 0x80;
		av->gid_index = ah_attr->grh.sgid_index;
		av->hop_limit = ah_attr->grh.hop_limit;
		av->sl_tclass_flowlabel |=
			cpu_to_be32((ah_attr->grh.traffic_class << 20) |
						ah_attr->grh.flow_label);
		memcpy(av->dgid, ah_attr->grh.dgid.raw, 16);
	}
	return 0;
}

