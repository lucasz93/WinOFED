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

#include "wv_base.h"
#include "wv_memory.h"
#include "wv_srq.h"
#include "wv_ioctl.h"

CWVSharedReceiveQueue::CWVSharedReceiveQueue(CWVProtectionDomain *pPd)
{
	pPd->AddRef();
	m_pPd = pPd;
	m_pVerbs = pPd->m_pVerbs;
	m_hFile = pPd->m_hFile;
	m_hVerbsSrq = NULL;
}

STDMETHODIMP CWVSharedReceiveQueue::
Create(SIZE_T MaxWr, SIZE_T MaxSge, SIZE_T SrqLimit)
{
	WV_IO_SRQ_ATTRIBUTES	*pattr;
	DWORD					bytes;
	ib_api_status_t			stat;
	HRESULT					hr;
	ci_umv_buf_t			verbsData;
	CWVBuffer				buf;
	ib_srq_attr_t			attr;

	attr.max_sge = (UINT32) MaxSge;
	attr.max_wr = (UINT32) MaxWr;
	attr.srq_limit = (UINT32) SrqLimit;
	stat = m_pVerbs->pre_create_srq(m_pPd->m_hVerbsPd, &attr,
									&verbsData, &m_hVerbsSrq);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_SRQ_ATTRIBUTES + max(verbsData.input_size, verbsData.output_size);
	pattr = (WV_IO_SRQ_ATTRIBUTES *) buf.Get(bytes);
	if (pattr == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pattr->Id.Id = m_pPd->m_Id;
	pattr->Id.VerbInfo = verbsData.command;
	pattr->MaxSge = attr.max_sge;
	pattr->MaxWr = attr.max_wr;
	pattr->SrqLimit = attr.srq_limit;
	pattr->Reserved = 0;
	RtlCopyMemory(pattr + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_CREATE,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.input_size,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		m_Id = pattr->Id.Id;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pattr->Id.VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pattr + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_create_srq(m_pPd->m_hVerbsPd, (ib_api_status_t) hr,
							  &m_hVerbsSrq, &verbsData);
	return hr;
}

CWVSharedReceiveQueue::~CWVSharedReceiveQueue()
{
	DWORD	bytes;
	HRESULT	hr;

	if (m_Id != 0) {
		m_pVerbs->pre_destroy_srq(m_hVerbsSrq);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_DESTROY, &m_Id, sizeof m_Id,
							   NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_pVerbs->post_destroy_srq(m_hVerbsSrq, (ib_api_status_t) hr);
	}
	m_pPd->Release();
}

STDMETHODIMP CWVSharedReceiveQueue::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVSharedReceiveQueue) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVSharedReceiveQueue::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVSharedReceiveQueue::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVSharedReceiveQueue::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVSharedReceiveQueue::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVSharedReceiveQueue::
Query(SIZE_T* pMaxWr, SIZE_T* pMaxSge, SIZE_T* pSrqLimit)
{
	WV_IO_SRQ_ATTRIBUTES	*pattr;
	DWORD					bytes;
	ib_api_status_t			stat;
	HRESULT					hr;
	ci_umv_buf_t			verbsData;
	CWVBuffer				buf;
	ib_srq_attr_t			attr;

	stat = m_pVerbs->pre_query_srq(m_hVerbsSrq, &verbsData);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_SRQ_ATTRIBUTES + max(verbsData.input_size, verbsData.output_size);
	pattr = (WV_IO_SRQ_ATTRIBUTES *) buf.Get(bytes);
	if (pattr == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pattr->Id.Id = m_Id;
	pattr->Id.VerbInfo = verbsData.command;
	pattr->Reserved = 0;
	RtlCopyMemory(pattr + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_QUERY,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.input_size,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		attr.max_sge = pattr->MaxSge;
		attr.max_wr = pattr->MaxWr;
		attr.srq_limit = pattr->SrqLimit;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pattr->Id.VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pattr + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_query_srq(m_hVerbsSrq, (ib_api_status_t) hr,
							  &attr, &verbsData);
	if (SUCCEEDED(hr)) {
		*pMaxWr = attr.max_wr;
		*pMaxSge = attr.max_sge;
		*pSrqLimit = attr.srq_limit;
	}
	return hr;
}

STDMETHODIMP CWVSharedReceiveQueue::
Modify(SIZE_T MaxWr, SIZE_T SrqLimit)
{
	WV_IO_SRQ_ATTRIBUTES	*pattr;
	DWORD					bytes;
	ib_api_status_t			stat;
	HRESULT					hr;
	ci_umv_buf_t			verbsData;
	CWVBuffer				buf;
	ib_srq_attr_t			attr;

	attr.max_wr = (UINT32) MaxWr;
	attr.srq_limit = (UINT32) SrqLimit;
	stat = m_pVerbs->pre_modify_srq(m_hVerbsSrq, &attr, (ib_srq_attr_mask_t)
									(IB_SRQ_MAX_WR & IB_SRQ_LIMIT), &verbsData);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_SRQ_ATTRIBUTES + max(verbsData.input_size, verbsData.output_size);
	pattr = (WV_IO_SRQ_ATTRIBUTES *) buf.Get(bytes);
	if (pattr == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pattr->Id.Id = m_Id;
	pattr->Id.VerbInfo = verbsData.command;
	pattr->MaxWr = attr.max_wr;
	pattr->SrqLimit = attr.srq_limit;
	pattr->Reserved = 0;
	RtlCopyMemory(pattr + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_MODIFY,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.input_size,
						  pattr, sizeof WV_IO_SRQ_ATTRIBUTES + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pattr->Id.VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pattr + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_modify_srq(m_hVerbsSrq, (ib_api_status_t) hr, &verbsData);
	return hr;
}

STDMETHODIMP CWVSharedReceiveQueue::
PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge)
{
	ib_recv_wr_t	wr, *pwr;
	ib_api_status_t	stat;
	HRESULT			hr;

	wr.p_next = NULL;
	wr.wr_id = WrId;
	wr.num_ds = (UINT32) nSge;
	wr.ds_array = WvConvertSgl(pSgl, nSge);

	stat = m_pVerbs->post_srq_recv(m_hVerbsSrq, &wr, &pwr);
	if (stat == IB_SUCCESS) {
		hr = WV_SUCCESS;
	} else {
		hr = WvConvertIbStatus(stat);
	}

	return hr;
}

STDMETHODIMP CWVSharedReceiveQueue::
Notify(OVERLAPPED* pOverlapped)
{
	DWORD		bytes;
	HRESULT		hr;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_SRQ_NOTIFY, &m_Id, sizeof m_Id,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}
