#pragma once

#define MLX4_BUS_FC_INTERFACE_VERSION		1
#define FC_MAX_PORTS	 2

struct mlx4_cmd_mailbox;
struct mlx4_qp;
struct mlx4_mtt;
struct mlx4_uar;
struct mlx4_mr;
struct pci_dev;

typedef int (*FC_MLX4_ALLOC_HWQ_RES)(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
			   int size, int max_direct);
typedef void (*FC_MLX4_FREE_HWQ_RES)(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
			  int size);
typedef int (*FC_MLX4_QP_RESERVE_RANGE)(struct mlx4_dev *dev, int cnt, int align, int *base);
typedef void (*FC_MLX4_QP_RELEASE_RANGE)(struct mlx4_dev *dev, int base_qpn, int cnt);
typedef struct mlx4_cmd_mailbox* (*FC_MLX4_ALLOC_CMD_MAILBOX)(struct mlx4_dev *dev);
typedef void (*FC_MLX4_FREE_CMD_MAILBOX)(struct mlx4_dev *dev, struct mlx4_cmd_mailbox *mailbox);
typedef int (*FC_MLX4_REGISTER_VLAN)(struct mlx4_dev *dev, u8 port, u16 vlan, int *index);
typedef void (*FC_MLX4_UNREGISTER_VLAN)(struct mlx4_dev *dev, u8 port, int index);
typedef int (*FC_MLX4_REGISTER_MAC)(struct mlx4_dev *dev, u8 port, u64 mac, int* qpn, bool wrap, bool reserve);
typedef void (*FC_MLX4_UNREGISTER_MAC)(struct mlx4_dev *dev, u8 port, int qpn, bool free);
typedef int (*FC_MLX4_MR_RESERVE_RANGE)(struct mlx4_dev *dev, int cnt, int align, int *base_mridx);
typedef void (*FC_MLX4_MR_RELEASE_RANGE)(struct mlx4_dev *dev, int base_mridx, int cnt);
typedef int (*FC_MLX4_MR_ALLOC)(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr);
typedef void (*FC_MLX4_MR_FREE)(struct mlx4_dev *dev, struct mlx4_mr *mr);
typedef int (*FC_MLX4_MR_ENABLE)(struct mlx4_dev *dev, struct mlx4_mr *mr);
typedef int (*FC_MLX4_PD_ALLOC)(struct mlx4_dev *dev, u32 *pdn);
typedef void (*FC_MLX4_PD_FREE)(struct mlx4_dev *dev, u32 pdn);
typedef int (*FC_MLX4_UAR_ALLOC)(struct mlx4_dev *dev, struct mlx4_uar *uar);
typedef void (*FC_MLX4_UAR_FREE)(struct mlx4_dev *dev, struct mlx4_uar *uar);
typedef int (*FC_MLX4_QP_ALLOC)(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp);
typedef void (*FC_MLX4_QP_FREE)(struct mlx4_dev *dev, struct mlx4_qp *qp);
typedef void (*FC_MLX4_QP_REMOVE)(struct mlx4_dev *dev, struct mlx4_qp *qp);
typedef void (*FC_MLX4_QP_FREE)(struct mlx4_dev *dev, struct mlx4_qp *qp);
typedef int (*FC_MLX4_QP_TO_READY)(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
			 struct mlx4_qp_context *context,
			 struct mlx4_qp *qp, enum mlx4_qp_state *qp_state);
typedef int (*FC_MLX4_QP_QUERY)(struct mlx4_dev *dev, struct mlx4_qp *qp,
		  struct mlx4_qp_context *context);
typedef int (*FC_MLX4_CQ_ALLOC)(struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
	   struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq,
	   unsigned vector, int collapsed);
typedef void (*FC_MLX4_CQ_FREE)(struct mlx4_dev *dev, struct mlx4_cq *cq);
typedef int (*FC_MLX4_REGISTER_INTERFACE)(struct mlx4_interface *intf);
typedef void (*FC_MLX4_UNREGISTER_INTERFACE)(struct mlx4_interface *intf);
typedef int (*FC_MLX4_CMD)(struct mlx4_dev *dev, u64 in_param, u64 *out_param, int out_is_imm,
		u32 in_modifier, u8 op_modifier, u16 op, unsigned long timeout);
typedef int (*FC_MLX4_MAP_PHYS_FMR_FBO)(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			  u64 *page_list, int npages, u64 iova,
			  u32 fbo, u32 len, u32 *lkey, u32 *rkey, int same_key);

typedef void (*FC_MLX4_FMR_UNMAP)(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
			u32 *lkey, u32 *rkey);

typedef int (*FC_MLX4_FMR_ALLOC_RESERVED)(struct mlx4_dev *dev, u32 mridx, u32 pd,
				u32 access, int max_pages, int max_maps,
				u8 page_shift, struct mlx4_fmr *fmr);

typedef int (*FC_MLX4_FMR_ENABLE)(struct mlx4_dev *dev, struct mlx4_fmr *fmr);

typedef int (*FC_MLX4_FMR_FREE_RESERVED)(struct mlx4_dev *dev, struct mlx4_fmr *fmr);

typedef struct
{
	FC_MLX4_ALLOC_HWQ_RES mlx4_alloc_hwq_res;
	FC_MLX4_FREE_HWQ_RES mlx4_free_hwq_res;
	FC_MLX4_QP_RESERVE_RANGE mlx4_qp_reserve_range;
	FC_MLX4_QP_RELEASE_RANGE mlx4_qp_release_range;
	FC_MLX4_ALLOC_CMD_MAILBOX mlx4_alloc_cmd_mailbox;
	FC_MLX4_FREE_CMD_MAILBOX mlx4_free_cmd_mailbox;
	FC_MLX4_REGISTER_VLAN mlx4_register_vlan;
	FC_MLX4_UNREGISTER_VLAN mlx4_unregister_vlan;
	FC_MLX4_REGISTER_MAC mlx4_register_mac;
	FC_MLX4_UNREGISTER_MAC mlx4_unregister_mac;
	FC_MLX4_MR_RESERVE_RANGE mlx4_mr_reserve_range;    
	FC_MLX4_MR_RELEASE_RANGE mlx4_mr_release_range; 
	FC_MLX4_MR_ALLOC mlx4_mr_alloc;
	FC_MLX4_MR_FREE mlx4_mr_free;	 
	FC_MLX4_MR_ENABLE mlx4_mr_enable;
	FC_MLX4_PD_ALLOC mlx4_pd_alloc;
	FC_MLX4_PD_FREE mlx4_pd_free;
	FC_MLX4_UAR_ALLOC mlx4_uar_alloc;
	FC_MLX4_UAR_FREE mlx4_uar_free;
	FC_MLX4_QP_ALLOC mlx4_qp_alloc;
	FC_MLX4_QP_FREE mlx4_qp_free;
	FC_MLX4_QP_REMOVE mlx4_qp_remove;
	FC_MLX4_QP_TO_READY mlx4_qp_to_ready;	
	FC_MLX4_QP_QUERY mlx4_qp_query;
	FC_MLX4_CQ_ALLOC mlx4_cq_alloc;
	FC_MLX4_CQ_FREE mlx4_cq_free;
	FC_MLX4_REGISTER_INTERFACE mlx4_register_interface;
	FC_MLX4_UNREGISTER_INTERFACE mlx4_unregister_interface;
	FC_MLX4_CMD mlx4_cmd;
	FC_MLX4_MAP_PHYS_FMR_FBO mlx4_map_phys_fmr_fbo;
	FC_MLX4_FMR_UNMAP mlx4_fmr_unmap;
	FC_MLX4_FMR_ALLOC_RESERVED mlx4_fmr_alloc_reserved;
	FC_MLX4_FMR_ENABLE mlx4_fmr_enable;
	FC_MLX4_FMR_FREE_RESERVED mlx4_fmr_free_reserved;	 
} mlx4_fc_bus_api;

typedef struct 
{
	INTERFACE i;
	mlx4_fc_bus_api mlx4_bus_api;
	struct pci_dev *pdev;
	struct mlx4_dev	*pmlx4_dev;
	u8 port_id;
} MLX4_FC_BUS_IF;

