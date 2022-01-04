/*
/*
 * Copyright (c) 2008-2009 Intel Corporation.  All rights reserved.
 * Copyright (c) 2012 Oce Printing Systems GmbH.  All rights reserved.
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
 
#include "openib_osd.h" 
#include "cma.h"
#include <rdma/rsocket.h>
#include <rdma/rwinsock.h>
#include <complib/cl_debug.h>
#include "../../../etc/user/gtod.c"

#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TRACE_INDEX 4096 // Has to be a power of 2

typedef struct {
	FILETIME  fTime;
	char*   pszFormat;
	ULONG64  qwArgs[4];
} RS_TRACE, *LPRS_TRACE;

/*
 * Globals used across files
 */
__declspec(thread) int WSAErrno = 0;

CRITICAL_SECTION    gCriticalSection;   // Critical section to protect startup/cleanup
WSPUPCALLTABLE      gMainUpCallTable;   // Winsock upcall table
WSAPROTOCOL_INFOW	gProtocolInfo;
BOOL				gDetached = FALSE;  // Indicates if process is detaching from DLL
fastlock_t			lock;
fastlock_t			mut;
HANDLE				heap;

/*
 * Globals local to this file
 */
static const WCHAR *Description    = L"OpenFabrics RSockets for InfiniBand";
static int           gStartupCount = 0;      // Global startup count (for every WSPStartup call)
static uint32_t		 iTrace        = 0;
static RS_TRACE		rsTrace[MAX_TRACE_INDEX] = {0};
static fastlock_t     TraceLock;

static HANDLE			 hMapFile      = NULL;
static PRS_NETSTAT_ENTRY pNetstat      = NULL;
static int				  NetstatCount = 0;

/*
 * Create trace entries at runtime
 */
void Trace (const char* fmt, ...)
{
	struct timeval	time;
	va_list			argptr;
	int				i;

	fastlock_acquire(&TraceLock);

	GetSystemTimeAsFileTime(&rsTrace[iTrace].fTime);
	rsTrace[iTrace].pszFormat = (char *)fmt;

	va_start(argptr, fmt);
	for (i = 0; i < sizeof(rsTrace[0].qwArgs) / sizeof(rsTrace[0].qwArgs[0]); i++)
		rsTrace[iTrace].qwArgs[i] = va_arg(argptr, uint64_t);
	va_end(argptr);

	iTrace = (iTrace + 1) & (MAX_TRACE_INDEX-1);

	fastlock_release(&TraceLock);
	
	return;
}

/**
 * \brief					Get current RSockets trace information for the calling process
 *							(by calling librdmacm.dll directly).
 *
 * \param lppTraceBuffer	Pointer to a buffer with an array of RS_TRACE information entries
 *							to be allocated and returned. The caller is responsible for
 *							deallocating that buffer via free() when it is no longer needed.
 *
 * \return					The number of RS_TRACE entries contained in the trace buffer
 *							returned by *lppTraceBuffer.
 */
static uint32_t rsGetTrace ( __out LPRS_TRACE_OUT *lppTraceBuffer )
{
	uint32_t i, e, count = 0;
	struct timeval tvTime;

	if (NULL ==   lppTraceBuffer ||
		NULL == (*lppTraceBuffer = (LPRS_TRACE_OUT)malloc(MAX_TRACE_INDEX * sizeof(**lppTraceBuffer)))
	   )
		return 0;

	memset( *lppTraceBuffer, 0, MAX_TRACE_INDEX * sizeof(**lppTraceBuffer) );
	
	fastlock_acquire(&TraceLock);
	for (
			i = 0, e = iTrace;
			i < MAX_TRACE_INDEX;
			i++, e = (e+1) & (MAX_TRACE_INDEX - 1)
		)
	{
		if (rsTrace[e].pszFormat)
		{
			FileTimeToTimeval(&rsTrace[e].fTime, &tvTime);
			sprintf_s(
				       (*lppTraceBuffer)[i].szLine,
				sizeof((*lppTraceBuffer)[0].szLine),
				"%010d:%010d %s",
				tvTime.tv_sec, tvTime.tv_usec,
				rsTrace[e].pszFormat,
				rsTrace[e].qwArgs[0], rsTrace[e].qwArgs[1], rsTrace[e].qwArgs[2], rsTrace[e].qwArgs[3]
			);
			cl_dbg_out("%s", (*lppTraceBuffer)[i].szLine);
			count++;
		}
	}
	fastlock_release(&TraceLock);
	
	return count;
}

RS_NETSTAT_ENTRY* rsNetstatEntryCreate (int rs, int *lpErrno)
{
	if (pNetstat) {
		for (int i = 0; i < NetstatCount; i++) {
			if (pNetstat[i].s == INVALID_SOCKET) {
				RS_NETSTAT_ENTRY* pEntry = &pNetstat[i];
	
				ZeroMemory(pEntry, sizeof(*pEntry));
				pEntry->s = (int) gMainUpCallTable.lpWPUCreateSocketHandle(
									gProtocolInfo.dwCatalogEntryId,
									rs,	// __in  DWORD_PTR dwContext
									lpErrno
								);
				if (INVALID_SOCKET == pEntry->s) {
					return NULL;
				}

				pEntry->rs = rs;
				pEntry->dwProcessId = GetCurrentProcessId();
				return pEntry;
			}
		}
	}

	return NULL;
}

RS_NETSTAT_ENTRY* rsNetstatEntryGet (int rs)
{
	if (pNetstat) {
		DWORD dwCurrentProcessId = GetCurrentProcessId();

		for (int i = 0; i < NetstatCount; i++) {
			if (   pNetstat[i].rs          == rs
				&& pNetstat[i].dwProcessId == dwCurrentProcessId)
				return &pNetstat[i];
		}
	}

	return NULL;
}

/*
 * SPI Function Implementation
 */

/* 
 * Function: WSPCleanup
 *
 * Description:
 *    Decrement the entry count. If equal to zero then we can prepare to have us
 *    unloaded so all resources should be freed
 */
int WSPAPI 
WSPCleanup(
        LPINT lpErrno  
        )
{
    int rc = SOCKET_ERROR;

    if ( gDetached ) {
        rc = NO_ERROR;
        goto cleanup;
    }

    //
    // Grab the DLL global critical section
    //
    EnterCriticalSection( &gCriticalSection );

    // Verify WSPStartup has been called
    if ( 0 == gStartupCount ) {
        *lpErrno = WSANOTINITIALISED;
        goto cleanup;
    }

    // Decrement the global entry count
    gStartupCount--;

TRACE("StartupCount decremented to %d", gStartupCount);

    if ( 0 == gStartupCount ) {
        // Free LSP structures if still present as well as call WSPCleanup
        //    for all providers this LSP loaded
		if (pNetstat) {
			DWORD dwCurrentProcessId = GetCurrentProcessId();

			for (int i = 0; i < NetstatCount; i++) {
				if (   pNetstat[i].s           != (int)INVALID_SOCKET
					&& pNetstat[i].dwProcessId == dwCurrentProcessId) {
					pNetstat[i].s = (int) INVALID_SOCKET; // Invalidate entries of current process
				}
			}
			
			UnmapViewOfFile(pNetstat);
			pNetstat     = NULL;
			NetstatCount = 0;
		}

		if (hMapFile) {
			CloseHandle(hMapFile);
			hMapFile = NULL;
		}
	}

    rc = NO_ERROR;

cleanup:
    LeaveCriticalSection( &gCriticalSection );

    return rc;
}

/*
 * Function: WSPSocket
 *
 * Description:
 *    This routine creates a socket. For an IFS LSP the lower provider's socket
 *    handle is returned to the uppler layer. When a socket is created, a socket
 *    context structure is created for the socket returned from the lower provider.
 *    This context is used if the socket is later connected to a proxied address.
 */
SOCKET WSPAPI 
WSPSocket(
        int                 af,
        int                 type,
        int                 protocol,
        LPWSAPROTOCOL_INFOW lpProtocolInfo,
        GROUP               g,
        DWORD               dwFlags,
        LPINT               lpErrno
        )
{
    WSAPROTOCOL_INFOW   InfoCopy = {0};
	int					rs = (int)INVALID_SOCKET;
    SOCKET              winSocket = INVALID_SOCKET,
                        sret = INVALID_SOCKET;
	RS_NETSTAT_ENTRY  *pNetstatEntry = NULL;
    int                 rc;

    WSAErrno = 0;

TRACE("af=%d  type=%d  protocol=%d  flags=0x%X", af, type, protocol, dwFlags);

	if (af != AF_INET) {
		*lpErrno = WSAEAFNOSUPPORT;
		return INVALID_SOCKET;
	}

	if (type != SOCK_STREAM) {
		*lpErrno = WSAEPROTOTYPE;
		return INVALID_SOCKET;
	}

	if (protocol != IPPROTO_TCP) {
		*lpErrno = WSAEPROTONOSUPPORT;
		return INVALID_SOCKET;
	}

	if (dwFlags != 0) {
		*lpErrno = WSAEINVAL;
		return INVALID_SOCKET;
	}

    //
    // Create the socket from the lower layer
    //
	if ( INVALID_SOCKET == (rs = rsocket(af, type, protocol)) ) {
        goto cleanup;
    }
	
	pNetstatEntry = rsNetstatEntryGet(rs);
	winSocket = pNetstatEntry	? pNetstatEntry->s
								: gMainUpCallTable.lpWPUCreateSocketHandle(
										gProtocolInfo.dwCatalogEntryId,
										rs,
										lpErrno
									);
	if (winSocket == INVALID_SOCKET) {
        goto cleanup;
	}

TRACE("  New socket handle = %d (%d)", winSocket, rs);
			
    return winSocket;

cleanup:

    // If an error occured close the socket if it was already created
    if (INVALID_SOCKET != rs) {
        rclose((int)rs);
	}

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

    return INVALID_SOCKET;
}

int WSPAPI
WSPBind(
	__in   SOCKET s,
	__in   const struct sockaddr* name,
	__in   int namelen,
	__out  LPINT lpErrno
)
{
	struct sockaddr_in * name_in  = (struct sockaddr_in *)name;
	struct sockaddr_in6* name_in6 = (struct sockaddr_in6*)name;

	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	ret = rbind((int)rs, name, namelen);

TRACE("Socket = %d:", s);
	if (AF_INET == name->sa_family) {
		TRACE("  Addr = %d.%d.%d.%d",
			name_in->sin_addr.S_un.S_un_b.s_b1,
			name_in->sin_addr.S_un.S_un_b.s_b2,
			name_in->sin_addr.S_un.S_un_b.s_b3,
			name_in->sin_addr.S_un.S_un_b.s_b4
		);
		TRACE("  Port = %d,   Returning %d", name_in->sin_port, ret);
	} else {
		TRACE("  Addr = %d:%d:%d:%d:%d:%d:%d:%d",
			name_in6->sin6_addr.u.Word[0],
			name_in6->sin6_addr.u.Word[1],
			name_in6->sin6_addr.u.Word[2],
			name_in6->sin6_addr.u.Word[3],
			name_in6->sin6_addr.u.Word[4],
			name_in6->sin6_addr.u.Word[5],
			name_in6->sin6_addr.u.Word[6],
			name_in6->sin6_addr.u.Word[7]
		);
		TRACE("  Port = %d ,  Returning %d", name_in6->sin6_port, ret);
	}

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPListen(
	__in   SOCKET s,
	__in   int backlog,
	__out  LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}
	
    WSAErrno = 0;
	ret = rlisten((int)rs, backlog);

TRACE("Socket = %d: Backlog=%d, Returning %d", s, backlog, ret);

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPConnect(
	__in   SOCKET s,
	__in   const struct sockaddr* name,
	__in   int namelen,
	__in   LPWSABUF lpCallerData,
	__out  LPWSABUF lpCalleeData,
	__in   LPQOS lpSQOS,
	__in   LPQOS lpGQOS,
	__out  LPINT lpErrno
)
{
	struct sockaddr_in * name_in  = (struct sockaddr_in *)name;
	struct sockaddr_in6* name_in6 = (struct sockaddr_in6*)name;

	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

	WSAErrno = 0;
	ret = rconnect((int)rs, name, namelen);
	if (lpCalleeData) {
		lpCalleeData->len = 0;
	}

TRACE("Socket = %d:", s);
	if (AF_INET == name->sa_family) {
		TRACE("  Addr = %d.%d.%d.%d",
			name_in->sin_addr.S_un.S_un_b.s_b1,
			name_in->sin_addr.S_un.S_un_b.s_b2,
			name_in->sin_addr.S_un.S_un_b.s_b3,
			name_in->sin_addr.S_un.S_un_b.s_b4
		);
		TRACE("  Port = %d,   Returning %d", name_in->sin_port, ret);
	} else {
		TRACE("  Addr = %d:%d:%d:%d:%d:%d:%d:%d",
			name_in6->sin6_addr.u.Word[0],
			name_in6->sin6_addr.u.Word[1],
			name_in6->sin6_addr.u.Word[2],
			name_in6->sin6_addr.u.Word[3],
			name_in6->sin6_addr.u.Word[4],
			name_in6->sin6_addr.u.Word[5],
			name_in6->sin6_addr.u.Word[6],
			name_in6->sin6_addr.u.Word[7]
		);
		TRACE("  Port = %d ,  Returning %d", name_in6->sin6_port, ret);
	}

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPShutdown(
	__in   SOCKET s,
	__in   int how,
	__out  LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

	WSAErrno = 0;
	ret = rshutdown((int)rs, how);

TRACE("Socket = %d: how=%d, Returning %d", s, how, ret);

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPCloseSocket(
	__in   SOCKET s,
	__out  LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

	WSAErrno = 0;
	ret = rclose((int)rs);
	if (SOCKET_ERROR == ret) {
        *lpErrno = WSAErrno;
		TRACE("rclose() failed with Errno %d!", *lpErrno);
	}
	
	ret = gMainUpCallTable.lpWPUCloseSocketHandle(s, lpErrno);

TRACE("Socket handle %s closed", s);

	return ret;
}

int WSPAPI
WSPRecv(
	__in    SOCKET								s,
	__inout LPWSABUF							lpBuffers,
	__in    DWORD								dwBufferCount,
	__out   LPDWORD								lpNumberOfBytesRecvd,
	__inout LPDWORD								lpFlags,
	__in    LPWSAOVERLAPPED						lpOverlapped,
	__in    LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	__in    LPWSATHREADID						lpThreadId,
	__out   LPINT								lpErrno
)
{
	DWORD_PTR rs  = INVALID_SOCKET;
	DWORD     i;
	DWORD     dwNumberOfBytesRecvd = 0;
	int       len = 0;
	int       ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	for (i = 0; i < dwBufferCount; i++) {
		len = (int)rrecv((int)rs, lpBuffers[i].buf, lpBuffers[i].len, *lpFlags);
		switch (len) {
		case  0:
			goto out;
		case -1:
			   ret = SOCKET_ERROR;
			goto out;
		default:
			dwNumberOfBytesRecvd += len;
		}
	}

out:
	*lpNumberOfBytesRecvd = dwNumberOfBytesRecvd;
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPSend(
	__in  SOCKET								s,
	__in  LPWSABUF								lpBuffers,
	__in  DWORD									dwBufferCount,
	__out LPDWORD								lpNumberOfBytesSent,
	__in  DWORD									dwFlags,
	__in  LPWSAOVERLAPPED						lpOverlapped,
	__in  LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
	__in  LPWSATHREADID							lpThreadId,
	__out LPINT									lpErrno
)
{
	DWORD_PTR rs  = INVALID_SOCKET;
	DWORD     i;
	DWORD     dwNumberOfBytesSent = 0;
	int       len;
	int       ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	for (i = 0; i < dwBufferCount; i++) {
		len = (int)rsend((int)rs, lpBuffers[i].buf, lpBuffers[i].len, dwFlags);
		if (-1 == len) {
		    ret = SOCKET_ERROR;
		    break;
		} else {
			dwNumberOfBytesSent += len;
		}
	}

	*lpNumberOfBytesSent = dwNumberOfBytesSent;
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPGetSockOpt(
	__in     SOCKET s,
	__in     int level,
	__in     int optname,
	__out    char* optval,
	__inout  LPINT optlen,
	__out    LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

	WSAErrno = 0;
	ret = rgetsockopt((int)rs, level, optname, optval, optlen);
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

    return ret;
}

int WSPAPI
WSPSetSockOpt(
	__in     SOCKET s,
	__in     int level,
	__in     int optname,
	__in     const char* optval,
	__in     int optlen,
	__out    LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	ret = rsetsockopt((int)rs, level, optname, optval, optlen);
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

    return ret;
}

int WSPAPI
WSPSelect(
	__in     int nfds,
	__inout  fd_set* readfds,
	__inout  fd_set* writefds,
	__inout  fd_set* exceptfds,
	__in     const struct timeval* timeout,
	__out    LPINT lpErrno
)
{
	u_int  i;
	int    ret;
	fd_set rreadfds, rwritefds, rexceptfds;
	
	FD_ZERO(&rreadfds);
	FD_ZERO(&rwritefds);
	FD_ZERO(&rexceptfds);
	
	nfds = 1;

	for (i = 0; readfds && i < readfds->fd_count; i++) {
		if (readfds->fd_array[i]) {
			ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(
						 readfds->fd_array[i],
						(PDWORD_PTR)&rreadfds.fd_array[i],
						lpErrno
					);
			if (SOCKET_ERROR == ret) {
				return ret;
			}
			
			if (rreadfds.fd_array[i] > (unsigned) nfds) {
				nfds = (int)rreadfds.fd_array[i];
			}

			rreadfds.fd_count++;
		}
	}

	for (i = 0; writefds && i < writefds->fd_count; i++) {
		if (writefds->fd_array[i]) {
			ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(
						 writefds->fd_array[i],
						(PDWORD_PTR)&rwritefds.fd_array[i],
						lpErrno
					);
			if (SOCKET_ERROR == ret) {
				return ret;
			}

			if (rwritefds.fd_array[i] > (unsigned) nfds) {
				nfds = (int)rwritefds.fd_array[i];
			}

			rwritefds.fd_count++;
		}
	}

	for (i = 0; exceptfds && i < exceptfds->fd_count; i++) {
		if (exceptfds->fd_array[i]) {
			ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(
						 exceptfds->fd_array[i],
						(PDWORD_PTR) &rexceptfds.fd_array[i],
						lpErrno
					);
			if (SOCKET_ERROR == ret) {
				return ret;
			}

			if (rexceptfds.fd_array[i] > (unsigned) nfds) {
				nfds = (int)rexceptfds.fd_array[i];
			}

			rexceptfds.fd_count++;
		}
	}

    WSAErrno = 0;
	ret = rselect(
			nfds + 1, // Max. valid rsocket descriptor + 1
			readfds   ? &rreadfds   : NULL,
			writefds  ? &rwritefds  : NULL,
			exceptfds ? &rexceptfds : NULL,
			(struct timeval*)timeout
		);
	if (SOCKET_ERROR == ret) {
        *lpErrno = WSAErrno;
		return ret;
	}

	nfds = ret;
	for (i = 0; ret && readfds && i < rreadfds.fd_count; i++) {
		if (rreadfds.fd_array[i] && readfds->fd_array[i]) {
			ret--;
		} else {
			readfds->fd_array[i] = 0;
			readfds->fd_count--;
		}
	}

	for (i = 0; ret && writefds && i < rwritefds.fd_count; i++) {
		if (rwritefds.fd_array[i] && writefds->fd_array[i]) {
			ret--;
		} else {
			writefds->fd_array[i] = 0;
			writefds->fd_count--;
		}
	}

	for (i = 0; ret && exceptfds && i < rexceptfds.fd_count; i++) {
		if (rexceptfds.fd_array[i] && exceptfds->fd_array[i]) {
			ret--;
		} else {
			exceptfds->fd_array[i] = 0;
			exceptfds->fd_count--;
		}
	}
	
	return (nfds - ret);
}

int WSPAPI
WSPIoctl(
	__in  SOCKET s,
	__in  DWORD dwIoControlCode,
	__in  LPVOID lpvInBuffer,
	__in  DWORD cbInBuffer,
	__out LPVOID lpvOutBuffer,
	__in  DWORD cbOutBuffer,
	__out LPDWORD lpcbBytesReturned,
	__in  LPWSAOVERLAPPED lpOverlapped,
	__in  LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	__in  LPWSATHREADID lpThreadId,
	__out LPINT lpErrno
)
{
	int       ret = 0;
	DWORD_PTR rs  = INVALID_SOCKET;
	DWORD     dwCount;
	LPVOID    lpResultBuffer = NULL;
	
	*lpcbBytesReturned = 0;
	
	ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);
	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	switch (dwIoControlCode) {
	case SIO_RS_IO_MAP:
		if (lpvInBuffer && lpvOutBuffer) {
		    LPRS_IO_MAP in  = (LPRS_IO_MAP)lpvInBuffer;
			off_t      *out = (off_t *)    lpvOutBuffer;
			
			*out = riomap((int)rs, in->buf, in->len, in->prot, in->flags, in->offset);
			if (-1 == *out) {
				ret = SOCKET_ERROR;
			}
		}
		break;
	case SIO_RS_IO_UNMAP:
		if (lpvInBuffer) {
		    LPRS_IO_UNMAP in = (LPRS_IO_UNMAP)lpvInBuffer;
			
			ret = riounmap((int)rs, in->buf, in->len);
		}
		break;
	case SIO_RS_IO_WRITE:
		if (lpvInBuffer && lpvOutBuffer) {
		    LPRS_IO_WRITE in  = (LPRS_IO_WRITE)lpvInBuffer;
			size_t       *out = (size_t *)     lpvOutBuffer;
			
			*out = riowrite((int)rs, in->buf, in->count, in->offset, in->flags);
			if (-1 == *out) {
				ret = SOCKET_ERROR;
			}
		}
		break;
	case SIO_RS_GET_TRACE:
		if (lpvOutBuffer) {
			dwCount = rsGetTrace((LPRS_TRACE_OUT *)&lpResultBuffer);
			if (lpResultBuffer) {
				if (cbOutBuffer >= dwCount * sizeof(RS_TRACE_OUT)) {
					*lpcbBytesReturned = dwCount * sizeof(RS_TRACE_OUT);
				} else {
					 ret				= SOCKET_ERROR;
					WSAErrno			= WSA_IO_INCOMPLETE;
					*lpcbBytesReturned	= cbOutBuffer; // Copy as much as possible anyway
				}
			}
		}
		break;
	case FIONBIO:
	case FIONREAD:
	case SIOCATMARK:
		if (lpvInBuffer && cbInBuffer >= sizeof(u_long)) {
			ret = rioctlsocket(
					(int)rs,
					dwIoControlCode,
					(u_long *)lpvInBuffer
				);
			break;
		}
	default:
		ret = SOCKET_ERROR;
		WSAErrno = WSAEINVAL;
	}

	if (lpResultBuffer)	{
		memcpy(lpvOutBuffer, lpResultBuffer, *lpcbBytesReturned);
		free(lpResultBuffer);
	}

    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPAddressToString(
  __in     LPSOCKADDR lpsaAddress,
  __in     DWORD dwAddressLength,
  __in     LPWSAPROTOCOL_INFOW lpProtocolInfo,
  __out    LPWSTR lpszAddressString,
  __inout  LPDWORD lpdwAddressStringLength,
  __out    LPINT lpErrno
)
{
	int ret = 0;
	
	if (lpProtocolInfo)	{
		if (0 != memcmp(&lpProtocolInfo->ProviderId, &rsProviderGuid, sizeof(rsProviderGuid))) {
			*lpErrno = WSAEINVALIDPROVIDER;
			return SOCKET_ERROR;
		}
	}
	if (SOCKET_ERROR == (ret = WSAAddressToStringW(
									lpsaAddress,
									dwAddressLength,
									NULL,
									lpszAddressString,
									lpdwAddressStringLength
								))) {
		*lpErrno = WSAGetLastError();
	}

	return ret;
}

int WSPAPI
WSPAsyncSelect(
  __in   SOCKET s,
  __in   HWND hWnd,
  __in   unsigned int wMsg,
  __in   long lEvent,
  __out  LPINT lpErrno
)
{
	*lpErrno = WSAEOPNOTSUPP;
	return SOCKET_ERROR;
}

int WSPAPI
WSPCancelBlockingCall(
  __out  LPINT lpErrno
)
{
	*lpErrno = WSAEOPNOTSUPP;
	return SOCKET_ERROR;
}

int WSPAPI
WSPDuplicateSocket(
    IN SOCKET  s,
    IN DWORD  dwProcessId,
    OUT LPWSAPROTOCOL_INFOW  lpProtocolInfo,
    OUT LPINT  lpErrno
    )
{
	*lpErrno = WSAEOPNOTSUPP;
	return SOCKET_ERROR;
}

int WSPAPI
WSPEnumNetworkEvents(
    IN SOCKET  s,
    IN WSAEVENT  hEventObject,
    OUT LPWSANETWORKEVENTS  lpNetworkEvents,
    OUT LPINT  lpErrno
    )
{
	*lpErrno = WSAEOPNOTSUPP;
	return SOCKET_ERROR;
}

int WSPAPI
WSPEventSelect(
  __in   SOCKET s,
  __in   WSAEVENT hEventObject,
  __in   long lNetworkEvents,
  __out  LPINT lpErrno
)
{
	*lpErrno = WSAEOPNOTSUPP;
	return SOCKET_ERROR;
}

BOOL WSPAPI
WSPGetOverlappedResult(
    IN SOCKET  s,
    IN LPWSAOVERLAPPED  lpOverlapped,
    OUT LPDWORD  lpcbTransfer,
    IN BOOL  fWait,
    OUT LPDWORD  lpdwFlags,
    OUT LPINT  lpErrno
    )
{
	*lpErrno = WSAEOPNOTSUPP;
	return FALSE;
}

int WSPAPI
WSPGetPeerName(
  __in     SOCKET s,
  __out    struct sockaddr* name,
  __inout  LPINT namelen,
  __out    LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	ret = rgetpeername((int)rs, name, namelen);
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

    return ret;
}

int WSPAPI
WSPGetSockName(
  __in     SOCKET s,
  __out    struct sockaddr* name,
  __inout  LPINT namelen,
  __out    LPINT lpErrno
)
{
	DWORD_PTR rs = INVALID_SOCKET;
	int      ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	ret = rgetsockname((int)rs, name, namelen);
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

    return ret;
}

BOOL WSPAPI
WSPGetQOSByName(
  __in     SOCKET s,
  __inout  LPWSABUF lpQOSName,
  __out    LPQOS lpQOS,
  __out    LPINT lpErrno
)
{
	*lpErrno = WSAEOPNOTSUPP;
	return FALSE;
}

SOCKET WSPAPI
WSPJoinLeaf(
  __in   SOCKET s,
  __in   const struct sockaddr* name,
  __in   int namelen,
  __in   LPWSABUF lpCallerData,
  __out  LPWSABUF lpCalleeData,
  __in   LPQOS lpSQOS,
  __in   LPQOS lpGQOS,
  __in   DWORD dwFlags,
  __out  LPINT lpErrno
)
{
	*lpErrno = WSAEOPNOTSUPP;
	return INVALID_SOCKET;
}

int WSPAPI
WSPRecvDisconnect(
  __in   SOCKET s,
  __out  LPWSABUF lpInboundDisconnectData,
  __out  LPINT lpErrno
)
{
	return WSPShutdown(s, SD_RECEIVE, lpErrno); // Ignore lpInboundDisconnectData
}

int WSPAPI
WSPRecvFrom(
  __in     SOCKET s,
  __inout  LPWSABUF lpBuffers,
  __in     DWORD dwBufferCount,
  __out    LPDWORD lpNumberOfBytesRecvd,
  __inout  LPDWORD lpFlags,
  __out    struct sockaddr* lpFrom,
  __inout  LPINT lpFromlen,
  __in     LPWSAOVERLAPPED lpOverlapped,
  __in     LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
  __in     LPWSATHREADID lpThreadId,
  __inout  LPINT lpErrno
)
{
	DWORD_PTR rs  = INVALID_SOCKET;
	DWORD     i;
	DWORD     dwNumberOfBytesRecvd = 0;
	int       len = 0;
	int       ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	for (i = 0; i < dwBufferCount; i++)	{
		switch (len = (int)rrecvfrom(
							(int)rs,
							lpBuffers[i].buf,
							lpBuffers[i].len,
							*lpFlags,
							lpFrom,
							lpFromlen
				)) {
		case  0:
			goto out;
		case -1:
			   ret = SOCKET_ERROR;
			goto out;
		default:
			dwNumberOfBytesRecvd += len;
		}
	}

out:
	*lpNumberOfBytesRecvd = dwNumberOfBytesRecvd;
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPSendDisconnect(
  __in   SOCKET s,
  __in   LPWSABUF lpOutboundDisconnectData,
  __out  LPINT lpErrno
)
{
	return WSPShutdown(s, SD_SEND, lpErrno); // Ignore lpOutboundDisconnectData
}

int WSPAPI
WSPSendTo(
  __in   SOCKET s,
  __in   LPWSABUF lpBuffers,
  __in   DWORD dwBufferCount,
  __out  LPDWORD lpNumberOfBytesSent,
  __in   DWORD dwFlags,
  __in   const struct sockaddr* lpTo,
  __in   int iTolen,
  __in   LPWSAOVERLAPPED lpOverlapped,
  __in   LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
  __in   LPWSATHREADID lpThreadId,
  __out  LPINT lpErrno
)
{
	DWORD_PTR rs  = INVALID_SOCKET;
	DWORD     i;
	DWORD     dwNumberOfBytesSent = 0;
	int       len;
	int       ret = gMainUpCallTable.lpWPUQuerySocketHandleContext(s, &rs, lpErrno);

	if (SOCKET_ERROR == ret) {
		return ret;
	}

    WSAErrno = 0;
	for (i = 0; i < dwBufferCount; i++)	{
		if (-1 == (len = (int)rsendto(
								(int)rs,
								lpBuffers[i].buf,
								lpBuffers[i].len,
								dwFlags,
								lpTo,
								iTolen
							)))	{
			   ret = SOCKET_ERROR;
			break;
		} else {
			dwNumberOfBytesSent += len;
		}
	}

	*lpNumberOfBytesSent = dwNumberOfBytesSent;
    if (WSAErrno) {
        *lpErrno = WSAErrno;
    }

	return ret;
}

int WSPAPI
WSPStringToAddress(
  __in     LPWSTR AddressString,
  __in     INT AddressFamily,
  __in     LPWSAPROTOCOL_INFOW lpProtocolInfo,
  __out    LPSOCKADDR lpAddress,
  __inout  LPINT lpAddressLength,
  __out    LPINT lpErrno
)
{
	int ret = 0;
	
	if (AF_INET != AddressFamily && AddressFamily != AF_INET6) {
		*lpErrno = WSAEAFNOSUPPORT;
		return SOCKET_ERROR;
	}
	
	if (lpProtocolInfo)	{
		if (0 != memcmp(&lpProtocolInfo->ProviderId, &rsProviderGuid, sizeof(rsProviderGuid))) {
			*lpErrno = WSAEINVALIDPROVIDER;
			return SOCKET_ERROR;
		}
	}
	
	if (SOCKET_ERROR == (ret = WSAStringToAddressW(
								AddressString,
								AddressFamily,
								NULL,
								lpAddress,
								lpAddressLength
			))) {
		*lpErrno = WSAGetLastError();
	}

	return ret;
}

/*
 * Function: WSPStartup
 *
 * Description:
 *    This function initializes the base provider. 
 */
#ifdef __cplusplus
extern "C" {
#endif

__control_entrypoint(DllExport)
__checkReturn
int
WSPAPI
WSPStartup(
    __in WORD wVersion,
    __in LPWSPDATA lpWSPData,
    __in LPWSAPROTOCOL_INFOW lpProtocolInfo,
    __in WSPUPCALLTABLE UpCallTable,
    __out LPWSPPROC_TABLE lpProcTable
    )
{
	static WSPDATA			 gWSPData;
	static WSPPROC_TABLE	 gProcTable;
	
    int						Error = NO_ERROR,
							rc;

TRACE("Requested version = %d.%d", LOBYTE(wVersion), HIBYTE(wVersion));

	/* Make sure that the version requested is >= 2.2. The low byte is the 
	   major version and the high byte is the minor version. */
	if( (LOBYTE(wVersion) < 2) || ((LOBYTE(wVersion) == 2) && (HIBYTE(wVersion) < 2)) ) {
		return WSAVERNOTSUPPORTED;
	}

    EnterCriticalSection( &gCriticalSection );

    // The first time the startup is called, create our heap and allocate some
    //    data structures for tracking the LSP providers
    if ( 0 == gStartupCount ) {
TRACE("Called 1st time => Initializing ProtocolInfo...");
		/* Save the global WSPData */
		gWSPData.wVersion = MAKEWORD(2, 2);
		gWSPData.wHighVersion = MAKEWORD(2, 2);
		wcscpy_s( gWSPData.szDescription, 2*sizeof(gWSPData.szDescription), Description );

		/* provide Service provider's entry points in proc table */
		ZeroMemory( &gProcTable, sizeof(gProcTable) );
		gProcTable.lpWSPSocket				= WSPSocket;
		gProcTable.lpWSPBind				= WSPBind;
		gProcTable.lpWSPListen				= WSPListen;
		gProcTable.lpWSPAccept				= WSPAccept;
		gProcTable.lpWSPConnect				= WSPConnect;
		gProcTable.lpWSPShutdown			= WSPShutdown;
		gProcTable.lpWSPCloseSocket			= WSPCloseSocket;
		gProcTable.lpWSPRecv				= WSPRecv;
		gProcTable.lpWSPSend				= WSPSend;
		gProcTable.lpWSPGetSockOpt			= WSPGetSockOpt;
		gProcTable.lpWSPSetSockOpt			= WSPSetSockOpt;
		gProcTable.lpWSPSelect				= WSPSelect;
		gProcTable.lpWSPIoctl				= WSPIoctl;
		gProcTable.lpWSPCleanup				= WSPCleanup;
// Additional functions required for (base provider's) WSPStartup:
		gProcTable.lpWSPAddressToString		= WSPAddressToString;
		gProcTable.lpWSPAsyncSelect			= WSPAsyncSelect;
		gProcTable.lpWSPCancelBlockingCall	= WSPCancelBlockingCall;
		gProcTable.lpWSPDuplicateSocket		= WSPDuplicateSocket;
		gProcTable.lpWSPEnumNetworkEvents	= WSPEnumNetworkEvents;
		gProcTable.lpWSPEventSelect			= WSPEventSelect;
		gProcTable.lpWSPGetOverlappedResult	= WSPGetOverlappedResult;
		gProcTable.lpWSPGetPeerName			= WSPGetPeerName;
		gProcTable.lpWSPGetSockName			= WSPGetSockName;
		gProcTable.lpWSPGetQOSByName		= WSPGetQOSByName;
		gProcTable.lpWSPJoinLeaf			= WSPJoinLeaf;
		gProcTable.lpWSPRecvDisconnect		= WSPRecvDisconnect;
		gProcTable.lpWSPRecvFrom			= WSPRecvFrom;
		gProcTable.lpWSPSendDisconnect		= WSPSendDisconnect;
		gProcTable.lpWSPSendTo				= WSPSendTo;
		gProcTable.lpWSPStringToAddress		= WSPStringToAddress;
	
		gProtocolInfo = *lpProtocolInfo;
		hMapFile      = OpenFileMapping(
							FILE_MAP_ALL_ACCESS,//__in DWORD   dwDesiredAccess
							FALSE,				//__in BOOL     bInheritHandle
							rsNetstatMapping	//__in LPCTSTR lpName
						);
		if (hMapFile) {
			pNetstat = (PRS_NETSTAT_ENTRY) MapViewOfFile(
												hMapFile,            // handle to map object
												FILE_MAP_ALL_ACCESS, // read/write permission
												0, 0, 0
											);
			if (pNetstat) {
				MEMORY_BASIC_INFORMATION MemInfo;
				SIZE_T	MemInfoSize = VirtualQuery(
										pNetstat,
										&MemInfo,
										sizeof(MemInfo)
									);

				if (0 < MemInfoSize && MemInfoSize <= sizeof(MemInfo)) {
					NetstatCount = (int) (MemInfo.RegionSize / sizeof(*pNetstat));
				} else {
					UnmapViewOfFile(pNetstat);
					pNetstat = NULL;
					CloseHandle(hMapFile);
					hMapFile = NULL;
				}
			} else {
				; // pNetstat == NULL: Don't treat as error
			}
		} else {
			; // hMapFile == NULL: Don't treat as error
		}
	}

    gStartupCount++;
TRACE("StartupCount incremented to %d", gStartupCount);

    LeaveCriticalSection( &gCriticalSection );

	/* Set the return parameters */
	*lpWSPData   = gWSPData;
	*lpProcTable = gProcTable;
	
	/* store the upcall function table */
    gMainUpCallTable = UpCallTable;

    return Error;
}

#ifdef __cplusplus
}
#endif

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(lpReserved);

	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		heap = HeapCreate(0, 0, 0);
		if (heap == NULL) {
			return FALSE;
		}

		fastlock_init(&lock);
		fastlock_init(&mut);
		fastlock_init(&TraceLock);

		//
		// Initialize some critical section objects 
		//
		__try {
			InitializeCriticalSection( &gCriticalSection );
		}
		__except( EXCEPTION_EXECUTE_HANDLER ) {
			goto cleanup;
		}

		break;
	case DLL_PROCESS_DETACH:
		gDetached = TRUE;
		
		//
		// In case the current process terminated without having called WSPCleanup(),
		// invalidate all related pNetstat entries + and unmap and close FileMapping:
		//
		if (pNetstat) {
			for (int i = 0; i < NetstatCount; i++) {
				if (   pNetstat[i].s           != (int)INVALID_SOCKET
					&& pNetstat[i].dwProcessId == GetCurrentProcessId() ) {
					pNetstat[i].s = (int) INVALID_SOCKET;
				}
			}
			
			UnmapViewOfFile(pNetstat);
			pNetstat     = NULL;
			NetstatCount = 0;
		}

		if (hMapFile) {
			CloseHandle(hMapFile);
			hMapFile = NULL;
		}

		DeleteCriticalSection( &gCriticalSection );
		fastlock_destroy(&mut);
		fastlock_destroy(&lock);
		fastlock_destroy(&TraceLock);
		HeapDestroy(heap);
		break;
	default:
		break;
	}

	return TRUE;

cleanup:
    return FALSE;
}
