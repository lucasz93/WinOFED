// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// Net Direct Helper Interface
//

#pragma once

#ifndef _NETDIRECT_H_
#define _NETDIRECT_H_

#include <ndspi.h>

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

#define ND_HELPER_API  __stdcall


//
// Initialization
//
HRESULT ND_HELPER_API
NdStartup(
    VOID
    );

HRESULT ND_HELPER_API
NdCleanup(
    VOID
    );

VOID ND_HELPER_API
NdFlushProviders(
    VOID
    );

//
// Network capabilities
//
#define ND_QUERY_EXCLUDE_EMULATOR_ADDRESSES 0x00000001
#define ND_QUERY_EXCLUDE_NDv1_ADDRESSES     0x00000002
#define ND_QUERY_EXCLUDE_NDv2_ADDRESSES     0x00000004

HRESULT ND_HELPER_API
NdQueryAddressList(
    __in DWORD flags,
    __out_bcount_part_opt(*pcbAddressList, *pcbAddressList) SOCKET_ADDRESS_LIST* pAddressList,
    __inout ULONG* pcbAddressList
    );


HRESULT ND_HELPER_API
NdResolveAddress(
    __in_bcount(cbRemoteAddress) const struct sockaddr* pRemoteAddress,
    __in SIZE_T cbRemoteAddress,
    __out_bcount(*pcbLocalAddress) struct sockaddr* pLocalAddress,
    __inout SIZE_T* pcbLocalAddress
    );


HRESULT ND_HELPER_API
NdCheckAddress(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    __in SIZE_T cbAddress
    );


HRESULT ND_HELPER_API
NdOpenAdapter(
    __in REFIID iid,
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    __in SIZE_T cbAddress,
    __deref_out VOID** ppIAdapter
    );


HRESULT ND_HELPER_API
NdOpenV1Adapter(
    __in_bcount(cbAddress) const struct sockaddr* pAddress,
    __in SIZE_T cbAddress,
    __deref_out INDAdapter** ppIAdapter
    );

#ifdef __cplusplus
}
#endif  // __cplusplus


#endif // _NETDIRECT_H_
