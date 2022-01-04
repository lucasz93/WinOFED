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
 *
 */


///////////////////////////////////////////////////////////////////////////////
//
// IbatRouter
//
// The IbatRouter stores the routes for a given source port.  A source port
// represents a single NDIS NIC, and has a single NET_LUID value.  The router
// instance stores destination MAC address information.  All local IP to router
// conversion is performed via IPHelper using the NET_LUID value.
//
// Defined as a struct to allow a handle to be defined such that C-code can use it.
//
struct IbatRouter
{
private:
    KSPIN_LOCK m_lock;
    union
    {
        RTL_AVL_TABLE m_table;
        ib_path_rec_t m_path;
    };
    UINT64 m_portGuid;
    UINT16 m_pkey;
    BOOLEAN m_isRoCE;

    static RTL_AVL_COMPARE_ROUTINE RouteCompare;
    static RTL_AVL_ALLOCATE_ROUTINE RouteAlloc;
    static RTL_AVL_FREE_ROUTINE RouteFree;

public:
    static IbatRouter* Create( UINT64 portGuid, UINT16 pkey, BOOLEAN roce );

    NTSTATUS Update(
        __in UINT64 destMac,
        __in const ib_gid_t* pDestGid
        );

    VOID Reset();

    NTSTATUS Resolve(
        __in UINT64 destMac,
        __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
        __in VOID* completionContext,
        __out ib_path_rec_t* pPath
        );

    NTSTATUS CancelResolve(
        __in UINT64 destMac,
        __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
        __in VOID* completionContext
        );
};
