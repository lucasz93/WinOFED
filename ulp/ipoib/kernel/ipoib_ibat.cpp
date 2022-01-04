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
 */

#include <precompile.h>


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "ipoib_ibat.tmh"
#endif
#include <iba/ib_at_ioctl.h>

extern PDRIVER_OBJECT				g_p_drv_obj;

static DRIVER_DISPATCH __ipoib_create;
static NTSTATUS
__ipoib_create(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp );

static DRIVER_DISPATCH __ipoib_cleanup;
static NTSTATUS
__ipoib_cleanup(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp );

static DRIVER_DISPATCH __ipoib_close;
static NTSTATUS
__ipoib_close(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp );

static DRIVER_DISPATCH __ipoib_dispatch;
static NTSTATUS
__ipoib_dispatch(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp );


static NTSTATUS
__ibat_get_ports(
	IN				IRP							*pIrp,
	IN				IO_STACK_LOCATION			*pIoStack )
{
	IOCTL_IBAT_PORTS_IN		*pIn;
	IOCTL_IBAT_PORTS_OUT	*pOut;
	KLOCK_QUEUE_HANDLE		hdl;
	cl_list_item_t			*pItem;
	ipoib_adapter_t			*pAdapter;
	LONG					nPorts;

	IPOIB_ENTER(IPOIB_DBG_IOCTL);

	if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(IOCTL_IBAT_PORTS_IN) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid input buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}
	
	if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength <
		sizeof(IOCTL_IBAT_PORTS_OUT) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid output buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	pIn = (IOCTL_IBAT_PORTS_IN *) pIrp->AssociatedIrp.SystemBuffer;
	pOut = (IOCTL_IBAT_PORTS_OUT *) pIrp->AssociatedIrp.SystemBuffer;

	if( pIn->Version != IBAT_IOCTL_VERSION )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid version.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	nPorts = (LONG)cl_qlist_count( &g_ipoib.adapter_list );
	switch( nPorts )
	{
	case 0:
		memset( pOut->Ports, 0, sizeof(pOut->Ports) );
		/* Fall through */
	case 1:
		pOut->Size = sizeof(IOCTL_IBAT_PORTS_OUT);
		break;

	default:
		pOut->Size = sizeof(IOCTL_IBAT_PORTS_OUT) + 
			(sizeof(IBAT_PORT_RECORD) * (nPorts - 1));
		break;
	}

	pIrp->IoStatus.Information = pOut->Size;

	if( pOut->Size > pIoStack->Parameters.DeviceIoControl.OutputBufferLength )
	{
		nPorts = 1 +
			(pIoStack->Parameters.DeviceIoControl.OutputBufferLength -
			sizeof(IOCTL_IBAT_PORTS_OUT)) / sizeof(IBAT_PORT_RECORD);

		pIrp->IoStatus.Information = sizeof(IOCTL_IBAT_PORTS_OUT) +
			((nPorts - 1) * sizeof(IBAT_PORT_RECORD));
	}

	pOut->NumPorts = 0;
	pItem = cl_qlist_head( &g_ipoib.adapter_list );
	while( pOut->NumPorts != nPorts )
	{
		pAdapter = CONTAINING_RECORD( pItem, ipoib_adapter_t, entry );
		pOut->Ports[pOut->NumPorts].CaGuid = pAdapter->guids.ca_guid;
		pOut->Ports[pOut->NumPorts].PortGuid = pAdapter->guids.port_guid.guid;
		pOut->Ports[pOut->NumPorts].PKey = IB_DEFAULT_PKEY;
		pOut->Ports[pOut->NumPorts].PortNum = pAdapter->guids.port_num;
		pOut->NumPorts++;

		pItem = cl_qlist_next( pItem );
	}

	KeReleaseInStackQueuedSpinLock( &hdl );
	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return STATUS_SUCCESS;
}


static NTSTATUS
__ibat_get_ips(
	IN				IRP							*pIrp,
	IN				IO_STACK_LOCATION			*pIoStack )
{
	IOCTL_IBAT_IP_ADDRESSES_IN	*pIn;
	IOCTL_IBAT_IP_ADDRESSES_OUT	*pOut;
	KLOCK_QUEUE_HANDLE			hdl;
	cl_list_item_t				*pItem;
	ipoib_adapter_t				*pAdapter;
	LONG						nIps, maxIps;
	size_t						idx;
	net_address_item_t			*pAddr;
	UINT64						PortGuid;

	IPOIB_ENTER(IPOIB_DBG_IOCTL);

	if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(IOCTL_IBAT_IP_ADDRESSES_IN) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid input buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}
	
	if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength <
		sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid output buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	pIn = (IOCTL_IBAT_IP_ADDRESSES_IN *) pIrp->AssociatedIrp.SystemBuffer;
	pOut = (IOCTL_IBAT_IP_ADDRESSES_OUT *) pIrp->AssociatedIrp.SystemBuffer;

	if( pIn->Version != IBAT_IOCTL_VERSION )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid version.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	PortGuid = pIn->PortGuid;

	nIps = 0;
	pOut->AddressCount = 0;
	maxIps = 1 +
		((pIoStack->Parameters.DeviceIoControl.OutputBufferLength -
		sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT)) / sizeof(IP_ADDRESS));

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	for( pItem = cl_qlist_head( &g_ipoib.adapter_list );
		pItem != cl_qlist_end( &g_ipoib.adapter_list );
		pItem = cl_qlist_next( pItem ) )
	{
		pAdapter = CONTAINING_RECORD( pItem, ipoib_adapter_t, entry );
		if( PortGuid && pAdapter->guids.port_guid.guid != PortGuid )
			continue;

		cl_obj_lock( &pAdapter->obj );
		nIps += (LONG)cl_vector_get_size( &pAdapter->ip_vector );

		for( idx = 0;
			idx < cl_vector_get_size( &pAdapter->ip_vector );
			idx++ )
		{
			if( pOut->AddressCount == maxIps )
				break;

			pAddr = (net_address_item_t*)
				cl_vector_get_ptr( &pAdapter->ip_vector, idx );

			pOut->Address[pOut->AddressCount].IpVersion = 4;
			memset( &pOut->Address[pOut->AddressCount].Address, 0, sizeof(IP_ADDRESS) );
			memcpy( &pOut->Address[pOut->AddressCount].Address[12],
					pAddr->address.as_bytes, IPV4_ADDR_SIZE );

			pOut->AddressCount++;
		}
		cl_obj_unlock( &pAdapter->obj );
	}

	pOut->Size = sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT);
	if( --nIps )
		pOut->Size += sizeof(IP_ADDRESS) * nIps;

	pIrp->IoStatus.Information = sizeof(IOCTL_IBAT_IP_ADDRESSES_OUT);
	if( --maxIps < nIps )
		pIrp->IoStatus.Information += (sizeof(IP_ADDRESS) * maxIps);
	else
		pIrp->IoStatus.Information += (sizeof(IP_ADDRESS) * nIps);

	KeReleaseInStackQueuedSpinLock( &hdl );
	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return STATUS_SUCCESS;
}


static NTSTATUS
__ibat_mac_to_gid(
	IN				IRP							*pIrp,
	IN				IO_STACK_LOCATION			*pIoStack )
{
	NTSTATUS					status = STATUS_INVALID_PARAMETER;
	IOCTL_IBAT_MAC_TO_GID_IN	*pIn;
	IOCTL_IBAT_MAC_TO_GID_OUT	*pOut;
	KLOCK_QUEUE_HANDLE			hdl;
	cl_list_item_t				*pItem;
	ipoib_adapter_t				*pAdapter;

	IPOIB_ENTER(IPOIB_DBG_IOCTL);

	if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(IOCTL_IBAT_MAC_TO_GID_IN) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid input buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}
	
	if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength !=
		sizeof(IOCTL_IBAT_MAC_TO_GID_OUT) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid output buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	pIn = (IOCTL_IBAT_MAC_TO_GID_IN	*) pIrp->AssociatedIrp.SystemBuffer;
	pOut = (IOCTL_IBAT_MAC_TO_GID_OUT *) pIrp->AssociatedIrp.SystemBuffer;

	if( pIn->Version != IBAT_IOCTL_VERSION )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid version.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );

	for( pItem = cl_qlist_head( &g_ipoib.adapter_list );
		pItem != cl_qlist_end( &g_ipoib.adapter_list );
		pItem = cl_qlist_next( pItem ) )
	{
		pAdapter = CONTAINING_RECORD( pItem, ipoib_adapter_t, entry );
		if( pIn->PortGuid != pAdapter->guids.port_guid.guid )
			continue;

		/* Found the port - lookup the MAC. */
		cl_obj_lock( &pAdapter->obj );
		if( pAdapter->p_port )
		{
			status = ipoib_mac_to_gid(
				pAdapter->p_port, *(mac_addr_t*)pIn->DestMac, &pOut->DestGid );
			if( NT_SUCCESS( status ) )
			{
				pIrp->IoStatus.Information =
					sizeof(IOCTL_IBAT_MAC_TO_GID_OUT);
			}
		}
		cl_obj_unlock( &pAdapter->obj );
		break;
	}

	KeReleaseInStackQueuedSpinLock( &hdl );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return status;
}


static NTSTATUS
__ibat_mac_to_path(
	IN				IRP							*pIrp,
	IN				IO_STACK_LOCATION			*pIoStack )
{
	NTSTATUS					status = STATUS_INVALID_PARAMETER;
	IOCTL_IBAT_MAC_TO_PATH_IN	*pIn;
	IOCTL_IBAT_MAC_TO_PATH_OUT	*pOut;
	KLOCK_QUEUE_HANDLE			hdl;
	cl_list_item_t				*pItem;
	ipoib_adapter_t				*pAdapter;

	IPOIB_ENTER(IPOIB_DBG_IOCTL);

	if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(IOCTL_IBAT_MAC_TO_PATH_IN) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid input buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}
	
	if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength !=
		sizeof(IOCTL_IBAT_MAC_TO_PATH_OUT) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid output buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	pIn = (IOCTL_IBAT_MAC_TO_PATH_IN *) pIrp->AssociatedIrp.SystemBuffer;
	pOut = (IOCTL_IBAT_MAC_TO_PATH_OUT *) pIrp->AssociatedIrp.SystemBuffer;

	if( pIn->Version != IBAT_IOCTL_VERSION )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid version.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );

	for( pItem = cl_qlist_head( &g_ipoib.adapter_list );
		pItem != cl_qlist_end( &g_ipoib.adapter_list );
		pItem = cl_qlist_next( pItem ) )
	{
		pAdapter = CONTAINING_RECORD( pItem, ipoib_adapter_t, entry );
		if( pIn->PortGuid != pAdapter->guids.port_guid.guid )
			continue;

		/* Found the port - lookup the MAC. */
		cl_obj_lock( &pAdapter->obj );
		if( pAdapter->p_port )
		{
			status = ipoib_mac_to_path(
				pAdapter->p_port, *(mac_addr_t*)pIn->DestMac, &pOut->Path );

			if( NT_SUCCESS( status ) )
			{
				pIrp->IoStatus.Information =
					sizeof(IOCTL_IBAT_MAC_TO_PATH_OUT);
			}
		}
		cl_obj_unlock( &pAdapter->obj );
		break;
	}

	KeReleaseInStackQueuedSpinLock( &hdl );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return status;
}


static NTSTATUS
__ibat_ip_to_port(
	IN				IRP							*pIrp,
	IN				IO_STACK_LOCATION			*pIoStack )
{
	IOCTL_IBAT_IP_TO_PORT_IN	*pIn;
	IOCTL_IBAT_IP_TO_PORT_OUT	*pOut;
	KLOCK_QUEUE_HANDLE			hdl;
	cl_list_item_t					*pItem;
	ipoib_adapter_t				*pAdapter;
	size_t						idx;
	net_address_item_t			*pAddr;
	NTSTATUS status = STATUS_NOT_FOUND;

	IPOIB_ENTER(IPOIB_DBG_IOCTL);

	if( pIoStack->Parameters.DeviceIoControl.InputBufferLength !=
		sizeof(IOCTL_IBAT_IP_TO_PORT_IN) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid input buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}
	
	if( pIoStack->Parameters.DeviceIoControl.OutputBufferLength !=
		sizeof(IOCTL_IBAT_IP_TO_PORT_OUT) )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid output buffer size.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	pIn = (IOCTL_IBAT_IP_TO_PORT_IN	*) pIrp->AssociatedIrp.SystemBuffer;
	pOut = (IOCTL_IBAT_IP_TO_PORT_OUT *) pIrp->AssociatedIrp.SystemBuffer;

	if( pIn->Version != IBAT_IOCTL_VERSION )
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid version.\n") );
		return STATUS_INVALID_PARAMETER;
	}

	if (pIn->Address.IpVersion != 4)
	{
		IPOIB_PRINT_EXIT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR,
			("Invalid IP version (%d). Supported only 4\n", pIn->Address.IpVersion) );
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireInStackQueuedSpinLock( &g_ipoib.lock, &hdl );
	for( pItem = cl_qlist_head( &g_ipoib.adapter_list );
		pItem != cl_qlist_end( &g_ipoib.adapter_list );
		pItem = cl_qlist_next( pItem ) )
	{
		pAdapter = CONTAINING_RECORD( pItem, ipoib_adapter_t, entry );

		cl_obj_lock( &pAdapter->obj );

		for( idx = 0;
			idx < cl_vector_get_size( &pAdapter->ip_vector );
			idx++ )
		{
			pAddr = (net_address_item_t*)
				cl_vector_get_ptr( &pAdapter->ip_vector, idx );

			if (!memcmp( &pIn->Address.Address[12], pAddr->address.as_bytes, IPV4_ADDR_SIZE))
			{
				pOut->Port.CaGuid = pAdapter->guids.ca_guid;
				pOut->Port.PortGuid = pAdapter->guids.port_guid.guid;
				pOut->Port.PKey = IB_DEFAULT_PKEY;
				pOut->Port.PortNum = pAdapter->guids.port_num;
				pIrp->IoStatus.Information = sizeof(IOCTL_IBAT_IP_TO_PORT_OUT);
				status = STATUS_SUCCESS;
				break;
			}
		}
		cl_obj_unlock( &pAdapter->obj );
		if (status == STATUS_SUCCESS)
			break;
	}

	KeReleaseInStackQueuedSpinLock( &hdl );
	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return status;
}

void
ipoib_ref_ibat()
{
	UNICODE_STRING      DeviceName;
    UNICODE_STRING      DeviceLinkUnicodeString;
    NDIS_DEVICE_OBJECT_ATTRIBUTES   DeviceObjectAttributes;
    PDRIVER_DISPATCH    DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];

	NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	if( InterlockedIncrement( &g_ipoib.ibat_ref ) == 1 )
	{

		memset(DispatchTable, 0, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));
				
		DispatchTable[IRP_MJ_CREATE]					= __ipoib_create;
		DispatchTable[IRP_MJ_CLEANUP]					= __ipoib_cleanup;
		DispatchTable[IRP_MJ_CLOSE]						= __ipoib_close;
		DispatchTable[IRP_MJ_DEVICE_CONTROL]			= __ipoib_dispatch;
		DispatchTable[IRP_MJ_INTERNAL_DEVICE_CONTROL] 	= __ipoib_dispatch;		
		
				
		NdisInitUnicodeString( &DeviceName, IBAT_DEV_NAME );
		NdisInitUnicodeString( &DeviceLinkUnicodeString, IBAT_DOS_DEV_NAME );
				
		
		memset(&DeviceObjectAttributes, 0, sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES));
		
		DeviceObjectAttributes.Header.Type = NDIS_OBJECT_TYPE_DEFAULT; // type implicit from the context
		DeviceObjectAttributes.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
		DeviceObjectAttributes.Header.Size = sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES);
		DeviceObjectAttributes.DeviceName = &DeviceName;
		DeviceObjectAttributes.SymbolicName = &DeviceLinkUnicodeString;
		DeviceObjectAttributes.MajorFunctions = &DispatchTable[0];
		DeviceObjectAttributes.ExtensionSize = 0;
		DeviceObjectAttributes.DefaultSDDLString = NULL;
		DeviceObjectAttributes.DeviceClassGuid = 0;
		
		Status = NdisRegisterDeviceEx(
							g_IpoibMiniportDriverHandle,
							&DeviceObjectAttributes,
							&g_ipoib.h_ibat_dev,
							&g_ipoib.h_ibat_dev_handle);


	
		if( Status != NDIS_STATUS_SUCCESS )
		{
			IPOIB_PRINT( TRACE_LEVEL_ERROR, IPOIB_DBG_ERROR, 
				("NdisRegisterDeviceEx failed with status of %d\n", Status) );
		}
	}

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
}


void
ipoib_deref_ibat()
{
	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	if( InterlockedDecrement( &g_ipoib.ibat_ref ) )
	{
		IPOIB_EXIT( IPOIB_DBG_IOCTL );
		return;
	}

	if( g_ipoib.h_ibat_dev )
	{
		NdisDeregisterDeviceEx( g_ipoib.h_ibat_dev_handle );
		g_ipoib.h_ibat_dev = NULL;
		g_ipoib.h_ibat_dev_handle = NULL; //TODO set here INVALID_HANDLE_VALUE
	}

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
}


static NTSTATUS
__ipoib_create(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp )
{
	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	UNREFERENCED_PARAMETER( pDevObj );

	ipoib_ref_ibat();

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return STATUS_SUCCESS;
}


static NTSTATUS
__ipoib_cleanup(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp )
{
	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	UNREFERENCED_PARAMETER( pDevObj );

	ipoib_deref_ibat();

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return STATUS_SUCCESS;
}


static NTSTATUS
__ipoib_close(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp )
{
	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	UNREFERENCED_PARAMETER( pDevObj );

	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return STATUS_SUCCESS;
}


static NTSTATUS
__ipoib_dispatch(
	IN				DEVICE_OBJECT* const		pDevObj,
	IN				IRP* const					pIrp )
{
	IO_STACK_LOCATION	*pIoStack;
	NTSTATUS			status = STATUS_SUCCESS;

	IPOIB_ENTER( IPOIB_DBG_IOCTL );

	UNREFERENCED_PARAMETER( pDevObj );

	pIoStack = IoGetCurrentIrpStackLocation( pIrp );

	pIrp->IoStatus.Information = 0;

	switch( pIoStack->Parameters.DeviceIoControl.IoControlCode )
	{
	case IOCTL_IBAT_PORTS:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_IOCTL,
			("IOCTL_IBAT_PORTS received\n") );
		status = __ibat_get_ports( pIrp, pIoStack );
		break;

	case IOCTL_IBAT_IP_ADDRESSES:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_IOCTL,
			("IOCTL_IBAT_IP_ADDRESSES received\n" ));
		status = __ibat_get_ips( pIrp, pIoStack );
		break;

	case IOCTL_IBAT_MAC_TO_GID:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_IOCTL,
			("IOCTL_IBAT_MAC_TO_GID received\n" ));
		status = __ibat_mac_to_gid( pIrp, pIoStack );
		break;

	case IOCTL_IBAT_IP_TO_PORT:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_IOCTL,
			("IOCTL_IBAT_IP_TO_PORT received\n" ));
		status = __ibat_ip_to_port( pIrp, pIoStack );
		break;

	case IOCTL_IBAT_MAC_TO_PATH:
		IPOIB_PRINT(TRACE_LEVEL_INFORMATION, IPOIB_DBG_IOCTL,
			("IOCTL_IBAT_MAC_TO_PATH received\n" ));
		status = __ibat_mac_to_path( pIrp, pIoStack );
		break;

	default:
		IPOIB_PRINT( TRACE_LEVEL_VERBOSE, IPOIB_DBG_IOCTL,
			("unknow IOCTL code = 0x%x\n",
			pIoStack->Parameters.DeviceIoControl.IoControlCode) );
		status = STATUS_INVALID_PARAMETER;
	}

	pIrp->IoStatus.Status = status;
	IoCompleteRequest( pIrp, IO_NO_INCREMENT );

	IPOIB_EXIT( IPOIB_DBG_IOCTL );
	return status;
}


