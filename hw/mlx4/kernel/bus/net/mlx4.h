/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#ifndef MLX4_H
#define MLX4_H

#include "l2w.h"
#include "driver.h"
#include "doorbell.h"
#include "bus_intf.h"
#include "eq.h"
#include "cmd.h"
#include "ib_verbs.h"
#include "Vip_dev.h"


//
// Structure for reporting data to WMI
//

typedef struct _BUS_WMI_STD_DATA {
	UINT32 DebugPrintLevel;
	UINT32 DebugPrintFlags;

} BUS_WMI_STD_DATA, * PBUS_WMI_STD_DATA;

//
// Driver global data
//

enum mlx4_port_state {
	MLX4_PORT_ENABLED	= 1 << 0,
	MLX4_PORT_DISABLED	= 1 << 1,
};

enum fw_issue_handle {
	RESUME_RUNNING,
	BUSY_WAIT,
	IMMIDIATE_BREAK
  
};

#pragma warning(disable:4201) // nameless struct/union
typedef struct _GLOBALS {
	BUS_WMI_STD_DATA bwsd;

	int log_num_qp;
	int log_rdmarc_per_qp;
	int log_num_srq;
	int log_num_cq;
	int log_num_mcg;
	int log_num_mpt;
	int log_num_mtt;
	int log_num_mac;
	int log_num_vlan;
	int use_prio;
	int num_fc_exch;

	int enable_qos;
	int mlx4_blck_lb;
	int interrupt_from_first;

	int PortType1;
	int PortType2;

	int affinity;
	int stat_flags; 		/* flags for statistics and tracing */

	/* Flex10 */
	int mode_flags; 		/* flags for various mode of work */
	int slave_num;			/* max number of PFs in multi-function mode */
	int log_mtts_per_seg;	/* log2 of MTTs per segment */
	int sr_iov; 			/* enable sr_iov support */
	int probe_vf;			/* number of vfs to probe by pf driver (when sr_iov > 0) */
	int set_4k_mtu; 		/* set 4K MTU */
	KEVENT start_event; 	/* start/remove sync event */
	//BUGBUG: SRIOV is not supported
	KEVENT surprise_removal_event;		/* surprise_removal_event sync event */
	int ppf_is_active;

	int cur_slave_num;		/* current number of PFs in multi-function mode */

	int mlx4_pre_t11_mode; /* enable pre-t11 mode (0=disabled; 1=enabled) */		
	int max_single_msix_num;	/* max number of MSI-X for Flex10 */
	int max_multi_msix_num;		/* max number of MSI-X for Flex10 */
	int max_working_threads;	/* Max Working Threads */
	int busy_wait_behavior;

	int cmd_num_to_fail; 	/* The command number which should be failed by the test */
		
} GLOBALS;
#pragma warning(default:4201) // nameless struct/union

extern GLOBALS g;

// mode flags
#define MLX4_MODE_SKIP_CHILDS			(1 << 0)	/* don't create child devices */
#define MLX4_MODE_COMM_TSTAMP			(1 << 1)	/* time-stamp COMM commands */
#define MLX4_MODE_PPF_UNVISIBLE 		(1 << 3)	/* make PPF unvisible */
#define MLX4_MODE_RESET_AFTER_CMD_TO	(1 << 4)	/* perform card reset after command timeout */
#define MLX4_MODE_RESTORE_MSIX			(1 << 5)	/* always perform save/restore MSI-X */
#define MLX4_MODE_RESTORE_PCI_CFG		(1 << 6)	/* always restore PCI config space */
#define MLX4_MODE_PRINT_MSIX_INFO		(1 << 7)	/* print MSI-X info before and after card reset */
#define MLX4_MODE_USE_CQE32				(1 << 8)	/* use CQE of 32 bytes despite CQE of 64 bytes is supported */
#define MLX4_MODE_SIM_FDR				(1 << 9)	/* simulate FDR */
#define MLX4_MODE_SIM_FDR10				(1 << 10)	/* simulate FDR10 */


enum {
	MLX4_HCR_BASE		= 0x80680,
	MLX4_HCR_SRIOV_BASE = 0x4080680, /* good for SRIOV FW ony */
	MLX4_HCR_SIZE		= 0x0001c,
	MLX4_CLR_INT_SIZE	= 0x00008,
	MLX4_SLAVE_COMM_BASE	= 0x0,
	MLX4_COMM_PAGESIZE	= 0x1000
};

enum {
	MLX4_MGM_ENTRY_SIZE =  0x200,
	MLX4_QP_PER_MGM 	= 4 * (MLX4_MGM_ENTRY_SIZE / 16 - 2),
	MLX4_MTT_ENTRY_PER_SEG	= 8
};

enum {
	MLX4_EQ_COMMANDS,
	MLX4_EQ_IB_COMP,
	MLX4_NUM_IB_EQ,
	MLX4_NUM_ETH_EQ = 2
};
#define MLX4_NUM_UNKNOWN	-1

enum {
	MLX4_NUM_PDS		= 1 << 15,
};

enum {
	MLX4_CMPT_TYPE_QP	= 0,
	MLX4_CMPT_TYPE_SRQ	= 1,
	MLX4_CMPT_TYPE_CQ	= 2,
	MLX4_CMPT_TYPE_EQ	= 3,
	MLX4_CMPT_NUM_TYPE
};

enum {
	MLX4_CMPT_SHIFT 	= 24,
	MLX4_NUM_CMPTS		= MLX4_CMPT_NUM_TYPE << MLX4_CMPT_SHIFT
};

#define MLX4_COMM_TIME		10000
enum {
	MLX4_COMM_CMD_RESET,
	MLX4_COMM_CMD_VHCR0,
	MLX4_COMM_CMD_VHCR1,
	MLX4_COMM_CMD_VHCR2,
	MLX4_COMM_CMD_VHCR_EN,
	MLX4_COMM_CMD_VHCR_POST,
	MLX4_COMM_CMD_DUMMY
};

enum mlx4_resource {
	RES_QP,
	RES_CQ,
	RES_SRQ,
	RES_MPT,
	RES_MTT,
	RES_MAC_AND_VLAN,
	RES_VLAN,
	RES_MCAST,
	/* update after adding new resource*/
	MLX4_NUM_OF_RESOURCE_TYPE = 8
};

enum mlx4_alloc_mode {
	ICM_RESERVE_AND_ALLOC,
	ICM_RESERVE,
	ICM_ALLOC,
	ICM_MAC_VLAN,
};

enum {
	MLX4_MFUNC_MAX		= 64,
	MLX4_MFUNC_EQ_NUM	= 4,
	MLX4_MFUNC_MAX_EQES = 8,
	MLX4_MFUNC_EQE_MASK = (MLX4_MFUNC_MAX_EQES - 1)
};


#define MGM_QPN_MASK	   0x00FFFFFF
#define MGM_BLCK_LB_BIT    30

#define MLX4_MAX_NUM_PF 	16
#define MLX4_MAX_NUM_VF 	64
#define MLX4_MAX_NUM_SLAVES (MLX4_MAX_NUM_PF + MLX4_MAX_NUM_VF)
#define MLX4_MAX_NUM_VF_PER_PF	4

struct mlx4_bitmap {
	u32 		last;
	u32 		top;
	u32 		max;
	u32 		effective_max;
	u32 		mask;
	u32			avail;
	spinlock_t		lock;
	unsigned long		   *table;
};

struct mlx4_buddy {
	unsigned long		  **bits;
	unsigned int		   *num_free;
	int 		max_order;
	spinlock_t		lock;
};

struct mlx4_icm;

struct mlx4_icm_table {
	u64 		virt;
	int 		num_icm;
	int 		num_obj;
	int 		obj_size;
	int 		lowmem;
	int 		coherent;
	struct mutex		mutex;
	struct mlx4_icm 	  **icm;
};

struct mlx4_profile {
	int 		num_qp;
	int 		rdmarc_per_qp;
	int 		num_srq;
	int 		num_cq;
	int 		num_mcg;
	int 		num_mpt;
	int 		num_mtt;
};

struct mlx4_fw {
	u64 		clr_int_base;
	u64 		catas_offset;
	u64 		comm_base;
	struct mlx4_icm 	   *fw_icm;
	struct mlx4_icm 	   *aux_icm;
	u32 		catas_size;
	u16 		fw_pages;
	u8			clr_int_bar;
	u8			catas_bar;
	u8			comm_bar;
};

struct mlx4_comm {
	u32 		slave_write;
	u32 		slave_read;
};

#define VLAN_FLTR_SIZE	128
struct mlx4_vlan_fltr {
	__be32 entry[VLAN_FLTR_SIZE];
};

#define GID_SIZE		16

enum mlx4_resource_state {
	RES_INIT = 0,
	RES_RESERVED = 1,
	RES_ALLOCATED = 2,
	RES_ALLOCATED_AFTER_RESERVATION = 3,
/*When registered mac the master reserved that qp, but the allocation should be in the slave*/
	RES_ALLOCATED_WITH_MASTER_RESERVATION = 4
};

struct mlx_tracked_qp_mcg {
	u8 gid[GID_SIZE];
	enum mlx4_protocol prot;
	struct list_head list;
};

struct mlx_tracked_vln_fltr {
	int port;
	struct mlx4_vlan_fltr vlan_fltr;
};

struct mlx4_tracked_resource {
	int slave_id;
	int res_type;
	int resource_id;
	/* state indicates the allocation stage,
	   importance where there is reservation and after that allocation
	*/
	unsigned long state;
	union {
		struct list_head mcg_list ; /*for the QP res*/
		u8 port ; /*for the MAC res and VLAN*/
		int order;/*for the MTT object*/
	} specific_data;
	struct list_head list;
};

struct mlx4_resource_tracker {
	spinlock_t lock;
	/* tree for each resources */
	struct radix_tree_root res_tree[MLX4_NUM_OF_RESOURCE_TYPE];
	/* num_of_slave's lists, one per slave */
	struct list_head *res_list;
};

struct mlx4_slave_eqe {
	u8 type;
	u8 port;
	u32 param;
};

struct mlx4_mcast_entry {
	struct list_head list;
	u64 addr;
};

struct mlx4_promisc_qp {
	struct list_head list;
	u32 qpn;
};

struct mlx4_steer_index { 
	struct list_head list;
	unsigned int index;
	struct list_head duplicates;
};

struct slave_event_eq_info {
	u32 eqn;
	u8	f_use_int; 
	u16 token;
	u64 event_type;
};

struct slave_register_mac_and_vlan {
	int*	qpn;
	u64 	mac;
	u8		check_vlan;
	u8 		vlan_valid;
	u16 	vlan;
	bool	reserve;
};

struct mlx4_slave_state {
	u8 comm_toggle; 			/* last toggle written */
	u8 last_cmd;				/* last command done */
	u8 init_port_mask;
	u8 pf_num;
	u8 vep_num;
	u8 port_num;
	bool active;
	u8 function;	
	dma_addr_t vhcr_dma;
	u16 mtu[MLX4_MAX_PORTS + 1];
	__be32 ib_cap_mask[MLX4_MAX_PORTS + 1];
	struct mlx4_slave_eqe eq[MLX4_MFUNC_MAX_EQES];
	struct list_head mcast_filters[MLX4_MAX_PORTS + 1];
	struct mlx4_vlan_fltr *vlan_filter[MLX4_MAX_PORTS + 1];
	struct slave_event_eq_info event_eq;
	struct mlx4_vep_cfg vep_cfg;
	u16 eq_pi;
	u16 eq_ci;
	int sqp_start;
	spinlock_t lock;
	// debug
	u32 				poll_stamp; 	/* time when last cmd found */
	u32 				done_stamp; 	/* time when last cmd done */
	u32 				cmd_num;		/* time when last cmd done */
};

struct working_thread {
	HANDLE		h_thread;	 
	KEVENT		trig_event;
	BOOLEAN 	f_stop;
};

#define SLAVE_EVENT_EQ_SIZE 128
struct slave_event_eq {
	int 		eqn;	
	u32 		cons_index;
	u32 		prod_index;
	struct mlx4_eqe event_eqe[SLAVE_EVENT_EQ_SIZE];
};

struct mlx4_mfunc_master_ctx {
	struct mlx4_slave_state *slave_state;
	int 		init_port_ref[MLX4_MAX_PORTS + 1];
	u16 		max_mtu[MLX4_MAX_PORTS + 1];
	int 		disable_mcast_ref[MLX4_MAX_PORTS + 1];
	u8			vep_num[MLX4_MAX_PORTS + 1];
	struct mlx4_resource_tracker res_tracker;
	int 		n_active_slaves;
	struct working_thread comm_channel_thread; 
	u32 		comm_arm_bit_vec[4];	
	struct mlx4_eqe cmd_eqe;
	struct slave_event_eq slave_eq;
	struct working_thread slave_event_thread;

	spinlock_t vep_config_lock;
	PIO_WORKITEM	vep_config_work;
	bool		vep_config_queued;
	u16 		vep_config_bitmap;
};

struct mlx4_mfunc_slave_ctx {
	int 		dummy;
	// debug
	u32 				issue_stamp;	/* time when last cmd issued */
	u32 				to_stamp;		/* time when last cmd timed out */
	u32 				last_cmd;		/* all command field including toggle */
	PKTHREAD			p_thread;		/* last thread, issued the command */
	u32 				cmd_num;		/* time when last cmd done */
};

struct mlx4_vhcr {
	u64 in_param;
	u64 out_param;
	u32 in_modifier;
	u32 timeout;
	u16 op;
	u16 token;
	u8 op_modifier;
	int err;
};

struct mlx4_mfunc {
	struct mlx4_comm __iomem		*comm;
	struct mlx4_vhcr				*vhcr;
	dma_addr_t						vhcr_dma;
	struct mlx4_mfunc_master_ctx	master;
	u32 							demux_sqp[MLX4_MFUNC_MAX];
	struct mlx4_mfunc_slave_ctx 	slave;
};

struct mlx4_mgm {
	__be32			next_gid_index;
	__be32			members_count;
	u32 		reserved[2];
	u8			gid[16];
	__be32			qp[MLX4_QP_PER_MGM];
};


struct mlx4_cmd {
	struct pci_pool 	   *pool;
	u8 __iomem		   *hcr;
	struct mutex		hcr_mutex;
	struct semaphore	poll_sem;
	struct semaphore	event_sem;
	struct semaphore	slave_sem;
	int 		max_cmds;
	spinlock_t		context_lock;
	int 		free_head;
	struct mlx4_cmd_context *context;
	u16 		token_mask;
	u8			use_events;
	u8			toggle;
	u8			comm_toggle;
};

struct mlx4_uar_table {
	struct mlx4_bitmap	bitmap;
};

struct mlx4_mr_table {
	struct mlx4_bitmap	mpt_bitmap;
	struct mlx4_buddy	mtt_buddy;
	u64 		mtt_base;
	u64 		mpt_base;
	struct mlx4_icm_table	mtt_table;
	struct mlx4_icm_table	dmpt_table;
};

struct mlx4_cq_table {
	struct mlx4_bitmap	bitmap;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_eq_table {
	struct mlx4_bitmap		bitmap;
	void __iomem			*clr_int;
	u8 __iomem				**uar_map;
	u32 					clr_mask;
	int 					num_eqs;		/* number of EQs in use */
	int 					num_eth_eqs;
	struct mlx4_eq			*eq;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
	int 					have_irq;
	u8						inta_pin;
	u8						max_extra_eqs;
#if 1//WORKAROUND_POLL_EQ
	KEVENT		thread_start_event;
	KEVENT		thread_stop_event;
	BOOLEAN 	bTerminated;
	PVOID		threadObject;
#endif
};

struct mlx4_srq_table {
	struct mlx4_bitmap	bitmap;
	spinlock_t		lock;
	struct radix_tree_root	tree;
	struct mlx4_icm_table	table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_qp_table {
	struct mlx4_bitmap	bitmap;
	u32 		rdmarc_base;
	int 		rdmarc_shift;
	spinlock_t		lock;
	struct mlx4_icm_table	qp_table;
	struct mlx4_icm_table	auxc_table;
	struct mlx4_icm_table	altc_table;
	struct mlx4_icm_table	rdmarc_table;
	struct mlx4_icm_table	cmpt_table;
};

struct mlx4_mcg_table {
	struct mutex		mutex;
	struct mlx4_bitmap	bitmap;
	struct mlx4_icm_table	table;
};

struct mlx4_catas_err {
	u32 __iomem 	   *map;
	/* Windows */
	int 				stop;
	KTIMER				timer;
	KDPC				timer_dpc;
	LARGE_INTEGER		interval;
	PIO_WORKITEM		catas_work;
};

struct mlx4_mac_table {
	
#define MLX4_MAX_MAC_NUM	128

#define MLX4_MAC_VALID_SHIFT	63
#define MLX4_MAC_TABLE_SIZE (MLX4_MAX_MAC_NUM << 3)
#define MLX4_MAC_VALID		(1ull << MLX4_MAC_VALID_SHIFT)
#define MLX4_MAC_MASK		(0xffffffffffffffff & ~MLX4_MAC_VALID)

	__be64 entries[MLX4_MAX_MAC_NUM];
	int refs[MLX4_MAX_MAC_NUM];
	struct semaphore mac_sem;
	int total;
	int max;
};

struct mlx4_vlan_table {
#define MLX4_MAX_VLAN_NUM	128
#define MLX4_VLAN_MASK		0xfff
#define MLX4_VLAN_VALID 	1 << 31
#define MLX4_VLAN_TABLE_SIZE	(MLX4_MAX_VLAN_NUM << 2)
	__be32 entries[MLX4_MAX_VLAN_NUM];
	int refs[MLX4_MAX_VLAN_NUM];
	struct semaphore vlan_sem;
	int total;
	int max;
};

#define SET_PORT_GEN_ALL_VALID		0x7
#define SET_PORT_PROMISC_SHIFT		31
#define SET_PORT_MC_PROMISC_SHIFT	30

struct mlx4_set_port_general_context {
	u8 reserved[3];
	u8 flags;
	u16 reserved2;
	__be16 mtu;
	u8 pptx;
	u8 pfctx;
	u16 reserved3;
	u8 pprx;
	u8 pfcrx;
	u16 reserved4;
};

struct mlx4_set_port_rqp_calc_context {
	__be32 base_qpn;
	u8 rererved;
	u8 n_mac;
	u8 n_vlan;
	u8 n_prio;
	u8 reserved2[3];
	u8 mac_miss;
	u8 intra_no_vlan;
	u8 no_vlan;
	u8 intra_vlan_miss;
	u8 vlan_miss;
	u8 reserved3[3];
	u8 no_vlan_prio;
	__be32 promisc;
	__be32 mcast;
};

struct mlx4_mac_entry { 
	u64 mac;
	u8 check_vlan;
	u8 vlan_valid;
	u16 vlan;
};

struct mlx4_port_info {
	struct mlx4_dev 	   *dev;
	int 		port;
	char		dev_name[16];
	enum mlx4_port_type tmp_type;
	struct mlx4_mac_table	mac_table;
	struct radix_tree_root	mac_tree;	 
	struct mlx4_vlan_table	vlan_table;    
	int 		base_qpn;
	BOOLEAN		is_qos_supported;
};

struct mlx4_sense {
	struct mlx4_dev 	*dev;
	u8			do_sense_port[MLX4_MAX_PORTS + 1];
	u8			sense_allowed[MLX4_MAX_PORTS + 1];
	u8			sense_results[MLX4_MAX_PORTS + 1];
    
	KTIMER				timer;
	KDPC				timer_dpc;
	LARGE_INTEGER		interval;
	PIO_WORKITEM		sense_work;
    u32         resched;
};

extern struct mutex drv_mutex;

struct mlx4_steer {
	struct list_head promisc_qps[MLX4_NUM_STEERS];
	struct list_head steer_entries[MLX4_NUM_STEERS];
};

struct msix_to_eq_mapping
{
	int num_msix_vectors;
	int max_num_eqs_per_vector;
	struct mlx4_eq ***eqs;
};

struct mlx4_priv {
	struct mlx4_dev 	dev;

	struct list_head	dev_list;
	struct list_head	ctx_list;
	spinlock_t		ctx_lock;

	struct list_head		pgdir_list;
	struct mutex			pgdir_mutex;

	struct mlx4_fw		fw;
	struct mlx4_cmd 	cmd;
	struct mlx4_mfunc	mfunc;

	struct mlx4_bitmap	pd_bitmap;
	struct mlx4_bitmap	xrcd_bitmap;
	struct mlx4_uar_table	uar_table;
	struct mlx4_mr_table	mr_table;
	struct mlx4_cq_table	cq_table;
	struct mlx4_eq_table	eq_table;
	struct mlx4_srq_table	srq_table;
	struct mlx4_qp_table	qp_table;
	struct mlx4_mcg_table	mcg_table;

	struct mlx4_catas_err	catas_err;

	u8 __iomem		   *clr_base;

	struct mlx4_port_info	port[MLX4_MAX_PORTS + 1];
	struct mlx4_sense		sense;
	struct mutex		port_mutex;
	struct mlx4_steer	*steer;
	bool			link_up[MLX4_MAX_PORTS + 1];
	bool			vep_mode[MLX4_MAX_PORTS + 1];
	atomic_t		opreq_count;
	PIO_WORKITEM	opreq_work; 
	PIO_WORKITEM	remove_work;
	struct list_head	bf_list;
	struct mutex		bf_mutex;
	struct io_mapping	   *bf_mapping;
	struct mlx4_qos_settings	qos_settings[MLX4_MAX_PORTS];	
	struct msix_to_eq_mapping 	msix_eq_mapping;
};


static inline struct mlx4_priv *mlx4_priv(struct mlx4_dev *dev)
{
	return container_of(dev, struct mlx4_priv, dev);
}

#define MLX4_SENSE_RANGE	(HZ * 3)

// extern struct workqueue_struct *mlx4_wq;

u32 mlx4_bitmap_alloc(struct mlx4_bitmap *bitmap);
void mlx4_bitmap_free(struct mlx4_bitmap *bitmap, u32 obj);
u32 mlx4_bitmap_alloc_range(struct mlx4_bitmap *bitmap, int cnt, int align);
void mlx4_bitmap_free_range(struct mlx4_bitmap *bitmap, u32 obj, int cnt);
int mlx4_bitmap_init(struct mlx4_bitmap *bitmap, u32 num, u32 mask, u32 reserved);
int mlx4_bitmap_init_with_effective_max(struct mlx4_bitmap *bitmap,
					u32 num, u32 mask, u32 reserved,
					u32 effective_max);
int mlx4_bitmap_init_no_mask(struct mlx4_bitmap *bitmap, u32 num,
	u32 reserved_bot, u32 reserved_top);
void mlx4_bitmap_cleanup(struct mlx4_bitmap *bitmap);
u32 mlx4_bitmap_avail(struct mlx4_bitmap *bitmap);

int mlx4_db_alloc(struct mlx4_dev *dev, 
				struct mlx4_db *db, int order);

int mlx4_get_ownership(struct mlx4_dev *dev);
void mlx4_free_ownership(struct mlx4_dev *dev);

int mlx4_alloc_eq_table(struct mlx4_dev *dev);
void mlx4_free_eq_table(struct mlx4_dev *dev);
int mlx4_GET_EVENT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);

int mlx4_init_pd_table(struct mlx4_dev *dev);
int mlx4_init_xrcd_table(struct mlx4_dev *dev);
int mlx4_init_uar_table(struct mlx4_dev *dev);
int mlx4_init_mr_table(struct mlx4_dev *dev);
int mlx4_init_eq_table(struct mlx4_dev *dev);
int mlx4_init_cq_table(struct mlx4_dev *dev);
int mlx4_init_qp_table(struct mlx4_dev *dev);
int mlx4_init_srq_table(struct mlx4_dev *dev);
int mlx4_init_mcg_table(struct mlx4_dev *dev);

void mlx4_cleanup_pd_table(struct mlx4_dev *dev);
void mlx4_cleanup_uar_table(struct mlx4_dev *dev);
void mlx4_cleanup_mr_table(struct mlx4_dev *dev);
void mlx4_cleanup_eq_table(struct mlx4_dev *dev);
void mlx4_cleanup_cq_table(struct mlx4_dev *dev);
void mlx4_cleanup_qp_table(struct mlx4_dev *dev);
void mlx4_cleanup_srq_table(struct mlx4_dev *dev);
void mlx4_cleanup_mcg_table(struct mlx4_dev *dev);
void mlx4_cleanup_xrcd_table(struct mlx4_dev *dev);

int mlx4_qp_alloc_icm(struct mlx4_dev *dev, int qpn);
void mlx4_qp_free_icm(struct mlx4_dev *dev, int qpn);
int mlx4_cq_alloc_icm(struct mlx4_dev *dev, int *cqn);
void mlx4_cq_free_icm(struct mlx4_dev *dev, int cqn);
int mlx4_srq_alloc_icm(struct mlx4_dev *dev, int *srqn);
void mlx4_srq_free_icm(struct mlx4_dev *dev, int srqn);
int mlx4_mr_reserve(struct mlx4_dev *dev);
void mlx4_mr_release(struct mlx4_dev *dev, u32 index);
int mlx4_mr_alloc_icm(struct mlx4_dev *dev, u32 index);
void mlx4_mr_free_icm(struct mlx4_dev *dev, u32 index);
u32 mlx4_alloc_mtt_range(struct mlx4_dev *dev, int order);
void mlx4_free_mtt_range(struct mlx4_dev *dev, u32 first_seg, int order);
int mlx4_WRITE_MTT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);

int mlx4_start_catas_poll(struct mlx4_dev *dev);
void mlx4_stop_catas_poll(struct mlx4_dev *dev);
int mlx4_restart_one(struct pci_dev *pdev);
int mlx4_register_device(struct mlx4_dev *dev);
void mlx4_unregister_device(struct mlx4_dev *dev);
void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_dev_event type,
			 int port);

void mlx4_intf_init();
int mlx4_net_init();

BOOLEAN mlx4_is_eth_port(struct mlx4_dev *dev, int port_number);
BOOLEAN mlx4_is_ib_port(struct mlx4_dev *dev, int port_number);
BOOLEAN mlx4_is_enabled_port(struct mlx4_dev *dev, int port_number);
int mlx4_count_ib_ports(struct mlx4_dev *dev);
char* mlx4_get_port_name(struct mlx4_dev *dev, u8 port_num);
u8 get_slave(struct mlx4_dev* dev, u8 port, u8 vep_num);

struct mlx4_dev_cap;
struct mlx4_init_hca_param;

u64 mlx4_make_profile(struct mlx4_dev *dev,
			  struct mlx4_profile *request,
			  struct mlx4_dev_cap *dev_cap,
			  struct mlx4_init_hca_param *init_hca);
#ifdef NTDDI_WIN8
KSTART_ROUTINE mlx4_master_comm_channel;
KSTART_ROUTINE mlx4_gen_slave_eqe;
#else
void mlx4_master_comm_channel(void* ctx);
void mlx4_gen_slave_eqe(void* ctx);
#endif

int mlx4_cmd_init(struct mlx4_dev *dev);
void mlx4_cmd_cleanup(struct mlx4_dev *dev);
int mlx4_multi_func_init(struct mlx4_dev *dev);
void mlx4_multi_func_cleanup(struct mlx4_dev *dev);
void mlx4_cmd_event(struct mlx4_dev *dev, u16 token, u8 status, u64 out_param);
int mlx4_cmd_use_events(struct mlx4_dev *dev);
void mlx4_cmd_use_polling(struct mlx4_dev *dev);

int mlx4_comm_cmd(struct mlx4_dev *dev, u8 cmd, u16 param, unsigned long timeout);

int mlx4_qp_get_region(struct mlx4_dev *dev,
			   enum qp_region region,
			   int *base_qpn, int *cnt);

void mlx4_cq_completion(struct mlx4_dev *dev, struct mlx4_eq* eq, u32 cqn);
void mlx4_cq_event(struct mlx4_dev *dev, struct mlx4_eq* eq, u32 cqn, int event_type);

void mlx4_init_mac_table(struct mlx4_dev *dev, struct mlx4_mac_table *table);
void mlx4_init_vlan_table(struct mlx4_dev *dev, struct mlx4_vlan_table *table);
int mlx4_update_ipv6_gids_win(struct mlx4_dev *dev, int port, int clear, u64 mac);


/* resource tracker functions*/
int mlx4_init_resource_tracker(struct mlx4_dev *dev);

void mlx4_free_resource_tracker(struct mlx4_dev *dev);

int mlx4_get_slave_from_resource_id(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					int resource_id, int *slave);

/* the parameter "state" indicates the current status (like in qp/mtt)
	need to reserve the renge before the allocation*/
int mlx4_add_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
				int slave_id, int resource_id, unsigned long state);
/*The MPT index need to have a mask*/
int mlx4_add_mpt_resource_for_slave(struct mlx4_dev *dev,
				enum mlx4_resource resource_type, int slave_id,
				int resource_id, unsigned long state);

/* use this fuction when there is call for resrvation of qp/mtt */
int mlx4_add_range_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					  int slave_id, int from, int cnt);

void mlx4_delete_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					int slave_id, int resource_id);

void mlx4_delete_range_resource_for_slave(struct mlx4_dev *dev, enum mlx4_resource resource_type,
					int slave_id, int from, int cnt);

void mlx4_delete_all_resources_for_slave(struct mlx4_dev *dev, int slave_id);

int mlx4_add_mcg_to_tracked_qp(struct mlx4_dev *dev, int qpn, u8* gid, enum mlx4_protocol prot) ;
int mlx4_remove_mcg_from_tracked_qp(struct mlx4_dev *dev, int qpn, u8* gid);

int mlx4_add_port_to_tracked_mac(struct mlx4_dev *dev, int qpn, u8 port) ;

int mlx4_add_vlan_fltr_to_tracked_slave(struct mlx4_dev *dev, int slave_id, int port);

void mlx4_delete_specific_res_type_for_slave(struct mlx4_dev *dev, int slave_id,
						 enum mlx4_resource resource_type);
int mlx4_add_mtt_resource_for_slave(struct mlx4_dev *dev,
						int slave_id, int resource_id,
						unsigned long state, int order);

/*Resource tracker - verification functions.*/

int mlx4_verify_resource_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_mpt_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_cq_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_srq_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_qp_index(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox);

int mlx4_verify_srq_aram(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
						  struct mlx4_cmd_mailbox *inbox) ;

/*Ruturns the mask according to specific bitmap allocator*/
u32 calculate_bitmap_mask(struct mlx4_dev *dev, enum mlx4_resource resource_type);

void mlx4_qp_event(struct mlx4_dev *dev, u32 qpn, int event_type);

void mlx4_srq_event(struct mlx4_dev *dev, u32 srqn, int event_type);

void mlx4_handle_catas_err(struct mlx4_dev *dev);

int mlx4_sense_port(struct mlx4_dev *dev);


void mlx4_do_sense_ports(struct mlx4_dev *dev,
			 enum mlx4_port_type *stype,
			 enum mlx4_port_type *defaults);
void mlx4_start_sense(struct mlx4_dev *dev);
//void mlx4_stop_sense(struct mlx4_dev *dev);
//void mlx4_sense_init(struct mlx4_dev *dev);
int mlx4_check_port_params(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_type);
int mlx4_change_port_types(struct mlx4_dev *dev,
			   enum mlx4_port_type *port_types);
void mlx4_set_port_mask(struct mlx4_dev *dev, struct mlx4_caps *caps, int function);

int mlx4_init_one(struct pci_dev *pdev, struct mlx4_dev_params *dev_params);

void mlx4_remove_one(struct pci_dev *pdev);

#define ETH_FCS_LEN 4		/* Frame Check Sequence Length	 */
#define ETH_HLEN 14

int mlx4_add_eq(struct mlx4_dev *dev, int nent,
	KAFFINITY cpu, u8* p_eq_num, struct mlx4_eq ** p_eq);

void mlx4_remove_eq(struct mlx4_dev *dev, u8 eq_num);

int mlx4_test_interrupts(struct mlx4_dev *dev);

int mlx4_reset_ready( struct ib_event_handler *event_handler );
int mlx4_reset_execute( struct ib_event_handler *event_handler );

int mlx4_reset_request( struct ib_event_handler *event_handler );

int mlx4_reset_cb_register( struct ib_event_handler *event_handler );

int mlx4_reset_cb_unregister( struct ib_event_handler *event_handler );

void fix_bus_ifc(struct pci_dev *pdev);

int mlx4_dispatch_reset_event(struct ib_device *ibdev, enum ib_event_type type);

int mlx4_SET_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_INIT_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_CLOSE_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_QUERY_PORT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_get_port_ib_caps(struct mlx4_dev *dev, u8 port, __be32 *caps);
int mlx4_get_ext_port_caps(struct mlx4_dev *dev, u8 port, u64 *get_ext_caps);

int mlx4_QP_MODIFY_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
			   struct mlx4_cmd_mailbox *inbox, struct mlx4_cmd_mailbox *outbox);
int mlx4_CONF_SPECIAL_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_GET_SLAVE_SQP(struct mlx4_dev *dev, u32 *sqp, int num);
int mlx4_GET_SLAVE_SQP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_MCAST_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_PROMISC_wrapper(struct mlx4_dev *dev, int slave,
	struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_qp_detach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
	enum mlx4_protocol prot, enum mlx4_steer_type steer);
int mlx4_qp_attach_common(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
	int block_mcast_loopback, enum mlx4_protocol prot,
	enum mlx4_steer_type steer, int attach_at_head, int block_lb);

int mlx4_SET_MCAST_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_SET_VLAN_FLTR_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_MAP_EQ_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_COMM_INT_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_RTR2RTS_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_INIT2RTR_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_RTS2RTS_QP_wrapper(struct mlx4_dev *dev, int slave, struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);
int mlx4_QUERY_IF_STAT_wrapper(struct mlx4_dev *dev, int slave,
	struct mlx4_vhcr *vhcr,
	struct mlx4_cmd_mailbox *inbox,
	struct mlx4_cmd_mailbox *outbox);

int mlx4_SET_IF_STAT(struct mlx4_dev *dev, u8 stat_mode);
int mlx4_QUERY_IF_STAT(struct mlx4_dev *dev,u8 reset,u8 batch, u16 counter_index,
			   struct net_device_stats *stats);

/* Legacy statistics */
int mlx4_DUMP_ETH_STATS(struct mlx4_dev *dev, u8 port, u8 reset,
			   struct net_device_stats *stats);

int mlx4_SET_PORT_PRIO2TC(struct mlx4_dev *dev, u8 port, prio2tc_context* inbox);
int mlx4_SET_PORT_SCHEDULER(struct mlx4_dev *dev, u8 port, set_port_scheduler_context* inbox);


int mlx4_GEN_EQE(struct mlx4_dev* dev, int slave, struct mlx4_eqe* eqe);

int mlx4_common_set_vlan_fltr(struct mlx4_dev *dev, int function,
					 int port, void *buf);

NTSTATUS init_working_thread(struct working_thread* p_thread, 
			PKSTART_ROUTINE  thread_outine, void* ctx);
void stop_working_thread(struct working_thread* p_thread);

inline void trig_working_thread(struct working_thread* p_thread)
{	 
	KeSetEvent(&p_thread->trig_event, IO_NO_INCREMENT, FALSE );
}

void mlx4_dev_remove(
	IN				struct mlx4_dev 			*dev );

void mlx4_print_mcg_table(struct mlx4_dev *dev);

int 
mlx4_set_protocol_qos(
	struct mlx4_dev *dev,
	struct mlx4_qos_settings* settings,
	u16 count,
	u8 port
	);

int mlx4_affinity_to_cpu_number(KAFFINITY *affinity, u8 *cpu_num);

#endif /* MLX4_H */
