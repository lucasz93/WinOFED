/*
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
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

#ifndef MTHCA_WQE_H
#define MTHCA_WQE_H

enum {
	MTHCA_RD_DOORBELL	= 0x00,
	MTHCA_SEND_DOORBELL	= 0x10,
	MTHCA_RECV_DOORBELL	= 0x18,
	MTHCA_CQ_DOORBELL	= 0x20,
	MTHCA_EQ_DOORBELL	= 0x28
};

enum {
	MTHCA_NEXT_DBD			= 1 << 7,
	MTHCA_NEXT_FENCE		= 1 << 6,
	MTHCA_NEXT_CQ_UPDATE	= 1 << 3,
	MTHCA_NEXT_EVENT_GEN	= 1 << 2,
	MTHCA_NEXT_SOLICIT		= 1 << 1,
	MTHCA_NEXT_IP_CSUM		= 1 << 4,
	MTHCA_NEXT_TCP_UDP_CSUM = 1 << 5,

	MTHCA_MLX_VL15			= 1 << 17,
	MTHCA_MLX_SLR			= 1 << 16
};

enum {
	MTHCA_INLINE_SEG = 1 << 31
};

enum {
	MTHCA_INVAL_LKEY = 0x100,
		MTHCA_TAVOR_MAX_WQES_PER_RECV_DB = 256,
		MTHCA_ARBEL_MAX_WQES_PER_SEND_DB = 255
};

struct mthca_next_seg {
	ib_net32_t nda_op;		/* [31:6] next WQE [4:0] next opcode */
	ib_net32_t ee_nds;		/* [31:8] next EE  [7] DBD [6] F [5:0] next WQE size */
	ib_net32_t flags;		/* [3] CQ [2] Event [1] Solicit */
	ib_net32_t imm;		/* immediate data */
};

struct mthca_tavor_ud_seg {
	uint32_t    reserved1;
	ib_net32_t lkey;
	ib_net64_t av_addr;
	uint32_t    reserved2[4];
	ib_net32_t dqpn;
	ib_net32_t qkey;
	uint32_t    reserved3[2];
};

struct mthca_arbel_ud_seg {
	ib_net32_t av[8];
	ib_net32_t dqpn;
	ib_net32_t qkey;
	uint32_t    reserved[2];
};

struct mthca_bind_seg {
	ib_net32_t flags;		/* [31] Atomic [30] rem write [29] rem read */
	uint32_t    reserved;
	ib_net32_t new_rkey;
	ib_net32_t lkey;
	ib_net64_t addr;
	ib_net64_t length;
};

struct mthca_raddr_seg {
	ib_net64_t raddr;
	ib_net32_t rkey;
	uint32_t    reserved;
};

struct mthca_atomic_seg {
	ib_net64_t swap_add;
	ib_net64_t compare;
};

struct mthca_data_seg {
	ib_net32_t byte_count;
	ib_net32_t lkey;
	ib_net64_t addr;
};

struct mthca_mlx_seg {
	ib_net32_t nda_op;
	ib_net32_t nds;
	ib_net32_t flags;		/* [17] VL15 [16] SLR [14:12] static rate
				   [11:8] SL [3] C [2] E */
	ib_net16_t rlid;
	ib_net16_t vcrc;
};

struct mthca_inline_seg {
	uint32_t	byte_count;
};


static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}

#endif /* MTHCA_WQE_H */

