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

#include "wv_base.h"
#include "wv_memory.h"
#include "wv_pd.h"
#include "wv_srq.h"
#include "wv_qp.h"
#include "wv_ioctl.h"

CWVProtectionDomain::CWVProtectionDomain(CWVDevice *pDevice)
{
	pDevice->AddRef();
	m_pDevice = pDevice;
	m_pVerbs = &pDevice->m_Verbs;
	m_hFile = pDevice->m_hFile;
	m_hVerbsPd = NULL;
}

STDMETHODIMP CWVProtectionDomain::
Allocate(void)
{
	WV_IO_ID		*pId;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	ci_umv_buf_t	verbsData;
	CWVBuffer		buf;

	stat = m_pVerbs->pre_allocate_pd(m_pDevice->m_hVerbsDevice, &verbsData,
									 &m_hVerbsPd);
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
	RtlCopyMemory(pId + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_PD_ALLOCATE,
						  pId, sizeof WV_IO_ID + verbsData.input_size,
						  pId, sizeof WV_IO_ID + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		m_Id = pId->Id;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pId->VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pId + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_allocate_pd(m_pDevice->m_hVerbsDevice, (ib_api_status_t) hr,
							   &m_hVerbsPd, &verbsData);
	return hr;
}

CWVProtectionDomain::~CWVProtectionDomain()
{
	DWORD	bytes;
	HRESULT	hr;

	if (m_Id != 0) {
		m_pVerbs->pre_deallocate_pd(m_hVerbsPd);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_PD_DEALLOCATE, &m_Id, sizeof m_Id,
							   NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_pVerbs->post_deallocate_pd(m_hVerbsPd, (ib_api_status_t) hr);
	}
	m_pDevice->Release();
}

STDMETHODIMP CWVProtectionDomain::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVProtectionDomain) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVProtectionDomain::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVProtectionDomain::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVProtectionDomain::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_PD_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVProtectionDomain::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVProtectionDomain::
CreateSharedReceiveQueue(SIZE_T MaxWr, SIZE_T MaxSge,
						 SIZE_T SrqLimit, IWVSharedReceiveQueue** ppSrq)
{
	return CWVSharedReceiveQueue::CreateInstance(this, MaxWr, MaxSge,
												 SrqLimit, ppSrq);
}

STDMETHODIMP CWVProtectionDomain::
CreateConnectQueuePair(WV_QP_CREATE* pAttributes, IWVConnectQueuePair** ppQp)
{
	return CWVConnectQueuePair::CreateInstance(this, pAttributes, ppQp);
}

STDMETHODIMP CWVProtectionDomain::
CreateDatagramQueuePair(WV_QP_CREATE* pAttributes, IWVDatagramQueuePair** ppQp)
{
	return CWVDatagramQueuePair::CreateInstance(this, pAttributes, ppQp);
}

STDMETHODIMP CWVProtectionDomain::
RegisterMemory(const VOID* pBuffer, SIZE_T BufferLength, DWORD AccessFlags,
			   OVERLAPPED* pOverlapped, WV_MEMORY_KEYS *pKeys)
{
	WV_IO_MEMORY_REGISTER	reg;
	DWORD					bytes;
	HRESULT					hr;

	reg.Id = m_Id;
	reg.Address = (UINT64) (ULONG_PTR) pBuffer;
	reg.BufferLength = BufferLength;
	reg.AccessFlags = AccessFlags;
	reg.Reserved = 0;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_MEMORY_REGISTER, &reg, sizeof reg,
						  pKeys, sizeof WV_MEMORY_KEYS, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVProtectionDomain::
DeregisterMemory(UINT32 Lkey, OVERLAPPED* pOverlapped)
{
	WV_IO_ID	id;
	DWORD		bytes;
	HRESULT		hr;

	id.Id = m_Id;
	id.Data = Lkey;
	hr = WvDeviceIoControl(m_hFile, WV_IOCTL_MEMORY_DEREGISTER, &id, sizeof id,
						   NULL, 0, &bytes, pOverlapped) ?
						   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
	return hr;
}

STDMETHODIMP CWVProtectionDomain::
AllocateMemoryWindow(IWVMemoryWindow** ppMw)
{
	return CWVMemoryWindow::CreateInstance(this, ppMw);
}

STDMETHODIMP CWVProtectionDomain::
CreateAddressHandle(WV_ADDRESS_VECTOR* pAddress, IWVAddressHandle** ppAh, ULONG_PTR *pAhKey)
{
	return CWVAddressHandle::CreateInstance(this, pAddress, ppAh, pAhKey);
}


//-----------------------
// Memory Window routines
//-----------------------

CWVMemoryWindow::CWVMemoryWindow(CWVProtectionDomain *pPd)
{
	pPd->AddRef();
	m_pPd = pPd;
	m_pVerbs = pPd->m_pVerbs;
	m_hFile = pPd->m_hFile;
	m_hVerbsMw = NULL;
}

STDMETHODIMP CWVMemoryWindow::
Allocate(void)
{
	WV_IO_ID		*pId;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	ci_umv_buf_t	verbsData;
	CWVBuffer		buf;

	stat = m_pVerbs->pre_create_mw(m_pPd->m_hVerbsPd, &verbsData, &m_hVerbsMw);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_ID + max(verbsData.input_size, verbsData.output_size);
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pId->Id = m_pPd->m_Id;
	pId->VerbInfo = verbsData.command;
	RtlCopyMemory(pId + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_MW_ALLOCATE,
						  pId, sizeof WV_IO_ID + verbsData.input_size,
						  pId, sizeof WV_IO_ID + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		m_Id = pId->Id;
		m_Rkey = pId->Data;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pId->VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pId + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_create_mw(m_pPd->m_hVerbsPd, (ib_api_status_t) hr,
							 m_Rkey, &m_hVerbsMw, &verbsData);
	return hr;
}

CWVMemoryWindow::~CWVMemoryWindow()
{
	DWORD	bytes;
	HRESULT	hr;

	if (m_Id != 0) {
		m_pVerbs->pre_destroy_mw(m_hVerbsMw);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_MW_DEALLOCATE,
							   &m_Id, sizeof m_Id, NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_pVerbs->post_destroy_mw(m_hVerbsMw, (ib_api_status_t) hr);
	}
	m_pPd->Release();
}

STDMETHODIMP CWVMemoryWindow::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVMemoryWindow) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVMemoryWindow::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVMemoryWindow::
Release(void)
{
	return CWVBase::Release();
}


//------------------------
// Address Handle routines
//------------------------

CWVAddressHandle::CWVAddressHandle(CWVProtectionDomain *pPd)
{
	pPd->AddRef();
	m_pPd = pPd;
	m_pVerbs = pPd->m_pVerbs;
	m_hFile = pPd->m_hFile;
	m_hVerbsAh = NULL;
}

STDMETHODIMP CWVAddressHandle::
Create(WV_ADDRESS_VECTOR* pAddress)
{
	WV_IO_AH_CREATE	*pav;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	ci_umv_buf_t	verbsData;
	ib_av_attr_t	attr;
	CWVBuffer		buf;

	hr = ConvertAv(&attr, pAddress);
	if (FAILED(hr)) {
		return hr;
	}

	stat = m_pVerbs->pre_create_av(m_pPd->m_hVerbsPd, &attr, &verbsData, &m_hVerbsAh);
	if (stat != IB_SUCCESS) {
		if (stat == IB_VERBS_PROCESSING_DONE) {
			m_Id = (ULONG_PTR) m_hVerbsAh;
		}
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_AH_CREATE + max(verbsData.input_size, verbsData.output_size);
	pav = (WV_IO_AH_CREATE *) buf.Get(bytes);
	if (pav == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pav->Id.Id = m_pPd->m_Id;
	pav->Id.VerbInfo = verbsData.command;
	RtlCopyMemory(&pav->AddressVector, pAddress, sizeof(pav->AddressVector));
	RtlCopyMemory(pav + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_AH_CREATE,
						  pav, sizeof WV_IO_AH_CREATE + verbsData.input_size,
						  pav, sizeof WV_IO_AH_CREATE + verbsData.output_size,
						  &bytes, NULL)) {
		hr = WV_SUCCESS;
		m_Id = pav->Id.Id;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	verbsData.status = pav->Id.VerbInfo;
	RtlCopyMemory((void *) (ULONG_PTR) verbsData.p_inout_buf, pav + 1,
				  verbsData.output_size);
	buf.Put();

post:
	m_pVerbs->post_create_av(m_pPd->m_hVerbsPd, (ib_api_status_t) hr,
							 &m_hVerbsAh, &verbsData);
	return hr;
}

CWVAddressHandle::~CWVAddressHandle()
{
	DWORD			bytes;
	HRESULT			hr;
	ib_api_status_t	stat;

	if (m_Id != 0) {
		stat = m_pVerbs->pre_destroy_av(m_hVerbsAh);
		if (stat != IB_VERBS_PROCESSING_DONE) {
			hr = WvDeviceIoControl(m_hFile, WV_IOCTL_AH_DESTROY,
								   &m_Id, sizeof m_Id, NULL, 0, &bytes, NULL) ?
								   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
			m_pVerbs->post_destroy_av(m_hVerbsAh, (ib_api_status_t) hr);
		}
	}
	m_pPd->Release();
}

STDMETHODIMP CWVAddressHandle::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVAddressHandle) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVAddressHandle::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVAddressHandle::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVAddressHandle::
ConvertAv(ib_av_attr_t *pVerbsAv, WV_ADDRESS_VECTOR *pAv)
{
	DWORD index;
	HRESULT hr;

	pVerbsAv->grh_valid = pAv->Route.Valid;
	if (pVerbsAv->grh_valid) {
		hr = m_pPd->m_pDevice->FindGid(pAv->PortNumber, &pAv->Route.SGid, &index);
		if (FAILED(hr)) {
			return hr;
		}

		pVerbsAv->grh.resv1 = (UINT16) index;
		pVerbsAv->grh.ver_class_flow =
			_byteswap_ulong(_byteswap_ulong(pAv->Route.FlowLabel) |
							 (pAv->Route.TrafficClass << 20));
		pVerbsAv->grh.hop_limit = pAv->Route.HopLimit;
		RtlCopyMemory(&pVerbsAv->grh.src_gid, &pAv->Route.SGid, sizeof(pAv->Route.SGid));
		RtlCopyMemory(&pVerbsAv->grh.dest_gid, &pAv->Route.DGid, sizeof(pAv->Route.DGid));
	}

	pVerbsAv->port_num = pAv->PortNumber;
	pVerbsAv->sl = pAv->ServiceLevel;
	pVerbsAv->dlid = pAv->DLid;
	pVerbsAv->static_rate = pAv->StaticRate;
	pVerbsAv->path_bits = pAv->SourcePathBits;

	return WV_SUCCESS;
}