/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
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

#ifndef IBSPDLL_H
#define IBSPDLL_H


#ifdef __GNUC__
#include <stdint.h>
#endif

#include <windows.h>
#include <stdlib.h>
#include <ws2spi.h>
#include <ws2san.h>
#include <devioctl.h>

#include <iba/ib_al.h>
#include <iba/ib_at_ioctl.h>
#include <complib/cl_timer.h>

#include "ibspdefines.h"
#include "ibspdebug.h"
#include "ibspstruct.h"
#include "ibspproto.h"
#include "ibsp_mem.h"

/*--------------------------------------------------------------------------*/

extern struct ibspdll_globals g_ibsp;

extern uint32_t		g_max_inline;
extern uint32_t		g_max_poll;
extern uint32_t		g_sa_timeout;
extern uint32_t		g_sa_retries;
extern DWORD_PTR	g_dwPollThreadAffinityMask;

/* Allow users to control SA timeouts behavior - fall back on IPoIB or fail. */
extern int			g_connect_err;
extern uint8_t		g_max_cm_retries;
extern int8_t		g_pkt_life_modifier;
extern uint8_t		g_qp_retries;
extern uint8_t		g_use_APM;


#endif /* IBSPDLL_H */
