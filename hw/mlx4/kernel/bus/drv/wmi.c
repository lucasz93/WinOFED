/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    WMI.C

Abstract:

    This module handles all the WMI Irps.

Environment:

    Kernel mode

--*/

#include "precomp.h"

#if defined (EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "wmi.tmh"
#endif 

NTSTATUS
WmiRegistration(
	WDFDEVICE      Device
	)
/*++
Routine Description

    Registers with WMI as a data provider for this
    instance of the device

--*/
{
	WDF_WMI_PROVIDER_CONFIG providerConfig;
	WDF_WMI_INSTANCE_CONFIG instanceConfig;
	NTSTATUS status;
	DECLARE_CONST_UNICODE_STRING(busRsrcName, BUSRESOURCENAME);

	//
	// Register WMI classes.
	// First specify the resource name which contain the binary mof resource.
	//
	status = WdfDeviceAssignMofResourceName(Device, &busRsrcName);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDF_WMI_PROVIDER_CONFIG_INIT(&providerConfig, &MLX4_BUS_WMI_STD_DATA_GUID);
	providerConfig.MinInstanceBufferSize = sizeof(BUS_WMI_STD_DATA);

	//
	// You would want to create a WDFWMIPROVIDER handle separately if you are
	// going to dynamically create instances on the provider.  Since we are
	// statically creating one instance, there is no need to create the provider
	// handle.
	//
	WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER_CONFIG(&instanceConfig, &providerConfig);

	//
	// By setting Register to TRUE, we tell the framework to create a provider
	// as part of the Instance creation call. This eliminates the need to
	// call WdfWmiProviderRegister.
	//
	instanceConfig.Register = TRUE;
	instanceConfig.EvtWmiInstanceQueryInstance = EvtStdDataQueryInstance;
	instanceConfig.EvtWmiInstanceSetInstance = EvtStdDataSetInstance;
	instanceConfig.EvtWmiInstanceSetItem = EvtStdDataSetItem;

	status = WdfWmiInstanceCreate( 	Device,
		&instanceConfig, WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE );

	return status;
}

//
// WMI System Call back functions
//
NTSTATUS
EvtStdDataSetItem(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG DataItemId,
	IN  ULONG InBufferSize,
	IN  PVOID InBuffer
	)
/*++

Routine Description:

	This routine is a callback into the driver to set for the contents of
	an instance.

Arguments:

	WmiInstance is the instance being set

	DataItemId has the id of the data item being set

	InBufferSize has the size of the data item passed

	InBuffer has the new values for the data item

Return Value:

	status

--*/
{
	PFDO_DEVICE_DATA fdoData;

	fdoData = FdoGetData(WdfWmiInstanceGetDevice(WmiInstance));

	switch(DataItemId)
	{
	case 1:
		if (InBufferSize < sizeof(ULONG)) {
			return STATUS_BUFFER_TOO_SMALL;
		}
		g_mlx4_dbg_level = fdoData->WmiData.DebugPrintLevel = *((PULONG)InBuffer);
		return STATUS_SUCCESS;

	case 2:
		if (InBufferSize < sizeof(ULONG)) {
			return STATUS_BUFFER_TOO_SMALL;
		}
		g_mlx4_dbg_flags = fdoData->WmiData.DebugPrintFlags = *((PULONG)InBuffer);
		return STATUS_SUCCESS;

	default:
		return STATUS_WMI_READ_ONLY;
	}
}

NTSTATUS
EvtStdDataSetInstance(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG InBufferSize,
	IN  PVOID InBuffer
	)
/*++

Routine Description:

	This routine is a callback into the driver to set for the contents of
	an instance.

Arguments:

	WmiInstance is the instance being set

	BufferSize has the size of the data block passed

	Buffer has the new values for the data block

Return Value:

	status

--*/
{
	PFDO_DEVICE_DATA   fdoData;

	UNREFERENCED_PARAMETER(InBufferSize);

	fdoData = FdoGetData(WdfWmiInstanceGetDevice(WmiInstance));

	//
	// We will update only writable elements.
	//
	g_mlx4_dbg_level = fdoData->WmiData.DebugPrintLevel = ((PBUS_WMI_STD_DATA)InBuffer)->DebugPrintLevel;
	g_mlx4_dbg_flags = fdoData->WmiData.DebugPrintFlags = ((PBUS_WMI_STD_DATA)InBuffer)->DebugPrintFlags;

	return STATUS_SUCCESS;
}

NTSTATUS
EvtStdDataQueryInstance(
	IN  WDFWMIINSTANCE WmiInstance,
	IN  ULONG OutBufferSize,
	IN  PVOID OutBuffer,
	OUT PULONG BufferUsed
	)
/*++

Routine Description:

	This routine is a callback into the driver to set for the contents of
	a wmi instance

Arguments:

	WmiInstance is the instance being set

	OutBufferSize on has the maximum size available to write the data
		block.

	OutBuffer on return is filled with the returned data block

	BufferUsed pointer containing how many bytes are required (upon failure) or
		how many bytes were used (upon success)

Return Value:

	status

--*/
{
	PFDO_DEVICE_DATA fdoData;

	UNREFERENCED_PARAMETER(OutBufferSize);

	fdoData = FdoGetData(WdfWmiInstanceGetDevice(WmiInstance));

	*BufferUsed = sizeof (BUS_WMI_STD_DATA);
	* (PBUS_WMI_STD_DATA) OutBuffer = fdoData->WmiData;

	return STATUS_SUCCESS;
}

