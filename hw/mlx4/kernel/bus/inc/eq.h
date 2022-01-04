/*
 * Copyright (c) 2008 Mellanox Technologies.  All rights reserved.
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

#ifndef MLX4_EQ_H
#define MLX4_EQ_H

enum {
	MLX4_NUM_ASYNC_EQE	= 0x100,
	MLX4_NUM_SPARE_EQE	= 0x80,
	MLX4_EQ_ENTRY_SIZE	= 0x20
};

enum {
	MLX4_EN_EQ_SIZE = 32
};


struct eq_debug {
    u32 cons_index_in_call;
    u32 cons_index_at_return;
};


struct mlx4_isr_entry {
	void (*isr)(void*);	/* isr */
	void *		ctx;		/* isr ctx */
	int 		cqn;		/* cq number */
};

#define MAX_ISR_ENTRIES 0x100

struct mlx4_eq {
	struct mlx4_dev	       *dev;
	void __iomem	       *doorbell;
	int			eqn;
	u32			cons_index;
	u16			irq;
	u16			have_irq;
	int			nent;
	int			load;
	struct mlx4_buf_list   *page_list;
	struct mlx4_mtt		mtt;
	// Windows
	KDPC		dpc;		/* DPC routine */
	spinlock_t	lock;		/* spinlock for simult DPCs */
	int			eq_ix;		/* EQ index */
	USHORT eq_no_progress;  /* used to look for stacked card */
	KAFFINITY	cpu;		/* CPU, this MSI-X vector is connected to */
	int			valid;

	/* CQ tree */
	spinlock_t		cq_lock;
	struct radix_tree_root	cq_tree;
	
    struct eq_debug debug[0x400];
    int dbg_inx;
    int no_of_empty_eq;
    uint64_t interrupt_time;
};

#pragma pack(push,1)
struct mlx4_eqe {
	u8			reserved1;
	u8			type;
	u8			reserved2;
	u8			subtype;
	union {
		u32		raw[6];
		struct {
			__be32	cqn;
		} __attribute__((packed)) comp;
		struct {
			u16	reserved1;
			__be16	token;
			u32	reserved2;
			u8	reserved3[3];
			u8	status;
			__be64	out_param;
		} __attribute__((packed)) cmd;
		struct {
			__be32	qpn;
		} __attribute__((packed)) qp;
		struct {
			__be32	srqn;
		} __attribute__((packed)) srq;
		struct {
			__be32	cqn;
			u32	reserved1;
			u8	reserved2[3];
			u8	syndrome;
		} __attribute__((packed)) cq_err;
		struct {
			u32	reserved1[2];
			__be32	port;
		} __attribute__((packed)) port_change;
        struct {
            #define COMM_CHANNEL_BIT_ARRAY_SIZE 4
            
            u32 reserved;
            u32 bit_vec[COMM_CHANNEL_BIT_ARRAY_SIZE];
        } __attribute__((packed)) comm_channel_arm;
		struct {
            u8  reserved[2];
            u8  vep_num;
            u8	port;
        } __attribute__((packed)) vep_config;
		struct {
            u8  port;
            u8  reserved[3];
            __be64  mac;
        } __attribute__((packed)) mac_update;
    }           event;
	u8			reserved3[3];
	u8			owner;
} __attribute__((packed));
#pragma pack(pop)

#pragma warning(disable:4505) //unreferenced local function
static inline void eq_set_ci(struct mlx4_eq *eq, int req_not)
{
    int dbg_inx = eq->dbg_inx & 0x3FF;
    
	__raw_writel((__force u32) cpu_to_be32((eq->cons_index & 0xffffff) |
					       req_not << 31),
		     eq->doorbell);
    eq->debug[dbg_inx].cons_index_at_return = eq->cons_index;
    if(req_not) 
        eq->dbg_inx++;
    /* We still want ordering, just not swabbing, so add a barrier */
	mb();
}

static struct mlx4_eqe *get_eqe(struct mlx4_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->nent - 1)) * MLX4_EQ_ENTRY_SIZE;
	return (struct mlx4_eqe *)(eq->page_list[off / PAGE_SIZE].buf + off % PAGE_SIZE);
}

static struct mlx4_eqe *next_eqe_sw(struct mlx4_eq *eq)
{
	struct mlx4_eqe *eqe = get_eqe(eq, eq->cons_index);
	return !!(eqe->owner & 0x80) ^ !!(eq->cons_index & eq->nent) ? NULL : eqe;
}

void mlx4_slave_event_all(struct mlx4_dev *dev, struct mlx4_eqe* eqe);

BOOLEAN mlx4_process_eqes( struct mlx4_eq *eq );


#endif /* MLX4_EQ_H */



