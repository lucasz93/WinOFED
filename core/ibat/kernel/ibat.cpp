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
#include "router.h"


static ULONG g_nReg = 0;
//
// There are a number of operations that can be performed on an IBAT port:
//  - Modification of the port array (register/update/deregister)
//  - Lengthy access of a port in the array (anything that calls into IPHelper)
//  - Short access of a port in the array (getting the path)
//
// For modifications, we first acquire the ERESOURCE for exclusive access, to ensure that
// no further lengthy accesses are outstanding.  We then take the spinlock to serialize
// actual changes to the port array.
//
// Because IPHelper calls may block, we cannot use the spinlock for lengthy accesses.
// We thus use the ERESOURCE, with shared access for lengthy operations.
//
// For short operations, such as resolving the path given a physical address (MAC)
// we can simply hold the spinlock and kick off path resolution (or copy the path if
// it has already been resolved).
//
static ERESOURCE g_ibatLock;
static KSPIN_LOCK g_ibatPortLock;
//
// Before Windows 8, ResolveIpNetEntry2 would always start a new neighbor resolution.
// We need to serialize to allow resolution to finish if we started it.  This is not
// needed for Windows 8 and beyond, as ResolveIpNetEntry2 does the right thing for
// multiple concurrent callers.  Removing the mutex gives us better parallelism.
//
#if OSVER(NTDDI_VERSION) <= OSVER(NTDDI_WIN7)
static KMUTEX g_ibatResolveMutex;
#endif
static MIB_UNICASTIPADDRESS_TABLE* g_ibatUnicastAddressTable;
static HANDLE g_hIbatAddressNotification;

//
// We expect there to be only a handful of ports <= 4, typically <= 2, per node.
// As such, we'll use a stupid array of IBAT_PORT structure.
//
static struct IBAT_PORT
{
    UINT64              Mac;
    //
    // In a VM environment, a local NIC can be shared with a VM and the
    // management OS.  In this case the LUID of the local NIC will never
    // have any IP addresses assigned.
    //
    // When IbatUpdateRegistration is called and an entry already exists, the LUID
    // is overwritten.  The virtual NIC will send just such a request.
    //
    UINT64              Luid;

    GUID                DriverId;
    //
    // The router is used to perform destination MAC to IB path resolution.
    // It is a pointer to allow direct access in the data path, at DISPATCH_LEVEL.
    //
    IbatRouter*         pRouter;

    IBAT_PORT_RECORD    Port;

} g_ibatPorts[4];


static
VOID
IbatpAddressChangeHandler(
    __in VOID* /*context*/,
    __in_opt MIB_UNICASTIPADDRESS_ROW* /*row*/,
    __in MIB_NOTIFICATION_TYPE /*type*/
    )
{
    MIB_UNICASTIPADDRESS_TABLE* pOldTable;
    NTSTATUS status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &g_ibatLock, TRUE );

    pOldTable = g_ibatUnicastAddressTable;

    status = GetUnicastIpAddressTable( AF_UNSPEC, &g_ibatUnicastAddressTable );
    if( !NT_SUCCESS(status) )
    {
        g_ibatUnicastAddressTable = pOldTable;
    }
    else if( pOldTable != NULL )
    {
        FreeMibTable( pOldTable );
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
}


EXTERN_C
NTSTATUS
IbatInitialize()
{
    RtlZeroMemory( g_ibatPorts, sizeof(g_ibatPorts) );
#if OSVER(NTDDI_VERSION) <= OSVER(NTDDI_WIN7)
    KeInitializeMutex( &g_ibatResolveMutex, 0 );
#endif
    KeInitializeSpinLock( &g_ibatPortLock );
    g_ibatUnicastAddressTable = NULL;
    g_hIbatAddressNotification = NULL;

    NTSTATUS status = ExInitializeResourceLite( &g_ibatLock );
    if( !NT_SUCCESS(status) )
    {
        return status;
    }

    IbatpAddressChangeHandler( NULL, NULL, MibInitialNotification );
    if( g_ibatUnicastAddressTable == NULL )
    {
        ExDeleteResourceLite( &g_ibatLock );
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = NotifyUnicastIpAddressChange(
        AF_UNSPEC,
        IbatpAddressChangeHandler,
        NULL,
        TRUE,
        &g_hIbatAddressNotification
        );
    if( !NT_SUCCESS(status) )
    {
        IbatCleanup();
    }

    return status;
}


EXTERN_C
VOID
IbatCleanup()
{
    if( g_hIbatAddressNotification != NULL )
    {
        CancelMibChangeNotify2( g_hIbatAddressNotification );
    }

    if( g_ibatUnicastAddressTable != NULL )
    {
        FreeMibTable( g_ibatUnicastAddressTable );
    }

    ExDeleteResourceLite( &g_ibatLock );
}


static IBAT_PORT*
IbatpLookupPortUnsafe(
    __in UINT64 mac
    )
{
    for( ULONG i = 0; i < g_nReg; i++ )
    {
        if( g_ibatPorts[i].Mac == mac )
        {
            return &g_ibatPorts[i];
        }
    }

    return NULL;
}


//
// Called by the Virtual MiniPort NIC of VmSwitch to update the LUID of the associated
// IPoIB instance.
//
NTSTATUS
IbatUpdateRegistration(
    __in UINT64 mac,
    __in UINT64 luid
    )
{
    IBAT_PORT* pPort;
    NTSTATUS status = STATUS_SUCCESS;
    KLOCK_QUEUE_HANDLE hLock;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &g_ibatLock, TRUE );
    KeAcquireInStackQueuedSpinLock( &g_ibatPortLock, &hLock );

    pPort = IbatpLookupPortUnsafe( mac );

    if( pPort != NULL )
    {
        pPort->Luid = luid;
    }
    else
    {
        // TODO: Should we support the Virtual MiniPort NIC to come up before the underlying
        // protocl NIC?
        status = STATUS_INVALID_PARAMETER;
    }

    KeReleaseInStackQueuedSpinLock( &hLock );
    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


//
// Called by IPoIB.
//
NTSTATUS
IbatRegister(
    __in UINT64 mac,
    __in UINT64 luid,
    __in const GUID* pDriverId,
    __in IBAT_PORT_RECORD* pPortRecord,
    __in BOOLEAN isRoCE,
    __out IBAT_ROUTING_CONTEXT* pRoutingContext
    )
{
    IBAT_PORT* pPort;
    NTSTATUS status = STATUS_SUCCESS;
    KLOCK_QUEUE_HANDLE hLock;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &g_ibatLock, TRUE );
    KeAcquireInStackQueuedSpinLock( &g_ibatPortLock, &hLock );

    pPort = IbatpLookupPortUnsafe( mac );

    if( pPort != NULL )
    {
        // TODO: Should we support the Virtual MiniPort NIC to come up before the underlying
        // protocl NIC?
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }

    if( g_nReg == ARRAYSIZE(g_ibatPorts) )
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    g_ibatPorts[g_nReg].Mac = mac;
    g_ibatPorts[g_nReg].Luid = luid;
    g_ibatPorts[g_nReg].DriverId = *pDriverId;
    g_ibatPorts[g_nReg].Port  = *pPortRecord;
    g_ibatPorts[g_nReg].pRouter = IbatRouter::Create(
        pPortRecord->PortGuid,
        pPortRecord->PKey,
        isRoCE
        );
    if( g_ibatPorts[g_nReg].pRouter == NULL )
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }
    *pRoutingContext = g_ibatPorts[g_nReg].pRouter;

    g_nReg++;

done:
    KeReleaseInStackQueuedSpinLock( &hLock );
    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();

    return status;
}


//
// Called by IPoIB
//
VOID
IbatDeregister(
    __in IBAT_ROUTING_CONTEXT routingContext
    )
{
    ULONG i;
    KLOCK_QUEUE_HANDLE hLock;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite( &g_ibatLock, TRUE );
    KeAcquireInStackQueuedSpinLock( &g_ibatPortLock, &hLock );

    //
    // We must search for the proper index, as the index can change at runtime.
    // A back pointer won't do at all.
    //
    for( i = 0; i < g_nReg; i++ )
    {
        if( g_ibatPorts[i].pRouter == routingContext )
        {
            break;
        }
    }

    if( i < g_nReg )
    {
        g_ibatPorts[i].pRouter->Reset();
        ExFreePoolWithTag( g_ibatPorts[i].pRouter, IBAT_POOL_TAG );

        g_nReg--;
        g_ibatPorts[i] = g_ibatPorts[g_nReg];
        g_ibatPorts[g_nReg].Port.CaGuid = 0;
    }

    KeReleaseInStackQueuedSpinLock( &hLock );
    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
}


NTSTATUS
IbatUpdateRoute(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in const ib_gid_t* pDestGid
    )
{
    return routingContext->Update( destMac, pDestGid );
}


VOID
IbatClearAllRoutes(
    __in IBAT_ROUTING_CONTEXT routingContext
    )
{
    routingContext->Reset();
}


EXTERN_C
NTSTATUS
IbatGetIpList(
    __in_opt const GUID* pDriverId,
    __in UINT64 caGuid,
    __in UINT64 portGuid,
    __in UINT16 pkey,
    __inout ULONG* pnAddrs,
    __out SOCKADDR_INET* pAddrs
    )
{
    NTSTATUS status;
    ULONG iAddrs = 0;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );
    NT_ASSERT( g_ibatUnicastAddressTable != NULL );

    for( ULONG i = 0; i < g_ibatUnicastAddressTable->NumEntries; i++ )
    {
        for( ULONG iReg = 0; iReg < g_nReg; iReg++ )
        {
            if( (caGuid != 0 && caGuid != g_ibatPorts[iReg].Port.CaGuid) ||
                (portGuid != 0 && portGuid != g_ibatPorts[iReg].Port.PortGuid) ||
                (pkey != 0 && pkey != g_ibatPorts[iReg].Port.PKey) ||
                ((pDriverId != NULL) && (*pDriverId != g_ibatPorts[iReg].DriverId)) )
            {
                continue;
            }

            if( g_ibatPorts[iReg].Luid ==
                g_ibatUnicastAddressTable->Table[i].InterfaceLuid.Value )
            {
                if( iAddrs < *pnAddrs )
                {
                    RtlCopyMemory(
                        &pAddrs[iAddrs],
                        &g_ibatUnicastAddressTable->Table[i].Address,
                        sizeof(SOCKADDR_INET)
                        );
                }
                iAddrs++;
            }
        }
    }
    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();

    if( pAddrs == NULL )
    {
        status =  STATUS_BUFFER_OVERFLOW;
    }
    else if( iAddrs > *pnAddrs )
    {
        status = STATUS_MORE_ENTRIES;
    }
    else
    {
        status = STATUS_SUCCESS;
    }

    *pnAddrs = iAddrs;
    return status;
}


EXTERN_C
NTSTATUS
IbatGetIpListIoctl(
    __in IRP* pIrp
    )
{
    IO_STACK_LOCATION* pIoStack;
    IBAT_PORT_RECORD* in;
    ULONG nAddrs;
    NTSTATUS status;

    pIoStack = IoGetCurrentIrpStackLocation( pIrp );

    CL_ASSERT( (pIoStack->Parameters.DeviceIoControl.IoControlCode & 0x03) == METHOD_BUFFERED );

    in = static_cast<IBAT_PORT_RECORD*>( pIrp->AssociatedIrp.SystemBuffer );

    nAddrs = pIoStack->Parameters.DeviceIoControl.OutputBufferLength / sizeof(SOCKADDR_INET);

    if( pIoStack->Parameters.DeviceIoControl.InputBufferLength == 0 )
    {
        status = IbatGetIpList(
            NULL,
            0,
            0,
            0,
            &nAddrs,
            static_cast<SOCKADDR_INET*>( pIrp->AssociatedIrp.SystemBuffer )
            );
    }
    else if( pIoStack->Parameters.DeviceIoControl.InputBufferLength == sizeof(IBAT_PORT_RECORD) )
    {
        status = IbatGetIpList(
            NULL,
            in->CaGuid,
            in->PortGuid,
            in->PKey,
            &nAddrs,
            static_cast<SOCKADDR_INET*>( pIrp->AssociatedIrp.SystemBuffer )
            );
    }
    else
    {
        status = STATUS_INVALID_PARAMETER;
    }

    switch( status )
    {
    case STATUS_SUCCESS:
    case STATUS_BUFFER_OVERFLOW:
        pIrp->IoStatus.Information = nAddrs * sizeof(SOCKADDR_INET);
        break;

    case STATUS_MORE_ENTRIES:
        pIrp->IoStatus.Information = pIoStack->Parameters.DeviceIoControl.OutputBufferLength -
            (pIoStack->Parameters.DeviceIoControl.OutputBufferLength % sizeof(SOCKADDR_INET));
        break;

    default:
        pIrp->IoStatus.Information = 0;
        break;
    }
    pIrp->IoStatus.Status = status;
    IoCompleteRequest( pIrp, IO_NO_INCREMENT );
    return status;
}


static NTSTATUS
IbatpIpToIndexUnsafe(
    __in const SOCKADDR_INET* pAddr,
    __out ULONG* pIndex,
    __out UINT64* pLuid
    )
{
    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    NT_ASSERT( g_ibatUnicastAddressTable != NULL );

    for( ULONG i = 0; i < g_ibatUnicastAddressTable->NumEntries; i++ )
    {
        if( IbatUtil::IsEqual(g_ibatUnicastAddressTable->Table[i].Address, *pAddr) == false )
        {
            continue;
        }

        for( ULONG iReg = 0; iReg < g_nReg; iReg++ )
        {
            if( g_ibatPorts[iReg].Luid ==
                g_ibatUnicastAddressTable->Table[i].InterfaceLuid.Value )
            {
                *pIndex = iReg;
                *pLuid = g_ibatPorts[iReg].Luid;
                return STATUS_SUCCESS;
            }
        }
    }

    return STATUS_INVALID_ADDRESS;
}


static NTSTATUS
IbatpPortToIndexUnsafe(
    __in UINT64 mac,
    __out ULONG* pIndex
    )
{
    for( ULONG iReg = 0; iReg < g_nReg; iReg++ )
    {
        if( g_ibatPorts[iReg].Mac == mac )
        {
            *pIndex = iReg;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_INVALID_PARAMETER;
}


EXTERN_C
NTSTATUS
IbatIpToPort(
    __in const SOCKADDR_INET* pAddr,
    __in_opt const GUID* pDriverId,
    __out IBAT_PORT_RECORD* pPortRecord
    )
{
    NTSTATUS status;
    ULONG iReg;
    UINT64 luid;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );
    status = IbatpIpToIndexUnsafe( pAddr, &iReg, &luid );
    if( NT_SUCCESS(status) )
    {
        if( (pDriverId != NULL) && (*pDriverId != g_ibatPorts[iReg].DriverId) )
        {
            status = STATUS_INVALID_ADDRESS;
        }
        else
        {
            *pPortRecord = g_ibatPorts[iReg].Port;
        }
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


EXTERN_C
NTSTATUS
IbatIpToPortIoctl(
    __in IRP* pIrp
    )
{
    IO_STACK_LOCATION* pIoStack;
    SOCKADDR_INET* pAddr;
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    pIoStack = IoGetCurrentIrpStackLocation( pIrp );

    CL_ASSERT( (pIoStack->Parameters.DeviceIoControl.IoControlCode & 0x03) == METHOD_BUFFERED );

    if( pIoStack->Parameters.DeviceIoControl.InputBufferLength != sizeof(SOCKADDR_INET) ||
        pIoStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(IBAT_PORT_RECORD) )
    {
        goto error;
    }

    pAddr = static_cast<SOCKADDR_INET*>( pIrp->AssociatedIrp.SystemBuffer );
    switch( pAddr->si_family )
    {
    case AF_INET:
    case AF_INET6:
        status = IbatIpToPort(
            pAddr,
            NULL,
            static_cast<IBAT_PORT_RECORD*>( pIrp->AssociatedIrp.SystemBuffer )
            );
        break;

    default:
        status = STATUS_INVALID_PARAMETER;
    }

error:
    if( !NT_SUCCESS( status ) )
    {
        pIrp->IoStatus.Information = 0;
    }
    else
    {
        pIrp->IoStatus.Information = sizeof(IBAT_PORT_RECORD);
    }

    pIrp->IoStatus.Status = status;
    IoCompleteRequest( pIrp, IO_NO_INCREMENT );
    return status;
}


EXTERN_C
NTSTATUS
IbatMacToPort(
    __in UINT64 mac,
    __in_opt const GUID* pDriverId,
    __out IBAT_PORT_RECORD* pPortRecord
    )
{
    NTSTATUS status;
    ULONG iReg;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );
    status = IbatpPortToIndexUnsafe( mac, &iReg );
    if( NT_SUCCESS(status) )
    {
        if( (pDriverId != NULL) && (*pDriverId != g_ibatPorts[iReg].DriverId) )
        {
            status = STATUS_INVALID_ADDRESS;
        }
        else
        {
            *pPortRecord = g_ibatPorts[iReg].Port;
        }
    }
    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


static
NTSTATUS
IbatpResolveRemoteAddressUnsafe(
    __in UINT64                 luid,
    __in const SOCKADDR_INET*   pLocalAddress,
    __in const SOCKADDR_INET*   pRemoteAddress,
    __out UINT64*               pMac
    )
{
    MIB_IPNET_ROW2 row;
    NTSTATUS status;

    NT_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    row.Address             = *pRemoteAddress;
    row.InterfaceIndex      = 0;
    row.InterfaceLuid.Value = luid;

#if OSVER(NTDDI_VERSION) <= OSVER(NTDDI_WIN7)
    KeWaitForSingleObject( &g_ibatResolveMutex, UserRequest, KernelMode, FALSE, NULL );
#endif
    status = GetIpNetEntry2( &row );
    //
    // If the neighbor does not exist (including local address), we will
    // get STATUS_NOT_FOUND.  Treat that as NlnsUnreachable.
    //
    if( status == STATUS_NOT_FOUND )
    {
        row.State = NlnsUnreachable;
    }
    else if( !NT_SUCCESS(status) )
    {
        goto Unlock;
    }

    switch( row.State )
    {
    case NlnsUnreachable:
        //
        // ResolveIpNetEntry2 will not fill the row if resolution is incomplete,
        // so we need to query the row to get the current state.
        //
        status = ResolveIpNetEntry2( &row, pLocalAddress );
        if( status == STATUS_BAD_NETWORK_NAME )
        {
            status = GetIpNetEntry2( &row );
            if( status == STATUS_NOT_FOUND )
            {
                status = STATUS_BAD_NETWORK_NAME;
            }
        }
    if( !NT_SUCCESS( status ) )
    {
            break;
    }
        if( row.State == NlnsUnreachable )
    {
            status = STATUS_BAD_NETWORK_NAME;
            break;
    }
        if( row.State > NlnsIncomplete )
        {
    //
            // Entry might be old, but it was valid before - run with it.
    //
            break;
}
        __fallthrough;

    case NlnsIncomplete:
        status = STATUS_IO_TIMEOUT;
        break;

    case NlnsProbe:
    case NlnsDelay:
    case NlnsStale:
    case NlnsReachable:
    case NlnsPermanent:
        status = STATUS_SUCCESS;
        break;
    }

Unlock:
#if OSVER(NTDDI_VERSION) <= OSVER(NTDDI_WIN7)
    KeReleaseMutex( &g_ibatResolveMutex, FALSE );
#endif
    if( !NT_SUCCESS(status) )
    {
        return status;
    }

    if( row.PhysicalAddressLength > sizeof(*pMac) )
    {
        return STATUS_NETWORK_UNREACHABLE;
    }

    //
    // The common case is that the physical address is an Ethernet MAC, and 6 bytes.
    // We use the MAC for looking up a route in an AVL tree, and treat it as a
    // 64-bit integer.  Clear the the entire 64-bit MAC variable and then copy the
    // physical address to set only the valid bytes.
    //
    *pMac = 0;
    RtlCopyMemory( pMac, row.PhysicalAddress, row.PhysicalAddressLength );

    return STATUS_SUCCESS;
}


static
NTSTATUS
IbatpResolvePhysicalAddressUnsafe(
    __in const SOCKADDR_INET*   pLocalAddress,
    __in const SOCKADDR_INET*   pRemoteAddress,
    __out ULONG*                pIndex,
    __out UINT64*               pMac
    )
{
    NTSTATUS status;
    UINT64 luid;

    NT_ASSERT( KeGetCurrentIrql() < DISPATCH_LEVEL );

    status = IbatpIpToIndexUnsafe( pLocalAddress, pIndex, &luid );
    if( !NT_SUCCESS( status ) )
    {
        return status;
    }

    status = IbatpResolveRemoteAddressUnsafe( luid, pLocalAddress, pRemoteAddress, pMac );
    return status;
}


//
// Summary:
//  Gets a path record given the specified ip address information
//
// Parameters:
//  pLocalAddress           - the local IP address assigned to an IB port.
//  pRemoteAddress          - the destination IP Address
//  pfnCompletionCallback   - call back function to be invoked if async operation required
//  pCompletionContext      - context value to be passed to callback when completed.
//  pPath                   - buffer to recieve the IB path if completed syncronously.
//
// Remarks:
// If the function returns STATUS_PENDING, completion will be indicated through
//   the completion callback and the path out parameter is not used.
//   In this case, pPath value is not carried to the callback routine, and must thus
//   be passed to the context via the pCompletionContext value in some way.  If the
//   task succeeded, the callback should then copy the provided path record to the
//   output buffer.
//
EXTERN_C
NTSTATUS
IbatQueryPathByIpAddress(
    __in const SOCKADDR_INET* pLocalAddress,
    __in const SOCKADDR_INET* pRemoteAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    )
{
    NTSTATUS status;
    ULONG iReg;
    ULONG64 mac;

    NT_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );

    status = IbatpResolvePhysicalAddressUnsafe(
                                pLocalAddress,
                                pRemoteAddress,
                                &iReg,
                                &mac
                                );

    if( NT_SUCCESS( status ) )
    {
        status = g_ibatPorts[iReg].pRouter->Resolve(
            mac,
            completionCallback,
            completionContext,
            pPath
            );
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


EXTERN_C
NTSTATUS
IbatResolvePhysicalAddress(
    __in const SOCKADDR_INET*   pLocalAddress,
    __in const SOCKADDR_INET*   pRemoteAddress,
    __out UINT64*               pSrcMac,
    __out UINT64*               pDestMac
    )
{
    NTSTATUS status;
    ULONG iReg;

    NT_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );

    status = IbatpResolvePhysicalAddressUnsafe(
                                pLocalAddress,
                                pRemoteAddress,
                                &iReg,
                                pDestMac
                                );
    if( NT_SUCCESS( status ) )
    {
        *pSrcMac = g_ibatPorts[iReg].Mac;
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


//
// Summary:
//  Gets a path record given the specified port and mac address information
//
// Parameters:
//  guid                    - port GUID.
//  pkey                    - port pkey
//  pfnCompletionCallback   - call back function to be invoked if async operation required
//  pCompletionContext      - context value to be passed to callback when completed.
//  pPath                   - buffer to recieve the IB path if completed syncronously.
//
// Remarks:
// If the function returns STATUS_PENDING, completion will be indicated through
//   the completion callback and the path out parameter is not used.
//   In this case, pPath value is not carried to the callback routine, and must thus
//   be passed to the context via the pCompletionContext value in some way.  If the
//   task succeeded, the callback should then copy the provided path record to the
//   output buffer.
//
EXTERN_C
NTSTATUS
IbatQueryPathByPhysicalAddress(
    __in UINT64 srcMac,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    )
{
    NTSTATUS status;
    ULONG iReg;
    KLOCK_QUEUE_HANDLE hLock;

    KeAcquireInStackQueuedSpinLock( &g_ibatPortLock, &hLock );

    status = IbatpPortToIndexUnsafe( srcMac, &iReg );
    if( NT_SUCCESS( status ) )
    {
        status = g_ibatPorts[iReg].pRouter->Resolve(
            destMac,
            completionCallback,
            completionContext,
            pPath
            );
    }

    KeReleaseInStackQueuedSpinLock( &hLock );
    return status;
}


EXTERN_C
NTSTATUS
IbatQueryPath(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    )
{
    return routingContext->Resolve(
        destMac,
        completionCallback,
        completionContext,
        pPath
        );
}


EXTERN_C
NTSTATUS
IbatCancelQuery(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    )
{
    return routingContext->CancelResolve(
        destMac,
        completionCallback,
        completionContext
        );
}


EXTERN_C
NTSTATUS
IbatCancelQueryByIpAddress(
    __in const SOCKADDR_INET* pLocalAddress,
    __in const SOCKADDR_INET* pRemoteAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    )
{
    NTSTATUS status;
    ULONG iReg;
    ULONG64 mac;

    NT_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );

    status = IbatpResolvePhysicalAddressUnsafe(
                                pLocalAddress,
                                pRemoteAddress,
                                &iReg,
                                &mac
                                );

    if( NT_SUCCESS( status ) )
    {
        status = g_ibatPorts[iReg].pRouter->CancelResolve(
            mac,
            completionCallback,
            completionContext
            );
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


EXTERN_C
NTSTATUS
IbatCancelQueryByPhysicalAddress(
    __in UINT64 srcMac,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    )
{
    NTSTATUS status;
    ULONG iReg;

    NT_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite( &g_ibatLock, TRUE );

    status = IbatpPortToIndexUnsafe( srcMac, &iReg );
    if( NT_SUCCESS( status ) )
    {
        status = g_ibatPorts[iReg].pRouter->CancelResolve(
            destMac,
            completionCallback,
            completionContext
            );
    }

    ExReleaseResourceLite( &g_ibatLock );
    KeLeaveCriticalRegion();
    return status;
}


EXTERN_C
void
IbatGetInterface(
    __out IBAT_IFC* pIbatIfc
    )
{
    pIbatIfc->Register =                    IbatRegister;
    pIbatIfc->Deregister =                  IbatDeregister;
    pIbatIfc->UpdateRegistration =          IbatUpdateRegistration;
    pIbatIfc->UpdateRoute =                 IbatUpdateRoute;
    pIbatIfc->ClearAllRoutes =              IbatClearAllRoutes;
    pIbatIfc->QueryPath =                   IbatQueryPath;
    pIbatIfc->CancelQuery =                 IbatCancelQuery;
    pIbatIfc->GetIpList =                   IbatGetIpList;
    pIbatIfc->IpToPort =                    IbatIpToPort;
    pIbatIfc->QueryPathByIpAddress =        IbatQueryPathByIpAddress;
    pIbatIfc->CancelQueryByIpAddress =      IbatCancelQueryByIpAddress;
    pIbatIfc->QueryPathByPhysicalAddress =  IbatQueryPathByPhysicalAddress;
    pIbatIfc->CancelQueryByPhysicalAddress = IbatCancelQueryByPhysicalAddress;
    pIbatIfc->ResolvePhysicalAddress =      IbatResolvePhysicalAddress;
    pIbatIfc->MacToPort =                   IbatMacToPort;
}
