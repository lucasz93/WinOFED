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

#include "wv_device.h"
#include "wv_pd.h"
#include "wv_cq.h"
#include "wv_ioctl.h"

void WvDeviceGet(WV_DEVICE *pDevice)
{
	InterlockedIncrement(&pDevice->Ref);
}

void WvDevicePut(WV_DEVICE *pDevice)
{
	if (InterlockedDecrement(&pDevice->Ref) == 0) {
		KeSetEvent(&pDevice->Event, 0, FALSE);
	}
}

WV_DEVICE *WvDeviceAcquire(WV_PROVIDER *pProvider, UINT64 Id)
{
	WV_DEVICE *dev;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	dev = IndexListAt(&pProvider->DevIndex, (SIZE_T) Id);
	if (dev != NULL && dev->hVerbsDevice != NULL) {
		WvDeviceGet(dev);
	} else {
		dev = NULL;
		WvProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	return dev;
}

void WvDeviceRelease(WV_DEVICE *pDevice)
{
	WvProviderEnableRemove(pDevice->pProvider);
	WvDevicePut(pDevice);
}

static UINT32 WvDeviceConvertEvent(ib_async_event_t event)
{
	switch (event) {
	case IB_AE_LOCAL_FATAL:
		return WV_IO_EVENT_ERROR;
	case IB_AE_PORT_ACTIVE:
	case IB_AE_PORT_DOWN:
		return WV_IO_EVENT_STATE;
	case IB_AE_CLIENT_REREGISTER:
	case IB_AE_SM_CHANGE:
		return WV_IO_EVENT_MANAGEMENT;
	case IB_AE_GID_CHANGE:
		return WV_IO_EVENT_ADDRESS;
	case IB_AE_LID_CHANGE:
		return WV_IO_EVENT_LINK_ADDRESS;
	case IB_AE_PKEY_CHANGE:
		return WV_IO_EVENT_PARTITION;
	default:
		return 0;
	}
}

static void WvDeviceCompleteRequests(WV_PORT *pPort, NTSTATUS ReqStatus, UINT32 Event)
{
	WDFREQUEST	request;
	NTSTATUS	status;
	UINT32		*flags;

	WdfObjectAcquireLock(pPort->Queue);
	pPort->Flags |= Event;
	Event = pPort->Flags;

	status = WdfIoQueueRetrieveNextRequest(pPort->Queue, &request);
	while (NT_SUCCESS(status)) {
		pPort->Flags = 0;

		status = WdfRequestRetrieveOutputBuffer(request, sizeof(UINT32), &flags, NULL);
		if (NT_SUCCESS(status)) {
			*flags = Event;
			WdfRequestCompleteWithInformation(request, ReqStatus, sizeof(UINT32));
		} else {
			WdfRequestComplete(request, status);
		}
		status = WdfIoQueueRetrieveNextRequest(pPort->Queue, &request);
	}

	WdfObjectReleaseLock(pPort->Queue);
}

static void WvDeviceEventHandler(ib_event_rec_t *pEvent)
{
	WV_DEVICE	*dev;
	UINT32		event;
	UINT8		i;

	event = WvDeviceConvertEvent(pEvent->type);
	if (event == 0) {
		return;
	}

	dev = CONTAINING_RECORD(pEvent->context, WV_DEVICE, EventHandler);

	if (event == WV_IO_EVENT_ERROR) {
		for (i = 0; i < dev->PortCount; i++) {
			WvDeviceCompleteRequests(&dev->pPorts[i], STATUS_SUCCESS, event);
		}
	} else {
		ASSERT(pEvent->port_number <= dev->PortCount);
		if(pEvent->port_number <= dev->PortCount) {
			WvDeviceCompleteRequests(&dev->pPorts[pEvent->port_number - 1],
									 STATUS_SUCCESS, event);
		}
	}
}

static WV_DEVICE *WvDeviceAlloc(WV_PROVIDER *pProvider)
{
	WV_DEVICE	*dev;

	dev = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_DEVICE), 'cdvw');
	if (dev == NULL) {
		return NULL;
	}

	dev->pDevice = NULL;
	dev->pVerbs = NULL;
	dev->hVerbsDevice = NULL;
	dev->pPorts = NULL;
	dev->PortCount = 0;
	dev->Ref = 1;
	InitializeListHead(&dev->PdList);
	InitializeListHead(&dev->CqList);
	KeInitializeEvent(&dev->Event, NotificationEvent, FALSE);
	dev->EventHandler.pfn_async_event_cb = WvDeviceEventHandler;

	dev->pProvider = pProvider;
	WvProviderGet(pProvider);
	return dev;
}

static ib_ca_attr_t *WvQueryCaAttributes(WV_DEVICE *pDevice)
{
	ib_ca_attr_t	*attr;
	UINT32			size;
	ib_api_status_t	ib_status;

	size = 0;
	ib_status = pDevice->pVerbs->query_ca(pDevice->pDevice->hDevice, NULL,
										  &size, NULL);
	if (ib_status != IB_INSUFFICIENT_MEMORY) {
		attr = NULL;
		goto out;
	}

	attr = ExAllocatePoolWithTag(PagedPool, size, 'acvw');
	if (attr == NULL) {
		goto out;
	}

	ib_status = pDevice->pVerbs->query_ca(pDevice->pDevice->hDevice, attr,
										  &size, NULL);
	if (ib_status != IB_SUCCESS) {
		ExFreePoolWithTag(attr, 'acvw');
		attr = NULL;
	}

out:
	return attr;
}

static NTSTATUS WvDeviceCreatePorts(WV_DEVICE *pDevice)
{
	WDF_IO_QUEUE_CONFIG	config;
	ib_ca_attr_t		*attr;
	NTSTATUS			status;
	UINT8				i;

	attr = WvQueryCaAttributes(pDevice);
	if (attr == NULL) {
		return STATUS_NO_MEMORY;
	}

	pDevice->PortCount = attr->num_ports;
	ExFreePoolWithTag(attr, 'acvw');

	pDevice->pPorts = ExAllocatePoolWithTag(NonPagedPool, sizeof(WV_PORT) *
											pDevice->PortCount, 'cpvw');
	if (pDevice->pPorts == NULL) {
		return STATUS_NO_MEMORY;
	}

	ASSERT(ControlDevice != NULL);

	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);
	for (i = 0; i < pDevice->PortCount; i++) {
		pDevice->pPorts[i].Flags = 0;
		status = WdfIoQueueCreate(ControlDevice, &config, WDF_NO_OBJECT_ATTRIBUTES,
								  &pDevice->pPorts[i].Queue);
		if (!NT_SUCCESS(status)) {
			goto err;
		}
	}

	return STATUS_SUCCESS;

err:
	while (i-- > 0) {
		WdfObjectDelete(pDevice->pPorts[i].Queue);
	}
	pDevice->PortCount = 0;
	return status;
}

static NTSTATUS WvDeviceInit(WV_DEVICE *pDevice, NET64 Guid,
							 ci_umv_buf_t *pVerbsData)
{
	WV_RDMA_DEVICE *dev;
	ib_api_status_t ib_status;
	NTSTATUS		status;

	dev = WvRdmaDeviceGet(Guid);
	if (dev == NULL) {
		return STATUS_NO_SUCH_DEVICE;
	}

	pDevice->pDevice = dev;
	pDevice->pVerbs = &dev->Interface.Verbs;

	ib_status = pDevice->pVerbs->um_open_ca(dev->hDevice, UserMode, pVerbsData,
											&pDevice->hVerbsDevice);
	if (ib_status != IB_SUCCESS) {
		goto err1;
	}

	status = WvDeviceCreatePorts(pDevice);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	pDevice->pVerbs->register_event_handler(dev->hDevice, &pDevice->EventHandler);
	return STATUS_SUCCESS;

err2:
	pDevice->pVerbs->um_close_ca(dev->hDevice, pDevice->hVerbsDevice);
err1:
	WvRdmaDevicePut(dev);
	pDevice->hVerbsDevice = NULL;
	return STATUS_UNSUCCESSFUL;
}

void WvDeviceOpen(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID		*inid, *outid;
	size_t			inlen, outlen;
	WV_DEVICE		*dev;
	NTSTATUS		status;
	ci_umv_buf_t	verbsData;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &inid, &inlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_ID), &outid, &outlen);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	dev = WvDeviceAlloc(pProvider);
	if (dev == NULL) {
		status = STATUS_NO_MEMORY;
		goto err1;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvInitVerbsData(&verbsData, inid->VerbInfo, inlen - sizeof(WV_IO_ID),
					outlen - sizeof(WV_IO_ID), inid + 1);
	status = WvDeviceInit(dev, inid->Id, &verbsData);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	outid->Id = IndexListInsertHead(&pProvider->DevIndex, dev);
	if (outid->Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	WvProviderEnableRemove(pProvider);
	outid->VerbInfo = verbsData.status;
	WdfRequestCompleteWithInformation(Request, status, outlen);
	return;

err2:
	WvDeviceFree(dev);
	WvProviderEnableRemove(pProvider);
err1:
	WdfRequestComplete(Request, status);
}

void WvDeviceClose(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_DEVICE	*dev;
	UINT64		*id;
	NTSTATUS	status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WvProviderDisableRemove(pProvider);
	dev = IndexListAt(&pProvider->DevIndex, (SIZE_T) *id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
	} else if (dev->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->DevIndex, (SIZE_T) *id);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WvDeviceFree(dev);
	}
	WvProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

static void WvDeviceFreePorts(WV_DEVICE *pDevice)
{
	UINT8 i;

	for (i = 0; i < pDevice->PortCount; i++) {
		WdfIoQueuePurgeSynchronously(pDevice->pPorts[i].Queue);
		WdfObjectDelete(pDevice->pPorts[i].Queue);
	}
	if (pDevice->pPorts != NULL) {
		ExFreePoolWithTag(pDevice->pPorts, 'cpvw');
	}
}

void WvDeviceFree(WV_DEVICE *pDevice)
{
	if (InterlockedDecrement(&pDevice->Ref) > 0) {
		KeWaitForSingleObject(&pDevice->Event, Executive, KernelMode, FALSE, NULL);
	}

	if (pDevice->hVerbsDevice != NULL) {
		pDevice->pVerbs->unregister_event_handler(pDevice->pDevice->hDevice,
												  &pDevice->EventHandler);
		pDevice->pVerbs->um_close_ca(pDevice->pDevice->hDevice,
									 pDevice->hVerbsDevice);
		WvRdmaDevicePut(pDevice->pDevice);
	}

	WvDeviceFreePorts(pDevice);
	WvProviderPut(pDevice->pProvider);
	ExFreePoolWithTag(pDevice, 'cdvw');
}

void WvDeviceRemoveHandler(WV_DEVICE *pDevice)
{
	LIST_ENTRY				*entry;
	WV_PROTECTION_DOMAIN	*pd;
	WV_COMPLETION_QUEUE		*cq;

	for (entry = pDevice->PdList.Flink; entry != &pDevice->PdList;
		 entry = entry->Flink) {
		pd = CONTAINING_RECORD(entry, WV_PROTECTION_DOMAIN, Entry);
		WvPdRemoveHandler(pd);
	}

	for (entry = pDevice->CqList.Flink; entry != &pDevice->CqList;
		 entry = entry->Flink) {
		cq = CONTAINING_RECORD(entry, WV_COMPLETION_QUEUE, Entry);
		pDevice->pVerbs->destroy_cq(cq->hVerbsCq);
		cq->hVerbsCq = NULL;
		cq->pVerbs = NULL;
	}

	pDevice->pVerbs->um_close_ca(pDevice->pDevice->hDevice,
								 pDevice->hVerbsDevice);
	WvRdmaDevicePut(pDevice->pDevice);
	pDevice->pDevice = NULL;
	pDevice->pVerbs = NULL;
	pDevice->hVerbsDevice = NULL;
}

static void WvSetDeviceCap(UINT32 *pFlags, ib_ca_attr_t *pCaAttr)
{
	*pFlags = 0;

	*pFlags |= pCaAttr->bad_pkey_ctr_support ? WV_IO_BAD_PKEY_COUNTER : 0;
	*pFlags |= pCaAttr->bad_qkey_ctr_support ? WV_IO_BAD_QKEY_COUNTER : 0;
	*pFlags |= pCaAttr->apm_support ? WV_IO_PATH_MIGRATION : 0;
	*pFlags |= pCaAttr->av_port_check ? WV_IO_AH_PORT_CHECKING : 0;
	*pFlags |= pCaAttr->change_primary_port ? WV_IO_CHANGE_PHYSICAL_PORT : 0;
	*pFlags |= pCaAttr->modify_wr_depth ? WV_IO_RESIZE_MAX_WR : 0;
	*pFlags |= pCaAttr->modify_srq_depth ? WV_IO_SRQ_RESIZE : 0;
	*pFlags |= pCaAttr->current_qp_state_support ? WV_IO_QP_STATE_MODIFIER : 0;
	*pFlags |= pCaAttr->shutdown_port_capability ? WV_IO_SHUTDOWN_PORT : 0;
	*pFlags |= pCaAttr->init_type_support ? WV_IO_INIT_TYPE : 0;
	*pFlags |= pCaAttr->port_active_event_support ? WV_IO_PORT_ACTIVE_EVENT : 0;
	*pFlags |= pCaAttr->system_image_guid_support ? WV_IO_SYSTEM_IMAGE_GUID : 0;
	*pFlags |= WV_IO_RC_RNR_NAK_GENERATION;
	*pFlags |= WV_IO_BATCH_NOTIFY_CQ;
}

static void WvSetDevicePages(UINT32 *pFlags, ib_ca_attr_t *pCaAttr)
{
	unsigned int i;
	UINT32 size;

	*pFlags = 0;

	for (i = 0; i < pCaAttr->num_page_sizes; i++) {
		size = pCaAttr->p_page_size[i];
		*pFlags |= (size & (size - 1)) ? 0 : size;
	}
}

static void WvConvertDevAttr(WV_IO_DEVICE_ATTRIBUTES* pAttributes,
							 ib_ca_attr_t *pCaAttr)
{
	pAttributes->FwVersion			= pCaAttr->fw_ver;
	pAttributes->NodeGuid			= pCaAttr->ca_guid;
	pAttributes->SystemImageGuid	= pCaAttr->system_image_guid;
	pAttributes->VendorId			= pCaAttr->vend_id;
	pAttributes->VendorPartId		= pCaAttr->dev_id;
	pAttributes->HwVersion			= pCaAttr->revision;

	WvSetDeviceCap(&pAttributes->CapabilityFlags, pCaAttr);
	pAttributes->AtomicCapability	= (UINT32) pCaAttr->atomicity;
	WvSetDevicePages(&pAttributes->PageSizeCapabilityFlags, pCaAttr);

	pAttributes->MaxMrSize			= pCaAttr->init_region_size;
	pAttributes->MaxQp				= pCaAttr->max_qps;
	pAttributes->MaxQpWr			= pCaAttr->max_wrs;
	pAttributes->MaxSge				= pCaAttr->max_sges;
	pAttributes->MaxCq				= pCaAttr->max_cqs;
	pAttributes->MaxCqEntries		= pCaAttr->max_cqes;
	pAttributes->MaxMr				= pCaAttr->init_regions;
	pAttributes->MaxPd				= pCaAttr->max_pds;
	pAttributes->MaxQpResponderResources	= pCaAttr->max_qp_resp_res;
	pAttributes->MaxResponderResources		= pCaAttr->max_resp_res;
	pAttributes->MaxQpInitiatorDepth		= pCaAttr->max_qp_init_depth;
	pAttributes->MaxMw				= pCaAttr->init_windows;
	pAttributes->MaxMulticast		= pCaAttr->max_mcast_grps;
	pAttributes->MaxQpAttach		= pCaAttr->max_qps_per_mcast_grp;
	pAttributes->MaxMulticastQp		= pCaAttr->max_mcast_qps;
	pAttributes->MaxAh				= (UINT32) pCaAttr->max_addr_handles;
	pAttributes->MaxFmr				= pCaAttr->max_fmr;
	pAttributes->MaxMapPerFmr		= pCaAttr->max_map_per_fmr;
	pAttributes->MaxSrq				= pCaAttr->max_srq;
	pAttributes->MaxSrqWr			= pCaAttr->max_srq_wrs;
	pAttributes->MaxSrqSge			= pCaAttr->max_srq_sges;
	pAttributes->MaxPkeys			= pCaAttr->max_partitions;
	pAttributes->DeviceType			= WV_DEVICE_INFINIBAND; // TODO: missing in ib_ca_attr_t
	pAttributes->LocalAckDelay		= pCaAttr->local_ack_delay;
	pAttributes->PhysPortCount		= pCaAttr->num_ports;
}

static void WvConvertPortCap(UINT32 *pFlags, ib_port_cap_t *pCap)
{
	*pFlags = 0;

	*pFlags |= pCap->qkey_ctr ? WV_IO_BAD_QKEY_COUNTER : 0;
	*pFlags |= pCap->pkey_ctr ? WV_IO_BAD_PKEY_COUNTER : 0;
	*pFlags |= pCap->apm ? WV_IO_PATH_MIGRATION : 0;
	*pFlags |= pCap->sysguid ? WV_IO_SYSTEM_IMAGE_GUID : 0;
	*pFlags |= pCap->port_active ? WV_IO_PORT_ACTIVE_EVENT : 0;

	// TODO: missing in ib_port_attr_t:
	// WV_IO_RESIZE_MAX_WR
	// WV_IO_CHANGE_PHYSICAL_PORT
	// WV_IO_AH_PORT_CHECKING
	*pFlags |= WV_IO_QP_STATE_MODIFIER;
	// WV_IO_SHUTDOWN_PORT
	// WV_IO_INIT_TYPE
	*pFlags |= WV_IO_RC_RNR_NAK_GENERATION;
	// WV_IO_SRQ_RESIZE
	*pFlags |= WV_IO_BATCH_NOTIFY_CQ;
}

static void WvConvertPortAttr(WV_IO_PORT_ATTRIBUTES *pAttributes,
							  ib_port_attr_t *pPortAttr)
{
	WvConvertPortCap(&pAttributes->PortCabilityFlags, &pPortAttr->cap);
	pAttributes->State			= pPortAttr->link_state;
	pAttributes->MaxMtu			= 0x80 << pPortAttr->mtu;
	pAttributes->ActiveMtu		= 0x80 << pPortAttr->mtu;
	pAttributes->GidTableLength	= pPortAttr->num_gids;
	pAttributes->MaxMessageSize	= (UINT32) pPortAttr->max_msg_size;
	pAttributes->BadPkeyCounter	= pPortAttr->pkey_ctr;
	pAttributes->QkeyViolationCounter	= pPortAttr->qkey_ctr;
	pAttributes->PkeyTableLength		= pPortAttr->num_pkeys;
	pAttributes->Lid			= pPortAttr->lid;
	pAttributes->SmLid			= pPortAttr->sm_lid;
	pAttributes->Lmc			= pPortAttr->lmc;
	pAttributes->MaxVls			= (UINT8) pPortAttr->max_vls;
	pAttributes->SmSl			= pPortAttr->sm_sl;
	pAttributes->SubnetTimeout	= pPortAttr->subnet_timeout;
	pAttributes->InitTypeReply	= pPortAttr->init_type_reply;
	pAttributes->ActiveWidth	= pPortAttr->active_width;
	pAttributes->ActiveSpeed	= pPortAttr->active_speed;
	pAttributes->ExtActiveSpeed	= pPortAttr->ext_active_speed >> 4;
	pAttributes->LinkEncoding	= pPortAttr->link_encoding;
	pAttributes->PhysicalState	= pPortAttr->phys_state;
	pAttributes->Transport		= (UINT8) pPortAttr->transport;
}

void WvDeviceQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64					*id;
	WV_IO_DEVICE_ATTRIBUTES	*attr;
	WV_DEVICE				*dev;
	ib_ca_attr_t			*ca_attr;
	NTSTATUS				status;
	UINT32					outlen = 0;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_DEVICE_ATTRIBUTES),
											&attr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	dev = WvDeviceAcquire(pProvider, *id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto complete;
	}

	ca_attr = WvQueryCaAttributes(dev);
	WvDeviceRelease(dev);

	if (ca_attr == NULL) {
		status = STATUS_NO_MEMORY;
		goto complete;
	}

	WvConvertDevAttr(attr, ca_attr);
	outlen = sizeof(WV_IO_DEVICE_ATTRIBUTES);
	ExFreePoolWithTag(ca_attr, 'acvw');

complete:
	WdfRequestCompleteWithInformation(Request, status, outlen);
}

void WvDevicePortQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_DEVICE_PORT_QUERY	*query;
	WV_IO_PORT_ATTRIBUTES	*attr;
	WV_DEVICE				*dev;
	ib_ca_attr_t			*ca_attr;
	NTSTATUS				status;
	UINT32					outlen = 0;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_DEVICE_PORT_QUERY),
										   &query, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_PORT_ATTRIBUTES),
											&attr, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	dev = WvDeviceAcquire(pProvider, query->Id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto complete;
	}

	ca_attr = WvQueryCaAttributes(dev);
	WvDeviceRelease(dev);

	if (ca_attr == NULL) {
		status = STATUS_NO_MEMORY;
		goto complete;
	}

	if (--query->PortNumber >= ca_attr->num_ports) {
		status = STATUS_INVALID_PORT_HANDLE;
		goto free;
	}

	WvConvertPortAttr(attr, &ca_attr->p_port_attr[query->PortNumber]);
	outlen = sizeof(WV_IO_PORT_ATTRIBUTES);

free:
	ExFreePoolWithTag(ca_attr, 'acvw');
complete:
	WdfRequestCompleteWithInformation(Request, status, outlen);
}

void WvDeviceGidQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_DEVICE_PORT_QUERY	*query;
	WV_IO_GID				*gid;
	WV_DEVICE				*dev;
	ib_ca_attr_t			*ca_attr;
	ib_port_attr_t			*port_attr;
	NTSTATUS				status;
	size_t					i, size, outlen = 0;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_DEVICE_PORT_QUERY),
										   &query, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(WV_IO_GID), &gid, &size);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	dev = WvDeviceAcquire(pProvider, query->Id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto complete;
	}

	ca_attr = WvQueryCaAttributes(dev);
	WvDeviceRelease(dev);

	if (ca_attr == NULL) {
		status = STATUS_NO_MEMORY;
		goto complete;
	}

	if (--query->PortNumber >= ca_attr->num_ports) {
		status = STATUS_INVALID_PORT_HANDLE;
		goto free;
	}

	size /= sizeof(WV_IO_GID);
	port_attr = &ca_attr->p_port_attr[query->PortNumber];
	for (i = 0; i < size && i < port_attr->num_gids; i++) {
		RtlCopyMemory(&gid[i], &port_attr->p_gid_table[i], sizeof(WV_IO_GID));
	}

	outlen = i * sizeof(WV_IO_GID);
	if (i < port_attr->num_gids) {
		status = STATUS_MORE_ENTRIES;
	}

free:
	ExFreePoolWithTag(ca_attr, 'acvw');
complete:
	WdfRequestCompleteWithInformation(Request, status, outlen);
}

void WvDevicePkeyQuery(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_DEVICE_PORT_QUERY	*query;
	NET16					*pkey;
	WV_DEVICE				*dev;
	ib_ca_attr_t			*ca_attr;
	ib_port_attr_t			*port_attr;
	NTSTATUS				status;
	size_t					i, size, outlen = 0;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_DEVICE_PORT_QUERY),
										   &query, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(NET16), &pkey, &size);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	dev = WvDeviceAcquire(pProvider, query->Id);
	if (dev == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto complete;
	}

	ca_attr = WvQueryCaAttributes(dev);
	WvDeviceRelease(dev);

	if (ca_attr == NULL) {
		status = STATUS_NO_MEMORY;
		goto complete;
	}

	if (--query->PortNumber >= ca_attr->num_ports) {
		status = STATUS_INVALID_PORT_HANDLE;
		goto free;
	}

	size /= sizeof(NET16);
	port_attr = &ca_attr->p_port_attr[query->PortNumber];
	for (i = 0; i < size && i < port_attr->num_pkeys; i++) {
		pkey[i] = port_attr->p_pkey_table[i];
	}

	outlen = i * sizeof(NET16);
	if (i < port_attr->num_pkeys) {
		status = STATUS_MORE_ENTRIES;
	}

free:
	ExFreePoolWithTag(ca_attr, 'acvw');
complete:
	WdfRequestCompleteWithInformation(Request, status, outlen);
}

void WvDeviceNotify(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	WV_IO_ID		*id;
	WV_DEVICE		*dev;
	WV_PORT			*port;
	NTSTATUS		status;
	UINT32			*flags;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WV_IO_ID), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UINT32), &flags, NULL);
	if (!NT_SUCCESS(status)) {
		goto complete;
	}

	dev = WvDeviceAcquire(pProvider, id->Id);
	if (dev == NULL) {
		status = STATUS_NOT_FOUND;
		goto complete;
	}

	if (--id->Data >= dev->PortCount) {
		status = STATUS_INVALID_PORT_HANDLE;
		goto release;
	}

	port = &dev->pPorts[id->Data];
	WdfObjectAcquireLock(port->Queue);

	if (port->Flags == 0) {
		status = WdfRequestForwardToIoQueue(Request, port->Queue);
	} else {
		*flags = port->Flags;
		WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(UINT32));
		port->Flags = 0;
	}

	WdfObjectReleaseLock(port->Queue);
release:
	WvDeviceRelease(dev);
complete:
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, status);
	}
}

void WvDeviceCancel(WV_PROVIDER *pProvider, WDFREQUEST Request)
{
	UINT64			*id;
	WV_DEVICE		*dev;
	NTSTATUS		status;
	UINT8			i;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	dev = WvDeviceAcquire(pProvider, *id);
	if (dev == NULL) {
		status = STATUS_NOT_FOUND;
		goto out;
	}

	for (i = 0; i < dev->PortCount; i++) {
		WvDeviceCompleteRequests(&dev->pPorts[i], STATUS_CANCELLED, 0);
	}
	WvDeviceRelease(dev);

out:
	WdfRequestComplete(Request, status);
}
