/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
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
 *		- Redistributions of source code must retain the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer.
 *
 *		- Redistributions in binary form must reproduce the above
 *		  copyright notice, this list of conditions and the following
 *		  disclaimer in the documentation and/or other materials
 *		  provided with the distribution.
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
#include "fw.h"
#include "icm.h"
#include "device.h"
#include "doorbell.h"
#include "complib\cl_thread.h"
#include <mlx4_debug.h>
#include "stat.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "main.tmh"
#endif

#define MLX4_VF 			(1 << 0)
#define MLX4_CX3 			(1 << 1)

#define VEN_ID_MLNX 				0x15b3
#define VEN_ID_TOPSPIN				0x1867

struct pci_device_id {
	USHORT		vendor;
	USHORT		device;
	ULONG		driver_data;
} mlx4_pci_table[] = {
	/* VIP */
	{ VEN_ID_MLNX, 0x6340, 0		},
	{ VEN_ID_MLNX, 0x6341, MLX4_VF	},
	{ VEN_ID_MLNX, 0x634a, 0		},
	{ VEN_ID_MLNX, 0x6354, 0		},
	{ VEN_ID_MLNX, 0x634b, MLX4_VF	},
	{ VEN_ID_MLNX, 0x6732, 0		},
	{ VEN_ID_MLNX, 0x6733, MLX4_VF	},
	{ VEN_ID_MLNX, 0x6778, 0		},
	{ VEN_ID_MLNX, 0x673c, 0		},
	{ VEN_ID_MLNX, 0x673d, MLX4_VF	},
	{ VEN_ID_MLNX, 0x6746, 0		},
	{ VEN_ID_MLNX, 0x6368, 0		},
	{ VEN_ID_MLNX, 0x6369, MLX4_VF	},
	{ VEN_ID_MLNX, 0x6372, 0		},
	{ VEN_ID_MLNX, 0x6750, 0		},
	{ VEN_ID_MLNX, 0x6751, MLX4_VF	},
	{ VEN_ID_MLNX, 0x675A, 0		},
	{ VEN_ID_MLNX, 0x6764, 0		},
	{ VEN_ID_MLNX, 0x6765, MLX4_VF	},
	{ VEN_ID_MLNX, 0x676E, 0		},
	{ VEN_ID_MLNX, 0x1000, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x1001, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x1002, MLX4_VF | MLX4_CX3},
	{ VEN_ID_MLNX, 0x1003, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x1004, MLX4_VF | MLX4_CX3},
	{ VEN_ID_MLNX, 0x1007, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x1008, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x1009, MLX4_CX3	},
	{ VEN_ID_MLNX, 0x100a, MLX4_CX3	},
};
#define MLX4_PCI_TABLE_SIZE (sizeof(mlx4_pci_table)/sizeof(struct pci_device_id))


static struct mlx4_profile default_profile = {
	1 << 17,	/* num_qp		*/
	1 << 4, 	/* rdmarc_per_qp	*/
	1 << 16,	/* num_srq	*/
	1 << 16,	/* num_cq		*/
	1 << 13,	/* num_mcg	*/
	1 << 17,	/* num_mpt	*/ 
	1 << 20 	/* num_mtt	*/
};

void build_log_name( struct mlx4_dev *dev, int bus, int device, int func )
{
	char *role;
	if (mlx4_is_mfunc(dev))
		role = mlx4_is_master(dev) ? "PPF" : (mlx4_is_vf(dev) ? "VF" : "PF");
	else
		role = "SF";
	RtlStringCbPrintfA( dev->pdev->name, sizeof(dev->pdev->name),
		"MLX4_%s_%d_%d_%d", role, bus, device, func );
}

static void process_mod_param_profile(void)
{
	if (g.log_num_qp)
		default_profile.num_qp = 1 << g.log_num_qp;

	if (g.log_rdmarc_per_qp)
		default_profile.rdmarc_per_qp = 1 << g.log_rdmarc_per_qp;

	if (g.log_num_srq)
		default_profile.num_srq = 1 << g.log_num_srq;

	if (g.log_num_cq)
		default_profile.num_cq = 1 << g.log_num_cq;

	if (g.log_num_mcg)
		default_profile.num_mcg = 1 << g.log_num_mcg;

	if (g.log_num_mpt)
		default_profile.num_mpt = 1 << g.log_num_mpt;

	if (g.log_num_mtt)
		default_profile.num_mtt = 1 << g.log_num_mtt;
}

NTSTATUS init_working_thread(struct working_thread* p_thread, 
				PKSTART_ROUTINE  thread_routine, void* ctx)
{
	OBJECT_ATTRIBUTES attr;
	NTSTATUS status;
	HANDLE h_thread;

	KeInitializeEvent(&p_thread->trig_event, SynchronizationEvent, FALSE);
	
	InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );
	status = PsCreateSystemThread(&h_thread, THREAD_ALL_ACCESS, &attr, NULL, 
						NULL, thread_routine, ctx);
	if (NT_SUCCESS(status)) {
		status = ObReferenceObjectByHandle(
			h_thread,
			THREAD_ALL_ACCESS,
			NULL,
			KernelMode,
			&p_thread->h_thread,
			NULL
			);
	
		ASSERT(status == STATUS_SUCCESS); 
		ZwClose(h_thread);
	}

	return status;
}

void stop_working_thread(struct working_thread* p_thread)
{
	NTSTATUS status;

	p_thread->f_stop = TRUE;
	KeSetEvent(&p_thread->trig_event, IO_NO_INCREMENT, FALSE );
	status = KeWaitForSingleObject(p_thread->h_thread,
			Executive, KernelMode, FALSE, NULL);
	ASSERT(status == STATUS_SUCCESS);
}


int mlx4_check_port_params(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_type)
{
	int i;
	struct mlx4_dev *mdev = dev;

	if(dev->caps.num_ports > 1)
	{
		ASSERT(dev->caps.num_ports == 2);
		if (port_type[0] != port_type[1]) 
		{
			if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP))
			{				 
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Only same port types supported on this HCA, aborting.\n", dev->pdev->name));
				WriteEventLogEntryData(dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_PORT_TYPE_DPDP, 0, 0, 1, L"%S", dev->pdev->name);
				return -EINVAL;
			}
			
			if (port_type[0] == MLX4_PORT_TYPE_ETH && port_type[1] == MLX4_PORT_TYPE_IB) 
			{
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: eth,ib port type configuration is not supported by design.\n ", dev->pdev->name));
				WriteEventLogEntryData(dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_PORT_TYPE_ETH_IB, 0, 0, 1, L"%S", dev->pdev->name);
				return -EINVAL;
			}			 
		}
	}

	for (i = 0; i < dev->caps.num_ports; i++) 
	{
		if (!(port_type[i] & dev->caps.port_types_cap[i+1])) 
		{
			MLX4_PRINT_EV(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Requested port type for port %d is not supported on this HCA\n", dev->pdev->name, i + 1));
			ASSERT(FALSE);			  
			return -EINVAL;
		}
	}

	return 0;
}

void mlx4_set_port_mask(struct mlx4_dev *dev, struct mlx4_caps *caps, int function)
{
	int i;

	for (i = 1; i <= caps->num_ports; ++i) {
		if (mlx4_is_master(dev) && (dev->caps.pf_num > 1) &&
			mlx4_priv(dev)->mfunc.master.slave_state[function].port_num != i)
			caps->port_mask[i] = MLX4_PORT_TYPE_NONE;
		else
			caps->port_mask[i] = (u8)(caps->port_type_final[i]);
		caps->transport_type[i] = (caps->port_type_final[i] == MLX4_PORT_TYPE_IB) ?
			RDMA_TRANSPORT_IB : RDMA_TRANSPORT_RDMAOE;
	}
}

static struct pci_device_id * mlx4_find_pci_dev(USHORT ven_id, USHORT dev_id)
{
	struct pci_device_id *p_id = mlx4_pci_table;
	int i;

	// find p_id (appropriate line in mlx4_pci_table)
	for (i = 0; i < MLX4_PCI_TABLE_SIZE; ++i, ++p_id) {
		if (p_id->device == dev_id && p_id->vendor ==  ven_id)
			return p_id;
	}
	return NULL;
}

BOOLEAN mlx4_is_cx3(struct mlx4_dev *dev)
{
	struct pci_device_id *p_id = mlx4_find_pci_dev(dev->pdev->ven_id, dev->pdev->dev_id);

	return (p_id->driver_data & MLX4_CX3) ? TRUE : FALSE;
}

void fill_capability_flag(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
    dev->capability_flag.u.Value = 0;
	
	if(dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP)
	{
		dev->capability_flag.u.ver.fDpDp = 1;
	}
    
    if(dev_cap->port_types_cap[1] & MLX4_PORT_TYPE_IB)
    {
        dev->capability_flag.u.ver.fPort1IB = 1;
    }
    
    if(dev_cap->port_types_cap[1] & MLX4_PORT_TYPE_ETH)
    {
        dev->capability_flag.u.ver.fPort1Eth = 1;
    }
    
    if(dev->caps.port_types_do_sense[1])
    {
        dev->capability_flag.u.ver.fPort1DoSenseAllowed = 1;
    }

    dev->capability_flag.u.ver.fPort1Default = dev->caps.port_types_default[1];

    if(dev->capability_flag.u.ver.fPort1Default == 0 &&  !dev->capability_flag.u.ver.fPort1IB)
    {
       dev->capability_flag.u.ver.fPort1Default = 1; 
    }

    if(dev->capability_flag.u.ver.fPort1Default == 1 &&  !dev->capability_flag.u.ver.fPort1Eth)
    {
       dev->capability_flag.u.ver.fPort1Default = 0; 
    }

    if(dev_cap->num_ports > 1)
    {
        if(dev_cap->port_types_cap[2] & MLX4_PORT_TYPE_IB)
        {
            dev->capability_flag.u.ver.fPort2IB = 1;
        }
        
        if(dev_cap->port_types_cap[2] & MLX4_PORT_TYPE_ETH)
        {
            dev->capability_flag.u.ver.fPort2Eth = 1;
        }
        
        if(dev->caps.port_types_do_sense[2])
        {
            dev->capability_flag.u.ver.fPort2DoSenseAllowed = 1;
        }
        
        dev->capability_flag.u.ver.fPort2Default = dev->caps.port_types_default[2];        
        
        if(dev->capability_flag.u.ver.fPort2Default == 0 &&  !dev->capability_flag.u.ver.fPort2IB)
        {
           dev->capability_flag.u.ver.fPort2Default = 1; 
        }
        
        if(dev->capability_flag.u.ver.fPort2Default == 1 &&  !dev->capability_flag.u.ver.fPort2Eth)
        {
           dev->capability_flag.u.ver.fPort2Default = 0; 
        }
        
	}


    if((!mlx4_is_cx3(dev) || (dev->caps.flags & MLX4_DEV_CAP_SENSE_SUPPORT)))
    {
        if(dev->capability_flag.u.ver.fPort1IB && dev->capability_flag.u.ver.fPort1Eth)
        {
            dev->capability_flag.u.ver.fPort1AutoSenseCap = 1;
        }

        if(dev->capability_flag.u.ver.fPort2IB && dev->capability_flag.u.ver.fPort2Eth)
        {
            dev->capability_flag.u.ver.fPort2AutoSenseCap = 1;
        }
    }
    else
    {
        dev->capability_flag.u.ver.fPort1AutoSenseCap = 0;
        dev->capability_flag.u.ver.fPort2AutoSenseCap = 0;
    }        
}
 
static void mlx4_set_port_type(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
    int i;
    enum mlx4_port_type port_type[MLX4_MAX_PORTS];

    for (i = 0; i < MLX4_MAX_PORTS; i++) 
        port_type[i] = dev->dev_params.mod_port_type[i];

    for (i = 1; i <= dev->caps.num_ports; ++i) 
    {
        mlx4_priv(dev)->sense.sense_allowed[i] = 0;
        dev->caps.port_type_final[i] = MLX4_PORT_TYPE_NONE;

        if (!dev_cap->port_types_cap[i]) 
        {
            MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: FW doesn't support Multi Protocol, "
            "loading IB only\n", dev->pdev->name));
            dev->caps.port_type_final[i] = MLX4_PORT_TYPE_IB;
            ASSERT(FALSE);
            continue;
        }

        if (port_type[i-1] & dev_cap->port_types_cap[i])
        {
            dev->caps.port_type_final[i] = (mlx4_port_type)(port_type[i-1] & dev_cap->port_types_cap[i]);
        }
        else 
        {
            if(dev_cap->port_types_cap[i] == MLX4_PORT_TYPE_IB)
            {
                dev->caps.port_type_final[i] = MLX4_PORT_TYPE_IB;
            }
            else if (dev_cap->port_types_cap[i] == MLX4_PORT_TYPE_ETH)
            {
                dev->caps.port_type_final[i] = MLX4_PORT_TYPE_ETH;
            }
        }

        if(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_IB || 
           dev->caps.port_type_final[i] == MLX4_PORT_TYPE_ETH)
        {
            //
            //  Done, final port type were set. No need for sensing
            //
            continue;
        }

        if(!mlx4_is_cx3(dev) || (dev->caps.flags & MLX4_DEV_CAP_SENSE_SUPPORT))
        {
            if(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_AUTO)
            {
                mlx4_priv(dev)->sense.sense_allowed[i] = 1;
            }
            else
            {
                ASSERT(dev->caps.port_type_final[i] == MLX4_PORT_TYPE_NONE);
                if(!mlx4_is_cx3(dev) || dev->caps.port_types_do_sense[i])
                {
                    mlx4_priv(dev)->sense.sense_allowed[i] = 1;
                }
                else
                {
                    dev->caps.port_type_final[i] = (dev->caps.port_types_default[i] == 0) ?
                    MLX4_PORT_TYPE_IB : MLX4_PORT_TYPE_ETH;
                }
            }
        }
    }
}

static int mlx4_dev_cap(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap)
{
	int err;
	int i;
	struct mlx4_dev *mdev = dev;

	err = mlx4_QUERY_DEV_CAP(dev, dev_cap);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: QUERY_DEV_CAP command failed, aborting.\n", dev->pdev->name));
		return err;
	}

	dev->caps.pf_num = dev_cap->pf_num;
	build_log_name( dev, dev->pdev->pci_bus, dev->pdev->pci_device, dev->pdev->pci_func );

	if (dev_cap->min_page_sz > PAGE_SIZE) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA minimum page size of %d bigger than "
			 "kernel PAGE_SIZE of %ld, aborting.\n",
			 dev->pdev->name, dev_cap->min_page_sz, PAGE_SIZE));
		return -ENODEV;
	}
	if (dev_cap->num_ports > MLX4_MAX_PORTS) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA has %d ports, but we only support %d, "
			 "aborting.\n",
			 dev->pdev->name, dev_cap->num_ports, MLX4_MAX_PORTS));
		return -ENODEV;
	}

	if (dev_cap->uar_size > (int)pci_resource_len(dev->pdev, 2)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA reported UAR size of 0x%x bigger than "
			 "PCI resource 2 size of 0x%I64x, aborting.\n",
			 dev->pdev->name, dev_cap->uar_size,
			 pci_resource_len(dev->pdev, 2)));
		return -ENODEV;
	}

	dev->caps.num_ports 	 = dev_cap->num_ports;
	for (i = 1; i <= dev->caps.num_ports; ++i) {
		dev->caps.vl_cap[i] 	= dev_cap->max_vl[i];
		dev->caps.ib_mtu_cap[i] 	= dev_cap->ib_mtu[i];		
		dev->caps.gid_table_len[i]	= dev_cap->max_gids[i];
		dev->caps.pkey_table_len[i] = dev_cap->max_pkeys[i];
		dev->caps.port_width_cap[i] = (u8)dev_cap->max_port_width[i];
		dev->caps.eth_mtu_cap[i]	= dev_cap->eth_mtu[i];
		dev->caps.def_mac[i]		= dev_cap->def_mac[i];		  
		dev->caps.port_types_cap[i] = dev_cap->port_types_cap[i];
        dev->caps.port_types_default[i] = dev_cap->port_types_default[i];
        dev->caps.port_types_do_sense[i] = dev_cap->port_types_do_sense[i];
	}

	dev->caps.uar_page_size 	 = PAGE_SIZE;
	dev->caps.num_uars		 = dev_cap->uar_size / PAGE_SIZE;
	dev->caps.local_ca_ack_delay = dev_cap->local_ca_ack_delay;
	dev->caps.bf_reg_size		 = dev_cap->bf_reg_size;
	dev->caps.bf_regs_per_page	 = dev_cap->bf_regs_per_page;
	dev->caps.max_sq_sg 	 = dev_cap->max_sq_sg;
	dev->caps.max_rq_sg 	 = dev_cap->max_rq_sg;
	dev->caps.max_wqes		 = dev_cap->max_qp_sz;
	dev->caps.max_qp_init_rdma	 = dev_cap->max_requester_per_qp;
	dev->caps.max_srq_wqes		 = dev_cap->max_srq_sz;
	dev->caps.max_srq_sge		 = dev_cap->max_rq_sg - 1;
	dev->caps.reserved_srqs 	 = dev_cap->reserved_srqs;
	dev->caps.max_sq_desc_sz	 = dev_cap->max_sq_desc_sz;
	dev->caps.max_rq_desc_sz	 = dev_cap->max_rq_desc_sz;
	dev->caps.num_qp_per_mgm	 = MLX4_QP_PER_MGM;
	/*
	 * Subtract 1 from the limit because we need to allocate a
	 * spare CQE so the HCA HW can tell the difference between an
	 * empty CQ and a full CQ.
	 */
	dev->caps.max_cqes		 = dev_cap->max_cq_sz - 1;
	dev->caps.reserved_cqs		 = dev_cap->reserved_cqs;
	dev->caps.reserved_eqs		 = dev_cap->reserved_eqs;
	dev->caps.mtts_per_seg		 = 1 << g.log_mtts_per_seg;
	dev->caps.reserved_mtts 	 = DIV_ROUND_UP(dev_cap->reserved_mtts,
							dev->caps.mtts_per_seg);
	dev->caps.reserved_mrws 	 = dev_cap->reserved_mrws;
	/* The first 128 UARs are used for EQ doorbells */
	dev->caps.reserved_uars 	 = max_t(int, 128, dev_cap->reserved_uars);
	dev->caps.reserved_pds		 = dev_cap->reserved_pds;
	dev->caps.mtt_entry_sz		 = dev->caps.mtts_per_seg * dev_cap->mtt_entry_sz;
	dev->caps.max_msg_sz		 = dev_cap->max_msg_sz;
	dev->caps.page_size_cap 	 = ~(u32) (dev_cap->min_page_sz - 1);
	dev->caps.flags 		 = dev_cap->flags;
	dev->caps.bmme_flags		 = dev_cap->bmme_flags;
	dev->caps.reserved_lkey 	 = dev_cap->reserved_lkey;
	dev->caps.stat_rate_support  = dev_cap->stat_rate_support;
	
	dev->caps.if_cnt_basic_support = dev_cap->if_cnt_basic_support;
	dev->caps.if_cnt_extended_support = dev_cap->if_cnt_extended_support;

	if (dev->caps.if_cnt_extended_support) {
		dev->caps.counters_mode = MLX4_CUNTERS_EXT;
	} else if (dev->caps.if_cnt_basic_support) {
		dev->caps.counters_mode = MLX4_CUNTERS_BASIC;
	} else {
		dev->caps.counters_mode = MLX4_CUNTERS_DISABLED;
	}	 
	
	dev->caps.loopback_support	 = dev_cap->loopback_support;
	dev->caps.vep_uc_steering	 = dev_cap->vep_uc_steering;
	dev->caps.vep_mc_steering	 = dev_cap->vep_mc_steering;
	dev->caps.steering_by_vlan	 = dev_cap->steering_by_vlan;
	dev->caps.hds				 = dev_cap->hds;
	dev->caps.header_lookahead	 = dev_cap->header_lookahead;
	dev->caps.wol				 = dev_cap->wol;
	dev->caps.max_gso_sz		 = dev_cap->max_gso_sz;
	dev->caps.reserved_xrcds	 = (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) ?
		dev_cap->reserved_xrcds : 0;
	dev->caps.max_xrcds 	 = (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) ?
		dev_cap->max_xrcds : 0;

	dev->caps.max_if_cnt_basic = dev_cap->max_if_cnt_basic;
	dev->caps.max_if_cnt_extended = dev_cap->max_if_cnt_extended;

	dev->caps.log_num_macs	= g.log_num_mac;
	dev->caps.log_num_vlans = g.log_num_vlan;
	dev->caps.log_num_prios = (g.use_prio)? 3: 0;
	dev->caps.num_fc_exch = g.num_fc_exch;
    
    mlx4_set_port_type(dev,dev_cap);
    fill_capability_flag(dev,dev_cap);
    
	// If port1=ETH, then port2 have to be ETH !
	if ( (dev->caps.num_ports == 2) &&
		 (dev->caps.port_type_final[1] == MLX4_PORT_TYPE_ETH && !dev->dev_params.enable_roce[1]) &&
		 (dev->caps.port_type_final[2] != MLX4_PORT_TYPE_ETH || dev->dev_params.enable_roce[2]) ) {
		MLX4_PRINT_EV(TRACE_LEVEL_ERROR,MLX4_DBG_DRV ,
			("%s: Unsupported configuration: port_type1 'ETH', port_type2 '%s' ! \n", 
			dev->pdev->name, 
			(dev->caps.port_type_final[2] != MLX4_PORT_TYPE_ETH) ? "IB" : "RoCE" ));
		return -ENODEV;
	}
    
	for (i = 1; i <= dev->caps.num_ports; ++i) {
		if ( dev->caps.port_type_final[i] == MLX4_PORT_TYPE_IB )
			g.slave_num = 0;
        
		if (dev->caps.log_num_macs > dev_cap->log_max_macs[i]) {
			dev->caps.log_num_macs = dev_cap->log_max_macs[i];
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Requested number of MACs is too much "
					   "for port %d, reducing to %d.\n",
				dev->pdev->name, i, 1 << dev->caps.log_num_macs));
		}
		if (dev->caps.log_num_vlans > dev_cap->log_max_vlans[i]) {
			dev->caps.log_num_vlans = dev_cap->log_max_vlans[i];
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Requested number of VLANs is too much "
					   "for port %d, reducing to %d.\n",
				dev->pdev->name, i, 1 << dev->caps.log_num_vlans));
		}
	}    

	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW] = dev_cap->reserved_qps;
	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR] =
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR] =
		(1 << dev->caps.log_num_macs)*
		(1 << dev->caps.log_num_vlans)*
		(1 << dev->caps.log_num_prios)*
		dev->caps.num_ports;
	dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH] = dev->caps.num_fc_exch;
	dev->caps.reserved_qps = dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR] +
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH];

	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: QP regions sizes: FW %d, ETH %d, FC %d, FC_EXCH %d, All %d\n",
		dev->pdev->name, 
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_ETH_ADDR],
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_ADDR],
		dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FC_EXCH],
		dev->caps.reserved_qps ));
		
		if (dev_cap->flags & (1ULL << 62)) {
			mlx4_warn(dev, "64 byte CQE supported\n");
			dev->caps.cqe_size = 64;
		} else {
			mlx4_warn(dev, "32 byte CQE supported\n");
			dev->caps.cqe_size = 32;
		}
		if ( g.mode_flags & MLX4_MODE_USE_CQE32 ) {
			mlx4_warn(dev, "But 32 byte CQE will be used because of %#x flag in ModeFlags\n", MLX4_MODE_USE_CQE32); 
			dev->caps.cqe_size = 32;
		}

        if(dev_cap->flags & (1ULL << 53)) {
            if(!mlx4_is_cx3(dev))
            {
                //
                //  Clear ETS capabilty support for CX2 cards
                //
                dev->caps.flags &= ~(1ULL << 53);
            }
        }

		dev->caps.eqe_size = 32;

	/* Master function demultiplexes mads */
	dev->caps.sqp_demux = MLX4_MAX_NUM_SLAVES;
	dev->caps.clp_ver = dev_cap->clp_ver;	 
	return 0;
}

static int mlx4_slave_cap(struct mlx4_dev *dev)
{
	int err;
	u32 page_size;

	err = mlx4_QUERY_SLAVE_CAP(dev, &dev->caps);
	if (err)
		return err;

	page_size = ~dev->caps.page_size_cap + 1;
	MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("HCA minimum page size:%d\n", page_size));
	if (page_size > PAGE_SIZE) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA minimum page size of %d bigger than "
			 "kernel PAGE_SIZE of %ld, aborting.\n",
			 dev->pdev->name, page_size, PAGE_SIZE));
		return -ENODEV;
	}

	/* TODO: relax this assumption */
	if (dev->caps.uar_page_size != PAGE_SIZE) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: UAR size:%d != kernel PAGE_SIZE of %ld\n",
			 dev->pdev->name, dev->caps.uar_page_size, PAGE_SIZE));
		return -ENODEV;
	}

	if (dev->caps.num_ports > MLX4_MAX_PORTS) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA has %d ports, but we only support %d, "
			 "aborting.\n", dev->pdev->name, dev->caps.num_ports, MLX4_MAX_PORTS));
		return -ENODEV;
	}

	if (dev->caps.uar_page_size * (dev->caps.num_uars -
					   dev->caps.reserved_uars) >
					   (int)pci_resource_len(dev->pdev, 2)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: HCA reported UAR region size of 0x%x bigger than "
			 "PCI resource 2 size of 0x%I64x, aborting.\n",
			 dev->pdev->name, dev->caps.uar_page_size * dev->caps.num_uars,
			 pci_resource_len(dev->pdev, 2)));
		return -ENODEV;
	}

	/* Adjust eq number */
	if (dev->caps.num_eqs - dev->caps.reserved_eqs > (int)num_possible_cpus() + MLX4_NUM_IB_EQ)
		dev->caps.num_eqs= dev->caps.reserved_eqs + num_possible_cpus() + MLX4_NUM_IB_EQ;

#if 0
	mlx4_warn(dev, "%s: sqp_demux:%d\n", dev->pdev->name, dev->caps.sqp_demux);
	mlx4_warn(dev, "%s: num_uars:%d reserved_uars:%d uar region:0x%x bar2:0x%llx\n",
					dev->pdev->name, 
					  dev->caps.num_uars, dev->caps.reserved_uars,
					  dev->caps.uar_page_size * dev->caps.num_uars,
					  pci_resource_len(dev->pdev, 2));
	mlx4_warn(dev, "%s: num_eqs:%d reserved_eqs:%d\n", dev->pdev->name, dev->caps.num_eqs,
							   dev->caps.reserved_eqs);
	mlx4_warn(dev, "%s: num_pds:%d reserved_pds:%d slave_pd_shift:%d pd_base:%d\n",
							dev->pdev->name, 
							dev->caps.num_pds,
							dev->caps.reserved_pds,
							dev->caps.slave_pd_shift,
							dev->caps.pd_base);
#endif
	return 0;
}

static int mlx4_load_fw(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;

	priv->fw.fw_icm = mlx4_alloc_icm(dev, priv->fw.fw_pages,
					 (gfp_t)(GFP_HIGHUSER | __GFP_NOWARN), 0);
	if (!priv->fw.fw_icm) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Couldn't allocate FW area, aborting.\n", dev->pdev->name));
		return -ENOMEM;
	}

	err = mlx4_MAP_FA(dev, priv->fw.fw_icm);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: MAP_FA command failed, aborting.\n", dev->pdev->name));
		WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_MAP_FA, 0, 0, 2,
			L"%S", dev->pdev->name, 
			L"%d", err );
		goto err_free;
	}

	err = mlx4_RUN_FW(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: RUN_FW command failed, aborting.\n", dev->pdev->name));
		WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_RUN_FW, 0, 0, 2,
			L"%S", dev->pdev->name, L"%d", err );
		goto err_unmap_fa;
	}

	return 0;

err_unmap_fa:
	mlx4_UNMAP_FA(dev);

err_free:
	mlx4_free_icm(dev, priv->fw.fw_icm, 0);
	return err;
}

static int mlx4_init_cmpt_table(struct mlx4_dev *dev, u64 cmpt_base,
					  int cmpt_entry_sz)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	int num_eqs;

	err = mlx4_init_icm_table(dev, &priv->qp_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_QP *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err)
		goto err;

	err = mlx4_init_icm_table(dev, &priv->srq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_SRQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_srqs,
				  dev->caps.reserved_srqs, 0, 0);
	if (err)
		goto err_qp;

	err = mlx4_init_icm_table(dev, &priv->cq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_CQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, dev->caps.num_cqs,
				  dev->caps.reserved_cqs, 0, 0);
	if (err)
		goto err_srq;

	num_eqs = mlx4_is_master(dev) ? 512 : dev->caps.num_eqs;
	err = mlx4_init_icm_table(dev, &priv->eq_table.cmpt_table,
				  cmpt_base +
				  ((u64) (MLX4_CMPT_TYPE_EQ *
					  cmpt_entry_sz) << MLX4_CMPT_SHIFT),
				  cmpt_entry_sz, num_eqs, num_eqs, 0, 0);
 
	if (err)
		goto err_cq;

	return 0;

err_cq:
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);

err_srq:
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);

err_qp:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

err:
	return err;
}

static int mlx4_init_icm(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap,
			 struct mlx4_init_hca_param *init_hca, u64 icm_size)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 aux_pages;
	int num_eqs;
	int err;

	err = mlx4_SET_ICM_SIZE(dev, icm_size, &aux_pages);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: SET_ICM_SIZE command failed, aborting.\n", dev->pdev->name));
		return err;
	}

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: %I64d KB of HCA context requires %I64d KB aux memory.\n",
		dev->pdev->name, 
		icm_size >> 10,
		aux_pages << 2));

	priv->fw.aux_icm = mlx4_alloc_icm(dev, (int)aux_pages,
					  (gfp_t)(GFP_HIGHUSER | __GFP_NOWARN), 0);
	if (!priv->fw.aux_icm) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Couldn't allocate aux memory, aborting.\n", dev->pdev->name));
		return -ENOMEM;
	}

	err = mlx4_MAP_ICM_AUX(dev, priv->fw.aux_icm);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: MAP_ICM_AUX command failed, aborting.\n", dev->pdev->name));
		goto err_free_aux;
	}

	err = mlx4_init_cmpt_table(dev, init_hca->cmpt_base, dev_cap->cmpt_entry_sz);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map cMPT context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_aux;
	}


	num_eqs = mlx4_is_master(dev) ? 512 : dev->caps.num_eqs;
	err = mlx4_init_icm_table(dev, &priv->eq_table.table,
		init_hca->eqc_base, dev_cap->eqc_entry_sz,
				  num_eqs, num_eqs, 0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map EQ context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_cmpt;
	}

	/*
	 * Reserved MTT entries must be aligned up to a cacheline
	 * boundary, since the FW will write to them, while the driver
	 * writes to all other MTT entries. (The variable
	 * dev->caps.mtt_entry_sz below is really the MTT segment
	 * size, not the raw entry size)
	 */
	dev->caps.reserved_mtts =
		ALIGN(dev->caps.reserved_mtts * dev->caps.mtt_entry_sz,
			  dma_get_cache_alignment()) / dev->caps.mtt_entry_sz;
	if ( dev->pdev->p_self_do->AlignmentRequirement + 1 != dma_get_cache_alignment()) {
		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Cache-line size %d, recommended value %d.\n",
			dev->pdev->name, 
			dev->pdev->p_self_do->AlignmentRequirement + 1,
			dma_get_cache_alignment() ));
	}

	err = mlx4_init_icm_table(dev, &priv->mr_table.mtt_table,
				  init_hca->mtt_base,
				  dev->caps.mtt_entry_sz,
				  dev->caps.num_mtt_segs,
				  dev->caps.reserved_mtts, 1, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map MTT context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_eq;
	}

	err = mlx4_init_icm_table(dev, &priv->mr_table.dmpt_table,
				  init_hca->dmpt_base,
				  dev_cap->dmpt_entry_sz,
				  dev->caps.num_mpts,
				  dev->caps.reserved_mrws, 1, 1);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map dMPT context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_mtt;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.qp_table,
				  init_hca->qpc_base,
				  dev_cap->qpc_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map QP context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_dmpt;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.auxc_table,
				  init_hca->auxc_base,
				  dev_cap->aux_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map AUXC context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_qp;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.altc_table,
				  init_hca->altc_base,
				  dev_cap->altc_entry_sz,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map ALTC context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_auxc;
	}

	err = mlx4_init_icm_table(dev, &priv->qp_table.rdmarc_table,
				  init_hca->rdmarc_base,
				  dev_cap->rdmarc_entry_sz << priv->qp_table.rdmarc_shift,
				  dev->caps.num_qps,
				  dev->caps.reserved_qps_cnt[MLX4_QP_REGION_FW],
				  0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map RDMARC context memory, aborting\n", dev->pdev->name));
		goto err_unmap_altc;
	}

	err = mlx4_init_icm_table(dev, &priv->cq_table.table,
				  init_hca->cqc_base,
				  dev_cap->cqc_entry_sz,
				  dev->caps.num_cqs,
				  dev->caps.reserved_cqs, 0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map CQ context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_rdmarc;
	}

	err = mlx4_init_icm_table(dev, &priv->srq_table.table,
				  init_hca->srqc_base,
				  dev_cap->srq_entry_sz,
				  dev->caps.num_srqs,
				  dev->caps.reserved_srqs, 0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map SRQ context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_cq;
	}

	/*
	 * It's not strictly required, but for simplicity just map the
	 * whole multicast group table now.  The table isn't very big
	 * and it's a lot easier than trying to track ref counts.
	 */
	err = mlx4_init_icm_table(dev, &priv->mcg_table.table,
				  init_hca->mc_base, MLX4_MGM_ENTRY_SIZE,
				  dev->caps.num_mgms + dev->caps.num_amgms,
				  dev->caps.num_mgms + dev->caps.num_amgms,
				  0, 0);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to map MCG context memory, aborting.\n", dev->pdev->name));
		goto err_unmap_srq;
	}

	return 0;

err_unmap_srq:
	mlx4_cleanup_icm_table(dev, &priv->srq_table.table);

err_unmap_cq:
	mlx4_cleanup_icm_table(dev, &priv->cq_table.table);

err_unmap_rdmarc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.rdmarc_table);

err_unmap_altc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.altc_table);

err_unmap_auxc:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.auxc_table);

err_unmap_qp:
	mlx4_cleanup_icm_table(dev, &priv->qp_table.qp_table);

err_unmap_dmpt:
	mlx4_cleanup_icm_table(dev, &priv->mr_table.dmpt_table);

err_unmap_mtt:
	mlx4_cleanup_icm_table(dev, &priv->mr_table.mtt_table);

err_unmap_eq:
	mlx4_cleanup_icm_table(dev, &priv->eq_table.table);

err_unmap_cmpt:
	mlx4_cleanup_icm_table(dev, &priv->eq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

err_unmap_aux:
	mlx4_UNMAP_ICM_AUX(dev);

err_free_aux:
	mlx4_free_icm(dev, priv->fw.aux_icm, 0);

	return err;
}

static void mlx4_free_icms(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	mlx4_cleanup_icm_table(dev, &priv->mcg_table.table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.rdmarc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.altc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.auxc_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.qp_table);
	mlx4_cleanup_icm_table(dev, &priv->mr_table.dmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->mr_table.mtt_table);
	mlx4_cleanup_icm_table(dev, &priv->eq_table.table); 
	mlx4_cleanup_icm_table(dev, &priv->eq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->cq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->srq_table.cmpt_table);
	mlx4_cleanup_icm_table(dev, &priv->qp_table.cmpt_table);

	mlx4_UNMAP_ICM_AUX(dev);
	mlx4_free_icm(dev, priv->fw.aux_icm, 0);
	priv->fw.aux_icm = NULL;
}

static void mlx4_slave_exit(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	down(&priv->cmd.slave_sem);
	
	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: close slave function.\n", dev->pdev->name));
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0, MLX4_COMM_TIME))
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to close slave function.\n", dev->pdev->name));
	up(&priv->cmd.slave_sem);
}

static int map_bf_area(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	resource_size_t bf_start;
	resource_size_t bf_len;
	int err = 0;

	bf_start = pci_resource_start(dev->pdev, 2) + (dev->caps.num_uars << PAGE_SHIFT);
	bf_len = pci_resource_len(dev->pdev, 2) - (dev->caps.num_uars << PAGE_SHIFT);
	priv->bf_mapping = io_mapping_create_wc(bf_start, (SIZE_T)bf_len);
	if (!priv->bf_mapping)
		err = -ENOMEM;

	return err;
}

static void unmap_bf_area(struct mlx4_dev *dev)
{
	if (mlx4_priv(dev)->bf_mapping)
#ifdef MAP_WC_EVERY_TIME
		io_mapping_free(mlx4_priv(dev)->bf_mapping);
#else
		io_mapping_free(mlx4_priv(dev)->bf_mapping->va);
#endif
}

static void mlx4_close_hca(struct mlx4_dev *dev)
{
	if (mlx4_is_slave(dev))
		mlx4_slave_exit(dev);
	else {
		unmap_bf_area(dev);
		mlx4_CLOSE_HCA(dev, 0);
		mlx4_free_icms(dev);
		mlx4_UNMAP_FA(dev);
		mlx4_free_icm(dev, mlx4_priv(dev)->fw.fw_icm, 0);
	}
}


static int mlx4_init_slave(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 dma = (u64) priv->mfunc.vhcr_dma.da;

	down(&priv->cmd.slave_sem);
	priv->cmd.max_cmds = 1;
	MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
		("%s: Sending 'MLX4_COMM_CMD_RESET'.\n", dev->pdev->name) );
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0, MLX4_COMM_TIME))
		goto err;
	MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
		("%s: Sending 'MLX4_COMM_CMD_VHCR0'.\n", dev->pdev->name) );
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR0, (u16)(dma >> 48), MLX4_COMM_TIME))
		goto err;
	MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
		("%s: Sending 'MLX4_COMM_CMD_VHCR1'.\n", dev->pdev->name) );
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR1, (u16)(dma >> 32), MLX4_COMM_TIME))
		goto err;
	MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
		("%s: Sending 'MLX4_COMM_CMD_VHCR2'.\n", dev->pdev->name) );
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR2, (u16)(dma >> 16), MLX4_COMM_TIME))
		goto err;
	MLX4_PRINT( TRACE_LEVEL_VERBOSE, MLX4_DBG_COMM,
		("%s: Sending 'MLX4_COMM_CMD_VHCR_EN'.\n", dev->pdev->name) );
	if (mlx4_comm_cmd(dev, MLX4_COMM_CMD_VHCR_EN, (u16)dma, MLX4_COMM_TIME))
		goto err;
	up(&priv->cmd.slave_sem);
	return 0;

err:
	mlx4_comm_cmd(dev, MLX4_COMM_CMD_RESET, 0, 0);
	up(&priv->cmd.slave_sem);
	return -EIO;
}

static int mlx4_init_hca(struct mlx4_dev *dev)
{
	struct mlx4_priv	  *priv = mlx4_priv(dev);
	struct mlx4_adapter    adapter;
	struct mlx4_mod_stat_cfg   mlx4_cfg;
	struct mlx4_profile    profile;
	struct mlx4_init_hca_param init_hca;
	u64 icm_size;
	int err;

	if (!mlx4_is_slave(dev)) {
		err = mlx4_QUERY_FW(dev);
		if (err) {
			if (err == -EACCES) {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("Non-primary physical function at '%S, "
						   "running in slave mode.\n", dev->pdev->location ));
//				WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_WARN_QUERY_FW, 0, 0, 1,
//					L"%s", dev->pdev->location );
			}
			else {
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: QUERY_FW command failed, aborting.\n", dev->pdev->name ));
				WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_QUERY_FW, 0, 0, 2,
					L"%S", dev->pdev->name, L"%d", err );
			}
			return err;
		}

		err = mlx4_load_fw(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: Failed to start FW, aborting.\n", dev->pdev->name));
			return err;
		}

		mlx4_cfg.log_pg_sz_m = 1;
		mlx4_cfg.log_pg_sz = 0;
		err = mlx4_MOD_STAT_CFG(dev, &mlx4_cfg);
		if (err)
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to override log_pg_sz parameter\n", dev->pdev->name));
	
		err = mlx4_dev_cap(dev, &dev->dev_cap);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: QUERY_DEV_CAP command failed, aborting.\n", dev->pdev->name));
			WriteEventLogEntryData( dev->pdev->p_self_do, (ULONG)EVENT_MLX4_ERROR_QUERY_DEV_CAP, 0, 0, 2,
				L"%S", dev->pdev->name, L"%d", err );
			goto err_stop_fw;
		}

		//
		// Initilize the port state. It's a new command that is supported only in FW 2.6.1280. 
		//If running on earlier FW version the command can fail. Ignore the error code returned
		//
		mlx4_port_state(dev);

		process_mod_param_profile();
		profile = default_profile;

		icm_size = mlx4_make_profile(dev, &profile, &dev->dev_cap, &init_hca);
		if ((long long) icm_size < 0) {
			err = (int)icm_size;
			goto err_stop_fw;
		}

		if (map_bf_area(dev)) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Not using Blue Flame\n"));
		}
		else {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Using Blue Flame\n"));
		}

		init_hca.log_uar_sz = (u8)ilog2(dev->caps.num_uars);

		err = mlx4_init_icm(dev, &dev->dev_cap, &init_hca, icm_size);
		if (err)
			goto err_stop_fw;

		err = mlx4_INIT_HCA(dev, &init_hca);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: INIT_HCA command failed, aborting.\n", dev->pdev->name));
			goto err_free_icm;
		}
	}
	else {
	
		err = mlx4_init_slave(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize slave\n", dev->pdev->name));
			return err;
		}

		err = mlx4_slave_cap(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to obtain slave caps\n", dev->pdev->name));
			goto err_close;
		}
	}

	err = mlx4_sense_port(dev);
	if (err)
		goto err_close;


	if(!mlx4_is_mfunc(dev) || mlx4_is_master(dev))
	{
		if(dev->caps.counters_mode != MLX4_CUNTERS_DISABLED)
		{
			err = mlx4_SET_IF_STAT(dev,1);
			if(err)
			{
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: mlx4_SET_IF_STAT command failed, aborting.\n", dev->pdev->name)); 		   
				goto err_close;
			}		 
		}
	}

	if (!mlx4_is_mfunc(dev))
		mlx4_set_port_mask(dev, &dev->caps, dev->caps.function);

	err = mlx4_QUERY_ADAPTER(dev, &adapter);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: QUERY_ADAPTER command failed, aborting.\n", dev->pdev->name));
		goto err_close;
	}

	priv->eq_table.inta_pin = adapter.inta_pin;
	memcpy(dev->board_id, adapter.board_id, sizeof dev->board_id);

	return 0;

err_close:
	mlx4_CLOSE_HCA(dev, 0);

err_free_icm:
	if (!mlx4_is_slave(dev))
		mlx4_free_icms(dev);

err_stop_fw:
	if (!mlx4_is_slave(dev)) {
		unmap_bf_area(dev);
		mlx4_UNMAP_FA(dev);
		mlx4_free_icm(dev, priv->fw.fw_icm, 0);
	}

	return err;
}

static int mlx4_setup_hca(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int err;
	u8 port;
	u64 ext_port_default_caps;
	__be32 ib_port_default_caps;

	err = mlx4_init_uar_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "user access region table, aborting.\n", dev->pdev->name));
		return err;
	}

	err = mlx4_init_pd_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			"protection domain table, aborting.\n", dev->pdev->name));
		goto err_uar_table_free;
	}

#ifdef SUPPORT_XRC
	err = mlx4_init_xrcd_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize extended "
			 "reliably connected domain table, aborting.\n", dev->pdev->name));
		goto err_pd_table_free;
	}
#endif

	err = mlx4_init_mr_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "memory region table, aborting.\n", dev->pdev->name));
		goto err_xrcd_table_free;
	}

	err = mlx4_init_mcg_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "multicast group table, aborting.\n", dev->pdev->name));
		goto err_mr_table_free;
	}

	err = mlx4_init_eq_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "event queue table, aborting.\n", dev->pdev->name));
		goto err_mcg_table_free;
	}

		err = mlx4_cmd_use_events(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to switch to event-driven "
				 "firmware commands, aborting.\n", dev->pdev->name));
			goto err_eq_table_free;
		}

	err = mlx4_NOP(dev);
	if (err) {
		if ( mlx4_is_msi(dev) ) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: NOP command failed to generate MSI-X "
				  "interrupt IRQ %d, EQN %d\n",
				  dev->pdev->name, 
				priv->eq_table.eq[priv->eq_table.num_eth_eqs].irq,
				priv->eq_table.eq[priv->eq_table.num_eth_eqs].eqn ));
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Trying again without MSI-X.\n", dev->pdev->name));
		} else {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: NOP command failed to generate interrupt "
				 "(IRQ %d), aborting.\n", dev->pdev->name,
				 priv->eq_table.eq[priv->eq_table.num_eth_eqs].irq));
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: BIOS or ACPI interrupt routing problem?\n", dev->pdev->name));
		}

		goto err_cmd_poll;
	}

	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: NOP command IRQ test passed\n", dev->pdev->name));

	err = mlx4_init_cq_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "completion queue table, aborting.\n", dev->pdev->name));
		goto err_cmd_poll;
	}

	err = mlx4_init_srq_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "shared receive queue table, aborting.\n", dev->pdev->name));
		goto err_cq_table_free;
	}

	err = mlx4_init_qp_table(dev);
	if (err) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to initialize "
			 "queue pair table, aborting.\n", dev->pdev->name));
		goto err_srq_table_free;
	}

	if (!mlx4_is_slave(dev)) {
		for (port = 1; port <= dev->caps.num_ports; port++) {
			ib_port_default_caps = 0;
			err = mlx4_get_port_ib_caps(dev, port, &ib_port_default_caps);
			if (err) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: failed to get port %d default "
					  "ib capabilities (%d). Continuing with "
					  "caps = 0\n", dev->pdev->name, port, err));
			}
			dev->caps.ib_port_def_cap[port] = ib_port_default_caps;

			ext_port_default_caps = 0;
			err = mlx4_get_ext_port_caps(dev, port, &ext_port_default_caps);
			if (err) {
				MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "failed to get port %d extended "
					  "port capabilities support (%d). Assuming "
					  "not supported\n", port, err));
			}
			dev->caps.ext_port_cap[port] = ext_port_default_caps;
			
			err = mlx4_SET_PORT(dev, port);
			if (err) {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to set port %d, aborting\n", dev->pdev->name,
					 port));
				goto err_qp_table_free;
			}
		}
	}

	return 0;

err_qp_table_free:
	mlx4_cleanup_qp_table(dev);

err_srq_table_free:
	mlx4_cleanup_srq_table(dev);

err_cq_table_free:
	mlx4_cleanup_cq_table(dev);

err_cmd_poll:
	if (!mlx4_is_slave(dev))
		mlx4_cmd_use_polling(dev);

err_eq_table_free:
	mlx4_cleanup_eq_table(dev);

err_mcg_table_free:
	mlx4_cleanup_mcg_table(dev);

err_mr_table_free:
	mlx4_cleanup_mr_table(dev);

err_xrcd_table_free:
#ifdef SUPPORT_XRC
	mlx4_cleanup_xrcd_table(dev);

err_pd_table_free:
#endif	
	mlx4_cleanup_pd_table(dev);

err_uar_table_free:
	mlx4_cleanup_uar_table(dev);
	return err;
}

static void mlx4_enable_msi_x(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int n_cpus = num_possible_cpus(), n_limit, n_eqs;

	if ( PCI_MULTIFUNCTION_DEVICE(&dev->pdev->pci_cfg_space) ) 
		n_limit = g.max_multi_msix_num;
	else
		n_limit = g.max_single_msix_num;

	// calculate the number of additional EQs for IB in VPI mode
	dev->n_ib_reserved_eqs = MLX4_NUM_IB_EQ;		/* reserve MLX4_NUM_IB_EQ EQs for IB */

	// calculate the number of EQs we can use
	n_eqs = dev->caps.num_eqs - dev->caps.reserved_eqs;

	// calculate the number of completion EQs for Eth. For now we leave MLX4_NUM_IB_EQ EQs for IB
	priv->eq_table.num_eth_eqs = min(n_eqs, dev->pdev->n_msi_vectors_alloc);
	priv->eq_table.num_eth_eqs -= dev->n_ib_reserved_eqs;

	if ( priv->eq_table.num_eth_eqs >= MLX4_NUM_ETH_EQ ) { // MSI-X case

		priv->eq_table.num_eqs = max(dev->n_ib_reserved_eqs + priv->eq_table.num_eth_eqs, n_cpus);
		priv->eq_table.num_eqs = min(n_eqs, priv->eq_table.num_eqs);
		dev->flags |= MLX4_FLAG_MSI_X;

		MLX4_PRINT(TRACE_LEVEL_WARNING ,MLX4_DBG_DRV ,
			(	"%s: MSI-X interrupt mode for %s device \n  MSI-X vectors: \n\tHwSupported \t%d \n\tInfRequested \t%d \n\tRegLimitedTo \t%d \n\tCpuNumber \t\t%d \n\tOsAllocated \t%d "
				"\n  EQs: \n\tHwSupported \t%d \n\tSwSupported \t%d \n\tHwReserved \t\t%d \n\tIbAllocated \t%d \n\tEthAllocated \t%d\n",
			dev->pdev->name, 
			PCI_MULTIFUNCTION_DEVICE(&dev->pdev->pci_cfg_space) ? "multi-function" : "single-function",
			dev->pdev->n_msi_vectors_sup, dev->pdev->n_msi_vectors_req, n_limit, n_cpus, dev->pdev->n_msi_vectors_alloc,  
			dev->dev_cap.max_eqs, dev->caps.num_eqs, dev->caps.reserved_eqs, 
			dev->n_ib_reserved_eqs, priv->eq_table.num_eth_eqs ));
	}
	else { // INTA case

		if ( dev->pdev->n_msi_vectors_alloc ) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
				("%s: Falling back to legacy interrupts as there is not enough MSI-X vectors: n_hw_eqs %d, n_req_eqs %d, n_alloc_vectors %d\n",
				dev->pdev->name, n_eqs, priv->eq_table.num_eth_eqs + dev->n_ib_reserved_eqs,
				dev->pdev->n_msi_vectors_alloc ));
		}

		priv->eq_table.num_eth_eqs = 1;
		priv->eq_table.num_eqs = dev->n_ib_reserved_eqs + priv->eq_table.num_eth_eqs;

		MLX4_PRINT(TRACE_LEVEL_WARNING ,MLX4_DBG_MSIX ,
			("%s: Interrupt mode 'Legacy', vectors (allocated:used:comp) %d:0:%d, n_cpus %d\n",
			dev->pdev->name, dev->pdev->n_msi_vectors_alloc, priv->eq_table.num_eth_eqs, n_cpus));
	}

}


static void mlx4_init_port_info(struct mlx4_dev *dev, int port)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port];

	info->dev = dev;
	info->port = port;
#if WINVER >= 0x602
	info->is_qos_supported = TRUE;
#endif
	if (!mlx4_is_slave(dev)) {
		INIT_RADIX_TREE(&info->mac_tree, GFP_KERNEL);
		mlx4_init_mac_table(dev, &info->mac_table);
		mlx4_init_vlan_table(dev, &info->vlan_table);

		ASSERT(dev->caps.log_num_macs > 0);
		info->base_qpn = dev->caps.reserved_qps_base[MLX4_QP_REGION_ETH_ADDR] +
			((port - 1) * (1 << dev->caps.log_num_macs))*
			((port - 1) * (1 << dev->caps.log_num_vlans))*
			((port - 1) * (1 << dev->caps.log_num_prios));

		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: [port %u] Reserved QP Based: 0x%x\n", 
			dev->pdev->name, port, info->base_qpn));
	}
}

static void mlx4_cleanup_port_info(struct mlx4_port_info *info)
{
	if (!mlx4_is_slave(info->dev)) {
		RMV_RADIX_TREE(&info->mac_tree);
	}
}


static void mlx4_cleanup_port_qos_settings(struct mlx4_qos_settings* p_qos_setting)
{
	if(p_qos_setting->p_qos_settings != NULL)
	{
		kfree(p_qos_setting->p_qos_settings);
	}
}

static int mlx4_init_steering(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	int num_entries = max(dev->caps.num_ports, dev->caps.pf_num);
	int i, j;

	ASSERT(!mlx4_is_slave(dev));
	
	priv->steer = (mlx4_steer *)kzalloc(sizeof(struct mlx4_steer) * num_entries, GFP_KERNEL);
	if (!priv->steer)
		return -ENOMEM;

	for (i = 0; i < num_entries; i++) {
		for (j = 0; j < MLX4_NUM_STEERS; j++) {
			INIT_LIST_HEAD(&priv->steer[i].promisc_qps[j]);
			INIT_LIST_HEAD(&priv->steer[i].steer_entries[j]);
		}
	}
	return 0;
}


static void mlx4_clear_steering(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_steer_index *entry, *tmp_entry;
	struct mlx4_promisc_qp *pqp, *tmp_pqp;
	int num_entries = max(dev->caps.num_ports, dev->caps.pf_num);
	int i, j;

	for (i = 0; i < num_entries; i++) {
		for (j = 0; j < MLX4_NUM_STEERS; j++) {
			list_for_each_entry_safe(pqp, tmp_pqp,	&priv->steer[i].promisc_qps[j],
					list, struct mlx4_promisc_qp, struct mlx4_promisc_qp) { 
				list_del(&pqp->list);
				kfree(pqp);
			}

			list_for_each_entry_safe(entry, tmp_entry, &priv->steer[i].steer_entries[j],
					 list, struct mlx4_steer_index, struct mlx4_steer_index) {
				list_del(&entry->list);
				list_for_each_entry_safe(pqp, tmp_pqp, &entry->duplicates, list,
						struct mlx4_promisc_qp, struct mlx4_promisc_qp) {
					list_del(&pqp->list);
					kfree(pqp);
				}
				kfree(entry);
			}
		}
	}
 
	kfree(priv->steer);
}

int mlx4_init_one(struct pci_dev *pdev, struct mlx4_dev_params *dev_params)
{
	struct pci_device_id *id;
	struct mlx4_priv *priv;
	struct mlx4_dev *dev;
	int err;
	NTSTATUS status;
	int port;
	int i;

	MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_LOW ,
		(">>>> %s: Calling mlx4_init_one. (%p)\n", pdev->name, pdev));

	st_et_event("Bus: mlx4_init_one started\n");

	/* we are going to recreate device anyway */
	pdev->dev = NULL;
	pdev->ib_dev = NULL;
	
	/* find the type of device */
	id = mlx4_find_pci_dev(pdev->ven_id, pdev->dev_id);
	if (id == NULL) {
		err = -ENOSYS;
		goto err;
	}

	/*
	 * Check for BARs.	We expect 0: 1MB, 2: 8MB, 4: DDR (may not
	 * be present)
	 */
#if 0	 
	if (pci_resource_len(pdev, 0) != 1 << 20) {
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_LOW ,
			("Missing DCS, aborting.\n"));
		err = -ENODEV;
		goto err;
	}
#endif	
	if (!pci_resource_len(pdev, 1)) {
		MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_LOW ,
			("%s: Missing UAR, aborting.\n", pdev->name));
		err = -ENODEV;
		goto err;
	}

	/* allocate mlx4_priv structure */
	priv = (struct mlx4_priv *)kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv) {
		MLX4_PRINT(TRACE_LEVEL_INFORMATION ,MLX4_DBG_LOW ,
			("%s: Device struct alloc failed, aborting.\n", pdev->name));
		err = -ENOMEM;
		goto err;
	}
	/* must be here for livefish */
	INIT_LIST_HEAD(&priv->ctx_list);
	spin_lock_init(&priv->ctx_lock);

	mutex_init(&priv->port_mutex);

	INIT_LIST_HEAD(&priv->pgdir_list);
	mutex_init(&priv->pgdir_mutex);

	INIT_LIST_HEAD(&priv->bf_list);
	mutex_init(&priv->bf_mutex);

	pdev->p_stat->priv = priv;

	/* deal with livefish, if any */
	dev 	  = &priv->dev;
	dev->signature = MLX4_DEV_SIGNATURE;
	dev->pdev = pdev;
	pdev->dev = dev;
	dev->rev_id = pdev->revision_id;

	atomic_set(&priv->opreq_count, 0);
	priv->opreq_work = IoAllocateWorkItem( pdev->p_self_do );
	if(!priv->opreq_work){
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_LOW , ("%s: Failed to allocate work item\n", pdev->name));
		err = -ENOMEM;
		goto err_free_dev;
	}

	/* Detect if this device is a virtual function */
	if (id->driver_data & MLX4_VF) {
		/* When acting as pf, we normally skip vfs unless explicitly
		 * requested to probe them.
		 * TODO: add ARI support */
		if (g.sr_iov && PCI_FUNC(pdev->devfn) > g.probe_vf) {			
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Skipping virtual function:%d\n",
				dev->pdev->name, PCI_FUNC(pdev->devfn)));
			err = -ENODEV;
			goto err_workqueue;
		}
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Detected virtual function - running in slave mode\n", dev->pdev->name));
		dev->flags |= MLX4_FLAG_SLAVE | MLX4_FLAG_VF;
	}

	build_log_name( dev, pdev->pci_bus, pdev->pci_device, pdev->pci_func );

	for (i = 0; i < MLX4_MAX_PORTS; i++) 
	{	
		// BUGBUG: Tzachid Need to decide if we are counting from 0 or from 1 !!!
		dev->dev_params.mod_port_type[i] = dev_params->mod_port_type[i];
		dev->dev_params.enable_fcox[i] = dev_params->enable_fcox[i];
		dev->dev_params.enable_roce[i+1] = dev_params->enable_roce[i+1];
	}

	/* We reset the device and enable SRIOV only for physical devices */
	if (!mlx4_is_slave(dev)) {
		
		/* Claim ownership on the device, if already taken, act as slave*/
		err = mlx4_get_ownership(dev);
		if (err) {
			if (err < 0) {
				goto err_workqueue;
			} else {
				err = 0;				
				dev->flags |= MLX4_FLAG_SLAVE;
				build_log_name( dev, pdev->pci_bus, pdev->pci_device, pdev->pci_func );
				goto slave_start;
			}
		}

		/*
		 * Now reset the HCA before we touch the PCI capabilities or
		 * attempt a firmware command, since a boot ROM may have left
		 * the HCA in an undefined state.
		 */
		status = mlx4_reset(dev);
		if ( !NT_SUCCESS( status ) ) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to reset HCA, aborting.(status %#x)\n", dev->pdev->name, status));
			err = -EFAULT;
			goto err_workqueue;
		}
		if (g.sr_iov) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Enabling sriov with:%d vfs\n", dev->pdev->name, g.sr_iov));
#ifdef SUPPORT_SRIOV			
			// one needs to implement pci_enable_sriov()
			if (pci_enable_sriov(pdev, g.sr_iov)) {
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to enable sriov, aborting.\n", dev->pdev->name));
				err = -EFAULT;
				goto err_workqueue;
			}
#endif			
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Running in master mode\n", dev->pdev->name));
			dev->flags |= MLX4_FLAG_SRIOV | MLX4_FLAG_MASTER;
		}
		build_log_name( dev, pdev->pci_bus, pdev->pci_device, pdev->pci_func );
	}

slave_start:
	/* In slave functions, the communication channel must be initialized before
	 * posting commands */
	if (mlx4_is_slave(dev)) {
		err = mlx4_cmd_init(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to init command interface, aborting.\n", dev->pdev->name));
			goto err_workqueue;
		}
		
		// (for slave) check whether PPF is not being removed now
		if ( !g.ppf_is_active ) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: PPF is being removed now. Aborting ...\n", dev->pdev->name ));
			err = -EFAULT;
			goto err_cmd;
		}
		priv->remove_work = IoAllocateWorkItem( pdev->p_self_do );
		if (!priv->remove_work) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to allocate work item for forced remove\n", dev->pdev->name));
			err = -EFAULT;
			goto err_cmd;
		}
		err = mlx4_multi_func_init(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Failed to init slave mfunc interface, aborting.\n", dev->pdev->name));
			goto err_remove_work;
		}
	}

	err = mlx4_init_hca(dev);
	if (err) {
		if (err == -EACCES) {
			/* Not primary Physical function
			 * Running in slave mode */
			mlx4_cmd_cleanup(dev);
			dev->flags |= MLX4_FLAG_SLAVE;
			dev->flags &= ~MLX4_FLAG_MASTER;
			build_log_name( dev, pdev->pci_bus, pdev->pci_device, pdev->pci_func );
			dev->num_slaves = 0;
			if ( ++g.cur_slave_num > g.slave_num )	  { 			   
				MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
					("%s: Slave is not allowed by customer or disallowed because IB is not supported by Flex10 slaves. (max_allowed_slaves %d, cur %d)\n",
					dev->pdev->name, priv->eq_table.num_eqs, dev->pdev->n_msi_vectors_alloc ));
				--g.cur_slave_num;
				err = -EFAULT;
				goto err_slave_mfunc_init;
			}
			if (mlx4_is_slave(dev)) {
				mlx4_multi_func_cleanup(dev);
				ASSERT(priv->remove_work);
				IoFreeWorkItem(priv->remove_work);
			}
			goto slave_start; 
		} else
			goto err_slave_mfunc_init;
	}

	/* Restore the clp version for later use */
	pdev->clp_ver = dev->caps.clp_ver;

	/* In master functions, the communication channel must be initialized after obtaining
	 * its address from fw */
	if (mlx4_is_master(dev)) {
		dev->num_slaves = MLX4_MAX_NUM_SLAVES;
		err = mlx4_multi_func_init(dev);
		if (err) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s, Failed to init master mfunc interface, aborting.\n", dev->pdev->name));
			goto err_close;
		}
	}

	err = mlx4_alloc_eq_table(dev);
	if (err)
		goto err_sriov;

	mlx4_enable_msi_x(dev);

	if (dev->caps.num_eqs - dev->caps.reserved_eqs < priv->eq_table.num_eqs) {
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "%s: Not enough EQs (requires %d, existed %d). Aborting ...\n", 
			dev->pdev->name, priv->eq_table.num_eqs, dev->caps.num_eqs - dev->caps.reserved_eqs));
		err = -EFAULT;
		goto err_free_eq;
	}

	if (!mlx4_is_slave(dev)) {
		err = mlx4_init_steering(dev);
		if (err)
			goto err_free_eq;
	}

	err = mlx4_setup_hca(dev);
	if (err == -EBUSY && mlx4_is_msi(dev) && !mlx4_is_slave(dev)) {
		dev->flags &= ~MLX4_FLAG_MSI_X;
//		pci_disable_msix(pdev);
		err = mlx4_setup_hca(dev);
	}

	if (err)
		goto err_steer;

	for (port = 1; port <= dev->caps.num_ports; port++)
		mlx4_init_port_info(dev, port);

	// for roce device, we update the gid table, before we first read the gid cache.
	for ( i = 1; i < dev->caps.num_ports + 1; ++i ) {
		if(dev->dev_params.enable_roce[i]) {
			err = mlx4_update_ipv6_gids_win(dev, i, 0, dev->caps.def_mac[i]);
			if (err)
				goto err_port;
		}
	}

	err = mlx4_register_device(dev);
	if (err)
		goto err_port;


	MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: NET device (dev_id=%d) is INITIALIZED ! \n", 
		dev->pdev->name, (int)pdev->dev_id));
	MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_LOW ,
		("<<<< %s: mlx4_init_one Completed. (%p)\n", pdev->name, pdev));

	if ( mlx4_is_master(dev) )
		g.ppf_is_active = TRUE;

	return 0;

err_port:
	for (port = 1; port <= dev->caps.num_ports; port++)
		mlx4_cleanup_port_info(&priv->port[port]);

	mlx4_cleanup_mcg_table(dev);
	mlx4_cleanup_qp_table(dev);
	mlx4_cleanup_srq_table(dev);
	mlx4_cleanup_cq_table(dev);
		mlx4_cmd_use_polling(dev);
	mlx4_cleanup_eq_table(dev);
	mlx4_cleanup_mr_table(dev);
#ifdef SUPPORT_XRC
	mlx4_cleanup_xrcd_table(dev);
#endif
	mlx4_cleanup_pd_table(dev);
	mlx4_cleanup_uar_table(dev);

err_steer:
	if (!mlx4_is_slave(dev))
		mlx4_clear_steering(dev);

err_free_eq:
	mlx4_free_eq_table(dev);

err_sriov:
	if (mlx4_is_master(dev))
		mlx4_multi_func_cleanup(dev);
	if (g.sr_iov && (dev->flags & MLX4_FLAG_SRIOV))
//		pci_disable_sriov(pdev);

err_close:
//	if (mlx4_is_msi(dev))
//		pci_disable_msix(pdev);

	mlx4_close_hca(dev);

err_slave_mfunc_init:
	if (mlx4_is_slave(dev)) {
		mlx4_multi_func_cleanup(dev);
	}

err_remove_work:
	if (mlx4_is_slave(dev)) {
		ASSERT(priv->remove_work);
		IoFreeWorkItem(priv->remove_work);
	}

err_cmd:
	mlx4_cmd_cleanup(dev);

err_workqueue:
	ASSERT(priv->opreq_work);
	IoFreeWorkItem(priv->opreq_work);
	
err_free_dev:
	pdev->dev = NULL;
	pdev->p_stat->priv = NULL;	  
	if (!mlx4_is_slave(dev))
		mlx4_free_ownership(dev);	 
	kfree(priv);

err:
	st_et_event("Bus: mlx4_init_one ended with err %d\n", err);
	MLX4_PRINT(TRACE_LEVEL_ERROR ,MLX4_DBG_LOW ,
		("<<<< %s: mlx4_init_one Completed Error=0x%x. (%p)\n", pdev->name, err, pdev));
	return err;
}

void mlx4_remove_one(struct pci_dev *pdev)
{
	struct mlx4_dev  *dev;
	struct mlx4_priv *priv;
	int p;
	LARGE_INTEGER wait_time;

	st_et_event("Bus: mlx4_remove_one started\n");

	KeWaitForSingleObject(&pdev->remove_dev_lock, Executive, KernelMode, FALSE, NULL);
	dev  = pdev->dev;
	if (dev) {
		priv = mlx4_priv(dev);
		
		/* Stop serving commands and events over comm channel */
		if (mlx4_is_master(dev)) {
			int i=0, n_slaves = priv->mfunc.master.n_active_slaves;

			KeWaitForSingleObject(&g.start_event, Executive, KernelMode, FALSE, NULL);
			g.ppf_is_active = FALSE;
			KeSetEvent(&g.start_event, 0, FALSE);

			MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,("%s: mlx4_remove_one has to remove %d PFs.\n",
				pdev->name, n_slaves));

#if 0
			{
				struct mlx4_eqe eqe;
				// send event to remove itself to all the slaves
				RtlZeroMemory(&eqe, sizeof(eqe));
				eqe.type = MLX4_EVENT_TYPE_PPF_REMOVE;
				mlx4_warn(dev, "%s: Got REMOVE_DEVICE command. Going to remove first all the slaves.\n",
					dev->pdev->name);
				mlx4_slave_event_all(dev, &eqe);
			}
#endif

			// wait for the slaves to exit
			wait_time.QuadPart	= (-10)* (__int64)(HZ);
			while(priv->mfunc.master.n_active_slaves > 0) {
				MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: %d from %d are still active. Waiting already for %d seconds before exit master working thread.\n",
					dev->pdev->name, priv->mfunc.master.n_active_slaves, n_slaves, i));
				KeDelayExecutionThread( KernelMode, FALSE, &wait_time );
				i++;
			}
			IoFreeWorkItem(priv->mfunc.master.vep_config_work);
			priv->mfunc.master.vep_config_work = NULL;
			stop_working_thread(&priv->mfunc.master.slave_event_thread);
			stop_working_thread(&priv->mfunc.master.comm_channel_thread);
		}

		dev->flags |= MLX4_FLAG_UNLOAD;
		
		mlx4_unregister_device(dev);
		for (p = 1; p <= dev->caps.num_ports; p++) {
			mlx4_cleanup_port_info(&priv->port[p]);
			mlx4_cleanup_port_qos_settings(&priv->qos_settings[p-1]);
			if (!mlx4_is_slave(dev))
				mlx4_CLOSE_PORT(dev, p);
		}
		
		mlx4_cleanup_mcg_table(dev);
		mlx4_cleanup_qp_table(dev);
		mlx4_cleanup_srq_table(dev);
		mlx4_cleanup_cq_table(dev);

			mlx4_cmd_use_polling(dev);

		if (!mlx4_is_slave(dev))
			mlx4_clear_steering(dev);
		mlx4_cleanup_eq_table(dev);
			mlx4_cleanup_mr_table(dev);
#ifdef SUPPORT_XRC			
		mlx4_cleanup_xrcd_table(dev);
#endif
		mlx4_cleanup_pd_table(dev);

		mlx4_cleanup_uar_table(dev);
		mlx4_free_eq_table(dev);
		mlx4_close_hca(dev);
		if (mlx4_is_mfunc(dev))
			mlx4_multi_func_cleanup(dev);
		mlx4_cmd_cleanup(dev);

		if (mlx4_is_msi(dev))
//			pci_disable_msix(pdev);
		if (g.sr_iov && (dev->flags & MLX4_FLAG_SRIOV)) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Disabling sriov\n", dev->pdev->name));
#ifdef SUPPORT_SRIOV			
			// one needs to implement pci_disable_sriov()
			pci_disable_sriov(pdev);
#endif
		}

		if (mlx4_is_slave(dev))
			--g.cur_slave_num;
		else
			mlx4_free_ownership(dev);	 
			

		MLX4_PRINT(TRACE_LEVEL_VERBOSE, MLX4_DBG_DRV,( "%s: NET device (dev_id=%d) is REMOVED ! \n", 
			dev->pdev->name, (int)pdev->dev_id));
		pdev->dev = NULL;
		kfree(priv);
	}
	KeSetEvent(&pdev->remove_dev_lock, 0, FALSE);

	st_et_event("Bus: mlx4_remove_one ended\n");

}

int mlx4_restart_one(struct pci_dev *pdev)
{
	int res;
	struct mlx4_dev_params dev_params;
	mlx4_copy_dev_params(&dev_params, &pdev->dev->dev_params);

	st_mm_enable_cache(TRUE);
	st_mm_report("Upon mlx4_restart_one before mlx4_remove_one");

	mlx4_remove_one(pdev);

	st_mm_report("Upon mlx4_restart_one before mlx4_init_one");

	res = mlx4_init_one(pdev, &dev_params);

	st_mm_report("Upon mlx4_restart_one after mlx4_init_one");   
	st_mm_enable_cache(FALSE);
	return res;
}

static int __init mlx4_verify_params(void)
{
	if ((g.log_num_mac < 0) || (g.log_num_mac > 7)) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_core: bad num_mac: %d\n", g.log_num_mac));
		return -1;
	}

	if ((g.log_num_vlan < 0) || (g.log_num_vlan > 7)) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_core: bad num_vlan: %d\n", g.log_num_vlan));
		return -1;
	}

	if ((g.log_mtts_per_seg < 1) || (g.log_mtts_per_seg > 5)) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "mlx4_core: bad log_mtts_per_seg: %d\n", g.log_mtts_per_seg));
		return -1;
	}

	return 0;
}

int mlx4_net_init()
{
	if (mlx4_verify_params())
		return -EINVAL;
	
	// for sense polling 
//	mlx4_wq = create_singlethread_workqueue("mlx4");
//	if (!mlx4_wq)
//		return -ENOMEM;

	mlx4_intf_init();
	return 0;
}
static void __exit mlx4_net_cleanup(void)
{
//	destroy_workqueue(mlx4_wq);
}

/*****************************************************************************/

static void mlx4_str2port_type(WCHAR **port_str,
				   enum mlx4_port_type *port_type)
{
	int i;

	for (i = 0; i < MLX4_MAX_PORTS; i++) {
		if (!wcscmp(port_str[i], L"eth"))
			port_type[i] = MLX4_PORT_TYPE_ETH;
		else
			port_type[i] = MLX4_PORT_TYPE_IB;
	}
}

BOOLEAN mlx4_is_allowed_port(struct mlx4_dev *dev, int port_number)
{
	if (dev->caps.port_mask[port_number] != MLX4_PORT_TYPE_NONE) {
		return TRUE;
	}
	return FALSE;
}

int mlx4_count_ib_ports(struct mlx4_dev *dev)
{
	u8 i;
	int count = 0;

	for (i = 0; i < dev->caps.num_ports; i++) {
		//TODO: We should deal the case when allowed only 2nd port
		if ( mlx4_is_allowed_port(dev, i+1) )
			if (mlx4_get_effective_port_type(dev,i+1) == MLX4_PORT_TYPE_IB) {
				count++;
			}
	}
	return count;
}

char* mlx4_get_port_name(struct mlx4_dev *dev, u8 port_num)
{
	u8 port_type;
	char *name = "OFF";

	if ( !mlx4_is_allowed_port(dev, port_num) )
		goto exit;

	port_type = (u8)(dev->caps.port_type_final[port_num]);
	if ( port_type == MLX4_PORT_TYPE_IB ) {
		name = "IB";
		goto exit;
	}
	
	if ( port_type == MLX4_PORT_TYPE_ETH ) {
		if ( mlx4_get_effective_port_type(dev,port_num) == MLX4_PORT_TYPE_IB) {
			name = "RoCE";
		}
		else {
			name = "ETH";
		}
		goto exit;
	}
	
	if ( port_type == MLX4_PORT_TYPE_AUTO ) 
		name = "AUTO";
	else
		name = "UNKNOWN";

exit:	
	return name;
}

BOOLEAN mlx4_is_eth_port(struct mlx4_dev *dev, int port_number)
{
	if (dev->caps.port_type_final[port_number] == MLX4_PORT_TYPE_ETH) {
		return TRUE;
	}
	return FALSE;
}

BOOLEAN mlx4_is_ib_port(struct mlx4_dev *dev, int port_number)
{
	if (dev->caps.port_type_final[port_number] == MLX4_PORT_TYPE_IB){
		return TRUE;
	}
	return FALSE;
}

BOOLEAN mlx4_is_roce_port(struct mlx4_dev *dev, int port_number)
{
	if(! mlx4_is_allowed_port(dev, port_number))
	{
		return FALSE;
	}
	
	if (dev->dev_params.enable_roce[port_number]) {
		return TRUE;
	}
	return FALSE;
}

int mlx4_count_roce_ports(struct mlx4_dev *dev)
{
	u8 i;
	int count = 0;

	for (i = 0; i < dev->caps.num_ports; i++) {
		//TODO: We should deal the case when allowed only 2nd port
		if ( mlx4_is_allowed_port(dev, i+1) ) {
			if ( mlx4_is_roce_port(dev, i+1) ) {
				count++;
			}
		}
	}
	return count;
}

int mlx4_count_eth_ports(struct mlx4_dev *dev)
{
	u8 i;
	int count = 0;

	for (i = 0; i < dev->caps.num_ports; i++) {
		//TODO: We should deal the case when allowed only 2nd port
		if ( mlx4_is_allowed_port(dev, i+1) ) {
			if ( mlx4_is_eth_port(dev, i+1) ) {
				count++;
			}
		}
	}
	return count;
}

BOOLEAN mlx4_is_enabled_port(struct mlx4_dev *dev, int port_number)
{
	if (dev->caps.port_state[port_number] == MLX4_PORT_ENABLED) {
		return mlx4_is_allowed_port(dev,port_number);
	}
	return FALSE;
}


u8 get_slave(struct mlx4_dev* dev, u8 port, u8 vep_num)
{
	struct mlx4_priv* priv = mlx4_priv(dev);
	struct mlx4_slave_state* slaves = priv->mfunc.master.slave_state;
	u8 i;
	
	for(i = 0; i < dev->num_slaves; ++i)
		if ((slaves[i].port_num == port) && (slaves[i].vep_num == vep_num))
			return i;

	ASSERT(FALSE);
	return 0;
}

BOOLEAN mlx4_is_qos_supported(struct mlx4_dev *dev, int port_number)
{
	struct mlx4_port_info *info = &mlx4_priv(dev)->port[port_number];
	return info->is_qos_supported;
}



