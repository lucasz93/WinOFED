/*
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
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
#include "nd_ep.h"


CNDAdapter::CNDAdapter(CNDProvider *pProvider)
{
	pProvider->AddRef();
	m_pProvider = pProvider;
	m_pWvProvider = NULL;
	m_pWvDevice = NULL;
	m_pWvPd = NULL;
	DListInit(&m_MrList);
	InitializeCriticalSection(&m_Lock);
}

STDMETHODIMP CNDAdapter::
Init(const struct sockaddr *pAddress, SIZE_T AddressLength)
{
	HRESULT hr;

	hr = WvGetObject(IID_IWVProvider, (LPVOID *) &m_pWvProvider);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvProvider->TranslateAddress(pAddress, &m_DevAddress);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvProvider->OpenDevice(m_DevAddress.DeviceGuid, &m_pWvDevice);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	hr = m_pWvDevice->AllocateProtectionDomain(&m_pWvPd);
	if (FAILED(hr)) {
		return NDConvertWVStatus(hr);
	}

	RtlCopyMemory(&m_Address, pAddress, AddressLength);
	return ND_SUCCESS;
}

CNDAdapter::~CNDAdapter(void)
{
	ND_MR	*mr;

	while (!DListEmpty(&m_MrList)) {
		mr = CONTAINING_RECORD(m_MrList.Next, ND_MR, Entry);
		DListRemove(&mr->Entry);
		m_pWvPd->DeregisterMemory(mr->Keys.Lkey, NULL);
		delete mr;
	}

	if (m_pWvPd != NULL) {
		m_pWvPd->Release();
	}
	if (m_pWvDevice != NULL) {
		m_pWvDevice->Release();
	}
	if (m_pWvProvider != NULL) {
		m_pWvProvider->Release();
	}
	m_pProvider->Release();
	DeleteCriticalSection(&m_Lock);
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

STDMETHODIMP CNDAdapter::
CancelOverlappedRequests(void)
{
	m_pWvPd->CancelOverlappedRequests();
	return ND_SUCCESS;
}

STDMETHODIMP CNDAdapter::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					SIZE_T *pNumberOfBytesTransferred, BOOL bWait)
{
	DLIST_ENTRY *entry;
	ND_MR *mr;
	HRESULT hr;

	::GetOverlappedResult(GetFileHandle(), pOverlapped,
						  (LPDWORD) pNumberOfBytesTransferred, bWait);
	hr = (HRESULT) pOverlapped->Internal;

	if (FAILED(hr)) {
		EnterCriticalSection(&m_Lock);
		for (entry = m_MrList.Next; entry != &m_MrList; entry = entry->Next) {
			mr = CONTAINING_RECORD(entry, ND_MR, Entry);
			if (mr->Context == pOverlapped) {
				DListRemove(entry);
				delete mr;
				break;
			}
		}
		LeaveCriticalSection(&m_Lock);
	}

	return NDConvertWVStatus(hr);
}

STDMETHODIMP_(HANDLE) CNDAdapter::
GetFileHandle(void)
{
	return m_pWvProvider->GetFileHandle();
}

STDMETHODIMP CNDAdapter::
Query(DWORD VersionRequested, ND_ADAPTER_INFO* pInfo, SIZE_T* pBufferSize)
{
	WV_DEVICE_ATTRIBUTES attr;
	HRESULT hr;

	if (VersionRequested != 1) {
		return ND_NOT_SUPPORTED;
	}

	if (*pBufferSize < sizeof(ND_ADAPTER_INFO)) {
		hr = ND_BUFFER_OVERFLOW;
		goto out;
	}

	hr = m_pWvDevice->Query(&attr);
	if (FAILED(hr)) {
		goto out;
	}

	pInfo->VendorId					= attr.VendorId;
	pInfo->DeviceId					= attr.VendorPartId;
	pInfo->MaxInboundSge			= min(attr.MaxSge, ND_MAX_SGE);
	pInfo->MaxInboundRequests		= attr.MaxQpWr;
	pInfo->MaxInboundLength			= 1 << 31;
	pInfo->MaxOutboundSge			= min(attr.MaxSge, ND_MAX_SGE);
	pInfo->MaxOutboundRequests		= attr.MaxQpWr;
	pInfo->MaxOutboundLength		= 1 << 31;
	pInfo->MaxInlineData			= attr.MaxInlineSend;
	pInfo->MaxInboundReadLimit		= attr.MaxQpResponderResources;
	pInfo->MaxOutboundReadLimit		= attr.MaxQpInitiatorDepth;
	pInfo->MaxCqEntries				= attr.MaxCqEntries;
	pInfo->MaxRegistrationSize		= attr.MaxMrSize;
	pInfo->MaxWindowSize			= attr.MaxMrSize;
	pInfo->LargeRequestThreshold	= 0;
	pInfo->MaxCallerData			= ND_PRIVATE_DATA_SIZE;
	pInfo->MaxCalleeData			= ND_PRIVATE_DATA_SIZE;

out:
	*pBufferSize = sizeof(ND_ADAPTER_INFO);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDAdapter::
Control(DWORD IoControlCode, const void* pInBuffer, SIZE_T InBufferSize,
		void* pOutBuffer, SIZE_T OutBufferSize, SIZE_T* pBytesReturned,
		OVERLAPPED* pOverlapped)
{
	return DeviceIoControl(GetFileHandle(), IoControlCode,
						   (LPVOID) pInBuffer, (DWORD) InBufferSize,
						   pOutBuffer, (DWORD) OutBufferSize,
						   (LPDWORD) pBytesReturned, pOverlapped) ?
						   ND_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CNDAdapter::
CreateCompletionQueue(SIZE_T nEntries, INDCompletionQueue** ppCq)
{
	return CNDCompletionQueue::CreateInstance(this, nEntries, ppCq);
}

STDMETHODIMP CNDAdapter::
RegisterMemory(const void* pBuffer, SIZE_T BufferSize,
			   OVERLAPPED* pOverlapped, ND_MR_HANDLE* phMr)
{
	ND_MR *mr;
	HRESULT hr;
	DWORD flags;

	mr = new ND_MR;
	if (mr == NULL) {
		return ND_NO_MEMORY;
	}

	mr->Context = pOverlapped;
	EnterCriticalSection(&m_Lock);
	DListInsertHead(&mr->Entry, &m_MrList);
	LeaveCriticalSection(&m_Lock);

	// TODO: restrict access when MWs are implemented
	flags = WV_ACCESS_REMOTE_READ | WV_ACCESS_REMOTE_WRITE |
			WV_ACCESS_REMOTE_ATOMIC | WV_ACCESS_LOCAL_WRITE | WV_ACCESS_MW_BIND;
	hr = m_pWvPd->RegisterMemory(pBuffer, BufferSize, flags, pOverlapped, &mr->Keys);
	if (SUCCEEDED(hr) || hr == WV_IO_PENDING) {
		*phMr = (ND_MR_HANDLE) mr;
	} else {
		CleanupMr(mr);
	}

	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDAdapter::
DeregisterMemory(ND_MR_HANDLE hMr, OVERLAPPED* pOverlapped)
{
	ND_MR *mr;
	HRESULT hr;

	mr = (ND_MR *) hMr;
	hr = m_pWvPd->DeregisterMemory(mr->Keys.Lkey, pOverlapped);
	if (SUCCEEDED(hr) || hr == WV_IO_PENDING) {
		CleanupMr(mr);
	}
	return NDConvertWVStatus(hr);
}

void CNDAdapter::
CleanupMr(ND_MR *pMr)
{
	EnterCriticalSection(&m_Lock);
	DListRemove(&pMr->Entry);
	LeaveCriticalSection(&m_Lock);
	delete pMr;
}

STDMETHODIMP CNDAdapter::
CreateMemoryWindow(ND_RESULT* pInvalidateResult, INDMemoryWindow** ppMw)
{
	// TODO: do something with pInvalidateResult
	return CNDMemoryWindow::CreateInstance(this, ppMw);
}

STDMETHODIMP CNDAdapter::
CreateConnector(INDConnector** ppConnector)
{
	return CNDConnector::CreateInstance(this, ppConnector);
}

STDMETHODIMP CNDAdapter::
Listen(SIZE_T Backlog, INT Protocol, USHORT Port,
	   USHORT* pAssignedPort, INDListen** ppListen)
{
	return CNDListen::CreateInstance(this, Backlog, Protocol, Port,
									 pAssignedPort, ppListen);
}
