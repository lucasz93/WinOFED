/*
 * Copyright (c) 2013 Oce Printing Systems GmbH.  All rights reserved.
 *
 * This software is available to you under the BSD license below:
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
 *      - Neither the name Oce Printing Systems GmbH nor the names
 *        of the authors may be used to endorse or promote products
 *        derived from this software without specific prior written
 *        permission.
 *
 * THIS SOFTWARE IS PROVIDED  “AS IS” AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * OR CONTRIBUTOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE. 
 */

#if !defined(RWINSOCK_H)
#define RWINSOCK_H

#include <sys/types.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>

static const GUID rsProviderGuid = { //D478E78B-A803-4a25-A5E4-83BFB7EAF4A7
	0xd478e78b,
	0xa803,
	0x4a25,
	{ 0xa5, 0xe4, 0x83, 0xbf, 0xb7, 0xea, 0xf4, 0xa7 }
};

static WSAPROTOCOL_INFO rsProtocolInfo = {0};

/**
 * \brief			Get RSockets Winsock provider's WSAPROTOCOL_INFO structure.
 *
 * \param lpStatus	Pointer to status variable to be returned. Can be NULL if not required.
 *
 * \return			Pointer to the RSockets Winsock provider's WSAPROTOCOL_INFO structure
 *					(NULL if the RSockets provider is not found or another error occured).
 */
static LPWSAPROTOCOL_INFO rsGetProtocolInfo (LPINT lpStatus)
{
	int                Status			= ERROR_SUCCESS;
	LPWSAPROTOCOL_INFO lpProtocolBuffer	= NULL;
	LPWSAPROTOCOL_INFO lpReturn			= NULL; 
	DWORD              BufferLength		= 0;
	DWORD              i;

	WSAEnumProtocols (NULL, NULL, &BufferLength); // Should always return the BufferLength

	if (NULL == (lpProtocolBuffer = (LPWSAPROTOCOL_INFO)malloc (BufferLength)))
	{
		Status = ERROR_NOT_ENOUGH_MEMORY;
		goto cleanup;
	}

	if (SOCKET_ERROR == WSAEnumProtocols (NULL, lpProtocolBuffer, &BufferLength))
	{
		Status = WSAGetLastError();
		goto cleanup;
	}
	
	for (i = 0; i < BufferLength / sizeof(*lpProtocolBuffer); i++)
	{
		if (0 == memcmp (&lpProtocolBuffer[i].ProviderId, &rsProviderGuid, sizeof(rsProviderGuid)))
		{
			rsProtocolInfo	= lpProtocolBuffer[i];
			lpReturn		= &rsProtocolInfo;
			break;
		}
	}

	cleanup:
	if (lpProtocolBuffer)
		free (lpProtocolBuffer);
   
	if (lpStatus)
		*lpStatus = Status;

	return lpReturn;
}

#ifndef SOL_RDMA
#define SOL_RDMA 0x10000 // for getsockopt + setsockopt
enum {
	RDMA_SQSIZE,
	RDMA_RQSIZE,
	RDMA_INLINE,
	RDMA_IOMAPSIZE
};

enum {
	PROT_NONE  = 0x0, /* page can not be accessed */
	PROT_READ  = 0x1, /* page can be read */
	PROT_WRITE = 0x2, /* page can be written */
	PROT_EXEC  = 0x4  /* page can be executed */
};
#endif /* SOL_RDMA */

typedef struct {
	char szLine[128];
} RS_TRACE_OUT, *LPRS_TRACE_OUT;

/*
 * IOCTL code definition to get RS_STATUS information via WSAIoctl():
 */
#define IOC_VENDOR_OFA		0x0FA0000
#define SIO_RS_GET_TRACE	(IOC_OUT | IOC_VENDOR | IOC_VENDOR_OFA | 2)
#define SIO_RS_IO_MAP		(IOC_OUT | IOC_VENDOR | IOC_VENDOR_OFA | 3)
#define SIO_RS_IO_UNMAP		(IOC_OUT | IOC_VENDOR | IOC_VENDOR_OFA | 4)
#define SIO_RS_IO_WRITE		(IOC_OUT | IOC_VENDOR | IOC_VENDOR_OFA | 5)

typedef struct {
	void  *buf;
	size_t len;
	int    prot;
	int    flags;
	off_t  offset;
} RS_IO_MAP, *LPRS_IO_MAP;

typedef struct {
	void  *buf;
	size_t len;
} RS_IO_UNMAP, *LPRS_IO_UNMAP;

typedef struct {
	void  *buf;
	size_t count;
	off_t  offset;
	int    flags;
} RS_IO_WRITE, *LPRS_IO_WRITE;

static __inline off_t rsIoMap (SOCKET s, void *buf, size_t len, int prot, int flags, off_t offset)
{
	RS_IO_MAP InBuf;
	off_t     OutBuf;
	off_t     ret;
	DWORD     dwBytesReturned = 0;
	
	InBuf.buf    = buf;
	InBuf.len    = len;
	InBuf.prot   = prot;
	InBuf.flags  = flags;
	InBuf.offset = offset;
	
	ret = WSAIoctl(
			s,
			SIO_RS_IO_MAP,
			&InBuf,  sizeof(InBuf),
			&OutBuf, sizeof(OutBuf),
			&dwBytesReturned,
			NULL, NULL
	);
	if (ret == 0)
		ret = OutBuf;

	return ret;
}

static __inline int rsIoUnmap(SOCKET s, void *buf, size_t len)
{
	RS_IO_UNMAP InBuf;
	DWORD       dwBytesReturned = 0;
	
	InBuf.buf = buf;
	InBuf.len = len;
	
	return (int)WSAIoctl(
			s,
			SIO_RS_IO_UNMAP,
			&InBuf, sizeof(InBuf),
			NULL, 0,
			&dwBytesReturned,
			NULL, NULL
		);
}

static __inline size_t rsIoWrite(SOCKET s, const void *buf, size_t count, off_t offset, int flags)
{
	RS_IO_WRITE InBuf;
	size_t		OutBuf;
	size_t		ret;
	DWORD       dwBytesReturned = 0;
	
	InBuf.buf    = (void *)buf;
	InBuf.count  = count;
	InBuf.offset = offset;
	InBuf.flags  = flags;
	
	ret = WSAIoctl(
			s,
			SIO_RS_IO_WRITE,
			&InBuf,  sizeof(InBuf),
			&OutBuf, sizeof(OutBuf),
			&dwBytesReturned,
			NULL, NULL
	);
	if (ret == 0)
		ret = OutBuf;
	
	return ret;
}

#endif /* RWINSOCK_H */
