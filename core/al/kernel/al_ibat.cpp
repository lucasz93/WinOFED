/*
 * Copyright (c) 2005 Mellanox Technologies.  All rights reserved.
 * Copyright (c) 2005 SilverStorm Technologies.  All rights reserved.
 * Portions Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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


#include "al_dev.h"
#include <iba\ibat.h>
extern "C"
{
#include "al_debug.h"
}

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "al_ibat.tmh"
#endif


static NTSTATUS
__ibat_get_ips(
    IN              IRP                         *pIrp,
    IN              IO_STACK_LOCATION           *pIoStack )
{
    IOCTL_IBAT_IP_ADDRESSES_IN  *pIn;
    IOCTL_IBAT_IP_ADDRESSES_OUT *pOut;
    ULONG                       nAddr;
    ULONG                       maxIps;
    NTSTATUS                    status;

    AL_ENTER(AL_DBG_DEV);

    if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
        sizeof(IOCTL_IBAT_IP_ADDRESSES_IN) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid input buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength <
        sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid output buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    pIn = (IOCTL_IBAT_IP_ADDRESSES_IN *) pIrp->AssociatedIrp.SystemBuffer;
    pOut = (IOCTL_IBAT_IP_ADDRESSES_OUT *) pIrp->AssociatedIrp.SystemBuffer;

    if( pIn->Version != IBAT_IOCTL_VERSION )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid version.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    maxIps = 1 +
        ((pIoStack->Parameters.DeviceIoControl.OutputBufferLength -
        sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT)) / sizeof(SOCKADDR_INET));

    nAddr = maxIps;
    status = IbatGetIpList( NULL, 0, pIn->PortGuid, 0, &nAddr, pOut->Address );
    if( status == STATUS_MORE_ENTRIES )
    {
        pOut->AddressCount = maxIps;
    }
    else if( status == STATUS_SUCCESS )
    {
        pOut->AddressCount = nAddr;
    }
    else
    {
        pOut->AddressCount = 0;
        return status;
    }

    pOut->Size =
        sizeof(*pOut) - sizeof(pOut->Address) + (sizeof(pOut->Address) * nAddr);
    pIrp->IoStatus.Information = sizeof(*pOut) - sizeof(pOut->Address);
    if( nAddr > maxIps )
    {
        pIrp->IoStatus.Information += sizeof(pOut->Address) * maxIps;
    }
    else
    {
        pIrp->IoStatus.Information += sizeof(pOut->Address) * nAddr;
    }

    AL_EXIT( AL_DBG_DEV );
    return STATUS_SUCCESS;
}


static VOID
__ibat_query_cb(
    VOID* context,
    NTSTATUS status,
    ib_path_rec_t* const pPath
    )
{
    IRP* pIrp = reinterpret_cast<IRP*>(context);

    if( !NT_SUCCESS(status) )
    {
        pIrp->IoStatus.Information = 0;
    }
    else
    {
        pIrp->IoStatus.Information = sizeof(*pPath);
        RtlCopyMemory( pIrp->AssociatedIrp.SystemBuffer, pPath, sizeof(*pPath) );
    }
    pIrp->IoStatus.Status = status;
    IoCompleteRequest( pIrp, IO_NO_INCREMENT );
}


//
// Summary:
//  IOCTL handler wrapper for IbatCreatePath
//
// Returns:
//  STATUS_SUCCESS - on success
//  STATUS_NOT_FOUND - when local address does not match IB port.
//  STATUS_BAD_NETWORK_NAME - when the remote address can not be resolved.
//  STATUS_NOT_SUPPORTED - when remote physical address is not of expected size.
//  STATUS_INVALID_PARAMETER - if input or output parameters are not valid
//  STATUS_INVALID_ADDRESS - when input address are not othe same version or are a valid version.
//
static
NTSTATUS
__ibat_query_path(
    __in IRP*                       pIrp,
    __in IO_STACK_LOCATION*         pIoStack
    )
{
    IOCTL_IBAT_QUERY_PATH_IN*      pIn;
    IOCTL_IBAT_QUERY_PATH_OUT*     pOut;

    if( pIoStack->Parameters.DeviceIoControl.InputBufferLength != sizeof(*pIn) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid input buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength != sizeof(*pOut) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid output buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    pIn = static_cast<IOCTL_IBAT_QUERY_PATH_IN*>(pIrp->AssociatedIrp.SystemBuffer);
    pOut = static_cast<IOCTL_IBAT_QUERY_PATH_OUT*>( pIrp->AssociatedIrp.SystemBuffer );

    if( pIn->Version != IBAT_IOCTL_VERSION )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid version.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    IoMarkIrpPending( pIrp );
    pIrp->IoStatus.Status = IbatQueryPathByIpAddress(
        &pIn->LocalAddress,
        &pIn->RemoteAddress,
        __ibat_query_cb,
        pIrp,
        pOut
        );
    if( pIrp->IoStatus.Status == STATUS_SUCCESS )
    {
        pIrp->IoStatus.Information = sizeof(*pOut);
        IoCompleteRequest( pIrp, IO_NO_INCREMENT );
    }
    else if( pIrp->IoStatus.Status != STATUS_PENDING )
    {
        pIrp->IoStatus.Information = 0;
        IoCompleteRequest( pIrp, IO_NO_INCREMENT );
    }

    AL_EXIT( AL_DBG_DEV );
    return STATUS_PENDING;
}


static NTSTATUS
__ibat_ip_to_port(
    IN              IRP                         *pIrp,
    IN              IO_STACK_LOCATION           *pIoStack )
{
    IOCTL_IBAT_IP_TO_PORT_IN    *pIn;
    IOCTL_IBAT_IP_TO_PORT_OUT   *pOut;
    NTSTATUS                    status;

    AL_ENTER(AL_DBG_DEV);

    if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
        sizeof(IOCTL_IBAT_IP_TO_PORT_IN) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid input buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength !=
        sizeof(IOCTL_IBAT_IP_TO_PORT_OUT) )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid output buffer size.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    pIn = (IOCTL_IBAT_IP_TO_PORT_IN *) pIrp->AssociatedIrp.SystemBuffer;
    pOut = (IOCTL_IBAT_IP_TO_PORT_OUT *) pIrp->AssociatedIrp.SystemBuffer;

    if( pIn->Version != IBAT_IOCTL_VERSION )
    {
        AL_PRINT_EXIT( TRACE_LEVEL_ERROR, AL_DBG_ERROR,
            ("Invalid version.\n") );
        return STATUS_INVALID_PARAMETER;
    }

    status = IbatIpToPort( &pIn->Address, NULL, &pOut->Port );
    if( status == STATUS_SUCCESS )
    {
        pIrp->IoStatus.Information = sizeof(*pOut);
    }
    else
    {
        if( status == STATUS_INVALID_ADDRESS )
        {
            status = STATUS_NOT_FOUND;
        }
        pIrp->IoStatus.Information = 0;
    }

    AL_EXIT( AL_DBG_DEV );
    return status;
}


extern "C"
NTSTATUS
ibat_ioctl(
    __in IRP* pIrp
    )
{
    IO_STACK_LOCATION   *pIoStack;
    NTSTATUS            status = STATUS_SUCCESS;

    AL_ENTER( AL_DBG_DEV );

    pIoStack = IoGetCurrentIrpStackLocation( pIrp );

    pIrp->IoStatus.Information = 0;

    switch( pIoStack->Parameters.DeviceIoControl.IoControlCode )
    {
    case IOCTL_IBAT_IP_ADDRESSES:
        AL_PRINT(TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
            ("IOCTL_IBAT_IP_ADDRESSES received\n" ));
        status = __ibat_get_ips( pIrp, pIoStack );
        break;

    case IOCTL_IBAT_IP_TO_PORT:
        AL_PRINT(TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
            ("IOCTL_IBAT_IP_TO_PORT received\n" ));
        status = __ibat_ip_to_port( pIrp, pIoStack );
        break;

    case IOCTL_IBAT_QUERY_PATH:
        AL_PRINT(TRACE_LEVEL_INFORMATION, AL_DBG_DEV,
            ("IOCTL_IBAT_QUERY_PATH received\n" ));
        status = __ibat_query_path( pIrp, pIoStack );
        break;

    default:
        AL_PRINT( TRACE_LEVEL_VERBOSE, AL_DBG_DEV,
            ("unknow IOCTL code = 0x%x\n",
            pIoStack->Parameters.DeviceIoControl.IoControlCode) );
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    if( status != STATUS_PENDING )
    {
        pIrp->IoStatus.Status = status;
        IoCompleteRequest( pIrp, IO_NO_INCREMENT );
    }

    AL_EXIT( AL_DBG_DEV );
    return status;
}


