/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
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

#ifndef DOORBELL_H
#define DOORBELL_H

enum {
	MTHCA_SEND_DOORBELL_FENCE = 1 << 5
};
#if defined _WIN64

static inline void mthca_write64(uint32_t val[2], struct mthca_context *ctx, int offset)
{
	*(volatile uint64_t *) ((char *)ctx->uar + offset) = *(volatile  uint64_t*)val;
}

static inline void mthca_write_db_rec(uint32_t val[2], uint32_t *db)
{
	*(volatile uint64_t *) db = *(volatile  uint64_t*)val;
}


#elif defined(_WIN32)

static inline void mthca_write64(uint32_t val[2], struct mthca_context *ctx, int offset)
{
	volatile uint64_t *target_p = (volatile uint64_t*)((uint8_t*)ctx->uar + offset);

	cl_spinlock_acquire(&ctx->uar_lock);
	*(volatile uint32_t *) ((uint8_t*)ctx->uar + offset)     = val[0];
	*(volatile uint32_t *) ((uint8_t*)ctx->uar + offset + 4) = val[1];
	cl_spinlock_release(&ctx->uar_lock);

	//TODO: can we save mm0 and not to use emms, as Linux do ?
	//__asm movq mm0,val
	//__asm movq target_p,mm0
	//__asm emms
}
static inline void mthca_write_db_rec(uint32_t val[2], uint32_t *db)
{
	db[0] = val[0];
	wmb();
	db[1] = val[1];
}


#endif

#endif /* MTHCA_H */
