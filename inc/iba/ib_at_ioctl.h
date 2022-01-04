/*
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
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

/* This file is shared between user- and kernel-mode */


#ifndef	_IB_AT_IOCTL_H_
#define	_IB_AT_IOCTL_H_


#include <iba/ib_types.h>

#ifdef _WDMDDK_
#include <Netioapi.h>
#else
#include <ws2tcpip.h>
#endif

#define IBAT_IOCTL_VERSION      6

#define	IBAT_MAC_LEN			6


#define IOCTL_IBAT( n )		\
	CTL_CODE( FILE_DEVICE_UNKNOWN, (0x800 + n), \
		METHOD_BUFFERED, FILE_ANY_ACCESS )


typedef struct _IBAT_PORT_RECORD
{
	UINT64				CaGuid;
	UINT64				PortGuid;
	UINT16				PKey;
	UINT8				PortNum;

} IBAT_PORT_RECORD;


/** This IRP is used to return all the ip addresses that
 * are assigned to a port */
#define	IOCTL_IBAT_IP_ADDRESSES		IOCTL_IBAT( 2 )

typedef struct _IOCTL_IBAT_IP_ADDRESSES_IN
{
	ULONG				Version;
	/** The guid of the port that we are querying for.  May be
	 * zero if querying for IP addresses of all ports. */
	UINT64				PortGuid;

} IOCTL_IBAT_IP_ADDRESSES_IN;

typedef struct _IOCTL_IBAT_IP_ADDRESSES_OUT
{
	/** Total size of the output buffer needed if the
	 * suplied buffer wasn't enough */
	ULONG				Size;
    ULONG               AddressCount;
    SOCKADDR_INET       Address[1];

} IOCTL_IBAT_IP_ADDRESSES_OUT;



/** This IRP is used to get port record, corresponding to its (local) IP address */
#define	IOCTL_IBAT_IP_TO_PORT		IOCTL_IBAT( 4 )

typedef struct _IOCTL_IBAT_IP_TO_PORT_IN
{
	ULONG				Version;
    SOCKADDR_INET       Address;

} IOCTL_IBAT_IP_TO_PORT_IN;

typedef struct _IOCTL_IBAT_IP_TO_PORT_OUT
{
	IBAT_PORT_RECORD	Port;

} IOCTL_IBAT_IP_TO_PORT_OUT;



/** This IRP is used as a batch routine to do all path resolution in the kernel */
#define IOCTL_IBAT_QUERY_PATH IOCTL_IBAT( 6 )

typedef struct _IOCTL_IBAT_QUERY_PATH_IN
{
	ULONG				Version;
    SOCKADDR_INET           LocalAddress;
    SOCKADDR_INET           RemoteAddress;

} IOCTL_IBAT_QUERY_PATH_IN;

typedef ib_path_rec_t  IOCTL_IBAT_QUERY_PATH_OUT;

/** This IRP is used to remove a remote MAC addresses from our endpoint DB */
#define	IOCTL_IBAT_REMOVE_REMOTE_MAC IOCTL_IBAT( 8 )

typedef struct _IOCTL_IBAT_REMOVE_REMOTE_MAC_IN
{
	ULONG				Version;
	UINT64				PortGuid;
	UCHAR				DestMac[IBAT_MAC_LEN];

} IOCTL_IBAT_REMOVE_REMOTE_MAC_IN;

typedef struct _IOCTL_IBAT_REMOVE_REMOTE_MAC_OUT
{
	ULONG		Status;

} IOCTL_IBAT_REMOVE_REMOTE_MAC_OUT;



#define	IBAT_DEV_NAME	L"\\Device\\ibat"
#define	IBAT_DOS_DEV_NAME L"\\DosDevices\\Global\\ibat"
#define	IBAT_WIN32_NAME L"\\\\.\\ibat"

#endif	/* _IB_AT_IOCTL_H_ */
