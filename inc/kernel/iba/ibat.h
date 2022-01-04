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

#ifndef _IBAT_H_
#define _IBAT_H_

#include "iba\ib_at_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct IbatRouter* IBAT_ROUTING_CONTEXT;

//
// The following function types are provided to kernel drivers that serve
// as sources of routing data.  These functions are not intended to be used
// by end-users.
//

//
// Registers a routing provider with IBAT, including the local NET_LUID value
// for the NDIS interface used to query IP Helper.
//
// Returns a routing context that can be used to update route information.
//
typedef NTSTATUS
FN_IBAT_REGISTER(
    __in UINT64 mac,
    __in UINT64 luid,
    __in const GUID* pDriverId,
    __in IBAT_PORT_RECORD* pPortRecord,
    __in BOOLEAN isRoCE,
    __out IBAT_ROUTING_CONTEXT* pRoutingContext
    );
FN_IBAT_REGISTER IbatRegister;


//
// Updates the NET_LUID value associated with an existing registration.
//
typedef NTSTATUS
FN_IBAT_UPDATE_REGISTRATION(
    __in UINT64 mac,
    __in UINT64 luid
    );
FN_IBAT_UPDATE_REGISTRATION IbatUpdateRegistration;

//
// Removes a routing provider from IBAT, including all associated routing information.
//
typedef VOID
FN_IBAT_DEREGISTER(
    __in IBAT_ROUTING_CONTEXT routingContext
    );
FN_IBAT_DEREGISTER IbatDeregister;

//
// Adds or updates a route to the destination MAC address.
//
typedef NTSTATUS
FN_IBAT_UPDATE_ROUTE(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in const ib_gid_t* pDestGid
    );
FN_IBAT_UPDATE_ROUTE IbatUpdateRoute;

//
// Deletes all routing information for the supplied routing context.
//
typedef VOID
FN_IBAT_CLEAR_ALL_ROUTES(
    __in IBAT_ROUTING_CONTEXT routingContext
    );
FN_IBAT_CLEAR_ALL_ROUTES IbatClearAllRoutes;

//
// Callback for path queries.
//
typedef
__drv_maxFunctionIRQL(DISPATCH_LEVEL)
VOID
FN_IBAT_QUERY_PATH_CALLBACK(
    __in VOID* completionContext,
    __in NTSTATUS status,
    __in ib_path_rec_t* const pPath
    );

//
// Queries the path record for the supplied route.
//
typedef
__drv_maxFunctionIRQL(DISPATCH_LEVEL)
NTSTATUS
FN_IBAT_QUERY_PATH(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    );
FN_IBAT_QUERY_PATH IbatQueryPath;

//
// Cancel an outstanding query.
//
typedef
__drv_maxFunctionIRQL(DISPATCH_LEVEL)
NTSTATUS
FN_IBAT_CANCEL_QUERY(
    __in IBAT_ROUTING_CONTEXT routingContext,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    );
FN_IBAT_CANCEL_QUERY IbatCancelQuery;

//
// Initializes global structures - called by whoever provides IBAT support.
//
NTSTATUS
IbatInitialize();


//
// Cleans up global structures.  Called by whoever provides IBAT support.
//
VOID
IbatCleanup();


//
// Get the list of local IP addresses for a given input NIC port.
//
// Input parameter values of 0 indicate wildcard.
//
// TODO: Should pAddrs be SOCKADDR_STORAGE to allow extensions in the future to
// other address families besides AF_INET and AF_INET6?
//
typedef
__drv_maxFunctionIRQL(APC_LEVEL)
NTSTATUS
FN_IBAT_GET_IP_LIST(
    __in_opt const GUID* pDriverId,
    __in UINT64 caGuid,
    __in UINT64 portGuid,
    __in UINT16 pkey,
    __inout ULONG* pnAddrs,
    __out SOCKADDR_INET* pAddrs
    );
FN_IBAT_GET_IP_LIST IbatGetIpList;

__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
IbatGetIpListIoctl(
    __in IRP* pIrp
    );


//
// Gets the local NIC port information for a given local IP address.
//
typedef
__drv_maxFunctionIRQL(APC_LEVEL)
NTSTATUS
FN_IBAT_IP_TO_PORT(
    __in const SOCKADDR_INET* pAddr,
    __in_opt const GUID* pDriverId,
    __out IBAT_PORT_RECORD* pPortRecord
    );
FN_IBAT_IP_TO_PORT IbatIpToPort;

__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
IbatIpToPortIoctl(
    __in IRP* pIrp
    );


typedef
__drv_maxFunctionIRQL(APC_LEVEL)
NTSTATUS
FN_IBAT_MAC_TO_PORT(
    __in UINT64 mac,
    __in_opt const GUID* pDriverId,
    __out IBAT_PORT_RECORD* pPortRecord
    );
FN_IBAT_MAC_TO_PORT IbatMacToPort;


//
// Gets a path record given a local and remote IP address.
//
// If the function returns STATUS_PENDING, completion will be indicated through
// the completion callback and the path out parameter is not used.
//
typedef
__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
FN_IBAT_QUERY_PATH_BY_IP_ADDRESS(
    __in const SOCKADDR_INET* pLocalAddress,
    __in const SOCKADDR_INET* pRemoteAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    );
FN_IBAT_QUERY_PATH_BY_IP_ADDRESS IbatQueryPathByIpAddress;


typedef
__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
FN_IBAT_CANCEL_QUERY_BY_IP_ADDRESS(
    __in const SOCKADDR_INET* pLocalAddress,
    __in const SOCKADDR_INET* pRemoteAddress,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    );
FN_IBAT_CANCEL_QUERY_BY_IP_ADDRESS IbatCancelQueryByIpAddress;


typedef
__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
FN_IBAT_QUERY_PATH_BY_PHYSICAL_ADDRESS(
    __in UINT64 srcMac,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    );
FN_IBAT_QUERY_PATH_BY_PHYSICAL_ADDRESS IbatQueryPathByPhysicalAddress;


typedef
__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
FN_IBAT_CANCEL_QUERY_BY_PHYSICAL_ADDRESS(
    __in UINT64 srcMac,
    __in UINT64 destMac,
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    );
FN_IBAT_CANCEL_QUERY_BY_PHYSICAL_ADDRESS IbatCancelQueryByPhysicalAddress;


typedef
__drv_maxFunctionIRQL(PASSIVE_LEVEL)
NTSTATUS
FN_IBAT_RESOLVE_PHYSICAL_ADDRESS(
    __in const SOCKADDR_INET*   pLocalAddress,
    __in const SOCKADDR_INET*   pRemoteAddress,
    __out UINT64*               pSrcMac,
    __out UINT64*               pDestMac
    );
FN_IBAT_RESOLVE_PHYSICAL_ADDRESS IbatResolvePhysicalAddress;


#ifdef __cplusplus
} //extern "C" {
#endif


#endif  // _IBAT_H_
