/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#ifndef MLX4_DEVICE_H
#define MLX4_DEVICE_H

#define MAX_DIRECT_ALLOC_SIZE	(PAGE_SIZE*1024*32)
enum mlx4_port_type {
	MLX4_PORT_TYPE_NONE = 0,
	MLX4_PORT_TYPE_IB	= 1,
	MLX4_PORT_TYPE_ETH	= 2,
	MLX4_PORT_TYPE_AUTO = 3
};

enum {
	MLX4_FLAG_MSI_X 		= 1 << 0,
	MLX4_FLAG_OLD_PORT_CMDS = 1 << 1,
	MLX4_FLAG_MASTER		= 1 << 2,
	MLX4_FLAG_SLAVE 		= 1 << 3,
	MLX4_FLAG_SRIOV 		= 1 << 4,
	MLX4_FLAG_VF			= 1 << 5,
	MLX4_FLAG_RESET_4_RMV	= 1 << 10,
	MLX4_FLAG_RESET_CLIENT	= 1 << 11,
	MLX4_FLAG_RESET_DRIVER	= 1 << 12,
	MLX4_FLAG_RESET_STARTED = 1 << 13,
	MLX4_FLAG_BUSY_WAIT 	= 1 << 14,
	MLX4_FLAG_UNLOAD		= 1 << 15
};

enum {
	MLX4_MAX_PORTS		= 2
};

enum {
	MLX4_BOARD_ID_LEN = 64
};

enum {
	MLX4_DEV_CAP_FLAG_RC		= 1 <<	0,
	MLX4_DEV_CAP_FLAG_UC		= 1 <<	1,
	MLX4_DEV_CAP_FLAG_UD		= 1 <<	2,
	MLX4_DEV_CAP_FLAG_XRC		= 1 <<	3,
	MLX4_DEV_CAP_FLAG_SRQ		= 1 <<	6,
	MLX4_DEV_CAP_FLAG_IPOIB_CSUM	= 1 <<	7,
	MLX4_DEV_CAP_FLAG_BAD_PKEY_CNTR = 1 <<	8,
	MLX4_DEV_CAP_FLAG_BAD_QKEY_CNTR = 1 <<	9,
	MLX4_DEV_CAP_FLAG_DPDP		= 1 << 12,
	MLX4_DEV_CAP_FLAG_RAW_ETY	= 1 << 13,
	MLX4_DEV_CAP_FLAG_BLH		= 1 << 15,
	MLX4_DEV_CAP_FLAG_MEM_WINDOW	= 1 << 16,
	MLX4_DEV_CAP_FLAG_APM		= 1 << 17,
	MLX4_DEV_CAP_FLAG_ATOMIC	= 1 << 18,
	MLX4_DEV_CAP_FLAG_RAW_MCAST = 1 << 19,
	MLX4_DEV_CAP_FLAG_UD_AV_PORT	= 1 << 20,
	MLX4_DEV_CAP_FLAG_UD_MCAST	= 1 << 21,	
	MLX4_DEV_CAP_FLAG_IBOE		= 1 << 30,
	MLX4_DEV_CAP_FLAG_FC_T11	= 1 << 31
};
#define MLX4_DEV_CAP_SENSE_SUPPORT		(1ull << 55)

enum {
	MLX_EXT_PORT_CAP_FLAG_EXTENDED_PORT_INFO	= 1 <<  0
};

enum {
	MLX4_BMME_FLAG_LOCAL_INV	= 1 <<	6,
	MLX4_BMME_FLAG_REMOTE_INV	= 1 <<	7,
	MLX4_BMME_FLAG_TYPE_2_WIN	= 1 <<	9,
	MLX4_BMME_FLAG_RESERVED_LKEY	= 1 << 10,
	MLX4_BMME_FLAG_FAST_REG_WR	= 1 << 11,
};

enum mlx4_event {
	MLX4_EVENT_TYPE_COMP		   = 0x00,
	MLX4_EVENT_TYPE_PATH_MIG	   = 0x01,
	MLX4_EVENT_TYPE_COMM_EST	   = 0x02,
	MLX4_EVENT_TYPE_SQ_DRAINED	   = 0x03,
	MLX4_EVENT_TYPE_CQ_ERROR	   = 0x04,
	MLX4_EVENT_TYPE_WQ_CATAS_ERROR	   = 0x05,
	MLX4_EVENT_TYPE_EEC_CATAS_ERROR    = 0x06,
	MLX4_EVENT_TYPE_PATH_MIG_FAILED    = 0x07,
	MLX4_EVENT_TYPE_LOCAL_CATAS_ERROR  = 0x08,
	MLX4_EVENT_TYPE_PORT_CHANGE 	   = 0x09,
	MLX4_EVENT_TYPE_CMD 			   = 0x0a,
	MLX4_EVENT_TYPE_CQ_OVERFLOW	   = 0x0c,
	MLX4_EVENT_TYPE_ECC_DETECT		   = 0x0e,
	MLX4_EVENT_TYPE_EQ_OVERFLOW 	   = 0x0f,
	MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	MLX4_EVENT_TYPE_WQ_ACCESS_ERROR    = 0x11,
	MLX4_EVENT_TYPE_SRQ_CATAS_ERROR    = 0x12,
	MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE    = 0x13,
	MLX4_EVENT_TYPE_SRQ_LIMIT		   = 0x14,
	MLX4_EVENT_TYPE_COMM_CHANNEL	   = 0x18,
	MLX4_EVENT_TYPE_VEP_UPDATE		   = 0x19,
	MLX4_EVENT_TYPE_OP_REQUIRED	   = 0x1a,
	MLX4_EVENT_TYPE_MAC_UPDATE		   = 0x20,
	MLX4_EVENT_TYPE_PPF_REMOVE		   = 0xf0,
	MLX4_EVENT_TYPE_SQP_UPDATE	   = 0xfe,
	MLX4_EVENT_TYPE_NONE		   = 0xff
};

enum {
	MLX4_PORT_CHANGE_SUBTYPE_DOWN	= 1,
	MLX4_PORT_CHANGE_SUBTYPE_ACTIVE = 4
};

enum {
	MLX4_PERM_LOCAL_READ	= 1 << 10,
	MLX4_PERM_LOCAL_WRITE	= 1 << 11,
	MLX4_PERM_REMOTE_READ	= 1 << 12,
	MLX4_PERM_REMOTE_WRITE	= 1 << 13,
	MLX4_PERM_ATOMIC	= 1 << 14
};

enum {
	MLX4_OPCODE_NOP 		= 0x00,
	MLX4_OPCODE_SEND_INVAL		= 0x01,
	MLX4_OPCODE_RDMA_WRITE		= 0x08,
	MLX4_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX4_OPCODE_SEND		= 0x0a,
	MLX4_OPCODE_SEND_IMM		= 0x0b,
	MLX4_OPCODE_LSO 		= 0x0e,
	MLX4_OPCODE_RDMA_READ		= 0x10,
	MLX4_OPCODE_ATOMIC_CS		= 0x11,
	MLX4_OPCODE_ATOMIC_FA		= 0x12,
	MLX4_OPCODE_ATOMIC_MASK_CS	= 0x14,
	MLX4_OPCODE_ATOMIC_MASK_FA	= 0x15,
	MLX4_OPCODE_BIND_MW 	= 0x18,
	MLX4_OPCODE_FMR 		= 0x19,
	MLX4_OPCODE_LOCAL_INVAL 	= 0x1b,
	MLX4_OPCODE_CONFIG_CMD		= 0x1f,

	MLX4_RECV_OPCODE_RDMA_WRITE_IMM = 0x00,
	MLX4_RECV_OPCODE_SEND		= 0x01,
	MLX4_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX4_RECV_OPCODE_SEND_INVAL = 0x03,

	MLX4_CQE_OPCODE_ERROR		= 0x1e,
	MLX4_CQE_OPCODE_RESIZE		= 0x16,
};

enum {
	MLX4_STAT_RATE_OFFSET	= 5
};

enum qp_region {
	MLX4_QP_REGION_FW = 0,
	MLX4_QP_REGION_ETH_ADDR,
	MLX4_QP_REGION_FC_ADDR,
	MLX4_QP_REGION_FC_EXCH,
	MLX4_NUM_QP_REGION		/* Must be last */
};

enum mlx4_protocol {
	MLX4_PROT_IB_IPV6 = 0, 
	MLX4_PROT_ETH,
	MLX4_PROT_IB_IPV4,
	MLX4_PROT_FCOE
};

enum mlx4_steer_type {
	MLX4_MC_STEER = 0,
	MLX4_UC_STEER,
	MLX4_NUM_STEERS
};

enum mlx4_mgid_steer_flags {
	MLX4_MGID_VLAN_PRESENT = 0x1,
	MLX4_MGID_UC = 0x2,
	MLX4_MGID_CHECK_VLAN = 0x4,
	MLX4_NUM_MGID_STEER_FLAGS
};

enum {
	MLX4_MTT_FLAG_PRESENT		= 1
};


#define MLX4_LEAST_ATTACHED_VECTOR	0xffffffff

//
//	The following bits are Wakeup types
//
#define WAKE_MAGIC			0x00000001
#define WAKE_PATTERN_MATCH	0x00000002
#define WAKE_LINK_CHANGE	0x00000004


#if 0
// not in use now
// should be set as default to numFcExch
enum {
	MLX4_NUM_FEXCH		= 64 * 1024,
};
#endif

enum {
	MLX4_CUNTERS_DISABLED,
	MLX4_CUNTERS_BASIC,
	MLX4_CUNTERS_EXT
};

struct mlx4_dev_cap {
	int max_srq_sz;
	int max_qp_sz;
	int reserved_qps;
	int max_qps;
	int reserved_srqs;
	int max_srqs;
	int max_cq_sz;
	int reserved_cqs;
	int max_cqs;
	int max_mpts;
	int reserved_eqs;
	int max_eqs;
	int reserved_mtts;
	int max_mrw_sz;
	int reserved_mrws;
	int max_mtt_seg;
	int max_requester_per_qp;
	int max_responder_per_qp;
	int max_rdma_global;
	int local_ca_ack_delay;
	int pf_num;
	int num_ports;
	u32 max_msg_sz;
	int ib_mtu[MLX4_MAX_PORTS + 1];    
	int max_port_width[MLX4_MAX_PORTS + 1];
	int max_vl[MLX4_MAX_PORTS + 1];
	int max_gids[MLX4_MAX_PORTS + 1];
	int max_pkeys[MLX4_MAX_PORTS + 1];
	u64 def_mac[MLX4_MAX_PORTS + 1];
	u16 eth_mtu[MLX4_MAX_PORTS + 1];
	u16 stat_rate_support;
    int if_cnt_basic_support;
    int if_cnt_extended_support;
	int loopback_support;
	int vep_uc_steering;
	int vep_mc_steering;
	int steering_by_vlan;
	int wol;
    int hds;
    int header_lookahead;
	u64 flags;
	int reserved_uars;
	int uar_size;
	int min_page_sz;
	int bf_reg_size;
	int bf_regs_per_page;
	int max_sq_sg;
	int max_sq_desc_sz;
	int max_rq_sg;
	int max_rq_desc_sz;
	int max_qp_per_mcg;
	int reserved_mgms;
	int max_mcgs;
	int reserved_pds;
	int max_pds;
	int reserved_xrcds;
	int max_xrcds;
    int max_if_cnt_basic;
    int max_if_cnt_extended;
	int qpc_entry_sz;
	int rdmarc_entry_sz;
	int altc_entry_sz;
	int aux_entry_sz;
	int srq_entry_sz;
	int cqc_entry_sz;
	int eqc_entry_sz;
	int dmpt_entry_sz;
	int cmpt_entry_sz;
	int mtt_entry_sz;
	int resize_srq;
	u32 bmme_flags;
	u32 reserved_lkey;
	u64 max_icm_sz;
	int max_gso_sz;
	u8	port_types_cap[MLX4_MAX_PORTS + 1];
    u8	port_types_default[MLX4_MAX_PORTS + 1];
    u8	port_types_do_sense[MLX4_MAX_PORTS + 1];
	u8	log_max_macs[MLX4_MAX_PORTS + 1];
	u8	log_max_vlans[MLX4_MAX_PORTS + 1];
	u16 clp_ver;
};


struct mlx4_caps {
	u64 		fw_ver;
	int 		function;
	int 		pf_num;
    u32			vep_num;
	int 		num_ports;
	int 		vl_cap[MLX4_MAX_PORTS + 1];
	int 		ib_mtu_cap[MLX4_MAX_PORTS + 1];
	__be32		ib_port_def_cap[MLX4_MAX_PORTS + 1];
	u64 		def_mac[MLX4_MAX_PORTS + 1];
	int 		eth_mtu_cap[MLX4_MAX_PORTS + 1];
	int 		gid_table_len[MLX4_MAX_PORTS + 1];
	int 		pkey_table_len[MLX4_MAX_PORTS + 1];
	int 		local_ca_ack_delay;
	int 		num_uars;
	int 		uar_page_size;
	int 		bf_reg_size;
	int 		bf_regs_per_page;
	int 		max_sq_sg;
	int 		max_rq_sg;
	int 		num_qps;
	int 		max_wqes;
	int 		max_sq_desc_sz;
	int 		max_rq_desc_sz;
	int 		max_qp_init_rdma;
	int 		max_qp_dest_rdma;
	int 		sqp_start;
	int 		tunnel_qpn;
	int 		num_srqs;
	int 		max_srq_wqes;
	int 		max_srq_sge;
	int 		reserved_srqs;

	int 		num_cqs;
	int 		max_cqes;
	int 		reserved_cqs;
	int 		num_eqs;
	int 		reserved_eqs;
	int 		max_eqs;
	int 		num_mpts;
	int 		num_mtt_segs;
	int 		mtts_per_seg;
	int 		fmr_reserved_mtts;
	int 		reserved_mtts;
	int 		reserved_mrws;
	int 		reserved_uars;
	int 		num_mgms;
	int 		num_amgms;
	int 		reserved_mcgs;
	int 		num_qp_per_mgm;
	int 		num_pds;
	int 		reserved_pds;
	int 		mtt_entry_sz;
	int 		reserved_xrcds;
	int 		max_xrcds;
    int         max_if_cnt_basic;
    int         max_if_cnt_extended;        
	u32 		max_msg_sz;
	u32 		page_size_cap;
	u64 		flags;
	u32 		bmme_flags;
	u32 		reserved_lkey;
	u16 		stat_rate_support;
    int         if_cnt_basic_support;
    int         if_cnt_extended_support;    
    u8          counters_mode;
	int 		loopback_support;
	int 		vep_uc_steering;
	int 		vep_mc_steering;    
    int         hds;
    int         header_lookahead;
	int 		steering_by_vlan;
	int 		wol;
	u8			port_width_cap[MLX4_MAX_PORTS + 1];
	int 		max_gso_sz;
	int 		reserved_qps_cnt[MLX4_NUM_QP_REGION];
	int 		reserved_qps;
	int 		reserved_qps_base[MLX4_NUM_QP_REGION];
	int 		log_num_macs;
	int 		log_num_vlans;
	int 		log_num_prios;
	enum mlx4_port_type port_type_final[MLX4_MAX_PORTS + 1];
	u8			port_types_cap[MLX4_MAX_PORTS + 1];
    u8			port_types_default[MLX4_MAX_PORTS + 1];
    u8			port_types_do_sense[MLX4_MAX_PORTS + 1];
	u8			sqp_demux;
	u8			port_mask[MLX4_MAX_PORTS + 1];
	int			eqe_size;
	int			cqe_size;
	u64 		ext_port_cap[MLX4_MAX_PORTS + 1];

	// Windows
	int 		num_fc_exch;
	enum rdma_transport_type	transport_type[MLX4_MAX_PORTS + 1];
	enum mlx4_port_state port_state[MLX4_MAX_PORTS + 1];
	int 		reserved_fexch_mpts_base;	
	u8			enable_fcox[MLX4_MAX_PORTS + 1];    
    u64         clr_int_base;    
    u8          clr_int_bar;
	u16			clp_ver;
};

struct mlx4_buf_list {
	u8				*buf;
	dma_addr_t		map;
};
enum {
	MLX4_DB_PER_PAGE = PAGE_SIZE / 4
};

struct mlx4_db_pgdir {
	struct list_head		list;
	DECLARE_BITMAP(order0, MLX4_DB_PER_PAGE);
	DECLARE_BITMAP(order1, MLX4_DB_PER_PAGE / 2);
	unsigned long		   *bits[2];
	__be32				   *db_page;
	dma_addr_t			   db_dma;
};

struct mlx4_db {
	__be32					*db;
	struct mlx4_db_pgdir	*pgdir;
	dma_addr_t				dma;
	int 					index;
	int 					order;
};

struct mlx4_mtt {
	u32 		first_seg;
	int 		order;
	int 		page_shift;
};

struct mlx4_buf {
	struct mlx4_buf_list	direct;
	struct mlx4_buf_list   *page_list;
	int 		nbufs;
	int 		npages;
	int 		page_shift;
	int			is_direct;
};

struct mlx4_hwq_resources {
	struct mlx4_db		db;
	struct mlx4_mtt 	mtt;
	struct mlx4_buf 	buf;
};

struct mlx4_mr {
	struct mlx4_mtt 	mtt;
	u64 		iova;
	u64 		size;
	u32 		key;
	u32 		pd;
	u32 		access;
	int 		enabled;
};

struct mlx4_fmr {
	struct mlx4_mr		mr;
	struct mlx4_mpt_entry  *mpt;
	__be64			   *mtts;
	dma_addr_t		dma_handle;
	int 		max_pages;
	int 		max_maps;
	int 		maps;
	u8			page_shift;
};

struct mlx4_uar {
	unsigned long		pfn;
	int 		index;
	struct list_head	bf_list;
	unsigned			free_bf_bmap;
	u8 __iomem	  		*map;
	u8 __iomem			*bf_map;
#ifdef MAP_WC_EVERY_TIME
	unsigned			bf_map_size;
#endif		
};

struct mlx4_bf {
	unsigned long		offset;
	int					buf_size;
	struct mlx4_uar	    *uar;
	u8 __iomem	    *reg;
 };

struct mlx4_cq {
	void (*comp)		(struct mlx4_cq *);
	void (*event)		(struct mlx4_cq *, enum mlx4_event);

	struct mlx4_uar 	   *uar;

	u32 		cons_index;

	__be32			   *set_ci_db;
	__be32			   *arm_db;
	u32 		arm_sn;

	int 		cqn;
	unsigned	vector;
	int 		comp_eq_idx;	

	atomic_t		refcount;
	struct completion	free;

	// Windows specific
	volatile u32 	*p_u_arm_sn;
	PMDL		mdl;
};

struct mlx4_qp {
	void (*event)		(struct mlx4_qp *, enum mlx4_event);

	int 		qpn;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_srq {
	void (*event)		(struct mlx4_srq *, enum mlx4_event);

	int 		srqn;
	int 		max;
	int 		max_gs;
	int 		wqe_shift;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_av {
	__be32			port_pd;
	u8			reserved1;
	u8			g_slid;
	__be16			dlid;
	u8			reserved2;
	u8			gid_index;
	u8			stat_rate;
	u8			hop_limit;
	__be32			sl_tclass_flowlabel;
	u8			dgid[16];
};

struct mlx4_eth_av {
	__be32		port_pd;
	u8		reserved1;
	u8		smac_idx;
	u16 	reserved2;
	u8		reserved3;
	u8		gid_index;
	u8		stat_rate;
	u8		hop_limit;
	__be32		sl_tclass_flowlabel;
	u8		dgid[16];
	u32 	reserved4[2];
	__be16		vlan;
	u8		mac_0_1[2];
	u8		mac_2_5[4];
};

union mlx4_ext_av {
	struct mlx4_av		ib;
	struct mlx4_eth_av	eth;
};

#define MLX4_DEV_SIGNATURE	0xf1b34a6e

struct mlx4_dev_params {
	enum mlx4_port_type mod_port_type[MLX4_MAX_PORTS];
	u8 enable_fcox[MLX4_MAX_PORTS];
	u8 enable_roce[MLX4_MAX_PORTS+1];
	enum ib_mtu roce_mtu[MLX4_MAX_PORTS+1];	
} ;

static inline void mlx4_copy_dev_params(
	struct mlx4_dev_params *dst,
	struct mlx4_dev_params *src)
{
	*dst = *src;
}

typedef struct
{
	union
	{
		struct
		{ 
            int   fDpDp:1;
            int   fPort1IB:1;
            int   fPort2IB:1;
            int   fPort1Eth:1;
            int   fPort2Eth:1;
            int   fPort1DoSenseAllowed:1;
            int   fPort2DoSenseAllowed:1;
            int   fPort1AutoSenseCap:1;
            int   fPort2AutoSenseCap:1;
            int   fPort1Default:1;
            int   fPort2Default:1; 
		} ver;
		DWORD Value;
	}u;
} CAPABILITY_FLAG;

/* This structure is a part of MLX4_BUS_IB_INTERFACE */
/* Upon its changing you have to change MLX4_BUS_IB_INTERFACE_VERSION */
struct mlx4_dev {
	u32 		signature;
	struct pci_dev			*pdev;
	unsigned long			flags;
	unsigned long			num_slaves;
	LONG					reset_pending;
	struct mlx4_caps		caps;
	struct radix_tree_root	qp_table_tree;
	u32 					rev_id;
	char					board_id[MLX4_BOARD_ID_LEN];
	struct mlx4_dev_params	dev_params;
	CAPABILITY_FLAG capability_flag;
	int 					n_ib_reserved_eqs;
	struct mlx4_dev_cap		dev_cap;
};

struct mlx4_init_port_param {
	int 		set_guid0;
	int 		set_node_guid;
	int 		set_si_guid;
	u16 		mtu;
	int 		port_width_cap;
	u16 		vl_cap;
	u16 		max_gid;
	u16 		max_pkey;
	u64 		guid0;
	u64 		node_guid;
	u64 		si_guid;
};

enum {
	MLX4_EN_1G_SPEED	= 0x02,
	MLX4_EN_10G_SPEED_XFI	= 0x01,
	MLX4_EN_10G_SPEED_XAUI	= 0x00,
	MLX4_EN_40G_SPEED	= 0x40,
	MLX4_EN_OTHER_SPEED	= 0x0f,
};

#define MLX4_EN_SPEED_MASK	0x43

struct mlx4_en_query_port_context {
		u8 link_up;
#define MLX4_EN_LINK_UP_MASK	0x80
		u8 reserved;
		__be16 mtu;
		u8 reserved2;
		u8 link_speed;
#define MLX4_EN_SPEED_MASK		0x43
		u16 reserved3[5];
		__be64 mac;
		u8 transceiver;
		u8 actual_speed;
};


#define LINE_SPEED_1GB  10
#define LINE_SPEED_10GB 100
#define LINE_SPEED_40GB 400

struct mlx4_vep_cfg {
	u64 mac;
	u8	link;
    //
    //  bw in 100Mbps units
    //
    int bw_value;
};

struct prio2tc_context 
{
    u8 prio_0_1_tc;
    u8 prio_2_3_tc;
    u8 prio_4_5_tc;
    u8 prio_6_7_tc;
};

struct port_scheduler_tc_cfg
{
    u8 reserved1;
    u8 pg;
    u8 reserved2;
    u8 bw_percentage;

    u8 reserved3;
    u8 max_bw_units;
    u8 reserved4;
    u8 max_bw_value;
};

struct set_port_scheduler_context
{
    struct port_scheduler_tc_cfg tc[8];
};


struct mlx4_stat_out_mbox {
	/* Received frames with a length of 64 octets */
	__be64 R64_prio_0;
	__be64 R64_prio_1;
	__be64 R64_prio_2;
	__be64 R64_prio_3;
	__be64 R64_prio_4;
	__be64 R64_prio_5;
	__be64 R64_prio_6;
	__be64 R64_prio_7;
	__be64 R64_novlan;
	/* Received frames with a length of 127 octets */
	__be64 R127_prio_0;
	__be64 R127_prio_1;
	__be64 R127_prio_2;
	__be64 R127_prio_3;
	__be64 R127_prio_4;
	__be64 R127_prio_5;
	__be64 R127_prio_6;
	__be64 R127_prio_7;
	__be64 R127_novlan;
	/* Received frames with a length of 255 octets */
	__be64 R255_prio_0;
	__be64 R255_prio_1;
	__be64 R255_prio_2;
	__be64 R255_prio_3;
	__be64 R255_prio_4;
	__be64 R255_prio_5;
	__be64 R255_prio_6;
	__be64 R255_prio_7;
	__be64 R255_novlan;
	/* Received frames with a length of 511 octets */
	__be64 R511_prio_0;
	__be64 R511_prio_1;
	__be64 R511_prio_2;
	__be64 R511_prio_3;
	__be64 R511_prio_4;
	__be64 R511_prio_5;
	__be64 R511_prio_6;
	__be64 R511_prio_7;
	__be64 R511_novlan;
	/* Received frames with a length of 1023 octets */
	__be64 R1023_prio_0;
	__be64 R1023_prio_1;
	__be64 R1023_prio_2;
	__be64 R1023_prio_3;
	__be64 R1023_prio_4;
	__be64 R1023_prio_5;
	__be64 R1023_prio_6;
	__be64 R1023_prio_7;
	__be64 R1023_novlan;
	/* Received frames with a length of 1518 octets */
	__be64 R1518_prio_0;
	__be64 R1518_prio_1;
	__be64 R1518_prio_2;
	__be64 R1518_prio_3;
	__be64 R1518_prio_4;
	__be64 R1518_prio_5;
	__be64 R1518_prio_6;
	__be64 R1518_prio_7;
	__be64 R1518_novlan;
	/* Received frames with a length of 1522 octets */
	__be64 R1522_prio_0;
	__be64 R1522_prio_1;
	__be64 R1522_prio_2;
	__be64 R1522_prio_3;
	__be64 R1522_prio_4;
	__be64 R1522_prio_5;
	__be64 R1522_prio_6;
	__be64 R1522_prio_7;
	__be64 R1522_novlan;
	/* Received frames with a length of 1548 octets */
	__be64 R1548_prio_0;
	__be64 R1548_prio_1;
	__be64 R1548_prio_2;
	__be64 R1548_prio_3;
	__be64 R1548_prio_4;
	__be64 R1548_prio_5;
	__be64 R1548_prio_6;
	__be64 R1548_prio_7;
	__be64 R1548_novlan;
	/* Received frames with a length of 1548 < octets < MTU */
	__be64 R2MTU_prio_0;
	__be64 R2MTU_prio_1;
	__be64 R2MTU_prio_2;
	__be64 R2MTU_prio_3;
	__be64 R2MTU_prio_4;
	__be64 R2MTU_prio_5;
	__be64 R2MTU_prio_6;
	__be64 R2MTU_prio_7;
	__be64 R2MTU_novlan;
	/* Received frames with a length of MTU< octets and good CRC */
	__be64 RGIANT_prio_0;
	__be64 RGIANT_prio_1;
	__be64 RGIANT_prio_2;
	__be64 RGIANT_prio_3;
	__be64 RGIANT_prio_4;
	__be64 RGIANT_prio_5;
	__be64 RGIANT_prio_6;
	__be64 RGIANT_prio_7;
	__be64 RGIANT_novlan;
	/* Received broadcast frames with good CRC */
	__be64 RBCAST_prio_0;
	__be64 RBCAST_prio_1;
	__be64 RBCAST_prio_2;
	__be64 RBCAST_prio_3;
	__be64 RBCAST_prio_4;
	__be64 RBCAST_prio_5;
	__be64 RBCAST_prio_6;
	__be64 RBCAST_prio_7;
	__be64 RBCAST_novlan;
	/* Received multicast frames with good CRC */
	__be64 MCAST_prio_0;
	__be64 MCAST_prio_1;
	__be64 MCAST_prio_2;
	__be64 MCAST_prio_3;
	__be64 MCAST_prio_4;
	__be64 MCAST_prio_5;
	__be64 MCAST_prio_6;
	__be64 MCAST_prio_7;
	__be64 MCAST_novlan;
	/* Received unicast not short or GIANT frames with good CRC */
	__be64 RTOTG_prio_0;
	__be64 RTOTG_prio_1;
	__be64 RTOTG_prio_2;
	__be64 RTOTG_prio_3;
	__be64 RTOTG_prio_4;
	__be64 RTOTG_prio_5;
	__be64 RTOTG_prio_6;
	__be64 RTOTG_prio_7;
	__be64 RTOTG_novlan;

	/* Count of total octets of received frames, includes framing characters */
	__be64 RTTLOCT_prio_0;
	/* Count of total octets of received frames, not including framing
	   characters */
	__be64 RTTLOCT_NOFRM_prio_0;
	/* Count of Total number of octets received
	   (only for frames without errors) */
	__be64 ROCT_prio_0;

	__be64 RTTLOCT_prio_1;
	__be64 RTTLOCT_NOFRM_prio_1;
	__be64 ROCT_prio_1;

	__be64 RTTLOCT_prio_2;
	__be64 RTTLOCT_NOFRM_prio_2;
	__be64 ROCT_prio_2;

	__be64 RTTLOCT_prio_3;
	__be64 RTTLOCT_NOFRM_prio_3;
	__be64 ROCT_prio_3;

	__be64 RTTLOCT_prio_4;
	__be64 RTTLOCT_NOFRM_prio_4;
	__be64 ROCT_prio_4;

	__be64 RTTLOCT_prio_5;
	__be64 RTTLOCT_NOFRM_prio_5;
	__be64 ROCT_prio_5;

	__be64 RTTLOCT_prio_6;
	__be64 RTTLOCT_NOFRM_prio_6;
	__be64 ROCT_prio_6;

	__be64 RTTLOCT_prio_7;
	__be64 RTTLOCT_NOFRM_prio_7;
	__be64 ROCT_prio_7;

	__be64 RTTLOCT_novlan;
	__be64 RTTLOCT_NOFRM_novlan;
	__be64 ROCT_novlan;

	/* Count of Total received frames including bad frames */
	__be64 RTOT_prio_0;
	/* Count of  Total number of received frames with 802.1Q encapsulation */
	__be64 R1Q_prio_0;
	__be64 reserved1;

	__be64 RTOT_prio_1;
	__be64 R1Q_prio_1;
	__be64 reserved2;

	__be64 RTOT_prio_2;
	__be64 R1Q_prio_2;
	__be64 reserved3;

	__be64 RTOT_prio_3;
	__be64 R1Q_prio_3;
	__be64 reserved4;

	__be64 RTOT_prio_4;
	__be64 R1Q_prio_4;
	__be64 reserved5;

	__be64 RTOT_prio_5;
	__be64 R1Q_prio_5;
	__be64 reserved6;

	__be64 RTOT_prio_6;
	__be64 R1Q_prio_6;
	__be64 reserved7;

	__be64 RTOT_prio_7;
	__be64 R1Q_prio_7;
	__be64 reserved8;

	__be64 RTOT_novlan;
	__be64 R1Q_novlan;
	__be64 reserved9;

	/* Total number of Successfully Received Control Frames */
	__be64 RCNTL;
	__be64 reserved10;
	__be64 reserved11;
	__be64 reserved12;
	/* Count of received frames with a length/type field  value between 46
	   (42 for VLANtagged frames) and 1500 (also 1500 for VLAN-tagged frames),
	   inclusive */
	__be64 RInRangeLengthErr;
	/* Count of received frames with length/type field between 1501 and 1535
	   decimal, inclusive */
	__be64 ROutRangeLengthErr;
	/* Count of received frames that are longer than max allowed size for
	   802.3 frames (1518/1522) */
	__be64 RFrmTooLong;
	/* Count frames received with PCS error */
	__be64 PCS;

	/* Transmit frames with a length of 64 octets */
	__be64 T64_prio_0;
	__be64 T64_prio_1;
	__be64 T64_prio_2;
	__be64 T64_prio_3;
	__be64 T64_prio_4;
	__be64 T64_prio_5;
	__be64 T64_prio_6;
	__be64 T64_prio_7;
	__be64 T64_novlan;
	__be64 T64_loopbk;
	/* Transmit frames with a length of 65 to 127 octets. */
	__be64 T127_prio_0;
	__be64 T127_prio_1;
	__be64 T127_prio_2;
	__be64 T127_prio_3;
	__be64 T127_prio_4;
	__be64 T127_prio_5;
	__be64 T127_prio_6;
	__be64 T127_prio_7;
	__be64 T127_novlan;
	__be64 T127_loopbk;
	/* Transmit frames with a length of 128 to 255 octets */
	__be64 T255_prio_0;
	__be64 T255_prio_1;
	__be64 T255_prio_2;
	__be64 T255_prio_3;
	__be64 T255_prio_4;
	__be64 T255_prio_5;
	__be64 T255_prio_6;
	__be64 T255_prio_7;
	__be64 T255_novlan;
	__be64 T255_loopbk;
	/* Transmit frames with a length of 256 to 511 octets */
	__be64 T511_prio_0;
	__be64 T511_prio_1;
	__be64 T511_prio_2;
	__be64 T511_prio_3;
	__be64 T511_prio_4;
	__be64 T511_prio_5;
	__be64 T511_prio_6;
	__be64 T511_prio_7;
	__be64 T511_novlan;
	__be64 T511_loopbk;
	/* Transmit frames with a length of 512 to 1023 octets */
	__be64 T1023_prio_0;
	__be64 T1023_prio_1;
	__be64 T1023_prio_2;
	__be64 T1023_prio_3;
	__be64 T1023_prio_4;
	__be64 T1023_prio_5;
	__be64 T1023_prio_6;
	__be64 T1023_prio_7;
	__be64 T1023_novlan;
	__be64 T1023_loopbk;
	/* Transmit frames with a length of 1024 to 1518 octets */
	__be64 T1518_prio_0;
	__be64 T1518_prio_1;
	__be64 T1518_prio_2;
	__be64 T1518_prio_3;
	__be64 T1518_prio_4;
	__be64 T1518_prio_5;
	__be64 T1518_prio_6;
	__be64 T1518_prio_7;
	__be64 T1518_novlan;
	__be64 T1518_loopbk;
	/* Counts transmit frames with a length of 1519 to 1522 bytes */
	__be64 T1522_prio_0;
	__be64 T1522_prio_1;
	__be64 T1522_prio_2;
	__be64 T1522_prio_3;
	__be64 T1522_prio_4;
	__be64 T1522_prio_5;
	__be64 T1522_prio_6;
	__be64 T1522_prio_7;
	__be64 T1522_novlan;
	__be64 T1522_loopbk;
	/* Transmit frames with a length of 1523 to 1548 octets */
	__be64 T1548_prio_0;
	__be64 T1548_prio_1;
	__be64 T1548_prio_2;
	__be64 T1548_prio_3;
	__be64 T1548_prio_4;
	__be64 T1548_prio_5;
	__be64 T1548_prio_6;
	__be64 T1548_prio_7;
	__be64 T1548_novlan;
	__be64 T1548_loopbk;
	/* Counts transmit frames with a length of 1549 to MTU bytes */
	__be64 T2MTU_prio_0;
	__be64 T2MTU_prio_1;
	__be64 T2MTU_prio_2;
	__be64 T2MTU_prio_3;
	__be64 T2MTU_prio_4;
	__be64 T2MTU_prio_5;
	__be64 T2MTU_prio_6;
	__be64 T2MTU_prio_7;
	__be64 T2MTU_novlan;
	__be64 T2MTU_loopbk;
	/* Transmit frames with a length greater than MTU octets and a good CRC. */
	__be64 TGIANT_prio_0;
	__be64 TGIANT_prio_1;
	__be64 TGIANT_prio_2;
	__be64 TGIANT_prio_3;
	__be64 TGIANT_prio_4;
	__be64 TGIANT_prio_5;
	__be64 TGIANT_prio_6;
	__be64 TGIANT_prio_7;
	__be64 TGIANT_novlan;
	__be64 TGIANT_loopbk;
	/* Transmit broadcast frames with a good CRC */
	__be64 TBCAST_prio_0;
	__be64 TBCAST_prio_1;
	__be64 TBCAST_prio_2;
	__be64 TBCAST_prio_3;
	__be64 TBCAST_prio_4;
	__be64 TBCAST_prio_5;
	__be64 TBCAST_prio_6;
	__be64 TBCAST_prio_7;
	__be64 TBCAST_novlan;
	__be64 TBCAST_loopbk;
	/* Transmit multicast frames with a good CRC */
	__be64 TMCAST_prio_0;
	__be64 TMCAST_prio_1;
	__be64 TMCAST_prio_2;
	__be64 TMCAST_prio_3;
	__be64 TMCAST_prio_4;
	__be64 TMCAST_prio_5;
	__be64 TMCAST_prio_6;
	__be64 TMCAST_prio_7;
	__be64 TMCAST_novlan;
	__be64 TMCAST_loopbk;
	/* Transmit good frames that are neither broadcast nor multicast */
	__be64 TTOTG_prio_0;
	__be64 TTOTG_prio_1;
	__be64 TTOTG_prio_2;
	__be64 TTOTG_prio_3;
	__be64 TTOTG_prio_4;
	__be64 TTOTG_prio_5;
	__be64 TTOTG_prio_6;
	__be64 TTOTG_prio_7;
	__be64 TTOTG_novlan;
	__be64 TTOTG_loopbk;

	/* total octets of transmitted frames, including framing characters */
	__be64 TTTLOCT_prio_0;
	/* total octets of transmitted frames, not including framing characters */
	__be64 TTTLOCT_NOFRM_prio_0;
	/* ifOutOctets */
	__be64 TOCT_prio_0;

	__be64 TTTLOCT_prio_1;
	__be64 TTTLOCT_NOFRM_prio_1;
	__be64 TOCT_prio_1;

	__be64 TTTLOCT_prio_2;
	__be64 TTTLOCT_NOFRM_prio_2;
	__be64 TOCT_prio_2;

	__be64 TTTLOCT_prio_3;
	__be64 TTTLOCT_NOFRM_prio_3;
	__be64 TOCT_prio_3;

	__be64 TTTLOCT_prio_4;
	__be64 TTTLOCT_NOFRM_prio_4;
	__be64 TOCT_prio_4;

	__be64 TTTLOCT_prio_5;
	__be64 TTTLOCT_NOFRM_prio_5;
	__be64 TOCT_prio_5;

	__be64 TTTLOCT_prio_6;
	__be64 TTTLOCT_NOFRM_prio_6;
	__be64 TOCT_prio_6;

	__be64 TTTLOCT_prio_7;
	__be64 TTTLOCT_NOFRM_prio_7;
	__be64 TOCT_prio_7;

	__be64 TTTLOCT_novlan;
	__be64 TTTLOCT_NOFRM_novlan;
	__be64 TOCT_novlan;

	__be64 TTTLOCT_loopbk;
	__be64 TTTLOCT_NOFRM_loopbk;
	__be64 TOCT_loopbk;

	/* Total frames transmitted with a good CRC that are not aborted  */
	__be64 TTOT_prio_0;
	/* Total number of frames transmitted with 802.1Q encapsulation */
	__be64 T1Q_prio_0;
	__be64 reserved13;

	__be64 TTOT_prio_1;
	__be64 T1Q_prio_1;
	__be64 reserved14;

	__be64 TTOT_prio_2;
	__be64 T1Q_prio_2;
	__be64 reserved15;

	__be64 TTOT_prio_3;
	__be64 T1Q_prio_3;
	__be64 reserved16;

	__be64 TTOT_prio_4;
	__be64 T1Q_prio_4;
	__be64 reserved17;

	__be64 TTOT_prio_5;
	__be64 T1Q_prio_5;
	__be64 reserved18;

	__be64 TTOT_prio_6;
	__be64 T1Q_prio_6;
	__be64 reserved19;

	__be64 TTOT_prio_7;
	__be64 T1Q_prio_7;
	__be64 reserved20;

	__be64 TTOT_novlan;
	__be64 T1Q_novlan;
	__be64 reserved21;

	__be64 TTOT_loopbk;
	__be64 T1Q_loopbk;
	__be64 reserved22;

	/* Received frames with a length greater than MTU octets and a bad CRC */
	__be32 RJBBR;
	/* Received frames with a bad CRC that are not runts, jabbers,
	   or alignment errors */
	__be32 RCRC;
	/* Received frames with SFD with a length of less than 64 octets and a
	   bad CRC */
	__be32 RRUNT;
	/* Received frames with a length less than 64 octets and a good CRC */
	__be32 RSHORT;
	/* Total Number of Received Packets Dropped */
	__be32 RDROP;
	/* Drop due to overflow  */
	__be32 RdropOvflw;
	/* Drop due to overflow */
	__be32 RdropLength;
	/* Total of good frames. Does not include frames received with
	   frame-too-long, FCS, or length errors */
	__be32 RTOTFRMS;
	/* Total dropped Xmited packets */
	__be32 TDROP;
};

struct mlx4_func_stat_out_mbox {
	__be64 etherStatsDropEvents;
	__be64 etherStatsOctets;
	__be64 etherStatsPkts;
	__be64 etherStatsBroadcastPkts;
	__be64 etherStatsMulticastPkts;
	__be64 etherStatsCRCAlignErrors;
	__be64 etherStatsUndersizePkts;
	__be64 etherStatsOversizePkts;
	__be64 etherStatsFragments;
	__be64 etherStatsJabbers;
	__be64 etherStatsCollisions;
	__be64 etherStatsPkts64Octets;
	__be64 etherStatsPkts65to127Octets;
	__be64 etherStatsPkts128to255Octets;
	__be64 etherStatsPkts256to511Octets;
	__be64 etherStatsPkts512to1023Octets;
	__be64 etherStatsPkts1024to1518Octets;
};

struct net_device_stats
{
	uint64_t lastUpdate; // The actual time that the data was read
	u64 tx_packets; 	// number of transmited packets
	u64 tx_bytes;		// number of transmited bytes
	u64 tx_errors;		// number of transmitted error
	
	u64 rx_packets; 	// number of received packets
	u64 rx_bytes;		// number of received bytes
	u64 rx_errors;		// number of received errors
	u64 rx_drop_ovflw;	// number of frames droped due to lack of receive space
	u64 rx_bad_crc; 	// number of frames that are received with checksum errors

	u64 tx_unicast_pkts;	    // number of transmited unicast packets
	u64 tx_unicast_octets;	    // number of transmited unicast octets
	
	u64 tx_multicast_pkts;	    // number of transmited multicast packets
	u64 tx_multicast_octets;	// number of transmited multicast octets
	
	u64 tx_broadcast_pkts;	    // number of transmited broadcast packets
	u64 tx_broadcast_octets;	// number of transmited broadcast octets

	u64 rx_unicast_pkts;	    // number of received unicast packets
	u64 rx_unicast_octets;	    // number of received unicast octets
	
	u64 rx_multicast_pkts;	    // number of received multicast packets
	u64 rx_multicast_octets;	// number of received multicast octets
	
	u64 rx_broadcast_pkts;  	// number of received broadcast packets
	u64 rx_broadcast_octets;	// number of received broadcast octets
};

struct mlx4_query_if_stat_ext_out_mbox
{
    u8 rs1[3];
    u8 if_cnt_mode;    
    u8 rs2[3];
    u8 num_of_if;
    u32 rs3[2];
    
    unsigned int    ifRxUnicastFrames_high;
    unsigned int    ifRxUnicastFrames_low;
    unsigned int    ifRxUnicastOctets_high;
    unsigned int    ifRxUnicastOctets_low;
    unsigned int    ifRxMulticastFrames_high;
    unsigned int    ifRxMulticastFrames_low;
    unsigned int    ifRxMulticastOctets_high;
    unsigned int    ifRxMulticastOctets_low;
    unsigned int    ifRxBroadcastFrames_high;
    unsigned int    ifRxBroadcastFrames_low;
    unsigned int    ifRxBroadcastOctets_high;
    unsigned int    ifRxBroadcastOctets_low;
    unsigned int    ifRxNoBufferFrames_high;
    unsigned int    ifRxNoBufferFrames_low;
    unsigned int    ifRxNoBufferOctets_high;
    unsigned int    ifRxNoBufferOctets_low;
    unsigned int    ifRxErrorFrames_high;
    unsigned int    ifRxErrorFrames_low;
    unsigned int    ifRxErrorOctets_high;
    unsigned int    ifRxErrorOctets_low;
    unsigned char   reserved0[156];
    unsigned int    ifTxUnicastFrames_high;
    unsigned int    ifTxUnicastFrames_low;
    unsigned int    ifTxUnicastOctets_high;
    unsigned int    ifTxUnicastOctets_low;
    unsigned int    ifTxMulticastFrames_high;
    unsigned int    ifTxMulticastFrames_low;
    unsigned int    ifTxMulticastOctets_high;
    unsigned int    ifTxMulticastOctets_low;
    unsigned int    ifTxBroadcastFrames_high;
    unsigned int    ifTxBroadcastFrames_low;
    unsigned int    ifTxBroadcastOctets_high;
    unsigned int    ifTxBroadcastOctets_low;
    unsigned int    ifTxDroppedFrames_high;
    unsigned int    ifTxDroppedFrames_low;
    unsigned int    ifTxDroppedOctets_high;
    unsigned int    ifTxDroppedOctets_low;
    unsigned int    ifTxRequestedFramesSent_high;
    unsigned int    ifTxRequestedFramesSent_low;
    unsigned int    ifTxGeneratedFramesSent_high;
    unsigned int    ifTxGeneratedFramesSent_low;
    unsigned int    ifTxTsoOctets_high;
    unsigned int    ifTxTsoOctets_low;
};

struct mlx4_query_if_stat_basic_out_mbox
{
    u8 rs1[3];
    u8 if_cnt_mode;
    u8 rs2[3];
    u8 num_of_if;
    u32 rs3[2];
    
    unsigned int    ifRxFrames_high;
    unsigned int    ifRxFrames_low;
    unsigned int    ifRxOctets_high;
    unsigned int    ifRxOctets_low;
    
    unsigned int    ifTxFrames_high;
    unsigned int    ifTxFrames_low;
    unsigned int    ifTxOctets_high;
    unsigned int    ifTxOctets_low;    
};

struct mlx4_qos_element
{
	u16 port_number; //port number to apply the priority on.
	u16 port_priority;
};

struct mlx4_qos_settings
{
	bool defaultSettingExist; 	//True if NDIS default setting exist.
	u16 priority; 				//default proirity.

	struct mlx4_qos_element* p_qos_settings; //save settings for rest of the protocols
	u16 alloc_count; 			//size of mlx4_qos_settings.
	u16 count; 					//number of elements in qosSetting
};


static inline void mlx4_query_steer_cap(struct mlx4_dev *dev, int *log_mac,
					int *log_vlan, int *log_prio)
{
	*log_mac = dev->caps.log_num_macs;
	*log_vlan = dev->caps.log_num_vlans;
	*log_prio = dev->caps.log_num_prios;
}



#define foreach_port(port, bitmap) \
	for ((port) = 1; (port) <= MLX4_MAX_PORTS; ++(port)) \
		if (bitmap & 1 << ((port)-1))

static inline int mlx4_get_fexch_mpts_base(struct mlx4_dev *dev)
{
	return dev->caps.reserved_fexch_mpts_base;
}

static inline int mlx4_is_slave(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_SLAVE;
}

static inline int mlx4_is_master(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_MASTER;
}

static inline int mlx4_is_mfunc(struct mlx4_dev *dev)
{
	return dev->flags & (MLX4_FLAG_MASTER | MLX4_FLAG_SLAVE);
}

static inline int mlx4_is_vf(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_VF;
}

static inline int mlx4_is_msi(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_MSI_X;
}


int mlx4_buf_alloc(struct mlx4_dev *dev, int size, int max_direct,
		   struct mlx4_buf *buf);
void mlx4_buf_free(struct mlx4_dev *dev, int size, struct mlx4_buf *buf);

static inline void *mlx4_buf_offset(struct mlx4_buf *buf, int offset)
{
	if (buf->nbufs == 1)
		return buf->direct.buf + offset;
	else
		return buf->page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

int mlx4_db_alloc(struct mlx4_dev *dev, 
				struct mlx4_db *db, int order);

void mlx4_db_free(struct mlx4_dev *dev, struct mlx4_db *db);

int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn);
void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn);

int mlx4_uar_alloc(struct mlx4_dev *dev, struct mlx4_uar *uar);
void mlx4_uar_free(struct mlx4_dev *dev, struct mlx4_uar *uar);

int mlx4_bf_alloc(struct mlx4_dev *dev, struct mlx4_bf *bf);
void mlx4_bf_free(struct mlx4_dev *dev, struct mlx4_bf *bf);

int mlx4_mtt_init(struct mlx4_dev *dev, int npages, int page_shift,
		  struct mlx4_mtt *mtt);
void mlx4_mtt_cleanup(struct mlx4_dev *dev, struct mlx4_mtt *mtt);
u64 mlx4_mtt_addr(struct mlx4_dev *dev, struct mlx4_mtt *mtt);

int mlx4_mr_alloc_reserved(struct mlx4_dev *dev, u32 mridx, u32 pd,
			   u64 iova, u64 size, u32 access, int npages,
			   int page_shift, struct mlx4_mr *mr);
int mlx4_mr_alloc(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr);
void mlx4_mr_free(struct mlx4_dev *dev, struct mlx4_mr *mr);
void mlx4_mr_free_reserved(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_mr_enable(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   int start_index, int npages, u64 *page_list);
int mlx4_buf_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
			   struct mlx4_buf *buf);
int mlx4_mr_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base_mridx);
void mlx4_mr_release_range(struct mlx4_dev *dev, int base_mridx, int cnt);

struct device;

int mlx4_alloc_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
			  int size, int max_direct);
void mlx4_free_hwq_res(struct mlx4_dev *mdev, struct mlx4_hwq_resources *wqres,
			  int size);

int mlx4_cq_alloc(struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
		  struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq,
		  unsigned vector, int collapsed);
void mlx4_cq_free(struct mlx4_dev *dev, struct mlx4_cq *cq);

struct mlx4_cq_context;
int mlx4_cq_modify(struct mlx4_dev *dev, struct mlx4_cq *cq,
		   struct mlx4_cq_context *context, int modify);

static inline void mlx4_cq_arm(struct mlx4_cq *cq, u32 cmd,
				   void __iomem *uar_page,
				   spinlock_t *doorbell_lock);

enum mlx4_qp_state;
enum mlx4_qp_optpar;
struct mlx4_qp_context;

int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base);
void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt);
int mlx4_qp_alloc(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp);
void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp);
u32 mlx4_get_slave_sqp(struct mlx4_dev *dev, int vf);

int mlx4_qp_modify(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		   struct mlx4_qp_context *context, enum mlx4_qp_optpar optpar,
		   int sqd_event, struct mlx4_qp *qp, int is_rdma_qp);


int mlx4_qp_to_ready(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
			 struct mlx4_qp_context *context,
			 struct mlx4_qp *qp, enum mlx4_qp_state *qp_state);

void mlx4_qp_remove(struct mlx4_dev *dev, struct mlx4_qp *qp);


int mlx4_srq_alloc(struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcd,
			struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq);
void mlx4_srq_free(struct mlx4_dev *dev, struct mlx4_srq *srq);

void mlx4_srq_invalidate(struct mlx4_dev *dev, struct mlx4_srq *srq);
void mlx4_srq_remove(struct mlx4_dev *dev, struct mlx4_srq *srq);

int mlx4_srq_arm(struct mlx4_dev *dev, struct mlx4_srq *srq, int limit_watermark);
int mlx4_srq_query(struct mlx4_dev *dev, struct mlx4_srq *srq, int *limit_watermark);

int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu, int roce_mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx);
int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc, u8 fVmq);

int mlx4_INIT_PORT(struct mlx4_dev *dev, int port);
int mlx4_CLOSE_PORT(struct mlx4_dev *dev, int port);

int mlx4_multicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8* gid,
	int block_mcast_loopback, enum mlx4_protocol prot, int block_lb);
int mlx4_multicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8* gid,
	enum mlx4_protocol prot);

int mlx4_multicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_multicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_enable_unicast_promisc(struct mlx4_dev *dev, u8 port, u32 qpn);
int mlx4_disable_unicast_promisc(struct mlx4_dev *dev, u8 port, u32 qpn);

int mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac, int* qpn, bool wrap, bool reserve);
void mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, int qpn, bool free);
int mlx4_replace_mac(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac);

int mlx4_register_mac_and_vlan(struct mlx4_dev *dev, u8 port, u64 mac, u8 check_vlan, u8 vlan_valid, u16 vlan, int* qpn, bool wrap, bool reserve);
void mlx4_unregister_mac_and_vlan(struct mlx4_dev *dev, u8 port, int qpn, bool free);

int mlx4_find_vlan_index(struct mlx4_dev *dev, u8 port, u16 vlan, int *vidx);

int mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan, int *index);
void mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, int index);

int mlx4_map_phys_fmr(struct mlx4_dev *dev, struct mlx4_fmr *fmr, u64 *page_list,
			  int npages, u64 iova, u32 *lkey, u32 *rkey);
int mlx4_map_phys_fmr_fbo(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			  u64 *page_list, int npages, u64 iova,
			  u32 fbo, u32 len, u32 *lkey, u32 *rkey, int same_key);
int mlx4_fmr_alloc(struct mlx4_dev *dev, u32 pd, u32 access, int max_pages,
		   int max_maps, u8 page_shift, struct mlx4_fmr *fmr);
int mlx4_fmr_alloc_reserved(struct mlx4_dev *dev, u32 mridx, u32 pd,
				u32 access, int max_pages, int max_maps,
				u8 page_shift, struct mlx4_fmr *fmr);
int mlx4_fmr_enable(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
void mlx4_fmr_unmap(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			u32 *lkey, u32 *rkey);
int mlx4_fmr_free(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
int mlx4_fmr_free_reserved(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
int mlx4_SYNC_TPT(struct mlx4_dev *dev);

int mlx4_SET_PORT(struct mlx4_dev *dev, u8 port);
int mlx4_QUERY_PORT(struct mlx4_dev *dev, u8 port, struct mlx4_vep_cfg* pContext);
int mlx4_SET_VLAN_FLTR(struct mlx4_dev *dev, u8 port, u32 *Vlans);
int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);

int mlx4_set_wol(struct mlx4_dev *dev, ULONG wol_type, int port);
bool is_wol_supported(struct mlx4_dev *dev, int port);

BOOLEAN mlx4_is_qos_supported(struct mlx4_dev *dev, int port_number);
BOOLEAN mlx4_is_roce_port(struct mlx4_dev *dev, int port_number);
int mlx4_count_roce_ports(struct mlx4_dev *dev);
int mlx4_count_eth_ports(struct mlx4_dev *dev);


#endif /* MLX4_DEVICE_H */
