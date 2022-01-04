/*
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	   Redistribution and use in source and binary forms, with or
 *	   without modification, are permitted provided that the following
 *	   conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include "mlx4.h"
#include "cmd.h"
#include "fw.h"
#include "eq.h"
#include "driver.h"
#include "stat.h"
#include "mlx4_debug.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "eq.tmh"
#endif

//
// Global object used for debugging porpuse
//
u64 g_bogus_interrupts = 0;

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mlx4_eq_context {
	__be32			flags;
	u16			reserved1[3];
	__be16			page_offset;
	u8			log_eq_size;
	u8			reserved2[4];
	u8			eq_period;
	u8			reserved3;
	u8			eq_max_count;
	u8			reserved4[3];
	u8			intr;
	u8			log_page_size;
	u8			reserved5[2];
	u8			mtt_base_addr_h;
	__be32			mtt_base_addr_l;
	u32			reserved6[2];
	__be32			consumer_index;
	__be32			producer_index;
	u32			reserved7[4];
};

#define MLX4_EQ_STATUS_OK	   ( 0 << 28)
#define MLX4_EQ_STATUS_WRITE_FAIL  (10 << 28)
#define MLX4_EQ_OWNER_SW	   ( 0 << 24)
#define MLX4_EQ_OWNER_HW	   ( 1 << 24)
#define MLX4_EQ_FLAG_EC		   ( 1 << 18)
#define MLX4_EQ_FLAG_OI		   ( 1 << 17)
#define MLX4_EQ_STATE_ARMED	   ( 9 <<  8)
#define MLX4_EQ_STATE_FIRED	   (10 <<  8)
#define MLX4_EQ_STATE_ALWAYS_ARMED (11 <<  8)

#define MLX4_ASYNC_EVENT_MASK ((1ull << MLX4_EVENT_TYPE_PATH_MIG)		| \
				   (1ull << MLX4_EVENT_TYPE_COMM_EST)		| \
				   (1ull << MLX4_EVENT_TYPE_SQ_DRAINED)		| \
				   (1ull << MLX4_EVENT_TYPE_CQ_ERROR)		| \
				   (1ull << MLX4_EVENT_TYPE_WQ_CATAS_ERROR)		| \
				   (1ull << MLX4_EVENT_TYPE_EEC_CATAS_ERROR)	| \
				   (1ull << MLX4_EVENT_TYPE_PATH_MIG_FAILED)	| \
				   (1ull << MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
				   (1ull << MLX4_EVENT_TYPE_WQ_ACCESS_ERROR)	| \
				   (1ull << MLX4_EVENT_TYPE_PORT_CHANGE)		| \
				   (1ull << MLX4_EVENT_TYPE_ECC_DETECT)			| \
				   (1ull << MLX4_EVENT_TYPE_SRQ_CATAS_ERROR)	| \
				   (1ull << MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE)	| \
				   (1ull << MLX4_EVENT_TYPE_SRQ_LIMIT)			| \
				   (1ull << MLX4_EVENT_TYPE_CMD)				| \
				   (1ull << MLX4_EVENT_TYPE_VEP_UPDATE)			| \
				   (1ull << MLX4_EVENT_TYPE_MAC_UPDATE)			| \
				   (1ull << MLX4_EVENT_TYPE_COMM_CHANNEL)		| \
				   (1ull << MLX4_EVENT_TYPE_OP_REQUIRED))
				   
static struct mlx4_eqe* next_slave_event_eqe(struct slave_event_eq* slave_eq)
{
	struct mlx4_eqe* eqe = &slave_eq->event_eqe[slave_eq->cons_index & (SLAVE_EVENT_EQ_SIZE - 1)];
	return (!!(eqe->owner & 0x80) ^ !!(slave_eq->cons_index & SLAVE_EVENT_EQ_SIZE)) ? eqe : NULL;
}

/* master command processing */
#ifdef NTDDI_WIN8
KSTART_ROUTINE mlx4_gen_slave_eqe;
#endif
void mlx4_gen_slave_eqe(void* ctx)
{
	struct mlx4_priv *priv = (struct mlx4_priv*) ctx;
	struct mlx4_dev *dev = &priv->dev;
	struct slave_event_eq* slave_eq = &priv->mfunc.master.slave_eq;
	struct working_thread * p_thread = &priv->mfunc.master.slave_event_thread;
	NTSTATUS status;	
	struct mlx4_eqe *eqe;
	int i, err = 0;
	short int slave;
	KIRQL oldIrql;	  

	ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	KeRaiseIrql(APC_LEVEL, &oldIrql);

	ASSERT( KeGetCurrentIrql() == APC_LEVEL );

	for(;;) {
		status = KeWaitForSingleObject( &p_thread->trig_event, 
										Executive, KernelMode, FALSE, NULL );

		ASSERT(status == STATUS_SUCCESS);
		if ((status != STATUS_SUCCESS) || p_thread->f_stop){
			/* thread stopped */
			break;		
		}

		for(eqe = next_slave_event_eqe(slave_eq); eqe != NULL; eqe = next_slave_event_eqe(slave_eq)) {
			slave = *(short int*)eqe->reserved3;
			*(short int*)eqe->reserved3 = 0;

			if (slave == -1) {
				for (i = 0; i < (int)dev->num_slaves; ++i)
					if (priv->mfunc.master.slave_state[i].last_cmd == MLX4_COMM_CMD_VHCR_POST){
						err = mlx4_GEN_EQE(dev, i, eqe);
						if (err) {
							MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Failed to generate an event: 0x%x for slave:%d\n", eqe->type, slave));
							ASSERT(FALSE);
						}
					}
			} else {
				err = mlx4_GEN_EQE(dev, slave, eqe);
				if (err) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Failed to generate an event: 0x%x for slave:%d\n", eqe->type, slave));
					ASSERT(FALSE);
				}
			}

			++slave_eq->cons_index;
		}
	}
	
	KeLowerIrql(oldIrql);
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Exit working thread mlx4_gen_slave_eqe.\n", dev->pdev->name));

	PsTerminateSystemThread(STATUS_SUCCESS);
}

static void new_slave_event(struct mlx4_dev *dev, int slave, struct mlx4_eqe* eqe)
{
	struct mlx4_priv *priv = mlx4_priv(dev);	
	struct slave_event_eq* slave_eq = &priv->mfunc.master.slave_eq;
	struct mlx4_eqe* s_eqe = &slave_eq->event_eqe[slave_eq->prod_index & (SLAVE_EVENT_EQ_SIZE - 1)];
	struct mlx4_eqe t_eqe;

	if (!!(s_eqe->owner & 0x80) ^ !!(slave_eq->prod_index & SLAVE_EVENT_EQ_SIZE))
	{
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Master failed to generate an EQE for slave: %d. "
					   "No free EQE on slave events queue\n", slave));
		ASSERT(FALSE);
		return;
	}

	//
	// prepare a temporray eqe that will be copied latter. We need to set the owner bit
	// and the slave id before writing it to queue. Otherwise the tread may read the eqe 
	// before updating the owner byte and ignore it.
	//
	memcpy(&t_eqe, eqe, sizeof(struct mlx4_eqe));
	*(short int*)t_eqe.reserved3 = (short int)slave;
	t_eqe.owner =  !!(slave_eq->prod_index & SLAVE_EVENT_EQ_SIZE) ? 0x0 : 0x80;  
	memcpy(s_eqe, &t_eqe, sizeof(struct mlx4_eqe));
	
	wmb();
	
	++slave_eq->prod_index;    
	trig_working_thread(&priv->mfunc.master.slave_event_thread);
}

static void mlx4_slave_event(struct mlx4_dev *dev, int slave, struct mlx4_eqe* eqe)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *ctx = &priv->mfunc.master.slave_state[slave];

	if (ctx->last_cmd != MLX4_COMM_CMD_VHCR_POST) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: received event for inactive slave:%d\n", dev->pdev->name, slave));
		return;
	}

	new_slave_event(dev, slave, eqe);
}

void mlx4_slave_event_all(struct mlx4_dev *dev, struct mlx4_eqe* eqe)
{
	new_slave_event(dev, -1,eqe);
}

int mlx4_GET_EVENT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						 struct mlx4_cmd_mailbox *inbox,
						 struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_slave_state *ctx = &priv->mfunc.master.slave_state[slave];
	unsigned long flags;
	UNUSED_PARAM(flags);

	UNUSED_PARAM(inbox);
	UNUSED_PARAM(outbox);

	spin_lock_irqsave(&ctx->lock, &flags);
	if (ctx->eq_ci == ctx->eq_pi) {
		vhcr->out_param = MLX4_EVENT_TYPE_NONE;
	} else if ((u16) (ctx->eq_pi - ctx->eq_ci) > MLX4_MFUNC_MAX_EQES) {
		ctx->eq_ci = (u16)(ctx->eq_pi - MLX4_MFUNC_MAX_EQES);
		vhcr->out_param = MLX4_EVENT_TYPE_EQ_OVERFLOW;
	} else {
		vhcr->out_param = ctx->eq[ctx->eq_ci & MLX4_MFUNC_EQE_MASK].type |
				  ((u64) ctx->eq[ctx->eq_ci & MLX4_MFUNC_EQE_MASK].port << 8) |
				  ((u64) ctx->eq[ctx->eq_ci & MLX4_MFUNC_EQE_MASK].param << 32);
		++ctx->eq_ci;
	}
	spin_unlock_irqrestore(&ctx->lock, flags);
	return 0;
}

void
mlx4_dev_remove(
	IN				struct mlx4_dev 			*dev )
{
	//BUGBUG: SRIOV is not supported
	KeWaitForSingleObject(&g.surprise_removal_event, Executive, KernelMode, FALSE, NULL);
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Removing itself because PPF got REMOVE_DEVICE command\n", 
		dev->pdev->name ));
	WdfDeviceSetFailed((WDFDEVICE)(dev->pdev->p_wdf_device), WdfDeviceFailedNoRestart );
	mlx4_remove_one(dev->pdev);
	KeSetEvent(&g.surprise_removal_event, 0, FALSE);
}

static IO_WORKITEM_ROUTINE mlx4_dev_remove_wi;
static VOID mlx4_dev_remove_wi(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				PVOID						context)
{
	struct mlx4_dev *dev = (struct mlx4_dev *) context;
	struct mlx4_priv *priv = mlx4_priv(dev);
	
	UNUSED_PARAM(p_dev_obj);
	IoFreeWorkItem( priv->remove_work );
	mlx4_dev_remove(dev);
}

static void mlx4_update_sqp(struct mlx4_dev *dev)
{
	if (!dev->caps.sqp_demux) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: unexpected update_sqp event\n", dev->pdev->name));
		return;
	}
	if (mlx4_GET_SLAVE_SQP(dev, mlx4_priv(dev)->mfunc.demux_sqp,
				   dev->caps.sqp_demux))
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: couldn't update sqp\n", dev->pdev->name));
}

//
// NOTE: The mlx4_update_vep_config doesn't call for SRIOV therefore 16 bits are 
//		 enough for all the functions
//

static IO_WORKITEM_ROUTINE mlx4_update_vep_config;
static void mlx4_update_vep_config(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				PVOID						context)
{
	struct mlx4_dev *dev = (struct mlx4_dev *) context;
	struct mlx4_priv *priv = mlx4_priv(dev);		
	struct mlx4_mfunc_master_ctx *master = &priv->mfunc.master;
	struct mlx4_vep_cfg* vep_cfg;
	struct mlx4_vep_cfg old_vep_cfg[16];
	struct mlx4_eqe new_eqe;
	enum mlx4_dev_event port_event;
	u8 vep_num;
	u8 slave_id;
	u8 port;	
	int err;
	u16 vep_config_map;
	bool port_updated[MLX4_MAX_PORTS + 1] = {false};
	int i;

	UNUSED_PARAM(p_dev_obj);

	spin_lock(&master->vep_config_lock);

	vep_config_map = master->vep_config_bitmap;
	master->vep_config_bitmap = 0;

	spin_unlock(&master->vep_config_lock);

	do {
		for (slave_id = 0; slave_id < 16; slave_id++) {
			// store old configuration
			memcpy(&old_vep_cfg[slave_id], &master->slave_state[slave_id].vep_cfg, sizeof(struct mlx4_vep_cfg));
		}
		
		while(vep_config_map)
		{
			for (slave_id = 0; slave_id < 16; slave_id++) {
				if (!(vep_config_map & (1 << slave_id)))
					continue;

				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: mlx4_update_vep_config for function %d\n", dev->pdev->name, slave_id));

				vep_num = master->slave_state[slave_id].vep_num;
				port = master->slave_state[slave_id].port_num;
				port_updated[port] = true;
				vep_cfg = &master->slave_state[slave_id].vep_cfg;

				err = mlx4_QUERY_VEP_CFG(dev, vep_num, port, vep_cfg);
				if(err) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,("failed to read VEP configuration for function %d\n", vep_num));
					ASSERT(FALSE);
					continue;
				}

				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: function %d, new link %d, old link %d\n", dev->pdev->name, vep_num,
				    vep_cfg->link, old_vep_cfg[slave_id].link));

			}

			spin_lock(&master->vep_config_lock);
			
			vep_config_map = master->vep_config_bitmap;
			master->vep_config_bitmap = 0;
			
			spin_unlock(&master->vep_config_lock);
		}

		for (i = 1; i <= dev->caps.num_ports; i++) {
			if (port_updated[i])
				mlx4_update_uplink_arbiter(dev, i);
		} 

		for (slave_id = 0; slave_id < 16; slave_id++) {
			vep_cfg = &master->slave_state[slave_id].vep_cfg;
			port = master->slave_state[slave_id].port_num;
			if ((old_vep_cfg[slave_id].link != vep_cfg->link) ||
				(old_vep_cfg[slave_id].bw_value != vep_cfg->bw_value)) {
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: update link state for function %d\n", dev->pdev->name, slave_id));

				new_eqe.type =  MLX4_EVENT_TYPE_PORT_CHANGE;
				new_eqe.event.port_change.port = cpu_to_be32(port << 28); 
				new_eqe.subtype = (u8)(master->slave_state[slave_id].vep_cfg.link ? MLX4_PORT_CHANGE_SUBTYPE_ACTIVE :
				                    MLX4_PORT_CHANGE_SUBTYPE_DOWN);
				if (priv->link_up[port]) {
					if (slave_id == dev->caps.function)  {
						port_event = vep_cfg->link ? MLX4_DEV_EVENT_PORT_UP : MLX4_DEV_EVENT_PORT_DOWN;
						mlx4_dispatch_event(dev, port_event  , port);
					} else {
						 mlx4_slave_event(dev, slave_id, &new_eqe);
					}
				}
			}

			if (old_vep_cfg[slave_id].mac != vep_cfg->mac) {
				if (slave_id == dev->caps.function) {
					dev->caps.def_mac[port] = vep_cfg->mac;
					mlx4_dispatch_event(dev, (mlx4_dev_event)MLX4_EVENT_TYPE_MAC_UPDATE, port);
				} else {
					new_eqe.type = MLX4_EVENT_TYPE_MAC_UPDATE;
					new_eqe.event.mac_update.port = port;
					new_eqe.event.mac_update.mac = cpu_to_be64(vep_cfg->mac);
					mlx4_slave_event(dev, slave_id, &new_eqe);
				}
			}
		}

		spin_lock(&master->vep_config_lock);
		vep_config_map = master->vep_config_bitmap;
		master->vep_config_bitmap = 0;

		if (vep_config_map == 0)
			master->vep_config_queued = false;
		spin_unlock(&master->vep_config_lock);

	} while (vep_config_map != 0);	 
}

static int mlx4_eq_int(struct mlx4_dev *dev, struct mlx4_eq *eq)
{
	struct mlx4_eqe *eqe;
	struct mlx4_slave_state *s_state;
	int cqn;
	int eqes_found = 0;
	int set_ci = 0;
	int slave ;
	int ret;
	static const uint32_t cDpcMaxTime = 10000; //max time to spend in a while loop
	struct mlx4_priv *priv = mlx4_priv(dev);		
	uint64_t start = cl_get_time_stamp();
	int port;
	PMLX4_ST_DEVICE p_stat = dev->pdev->p_stat;
	u8 vep_num;
	u8 slave_id = 0;
	unsigned long i;
	u64 mac;
	int dbg_inx = eq->dbg_inx & 0x3FF;

	p_stat->dpc_n_calls++;
	p_stat->dpc_eq_ci_first = eq->cons_index;
	p_stat->dpc_eq = eq;

	eq->debug[dbg_inx].cons_index_in_call = eq->cons_index;
	
	while ((eqe = next_eqe_sw(eq)) != 0 ) {
		/*
		 * Make sure we read EQ entry contents after we've
		 * checked the ownership bit.
		 */
		rmb();

		switch (eqe->type) {
		case MLX4_EVENT_TYPE_COMP:
			cqn = be32_to_cpu(eqe->event.comp.cqn) & 0xffffff;
			mlx4_cq_completion(dev, eq, cqn);
			break;

		case MLX4_EVENT_TYPE_PATH_MIG:
		case MLX4_EVENT_TYPE_COMM_EST:
		case MLX4_EVENT_TYPE_SQ_DRAINED:
		case MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE:
		case MLX4_EVENT_TYPE_WQ_CATAS_ERROR:
		case MLX4_EVENT_TYPE_PATH_MIG_FAILED:
		case MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		case MLX4_EVENT_TYPE_WQ_ACCESS_ERROR:
			if (mlx4_is_master(dev)) {
				/* forward only to slave owning the QP */
				ret = mlx4_get_slave_from_resource_id(dev, RES_QP, eqe->event.qp.qpn, &slave);
				if (!ret)
					mlx4_slave_event(dev, slave, eqe);
			} else
				mlx4_qp_event(dev, be32_to_cpu(eqe->event.qp.qpn) &
					0xffffff, eqe->type);
			break;

		case MLX4_EVENT_TYPE_SRQ_LIMIT:
		case MLX4_EVENT_TYPE_SRQ_CATAS_ERROR:
			if (mlx4_is_master(dev)) {
				/* forward only to slave owning the SRQ */
				ret = mlx4_get_slave_from_resource_id(dev, RES_SRQ, eqe->event.srq.srqn, &slave);
				if (!ret) {
					mlx4_slave_event(dev, slave, eqe);
				}
			} else
				mlx4_srq_event(dev, be32_to_cpu(eqe->event.srq.srqn) &
							0xffffff, eqe->type);
			break;

		case MLX4_EVENT_TYPE_CMD:
			mlx4_cmd_event(dev,
					   be16_to_cpu(eqe->event.cmd.token),
					   eqe->event.cmd.status,
					   be64_to_cpu(eqe->event.cmd.out_param));
			break;

		case MLX4_EVENT_TYPE_PORT_CHANGE:
			port = be32_to_cpu(eqe->event.port_change.port) >> 28;
			if (eqe->subtype == MLX4_PORT_CHANGE_SUBTYPE_DOWN) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Port%d change down event\n", dev->pdev->name,port));
			priv->link_up[port] = false;
			mlx4_dispatch_event(dev, MLX4_DEV_EVENT_PORT_DOWN, port);
			mlx4_priv(dev)->sense.do_sense_port[port] = 1;
			if (mlx4_is_master(dev))
				mlx4_slave_event_all(dev, eqe);
			} else {
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Port%d change up event\n", dev->pdev->name,port));
				s_state = priv->mfunc.master.slave_state; 
				priv->link_up[port] = true;
				/* Link UP event is acceptable only in case VEP link is enabled*/
				if (!mlx4_is_master(dev) ||
						s_state[dev->caps.function].vep_cfg.link) {
					mlx4_dispatch_event(dev, MLX4_DEV_EVENT_PORT_UP, port);
					mlx4_priv(dev)->sense.do_sense_port[port] = 0;
				}
				if (mlx4_is_master(dev)) {
					for (i = 0; i < dev->num_slaves; i++) {
						if ((int)i == dev->caps.function || !(s_state[i].active))
							continue; 
						vep_num = s_state[i].pf_num;
						if (s_state[vep_num].vep_cfg.link)
							mlx4_slave_event(dev, i, eqe);
					}
				}
			}
			break;

		case MLX4_EVENT_TYPE_CQ_ERROR:
            if (eqe->event.cq_err.syndrome == 1) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: CQ overrun on CQN %06x\n",
					  dev->pdev->name, 
					  be32_to_cpu(eqe->event.cq_err.cqn) & 0xffffff));
                eqe->type = MLX4_EVENT_TYPE_CQ_OVERFLOW;
            } else {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: CQ access violation on CQN %06x\n",
				dev->pdev->name, 
				  be32_to_cpu(eqe->event.cq_err.cqn) & 0xffffff));
            }
			if (mlx4_is_master(dev)) {
				/* TODO: forward only to slave owning the CQ */
				ret = mlx4_get_slave_from_resource_id(dev, RES_CQ, eqe->event.cq_err.cqn, &slave);
				if (!ret)
					mlx4_slave_event(dev, slave, eqe);
			} else {
				mlx4_cq_event(dev, eq, be32_to_cpu(eqe->event.cq_err.cqn), eqe->type);
			}
			break;

		case MLX4_EVENT_TYPE_EQ_OVERFLOW:
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: EQ overrun on EQN %d\n", dev->pdev->name, eq->eqn));
			break;

		case MLX4_EVENT_TYPE_SQP_UPDATE:
			if (!mlx4_is_slave(dev)) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Non slave function received SQP_UPDATE event\n"));
				break;
			}
			mlx4_update_sqp(dev);
			break;

		case MLX4_EVENT_TYPE_OP_REQUIRED:
		{
			int32_t opreq_cont = atomic_inc(&priv->opreq_count);
			ASSERT(mlx4_is_master(dev) || !mlx4_is_mfunc(dev));

			/* FW commands can't be executed from interrupt context 
			   working in deferred task */
			if (opreq_cont == 1)
			{
				// The work iteam wasn't schedule yet. queue it
				IoQueueWorkItem( priv->opreq_work, (PIO_WORKITEM_ROUTINE)mlx4_opreq_action, DelayedWorkQueue, dev );
			}
			break;
		}
		
		case MLX4_EVENT_TYPE_COMM_CHANNEL:
			ASSERT(mlx4_is_mfunc(dev) && mlx4_is_master(dev));
			memcpy(priv->mfunc.master.comm_arm_bit_vec, 
				   eqe->event.comm_channel_arm.bit_vec, 
				   sizeof(eqe->event.comm_channel_arm.bit_vec[0]) * COMM_CHANNEL_BIT_ARRAY_SIZE);
			trig_working_thread(&priv->mfunc.master.comm_channel_thread);
			break;

		case MLX4_EVENT_TYPE_PPF_REMOVE:
			if (mlx4_is_slave(dev)) {
                i = 0;
				if (dev->pdev->ib_dev) { 
					dev->flags |= MLX4_FLAG_RESET_4_RMV;	// bar the device
					i = mlx4_dispatch_reset_event(dev->pdev->ib_dev, IB_EVENT_RESET_4_RMV);
				}
				if (i) {
					MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: %d children are asked to prepare for remove\n", dev->pdev->name, i));
				}
				else {
					IoQueueWorkItem( priv->remove_work, (PIO_WORKITEM_ROUTINE)mlx4_dev_remove_wi, DelayedWorkQueue, dev );
				}
			}
			else
				ASSERT(FALSE);
			break;

		case MLX4_EVENT_TYPE_MAC_UPDATE:
			port = eqe->event.mac_update.port;
			mac = be64_to_cpu(eqe->event.mac_update.mac);
			dev->caps.def_mac[port] = mac;
			mlx4_dispatch_event(dev, (mlx4_dev_event)MLX4_EVENT_TYPE_MAC_UPDATE, port);
			break;

		case MLX4_EVENT_TYPE_VEP_UPDATE:
			if (!mlx4_is_master(dev)) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "Non-master function received VEP_UPDATE event\n"));
				ASSERT(FALSE);
				break;
			}

			vep_num = eqe->event.vep_config.vep_num;
			port = eqe->event.vep_config.port;

			slave_id = get_slave(dev, (u8)port, vep_num);

			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: MLX4_EVENT_TYPE_VEP_UPDATE for function %d\n", dev->pdev->name, slave_id));

			spin_lock(&priv->mfunc.master.vep_config_lock);

			ASSERT(slave_id < 16);
			priv->mfunc.master.vep_config_bitmap |= (1 << slave_id);

			if (!priv->mfunc.master.vep_config_queued)
			{
				ASSERT(priv->mfunc.master.vep_config_work != NULL);
				IoQueueWorkItem(priv->mfunc.master.vep_config_work,
						(PIO_WORKITEM_ROUTINE)mlx4_update_vep_config, DelayedWorkQueue, dev);
				priv->mfunc.master.vep_config_queued = true;
			}

			spin_unlock(&priv->mfunc.master.vep_config_lock);
			
			break;
			
		case MLX4_EVENT_TYPE_EEC_CATAS_ERROR:
		case MLX4_EVENT_TYPE_ECC_DETECT:
		default:
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Unhandled event %02x(%02x) on EQ %d at index %u\n",
				 dev->pdev->name,  eqe->type, eqe->subtype, eq->eqn, eq->cons_index));
			break;
		};

		++eq->cons_index;
		eqes_found = 1;
		++set_ci;

		/*
		 * The HCA will think the queue has overflowed if we
		 * don't tell it we've been processing events.	We
		 * create our EQs with MLX4_NUM_SPARE_EQE extra
		 * entries, so we must update our consumer index at
		 * least that often.
		 */
		if (unlikely(set_ci >= MLX4_NUM_SPARE_EQE)) {
			eq_set_ci(eq, 0);
			set_ci = 0;
		}
		
		if (cl_get_time_stamp() - start > cDpcMaxTime ) {
			break; //allow other DPCs as well
		}
	}

	if (!eqes_found)
		eq->no_of_empty_eq++;
	
	eq_set_ci(eq, 1);
	eq->eq_no_progress = 0;
	p_stat->dpc_eq_ci_last = eq->cons_index;
	return eqes_found;
}

static void mlx4_dpc( PRKDPC dpc, 
	PVOID ctx, PVOID arg1, PVOID arg2 )
{
	struct mlx4_eq *eq	= (mlx4_eq *)ctx;
	uint64_t dpc_time = get_tickcount_in_ms();
	static BOOLEAN fIgnoreAssert = TRUE;
	
	UNREFERENCED_PARAMETER(dpc);
	UNREFERENCED_PARAMETER(arg1);
	UNREFERENCED_PARAMETER(arg2);
	UNREFERENCED_PARAMETER(dpc_time);

#if 0
	if ((dpc_time - eq->interrupt_time) > 500){
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_EQ,("%s: Calling to interrupt DPC took too much time (%I64d ms)\n", 
			eq->dev->pdev->name, (dpc_time - eq->interrupt_time));
		ASSERT(fIgnoreAssert);
	}
#endif

	spin_lock_dpc(&eq->lock);
	mlx4_eq_int(eq->dev, eq);
	spin_unlock_dpc(&eq->lock);
}

BOOLEAN mlx4_process_eqes( struct mlx4_eq *eq )
{
	int dbg_inx = eq->dbg_inx & 0x3FF;

	eq->debug[dbg_inx].cons_index_in_call = eq->cons_index;

	eq->interrupt_time = get_tickcount_in_ms();
	KeInsertQueueDpc(&eq->dpc, NULL, NULL);
	return 1;
}

static BOOLEAN legacy_isr(struct mlx4_dev *dev)
{
	int i;
	int work = 0;
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_eq* eq;
	struct mlx4_eqe* eqe;
	int dbg_inx;

	for (i = 0; i < priv->eq_table.num_eqs; ++i) {
		eqe = next_eqe_sw(&priv->eq_table.eq[i]);
		if ( eqe ) {
//			if ( priv->eq_table.eq[i].isr_cons_index > priv->eq_table.eq[i].cons_index )
//				continue;
//			priv->eq_table.eq[i].isr_cons_index = priv->eq_table.eq[i].cons_index + 1;
			/* another interrupt may happen instantly after writel above.
			If it comes to another processor, mlx4_interrupt will be called
			and try to schedule the same DPC. So we protect KeInsertQueueDpc
			from that race */

			while(InterlockedCompareExchange(&dev->pdev->dpc_lock, 1, 0));
			int ret = mlx4_process_eqes(&priv->eq_table.eq[i]);
			work |= ret;
			InterlockedCompareExchange(&dev->pdev->dpc_lock, 0, 1);
		} else {
			eq= &priv->eq_table.eq[i];
			
			dbg_inx = eq->dbg_inx & 0x3FF;
			eq->debug[dbg_inx].cons_index_in_call = eq->cons_index;
			eq->no_of_empty_eq++;			 
			
			/* re-arm the EQ for a case when interrupt comes before EQE
			and we didn't scheduled the DPC */
			eq_set_ci(&priv->eq_table.eq[i], 1);
		}
	}

	return (BOOLEAN)work;
}

#ifdef NTDDI_WIN8
static KSERVICE_ROUTINE mlx4_interrupt;
#endif
static BOOLEAN mlx4_interrupt(
	IN struct _KINTERRUPT *Interrupt,
	IN PVOID ServiceContext
	)
{
	struct mlx4_dev *dev = (mlx4_dev *)ServiceContext;
	struct mlx4_priv *priv = mlx4_priv(dev);
	BOOLEAN fRet, fRes;
	static int new_mode = TRUE;
	
	UNUSED_PARAM(Interrupt);

	writel(priv->eq_table.clr_mask, priv->eq_table.clr_int);
	fRet = legacy_isr(dev);

/*
	ISR always returns FALSE fro Flex10 chips.
	It will make OS to call ISRs of all Flex10 devices.
	This inefficient solution is a workaround of a problem, descrived below.

	Problem:
		Interrupts of PFi get lost.

	Explanation:	
		The problem arises from the fact, that driver doesn't know, which function 
		has made an interrupt. 
		The interrupt get lost in the following scenario:
			- PF makes an interrupt;
			- PPF puts a new EQE into it's EQ, but doesn't raise 
			  an interrupt, because PF has already done it;
			- OS gets the interrupt and calls first PPFs ISR;

		This causes the following:
			1. PPF's ISR grounds the interrupt line;
			2. then it finds EQE, schedules DPC and return TRUE;
			3. OS decides that interrupt is handled and doesn't call PF's ISR;
			4. the interrupt line is still HIGH, so OS gets interrupt and calls PPF's ISR;

		and we comes once more to 1., i.e having an endless interrupt loop.
		The loop freezes only one CPU.
		On some other CPU PFi is waiting for the completion of its command
		and fails with TIMEOUT in 10 seconds.
*/	
	if (new_mode)
		fRes = (mlx4_is_mfunc(dev) ? FALSE : fRet);
	else
		fRes = fRet;
	return fRes;
}

#ifdef NTDDI_WIN8
static KDEFERRED_ROUTINE mlx4_dpc_msix;
#endif
static void mlx4_dpc_msix( PRKDPC dpc, 
	PVOID ctx, PVOID arg1, PVOID arg2 )
{
	struct mlx4_eq *eq	= (mlx4_eq *)ctx;
	uint64_t dpc_time = get_tickcount_in_ms();
	static BOOLEAN fIgnoreAssert = TRUE;

	UNREFERENCED_PARAMETER(dpc);
	UNREFERENCED_PARAMETER(arg1);
	UNREFERENCED_PARAMETER(arg2);
	UNREFERENCED_PARAMETER(dpc_time);

#if 0	 
	if ((dpc_time - eq->interrupt_time) > 500){
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_EQ,("%s: Calling to interrupt DPC took too much time (%I64d ms)\n", 
			eq->dev->pdev->name, (dpc_time - eq->interrupt_time)));
		ASSERT(fIgnoreAssert);
	}
#endif

	mlx4_eq_int(eq->dev, eq);
}


#if 1//WORKAROUND_POLL_EQ

#ifdef NTDDI_WIN8
KSYNCHRONIZE_ROUTINE IsrSynchronizeRoutine;
#endif
BOOLEAN
IsrSynchronizeRoutine(
	IN PVOID  SynchronizeContext
	)
{
	struct mlx4_dev *dev = (struct mlx4_dev *)SynchronizeContext;
	
	mlx4_interrupt(dev->pdev->int_obj,dev);
	
	return TRUE;
}

#ifdef NTDDI_WIN8
KSTART_ROUTINE eq_polling_thread;
#endif
VOID eq_polling_thread(void *ctx) 
{
#define POLLING_INTERVAL_MS	50
	NTSTATUS status;
	struct mlx4_priv *priv = (struct mlx4_priv *)ctx;
	PVOID wait_objects[2];
	LARGE_INTEGER  wait_time;

	wait_objects[0] = &priv->eq_table.thread_stop_event;
	wait_objects[1] = &priv->eq_table.thread_start_event;

	for(;;){

		/* before start polling */
		for (;;) {
			status = KeWaitForMultipleObjects( 2, wait_objects, 
											   WaitAny, Executive, KernelMode, FALSE, NULL, NULL );

			if ( status == STATUS_WAIT_0 ){/* thread stopped */
				break;		
			}

			/* start polling */
			if ( status == STATUS_WAIT_1 ){
				break;		
			}

		}

		if(priv->eq_table.bTerminated) break;
		if ( status == STATUS_WAIT_0 ) continue;/* thread stopped, wait for start again */

		/* polling */
		wait_time.QuadPart = -(int64_t)(((uint64_t)POLLING_INTERVAL_MS) * 10000);
		for (;;) {
			//mlx4_interrupt( NULL, &priv->dev );
			KeSynchronizeExecution(priv->dev.pdev->int_obj, IsrSynchronizeRoutine, &priv->dev);

			status = KeWaitForSingleObject( &priv->eq_table.thread_stop_event, 
											Executive, KernelMode, FALSE, &wait_time );
			if ( status == STATUS_SUCCESS ) {
				//KeClearEvent(&priv->eq_table.thread_stop_event);
				break;		/* thread stopped */
			}
		}

		if(priv->eq_table.bTerminated) break;
	}

	PsTerminateSystemThread(STATUS_SUCCESS);

}


void mlx4_poll_eq(struct ib_device *device, BOOLEAN bStart)
{
	LONG signalled=0;
	struct mlx4_priv *priv = mlx4_priv(device->dma_device);

	if(bStart){
		/* signal start of polling */
		signalled = KeSetEvent( 	
			&priv->eq_table.thread_start_event, IO_NO_INCREMENT, FALSE );
	}else{
		/* signal end of polling */
		signalled = KeSetEvent( 	
			&priv->eq_table.thread_stop_event, IO_NO_INCREMENT, FALSE );
	}

}

#endif

#ifdef NTDDI_WIN8
KMESSAGE_SERVICE_ROUTINE mlx4_msi_x_interrupt;
#endif
BOOLEAN
mlx4_msi_x_interrupt (
	IN PKINTERRUPT	Interrupt,
	IN PVOID  ServiceContext,
	IN ULONG  MessageId 
	)
{
	struct mlx4_eq *eq;
	struct mlx4_priv *priv;
	struct mlx4_dev *dev = (mlx4_dev *)ServiceContext;
	PMLX4_ST_DEVICE p_stat = dev->pdev->p_stat;
 	struct mlx4_eqe *eqe;
	int i = 0;
	
	UNUSED_PARAM(Interrupt);

	p_stat->isr_n_total++;
	
	// Check, that it is our interrupt
	if ( dev->signature != MLX4_DEV_SIGNATURE)
		return FALSE;
	p_stat->isr_n_calls++;

	if ( mlx4_is_msi(dev) ) {
		// MSI-X mode
		priv = mlx4_priv(dev);
		// Optimize case when there is no lack of MSI-X vectors and each EQ has its own vector
		if(priv->msix_eq_mapping.max_num_eqs_per_vector == 1)
		{
			eq = priv->msix_eq_mapping.eqs[MessageId][0];
			eqe = next_eqe_sw(eq);
			if ( eqe ) {
				mlx4_process_eqes(eq);
			}
			else {
				int dbg_inx = eq->dbg_inx & 0x3FF;
				eq->debug[dbg_inx].cons_index_in_call = eq->cons_index;
				eq->no_of_empty_eq++;						 
				
				eq_set_ci(eq, 1);
			}
		}
		else
		{
			for (i = 0; i < priv->msix_eq_mapping.max_num_eqs_per_vector; ++i) {
				eq = priv->msix_eq_mapping.eqs[MessageId][i];
				if(eq == NULL)
				{
					continue;
				}
				eqe = next_eqe_sw(eq);
				if ( eqe ) {
					mlx4_process_eqes(eq);
				}
				else {
					int dbg_inx = eq->dbg_inx & 0x3FF;
					eq->debug[dbg_inx].cons_index_in_call = eq->cons_index;
					eq->no_of_empty_eq++;						 
					
					eq_set_ci(eq, 1);
				}
			}
		}
		return TRUE;
	}

	// legacy mode
	return mlx4_interrupt(NULL,dev);
}


int mlx4_MAP_EQ_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
				   struct mlx4_cmd_mailbox *inbox,
				   struct mlx4_cmd_mailbox *outbox)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct slave_event_eq_info* event_eq = &priv->mfunc.master.slave_state[slave].event_eq;
	u32 in_modifier = vhcr->in_modifier;
	u32 eqn = in_modifier & 0x1FF;	
	u64 in_param =	vhcr->in_param;

	UNUSED_PARAM(inbox);
	UNUSED_PARAM(outbox);

	if (in_modifier >> 31)
	{
		// unmap
		event_eq->event_type &= ~in_param;
		return 0;
	}

	event_eq->eqn = eqn;
	event_eq->event_type = in_param;

	return 0;
}

static int mlx4_MAP_EQ(struct mlx4_dev *dev, u64 event_mask, int unmap,
			int eq_num)
{
	return mlx4_cmd(dev, event_mask, (unmap << 31) | eq_num,
			0, MLX4_CMD_MAP_EQ, MLX4_CMD_TIME_CLASS_B);
}

static int mlx4_SW2HW_EQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int eq_num)
{
	return mlx4_cmd(dev, mailbox->dma.da | dev->caps.function, eq_num, 0, 
			MLX4_CMD_SW2HW_EQ, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_HW2SW_EQ(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox,
			 int eq_num)
{
	return mlx4_cmd_box(dev, dev->caps.function, mailbox->dma.da, eq_num, 0,
				MLX4_CMD_HW2SW_EQ, MLX4_CMD_TIME_CLASS_A);
}

static int mlx4_num_eq_uar(struct mlx4_dev *dev)
{
	/*
	 * Each UAR holds 4 EQ doorbells.  To figure out how many UARs
	 * we need to map, take the difference of highest index and
	 * the lowest index we'll use and add 1.
	 */
	return (mlx4_priv( dev )->eq_table.num_eqs + 1 + dev->caps.reserved_eqs) / 4 -
		dev->caps.reserved_eqs / 4 + 1;
}


static void __iomem *mlx4_get_eq_uar(struct mlx4_dev *dev, struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int index;
	io_addr_t addr;

	index = eq->eqn / 4 - dev->caps.reserved_eqs / 4;

	if (!priv->eq_table.uar_map[index]) {
		addr = pci_resource_start(dev->pdev, 2) + ((eq->eqn / 4) << PAGE_SHIFT);
		priv->eq_table.uar_map[index] = (u8*)ioremap( addr, PAGE_SIZE, MmNonCached);
		if (!priv->eq_table.uar_map[index]) {
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                ("%s: Couldn't map EQ doorbell for EQN 0x%06x\n", dev->pdev->name, eq->eqn));
			return NULL;
		}
		else
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: EQ doorbell for EQN 0x%06x mapped to va %p (pa %I64x) \n",
				dev->pdev->name, eq->eqn, priv->eq_table.uar_map[index], addr));
	}

	return priv->eq_table.uar_map[index] + 0x800 + 8 * (eq->eqn % 4);
}

static int mlx4_create_eq(struct mlx4_dev *dev, int nent,
		int eq_ix, struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_eq_context *eq_context;
	int npages;
	u64 *dma_list = NULL;
	dma_addr_t t;
	u64 mtt_addr;
	int err = -ENOMEM;
	int i;

	eq->valid = 0;
	eq->eq_ix = eq_ix;
	eq->dev   = dev;
	eq->nent  = roundup_pow_of_two(max(nent, 2));
	npages = (int)(NEXT_PAGE_ALIGN(eq->nent * MLX4_EQ_ENTRY_SIZE) / PAGE_SIZE);

	eq->page_list = (mlx4_buf_list *)kmalloc(npages * sizeof *eq->page_list,
				GFP_KERNEL);
	if (!eq->page_list)
		goto err_malloc;

	for (i = 0; i < npages; ++i)
		eq->page_list[i].buf = NULL;

	dma_list = (u64*)kmalloc(npages * sizeof *dma_list, GFP_KERNEL);
	if (!dma_list)
		goto err_out_free;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		goto err_out_free;
	eq_context = (struct mlx4_eq_context *)mailbox->buf;

	for (i = 0; i < npages; ++i) {
		eq->page_list[i].buf = (u8*)dma_alloc_coherent(&dev->pdev->dev,
							  PAGE_SIZE, &t, GFP_KERNEL);
		if (!eq->page_list[i].buf)
			goto err_out_free_pages;

		dma_list[i] = t.da;
		eq->page_list[i].map = t;

		memset(eq->page_list[i].buf, 0, PAGE_SIZE);
	}

	eq->eqn = mlx4_bitmap_alloc(&priv->eq_table.bitmap);
	if (eq->eqn == -1)
		goto err_out_free_pages;

	eq->doorbell = mlx4_get_eq_uar(dev, eq);
	if (!eq->doorbell) {
		err = -ENOMEM;
		goto err_out_free_eq;
	}

	err = mlx4_mtt_init(dev, npages, PAGE_SHIFT, &eq->mtt);
	if (err)
		goto err_out_free_eq;

	err = mlx4_write_mtt(dev, &eq->mtt, 0, npages, dma_list);
	if (err)
		goto err_out_free_mtt;

	memset(eq_context, 0, sizeof *eq_context);
	eq_context->flags	  = cpu_to_be32(MLX4_EQ_STATUS_OK	|
						MLX4_EQ_STATE_ARMED);
	eq_context->log_eq_size	  = (u8)ilog2(eq->nent);
	eq_context->intr	  = (u8)eq->irq;
	eq_context->log_page_size = PAGE_SHIFT - MLX4_ICM_PAGE_SHIFT;

	mtt_addr = mlx4_mtt_addr(dev, &eq->mtt);
	eq_context->mtt_base_addr_h = (u8)(mtt_addr >> 32);
	eq_context->mtt_base_addr_l = cpu_to_be32(mtt_addr & 0xffffffff);

	err = mlx4_SW2HW_EQ(dev, mailbox, eq->eqn);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: SW2HW_EQ failed (%d)\n", dev->pdev->name, err));
		goto err_out_free_mtt;
	}

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: EQ created: EQN %d, size %d(%d), MSI ID %d, CPU %#x\n", 
		dev->pdev->name, eq->eqn, nent, eq->nent, eq->irq, (int)eq->cpu ));

	kfree(dma_list);
	mlx4_free_cmd_mailbox(dev, mailbox);

	eq->cons_index = 0;
	eq->valid = 1;
	
	spin_lock_init(&eq->cq_lock);
	INIT_RADIX_TREE(&eq->cq_tree, GFP_ATOMIC);

	return 0;

err_out_free_mtt:
	mlx4_mtt_cleanup(dev, &eq->mtt);

err_out_free_eq:
	mlx4_bitmap_free(&priv->eq_table.bitmap, eq->eqn);

err_out_free_pages:
	for (i = 0; i < npages; ++i)
		if (eq->page_list[i].buf)
			dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
					  eq->page_list[i].buf,
					  eq->page_list[i].map);

	mlx4_free_cmd_mailbox(dev, mailbox);

err_out_free:
	kfree(eq->page_list);
	kfree(dma_list);

err_malloc:
	return err;
}

static void mlx4_free_eq(struct mlx4_dev *dev,
			 struct mlx4_eq *eq)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_cmd_mailbox *mailbox;
	int err;
	int npages = (int)(NEXT_PAGE_ALIGN(MLX4_EQ_ENTRY_SIZE * eq->nent) / PAGE_SIZE);
	int i;

	if (!eq->valid)
		return;
	eq->valid = 0;
	
	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) 
		mailbox = NULL;
	else {
		err = mlx4_HW2SW_EQ(dev, mailbox, eq->eqn);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: HW2SW_EQ failed (%d)\n", dev->pdev->name, err));
	}

#if 0
	{
		mlx4_dbg(dev, "%s: Dumping EQ context %02x:\n", dev->pdev->name, eq->eqn);
		for (i = 0; i < sizeof (struct mlx4_eq_context) / 4; ++i) {
			if (i % 4 == 0)
				printk("[%02x] ", i * 4);
			printk(" %08x", be32_to_cpup(mailbox->buf + i * 4));
			if ((i + 1) % 4 == 0)
				printk("\n");
		}
	}
#endif	

	mlx4_mtt_cleanup(dev, &eq->mtt);
	for (i = 0; i < npages; ++i)
		pci_free_consistent(dev->pdev, PAGE_SIZE,
					eq->page_list[i].buf,
					eq->page_list[i].map);

	kfree(eq->page_list);
	mlx4_bitmap_free(&priv->eq_table.bitmap, eq->eqn);
	mlx4_free_cmd_mailbox(dev, mailbox);

	RMV_RADIX_TREE(&eq->cq_tree);
}

static void mlx4_free_irqs(struct mlx4_dev *dev)
{
	struct mlx4_eq_table *eq_table = &mlx4_priv(dev)->eq_table;
	
	if (eq_table->have_irq)
		free_irq(dev);
}

static int mlx4_map_clr_int(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->clr_base = (u8*)ioremap(pci_resource_start(dev->pdev, priv->fw.clr_int_bar) +
				 priv->fw.clr_int_base, MLX4_CLR_INT_SIZE, MmNonCached);
	if (!priv->clr_base) {
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Couldn't map interrupt clear register, aborting.\n", dev->pdev->name));
		return -ENOMEM;
	}

	return 0;
}

static void mlx4_unmap_clr_int(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (priv->clr_base) 
		iounmap(priv->clr_base, MLX4_CLR_INT_SIZE);
}

int mlx4_affinity_to_cpu_number(KAFFINITY *affinity, u8 *cpu_num)
{
    unsigned long cpu_number;
    unsigned long cpu_number_in_group;
	
    if(affinity == NULL || *affinity == 0)
    {
        return -EINVAL;
    }
    
#if _M_IX86
    _BitScanForward(&cpu_number_in_group, *affinity);
#else
    _BitScanForward64(&cpu_number_in_group, *affinity);
#endif

#if WINVER >= 0x0601	
    PROCESSOR_NUMBER proc_number = {0};

    proc_number.Group = 0;
    proc_number.Number = (UCHAR) cpu_number_in_group;

    cpu_number = KeGetProcessorIndexFromNumber(&proc_number);
#else
	cpu_number = cpu_number_in_group;
#endif

	if(cpu_num)
	{
		*cpu_num = (u8) cpu_number;
	}
    return 0;     
}

int mlx4_alloc_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	priv->eq_table.eq = (mlx4_eq *)kcalloc(dev->caps.num_eqs - dev->caps.reserved_eqs,
					sizeof *priv->eq_table.eq, GFP_KERNEL);
	if (!priv->eq_table.eq)
		return -ENOMEM;

	return 0;
}

void mlx4_free_eq_table(struct mlx4_dev *dev)
{
	kfree(mlx4_priv(dev)->eq_table.eq);
}

int mlx4_init_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	int i, j;
	ULONG cpu_num;
	struct mlx4_eq *eq;
	
	priv->eq_table.uar_map = (u8**)kcalloc(sizeof *priv->eq_table.uar_map,
					 mlx4_num_eq_uar(dev), GFP_KERNEL);
	if (!priv->eq_table.uar_map) {
		err = -ENOMEM;
		goto exit;
	}

	err = mlx4_bitmap_init_no_mask(&priv->eq_table.bitmap, dev->caps.num_eqs,
		dev->caps.reserved_eqs, 0);
	if (err)
		goto err_bitmap_init;

	for (i = 0; i < mlx4_num_eq_uar(dev); ++i)
		priv->eq_table.uar_map[i] = NULL;

	err = mlx4_map_clr_int(dev);
	if (err)
		goto err_map_clr_in;

	priv->eq_table.clr_mask =
		swab32(1 << (priv->eq_table.inta_pin & 31));
	priv->eq_table.clr_int	= priv->clr_base +
		(priv->eq_table.inta_pin < 32 ? 4 : 0);

	/* init DPC stuff */
	dev->pdev->dpc_lock = 0;
	priv->msix_eq_mapping.num_msix_vectors = dev->pdev->n_msi_vectors_alloc;
	priv->msix_eq_mapping.max_num_eqs_per_vector = 
		DIV_ROUND_UP(priv->eq_table.num_eqs, dev->pdev->n_msi_vectors_alloc);
	priv->msix_eq_mapping.eqs = 
		(struct mlx4_eq ***) kcalloc(priv->msix_eq_mapping.num_msix_vectors,
					 sizeof(struct mlx4_eq **), GFP_KERNEL);
	if(priv->msix_eq_mapping.eqs == NULL)
	{
		goto err_msix_map_alloc;
	}
	
	for (i = 0; i < priv->msix_eq_mapping.num_msix_vectors; ++i) {
		priv->msix_eq_mapping.eqs[i] = (struct mlx4_eq **) kcalloc(priv->msix_eq_mapping.max_num_eqs_per_vector,
					 sizeof(struct mlx4_eq *), GFP_KERNEL);
		if(priv->msix_eq_mapping.eqs[i] == NULL)
		{
			goto err_msix_map_alloc;
		}
	}
	
	for (i = 0; i < priv->eq_table.num_eqs; ++i) {
		eq = &priv->eq_table.eq[i];
		spin_lock_init( &eq->lock );	
		KeInitializeDpc( &eq->dpc, 
			mlx4_is_msi(dev) ? mlx4_dpc_msix : mlx4_dpc, 
			eq);
		eq->irq = (u16) (i % dev->pdev->n_msi_vectors_alloc);
		priv->msix_eq_mapping.eqs[eq->irq][i / dev->pdev->n_msi_vectors_alloc] = eq;
	}
	
	for (i = 0; i < priv->eq_table.num_eth_eqs; ++i) {
		err = mlx4_create_eq(dev, dev->caps.num_cqs -
					 dev->caps.reserved_cqs +
					 MLX4_NUM_SPARE_EQE,
					 mlx4_is_msi(dev) ? i : 0,
					 &priv->eq_table.eq[i]);
		if (err)
			goto err_create_eq;
	}

	err = mlx4_create_eq(dev, MLX4_NUM_ASYNC_EQE + MLX4_NUM_SPARE_EQE,
				 mlx4_is_msi(dev) ? priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS : 0,
				 &priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS]);
	if (err)
		goto err_out_comp;

	err = mlx4_create_eq(dev, MLX4_NUM_ASYNC_EQE + MLX4_NUM_SPARE_EQE,
				 mlx4_is_msi(dev) ? priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP : 0,
				 &priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP]);
	if (err)
		goto err_out_commands;

	for (i = priv->eq_table.num_eth_eqs + MLX4_NUM_ETH_EQ; i < priv->eq_table.num_eqs; ++i) {
		err = mlx4_create_eq(dev, dev->caps.num_cqs -
					 dev->caps.reserved_cqs +
					 MLX4_NUM_SPARE_EQE,
					 mlx4_is_msi(dev) ? i : 0,
					 &priv->eq_table.eq[i]);
		if (err)
			goto err_create_eq;
	}

	// connect interrupts
#ifdef USE_WDM_INTERRUPTS
	err = request_irq( dev, mlx4_interrupt, dev, 
		mlx4_msi_x_interrupt, &dev->pdev->int_obj );
	if (err)
		goto err_out_commands;

	// Assign EQ affinity (DPC affinity).
	// TODO: Need to revise this code when adding support for group > 0
	if ( mlx4_is_msi(dev) )
	{
		// Assign affinity of EQs per MSI-X vectors
		for (i = 0; i < min(priv->eq_table.num_eqs, dev->pdev->n_msi_vectors_alloc); ++i) {
			struct mlx4_eq *eq = &priv->eq_table.eq[i];
			eq->cpu = dev->pdev->p_msix_map[i].cpu;
			eq->have_irq = 1;
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: MSI-X EQ%02d: EQN %d, size %d, MSI ID %d, CPU %#x\n", 
				dev->pdev->name, i, eq->eqn, eq->nent, eq->irq, (int)eq->cpu ));
		}

		// Assign affinity of EQs to CPUs that were not assigned an MSI-X vector
		j = i;
		for(cpu_num = 0; cpu_num < num_possible_cpus(); cpu_num++)
		{
			int eq_found = FALSE;
			
			for(i = 0; i < min(priv->eq_table.num_eqs, dev->pdev->n_msi_vectors_alloc); i++)
			{
				struct mlx4_eq *eq = &priv->eq_table.eq[i];
				if(eq->cpu == (((KAFFINITY) 1) << cpu_num))
				{
					eq_found = TRUE;
					break;
				}
			}
			if(eq_found)
			{// eq for cpu was found
				continue;
			}
			// eq for cpu was not found - use free eq
			if(j < priv->eq_table.num_eqs)
			{
				struct mlx4_eq *eq = &priv->eq_table.eq[j];
				eq->cpu = ((KAFFINITY) 1) << cpu_num;
				j++;
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: MSI-X EQ%02d: EQN %d, size %d, MSI ID %d, CPU %#x\n", 
					dev->pdev->name, i, eq->eqn, eq->nent, eq->irq, (int)eq->cpu ));
			}
		}

		// Assign affinity of remaining EQs, if there are more EQs than CPUs, according to MSI-X vectors
		if(j != priv->eq_table.num_eqs - 1)
		{
			for(i = j; i < priv->eq_table.num_eqs; i++)
			{
				struct mlx4_eq *eq = &priv->eq_table.eq[j];
				eq->cpu = dev->pdev->p_msix_map[eq->irq].cpu;
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: MSI-X EQ%02d: EQN %d, size %d, MSI ID %d, CPU %#x\n", 
					dev->pdev->name, i, eq->eqn, eq->nent, eq->irq, (int)eq->cpu ));
			}
		}
	}

	priv->eq_table.have_irq = 1;

#else
	// not implemented - prevent compilation
	#error Interrupts in WDF style are not implemented !
	goto err_out_async;
#endif		

	for (i = 0; i < priv->eq_table.num_eqs; ++i)
	{
		u8 cpu_num = 0;
		int err = mlx4_affinity_to_cpu_number(&priv->eq_table.eq[i].cpu, &cpu_num);
		if(! err)
		{
			KeSetTargetProcessorDpc(&priv->eq_table.eq[i].dpc, cpu_num);
		}
	}

	err = mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
			  priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS ].eqn);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: MAP_EQ for async EQ %d failed (%d)\n",
			dev->pdev->name, 
			   priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS].eqn, err));
		goto err_map_eq;
	}
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Commands and async events are mapped to EQ %d\n",
		dev->pdev->name, priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS].eqn));


	for (i = 0; i < priv->eq_table.num_eqs; ++i)
		eq_set_ci(&priv->eq_table.eq[i], 1);

#if 1//WORKAROUND_POLL_EQ
	if ( !mlx4_is_mfunc(dev) )
	{ /* Create a thread for polling EQs in case of missing interrupts from the card  */
		NTSTATUS status;
		OBJECT_ATTRIBUTES	attr;
		HANDLE	handle;

		KeInitializeEvent(&priv->eq_table.thread_start_event, SynchronizationEvent, FALSE);
		KeInitializeEvent(&priv->eq_table.thread_stop_event, SynchronizationEvent, FALSE);
		priv->eq_table.bTerminated = FALSE;
		priv->eq_table.threadObject = NULL;
		InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );
		status = PsCreateSystemThread( &handle, 
			THREAD_ALL_ACCESS, &attr, NULL, NULL, eq_polling_thread, priv );
		if (NT_SUCCESS(status)) {
			status = ObReferenceObjectByHandle(
				handle,
				THREAD_ALL_ACCESS,
				NULL,
				KernelMode,
				&priv->eq_table.threadObject,
				NULL
				);

			ASSERT(status == STATUS_SUCCESS); //

			status = ZwClose(handle);

			ASSERT(NT_SUCCESS(status)); // Should always succeed

		}
	}
#endif

	return 0;

err_map_eq:
	mlx4_free_irqs(dev);
	free_irq(dev);


err_out_commands:
	mlx4_free_eq(dev, &priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP]);

err_out_comp:
	i = priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS;

err_create_eq:
err_msix_map_alloc:
	while (i > 0) {
		--i;
		mlx4_free_eq(dev, &priv->eq_table.eq[i]);
	}
	mlx4_unmap_clr_int(dev);

	if(priv->msix_eq_mapping.eqs)
	{
		for(j = 0; j < priv->msix_eq_mapping.num_msix_vectors; j++)
		{
			if(priv->msix_eq_mapping.eqs[j])
			{
				kfree(priv->msix_eq_mapping.eqs[j]);
				priv->msix_eq_mapping.eqs[j] = NULL;
			}
		}
		kfree(priv->msix_eq_mapping.eqs);
		priv->msix_eq_mapping.eqs = NULL;
	}
	
err_map_clr_in:
	mlx4_bitmap_cleanup(&priv->eq_table.bitmap);

err_bitmap_init:	
	kfree( priv->eq_table.uar_map );
	priv->eq_table.uar_map = NULL;
	
exit:	
	return err;
}

int mlx4_add_eq(struct mlx4_dev *dev, int nent,
	KAFFINITY cpu, u8* p_eq_num, struct mlx4_eq ** p_eq)
{
#define DUMMY_EQ_NUM		0xff
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	u8 new_eq = DUMMY_EQ_NUM;

	UNUSED_PARAM(nent);

	if ( mlx4_is_barred(dev) )
		return -EFAULT;

	for (i = 0; i < priv->eq_table.num_eth_eqs; i++) {

		if ( !cpu || (cpu & priv->eq_table.eq[i].cpu) ) {
			new_eq = (u8)i;
			break;
		}
	}
	if (new_eq == DUMMY_EQ_NUM) {
		new_eq = 0;
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
            ("%s: mlx4_add_eq failed to find requested_cpu %x. Allocated default eq %d. (num_eth_eqs %d)\n", 
			dev->pdev->name, (int)cpu, new_eq, priv->eq_table.num_eth_eqs ));
	}

	*p_eq = &priv->eq_table.eq[new_eq];
	*p_eq_num = new_eq;
	MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: returned EQ %02d (EQN %d) for CPU %#x (eq cpu %#x)\n", 
		dev->pdev->name, new_eq, priv->eq_table.eq[new_eq].eqn, (int)cpu, (int)priv->eq_table.eq[new_eq].cpu ));
	return 0;
}

void mlx4_remove_eq(struct mlx4_dev *dev, u8 eq_num)
{
	struct mlx4_eq_table *eq_table = &mlx4_priv(dev)->eq_table;

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: mlx4_remove_eq: removed EQ %02d (EQN %d)\n", 
		dev->pdev->name, eq_num, eq_table->eq[eq_num].eqn ));
}

void mlx4_cleanup_eq_table(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;

#if 1//WORKAROUND_POLL_EQ
	/* stop the EQ polling thread */
	if (priv->eq_table.threadObject) { 
		NTSTATUS status;
		LONG signalled;

		priv->eq_table.bTerminated = TRUE;

		/* signal polling stopped in case it is not */
		signalled = KeSetEvent(
			&priv->eq_table.thread_stop_event, IO_NO_INCREMENT, FALSE );

		/* wait for completion of the thread */
		status = KeWaitForSingleObject( priv->eq_table.threadObject,
			Executive, KernelMode, FALSE, NULL );
		ASSERT(status == STATUS_SUCCESS);

		ObDereferenceObject(priv->eq_table.threadObject);

		/* cleanup */
		priv->eq_table.threadObject = NULL;
	}
#endif

	mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 1,
			priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_IB_COMP].eqn);

	mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 1,
			priv->eq_table.eq[priv->eq_table.num_eth_eqs + MLX4_EQ_COMMANDS].eqn);

	mlx4_free_irqs(dev);

	for (i = 0; i < priv->eq_table.num_eqs; ++i)
		mlx4_free_eq(dev, &priv->eq_table.eq[i]);

	mlx4_unmap_clr_int(dev);

	for (i = 0; i < mlx4_num_eq_uar(dev); ++i)
		if (priv->eq_table.uar_map[i])
			iounmap(priv->eq_table.uar_map[i],PAGE_SIZE);

	mlx4_bitmap_cleanup(&priv->eq_table.bitmap);

	kfree(priv->eq_table.uar_map);

	if(priv->msix_eq_mapping.eqs)
	{		
		for (i = 0; i < priv->msix_eq_mapping.num_msix_vectors; ++i) {
			if(priv->msix_eq_mapping.eqs[i])
			{
				kfree(priv->msix_eq_mapping.eqs[i]);
				priv->msix_eq_mapping.eqs[i] = NULL;
			}
		}
		kfree(priv->msix_eq_mapping.eqs);
		priv->msix_eq_mapping.eqs = NULL;
	}
}

/* A test that verifies that we can accept interrupts on all
 * the irq vectors of the device.
 * Interrupts are checked using the NOP command.
 */
int mlx4_test_interrupts(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int i;
	int err;

	err = mlx4_NOP(dev);
	/* When not in MSI_X, there is only one irq to check */
	if ( !mlx4_is_msi(dev) )
		return err;

	/* A loop over all completion vectors, for each vector we will check
	 * whether it works by mapping command completions to that vector
	 * and performing a NOP command
	 */
	for(i = 0; !err && (i < priv->eq_table.num_eth_eqs); ++i) {
		/* Temporary use polling for command completions */
		mlx4_cmd_use_polling(dev);

		/* Map the new eq to handle all asyncronous events */
		err = mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
				  priv->eq_table.eq[i].eqn);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed mapping eq for interrupt test\n", dev->pdev->name));
			mlx4_cmd_use_events(dev);
			break;
		}

		/* Go back to using events */
		mlx4_cmd_use_events(dev);
		err = mlx4_NOP(dev);
	}

	/* Return to default */
	mlx4_MAP_EQ(dev, MLX4_ASYNC_EVENT_MASK, 0,
			priv->eq_table.eq[priv->eq_table.num_eth_eqs].eqn);
	return err;
}
EXPORT_SYMBOL(mlx4_test_interrupts);

#ifdef USE_WDM_INTERRUPTS

void free_irq(struct mlx4_dev *dev)
{
	if (!dev->pdev->int_obj)
		return;
	
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
	// Vista build environment
	if (dev->pdev->legacy_connect)
		IoDisconnectInterrupt( dev->pdev->int_obj );
	else {
		IO_DISCONNECT_INTERRUPT_PARAMETERS ctx;
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
			("%s: IoDisconnectInterrupt: Version %d\n", dev->pdev->name, dev->pdev->version)); 
		ctx.Version = dev->pdev->version;
		ctx.ConnectionContext.InterruptObject = dev->pdev->int_obj;
		IoDisconnectInterruptEx( &ctx );
	}

#else
	// legacy build environment
	IoDisconnectInterrupt( dev->pdev->int_obj );
#endif
	dev->pdev->int_obj = NULL;
}


int request_irq(
	IN		struct mlx4_dev *	dev,		
	IN		PKSERVICE_ROUTINE	isr,			/* Line ISR */
	IN		PVOID				isr_ctx,		/* ISR context */
	IN		PKMESSAGE_SERVICE_ROUTINE	misr,	/* Message ISR */
	OUT		PKINTERRUPT		*	int_obj
	)
{
	NTSTATUS status;
	struct pci_dev *pdev = dev->pdev;		/* interrupt resources */

#if (NTDDI_VERSION >= NTDDI_LONGHORN)

	IO_CONNECT_INTERRUPT_PARAMETERS params;
	PIO_INTERRUPT_MESSAGE_INFO p_msi_info;

	KeInitializeSpinLock( &pdev->isr_lock );
	pdev->n_msi_vectors = 0;  // not using MSI/MSI-X

	//
	// Vista and later platforms build environment
	//

	RtlZeroMemory( &params, sizeof(IO_CONNECT_INTERRUPT_PARAMETERS) );
	if ( !mlx4_is_msi(dev) ) {
		params.Version = CONNECT_FULLY_SPECIFIED;
		goto get_legacy_int;
	}
		
	//
	// try to connect our Interrupt Message Service Rotuine to
	// all Message-Signaled Interrupts our device has been granted,
	// with automatic fallback to a single line-based interrupt.
	//
	
	params.Version = CONNECT_MESSAGE_BASED;
	params.MessageBased.PhysicalDeviceObject = pdev->pdo;
	params.MessageBased.ConnectionContext.Generic = (PVOID *)(&p_msi_info);
	params.MessageBased.MessageServiceRoutine = misr;
	params.MessageBased.ServiceContext = isr_ctx;
	params.MessageBased.SpinLock = NULL;
	params.MessageBased.SynchronizeIrql = 0;
	params.MessageBased.FloatingSave = FALSE;
	// fallback to line-based ISR if there is no MSI support
	params.MessageBased.FallBackServiceRoutine = isr;
	
	status = IoConnectInterruptEx(&params);

	pdev->version = params.Version;
	*int_obj = (PKINTERRUPT)p_msi_info;
	
	if ( NT_SUCCESS(status) ) {
	
		//
		// It worked, so we're running on Vista or later.
		//
	
		if(params.Version == CONNECT_MESSAGE_BASED) {
			ULONG i;
		
			//
			// Because we succeeded in connecting to one or more Message-Signaled
			// Interrupts, the connection context that was returned was
			// a pointer to an IO_INTERRUPT_MESSAGE_INFO structure.
			//
			pdev->n_msi_vectors = (u8)p_msi_info->MessageCount;  // not using MSI/MSI-X
			// print it 
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
				("%s: request_irq: Granted %d MSI vectors ( UnifiedIrql %#x)\n", 
				dev->pdev->name, p_msi_info->MessageCount, p_msi_info->UnifiedIrql ));
			for (i=0; i < p_msi_info->MessageCount; ++i) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_MSIX,
					("%s: *** Vector %#x, Affinity %#x, Irql %#x, MsgAddr %I64x, MsgData %#x, Mode %d\n", 
					dev->pdev->name, 
					p_msi_info->MessageInfo[i].Vector,
					(ULONG)p_msi_info->MessageInfo[i].TargetProcessorSet,
					p_msi_info->MessageInfo[i].Irql,
					p_msi_info->MessageInfo[i].MessageAddress.QuadPart,
					p_msi_info->MessageInfo[i].MessageData,
					p_msi_info->MessageInfo[i].Mode ));
			}

			// sanity check
			if (pdev->n_msi_vectors_alloc != pdev->n_msi_vectors) {
				MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_INIT ,
					("%s: Connected to %d interrupts from %d allocated to us !!!\n",
					dev->pdev->name, pdev->n_msi_vectors, pdev->n_msi_vectors_alloc ));
			}

			// fill MSI-X map table
			for (i=0; i < p_msi_info->MessageCount; ++i) {
				pdev->p_msix_map[i].cpu = p_msi_info->MessageInfo[i].TargetProcessorSet;
			}

		} else {
			//
			// We are on Vista, but there is no HW MSI support
			// So we are connected to line interrupt
			ASSERT(params.Version == CONNECT_LINE_BASED);
		}
	
	
	} else {
	
		//
		// We are on a legacy system and maybe can proceed
		//

		if (params.Version == CONNECT_FULLY_SPECIFIED) {
	
			//
			// use IoConnectInterruptEx to connect our ISR to a
			// line-based interrupt.
			//
get_legacy_int:
			params.FullySpecified.PhysicalDeviceObject = pdev->pdo;
			params.FullySpecified.InterruptObject  = int_obj;
			params.FullySpecified.ServiceRoutine  = isr;
			params.FullySpecified.ServiceContext = isr_ctx;
			params.FullySpecified.FloatingSave = FALSE;
			params.FullySpecified.SpinLock = NULL;

			if (pdev->int_info.Flags & CM_RESOURCE_INTERRUPT_MESSAGE) {
				// The resource is for a message-based interrupt. Use the u.MessageInterrupt.Translated member of IntResource.
				
				params.FullySpecified.Vector = pdev->int_info.u.MessageInterrupt.Translated.Vector;
				params.FullySpecified.Irql = (KIRQL)pdev->int_info.u.MessageInterrupt.Translated.Level;
				params.FullySpecified.SynchronizeIrql = (KIRQL)pdev->int_info.u.MessageInterrupt.Translated.Level;
				params.FullySpecified.ProcessorEnableMask = g.affinity ? 
					g.affinity : pdev->int_info.u.MessageInterrupt.Translated.Affinity;
			} else {
				// The resource is for a line-based interrupt. Use the u.Interrupt member of IntResource.
				
				params.FullySpecified.Vector = pdev->int_info.u.Interrupt.Vector;
				params.FullySpecified.Irql = (KIRQL)pdev->int_info.u.Interrupt.Level;
				params.FullySpecified.SynchronizeIrql = (KIRQL)pdev->int_info.u.Interrupt.Level;
				params.FullySpecified.ProcessorEnableMask = g.affinity ? 
					g.affinity : pdev->int_info.u.Interrupt.Affinity;
			}
			
			params.FullySpecified.InterruptMode = (pdev->int_info.Flags & CM_RESOURCE_INTERRUPT_LATCHED ? Latched : LevelSensitive);
			params.FullySpecified.ShareVector = (BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared);

			status = IoConnectInterruptEx(&params);
			pdev->version = params.Version;
		}
		else {

			// Something wrong with IoConnectInterruptEx.
			// Lets try the usual way
			status = IoConnectInterrupt(
				int_obj,										/* InterruptObject */
				isr,											/* ISR */ 
				isr_ctx,										/* ISR context */
				&pdev->isr_lock,								/* spinlock */
				pdev->int_info.u.Interrupt.Vector,				/* interrupt vector */
				(KIRQL)pdev->int_info.u.Interrupt.Level,		/* IRQL */
				(KIRQL)pdev->int_info.u.Interrupt.Level,		/* Synchronize IRQL */
				(KINTERRUPT_MODE)((pdev->int_info.Flags == CM_RESOURCE_INTERRUPT_LATCHED) ? 
					Latched : LevelSensitive),					/* interrupt type: LATCHED or LEVEL */
				(BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared),	/* vector shared or not */
				g.affinity ? g.affinity : (KAFFINITY)pdev->int_info.u.Interrupt.Affinity,	/* interrupt affinity */
				FALSE															/* whether to save Float registers */
				);
			pdev->legacy_connect = TRUE;
		}

	}

#else

	//
	// Legacy (before Vista) platform build environment
	//

	UNUSED_PARAM(misr);

	KeInitializeSpinLock( &pdev->isr_lock );
	pdev->n_msi_vectors = 0;  // not using MSI/MSI-X

	status = IoConnectInterrupt(
		int_obj,										/* InterruptObject */
		isr,											/* ISR */ 
		isr_ctx,										/* ISR context */
		&pdev->isr_lock,								/* spinlock */
		pdev->int_info.u.Interrupt.Vector,				/* interrupt vector */
		(KIRQL)pdev->int_info.u.Interrupt.Level,		/* IRQL */
		(KIRQL)pdev->int_info.u.Interrupt.Level,		/* Synchronize IRQL */
		(KINTERRUPT_MODE)((pdev->int_info.Flags == CM_RESOURCE_INTERRUPT_LATCHED) ? 
			Latched : LevelSensitive),					/* interrupt type: LATCHED or LEVEL */
		(BOOLEAN)(pdev->int_info.ShareDisposition == CmResourceShareShared),	/* vector shared or not */
		g.affinity ? g.affinity : (KAFFINITY)pdev->int_info.u.Interrupt.Affinity,	/* interrupt affinity */
		FALSE															/* whether to save Float registers */
		);

#endif

	if (!NT_SUCCESS(status)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_INIT ,
			("%s: Connect interrupt failed with status %#x, affinity %#x )\n",
			dev->pdev->name, status, g.affinity ? g.affinity : (unsigned int)pdev->int_info.u.Interrupt.Affinity));
		*int_obj = NULL;
		return -EFAULT;		/* failed to connect interrupt */
	} 

	return 0;
}
#endif

