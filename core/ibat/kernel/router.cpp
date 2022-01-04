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


#include "ibatp.h"
#include "route.h"
#include "router.h"


//
// Structure to store a route in a router's AVL tree.
//
struct IBAT_ROUTE_ENTRY
{
    //
    // The Ethernet MAC is stored as a 64-bit value to make arithmetic comparissons easy.
    //
    // This *must* be the first member, to allow the compare routine to compare on MAC only.
    //
    UINT64 mac;
    IbatRoute* pRoute;
};


RTL_GENERIC_COMPARE_RESULTS
NTAPI
IbatRouter::RouteCompare(
    __in RTL_AVL_TABLE* table,
    __in PVOID firstStruct,
    __in PVOID secondStruct
    )
{
    UINT64 firstMac = *static_cast<UINT64*>(firstStruct);
    UINT64 secondMac = *static_cast<UINT64*>(secondStruct);

    if( firstMac > secondMac )
    {
        return GenericGreaterThan;
    }
    if( firstMac < secondMac )
    {
        return GenericLessThan;
    }
    return GenericEqual;

    UNREFERENCED_PARAMETER(table);
}


PVOID
NTAPI
IbatRouter::RouteAlloc(
    __in RTL_AVL_TABLE* table,
    __in CLONG cbEntry
    )
{
    return ExAllocatePoolWithTag( NonPagedPool, cbEntry, IBAT_POOL_TAG );
    UNREFERENCED_PARAMETER(table);
}


VOID
NTAPI
IbatRouter::RouteFree(
    __in RTL_AVL_TABLE* table,
    __in __drv_freesMem(Mem) __post_invalid PVOID pEntry
    )
{
    ExFreePoolWithTag( pEntry, IBAT_POOL_TAG );
    UNREFERENCED_PARAMETER(table);
}


IbatRouter* IbatRouter::Create( UINT64 portGuid, UINT16 pkey, BOOLEAN isRoCE )
{
    NT_ASSERT( portGuid != 0 );

    IbatRouter* pRouter = static_cast<IbatRouter*>(
        ExAllocatePoolWithTag( NonPagedPool, sizeof(IbatRouter), IBAT_POOL_TAG )
        );
    if( pRouter == NULL )
    {
        return NULL;
    }

    KeInitializeSpinLock( &pRouter->m_lock );
    pRouter->m_portGuid = portGuid;
    pRouter->m_pkey = pkey;
    pRouter->m_isRoCE = isRoCE;
    if( isRoCE == TRUE )
    {
        ib_gid_t srcGid;
        ib_gid_set_default( &srcGid, portGuid );

        ib_gid_t destGid;
        ib_gid_set_default( &destGid, 0 );

        ib_path_rec_init_local(
            &pRouter->m_path,
            &destGid,
            &srcGid,
            0,
            0,
            0,
            pkey,
            0,
            0,
            IB_PATH_SELECTOR_EXACTLY,
            IB_MTU_LEN_2048,
            IB_PATH_SELECTOR_EXACTLY,
            IB_PATH_RECORD_RATE_10_GBS,
            IB_PATH_SELECTOR_EXACTLY,
            0,
            0
            );
        //
        // IBTA RoCE Annex, A16.4.4, states that hop limit should be set to 0xFF.
        //
        ib_path_rec_set_hop_flow_raw( &pRouter->m_path, 0xFF, 0, FALSE );
    }
    else
    {
        RtlInitializeGenericTableAvl(
            &pRouter->m_table,
            &IbatRouter::RouteCompare,
            &IbatRouter::RouteAlloc,
            &IbatRouter::RouteFree,
            pRouter
            );
    }

    return pRouter;
}


NTSTATUS
IbatRouter::Update(
    __in UINT64 destMac,
    __in const ib_gid_t* pDestGid
    )
{
    if( m_isRoCE == TRUE )
    {
        return STATUS_SUCCESS;
    }

    NTSTATUS status = STATUS_SUCCESS;
    KLOCK_QUEUE_HANDLE hdl;
    IbatRoute* pRoute;
    IBAT_ROUTE_ENTRY routeEntry;
    IBAT_ROUTE_ENTRY* pEntry;
    BOOLEAN newElement;
    ib_gid_t srcGid;

    ib_gid_set_default( &srcGid, m_portGuid );
    pRoute = IbatRoute::Create( &srcGid, pDestGid, m_pkey );
    if( pRoute == NULL )
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    routeEntry.mac = destMac;
    routeEntry.pRoute = pRoute;

    KeAcquireInStackQueuedSpinLock( &m_lock, &hdl );
    pEntry = static_cast<IBAT_ROUTE_ENTRY*>(
        RtlInsertElementGenericTableAvl(
            &m_table,
            &routeEntry,
            sizeof(routeEntry),
            &newElement
            )
        );

    if( pEntry == NULL )
    {
        pRoute->Release();
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else if( newElement == FALSE )
    {
        //
        // Something about the destination changed - reset and update it.
        //
        // Note that we expect the caller to decide when to update a route, as
        // the endpoint information alone might not reflect a change in the path.
        //
        pRoute->Resolve( pEntry->pRoute->PopRequestList() );
        pEntry->pRoute->Release();
        pEntry->pRoute = pRoute;
    }

    KeReleaseInStackQueuedSpinLock( &hdl );
    return status;
}


VOID
IbatRouter::Reset()
{
    if( m_isRoCE == TRUE )
    {
        return;
    }

    KLOCK_QUEUE_HANDLE hdl;
    IBAT_ROUTE_ENTRY* pRouteEntry;

    KeAcquireInStackQueuedSpinLock( &m_lock, &hdl );
    while( RtlIsGenericTableEmptyAvl( &m_table ) == FALSE )
    {
        pRouteEntry = static_cast<IBAT_ROUTE_ENTRY*>(
            RtlGetElementGenericTableAvl( &m_table, 0 )
            );

        NT_ASSERT( pRouteEntry != NULL );

        if( pRouteEntry->pRoute != NULL )
        {
            pRouteEntry->pRoute->Shutdown();
            pRouteEntry->pRoute->Release();
        }
        RtlDeleteElementGenericTableAvl( &m_table, &pRouteEntry->mac );
    }
    KeReleaseInStackQueuedSpinLock( &hdl );
}


NTSTATUS
IbatRouter::Resolve(
    __in UINT64 mac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    )
{
    if( m_isRoCE == TRUE )
    {
        //
        // The GID for RoCE is generated following the IPv6 stateless address
        // autoconfiguration rules.  The subnet prefix was already set when the
        // path was setup upon router creation, so only the EUI-64 portion of the
        // address must be generated.  The EUI-64 value is generated from the
        // 48-bit MAC by adding 0xFFFE as the middle two bytes and setting the
        // Universal/Local (U/L) bit (bit 2 of the first byte).
        //
        RtlCopyMemory( pPath, &m_path, sizeof(m_path) );

        RtlCopyMemory( &pPath->dgid.raw[8], &mac, 3 );
        pPath->dgid.raw[8] |= 2;
        pPath->dgid.raw[11] = 0xFF;
        pPath->dgid.raw[12] = 0xFE;
        RtlCopyMemory( &pPath->dgid.raw[13], reinterpret_cast<UINT8*>(&mac) + 3, 3);
        return STATUS_SUCCESS;
    }

    NTSTATUS status = STATUS_HOST_UNREACHABLE;
    KLOCK_QUEUE_HANDLE hdl;

    KeAcquireInStackQueuedSpinLock( &m_lock, &hdl );
    IBAT_ROUTE_ENTRY* pEntry = static_cast<IBAT_ROUTE_ENTRY*>(
        RtlLookupElementGenericTableAvl( &m_table, &mac )
        );

    if( pEntry != NULL )
    {
        status = pEntry->pRoute->Resolve(
            completionCallback,
            completionContext,
            pPath
            );
    }

    KeReleaseInStackQueuedSpinLock( &hdl );
    return status;
}


NTSTATUS
IbatRouter::CancelResolve(
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    )
{
    if( m_isRoCE == TRUE )
    {
        return STATUS_NOT_FOUND;
    }

    NTSTATUS status = STATUS_NOT_FOUND;
    KLOCK_QUEUE_HANDLE hdl;

    KeAcquireInStackQueuedSpinLock( &m_lock, &hdl );
    IBAT_ROUTE_ENTRY* pEntry = static_cast<IBAT_ROUTE_ENTRY*>(
        RtlLookupElementGenericTableAvl( &m_table, &destMac )
        );

    if( pEntry != NULL )
    {
        status = pEntry->pRoute->CancelResolve(
            completionCallback,
            completionContext
            );
    }

    KeReleaseInStackQueuedSpinLock( &hdl );
    return status;
}
