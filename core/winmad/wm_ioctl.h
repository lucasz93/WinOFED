/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef _WM_IOCTL_H_
#define _WM_IOCTL_H_

typedef UINT16 NET16;
typedef UINT32 NET32;
typedef UINT64 NET64;

#define WM_IOCTL(f)	CTL_CODE(FILE_DEVICE_INFINIBAND, f, METHOD_BUFFERED,\
							 FILE_READ_DATA | FILE_WRITE_DATA)

// input parameter / output parameter
// IOCTL
#define WM_IO_FUNCTION_BASE				0x800

enum {
	WM_IO_FUNCTION_MIN,
	WM_IO_FUNCTION_REGISTER,
	WM_IO_FUNCTION_DEREGISTER,
	WM_IO_FUNCTION_CANCEL,
	WM_IO_FUNCTION_MAX
};

// WM_IO_REGISTER / UINT64 Id
#define WM_IOCTL_REGISTER				WM_IOCTL(WM_IO_FUNCTION_BASE + \
												 WM_IO_FUNCTION_REGISTER)

// UINT64 Id / none
#define WM_IOCTL_DEREGISTER				WM_IOCTL(WM_IO_FUNCTION_BASE + \
												 WM_IO_FUNCTION_DEREGISTER)

// none / none
#define WM_IOCTL_CANCEL					WM_IOCTL(WM_IO_FUNCTION_BASE + \
												 WM_IO_FUNCTION_CANCEL)

#define WM_IOCTL_MIN					WM_IO_FUNCTION_BASE + WM_IO_FUNCTION_MIN
#define WM_IOCTL_MAX					WM_IO_FUNCTION_BASE + WM_IO_FUNCTION_MAX

typedef struct _WM_IO_REGISTER
{
	NET64			Guid;
	NET32			Qpn;
	UINT8			Port;
	UINT8			Class;
	UINT8			Version;
	UINT8			Reserved[6];
	UINT8			Oui[3];
	UINT8			Methods[16];

}	WM_IO_REGISTER;

typedef struct _WM_IO_MAD_AV
{
	NET32			Qpn;
	NET32			Qkey;
	NET32			VersionClassFlow;	
	UINT16			PkeyIndex;
	UINT8			HopLimit;
	UINT8			GidIndex;
	UINT8			Gid[16];

	UINT16			Reserved;
	NET16			Lid;
	UINT8			ServiceLevel;
	UINT8			PathBits;
	UINT8			StaticRate;
	UINT8			GrhValid;

}	WM_IO_MAD_AV;

typedef struct _WM_IO_UMAD_AV
{
	NET32			Qpn;
	NET32			Qkey;
	UINT32			FlowLabel;	
	UINT16			PkeyIndex;
	UINT8			HopLimit;
	UINT8			GidIndex;
	UINT8			Gid[16];

	UINT8			TrafficClass;
	UINT8			Reserved;
	NET16			Lid;
	UINT8			ServiceLevel;
	UINT8			PathBits;
	UINT8			StaticRate;
	UINT8			GrhValid;

}	WM_IO_UMAD_AV;

#define WM_IO_SUCCESS 0
#define WM_IO_TIMEOUT 138

#pragma warning(push)
#pragma warning(disable: 4200)
typedef struct _WM_IO_MAD
{
	UINT64			Id;
	union {
		WM_IO_MAD_AV	Address;
		WM_IO_UMAD_AV	UmadAddress;
	};

	UINT32			Status;
	UINT32			Timeout;
	UINT32			Retries;
	UINT32			Length;

	UINT8			Data[0];

}	WM_IO_MAD;
#pragma warning(pop)

#endif // _WM_IOCTL_H_
