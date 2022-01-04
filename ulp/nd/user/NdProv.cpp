/*
 * Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#include <tchar.h>
#include <ndspi.h>
#include <iba/ib_at_ioctl.h>
#include <complib/cl_types.h>
#include <complib/cl_ioctl.h>
#pragma warning( push, 3 )
#include <unknwn.h>
#include <assert.h>
#include <ws2tcpip.h>
#include <winioctl.h>
#include <limits.h>
#include <ws2spi.h>
#pragma warning( pop )
#include "ndprov.h"
#include "ndadapter.h"
#include <process.h>

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdProv.tmh"
#endif

#include "nddebug.h"

uint32_t g_nd_dbg_level = TRACE_LEVEL_ERROR;
/* WPP doesn't want here literals! */
uint32_t g_nd_dbg_flags = 0x80000001; /* ND_DBG_ERROR | ND_DBG_NDI; */
uint32_t g_nd_max_inline_size = 160;

HANDLE ghHeap;

HMODULE g_hNtDll = NULL;
NtDeviceIoControlFile_t g_NtDeviceIoControlFile = NULL;

namespace NetworkDirect
{

    static LONG gnRef = 0;

    CProvider::CProvider() :
        m_nRef( 1 )
    {
        InterlockedIncrement( &gnRef );
    }

    CProvider::~CProvider()
    {
        InterlockedDecrement( &gnRef );
    }

    HRESULT CProvider::QueryInterface(
        const IID &riid,
        void **ppObject )
    {
        if( IsEqualIID( riid, IID_IUnknown ) )
        {
            *ppObject = this;
            return S_OK;
        }

        if( IsEqualIID( riid, IID_INDProvider ) )
        {
            *ppObject = this;
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG CProvider::AddRef()
    {
        return InterlockedIncrement( &m_nRef );
    }

    ULONG CProvider::Release()
    {
        ULONG ref = InterlockedDecrement( &m_nRef );
        if( ref == 0 )
            delete this;

        return ref;
    }

    HRESULT CProvider::QueryAddressList(
            __out_bcount_part_opt(*pBufferSize, *pBufferSize) SOCKET_ADDRESS_LIST* pAddressList,
            __inout SIZE_T* pBufferSize )
    {
        SOCKADDR_INET* pInetAddrList;

        ND_ENTER( ND_DBG_NDI );

        HANDLE hIbatDev = CreateFileW( IBAT_WIN32_NAME,
            MAXIMUM_ALLOWED, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        if( hIbatDev == INVALID_HANDLE_VALUE )
            return ND_NO_MEMORY;

        IOCTL_IBAT_IP_ADDRESSES_IN addrIn;
        addrIn.Version = IBAT_IOCTL_VERSION;
        addrIn.PortGuid = 0;

        DWORD size = sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT);
        IOCTL_IBAT_IP_ADDRESSES_OUT *pAddrOut;
        do
        {
            pAddrOut = (IOCTL_IBAT_IP_ADDRESSES_OUT*)HeapAlloc(
                GetProcessHeap(),
                0,
                size );
            if( !pAddrOut )
            {
                //AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
                //    ("Failed to allocate output buffer.\n") );
                return ND_NO_MEMORY;
        }

            if( !DeviceIoControl( hIbatDev, IOCTL_IBAT_IP_ADDRESSES,
                &addrIn, sizeof(addrIn), pAddrOut, size, &size, NULL ) )
            {
                HeapFree( GetProcessHeap(), 0, pAddrOut );
                //AL_PRINT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
                //    ("IOCTL_IBAT_IP_ADDRESSES failed (%x).\n", GetLastError()) );
                return ND_UNSUCCESSFUL;
            }

            if( pAddrOut->Size > size )
            {
                size = pAddrOut->Size;
                HeapFree( GetProcessHeap(), 0, pAddrOut );
                pAddrOut = NULL;
        }

        } while( !pAddrOut );

        CloseHandle( hIbatDev );

        //
        // Note: the required size computed is a few bytes larger than necessary,
        // but that keeps the code clean.
        //
        SIZE_T size_req;

        if( pAddrOut->AddressCount > 0 )
        {
            //
            // size of header + ((sizeof(element) + sizeof(addr)) * count)
            //
            size_req = (sizeof(*pAddressList) - sizeof(pAddressList->Address)) +
                          (pAddrOut->AddressCount * (sizeof(pAddressList->Address[0]) + sizeof(*pInetAddrList) ));
        }
        else
        {
            size_req = sizeof(*pAddressList);
        }

        if( size_req > *pBufferSize )
        {
            HeapFree( GetProcessHeap(), 0, pAddrOut );
            *pBufferSize = size_req;
            return ND_BUFFER_OVERFLOW;
        }

        RtlZeroMemory( pAddressList, size_req );

        /* We store the array of addresses after the last address pointer:
        *      iAddressCount
        *      Address[0]; <-- points to sockaddr[0]
        *      Address[1]; <-- points to sockaddr[1]
        *      ...
        *      Address[n-1]; <-- points to sockaddr[n-1]
        *      sockaddr[0];
        *      sockaddr[1];
        *      ...
        *      sockaddr[n-1]
        */
        pInetAddrList = reinterpret_cast<SOCKADDR_INET*>(
            &(pAddressList->Address[pAddrOut->AddressCount])
            );
        *pBufferSize = size_req;

        for( ULONG i = 0; i < pAddrOut->AddressCount; i++ )
        {
            pAddressList->Address[i].lpSockaddr =
                reinterpret_cast<LPSOCKADDR>(&pInetAddrList[i]);
            if( pAddrOut->Address[i].si_family == AF_INET )
            {
                pAddressList->Address[i].iSockaddrLength = sizeof(pInetAddrList[i].Ipv4);
            }
            else
            {
                pAddressList->Address[i].iSockaddrLength = sizeof(pInetAddrList[i].Ipv6);
        }

            pInetAddrList[i] = pAddrOut->Address[i];
        }
        pAddressList->iAddressCount = min( pAddrOut->AddressCount, LONG_MAX );

        HeapFree( GetProcessHeap(), 0, pAddrOut );

        return S_OK;
    }

    HRESULT CProvider::OpenAdapter(
            __in_bcount(AddressLength) const struct sockaddr* pAddress,
            __in SIZE_T AddressLength,
            __deref_out INDAdapter** ppAdapter )
    {
        ND_ENTER( ND_DBG_NDI );

        if( AddressLength < sizeof(struct sockaddr) )
            return ND_INVALID_ADDRESS;

        IOCTL_IBAT_IP_TO_PORT_IN in;
        in.Version = IBAT_IOCTL_VERSION;

        switch( pAddress->sa_family )
        {
        case AF_INET:
            if( AddressLength < sizeof(in.Address.Ipv4) )
                return ND_INVALID_ADDRESS;

            RtlCopyMemory( &in.Address.Ipv4, pAddress, sizeof(in.Address.Ipv4) );
            break;

        case AF_INET6:
            if( AddressLength < sizeof(in.Address.Ipv6) )
                return ND_INVALID_ADDRESS;

            RtlCopyMemory( &in.Address.Ipv6, pAddress, sizeof(in.Address.Ipv6) );
            break;

        default:
            return ND_INVALID_ADDRESS;
        }

        HANDLE hIbatDev = CreateFileW( IBAT_WIN32_NAME,
            MAXIMUM_ALLOWED, 0, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        if( hIbatDev == INVALID_HANDLE_VALUE )
            return ND_NO_MEMORY;

        IBAT_PORT_RECORD out;
        DWORD size;
        BOOL fSuccess = DeviceIoControl( hIbatDev, IOCTL_IBAT_IP_TO_PORT,
            &in, sizeof(in), &out, sizeof(out), &size, NULL );

        CloseHandle( hIbatDev );
        if( !fSuccess || size == 0 )
            return ND_INVALID_ADDRESS;

        return CAdapter::Create( this, pAddress, &out, ppAdapter );
    }

    CClassFactory::CClassFactory(void) :
        m_nRef( 1 )
    {
        InterlockedIncrement( &gnRef );
    }

    CClassFactory::~CClassFactory(void)
    {
        InterlockedDecrement( &gnRef );
    }

    HRESULT CClassFactory::QueryInterface(
        REFIID riid,
        void** ppObject )
    {
        if( IsEqualIID( riid, IID_IUnknown ) )
        {
            *ppObject = this;
            return S_OK;
        }
        if( IsEqualIID( riid, IID_IClassFactory ) )
        {
            *ppObject = this;
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG CClassFactory::AddRef()
    {
        return InterlockedIncrement( &m_nRef );
    }

    ULONG CClassFactory::Release()
    {
        ULONG ref = InterlockedDecrement( &m_nRef );
        if( ref == 0 )
            delete this;

        return ref;
    }

    HRESULT CClassFactory::CreateInstance(
        IUnknown* pUnkOuter,
        REFIID riid,
        void** ppObject )
    {
        if( pUnkOuter != NULL )
            return CLASS_E_NOAGGREGATION;

        if( IsEqualIID( riid, IID_INDProvider ) )
        {
            *ppObject = new CProvider();
            if( !*ppObject )
                return E_OUTOFMEMORY;

            return S_OK;
        }

        return E_NOINTERFACE;
    }

    HRESULT CClassFactory::LockServer( BOOL fLock )
    { 
        UNREFERENCED_PARAMETER( fLock );
        return S_OK;
    }

} // namespace

void* __cdecl operator new(
    size_t count
    )
{
    return HeapAlloc( ghHeap, 0, count );
}


void __cdecl operator delete(
    void* object
    )
{
    HeapFree( ghHeap, 0, object );
}

extern "C" {
STDAPI DllGetClassObject(
    REFCLSID rclsid,
    REFIID riid,
    LPVOID * ppv
    )
{
    ND_ENTER( ND_DBG_NDI );

    UNREFERENCED_PARAMETER( rclsid );

    if( IsEqualIID( riid, IID_IClassFactory ) )
    {
        NetworkDirect::CClassFactory* pFactory = new NetworkDirect::CClassFactory();
        if( pFactory == NULL )
            return E_OUTOFMEMORY;

        *ppv = pFactory;
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDAPI DllCanUnloadNow(void)
{
    ND_ENTER( ND_DBG_NDI );

    if( InterlockedCompareExchange( &NetworkDirect::gnRef, 0, 0 ) != 0 )
        return S_FALSE;

    return S_OK;
}

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

static BOOL
_DllMain(
    IN                HINSTANCE                    hinstDll,
    IN                DWORD                        dwReason,
    IN                LPVOID                        lpvReserved )
{

    ND_ENTER( ND_DBG_NDI );

    UNUSED_PARAM( hinstDll );
    UNUSED_PARAM( lpvReserved );

    switch( dwReason )
    {
    case DLL_PROCESS_ATTACH:
        TCHAR    env_var[16];
        DWORD    i;


#if defined(EVENT_TRACING)
#if DBG
        WPP_INIT_TRACING(L"ibndprov.dll");
#else
        WPP_INIT_TRACING(L"ibndprov.dll");
#endif
#elif DBG 
        i = GetEnvironmentVariable( "IBNDPROV_DBG_LEVEL", env_var, sizeof(env_var) );
        if( i && i <= 16 )
        {
            g_nd_dbg_level = _tcstoul( env_var, NULL, 16 );
        }

        i = GetEnvironmentVariable( "IBNDPROV_DBG_FLAGS", env_var, sizeof(env_var) );
        if( i && i <= 16 )
        {
            g_nd_dbg_flags = _tcstoul( env_var, NULL, 16 );
        }

        if( g_nd_dbg_flags & ND_DBG_ERR )
            g_nd_dbg_flags |= CL_DBG_ERROR;

        ND_PRINT( TRACE_LEVEL_ERROR, ND_DBG_ERR ,
            ("(pcs %#x) IbNdProv: Debug print: level:%d, flags 0x%x\n",
            GetCurrentProcessId(), g_nd_dbg_level ,g_nd_dbg_flags) );
#endif

		i = GetEnvironmentVariable( "IBNDPROV_MAX_INLINE_SIZE", env_var, sizeof(env_var) );
		if( i && i <= 16 )
		{
			g_nd_max_inline_size = _tcstoul( env_var, NULL, 16 );
		}

        ghHeap = HeapCreate( 0, 0, 0 );
        if( ghHeap == NULL )
        {
            ND_PRINT_EXIT(
                TRACE_LEVEL_ERROR, ND_DBG_NDI, ("Failed to allocate private heap.\n") );
#if defined(EVENT_TRACING)
            WPP_CLEANUP();
#endif
            return FALSE;
        }

		if ( g_hNtDll == NULL )
		{
			g_hNtDll = LoadLibraryA( "ntdll.dll" );
			if( g_hNtDll == NULL )
			{
				ND_PRINT_EXIT(
					TRACE_LEVEL_ERROR, ND_DBG_NDI, ("Failed to load NTDLL.DLL (%#x).\n", GetLastError()) );
#if defined(EVENT_TRACING)
				WPP_CLEANUP();
#endif
				HeapDestroy( ghHeap );
				return FALSE;
			}

			g_NtDeviceIoControlFile =
				(NtDeviceIoControlFile_t)GetProcAddress( g_hNtDll, "NtDeviceIoControlFile" );
			if( g_NtDeviceIoControlFile == NULL ) 
			{
				ND_PRINT_EXIT(
					TRACE_LEVEL_ERROR, ND_DBG_NDI, ("Failed to GetProcAddress for NtDeviceIoControlFile (%#x).\n", GetLastError()) );
#if defined(EVENT_TRACING)
				WPP_CLEANUP();
#endif
				FreeLibrary( g_hNtDll );
				g_hNtDll= NULL;
				HeapDestroy( ghHeap );
				return FALSE;
			}
		}
		

        ND_PRINT(TRACE_LEVEL_INFORMATION, ND_DBG_NDI, ("DllMain: DLL_PROCESS_ATTACH\n") );
        break;

    case DLL_PROCESS_DETACH:
		if ( g_hNtDll )
		{
			FreeLibrary( g_hNtDll );
			g_hNtDll = NULL;
		}
        HeapDestroy( ghHeap );
        ND_PRINT(TRACE_LEVEL_INFORMATION, ND_DBG_NDI,
            ("DllMain: DLL_PROCESS_DETACH, ref count %d\n", NetworkDirect::gnRef) );

#if defined(EVENT_TRACING)
        WPP_CLEANUP();
#endif
        break;
    }

    ND_EXIT( ND_DBG_NDI );

    return TRUE;
}


BOOL APIENTRY
DllMain(
    IN                HINSTANCE                    h_module,
    IN                DWORD                        ul_reason_for_call, 
    IN                LPVOID                        lp_reserved )
{
	__security_init_cookie();
    switch( ul_reason_for_call )
    {
    case DLL_PROCESS_ATTACH:
        return _DllMain( h_module, ul_reason_for_call, lp_reserved );

    case DLL_THREAD_ATTACH:
        ND_PRINT(TRACE_LEVEL_INFORMATION, ND_DBG_NDI, ("DllMain: DLL_THREAD_ATTACH\n") );
        break;

    case DLL_THREAD_DETACH:
        ND_PRINT(TRACE_LEVEL_INFORMATION, ND_DBG_NDI, ("DllMain: DLL_THREAD_DETACH\n") );
        break;

    case DLL_PROCESS_DETACH:
        return _DllMain( h_module, ul_reason_for_call, lp_reserved );
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
    { 0x52CDAA00, 0x29D0, 0x46be, { 0x8f, 0xc6, 0xe5, 0x1d, 0x70, 0x75, 0xc3, 0x38 } };

    if (WSAStartup (MAKEWORD (2, 2), &wsd) != 0)
    {
    return;
    }

    if (LOBYTE (wsd.wVersion) != 2 || HIBYTE (wsd.wVersion) != 2)
    {
    WSACleanup ();
    return;
    }

    /* Setup the values in PROTOCOL_INFO */
    provider.dwServiceFlags1 = XP1_GUARANTEED_DELIVERY | 
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
    wcscpy_s( provider.szProtocol, WSAPROTOCOL_LEN+1, L"OpenFabrics Network Direct Provider");

    WSCInstallProvider64_32(
    &providerId, L"%SYSTEMROOT%\\system32\\ibndprov.dll", &provider, 1, &err_no );

    WSACleanup ();
#endif
}

}   // extern "C"

