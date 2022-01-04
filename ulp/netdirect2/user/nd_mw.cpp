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

#include "nd_mw.h"


CNDMemoryRegion::CNDMemoryRegion(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	RtlZeroMemory(&m_Keys, sizeof(m_Keys));
	m_pOverlapped = NULL;
}

CNDMemoryRegion::~CNDMemoryRegion()
{
	m_pAdapter->Release();
}

STDMETHODIMP CNDMemoryRegion::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDMemoryRegion) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDMemoryRegion::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDMemoryRegion::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP CNDMemoryRegion::
CancelOverlappedRequests(void)
{
	if (m_pOverlapped != NULL) {
		CancelIoEx(m_pAdapter->GetFileHandle(), m_pOverlapped);
	}

	return ND_SUCCESS;
}

STDMETHODIMP CNDMemoryRegion::
GetOverlappedResult(OVERLAPPED *pOverlapped, BOOL bWait)
{
	DWORD bytes;
	HRESULT hr;

	hr = NDConvertWVStatus(m_pAdapter->m_pWvPd->
						   GetOverlappedResult(pOverlapped, &bytes, bWait));
	if (hr != ND_PENDING) {
		m_pOverlapped = NULL;
	}

	return hr;
}

DWORD CNDMemoryRegion::
ConvertAccessFlags(DWORD Flags)
{
	DWORD opts = 0;

	if (Flags & ND_MR_FLAG_ALLOW_LOCAL_WRITE) {
		opts |= WV_ACCESS_LOCAL_WRITE;
	}
	if (Flags & ND_MR_FLAG_ALLOW_REMOTE_READ) {
		opts |= WV_ACCESS_REMOTE_READ | WV_ACCESS_MW_BIND;
	}
	if (Flags & ND_MR_FLAG_ALLOW_REMOTE_WRITE) {
		opts |= WV_ACCESS_REMOTE_WRITE | WV_ACCESS_MW_BIND;
	}
	if (!(Flags & ND_MR_FLAG_DO_NOT_SECURE_VM)) {
		opts |= WV_ACCESS_CACHABLE;
	}

	return opts;
}

STDMETHODIMP CNDMemoryRegion::
Register(const VOID* pBuffer, SIZE_T cbBuffer, DWORD flags, OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	m_pOverlapped = pOverlapped;
	hr = m_pAdapter->m_pWvPd->RegisterMemory(pBuffer, cbBuffer, ConvertAccessFlags(flags),
											 pOverlapped, &m_Keys);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP CNDMemoryRegion::
Deregister(OVERLAPPED* pOverlapped)
{
	HRESULT hr;

	m_pOverlapped = pOverlapped;
	hr = m_pAdapter->m_pWvPd->DeregisterMemory(m_Keys.Lkey, pOverlapped);
	return NDConvertWVStatus(hr);
}

STDMETHODIMP_(UINT32) CNDMemoryRegion::
GetLocalToken()
{
	return m_Keys.Lkey;
}

STDMETHODIMP_(UINT32) CNDMemoryRegion::
GetRemoteToken()
{
	return m_Keys.Rkey;
}


CNDMemoryWindow::CNDMemoryWindow(CNDAdapter *pAdapter)
{
	pAdapter->AddRef();
	m_pAdapter = pAdapter;
	m_pMr = NULL;
}

CNDMemoryWindow::~CNDMemoryWindow()
{
	m_pAdapter->Release();
}

STDMETHODIMP CNDMemoryWindow::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_INDMemoryWindow) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return ND_SUCCESS;
}

STDMETHODIMP_(ULONG) CNDMemoryWindow::
AddRef(void)
{
	return CNDBase::AddRef();
}

STDMETHODIMP_(ULONG) CNDMemoryWindow::
Release(void)
{
	return CNDBase::Release();
}

STDMETHODIMP_(UINT32) CNDMemoryWindow::
GetRemoteToken()
{
	return m_pMr->GetRemoteToken();
}
