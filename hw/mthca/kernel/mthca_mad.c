/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#include <ib_verbs.h>
#include <ib_mad.h>
#include <ib_smi.h>

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_mad.tmh"
#endif
#include "mthca_cmd.h"

struct mthca_trap_mad {
	struct scatterlist sg;
};

static void update_sm_ah(struct mthca_dev *dev,
			 u8 port_num, u16 lid, u8 sl)
{
	struct ib_ah *new_ah;
	struct ib_ah_attr ah_attr;
	SPIN_LOCK_PREP(lh);

	if (!dev->send_agent[port_num - 1][0])
		return;

	RtlZeroMemory(&ah_attr, sizeof ah_attr);
	ah_attr.dlid     = lid;
	ah_attr.sl       = sl;
	ah_attr.port_num = port_num;

	new_ah = ibv_create_ah(dev->send_agent[port_num - 1][0]->qp->pd,
			      &ah_attr, NULL, NULL);
	if (IS_ERR(new_ah))
		return;

	spin_lock_irqsave(&dev->sm_lock, &lh);
	if (dev->sm_ah[port_num - 1]) {
		ibv_destroy_ah(dev->sm_ah[port_num - 1]);
	}
	dev->sm_ah[port_num - 1] = new_ah;
	spin_unlock_irqrestore(&lh);
}

/*
 * Snoop SM MADs for port info and P_Key table sets, so we can
 * synthesize LID change and P_Key change events.
 */
static void smp_snoop(struct ib_device *ibdev,
		      u8 port_num,
		      struct ib_mad *mad)
{
	struct ib_event event;

	if ((mad->mad_hdr.mgmt_class  == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class  == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method     == IB_MGMT_METHOD_SET) {
		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO) {
			update_sm_ah(to_mdev(ibdev), port_num,
				     cl_ntoh16(*(__be16 *) (mad->data + 58)),
				     (*(u8 *) (mad->data + 76)) & 0xf);

			event.device           = ibdev;
			event.event            = IB_EVENT_LID_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}

		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PKEY_TABLE) {
			event.device           = ibdev;
			event.event            = IB_EVENT_PKEY_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}
	}
}

static void forward_trap(struct mthca_dev *dev,
			 u8 port_num,
			 struct ib_mad *mad)
{
	int qpn = mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED;
	struct mthca_trap_mad *tmad;
	struct ib_sge      gather_list;
	struct _ib_send_wr wr;
	struct ib_mad_agent *agent = dev->send_agent[port_num - 1][qpn];
	int ret;
	SPIN_LOCK_PREP(lh);

	/* fill the template */
	wr.ds_array = (ib_local_ds_t*)(void*)&gather_list;
	wr.num_ds = 1;
	wr.wr_type = WR_SEND;
	wr.send_opt = IB_SEND_OPT_SIGNALED;
	wr.dgrm.ud.remote_qp = cl_hton32(qpn);
	wr.dgrm.ud.remote_qkey = qpn ? IB_QP1_QKEY : 0;
	
	if (agent) {
		tmad = kmalloc(sizeof *tmad, GFP_KERNEL);
		if (!tmad)
			return;

		alloc_dma_zmem(dev, sizeof *mad, &tmad->sg);
		if (!tmad->sg.page) {
			kfree(tmad);
			return;
		}

		memcpy(tmad->sg.page, mad, sizeof *mad);

		wr.dgrm.ud.rsvd = (void*)&((struct ib_mad *)tmad->sg.page)->mad_hdr;
		wr.wr_id         = (u64)(ULONG_PTR)tmad;
		gather_list.addr   = tmad->sg.dma_address;
		gather_list.length = tmad->sg.length;
		gather_list.lkey   = to_mpd(agent->qp->pd)->ntmr.ibmr.lkey;

		/*
		 * We rely here on the fact that MLX QPs don't use the
		 * address handle after the send is posted (this is
		 * wrong following the IB spec strictly, but we know
		 * it's OK for our devices).
		 */
		spin_lock_irqsave(&dev->sm_lock, &lh);
		wr.dgrm.ud.h_av = (ib_av_handle_t)dev->sm_ah[port_num - 1];
		if (wr.dgrm.ud.h_av) {
				HCA_PRINT( TRACE_LEVEL_ERROR ,HCA_DBG_MAD ,(" ib_post_send_mad not ported \n" ));
				ret = -EINVAL;
		}
		else
			ret = -EINVAL;
		spin_unlock_irqrestore(&lh);

		if (ret) {
			free_dma_mem_map(dev, &tmad->sg, PCI_DMA_BIDIRECTIONAL );
			kfree(tmad);
		}
	}
}

int mthca_process_mad(struct ib_device *ibdev,
		      int mad_flags,
		      u8 port_num,
		      struct _ib_wc *in_wc,
		      struct _ib_grh *in_grh,
		      struct ib_mad *in_mad,
		      struct ib_mad *out_mad)
{
	int err;
	u8 status;
	u16 slid = in_wc ? in_wc->recv.ud.remote_lid : cl_ntoh16(IB_LID_PERMISSIVE);

	HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD ,("in: Class %02x, Method %02x, AttrId %x, AttrMod %x, ClSpec %x, Tid %I64x\n",
		(u32)in_mad->mad_hdr.mgmt_class, (u32)in_mad->mad_hdr.method, 
		(u32)in_mad->mad_hdr.attr_id, in_mad->mad_hdr.attr_mod, 
		(u32)in_mad->mad_hdr.class_specific, in_mad->mad_hdr.tid ));

	/* Forward locally generated traps to the SM */
	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP &&
	    slid == 0) {
		forward_trap(to_mdev(ibdev), port_num, in_mad);
		HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD ,("Not sent, but locally forwarded\n"));
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}

	/*
	 * Only handle SM gets, sets and trap represses for SM class
	 *
	 * Only handle PMA and Mellanox vendor-specific class gets and
	 * sets for other classes.
	 */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	    in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {

		if (in_mad->mad_hdr.method   != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_SET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_TRAP_REPRESS) {
			HCA_PRINT( TRACE_LEVEL_VERBOSE,HCA_DBG_MAD,(" Skip some methods. Nothing done !\n"));
			return IB_MAD_RESULT_SUCCESS;
		}

		/*
		 * Don't process SMInfo queries or vendor-specific
		 * MADs -- the SMA can't handle them.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_SM_INFO ||
		    ((in_mad->mad_hdr.attr_id & IB_SMP_ATTR_VENDOR_MASK) ==
		     IB_SMP_ATTR_VENDOR_MASK)) {
			HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD ,("Skip SMInfo queries or vendor-specific MADs. Nothing done !\n"));
			return IB_MAD_RESULT_SUCCESS;
		}
	} 
	else {
		if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT ||
		   in_mad->mad_hdr.mgmt_class == IB_MLX_VENDOR_CLASS1     ||
		   in_mad->mad_hdr.mgmt_class == IB_MLX_VENDOR_CLASS2) {

			if (in_mad->mad_hdr.method  != IB_MGMT_METHOD_GET &&
			    in_mad->mad_hdr.method  != IB_MGMT_METHOD_SET) {
				HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD ,("Skip some management methods. Nothing done !\n"));
				return IB_MAD_RESULT_SUCCESS;
			}
		} 
		else {
			HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD ,("Skip IB_MGMT_CLASS_PERF_MGMT et al. Nothing done !\n"));
			return IB_MAD_RESULT_SUCCESS;
		}	
	}

	// send MAD
	err = mthca_MAD_IFC(to_mdev(ibdev),
			    mad_flags & IB_MAD_IGNORE_MKEY,
			    mad_flags & IB_MAD_IGNORE_BKEY,
			    port_num, in_wc, in_grh, in_mad, out_mad,
			    &status);
	if (err) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MAD ,("MAD_IFC failed\n"));
		return IB_MAD_RESULT_FAILURE;
	}
	if (status == MTHCA_CMD_STAT_BAD_PKT)
		return IB_MAD_RESULT_SUCCESS;
	if (status) {
		HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MAD ,("MAD_IFC returned status %02x\n", status));
		return IB_MAD_RESULT_FAILURE;
	}

	if (!out_mad->mad_hdr.status)
		smp_snoop(ibdev, port_num, in_mad);

	HCA_PRINT( TRACE_LEVEL_VERBOSE ,HCA_DBG_MAD,("out: Class %02x, Method %02x, AttrId %x, AttrMod %x, ClSpec %x, Tid %I64x, Status %x\n",
		(u32)out_mad->mad_hdr.mgmt_class, (u32)out_mad->mad_hdr.method, 
		(u32)out_mad->mad_hdr.attr_id, out_mad->mad_hdr.attr_mod, 
		(u32)out_mad->mad_hdr.class_specific, out_mad->mad_hdr.tid,
		(u32)out_mad->mad_hdr.status ));

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS) {
		/* no response for trap repress */
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc)
{
	struct mthca_trap_mad *tmad =
		(void *) (ULONG_PTR) mad_send_wc->wr_id;

	free_dma_mem_map(agent->device->mdev, &tmad->sg, PCI_DMA_BIDIRECTIONAL );
	kfree(tmad);
}
