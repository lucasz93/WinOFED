/*
 * Copyright (c) 2013 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 */

#pragma warning( push, 3 )
#include <wtypes.h>

#include <stdlib.h>
#include <winioctl.h>
#include <malloc.h>
#pragma warning( pop )
#include "iba/ibat.h"
#include <iphlpapi.h>
#include "iba/ib_at_ioctl.h"

namespace IBAT
{

HRESULT
QueryIPaddrByGid (
    IN ib_gid_t         *pGid,
    IN HANDLE           heap,
    OUT SOCKADDR_INET   *pDestAddr )
{
    HRESULT hr;
    IOCTL_IBAT_IP_ADDRESSES_IN in;
    IOCTL_IBAT_IP_ADDRESSES_OUT *out;
    SIZE_T bytes = sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT) + (sizeof(SOCKADDR_INET) * 5);
    DWORD ret_bytes;

    in.Version = IBAT_IOCTL_VERSION;
    // [0 == all ports] otherwise a port GUID
    in.PortGuid = pGid->unicast.interface_id;

    do {
        out = (IOCTL_IBAT_IP_ADDRESSES_OUT *)
                    HeapAlloc( heap, HEAP_ZERO_MEMORY, bytes );
        if( !out )
            return HRESULT_FROM_WIN32( GetLastError() );

        out->Size = bytes;

        hr = IBAT::IoControl( IOCTL_IBAT_IP_ADDRESSES, in, out );
        if ( FAILED(hr) )
        {
            if( hr == ERROR_MORE_DATA )
            {
                HeapFree(heap, 0, (void*) out);
                bytes += sizeof(SOCKADDR_INET) * 3;
                continue;
            }
            HeapFree(heap, 0, (void*) out);
    	    return hr;
        }
    } while( FAILED(hr) );

    if (out->AddressCount > 0 )
    {
    	VOID *ipa;
	SIZE_T ipa_sz;

	if (out->Address[0].si_family == AF_INET )
	{
	    ipa = (void*) &out->Address[0].Ipv4;
	    ipa_sz = sizeof(SOCKADDR_IN);
	}
	else
	{
	    ipa = (void*) &out->Address[0].Ipv6;
	    ipa_sz = sizeof(SOCKADDR_IN6);
	}
  	RtlCopyMemory( (void*)pDestAddr, (void*)ipa, ipa_sz );
    }
    else
	hr = HRESULT_FROM_WIN32( ERROR_HOST_UNREACHABLE );

    HeapFree(heap, 0, (void*) out);

    return hr;
}

} // IBAT namespace


extern "C"
{

HRESULT
IbatQueryIPaddrByGid (
    IN  ib_gid_t	*pGid,
    OUT SOCKADDR_INET	*pDestAddr )
{
	extern HANDLE heap;

	return IBAT::QueryIPaddrByGid( pGid, heap, pDestAddr );
}

} // extern "C"

