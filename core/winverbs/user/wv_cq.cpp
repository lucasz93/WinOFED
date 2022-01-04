/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions Copyright (c) 2009 Microsoft Corporation.  All rights reserved.
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

#include <iba\ib_ci.h>
#include "wv_memory.h"
#include "wv_cq.h"
#include "wv_qp.h"
#include "wv_ioctl.h"

CWVCompletionQueue::CWVCompletionQueue(CWVDevice *pDevice)
{
	pDevice->AddRef();
	m_pDevice = pDevice;
	m_pVerbs = &pDevice->m_Verbs;
	m_hFile = pDevice->m_hFile;

	m_hVerbsCq = NULL;
}

STDMETHODIMP CWVCompletionQueue::
Create(SIZE_T *pEntries)
{
	WV_IO_ID		*pId;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	ci_umv_buf_t	verbsData;
	CWVBuffer		buf;

	stat = m_pVerbs->pre_create_cq(m_pDevice->m_hVerbsDevice, (UINT32 *) pEntries,
								   &verbsData, &m_hVerbsCq);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_ID + max(verbsData.input_size, verbsData.output_size);
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pId->Id = m_pDevice->m_Id;
	pId->VerbInfo = verbsData.command;
	pId->Data = (UINT32) *pEntries;
	RtlCopyMemory(pId + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_CREATE,
						  pId, sizeof WV_IO_ID + verbsData.input_size,
						  pId, sizeof WV_IO_ID + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		m_Id = pId->Id;
		*pEntries = pId->Data;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pId->VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pId + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_create_cq(m_pDevice->m_hVerbsDevice, (ib_api_status_t) hr,
							 (UINT32) *pEntries, &m_hVerbsCq, &verbsData);
	return hr;
}

CWVCompletionQueue::~CWVCompletionQueue()
{
	DWORD	bytes;
	HRESULT	hr;

	if (m_Id != 0) {
		m_pVerbs->pre_destroy_cq(m_hVerbsCq);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_DESTROY, &m_Id, sizeof m_Id,
							   NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_pVerbs->post_destroy_cq(m_hVerbsCq, (ib_api_status_t) hr);
	}
	m_pDevice->Release();
}

STDMETHODIMP CWVCompletionQueue::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVCompletionQueue) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVCompletionQueue::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVCompletionQueue::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVCompletionQueue::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVCompletionQueue::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVCompletionQueue::
Resize(SIZE_T* pEntries)
{
	WV_IO_ID		*pId;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	ci_umv_buf_t	verbsData;
	CWVBuffer		buf;

	stat = m_pVerbs->pre_resize_cq(m_hVerbsCq, (UINT32 *) pEntries, &verbsData);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_ID + max(verbsData.input_size, verbsData.output_size);
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pId->Id = m_Id;
	pId->VerbInfo = verbsData.command;
	pId->Data = (UINT32) *pEntries;
	RtlCopyMemory(pId + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_RESIZE,
						  pId, sizeof WV_IO_ID + verbsData.input_size,
						  pId, sizeof WV_IO_ID + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		*pEntries = pId->Data;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pId->VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pId + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_resize_cq(m_hVerbsCq, (ib_api_status_t) hr,
							 (UINT32) *pEntries, &verbsData);
	return hr;
}

STDMETHODIMP CWVCompletionQueue::
Peek(SIZE_T* pCompletedEntries)
{
	ib_api_status_t	stat;

	stat = m_pVerbs->peek_cq(m_hVerbsCq, (UINT32 *) pCompletedEntries);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	return WV_SUCCESS;
}

STDMETHODIMP CWVCompletionQueue::
Notify(WV_CQ_NOTIFY_TYPE Type, OVERLAPPED* pOverlapped)
{
	WV_IO_ID	id;
	DWORD		bytes;
	HRESULT		hr;

	id.Id = m_Id;
	id.Data = Type;
	if (WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_NOTIFY, &id, sizeof id,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	if (SUCCEEDED(hr) || hr == WV_IO_PENDING) {
		switch (Type) {
		case WvCqSolicited:
			m_pVerbs->rearm_cq(m_hVerbsCq, 1);
			break;
		case WvCqNextCompletion:
			m_pVerbs->rearm_cq(m_hVerbsCq, 0);
			break;
		default:
			break;
		}
	}

	return hr;
}

STDMETHODIMP CWVCompletionQueue::
BatchNotify(SIZE_T CompletedEntries, OVERLAPPED* pOverlapped)
{
	DWORD		bytes;
	HRESULT		hr;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_CQ_NOTIFY, &m_Id, sizeof m_Id,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	if (SUCCEEDED(hr) || hr == WV_IO_PENDING) {
		m_pVerbs->rearm_n_cq(m_hVerbsCq, (UINT32) CompletedEntries);
	}

	return hr;
}

STDMETHODIMP_(SIZE_T) CWVCompletionQueue::
Poll(WV_COMPLETION Completions[], SIZE_T Entries)
{
	// WV_COMPLETION aligns with uvp_wc_t by design.
	return m_pVerbs->poll_cq_array(m_hVerbsCq, (UINT32) Entries, (uvp_wc_t *) Completions);
}
