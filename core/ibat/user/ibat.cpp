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

//
// IBAT: InfiniBand Address Translation
//
// Description:
//  Maps source & remote IP addresses (IPv4 and IPv6) to a path record.
//
//  The mapping requires two steps:
//      1. Mapping the remote IP address to the remote Ethernet MAC address
//      2. Retrieve the path given the remote Ethernet MAC address from IPoIB
//
//  The first step is accomplished as follows on Windows Server 2008:
//      1. Lookup the desired MAC from the OS using GetIpNetEntry2
//      2. If the remote IP isn't found, resolve the remote IP address
//      using ResolveIpNetEntry2
//
//  The first step is accomplished as follows on Windows Server 2003:
//      1. Retrieve the whole IP->MAC mapping table from the OS using
//      GetIpNetTable.
//      2. Walk the returned table looking for the destination IP to
//      find the destination Ethernet MAC address.
//      3. If the remote IP isn't found, resolve the remote IP address using
//      SendARP.
//
//  The second step is accomplished by asking IPoIB for the path
//  given the remote MAC.  IPoIB creates the path internally without going to
//  the SA.

#pragma warning( push, 3 )
#include <wtypes.h>

#include <stdlib.h>
#include <winioctl.h>
#pragma warning( pop )
#include "iba/ibat.h"
#include <iphlpapi.h>
#include "iba/ib_at_ioctl.h"

C_ASSERT( sizeof(IBAT_PATH_BLOB) == sizeof(ib_path_rec_t) );

namespace IBAT
{

//
// Summary:
//  Utility method to send IoControl messages to IBAT
//
HRESULT
IoControl(
  __in         DWORD dwIoControlCode,
  __in_opt     LPVOID lpInBuffer,
  __in         DWORD nInBufferSize,
  __out_opt    LPVOID lpOutBuffer,
  __in         DWORD nOutBufferSize,
  __out_opt    LPDWORD lpBytesReturned
  )
{
    HRESULT hr = S_OK;
    HANDLE hIbat;
    BOOL fSuccess;

    hIbat = ::CreateFileW(
        IBAT_WIN32_NAME,
        MAXIMUM_ALLOWED,(FILE_SHARE_READ|FILE_SHARE_WRITE),
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    if( hIbat == INVALID_HANDLE_VALUE )
    {
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    fSuccess  = ::DeviceIoControl(
        hIbat,
        dwIoControlCode,
        lpInBuffer,
        nInBufferSize,
        lpOutBuffer,
        nOutBufferSize,
        lpBytesReturned,
        NULL
        );
    if( FALSE == fSuccess )
    {
        hr = HRESULT_FROM_WIN32( ::GetLastError() );
    }
    ::CloseHandle(hIbat);
    return hr;
}


HRESULT
QueryPath(
    __in const struct sockaddr* pSrcAddr,
    __in const struct sockaddr* pDestAddr,
    __out IBAT_PATH_BLOB* pPath
    )
{
    IOCTL_IBAT_QUERY_PATH_IN queryIn = {};
    queryIn.Version = IBAT_IOCTL_VERSION;

    if( AF_INET == pSrcAddr->sa_family )
    {
        queryIn.LocalAddress.Ipv4 = *reinterpret_cast<const sockaddr_in*>(pSrcAddr);
    }
    else
    {
        queryIn.LocalAddress.Ipv6 = *reinterpret_cast<const sockaddr_in6*>(pSrcAddr);
    }

    if( AF_INET == pDestAddr->sa_family )
    {
        queryIn.RemoteAddress.Ipv4 = *reinterpret_cast<const sockaddr_in*>(pDestAddr);
    }
    else
    {
        queryIn.RemoteAddress.Ipv6 = *reinterpret_cast<const sockaddr_in6*>(pDestAddr);
    }

    return IBAT::IoControl(
                    IOCTL_IBAT_QUERY_PATH,
                    queryIn,
                    pPath
                    );
}


} /* IBAT namespace */

extern "C"
{

HRESULT
IbatQueryPath(
    __in const struct sockaddr* pSrcAddr,
    __in const struct sockaddr* pDestAddr,
    __out IBAT_PATH_BLOB* pPath
    )
{
    return IBAT::QueryPath(pSrcAddr, pDestAddr, pPath);
}

} /* extern "C" */
