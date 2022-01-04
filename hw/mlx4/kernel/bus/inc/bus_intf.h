#pragma once

#define MLX4_BUS_IB_INTERFACE_VERSION		17

#include <ib_verbs.h>
//
// Interface for work with MLX4 IB driver
//

struct mlx4_interface;
struct mlx4_dev;
struct mlx4_cq_context;
enum mlx4_qp_state;
struct mlx4_qp_context;
enum mlx4_qp_optpar;
struct mlx4_eq;


typedef int (*MLX4_REGISTER_INTERFACE)(struct mlx4_interface *intf);
typedef VOID (*MLX4_UNREGISTER_INTERFACE)(struct mlx4_interface *intf);


int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn);
void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn);

typedef int  (*MLX4_PD_ALLOC) (struct mlx4_dev *dev, u32 *pdn);
typedef void (*MLX4_PD_FREE)(struct mlx4_dev *dev, u32 pdn);

typedef int  (*MLX4_UAR_ALLOC)(struct mlx4_dev *dev, struct mlx4_uar *uar);
typedef void (*MLX4_UAR_FREE)(struct mlx4_dev *dev, struct mlx4_uar *uar);

typedef int  (*MLX4_MR_ALLOC)(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
				 int npages, int page_shift, struct mlx4_mr *mr);
typedef void (*MLX4_MR_FREE)(struct mlx4_dev *dev, struct mlx4_mr *mr);
typedef int  (*MLX4_MR_ENABLE)(struct mlx4_dev *dev, struct mlx4_mr *mr);

typedef int  (*MLX4_QP_GET_REGION) (struct mlx4_dev *dev, enum qp_region region, int *base_qpn, int *cnt);

typedef int  (*MLX4_ALLOC_HWQ_RES)(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
			  int size, int max_direct);

typedef void (*MLX4_FREE_HWQ_RES)(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
			  int size);

typedef int (*MLX4_CQ_ALLOC) (struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
			  struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq, unsigned vector, int collapsed);

typedef void (*MLX4_CQ_FREE) (struct mlx4_dev *dev, struct mlx4_cq *cq);

typedef int (*MLX4_CQ_MODIFY) (struct mlx4_dev *dev, struct mlx4_cq *cq,
			  struct mlx4_cq_context *context, int modify);

typedef void (*MLX4_CQ_ARM)(struct mlx4_cq *cq, u32 cmd,
				   void __iomem *uar_page,
				   spinlock_t *doorbell_lock);

typedef int (*MLX4_REGISTER_MAC)(struct mlx4_dev *dev, u8 port, u64 mac, int* qpn, bool wrap, bool reserve);
typedef void (*MLX4_UNREGISTER_MAC) (struct mlx4_dev *dev, u8 port, int index, bool free);
typedef int (*MLX4_REPLACE_MAC)(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac);

typedef int (*MLX4_REGISTER_MAC_AND_VLAN)(struct mlx4_dev *dev, u8 port, u64 mac, u8 check_vlan, u8 vlan_valid, u16 vlan, int* qpn, bool wrap, bool reserve);
typedef void (*MLX4_UNREGISTER_MAC_AND_VLAN)(struct mlx4_dev *dev, u8 port, int qpn, bool free);

typedef int (*MLX4_SRQ_ALLOC) (struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcd,
	struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq);

typedef void (*MLX4_SRQ_FREE)(struct mlx4_dev *dev, struct mlx4_srq *srq);

typedef void (*MLX4_SRQ_INVALIDATE)(struct mlx4_dev *dev, struct mlx4_srq *srq);

typedef void (*MLX4_SRQ_REMOVE)(struct mlx4_dev *dev, struct mlx4_srq *srq);

typedef int (*MLX4_QP_ALLOC)(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp);

typedef void (*MLX4_QP_FREE)(struct mlx4_dev *dev, struct mlx4_qp *qp);


typedef int (*MLX4_QP_RESERVE_RANGE)(struct mlx4_dev *dev, int cnt, int align, int *base);

typedef void (*MLX4_QP_RELEASE_RANGE)(struct mlx4_dev *dev, int base_qpn, int cnt);


typedef void (*MLX4_QP_REMOVE)(struct mlx4_dev *dev, struct mlx4_qp *qp);

typedef int (*MLX4_QP_MODIFY)(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   enum mlx4_qp_state cur_state, enum mlx4_qp_state new_state,
		   struct mlx4_qp_context *context, enum mlx4_qp_optpar optpar,
		   int sqd_event, struct mlx4_qp *qp, int is_rdma_qp);

typedef int (*MLX4_QP_TO_READY)(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
			 struct mlx4_qp_context *context,
			 struct mlx4_qp *qp, enum mlx4_qp_state *qp_state);

typedef struct mlx4_cmd_mailbox *(*MLX4_ALLOC_CMD_MAILBOX)(struct mlx4_dev *dev);

typedef void (*MLX4_FREE_CMD_MAILBOX)(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox);

typedef int (*MLX4_CMD)(struct mlx4_dev *dev, u64 in_param, u64 *out_param, int out_is_imm,
			u32 in_modifier, u8 op_modifier, u16 op, unsigned long timeout);

typedef int (*MLX4_INIT_PORT)(struct mlx4_dev *dev, int port);
typedef int (*MLX4_CLOSE_PORT)(struct mlx4_dev *dev, int port);

typedef int (*MLX4_MCAST_ATTACH)(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
				int block_mcast_loopback, enum mlx4_protocol prot, int block_lb);
typedef int (*MLX4_MCAST_DETACH)(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
				enum mlx4_protocol prot);
typedef int (*MLX4_ENABLE_PROMISC_MULTICAST)(struct mlx4_dev *dev, u32 qpn, u8 port);
typedef int (*MLX4_DISABLE_PROMISC_MULTICAST)(struct mlx4_dev *dev, u32 qpn, u8 port);
typedef int (*MLX4_ENABLE_PROMISC_UNIST)(struct mlx4_dev *dev, u8 port, u32 qpn);
typedef int (*MLX4_DISABLE_PROMISC_UNIST)(struct mlx4_dev *dev, u8 port, u32 qpn);

typedef
void
(*PISR_FUNC)(
	IN PVOID  IsrContext
	);


typedef int (*MLX4_ADD_EQ) (struct mlx4_dev *dev, int nent,
	KAFFINITY cpu, u8* p_eq_num, struct mlx4_eq ** p_eq);

typedef void (*MLX4_REMOVE_EQ) (struct mlx4_dev *dev, u8 eq_num);

typedef BOOLEAN (*MLX4_PROCESS_EQES)( struct mlx4_eq *p_eq );

typedef int (*MLX4_REGISTER_EVENT_HANDLER) (struct ib_event_handler *event_handler);
typedef int (*MLX4_UNREGISTER_EVENT_HANDLER)(struct ib_event_handler *event_handler);

typedef int (*MLX4_RESET_REQUEST) (struct ib_event_handler *event_handler);
typedef int (*MLX4_RESET_EXECUTE) (struct ib_event_handler *event_handler);
typedef int (*MLX4_RESET_READY) (struct ib_event_handler *event_handler);

typedef int (*MLX4_SET_VLAN_FLTR) (struct mlx4_dev *dev, u8 port, u32 *Vlans);
typedef int (*MLX4_SET_MCAST_FLTR) (struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);
typedef int (*MLX4_SET_PORT_QPN_CALC) (struct mlx4_dev *dev, u8 port, u32 base_qpn, u8 promisc, u8 fVmq);
typedef int (*MLX4_SET_PORT_GEN) (struct mlx4_dev *dev, u8 port, int mtu,int roce_mtu,
                u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx);

typedef int(*MLX4_SET_PORT) (struct mlx4_dev *dev, u8 port, struct mlx4_vep_cfg* pContext);

typedef int (*MLX4_SET_PORT_PRIO2TC)  (struct mlx4_dev *dev, u8 port, struct prio2tc_context* inbox);
typedef int (*MLX4_SET_PORT_SCHEDULER)(struct mlx4_dev *dev, u8 port, struct set_port_scheduler_context* inbox);

typedef int(*MLX4_QUERY_PORT) (struct mlx4_dev *dev, u8 port, struct mlx4_vep_cfg* pContext);

typedef int(*MLX4_QUERY_IF_STAT) (struct mlx4_dev *dev,u8 reset,u8 batch, u16 counter_index,
                                  struct net_device_stats *stats);

typedef int(*MLX4_SET_WOL)(struct mlx4_dev *dev, ULONG wol_type, int port);

typedef int (*MLX4_SET_PROTOCOL_QOS)(struct mlx4_dev *dev,	struct mlx4_qos_settings* settings, u16 count, u8 port);

/* Diagnostics */
typedef int(*MLX4_TEST_INTERRUPTS)(struct mlx4_dev *dev);

/* WorkerThread */
typedef NTSTATUS (*MLX4_ADD_RING)(ULONG CpuNumber, void* pWorkerThreadData	);
typedef void (*MLX4_REMOVE_RING)(ULONG CpuNumber, void* pWorkerThreadData);
typedef bool (*MLX4_CHECK_IF_STARVING)(ULONG ulProcessorNumber);
typedef void (*MLX4_START_POLLING)();
typedef void (*MLX4_STOP_POLLING)();
typedef void (*MLX4_CONTINUE_POLLING)();

/* Legacy statistics */
typedef int(*MLX4_DUMP_STATS) (struct mlx4_dev *dev, u8 port, u8 reset, 
								struct net_device_stats *stats);
	
/* This structure is a part of MLX4_BUS_IB_INTERFACE */
/* Upon its changing you have to change MLX4_BUS_IB_INTERFACE_VERSION */
struct mlx4_interface_ex {
	MLX4_PD_ALLOC		mlx4_pd_alloc;
	MLX4_PD_FREE		mlx4_pd_free;

	MLX4_UAR_ALLOC		mlx4_uar_alloc;
	MLX4_UAR_FREE		mlx4_uar_free;

	MLX4_MR_ALLOC		mlx4_mr_alloc;
	MLX4_MR_FREE		mlx4_mr_free;
	MLX4_MR_ENABLE		mlx4_mr_enable;
	MLX4_QP_GET_REGION	mlx4_qp_get_region;
	MLX4_ALLOC_HWQ_RES	mlx4_alloc_hwq_res;
	MLX4_FREE_HWQ_RES	mlx4_free_hwq_res;

	MLX4_CQ_ALLOC		mlx4_cq_alloc;
	MLX4_CQ_FREE		mlx4_cq_free;
	MLX4_CQ_MODIFY		mlx4_cq_modify;

//	Not part of the interface since it is an inlie function
//	MLX4_CQ_ARM 		mlx4_cq_arm;

	MLX4_REPLACE_MAC	mlx4_replace_mac;

	MLX4_SRQ_ALLOC		mlx4_srq_alloc;
	MLX4_SRQ_FREE		mlx4_srq_free;
	MLX4_SRQ_INVALIDATE mlx4_srq_invalidate;
	MLX4_SRQ_REMOVE 	mlx4_srq_remove;

	MLX4_QP_ALLOC		mlx4_qp_alloc;
	MLX4_QP_FREE		mlx4_qp_free;
	MLX4_QP_REMOVE		mlx4_qp_remove;
	MLX4_QP_MODIFY		mlx4_qp_modify;
	MLX4_QP_TO_READY	mlx4_qp_to_ready;
	MLX4_QP_RESERVE_RANGE mlx4_qp_reserve_range;
	MLX4_QP_RELEASE_RANGE mlx4_qp_release_range;

	MLX4_ALLOC_CMD_MAILBOX mlx4_alloc_cmd_mailbox;
	MLX4_FREE_CMD_MAILBOX  mlx4_free_cmd_mailbox;
	MLX4_CMD			   mlx4_cmd;

	MLX4_INIT_PORT		   mlx4_INIT_PORT;
	MLX4_CLOSE_PORT 	   mlx4_CLOSE_PORT;

	MLX4_ADD_EQ 		   mlx4_add_eq;
	MLX4_REMOVE_EQ		   mlx4_remove_eq;
	MLX4_PROCESS_EQES 	   mlx4_process_eqes;

	MLX4_REGISTER_EVENT_HANDLER mlx4_register_ev_cb;
	MLX4_UNREGISTER_EVENT_HANDLER mlx4_unregister_ev_cb;
	MLX4_RESET_REQUEST mlx4_reset_request;
	MLX4_RESET_EXECUTE mlx4_reset_execute;
	MLX4_RESET_READY mlx4_reset_ready;

	MLX4_REGISTER_MAC_AND_VLAN	   mlx4_register_mac_and_vlan;
	MLX4_UNREGISTER_MAC_AND_VLAN   mlx4_unregister_mac_and_vlan;

	MLX4_MCAST_ATTACH	   mlx4_multicast_attach;
	MLX4_MCAST_DETACH	   mlx4_multicast_detach;
	MLX4_ENABLE_PROMISC_MULTICAST  mlx4_multicast_promisc_add;
	MLX4_DISABLE_PROMISC_MULTICAST mlx4_multicast_promisc_remove;
	MLX4_ENABLE_PROMISC_UNIST mlx4_enable_unicast_promisc;
	MLX4_DISABLE_PROMISC_UNIST mlx4_disable_unicast_promisc;

	MLX4_SET_VLAN_FLTR	   mlx4_set_vlan_fltr;
	MLX4_SET_MCAST_FLTR    mlx4_set_mcast_fltr;
	MLX4_SET_PORT_QPN_CALC mlx4_set_port_qpn_calc;
	MLX4_SET_PORT_GEN	   mlx4_set_port_general;

    MLX4_SET_PORT_PRIO2TC   mlx4_set_port_prio2tc;
    MLX4_SET_PORT_SCHEDULER mlx4_set_port_scheduler;

	MLX4_QUERY_PORT 	   mlx4_query_port;
	MLX4_SET_WOL		   mlx4_set_wol;

    MLX4_QUERY_IF_STAT     mlx4_query_if_stat;

	MLX4_SET_PROTOCOL_QOS  mlx4_set_protocol_qos;

	/* Diagnostics */
	MLX4_TEST_INTERRUPTS   mlx4_test_interrupts;

	/* WorkerThread */
	MLX4_ADD_RING		   mlx4_add_ring;
	MLX4_REMOVE_RING	   mlx4_remove_ring;
	MLX4_CHECK_IF_STARVING mlx4_check_if_starving;
	MLX4_START_POLLING	   mlx4_start_polling;
	MLX4_STOP_POLLING 	   mlx4_stop_polling;
	MLX4_CONTINUE_POLLING  mlx4_continue_polling;
	
	MLX4_DUMP_STATS 	   mlx4_dump_stats;
};


/* Upon changing the structure you have to change also MLX4_BUS_IB_INTERFACE_VERSION */
typedef struct _MLX4_BUS_IB_INTERFACE{
	INTERFACE i;
	struct ib_device		*	p_ibdev;
	struct pci_dev			*	pdev;
	struct mlx4_dev			*	pmlx4_dev;
	struct mlx4_interface_ex	mlx4_interface;
	u8							port_id;
	struct VipBusIfc			*pVipBusIfc;
	int							n_msi_vectors;
	PDEVICE_OBJECT				p_ibbus_fdo;
	
} MLX4_BUS_IB_INTERFACE, *PMLX4_BUS_IB_INTERFACE;




