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

#if !defined _IBAT_IFC_H_
#define _IBAT_IFC_H_

#include <iba\ibat.h>

#define IBAT_INTERFACE_VERSION		(4)


/* Interface definitions */
typedef struct _IBAT_IFC
{
    /* Standard interface header. */
    INTERFACE                               InterfaceHeader;

    FN_IBAT_REGISTER*                       Register;
    FN_IBAT_DEREGISTER*                     Deregister;
    FN_IBAT_UPDATE_REGISTRATION*            UpdateRegistration;
    FN_IBAT_UPDATE_ROUTE*                   UpdateRoute;
    FN_IBAT_CLEAR_ALL_ROUTES*               ClearAllRoutes;
    FN_IBAT_QUERY_PATH*                     QueryPath;
    FN_IBAT_CANCEL_QUERY*                   CancelQuery;
    FN_IBAT_GET_IP_LIST*                    GetIpList;
    FN_IBAT_IP_TO_PORT*                     IpToPort;
    FN_IBAT_QUERY_PATH_BY_IP_ADDRESS*       QueryPathByIpAddress;
    FN_IBAT_CANCEL_QUERY_BY_IP_ADDRESS*     CancelQueryByIpAddress;
    FN_IBAT_QUERY_PATH_BY_PHYSICAL_ADDRESS* QueryPathByPhysicalAddress;
    FN_IBAT_CANCEL_QUERY_BY_PHYSICAL_ADDRESS* CancelQueryByPhysicalAddress;
    FN_IBAT_RESOLVE_PHYSICAL_ADDRESS*       ResolvePhysicalAddress;
    FN_IBAT_MAC_TO_PORT*                    MacToPort;

}   IBAT_IFC;


EXTERN_C
void
IbatGetInterface(
    __out IBAT_IFC* pIbatIfc
    );

#endif	/* !defined _IB_AT_IFC_H_ */

/*
 * AL interface GUID.  The GUID is defined outside the conditional include
 * on purpose so that it can be instantiated only once where it is actually
 * needed.  See the DDK docs section "Using GUIDs in Drivers" for more info.
 */
#ifdef DEFINE_GUID
// {6497B483-E13E-4887-97CE-1F0EE1D5FF7B}
DEFINE_GUID(GUID_IBAT_INTERFACE, 
0x6497b483, 0xe13e, 0x4887, 0x97, 0xce, 0x1f, 0xe, 0xe1, 0xd5, 0xff, 0x7b);
#endif // DEFINE_GUID
