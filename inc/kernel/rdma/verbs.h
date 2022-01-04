/*
 * Copyright (c) 1996-2008 Intel Corporation. All rights reserved.
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

#ifndef _VERBS_H_
#define _VERBS_H_

#include <iba/ib_ci.h>
#include <event_trace.h>

static inline USHORT VerbsVersion(UINT8 Major, UINT8 Minor)
{
	return ((USHORT) Major << 8) | ((USHORT) Minor);
}

static inline UINT8 VerbsVersionMajor(USHORT Version)
{
	return (UINT8) (Version >> 8);
}

static inline UINT8 VerbsVersionMinor(USHORT Version)
{
	return (UINT8) Version;
}

#define RDMA_INTERFACE_VERBS_VERSION VerbsVersion(VERBS_MAJOR_VER, VERBS_MINOR_VER)

typedef struct _RDMA_INTERFACE_VERBS
{
	INTERFACE		InterfaceHeader;
	ci_interface_t	Verbs;
	ET_POST_EVENT	post_event;

}	RDMA_INTERFACE_VERBS;

//
// Interface, intended for notifications
//

typedef VOID (*MLX4_NOTIFY) (PVOID ifc_ctx, ULONG type, PVOID p_data, PCHAR str);

typedef struct _MLX4_BUS_NOTIFY_INTERFACE{
	INTERFACE i;
	MLX4_NOTIFY				notify;
	
} MLX4_BUS_NOTIFY_INTERFACE, *PMLX4_BUS_NOTIFY_INTERFACE;

#endif // _VERBS_H_

#ifdef DEFINE_GUID
DEFINE_GUID(GUID_RDMA_INTERFACE_VERBS, 0xf0ebae86, 0xedb5, 0x4b40,
			0xa1, 0xa, 0x44, 0xd5, 0xdb, 0x3b, 0x96, 0x4e);

// {A027188D-564D-4d4e-825A-6AEC19774BAB}
DEFINE_GUID(MLX4_BUS_NOTIFY_GUID, 
0xa027188d, 0x564d, 0x4d4e, 0x82, 0x5a, 0x6a, 0xec, 0x19, 0x77, 0x4b, 0xab);

#endif // DEFINE_GUID
