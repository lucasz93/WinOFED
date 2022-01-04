/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ntddk.h>
#include <wdf.h>
#include <wdmsec.h>
#include <ntstatus.h>
#include <initguid.h>

#include "wm_ioctl.h"
#include "wm_driver.h"
#include "wm_provider.h"
#include "wm_reg.h"
#include <event_trace.h>

ET_POST_EVENT 			g_post_event_func = NULL;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WM_IB_DEVICE, WmIbDeviceGetContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WM_PROVIDER, WmProviderGetContext)

WDFDEVICE				ControlDevice = NULL;
static LIST_ENTRY		DevList;
static LIST_ENTRY		ProvList;
static KGUARDED_MUTEX	Lock;

static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL	WmIoDeviceControl;
static EVT_WDF_IO_QUEUE_IO_READ				WmIoRead;
static EVT_WDF_IO_QUEUE_IO_WRITE			WmIoWrite;
static EVT_WDF_DEVICE_FILE_CREATE			WmFileCreate;
static EVT_WDF_FILE_CLEANUP					WmFileCleanup;
static EVT_WDF_FILE_CLOSE					WmFileClose;

static WM_IB_DEVICE *WmIbDeviceFind(NET64 Guid)
{
	WM_IB_DEVICE	*cur_dev, *dev = NULL;
	LIST_ENTRY		*entry;

	for (entry = DevList.Flink; entry != &DevList; entry = entry->Flink) {
		cur_dev = CONTAINING_RECORD(entry, WM_IB_DEVICE, Entry);
		if (cur_dev->Guid == Guid) {
			dev = cur_dev;
			break;
		}
	}
	return dev;
}

WM_IB_DEVICE *WmIbDeviceGet(NET64 Guid)
{
	WM_IB_DEVICE *dev;

	KeAcquireGuardedMutex(&Lock);
	dev = WmIbDeviceFind(Guid);
	if (dev != NULL) {
			InterlockedIncrement(&dev->Ref);
	}
	KeReleaseGuardedMutex(&Lock);
	return dev;
}

void WmIbDevicePut(WM_IB_DEVICE *pDevice)
{
	if (InterlockedDecrement(&pDevice->Ref) == 0) {
		KeSetEvent(&pDevice->Event, 0, FALSE);
	}
}

static VOID WmIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
							  size_t OutLen, size_t InLen, ULONG IoControlCode)
{
	WDFFILEOBJECT	file;
	WM_PROVIDER		*prov;
	UNREFERENCED_PARAMETER(OutLen);
	UNREFERENCED_PARAMETER(InLen);
	UNREFERENCED_PARAMETER(Queue);

	file = WdfRequestGetFileObject(Request);
	prov = WmProviderGetContext(file);
	if (WmProviderGet(prov) == 0) {
		g_post_event("WinMad: Device closed - reject ioctl %#x \n", IoControlCode);
		WdfRequestComplete(Request, STATUS_FILE_CLOSED);
		return;
	}

	switch (IoControlCode) {
	case WM_IOCTL_REGISTER:
		WmRegister(prov, Request);
		break;
	case WM_IOCTL_DEREGISTER:
		WmDeregister(prov, Request);
		break;
	case WM_IOCTL_CANCEL:
		WmProviderCancel(prov, Request);
		break;
	default:
		WdfRequestComplete(Request, STATUS_PROCEDURE_NOT_FOUND);
		break;
	}

	WmProviderPut(prov);
}

static VOID WmIoRead(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
	WDFFILEOBJECT	file;
	WM_PROVIDER		*prov;
	UNREFERENCED_PARAMETER(Queue);

	file = WdfRequestGetFileObject(Request);
	prov = WmProviderGetContext(file);
	if (WmProviderGet(prov) > 0) {
		WmProviderRead(prov, Request);
		WmProviderPut(prov);
	} else {
		g_post_event("WinMad: Device closed - reject WmIoRead\n");
		WdfRequestCompleteWithInformation(Request, STATUS_FILE_CLOSED, 0);
	}
}

static VOID WmIoWrite(WDFQUEUE Queue, WDFREQUEST Request, size_t Length)
{
	WDFFILEOBJECT	file;
	WM_PROVIDER		*prov;
	UNREFERENCED_PARAMETER(Queue);

	file = WdfRequestGetFileObject(Request);
	prov = WmProviderGetContext(file);
	if (WmProviderGet(prov) > 0) {
		WmProviderWrite(prov, Request);
		WmProviderPut(prov);
	} else {
		g_post_event("WinMad: Device closed - reject WmIoWrite\n");
		WdfRequestComplete(Request, STATUS_FILE_CLOSED);
	}
}

static VOID WmFileCreate(WDFDEVICE Device, WDFREQUEST Request,
						 WDFFILEOBJECT FileObject)
{
	WM_PROVIDER	*prov = WmProviderGetContext(FileObject);
	NTSTATUS status;
	UNREFERENCED_PARAMETER(Device);

	status = WmProviderInit(prov);
	if (NT_SUCCESS(status)) {
		KeAcquireGuardedMutex(&Lock);
		InsertHeadList(&ProvList, &prov->Entry);
		KeReleaseGuardedMutex(&Lock);
	}
	WdfRequestComplete(Request, status);
}

static VOID WmFileCleanup(WDFFILEOBJECT FileObject)
{
	LIST_ENTRY *entry;
	WM_PROVIDER *prov = WmProviderGetContext(FileObject);
	WM_PROVIDER *prov_entry;

	KeAcquireGuardedMutex(&Lock);
	for (entry = ProvList.Flink; entry != &ProvList; entry = entry->Flink) {
		prov_entry = CONTAINING_RECORD(entry, WM_PROVIDER, Entry);
		if (prov == prov_entry) {
			RemoveEntryList(&prov->Entry);
			goto found;
		}
	}
	prov = NULL;
found:
	KeReleaseGuardedMutex(&Lock);
	if (prov) {
		WmProviderCleanup(prov);
	}
}

static VOID WmFileClose(WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(FileObject);
}

static VOID WmCreateControlDevice(WDFDRIVER Driver)
{
	PWDFDEVICE_INIT			pinit;
	WDF_FILEOBJECT_CONFIG	fileconfig;
	WDF_OBJECT_ATTRIBUTES	attr;
	WDF_IO_QUEUE_CONFIG		ioconfig;
	NTSTATUS				status;
	WDFQUEUE				queue;
	DECLARE_CONST_UNICODE_STRING(name, L"\\Device\\WinMad");
	DECLARE_CONST_UNICODE_STRING(symlink, L"\\DosDevices\\WinMad");

	pinit = WdfControlDeviceInitAllocate(Driver,
										 &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R);
	if (pinit == NULL) {
			return;
	}

	WdfDeviceInitSetExclusive(pinit, FALSE);
	status = WdfDeviceInitAssignName(pinit, &name);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	WDF_FILEOBJECT_CONFIG_INIT(&fileconfig, WmFileCreate, WmFileClose,
							   WmFileCleanup);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WM_PROVIDER);
	WdfDeviceInitSetFileObjectConfig(pinit, &fileconfig, &attr);

	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	ASSERT(ControlDevice == NULL);
	status = WdfDeviceCreate(&pinit, &attr, &ControlDevice);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WINMAD: WmCreateControlDevice:  Device Create. ControlDevice = %p\n",  ControlDevice);
	ASSERT(ControlDevice != NULL);
	status = WdfDeviceCreateSymbolicLink(ControlDevice, &symlink);
	if (!NT_SUCCESS(status)) {
		WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"WINMAD: WdfDeviceCreateSymbolicLink was failed. status=0x%x\n",  status);
		goto err2;
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioconfig, WdfIoQueueDispatchParallel);
	ioconfig.EvtIoDeviceControl = WmIoDeviceControl;
	ioconfig.EvtIoRead = WmIoRead;
	ioconfig.EvtIoWrite = WmIoWrite;
	status = WdfIoQueueCreate(ControlDevice, &ioconfig,
							  WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(status)) {
		WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"WINMAD: WdfIoQueueCreate was failed. status=0x%x\n",  status);
		goto err2;
	}

	WdfControlFinishInitializing(ControlDevice);
	return;

err2:
	WdfObjectDelete(ControlDevice);
	ControlDevice = NULL;
	
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WINMAD: WmCreateControlDevice:  ERROR set ControlDevice = %p\n",  ControlDevice);
	return;
err1:
	WdfDeviceInitFree(pinit);
}

static ib_ca_attr_t *WmQueryCaAttributes(WM_IB_DEVICE *pDevice)
{
	ib_ca_attr_t	*attr;
	UINT32			size;
	ib_api_status_t	ib_status;

	size = 0;
	ib_status = pDevice->VerbsInterface.Verbs.
				query_ca(pDevice->VerbsInterface.Verbs.p_hca_obj, NULL, &size, NULL);
	if (ib_status != IB_INSUFFICIENT_MEMORY) {
		attr = NULL;
		goto out;
	}

	attr = ExAllocatePoolWithTag(PagedPool, size, 'acmw');
	if (attr == NULL) {
		goto out;
	}

	ib_status = pDevice->VerbsInterface.Verbs.
				query_ca(pDevice->VerbsInterface.Verbs.p_hca_obj, attr, &size, NULL);
	if (ib_status != IB_SUCCESS) {
		ExFreePoolWithTag(attr, 'acmw');
		attr = NULL;
	}

out:
	return attr;
}

static NTSTATUS WmAddCa(WM_IB_DEVICE *pDevice)
{
	NTSTATUS		status;
	ib_api_status_t ib_status;
	ib_ca_attr_t	*attr;
	UINT32			size;
	UINT8			i;

	attr = WmQueryCaAttributes(pDevice);
	if (attr == NULL) {
		return STATUS_NO_MEMORY;
	}

	size = sizeof(WM_IB_PORT) * attr->num_ports;
	pDevice->pPortArray = ExAllocatePoolWithTag(PagedPool, size, 'pimw');
	if (pDevice->pPortArray == NULL) {
		status = STATUS_NO_MEMORY;
		goto out;
	}

	for (i = 0; i < attr->num_ports; i++) {
		pDevice->pPortArray[i].Guid = attr->p_port_attr[i].port_guid;
	}
	pDevice->PortCount = attr->num_ports;

	status = STATUS_SUCCESS;
out:
	ExFreePoolWithTag(attr, 'acmw');
	return status;
}

EVT_WDF_DEVICE_D0_ENTRY WmPowerD0Entry;
static NTSTATUS WmPowerD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
	WM_IB_DEVICE	*dev;
	BOOLEAN			create;
	NTSTATUS		status;

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WmPowerD0Entry\n");

	dev = WmIbDeviceGetContext(Device);
	RtlZeroMemory(dev, sizeof *dev);
	dev->Ref = 1;
	KeInitializeEvent(&dev->Event, NotificationEvent, FALSE);

	status = WdfFdoQueryForInterface(Device, &GUID_RDMA_INTERFACE_VERBS,
									 (PINTERFACE) &dev->VerbsInterface,
									 sizeof(dev->VerbsInterface), RDMA_INTERFACE_VERBS_VERSION,
									 NULL);
	if ( status == STATUS_NOT_SUPPORTED ) {
		status = STATUS_SUCCESS;
		DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, "No IB ports - WinMad is started in non-operational mode\n" );
		goto exit;
	}
	if (!NT_SUCCESS(status)) {
		goto exit;
	} else {
		if( RDMA_INTERFACE_VERBS_VERSION != dev->VerbsInterface.InterfaceHeader.Version )
		{
			WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WinMad: GUID_RDMA_INTERFACE_VERBS version mismatch: requested version=%d, returned version=%d.\n",
				RDMA_INTERFACE_VERBS_VERSION, dev->VerbsInterface.InterfaceHeader.Version);
	
			dev->VerbsInterface.InterfaceHeader.InterfaceDereference(dev->VerbsInterface.InterfaceHeader.Context);
			status = STATUS_NOT_SUPPORTED;
			goto exit;
		}
	}

	g_post_event_func = dev->VerbsInterface.post_event;
	g_post_event("WinMad: WmPowerD0Entry started \n");
	dev->Guid = dev->VerbsInterface.Verbs.guid;
	status = WmAddCa(dev);

	dev->VerbsInterface.InterfaceHeader.InterfaceDereference(dev->VerbsInterface.
															 InterfaceHeader.Context);
	if (!NT_SUCCESS(status)) {
		goto exit;
	}

	status = WdfFdoQueryForInterface(Device, &GUID_IB_AL_INTERFACE,
									 (PINTERFACE) &dev->IbInterface,
									 sizeof(dev->IbInterface),
									 AL_INTERFACE_VERSION, NULL);
	if (!NT_SUCCESS(status)) {
		goto exit;
	} else {
		if( AL_INTERFACE_VERSION != dev->IbInterface.wdm.Version )
		{
			WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WinMad: GUID_IB_AL_INTERFACE version mismatch: requested version=%d, returned version=%d.\n",
				AL_INTERFACE_VERSION, dev->IbInterface.wdm.Version);
	
			dev->IbInterface.wdm.InterfaceDereference(dev->IbInterface.wdm.Context);
			status = STATUS_NOT_SUPPORTED;
			goto exit;
		}
	}
	g_post_event("WinMad: Taken GUID_IB_AL_INTERFACE interface \n");


	KeAcquireGuardedMutex(&Lock);
	create = IsListEmpty(&DevList);
	InsertHeadList(&DevList, &dev->Entry);
	KeReleaseGuardedMutex(&Lock);

	if (create) {
		WmCreateControlDevice(WdfGetDriver());
	}
	
exit:	
	g_post_event("WinMad: WmPowerD0Entry ended with status %#x \n", status);
	return status;
}

EVT_WDF_DEVICE_D0_EXIT WmPowerD0Exit;
static NTSTATUS WmPowerD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
	WM_PROVIDER			*prov;
	WM_IB_DEVICE		*pdev;
	WM_REGISTRATION		*reg;
	LIST_ENTRY			*entry;
	BOOLEAN				destroy;
	WDFDEVICE			ctrldev = NULL;

	g_post_event("WinMad: WmPowerD0Exit started\n");
	
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WmPowerD0Exit enter with TargetState %d\n", TargetState);

	pdev = WmIbDeviceGetContext(Device);

	if ( !pdev->PortCount )
		goto exit;

	KeAcquireGuardedMutex(&Lock);
	RemoveEntryList(&pdev->Entry);
	destroy = IsListEmpty(&DevList);

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WmPowerD0Exit ProvList is empty %d\n", IsListEmpty(&ProvList));
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WINAMD: WmPowerD0Exit:  destroy =%d, ControlDevice = %p\n",  destroy, ControlDevice);

	if (destroy) {
		ctrldev = ControlDevice;
		ControlDevice = NULL;
	}

	for (entry = ProvList.Flink; entry != &ProvList; entry = entry->Flink) {
		prov = CONTAINING_RECORD(entry, WM_PROVIDER, Entry);
		WmProviderRemoveHandler(prov, pdev);
	} 
	KeReleaseGuardedMutex(&Lock);

	if (InterlockedDecrement(&pdev->Ref) > 0) {
		KeWaitForSingleObject(&pdev->Event, Executive, KernelMode, FALSE, NULL);
	}

	pdev->IbInterface.wdm.InterfaceDereference(pdev->IbInterface.wdm.Context);
	g_post_event("WinMad: Released GUID_IB_AL_INTERFACE interface \n");
	if (pdev->pPortArray != NULL) {
		ExFreePoolWithTag(pdev->pPortArray, 'pimw');
	}

	if (destroy && (ctrldev != NULL)) {
		WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"WINAMD: WmPowerD0Exit:  Delete object ControlDevice = %p\n",  ControlDevice);
		WdfObjectDelete(ctrldev);
	}

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WmPowerD0Exit exit\n");

exit:
	g_post_event("WinMad: WmPowerD0Exit ended\n");
	return STATUS_SUCCESS;
}

EVT_WDF_DRIVER_DEVICE_ADD WmIbDeviceAdd;
static NTSTATUS WmIbDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
	WDF_OBJECT_ATTRIBUTES			attr;
	WDF_PNPPOWER_EVENT_CALLBACKS	power;
	WDFDEVICE						dev;
	NTSTATUS						status;

	WdfFdoInitSetFilter(DeviceInit);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WM_IB_DEVICE);
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
	power.EvtDeviceD0Entry = WmPowerD0Entry;
	power.EvtDeviceD0Exit = WmPowerD0Exit;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &power);

	status = WdfDeviceCreate(&DeviceInit, &attr, &dev);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	return STATUS_SUCCESS;
}

DRIVER_INITIALIZE DriverEntry;
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	WDF_DRIVER_CONFIG		config;
	NTSTATUS				status;
	WDFDRIVER				driv;

	InitializeListHead(&DevList);
	InitializeListHead(&ProvList);
	KeInitializeGuardedMutex(&Lock);

	WDF_DRIVER_CONFIG_INIT(&config, WmIbDeviceAdd);
	status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
							 &config, &driv);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	return STATUS_SUCCESS;
}
