/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
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

#ifdef _WIN64

static inline void mlx4_write64(uint32_t val[2], struct mlx4_context *ctx, int offset)
{
	*(volatile uint64_t *) (ctx->uar + offset) = *(volatile uint64_t *)val;
}

#else

static inline void mlx4_write64(uint32_t val[2], struct mlx4_context *ctx, int offset)
{
	pthread_spin_lock(&ctx->uar_lock);
	*(volatile uint32_t *) (ctx->uar + offset)     = val[0];
	*(volatile uint32_t *) (ctx->uar + offset + 4) = val[1];
	pthread_spin_unlock(&ctx->uar_lock);
}

#endif

#endif /* DOORBELL_H */
