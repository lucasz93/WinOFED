/*
 * Copyright (c) Microsoft Corporation.  All rights reserved.
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

#include "precomp.h"


HANDLE g_hHeap;
#if DBG
LONG g_nRef[ND_RESOURCE_TYPE_COUNT];
#else
LONG g_nRef[1];
#endif

extern "C"
{
typedef NTSTATUS (WINAPI *PFN_NtDeviceIoControlFile)(
    __in   HANDLE FileHandle,
    __in   HANDLE Event,
    __in   PIO_APC_ROUTINE ApcRoutine,
    __in   PVOID ApcContext,
    __out  PIO_STATUS_BLOCK IoStatusBlock,
    __in   ULONG IoControlCode,
    __in   PVOID InputBuffer,
    __in   ULONG InputBufferLength,
    __out  PVOID OutputBuffer,
    __in   ULONG OutputBufferLength
    );
}

static PFN_NtDeviceIoControlFile pfnNtDeviceIoControlFile;
static HANDLE g_hIoctlEvent;
static INIT_ONCE g_InitOnce;
static CRITICAL_SECTION g_CritSec;

BOOL CALLBACK InitIoctl(
    INIT_ONCE*,
    void*,
    void**
    )
{
    g_hIoctlEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    if( g_hIoctlEvent == NULL )
    {
        return FALSE;
    }

    //
    // Set the low bit of the event, so that if we issue an IOCTL on a file bound to
    // an I/O completion port, the completion gets reported via the event and not the
    // IOCP.
    //
    g_hIoctlEvent = reinterpret_cast<HANDLE>(
        reinterpret_cast<SIZE_T>(g_hIoctlEvent) | 1
        );

    HMODULE hNtDll = GetModuleHandleW( L"ntdll.dll" );
    if( hNtDll == NULL )
    {
        return FALSE;
    }
    pfnNtDeviceIoControlFile = reinterpret_cast<PFN_NtDeviceIoControlFile>(
        GetProcAddress( hNtDll, "NtDeviceIoControlFile" )
        );
    if( pfnNtDeviceIoControlFile == NULL )
    {
        return FALSE;
    }
    return TRUE;
}


HRESULT Ioctl(
    __in HANDLE hFile,
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput,
    __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
    __inout ULONG* pcbOutput
    )
{
    IO_STATUS_BLOCK iosb;
    EnterCriticalSection( &g_CritSec );

    NTSTATUS status = pfnNtDeviceIoControlFile(
        hFile,
        g_hIoctlEvent,
        NULL,
        NULL,
        &iosb,
        IoControlCode,
        pInput,
        cbInput,
        pOutput,
        *pcbOutput
        );
    if( status == STATUS_PENDING )
    {
        WaitForSingleObject( g_hIoctlEvent, INFINITE );
    }

    LeaveCriticalSection( &g_CritSec );

    *pcbOutput = static_cast<ULONG>(iosb.Information);
    return status;
}


HRESULT IoctlAsync(
    __in HANDLE hFile,
    __in ULONG IoControlCode,
    __in_bcount(cbInput) void* pInput,
    __in ULONG cbInput,
    __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
    __in ULONG cbOutput,
    __inout OVERLAPPED* pOverlapped
    )
{
    pOverlapped->Internal = STATUS_PENDING;
    NTSTATUS status = pfnNtDeviceIoControlFile(
        hFile,
        pOverlapped->hEvent,
        NULL,
        reinterpret_cast<ULONG_PTR>(pOverlapped->hEvent) & 1 ? NULL : pOverlapped,
        reinterpret_cast<IO_STATUS_BLOCK*>(&pOverlapped->Internal),
        IoControlCode,
        pInput,
        cbInput,
        pOutput,
        cbOutput
        );
    return status;
}


HRESULT ConvertIbStatus( __in ib_api_status_t status )
{
    switch( status )
    {
    case IB_SUCCESS:
        return STATUS_SUCCESS;
    case IB_INVALID_PARAMETER:
    case IB_INVALID_SETTING:
        return STATUS_INVALID_PARAMETER;
    case IB_INSUFFICIENT_MEMORY:
    case IB_INSUFFICIENT_RESOURCES:
        return STATUS_INSUFFICIENT_RESOURCES;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}


void* __cdecl operator new(
    size_t count
    )
{
    return HeapAlloc( g_hHeap, 0, count );
}


void __cdecl operator delete(
    void* object
    )
{
    HeapFree( g_hHeap, 0, object );
}


STDAPI DllGetClassObject(
    REFCLSID,
    REFIID riid,
    LPVOID * ppv
    )
{
    if( InitOnceExecuteOnce( &g_InitOnce, InitIoctl, NULL, NULL ) == FALSE )
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if( IsEqualIID( riid, IID_IClassFactory ) )
    {
        IClassFactory* pFactory = static_cast<IClassFactory*>( new ND::ClassFactory() );
        if( pFactory == NULL )
            return E_OUTOFMEMORY;

        *ppv = pFactory;
        return S_OK;
    }

    return ND::v2::Provider::Create( riid, ppv );
}


STDAPI DllCanUnloadNow(void)
{
    return g_nRef[NdProvider] != 0;
}


extern "C" {
int
WSPAPI
WSPStartup(
    IN WORD wVersionRequested,
    OUT LPWSPDATA lpWSPData,
    IN LPWSAPROTOCOL_INFOW lpProtocolInfo,
    IN WSPUPCALLTABLE UpcallTable,
    OUT LPWSPPROC_TABLE lpProcTable
    )
{
    UNREFERENCED_PARAMETER( wVersionRequested );
    UNREFERENCED_PARAMETER( lpWSPData );
    UNREFERENCED_PARAMETER( lpProtocolInfo );
    UNREFERENCED_PARAMETER( UpcallTable );
    UNREFERENCED_PARAMETER( lpProcTable );
    return WSASYSNOTREADY;
}


BOOL APIENTRY
DllMain(
    __in HINSTANCE,
    DWORD Reason,
    __in LPVOID
    )
{
    switch( Reason )
    {
    case DLL_PROCESS_ATTACH:
        __security_init_cookie();
        g_hHeap = HeapCreate( 0, 0, 0 );
        if( g_hHeap == NULL )
        {
            return FALSE;
        }

        RtlZeroMemory( g_nRef, sizeof(g_nRef) );
        g_hIoctlEvent = NULL;

        InitOnceInitialize( &g_InitOnce );
        InitializeCriticalSection( &g_CritSec );
        break;

    case DLL_PROCESS_DETACH:
        if( g_hIoctlEvent != NULL )
        {
            CloseHandle( g_hIoctlEvent );
        }
        DeleteCriticalSection( &g_CritSec );
        HeapDestroy( g_hHeap );
        break;
    }

    return TRUE;
}


/*
* Function: RegisterProviderW
*   Description: installs the service provider
*
* Note: most of the information setup here comes from "MSDN Home >
* MSDN Library > Windows Development > Network Devices and
* Protocols > Design Guide > System Area Networks > Windows Sockets
* Direct > Windows Sockets Direct Component Operation > Installing
* Windows Sockets Direct Components".
* The direct link is http://msdn.microsoft.com/library/default.asp?url=/library/en-us/network/hh/network/wsdp_2xrb.asp
*/
void CALLBACK RegisterProviderW(HWND, HINSTANCE, LPWSTR, int)
{
#ifndef _WIN64
    return;
#else

#ifndef PFL_NETWORKDIRECT_PROVIDER
#define PFL_NETWORKDIRECT_PROVIDER          0x00000010
#endif

    int err_no;
    WSAPROTOCOL_INFOW provider;
    WSADATA wsd;
    static GUID providerId = 
        { 0xc42da8c6, 0xd1a1, 0x4f78, { 0x9f, 0x52, 0x27, 0x6c, 0x74, 0x93, 0x55, 0x96 } };

    if( WSAStartup(MAKEWORD(2, 2), &wsd) != 0 )
    {
        return;
    }

    if( LOBYTE(wsd.wVersion) != 2 || HIBYTE(wsd.wVersion) != 2 )
    {
        WSACleanup();
        return;
    }

    /* Setup the values in PROTOCOL_INFO */
    provider.dwServiceFlags1 = 
        XP1_GUARANTEED_DELIVERY | 
        XP1_GUARANTEED_ORDER | 
        XP1_MESSAGE_ORIENTED |
        XP1_CONNECT_DATA;  /*XP1_GRACEFUL_CLOSE;*/
    provider.dwServiceFlags2 = 0;	/* Reserved */
    provider.dwServiceFlags3 = 0;	/* Reserved */
    provider.dwServiceFlags4 = 0;	/* Reserved */
    provider.dwProviderFlags = PFL_HIDDEN | PFL_NETWORKDIRECT_PROVIDER;
    provider.ProviderId = providerId;
    provider.dwCatalogEntryId = 0;
    provider.ProtocolChain.ChainLen = 1;	/* Base Protocol Service Provider */
    provider.iVersion = 1;
    provider.iAddressFamily = AF_INET;
    provider.iMaxSockAddr = sizeof(SOCKADDR_IN);
    provider.iMinSockAddr = 16;
    provider.iSocketType = -1;
    provider.iProtocol = 0;
    provider.iProtocolMaxOffset = 0;
    provider.iNetworkByteOrder = BIGENDIAN;
    provider.iSecurityScheme = SECURITY_PROTOCOL_NONE;
    provider.dwMessageSize = 0xFFFFFFFF; /* IB supports 32-bit lengths for data transfers on RC */
    provider.dwProviderReserved = 0;
    wcscpy_s( provider.szProtocol, _countof(provider.szProtocol), L"OpenFabrics Network Direct Provider for Mellanox ConnectX" );

    WSCInstallProvider64_32(
        &providerId, L"%SYSTEMROOT%\\system32\\mlx4nd.dll", &provider, 1, &err_no );

    WSACleanup ();
#endif
}

}   // extern "C"

