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
 
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <Psapi.h>

#include <rdma/rwinsock.h>
#include <rdma/rsocksvc.h>
#include "getopt.c"

static BOOL bShowProcess	= FALSE;
static BOOL bShowProcessId	= FALSE;
static BOOL bShowNumeric	= FALSE;
static BOOL bShowDescriptor	= FALSE;

static BOOL EnablePrivilege (HANDLE hToken, LPCTSTR szPrivName, BOOL fEnable)
{
	TOKEN_PRIVILEGES tp;

	ZeroMemory(&tp, sizeof(tp));
	tp.PrivilegeCount = 1;

	if (FALSE == LookupPrivilegeValue(NULL, szPrivName, &tp.Privileges[0].Luid)) {
		return FALSE;
	}

	tp.Privileges[0].Attributes = fEnable ? SE_PRIVILEGE_ENABLED : 0;

	return (   AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)
			&& GetLastError() == ERROR_SUCCESS
	);
}

static void PrintConnection (PRS_NETSTAT_ENTRY pEntry)
{
	int  ret;
	char szSrcAddr[NI_MAXHOST];
	char szSrcPort[NI_MAXSERV];
	char szDstAddr[NI_MAXHOST];
	char szDstPort[NI_MAXSERV];
	char szProcess[MAX_PATH];
	char szProcessId [16];
	char szDescriptor[16];

	struct sockaddr_in* psrc = (struct sockaddr_in *)&pEntry->saSrc;
	struct sockaddr_in* pdst = (struct sockaddr_in *)&pEntry->saDst;

	*szSrcAddr    = '\0';
	*szSrcPort    = '\0';
	*szDstAddr    = '\0';
	*szDstPort    = '\0';
	*szProcess    = '\0';
	*szProcessId  = '\0';
	*szDescriptor = '\0';

	if (getnameinfo(
			&pEntry->saSrc,	sizeof(&pEntry->saSrc),
			szSrcAddr,		sizeof(szSrcAddr),
			szSrcPort,		sizeof(szSrcPort),
			bShowNumeric ? (NI_NUMERICHOST | NI_NUMERICSERV) : 0
		)) {
		_snprintf(szSrcAddr, sizeof(szSrcAddr)-1, "%d.%d.%d.%d",
			psrc->sin_addr.S_un.S_un_b.s_b1,
			psrc->sin_addr.S_un.S_un_b.s_b2,
			psrc->sin_addr.S_un.S_un_b.s_b3,
			psrc->sin_addr.S_un.S_un_b.s_b4
		);
		_snprintf(szSrcPort, sizeof(szSrcPort)-1, "%d", ntohs(psrc->sin_port));
	}

	strcat_s(szSrcAddr, sizeof(szSrcAddr), ":");
	strcat_s(szSrcAddr, sizeof(szSrcAddr), szSrcPort);
	
	if (getnameinfo(
			&pEntry->saDst,	sizeof(&pEntry->saDst),
			szDstAddr,		sizeof(szDstAddr),
			szDstPort,		sizeof(szDstPort),
			bShowNumeric ? (NI_NUMERICHOST | NI_NUMERICSERV) : 0
		)) {
		_snprintf(szDstAddr, sizeof(szDstAddr)-1, "%d.%d.%d.%d",
			pdst->sin_addr.S_un.S_un_b.s_b1,
			pdst->sin_addr.S_un.S_un_b.s_b2,
			pdst->sin_addr.S_un.S_un_b.s_b3,
			pdst->sin_addr.S_un.S_un_b.s_b4
		);
		_snprintf(szDstPort, sizeof(szDstPort)-1, "%d", ntohs(pdst->sin_port));
	}

	strcat_s(szDstAddr, sizeof(szDstAddr), ":");
	strcat_s(szDstAddr, sizeof(szDstAddr), szDstPort);

	if (bShowProcess) {
		HANDLE hProcess = OpenProcess(
							  READ_CONTROL
							| PROCESS_QUERY_INFORMATION
							| PROCESS_VM_READ,
							FALSE,
							pEntry->dwProcessId
						);
		if (hProcess) {
			GetModuleFileNameExA(hProcess, NULL, szProcess, sizeof(szProcess)-1);
			CloseHandle(hProcess);
		} else {
			_snprintf(szProcess,   sizeof(szProcess  )-1, "       "); // Length of "Process"
			_snprintf(szProcessId, sizeof(szProcessId)-1, "%d", pEntry->dwProcessId); // Use PID instead
		}
	}

	if (bShowProcessId && *szProcessId == '\0') {
		_snprintf(szProcessId, sizeof(szProcessId)-1, "%d", pEntry->dwProcessId);
	}

	if (bShowDescriptor) {
		_snprintf(szDescriptor, sizeof(szDescriptor)-1, "%d (%d)", pEntry->s, pEntry->rs);
	}

	printf("TCP   %-21s %-21s %-15s %s %-5s %s\n",
		szSrcAddr, szDstAddr, pEntry->szState, szProcess, szProcessId, szDescriptor
	);
}

int __cdecl main (int argc, char **argv)
{
	int i, op, ret;
	HANDLE					hToken		  = NULL;
	HANDLE					hMapFile	  = NULL;
	RS_NETSTAT_ENTRY*		pNetstat	  = NULL;
	int				  		 NetstatCount = 0;
	MEMORY_BASIC_INFORMATION MemInfo;
	SIZE_T					 MemInfoSize;

	while ((op = getopt(argc, argv, "n?b?o?d?")) != -1) {
		switch (op) {
		case 'n':
			bShowNumeric = TRUE;
			break;
		case 'b':
			bShowProcess = TRUE;
			break;
		case 'o':
			bShowProcessId = TRUE;
			break;
		case 'd':
			bShowDescriptor = TRUE;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t-n Show addresses and ports in numeric format\n");
			printf("\t-b Show executable file of each connection\n");
			printf("\t-o Show process ID of each connection\n");
			printf("\t-d Show Socket and RSocket descriptors of each connection\n");
			exit(1);
		}
	}

	if (bShowProcess) {
		if (FALSE == OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken)) {
			printf("OpenProcessToken(TOKEN_ADJUST_PRIVILEGES) failed! (error=%d)", GetLastError());
		}

		if (FALSE == EnablePrivilege(hToken, SE_DEBUG_NAME, TRUE)) {
			; // Don't treat as error, we just can't get the rsocket process filenames.
			  // printf("WARNING: Failed to enable DEBUG Privilege! (error=%d)\n", GetLastError());
		}
	}

	hMapFile = OpenFileMapping(
					  FILE_MAP_READ
					| FILE_MAP_WRITE,	//__in DWORD   dwDesiredAccess
					FALSE,				//__in BOOL     bInheritHandle
					rsNetstatMapping	//__in LPCTSTR lpName
				);
	if (hMapFile == NULL) {
		ret = GetLastError();
		printf("Failed to open FileMapping \"%s\" (error=%d)!\n", rsNetstatMapping, ret);
		printf("Make sure that the %s \"%s\" is running.\n", SZSERVICEDISPLAYNAME, SZSERVICENAME);
		goto out;
	}
	
	pNetstat = (PRS_NETSTAT_ENTRY) MapViewOfFile(
										hMapFile,			// handle to map object
										  FILE_MAP_READ
										| FILE_MAP_WRITE,	// permission
										0, 0, 0
									);
	if (pNetstat == NULL) {
		ret = GetLastError();
		printf("Failed to map view of file \"%s\" (error=%d)!\n", rsNetstatMapping, ret);
		goto out;
	}

	MemInfoSize = VirtualQuery(
						pNetstat,
						&MemInfo,
						sizeof(MemInfo)
					);
	if (0 < MemInfoSize && MemInfoSize <= sizeof(MemInfo)) {
		NetstatCount = (int) ( MemInfo.RegionSize / sizeof(*pNetstat) );
	} else {
		printf("Failed to determine size of FileMapping \"%s\" (MemInfoSize=%d)!\n",
			rsNetstatMapping,
			MemInfoSize
		);

		goto out;
	}
		
	printf("Active RSocket connections:\n\n");
	printf("Proto Local Address         Remote Address        State           %s %-5s %s\n",
		bShowProcess	? "Process"		: "",
		bShowProcessId	? "PID"			: "",
		bShowDescriptor	? "skt (rskt)"	: ""
	);
	
	for (i = 0; i < NetstatCount; i++) {
		if (pNetstat[i].s != INVALID_SOCKET) {
			PrintConnection(&pNetstat[i]);
		}
	}
	
out:
	if (pNetstat) {
		if (!UnmapViewOfFile(pNetstat)) {
			printf("Failed to unmap Filemapping \"%s\" (error=%d)!\n", rsNetstatMapping, GetLastError());
		}
	}

	if (hMapFile) {
		if (!CloseHandle(hMapFile)) {
			printf("Failed to close Filemapping \"%s\" (error=%d)!\n", rsNetstatMapping, GetLastError());
		}
	}

	if (hToken) {
		if (!CloseHandle(hToken)) {
			printf("Failed to close ProcessToken (error=%d)!\n", GetLastError());
		}
	}
		
	return ret;
}
