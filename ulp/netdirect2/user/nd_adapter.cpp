/*
 * Copyright (c) 2009-2010 Intel Corporation. All rights reserved.
 * Copyright (c) 2010 Microsoft Corporation.  All rights reserved.
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

#include "nd_adapter.h"
#include "nd_cq.h"
#include "nd_listen.h"
#include "nd_connect.h"
#include "nd_mw.h"
#include "nd_qp.h"
#include "nd_srq.h"
#include "nd_ep.h"

CNDAdapter::CNDAdapter(CNDProvider *pProvider)
{
	pProvider->AddRef();
	m_pProvider = pProvider;
	m_pWvDevice = NULL;
	m_pWvPd = NULL;
	m_MaxInlineSend = 0;
}

STDMETHODIMP CNDAdapter::
Init(UINT64 adapterId)
{
	HRESULT hr;
	TCHAR val[16];
	DWORD ret;

	m_DeviceGuid = adapterId;
	hr = m_pProvider->m_pWvProvider->OpenDevice(m_DeviceGuid, &m_pWvDevice);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvDevice->AllocateProtectionDomain(&m_pWvPd);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	ret = GetEnvironmentVariable("IBNDPROV_MAX_INLINE_SIZE", val, 16);
	m_MaxInlineSend = (ret > 0 && ret <= 16) ? (SIZE_T) strtoul(val, NULL, 16) : 160;
	return ND_SUCCESS;
}

CNDAdapter::~CNDAdapter(void)
{
	if (m_pWvPd != NULL) {
		m_pWvPd->Release();
	}
	if (m_pWvDevice != NULL) {
		m_pWvDevice->Release();
	}
	m_pProvider->Release();
}

STDMETHODIMP CNDAdapter::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDAdapter) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDAdapter::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDAdapter::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP_(HANDLE) CNDAdapter::
GetFileHandle(void)
{
	return m_pProvider->m_pWvProvider->GetFileHandle();
}

STDMETHODIMP CNDAdapter::
Query(ND_ADAPTER_INFO* pInfo, SIZE_T* pcbInfo)
{
	WV_DEVICE_ATTRIBUTES attr;
	HRESULT hr;

	if (*pcbInfo < sizeof(ND_ADAPTER_INFO)) {
		hr = ND_BUFFER_OVERFLOW;
		goto out;
	}

	if (pInfo != NULL && pInfo->InfoVersion != 1) {
		hr = ND_NOT_SUPPORTED;
		goto out;
	}

	hr = NDConvertWVStatus(m_pWvDevice->Query(&attr));
	if (FAILED(hr)) {
		goto out;
	}

	pInfo->VendorId					= (UINT16) attr.VendorId;
	pInfo->DeviceId					= (UINT16) attr.VendorPartId;
	pInfo->AdapterId				= m_DeviceGuid;
	pInfo->MaxRegistrationSize		= attr.MaxMrSize;
	pInfo->MaxWindowSize			= attr.MaxMrSize;
    pInfo->MaxReceiveSge			= (DWORD) attr.MaxSge;
    pInfo->MaxInitiatorSge			= (DWORD) attr.MaxSge;
    pInfo->MaxReadSge				= (DWORD) attr.MaxSge;
    pInfo->MaxTransferLength		= 1 << 31;
	pInfo->MaxInboundReadLimit		= (DWORD) attr.MaxQpResponderResources;
	pInfo->MaxOutboundReadLimit		= (DWORD) attr.MaxQpInitiatorDepth;
    pInfo->MaxReceiveQueueDepth		= (DWORD) attr.MaxQpWr;
    pInfo->MaxInitiatorQueueDepth	= (DWORD) attr.MaxQpWr;
    pInfo->MaxSharedReceiveQueueDepth = (DWORD) attr.MaxSrqWr;
    pInfo->MaxCompletionQueueDepth	= (DWORD) attr.MaxCqEntries;
    pInfo->InlineRequestThreshold	= m_MaxInlineSend;
    pInfo->LargeRequestThreshold	= 0;
	pInfo->MaxCallerData			= ND_PRIVATE_DATA_SIZE;
	pInfo->MaxCalleeData			= ND_PRIVATE_DATA_SIZE;
    pInfo->InlineDataFactor			= 0;
    pInfo->InlineDataAdjustment		= -((LONG) attr.MaxInlineSend);
    pInfo->InOrderDMA				= TRUE;
    pInfo->SupportsCQResize			= TRUE;
    pInfo->SupportsLoopbackConnections = TRUE;

out:
	*pcbInfo = sizeof(ND_ADAPTER_INFO);
	return hr;
}

STDMETHODIMP CNDAdapter::
QueryAddressList(SOCKET_ADDRESS_LIST* pAddressList, SIZE_T* pcbAddressList)
{
	return m_pProvider->QueryAdapterAddressList(pAddressList, pcbAddressList, m_DeviceGuid);
}

STDMETHODIMP CNDAdapter::
CreateCompletionQueue(REFIID iid, DWORD queueDepth, USHORT group, KAFFINITY affinity,
					  VOID** ppCompletionQueue)
{
	return CNDCompletionQueue::CreateInstance(this, queueDepth, group,
											  affinity, ppCompletionQueue);
}

STDMETHODIMP CNDAdapter::
CreateMemoryRegion(REFIID iid, VOID** ppMemoryRegion)
{
	if (iid != IID_INDMemoryRegion) {
		return E_NOINTERFACE;
	}

	return CNDMemoryRegion::CreateInstance(this, ppMemoryRegion);
}

STDMETHODIMP CNDAdapter::
CreateMemoryWindow(REFIID iid, VOID** ppMemoryWindow)
{
	if (iid != IID_INDMemoryWindow) {
		return E_NOINTERFACE;
	}

	return CNDMemoryWindow::CreateInstance(this, ppMemoryWindow);
}

STDMETHODIMP CNDAdapter::
CreateSharedReceiveQueue(REFIID iid, DWORD queueDepth, DWORD maxSge,
						 DWORD notifyThreshold, USHORT group,
						 KAFFINITY affinity, VOID** ppSharedReceiveQueue)
{
	if (iid != IID_INDSharedReceiveQueue) {
		return E_NOINTERFACE;
	}

	return CNDSharedReceiveQueue::CreateInstance(this, queueDepth, maxSge,
												 notifyThreshold, group, affinity,
												 ppSharedReceiveQueue);
}

STDMETHODIMP CNDAdapter::
CreateQueuePair(REFIID iid, IUnknown* pReceiveCompletionQueue,
				IUnknown* pInitiatorCompletionQueue, VOID* context,
				DWORD receiveQueueDepth, DWORD initiatorQueueDepth,
				DWORD maxReceiveRequestSge, DWORD maxInitiatorRequestSge,
				VOID** ppQueuePair)
{
	CNDCompletionQueue *rcq = (CNDCompletionQueue *) pReceiveCompletionQueue;
	CNDCompletionQueue *icq = (CNDCompletionQueue *) pInitiatorCompletionQueue;

	if (iid != IID_INDQueuePair) {
		return E_NOINTERFACE;
	}

	return CNDQueuePair::CreateInstance(this, rcq, icq, NULL,
										context, receiveQueueDepth, initiatorQueueDepth,
										maxReceiveRequestSge, maxInitiatorRequestSge,
										ppQueuePair);
}

STDMETHODIMP CNDAdapter::
CreateQueuePairWithSrq(REFIID iid, IUnknown* pReceiveCompletionQueue,
					   IUnknown* pInitiatorCompletionQueue,
					   IUnknown* pSharedReceiveQueue, VOID* context,
					   DWORD initiatorQueueDepth, DWORD maxInitiatorRequestSge,
					   VOID** ppQueuePair)
{
	CNDCompletionQueue *rcq = (CNDCompletionQueue *) pReceiveCompletionQueue;
	CNDCompletionQueue *icq = (CNDCompletionQueue *) pInitiatorCompletionQueue;
	CNDSharedReceiveQueue *srq = (CNDSharedReceiveQueue *) pSharedReceiveQueue;

	if (iid != IID_INDQueuePair) {
		return E_NOINTERFACE;
	}

	return CNDQueuePair::CreateInstance(this, rcq, icq, srq,
										context, 0, initiatorQueueDepth, 0,
										maxInitiatorRequestSge, ppQueuePair);
}

STDMETHODIMP CNDAdapter::
CreateSharedEndpoint(REFIID iid, VOID** ppSharedEndpoint)
{
	if (iid != IID_INDSharedEndpoint) {
		return E_NOINTERFACE;
	}

	return CNDSharedEndpoint::CreateInstance(this, ppSharedEndpoint);
}

STDMETHODIMP CNDAdapter::
CreateConnector(REFIID iid, VOID** ppConnector)
{
	if (iid != IID_INDConnector) {
		return E_NOINTERFACE;
	}

	return CNDConnector::CreateInstance(this, ppConnector);
}

STDMETHODIMP CNDAdapter::
CreateListen(REFIID iid, VOID** ppListen)
{
	if (iid != IID_INDListen) {
		return E_NOINTERFACE;
	}

	return CNDListen::CreateInstance(this, ppListen);
}
