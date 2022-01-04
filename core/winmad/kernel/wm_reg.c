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

#include <iba\ib_al.h>
#include "wm_driver.h"
#include "wm_reg.h"
#include "wm_ioctl.h"

WM_REGISTRATION *WmRegAcquire(WM_PROVIDER *pProvider, UINT64 Id)
{
	WM_REGISTRATION *reg;

	KeAcquireGuardedMutex(&pProvider->Lock);
	WmProviderDisableRemove(pProvider);
	reg = IndexListAt(&pProvider->RegIndex, (SIZE_T) Id);
	if (reg != NULL && reg->pDevice != NULL) {
		InterlockedIncrement(&reg->Ref);
	} else {
		reg = NULL;
		WmProviderEnableRemove(pProvider);
	}
	KeReleaseGuardedMutex(&pProvider->Lock);
	return reg;
}

void WmRegRelease(WM_REGISTRATION *pRegistration)
{
	WmProviderEnableRemove(pRegistration->pProvider);
	InterlockedDecrement(&pRegistration->Ref);
}

static WM_REGISTRATION *WmRegAlloc(WM_PROVIDER *pProvider)
{
	WM_REGISTRATION *reg;

	reg = ExAllocatePoolWithTag(NonPagedPool, sizeof(WM_REGISTRATION), 'grmw');
	if (reg == NULL) {
		return NULL;
	}

	RtlZeroMemory(reg, sizeof(WM_REGISTRATION));
	reg->Ref = 1;

	reg->pProvider = pProvider;
	WmProviderGet(pProvider);
	return reg;
}

static int WmConvertMethods(ib_mad_svc_t *svc, WM_IO_REGISTER *pAttributes)
{
	int i, j, unsolicited = 0;

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 8; j++) {
			if (((pAttributes->Methods[i] >> j) & 0x01) != 0) {
				svc->method_array[i * 8 + j] = 1;
				unsolicited = 1;
			}
		}
	}
	return unsolicited;
}

static NTSTATUS WmRegInit(WM_REGISTRATION *pRegistration, WM_IO_REGISTER *pAttributes)
{
	WM_IB_DEVICE		*dev;
	ib_qp_create_t		attr;
	ib_mad_svc_t		svc;
	ib_port_attr_mod_t	port_cap;
	ib_api_status_t		ib_status;
	NTSTATUS			status;

	RtlZeroMemory(&attr, sizeof attr);
	if (pAttributes->Qpn == 0) {
		attr.qp_type = IB_QPT_QP0_ALIAS;
	} else if (pAttributes->Qpn == IB_QP1) {
		attr.qp_type = IB_QPT_QP1_ALIAS;
	} else {
		return STATUS_BAD_NETWORK_PATH;
	}

	dev = WmIbDeviceGet(pAttributes->Guid);
	if (dev == NULL) {
		return STATUS_NO_SUCH_DEVICE;
	}

	pRegistration->PortNum = pAttributes->Port;
	if (--pAttributes->Port > dev->PortCount) {
		status = STATUS_INVALID_PORT_HANDLE;
		goto err1;
	}

	ib_status = dev->IbInterface.open_al_trk(AL_WLOCATION, &pRegistration->hIbal);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err1;
	}
	
	ib_status = dev->IbInterface.open_ca(pRegistration->hIbal, pAttributes->Guid,
										 NULL, NULL, &pRegistration->hCa);
	if (ib_status != IB_SUCCESS) {
        status = STATUS_UNSUCCESSFUL;
		goto err2;
	}
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,"REF_CNT_DBG: WM open_ca %p\n", pRegistration->hCa);

	ib_status = dev->IbInterface.alloc_pd(pRegistration->hCa, IB_PDT_ALIAS,
										  NULL, &pRegistration->hPd);
	if (ib_status != IB_SUCCESS) {
        status = STATUS_UNSUCCESSFUL;
		goto err3;
	}

	attr.sq_depth = attr.rq_depth = 1;
	attr.sq_sge = attr.rq_sge = 1;
	attr.sq_signaled = 1;

	ib_status = dev->IbInterface.get_spl_qp(pRegistration->hPd,
											dev->pPortArray[pAttributes->Port].Guid,
											&attr, pRegistration, NULL,
											&pRegistration->hMadPool,
											&pRegistration->hQp);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err4;
	}

	RtlZeroMemory(&svc, sizeof svc);
	svc.mad_svc_context = pRegistration;
	svc.pfn_mad_send_cb = WmSendHandler;
	svc.pfn_mad_recv_cb = WmReceiveHandler;
	svc.support_unsol = WmConvertMethods(&svc, pAttributes);
	svc.mgmt_class = pAttributes->Class;
	svc.mgmt_version = pAttributes->Version;
	svc.svc_type = IB_MAD_SVC_DEFAULT;

	pRegistration->pDevice = dev;

	ib_status = dev->IbInterface.reg_mad_svc(pRegistration->hQp, &svc,
											 &pRegistration->hService);
	if (ib_status != IB_SUCCESS) {
		status = STATUS_UNSUCCESSFUL;
		goto err5;
	}

	if (svc.mgmt_class == IB_MCLASS_SUBN_DIR && svc.support_unsol) {
		RtlZeroMemory(&port_cap, sizeof port_cap);
		port_cap.cap.sm = 1;
		ib_status = dev->IbInterface.modify_ca(pRegistration->hCa,
											   pRegistration->PortNum,
											   IB_CA_MOD_IS_SM, &port_cap);
		if (ib_status != IB_SUCCESS) {
			status = STATUS_UNSUCCESSFUL;
			goto err5;
		}
		pRegistration->PortCapMask = IB_CA_MOD_IS_SM;
	}

	return STATUS_SUCCESS;

err5:
	dev->IbInterface.destroy_qp(pRegistration->hQp, NULL);
err4:
	dev->IbInterface.dealloc_pd(pRegistration->hPd, NULL);
err3:
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WM close_ca error %p\n", pRegistration->hCa);
	dev->IbInterface.close_ca(pRegistration->hCa, NULL);
err2:
	dev->IbInterface.close_al(pRegistration->hIbal);
err1:
	WmIbDevicePut(dev);
	pRegistration->pDevice = NULL;
	return status;
}

void WmRegister(WM_PROVIDER *pProvider, WDFREQUEST Request)
{
	WM_REGISTRATION	*reg;
	WM_IO_REGISTER	*attr;
	UINT64			*id;
	NTSTATUS		status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(WM_IO_REGISTER), &attr, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}
	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto err1;
	}

	reg = WmRegAlloc(pProvider);
	if (reg == NULL) {
		status = STATUS_NO_MEMORY;
		goto err1;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WmProviderDisableRemove(pProvider);
	KeReleaseGuardedMutex(&pProvider->Lock);

	status = WmRegInit(reg, attr);
	if (!NT_SUCCESS(status)) {
		goto err2;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	reg->Id = IndexListInsertHead(&pProvider->RegIndex, reg);
	if (reg->Id == 0) {
		status = STATUS_NO_MEMORY;
		goto err2;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	WmProviderEnableRemove(pProvider);
	*id = reg->Id;
	WdfRequestCompleteWithInformation(Request, status, sizeof(UINT64));
	return;

err2:
	WmRegFree(reg);
	WmProviderEnableRemove(pProvider);
err1:
	WdfRequestComplete(Request, status);
}

void WmDeregister(WM_PROVIDER *pProvider, WDFREQUEST Request)
{
	WM_REGISTRATION *reg;
	UINT64			*id;
	NTSTATUS		status;

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(UINT64), &id, NULL);
	if (!NT_SUCCESS(status)) {
		goto out;
	}

	KeAcquireGuardedMutex(&pProvider->Lock);
	WmProviderDisableRemove(pProvider);
	reg = IndexListAt(&pProvider->RegIndex, (SIZE_T) *id);
	if (reg == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
	} else if (reg->Ref > 1) {
		status = STATUS_ACCESS_DENIED;
	} else {
		IndexListRemove(&pProvider->RegIndex, (SIZE_T) *id);
		status = STATUS_SUCCESS;
	}
	KeReleaseGuardedMutex(&pProvider->Lock);

	if (NT_SUCCESS(status)) {
		WmRegFree(reg);
	}
	WmProviderEnableRemove(pProvider);
out:
	WdfRequestComplete(Request, status);
}

void WmRegFree(WM_REGISTRATION *pRegistatration)
{
	WmRegRemoveHandler(pRegistatration);
	WmProviderPut(pRegistatration->pProvider);
	ExFreePoolWithTag(pRegistatration, 'grmw');
}

void WmRegRemoveHandler(WM_REGISTRATION *pRegistration)
{
	ib_port_attr_mod_t	port_cap;

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WmRegRemoveHandler enter\n");

	if (pRegistration->pDevice == NULL) {
		WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
			"REF_CNT_DBG: WmRegRemoveHandler pRegistration->pDevice == NULL\n");
		return;
	}

	if (pRegistration->PortCapMask) {
		RtlZeroMemory(&port_cap.cap, sizeof(port_cap.cap));
		pRegistration->pDevice->IbInterface.modify_ca(pRegistration->hCa,
													  pRegistration->PortNum,
													  pRegistration->PortCapMask,
													  &port_cap);
	}

	WmProviderDeregister(pRegistration->pProvider, pRegistration);
	pRegistration->pDevice->IbInterface.destroy_qp(pRegistration->hQp, NULL);
	pRegistration->pDevice->IbInterface.dealloc_pd(pRegistration->hPd, NULL);
	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WM close_ca %p \n", pRegistration->hCa);
	pRegistration->pDevice->IbInterface.close_ca(pRegistration->hCa, NULL);
	pRegistration->pDevice->IbInterface.close_al(pRegistration->hIbal);

	WmIbDevicePut(pRegistration->pDevice);
	pRegistration->pDevice = NULL;

	WmDbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL,
		"REF_CNT_DBG: WmRegRemoveHandler exit\n");
}
