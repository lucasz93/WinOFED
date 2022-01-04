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

#include "index_list.c"
#include "wv_driver.h"
#include "wv_ioctl.h"
#include "wv_provider.h"
#include "wv_device.h"
#include "wv_pd.h"
#include "wv_srq.h"
#include "wv_cq.h"
#include "wv_srq.h"
#include "wv_qp.h"
#include "wv_ep.h"
#include <event_trace.h>

#include <initguid.h>
#include <rdma/verbs.h>
#include <iba/ib_cm_ifc.h>
ET_POST_EVENT 			g_post_event_func = NULL;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WV_RDMA_DEVICE, WvRdmaDeviceGetContext)

WDFDEVICE				ControlDevice = NULL;
static LIST_ENTRY		DevList;
static LIST_ENTRY		ProvList;
static KGUARDED_MUTEX	Lock;
ULONG					RandomSeed;

INFINIBAND_INTERFACE_CM	IbCmInterface;

static EVT_WDF_DRIVER_DEVICE_ADD			WvRdmaDeviceAdd;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP		WvRdmaDeviceCleanup;
static EVT_WDF_DEVICE_D0_ENTRY				WvPowerD0Entry;
static EVT_WDF_DEVICE_D0_EXIT				WvPowerD0Exit;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL	WvIoDeviceControl;
static EVT_WDF_DEVICE_FILE_CREATE			WvFileCreate;
static EVT_WDF_FILE_CLEANUP					WvFileCleanup;
static EVT_WDF_FILE_CLOSE					WvFileClose;


#if DBG
#define WvDbgPrintEx DbgPrintEx
#else
#define WvDbgPrintEx //
#endif



WV_RDMA_DEVICE *WvRdmaDeviceGet(NET64 Guid)
{
	WV_RDMA_DEVICE	*cur_dev, *dev = NULL;
	LIST_ENTRY		*entry;

	KeAcquireGuardedMutex(&Lock);
	for (entry = DevList.Flink; entry != &DevList; entry = entry->Flink) {
		cur_dev = CONTAINING_RECORD(entry, WV_RDMA_DEVICE, Entry);
		if (cur_dev->Interface.Verbs.guid == Guid) {
			InterlockedIncrement(&cur_dev->Ref);
			dev = cur_dev;
			break;
		}
	}
	KeReleaseGuardedMutex(&Lock);
	return dev;
}

void WvRdmaDevicePut(WV_RDMA_DEVICE *pDevice)
{
	if (InterlockedDecrement(&pDevice->Ref) == 0) {
		KeSetEvent(&pDevice->Event, 0, FALSE);
	}
}


void WvCompleteRequests(WDFQUEUE Queue, NTSTATUS ReqStatus)
{
	WDFREQUEST	request;
	NTSTATUS	status;

	status = WdfIoQueueRetrieveNextRequest(Queue, &request);

	while (NT_SUCCESS(status)) {
		WdfRequestComplete(request, ReqStatus);
		status = WdfIoQueueRetrieveNextRequest(Queue, &request);
	}
}

void WvFlushQueue(WDFQUEUE Queue, NTSTATUS ReqStatus)
{
	WdfObjectAcquireLock(Queue);
	WvCompleteRequests(Queue, ReqStatus);
	WdfObjectReleaseLock(Queue);
}

void WvCompleteRequestsWithInformation(WDFQUEUE Queue, NTSTATUS ReqStatus)
{
	WDFREQUEST	request;
	NTSTATUS	status;
	UINT8		*out;
	size_t		outlen;

	status = WdfIoQueueRetrieveNextRequest(Queue, &request);

	while (NT_SUCCESS(status)) {
		outlen = 0;
		WdfRequestRetrieveOutputBuffer(request, 0, &out, &outlen);
		WdfRequestCompleteWithInformation(request, ReqStatus, outlen);
		status = WdfIoQueueRetrieveNextRequest(Queue, &request);
	}
}

static void WvGuidQuery(WDFREQUEST Request)
{
	WV_IO_GUID_LIST	*list;
	size_t			len = 0;
	WV_RDMA_DEVICE	*dev;
	ULONG			count, i;
	LIST_ENTRY		*entry;
	NTSTATUS		status;

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_GUID_LIST),
											&list, &len);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	count = (ULONG) ((len - sizeof(NET64)) / sizeof(NET64));
	i = 0;
	len = sizeof(NET64);
	KeAcquireGuardedMutex(&Lock);
	for (entry = DevList.Flink; entry != &DevList; entry = entry->Flink) {
		dev = CONTAINING_RECORD(entry, WV_RDMA_DEVICE, Entry);
		if (i < count) {
			list->Guid[i] = dev->Interface.Verbs.guid;
			len += sizeof(NET64);
		}
		i++;
	}
	list->Count = i;
	KeReleaseGuardedMutex(&Lock);

out:
	WdfRequestCompleteWithInformation(Request, status, len);
}

static void WvLibraryQuery(WDFREQUEST Request)
{
	NET64			*guid;
	char			*name;
	size_t			len = 0, bytes;
	WV_RDMA_DEVICE	*dev;
	NTSTATUS		status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(NET64), &guid, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, 1, &name, &bytes);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	dev = WvRdmaDeviceGet(*guid);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto out;
	}

	len = strlen(dev->Interface.Verbs.libname) + 1;
	if (bytes >= len) {
		RtlCopyMemory(name, dev->Interface.Verbs.libname, len);
	} else {
		status = STATUS_BUFFER_TOO_SMALL;
		*name = (char) len;
		len = 1;
	}
	WvRdmaDevicePut(dev);

out:
	WdfRequestCompleteWithInformation(Request, status, len);
}

static VOID WvIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request,
							  size_t OutLen, size_t InLen, ULONG IoControlCode)
{
	WDFFILEOBJECT	file;
	WV_PROVIDER		*prov;
	UNREFERENCED_PARAMETER(OutLen);
	UNREFERENCED_PARAMETER(InLen);
	UNREFERENCED_PARAMETER(Queue);

	file = WdfRequestGetFileObject(Request);
	prov = WvProviderGetContext(file);
	if (WvProviderGet(prov) == 0) {
		g_post_event("WinVerbs: Device closed - reject ioctl %#x \n", IoControlCode);
		WdfRequestComplete(Request, STATUS_FILE_CLOSED);
		return;
	}


	// TODO: verify this compiles as a jump table, or use function pointers
	switch (IoControlCode) {
	case WV_IOCTL_GUID_QUERY:
		WvGuidQuery(Request);
		break;
	case WV_IOCTL_LIBRARY_QUERY:
		WvLibraryQuery(Request);
		break;
	case WV_IOCTL_DEVICE_OPEN:
		WvDeviceOpen(prov, Request);
		break;
	case WV_IOCTL_DEVICE_CLOSE:
		WvDeviceClose(prov, Request);
		break;
	case WV_IOCTL_DEVICE_QUERY:
		WvDeviceQuery(prov, Request);
		break;
	case WV_IOCTL_DEVICE_PORT_QUERY:
		WvDevicePortQuery(prov, Request);
		break;
	case WV_IOCTL_DEVICE_GID_QUERY:
		WvDeviceGidQuery(prov, Request);
		break;
	case WV_IOCTL_DEVICE_PKEY_QUERY:
		WvDevicePkeyQuery(prov, Request);
		break;
	case WV_IOCTL_DEVICE_NOTIFY:
		WvDeviceNotify(prov, Request);
		break;
	case WV_IOCTL_DEVICE_CANCEL:
		WvDeviceCancel(prov, Request);
		break;
	case WV_IOCTL_PD_ALLOCATE:
		WvPdAllocate(prov, Request);
		break;
	case WV_IOCTL_PD_CANCEL:
		WvPdCancel(prov, Request);
		break;
	case WV_IOCTL_PD_DEALLOCATE:
		WvPdDeallocate(prov, Request);
		break;
	case WV_IOCTL_MEMORY_REGISTER:
		WvMrRegister(prov, Request);
		break;
	case WV_IOCTL_MEMORY_DEREGISTER:
		WvMrDeregister(prov, Request);
		break;
	case WV_IOCTL_MW_ALLOCATE:
		WvMwAllocate(prov, Request);
		break;
	case WV_IOCTL_MW_DEALLOCATE:
		WvMwDeallocate(prov, Request);
		break;
	case WV_IOCTL_AH_CREATE:
		WvAhCreate(prov, Request);
		break;
	case WV_IOCTL_AH_DESTROY:
		WvAhDestroy(prov, Request);
		break;
	case WV_IOCTL_CQ_CREATE:
		WvCqCreate(prov, Request);
		break;
	case WV_IOCTL_CQ_DESTROY:
		WvCqDestroy(prov, Request);
		break;
	case WV_IOCTL_CQ_RESIZE:
		WvCqResize(prov, Request);
		break;
	case WV_IOCTL_CQ_NOTIFY:
		WvCqNotify(prov, Request);
		break;
	case WV_IOCTL_CQ_CANCEL:
		WvCqCancel(prov, Request);
		break;
	case WV_IOCTL_SRQ_CREATE:
		WvSrqCreate(prov, Request);
		break;
	case WV_IOCTL_SRQ_DESTROY:
		WvSrqDestroy(prov, Request);
		break;
	case WV_IOCTL_SRQ_QUERY:
		WvSrqQuery(prov, Request);
		break;
	case WV_IOCTL_SRQ_MODIFY:
		WvSrqModify(prov, Request);
		break;
	case WV_IOCTL_SRQ_NOTIFY:
		WvSrqNotify(prov, Request);
		break;
	case WV_IOCTL_SRQ_CANCEL:
		WvSrqCancel(prov, Request);
		break;
	case WV_IOCTL_QP_CREATE:
		WvQpCreate(prov, Request);
		break;
	case WV_IOCTL_QP_DESTROY:
		WvQpDestroy(prov, Request);
		break;
	case WV_IOCTL_QP_QUERY:
		WvQpQuery(prov, Request);
		break;
	case WV_IOCTL_QP_MODIFY:
		WvQpModify(prov, Request);
		break;
	case WV_IOCTL_QP_ATTACH:
		WvQpAttach(prov, Request);
		break;
	case WV_IOCTL_QP_DETACH:
		WvQpDetach(prov, Request);
		break;
	case WV_IOCTL_QP_CANCEL:
		WvQpCancel(prov, Request);
		break;
	case WV_IOCTL_EP_CREATE:
		WvEpCreate(prov, Request);
		break;
	case WV_IOCTL_EP_DESTROY:
		WvEpDestroy(prov, Request);
		break;
	case WV_IOCTL_EP_MODIFY:
		WvEpModify(prov, Request);
		break;
	case WV_IOCTL_EP_BIND:
		WvEpBind(prov, Request);
		break;
	case WV_IOCTL_EP_REJECT:
		WvEpReject(prov, Request);
		break;
	case WV_IOCTL_EP_CONNECT:
		WvEpConnect(prov, Request);
		break;
	case WV_IOCTL_EP_ACCEPT:
		WvEpAccept(prov, Request);
		break;
	case WV_IOCTL_EP_DISCONNECT:
		WvEpDisconnect(prov, Request);
		break;
	case WV_IOCTL_EP_DISCONNECT_NOTIFY:
		WvEpDisconnectNotify(prov, Request);
		break;
	case WV_IOCTL_EP_QUERY:
		WvEpQuery(prov, Request);
		break;
	case WV_IOCTL_EP_LOOKUP:
		WvEpLookup(prov, Request);
		break;
	case WV_IOCTL_EP_MULTICAST_JOIN:
		WvEpMulticastJoin(prov, Request);
		break;
	case WV_IOCTL_EP_MULTICAST_LEAVE:
		WvEpMulticastLeave(prov, Request);
		break;
	case WV_IOCTL_EP_CANCEL:
		WvEpCancel(prov, Request);
		break;
	case WV_IOCTL_EP_LISTEN:
		WvEpListen(prov, Request);
		break;
	case WV_IOCTL_EP_GET_REQUEST:
		WvEpGetRequest(prov, Request);
		break;
	default:
		WdfRequestComplete(Request, STATUS_PROCEDURE_NOT_FOUND);
		break;
	}

	WvProviderPut(prov);
}

static VOID WvFileCreate(WDFDEVICE Device, WDFREQUEST Request,
						 WDFFILEOBJECT FileObject)
{
	WV_PROVIDER	*prov = WvProviderGetContext(FileObject);
	NTSTATUS status;

	status = WvProviderInit(Device, prov);
	if (NT_SUCCESS(status)) {
		KeAcquireGuardedMutex(&Lock);
		InsertHeadList(&ProvList, &prov->Entry);
		KeReleaseGuardedMutex(&Lock);
	}
	WdfRequestComplete(Request, status);
}

static VOID WvFileCleanup(WDFFILEOBJECT FileObject)
{
	LIST_ENTRY *entry;
	WV_PROVIDER *prov = WvProviderGetContext(FileObject);
	WV_PROVIDER *prov_entry;

	KeAcquireGuardedMutex(&Lock);
	for (entry = ProvList.Flink; entry != &ProvList; entry = entry->Flink) {
		prov_entry = CONTAINING_RECORD(entry, WV_PROVIDER, Entry);
		if (prov == prov_entry) {
	RemoveEntryList(&prov->Entry);
			goto found;
		}
	}
	prov = NULL;
found:
	KeReleaseGuardedMutex(&Lock);
	if (prov) {
	WvProviderCleanup(prov);
	}
}

static VOID WvFileClose(WDFFILEOBJECT FileObject)
{
	UNREFERENCED_PARAMETER(FileObject);
}

static VOID WvCreateControlDevice(WDFDRIVER Driver)
{
	PWDFDEVICE_INIT			pinit;
	WDF_FILEOBJECT_CONFIG	fileconfig;
	WDF_OBJECT_ATTRIBUTES	attr;
	WDF_IO_QUEUE_CONFIG		ioconfig;
	NTSTATUS				status;
	WDFQUEUE				queue;
	DECLARE_CONST_UNICODE_STRING(name, L"\\Device\\WinVerbs");
	DECLARE_CONST_UNICODE_STRING(symlink, L"\\DosDevices\\WinVerbs");

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

	WDF_FILEOBJECT_CONFIG_INIT(&fileconfig, WvFileCreate, WvFileClose,
							   WvFileCleanup);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WV_PROVIDER);
	WdfDeviceInitSetFileObjectConfig(pinit, &fileconfig, &attr);

	ASSERT(ControlDevice == NULL);

	WDF_OBJECT_ATTRIBUTES_INIT(&attr);
	status = WdfDeviceCreate(&pinit, &attr, &ControlDevice);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	ASSERT(ControlDevice != NULL);
	
	status = WdfDeviceCreateSymbolicLink(ControlDevice, &symlink);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioconfig, WdfIoQueueDispatchParallel);
	ioconfig.EvtIoDeviceControl = WvIoDeviceControl;
	status = WdfIoQueueCreate(ControlDevice, &ioconfig,
							  WDF_NO_OBJECT_ATTRIBUTES, &queue);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	WdfControlFinishInitializing(ControlDevice);

	return;

err2:
	WdfObjectDelete(ControlDevice);
	ControlDevice = NULL;
	return;
err1:
	WdfDeviceInitFree(pinit);
}

static NTSTATUS WvPowerD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
	WV_RDMA_DEVICE	*dev;
	BOOLEAN			create;
	NTSTATUS		status;

	WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WvPowerD0Entry\n");

	dev = WvRdmaDeviceGetContext(Device);
	if (dev->hDevice != NULL) {
		status = STATUS_SUCCESS;
		goto exit;
	}

	status = WdfFdoQueryForInterface(Device, &GUID_RDMA_INTERFACE_VERBS,
									 (PINTERFACE) &dev->Interface,
									 sizeof(dev->Interface), RDMA_INTERFACE_VERBS_VERSION,
									 NULL);
	if ( status == STATUS_NOT_SUPPORTED ) {
		status = STATUS_SUCCESS;
		WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, "No IB ports - WinVerbs is started in non-operational mode\n" );
		goto exit;
	}
	if (!NT_SUCCESS(status)) {
		goto exit;
	} else {
		if( RDMA_INTERFACE_VERBS_VERSION != dev->Interface.InterfaceHeader.Version )
		{
			WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WinVerbs: GUID_RDMA_INTERFACE_VERBS version mismatch: requested version=%d, returned version=%d.\n",
				RDMA_INTERFACE_VERBS_VERSION, dev->Interface.InterfaceHeader.Version);
	
			dev->Interface.InterfaceHeader.InterfaceDereference(dev->Interface.InterfaceHeader.Context);
			status = STATUS_NOT_SUPPORTED;
			goto exit;
		}
	}
	dev->InterfaceTaken = TRUE;
	dev->hDevice = dev->Interface.Verbs.p_hca_obj;
	g_post_event_func = dev->Interface.post_event;
	g_post_event("WinVerbs: WvPowerD0Entry started \n");
	g_post_event("WinVerbs: Taken GUID_RDMA_INTERFACE_VERBS interface \n");

	KeAcquireGuardedMutex(&Lock);
	create = IsListEmpty(&DevList);
	InsertTailList(&DevList, &dev->Entry);
	KeReleaseGuardedMutex(&Lock);

	status = WdfFdoQueryForInterface(Device, &GUID_INFINIBAND_INTERFACE_CM,
									 (PINTERFACE) &dev->IbCmInterface,
									 sizeof(dev->IbCmInterface),
									 INFINIBAND_INTERFACE_CM_VERSION, NULL);
	if( NT_SUCCESS( status ) ) 
	{
		if( INFINIBAND_INTERFACE_CM_VERSION != dev->IbCmInterface.InterfaceHeader.Version )
		{
			WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WinVerbs: GUID_INFINIBAND_INTERFACE_CM version mismatch: requested version=%d, returned version=%d.\n",
				INFINIBAND_INTERFACE_CM_VERSION, dev->IbCmInterface.InterfaceHeader.Version);
	
			dev->IbCmInterface.InterfaceHeader.InterfaceDereference(dev->IbCmInterface.InterfaceHeader.Context);
			dev->Interface.InterfaceHeader.InterfaceDereference(dev->Interface.InterfaceHeader.Context);
			dev->InterfaceTaken = FALSE;
			status = STATUS_NOT_SUPPORTED;
			goto exit;
		}
	}
	dev->IbCmInterfaceTaken = TRUE;
	g_post_event("WinVerbs: Taken GUID_INFINIBAND_INTERFACE_CM interface \n");
	if (IbCmInterface.CM.create_id == NULL) 
		IbCmInterface = dev->IbCmInterface;

	if (create) 
		WvCreateControlDevice(WdfGetDriver());
	
exit:
	g_post_event("WinVerbs: WvPowerD0Entry ended with status %#x \n", status);
	return status;
}

static NTSTATUS WvPowerD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
	WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WvPowerD0Exit enter with TargetState %d\n", TargetState);

	WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"WvPowerD0Exit exit\n");

	WvRdmaDeviceCleanup(Device);

	g_post_event("WinVerbs: WvPowerD0Exit called\n");
	return STATUS_SUCCESS;
}

static NTSTATUS WvRdmaDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
	WDF_OBJECT_ATTRIBUTES			attr;
	WDF_PNPPOWER_EVENT_CALLBACKS	power;
	WDFDEVICE						dev;
	WV_RDMA_DEVICE					*pdev;
	NTSTATUS						status;

	WdfFdoInitSetFilter(DeviceInit);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, WV_RDMA_DEVICE);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
	power.EvtDeviceD0Entry = WvPowerD0Entry;
	power.EvtDeviceD0Exit = WvPowerD0Exit;
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &power);

	status = WdfDeviceCreate(&DeviceInit, &attr, &dev);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pdev = WvRdmaDeviceGetContext(dev);
	RtlZeroMemory(pdev, sizeof *pdev);
	pdev->Ref = 1;
	KeInitializeEvent(&pdev->Event, NotificationEvent, FALSE);

	return STATUS_SUCCESS;
}

static VOID WvRdmaDeviceCleanup(WDFDEVICE Device)
{
	WV_RDMA_DEVICE			*pdev;
	WV_PROVIDER				*prov;
	LIST_ENTRY				*entry;
	BOOLEAN					destroy;
	WDFDEVICE				ctrldev = NULL;

	pdev = WvRdmaDeviceGetContext(Device);
	g_post_event("WvRdmaDeviceCleanup: WvPowerD0Exit started with WV_RDMA_DEVICE %p\n", pdev->hDevice);
	if (pdev->hDevice == NULL) {
		return;
	}

	KeAcquireGuardedMutex(&Lock);
	RemoveEntryList(&pdev->Entry);
	destroy = IsListEmpty(&DevList);
	if (destroy) {
		ctrldev = ControlDevice;
		ControlDevice = NULL;
	}

	for (entry = ProvList.Flink; entry != &ProvList; entry = entry->Flink) {
		prov = CONTAINING_RECORD(entry, WV_PROVIDER, Entry);
		WvProviderRemoveHandler(prov, pdev);
	}

	KeReleaseGuardedMutex(&Lock);

	if (InterlockedDecrement(&pdev->Ref) > 0) {
		KeWaitForSingleObject(&pdev->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pdev->InterfaceTaken) {
		pdev->Interface.InterfaceHeader.InterfaceDereference(pdev->Interface.
														 InterfaceHeader.Context);
		g_post_event("WinVerbs: Released GUID_RDMA_INTERFACE_VERBS interface \n");
	}
	pdev->hDevice = NULL;

	if (pdev->IbCmInterfaceTaken) {
		pdev->IbCmInterface.InterfaceHeader.InterfaceDereference(pdev->IbCmInterface.
														   InterfaceHeader.Context);
		g_post_event("WinVerbs: Released GUID_INFINIBAND_INTERFACE_CM interface \n");
	}
	
	if (destroy) {
		if (ctrldev!= NULL) {
			WdfObjectDelete(ctrldev);
		}
	}

	WvDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"WvRdmaDeviceCleanup: WinVerbs exit\n");
	g_post_event("WvRdmaDeviceCleanup: WvPowerD0Exit ended\n");
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
	RandomSeed = (ULONG) (ULONG_PTR) DriverObject;

	WDF_DRIVER_CONFIG_INIT(&config, WvRdmaDeviceAdd);
	status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
							 &config, &driv);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	return STATUS_SUCCESS;
}
