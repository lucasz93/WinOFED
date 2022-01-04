/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <stdio.h>
#include <infiniband/verbs.h>
#include <rdma/winverbs.h>
#include "..\..\..\etc\user\comp_channel.cpp"

CRITICAL_SECTION lock;
IWVProvider *prov;
COMP_MANAGER comp_mgr;
static DWORD ref;

struct verbs_device
{
	struct ibv_device	device;
	uint64_t			guid;
	uint8_t				phys_port_cnt;
};

struct verbs_port
{
	COMP_ENTRY			comp_entry;
	DWORD				event_flag;
	uint8_t				port_num;
};

struct verbs_context
{
	struct ibv_context	context;
	struct verbs_device	device;
	uint8_t				closing;
	struct verbs_port	*port;
	verbs_port			*event_port;
};

static int ibv_acquire(void)
{
	HRESULT hr;

	EnterCriticalSection(&lock);
	if (ref++ == 0) {
		hr = WvGetObject(IID_IWVProvider, (LPVOID*) &prov);
		if (FAILED(hr)) {
			goto err1;
		}
		hr = CompManagerOpen(&comp_mgr);
		if (FAILED(hr)) {
			goto err2;
		}
		hr = CompManagerMonitor(&comp_mgr, prov->GetFileHandle(), 0);
		if (FAILED(hr)) {
			goto err3;
		}
	}
	LeaveCriticalSection(&lock);
	return 0;

err3:
	CompManagerClose(&comp_mgr);
err2:
	prov->Release();
err1:
	ref--;
	LeaveCriticalSection(&lock);
	return hr;
}

static void ibv_release(void)
{
	EnterCriticalSection(&lock);
	if (--ref == 0) {
		CompManagerClose(&comp_mgr);
		prov->Release();
	}
	LeaveCriticalSection(&lock);
}

__declspec(dllexport)
int ibvw_get_windata(struct ibvw_windata *windata, int version)
{
	int ret;

	if (version != IBVW_WINDATA_VERSION || ibv_acquire()) {
		return -1;
	}

	prov->AddRef();
	windata->prov = prov;
	windata->comp_mgr = &comp_mgr;
	return 0;
}

__declspec(dllexport)
void ibvw_release_windata(struct ibvw_windata *windata, int version)
{
	prov->Release();
	ibv_release();
}

__declspec(dllexport)
struct ibv_device **ibv_get_device_list(int *num)
{
	WV_DEVICE_ATTRIBUTES attr;
	struct verbs_device *dev_array;
	struct ibv_device **pdev_array;
	NET64 *guid = NULL;
	SIZE_T size, cnt;
	HRESULT hr;

	if (ibv_acquire()) {
		goto err1;
	}

	cnt = 0;
	size = sizeof(NET64);

	while ((size / sizeof(NET64)) > cnt) {
		if (cnt > 0) {
			delete guid;
		}

		cnt = size / sizeof(NET64);
		guid = new NET64[cnt];
		if (guid == NULL) {
			goto err1;
		}

		hr = prov->QueryDeviceList(guid, &size);
		if (FAILED(hr)) {
			goto err2;
		}
	}

	size /= sizeof(NET64);
	dev_array = new struct verbs_device[size];
	pdev_array = new struct ibv_device*[size + 1];
	if (dev_array == NULL || pdev_array == NULL) {
		goto err2;
	}

	for (cnt = 0; cnt < size; cnt++) {
		pdev_array[cnt] = &dev_array[cnt].device;
		hr = prov->QueryDevice(guid[cnt], &attr);
		if (FAILED(hr)) {
			goto err3;
		}

		sprintf(dev_array[cnt].device.name, "ibv_device%d", cnt);
		dev_array[cnt].device.node_type = IBV_NODE_UNKNOWN;
		dev_array[cnt].device.transport_type = (ibv_transport_type) attr.DeviceType;
		dev_array[cnt].guid = guid[cnt];
		dev_array[cnt].phys_port_cnt = attr.PhysPortCount;
	}

	pdev_array[cnt] = NULL;
	if (num != NULL) {
		*num = (int) size;
	}
	delete [] guid;
	return pdev_array;

err3:
	ibv_free_device_list(pdev_array);
err2:
	delete [] guid;
err1:
	return NULL;
}

__declspec(dllexport)
void ibv_free_device_list(struct ibv_device **list)
{
	ibv_release();
	delete CONTAINING_RECORD(list[0], struct verbs_device, device);
	delete list;
}

__declspec(dllexport)
const char *ibv_get_device_name(struct ibv_device *device)
{
	return device->name;
}

__declspec(dllexport)
uint64_t ibv_get_device_guid(struct ibv_device *device)
{
	return CONTAINING_RECORD(device, struct verbs_device, device)->guid;
}

__declspec(dllexport)
struct ibv_context *ibv_open_device(struct ibv_device *device)
{
	struct verbs_device *vdev;
	struct verbs_context *vcontext;
	HRESULT hr;
	int i;

	vdev = CONTAINING_RECORD(device, struct verbs_device, device);
	vcontext = new struct verbs_context;
	if (vcontext == NULL) {
		return NULL;
	}

	ibv_acquire();
	memcpy(&vcontext->device, vdev, sizeof(struct verbs_device));
	vcontext->context.device = &vcontext->device.device;
	vcontext->event_port = NULL;
	vcontext->closing = 0;
	CompChannelInit(&comp_mgr, &vcontext->context.channel, INFINITE);

	vcontext->port = new struct verbs_port[vdev->phys_port_cnt];
	if (vcontext->port == NULL) {
		goto err1;
	}

	hr = prov->OpenDevice(vdev->guid, &vcontext->context.cmd_if);
	if (FAILED(hr)) {
		goto err2;
	}

	for (i = 0; i < vdev->phys_port_cnt; i++) {
		vcontext->port[i].port_num = (uint8_t) i + 1;
		vcontext->port[i].event_flag = 0;
		CompEntryInit(&vcontext->context.channel, &vcontext->port[i].comp_entry);
		vcontext->port[i].comp_entry.Busy = 1;
		vcontext->context.cmd_if->Notify(vcontext->port[i].port_num,
										 &vcontext->port[i].comp_entry.Overlap,
										 &vcontext->port[i].event_flag);
	}

	return &vcontext->context;

err2:
	delete vcontext->port;
err1:
	delete vcontext;
	ibv_release();
	return NULL;
}

__declspec(dllexport)
int ibv_close_device(struct ibv_context *context)
{
	struct verbs_context *vcontext;
	int i;

	vcontext = CONTAINING_RECORD(context, struct verbs_context, context);
	vcontext->closing = 1;
	context->cmd_if->CancelOverlappedRequests();

	for (i = 0; i < vcontext->device.phys_port_cnt; i++) {
		CompEntryCancel(&vcontext->port[i].comp_entry);
	}

	context->cmd_if->Release();
	CompChannelCleanup(&vcontext->context.channel);
	ibv_release();
	delete vcontext->port;
	delete vcontext;
	return 0;
}

static enum ibv_event_type ibv_get_port_event_state(struct verbs_context *vcontext)
{
	WV_PORT_ATTRIBUTES attr;
	HRESULT hr;

	hr = vcontext->context.cmd_if->QueryPort(vcontext->event_port->port_num, &attr);
	if (FAILED(hr)) {
		return IBV_EVENT_PORT_ERR;
	}

	return (attr.State == WvPortActive) ?
		   IBV_EVENT_PORT_ACTIVE : IBV_EVENT_PORT_ERR;
}

static int ibv_report_port_event(struct verbs_context *vcontext,
								 struct ibv_async_event *event)
{
	struct verbs_port *port;
	int ret = 0;

	port = vcontext->event_port;
	event->element.port_num = port->port_num;

	if (port->event_flag & WV_EVENT_ERROR) {
		event->event_type = IBV_EVENT_DEVICE_FATAL;
		port->event_flag = 0;
	} else if (port->event_flag & WV_EVENT_STATE) {
		event->event_type = ibv_get_port_event_state(vcontext);
		port->event_flag = 0;
	} else if (port->event_flag & WV_EVENT_MANAGEMENT) {
		event->event_type = IBV_EVENT_SM_CHANGE;
		port->event_flag = 0;
	} else if (port->event_flag & WV_EVENT_LINK_ADDRESS) {
		event->event_type = IBV_EVENT_LID_CHANGE;
		port->event_flag &= ~WV_EVENT_LINK_ADDRESS;
	} else if (port->event_flag & WV_EVENT_PARTITION) {
		event->event_type = IBV_EVENT_PKEY_CHANGE;
		port->event_flag &= ~WV_EVENT_PARTITION;
	} else {
		port->event_flag = 0;
		ret = -1;
	}
	
	if (port->event_flag == 0 && !vcontext->closing) {
		port->comp_entry.Busy = 1;
		vcontext->context.cmd_if->Notify(vcontext->event_port->port_num,
										 &port->comp_entry.Overlap,
										 &port->event_flag);
		vcontext->event_port = NULL;
	}
	return ret;
}

__declspec(dllexport)
int ibv_get_async_event(struct ibv_context *context,
						struct ibv_async_event *event)
{
	struct verbs_context *vcontext;
	COMP_ENTRY *entry;
	int ret;

	vcontext = CONTAINING_RECORD(context, struct verbs_context, context);
	if (vcontext->event_port) {
		if (ibv_report_port_event(vcontext, event) == 0) {
			return 0;
		}
	}

	ret = CompChannelPoll(&context->channel, &entry);
	if (!ret) {
		vcontext->event_port = CONTAINING_RECORD(entry, struct verbs_port, comp_entry);
		ret = ibv_report_port_event(vcontext, event);
	}

	return ret;
}

__declspec(dllexport)
void ibv_ack_async_event(struct ibv_async_event *event)
{
	// Only device/port level events are currently supported
	// nothing to do here at the moment
}
