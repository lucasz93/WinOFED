/*
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Copyright (c) 2006 Mellanox Technologies.  All rights reserved.
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

#include "ibspdll.h"

#ifdef _DEBUG_


void
DebugPrintIBSPIoctlParams(
					uint32_t						flags,
					DWORD						dwIoControlCode,
					LPVOID						lpvInBuffer,
					DWORD						cbInBuffer,
					LPVOID						lpvOutBuffer,
					DWORD						cbOutBuffer,
					LPWSAOVERLAPPED				lpOverlapped,
					LPWSAOVERLAPPED_COMPLETION_ROUTINE	lpCompletionRoutine,
					LPWSATHREADID				lpThreadId )
{
	UNUSED_PARAM( lpThreadId );

	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tdwIoControlCode :") );
	switch( dwIoControlCode )
	{
	case SIO_GET_QOS:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("SIO_GET_QOS\n") );
		break;
	case SIO_GET_GROUP_QOS:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("SIO_GET_GROUP_QOS\n") );
		break;
	case SIO_SET_QOS:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("SIO_SET_QOS\n") );
		break;
	case SIO_SET_GROUP_QOS:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("SIO_SET_GROUP_QOS\n") );
		break;
	case SIO_ADDRESS_LIST_QUERY:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("SIO_ADDRESS_LIST_QUERY\n") );
		break;
	default:
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("UNKNOWN control code 0x%x)\n", dwIoControlCode) );
		break;
	}

	if( lpvInBuffer == NULL )
	{
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tInput Buffer pointer is NULL\n") );
	}
	else
	{
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tInput buffer len (%d)\n", cbInBuffer) );
	}
	if( lpvOutBuffer == NULL )
	{
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tOutput Buffer pointer is NULL\n") );
	}
	else
	{
		IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tOutput buffer len (%d)\n", cbOutBuffer) );
	}
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,
		("\tOverlapped IO is (%s)\n", ( lpOverlapped == NULL) ? "FALSE" : "TRUE") );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,
		("\tCompletion Routine is %s\n",
		( lpCompletionRoutine == NULL) ? "NULL" : "non NULL") );
}


void
DebugPrintSockAddr(
					uint32_t					flags,
					struct sockaddr_in			*pSockAddr )
{

	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tAddressFamily (0x%x)\n", pSockAddr->sin_family) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tPortNumber (0x%x)\n", pSockAddr->sin_port) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags,("\tIPAddress (%s)\n", inet_ntoa(pSockAddr->sin_addr )) );
}


void
debug_dump_buffer(
					uint32_t					flags,
					const char					*name,
					void						*buf,
					size_t						len )
{
	unsigned char *p = buf;
	size_t i;
	char str[100];
	char *s;

	s = str;
	*s = 0;

	IBSP_PRINT( TRACE_LEVEL_VERBOSE, flags,("HEX for %s:\n", name) );

	for( i = 0; i < len; i++ )
	{
		s += sprintf( s, "%02x ", p[i] );
		if( i % 16 == 15 )
		{
			IBSP_PRINT( TRACE_LEVEL_VERBOSE, flags, ("HEX:%s: %s\n", name, str) );
			s = str;
			*s = 0;
		}
	}
	IBSP_PRINT( TRACE_LEVEL_VERBOSE, flags, ("HEX:%s: %s\n", name, str) );
}


void
debug_dump_overlapped(
					uint32_t					flags,
			const	char						*name,
					LPWSAOVERLAPPED				lpOverlapped )
{
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("dumping OVERLAPPED %s:\n", name) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  lpOverlapped = %p\n", lpOverlapped) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  Internal = %x\n", lpOverlapped->Internal) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  InternalHigh = %d\n", lpOverlapped->InternalHigh) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  Offset = %d\n", lpOverlapped->Offset) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  OffsetHigh = %d %\n", lpOverlapped->OffsetHigh) );
	IBSP_PRINT( TRACE_LEVEL_INFORMATION, flags, ("  hEvent = %x\n", (uintptr_t) lpOverlapped->hEvent) );
}

#endif /* _DEBUG_ */


#ifdef IBSP_LOGGING

VOID DataLogger_Init(
					DataLogger					*pLogger,
					char						*prefix,
					struct sockaddr_in			*addr1,
					struct sockaddr_in			*addr2 )
{
	HANDLE hFile;
	HANDLE hMapFile;

	char Name[100];
	DWORD DataSize = 20 * 1024 * 1024; 

	sprintf(Name,"c:\\%s_%d.%d.%d.%d_%d_%d.%d.%d.%d_%d", 
		prefix,
		addr1->sin_addr.S_un.S_un_b.s_b1,
		addr1->sin_addr.S_un.S_un_b.s_b2,
		addr1->sin_addr.S_un.S_un_b.s_b3,
		addr1->sin_addr.S_un.S_un_b.s_b4,
		CL_NTOH16(addr1->sin_port),
		addr2->sin_addr.S_un.S_un_b.s_b1,
		addr2->sin_addr.S_un.S_un_b.s_b2,
		addr2->sin_addr.S_un.S_un_b.s_b3,
		addr2->sin_addr.S_un.S_un_b.s_b4,
		CL_NTOH16(addr2->sin_port)
		);

	pLogger->NextPrint = NULL;
	pLogger->BufferStart = NULL;
	pLogger->ShutdownClosed = FALSE;
	pLogger->ToatalPrinted = 0;
	pLogger->TotalSize = DataSize;

	hFile = CreateFile( Name, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL );

	if (hFile == INVALID_HANDLE_VALUE)
	{
		IBSP_ERROR( ("CreateFile failed with error %d\n", GetLastError()) );
		return;
	}

	hMapFile = CreateFileMapping(hFile,    // current file handle 
		NULL,                              // default security 
		PAGE_READWRITE,                    // read/write permission 
		0,                                 // max. object size 
		DataSize,                           // size of hFile 
		NULL);            // name of mapping object

	CloseHandle( hFile );

	if (hMapFile == NULL) 
	{ 
		IBSP_ERROR( ("Could not create file mapping object.\n") );
		return;
	}

	pLogger->BufferStart = MapViewOfFile(hMapFile, // handle to mapping object 
		FILE_MAP_ALL_ACCESS,               // read/write permission 
		0,                                 // max. object size 
		0,                                 // size of hFile 
		0);                                // map entire file 

	CloseHandle( hMapFile );

	if( pLogger->BufferStart == NULL )
	{
		IBSP_ERROR( ("Could not MapViewOfFile.\n") );
		return;
	}

	pLogger->NextPrint = pLogger->BufferStart;
	cl_memclr(pLogger->NextPrint, DataSize);
}


VOID DataLogger_WriteData(
					DataLogger					*pLogger,
					long						Idx,
					char						*Data,
					DWORD						Len )
{
	char MessageHeader[16];
	CL_ASSERT(Len < 64000);
	CL_ASSERT(pLogger->ShutdownClosed == FALSE);
	CL_ASSERT(Len < pLogger->TotalSize / 3);

	if( !pLogger->BufferStart )
		return;

	cl_memset( MessageHeader, 0xff, sizeof(MessageHeader) );
	cl_memcpy( MessageHeader+4, &Idx, sizeof(Idx) );
	cl_memcpy( MessageHeader+8, &Len, sizeof(Len) );

	pLogger->ToatalPrinted += Len;

	if( pLogger->NextPrint + Len + (2 * sizeof (MessageHeader)) >
		pLogger->BufferStart + pLogger->TotalSize )
	{
		/* We will now zero the remaing of the buffer, and restart */
		cl_memclr( pLogger->NextPrint,
			pLogger->TotalSize - (pLogger->NextPrint - pLogger->BufferStart) );
		pLogger->NextPrint = pLogger->BufferStart;
	}

	/* Just simple copy */
	cl_memcpy( pLogger->NextPrint, MessageHeader, sizeof(MessageHeader) );
	pLogger->NextPrint += sizeof(MessageHeader);

	cl_memcpy( pLogger->NextPrint, Data, Len );
	pLogger->NextPrint += Len;

	/*
	 * Add the end marker but don't update NextPrint so the next message
	 * overwrites the previous message's end marker.
	 */
	cl_memset( pLogger->NextPrint, 0xff, sizeof(MessageHeader) );
}


VOID DataLogger_Shutdown(
					DataLogger					*pLogger )
{
	if( !pLogger->BufferStart )
		return;

	UnmapViewOfFile( pLogger->BufferStart );
}

#endif	/* IBSP_LOGGING */
