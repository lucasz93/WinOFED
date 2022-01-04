/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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
#include <iba/ib_types.h>
#include <iba/ib_uvp.h>

// taken from ib_defs.h
typedef uint32_t IB_wqpn_t;  /* Work QP number: Only 24 LSbits */
typedef uint8_t  IB_port_t;
typedef uint8_t IB_gid_t[16]; /* GID (aka IPv6) H-to-L (big) (network) endianess */
typedef uint32_t IB_ts_t;

typedef struct _ib_ca
{
	struct ibv_context *ibv_ctx;
} mlnx_ual_hobul_t;


typedef struct _ib_pd
{
	struct ibv_pd *ibv_pd;
	mlnx_ual_hobul_t	*p_hobul;
} mlnx_ual_pd_info_t;

