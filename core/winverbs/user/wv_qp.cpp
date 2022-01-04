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
#include "wv_cq.h"
#include "wv_qp.h"
#include "wv_srq.h"
#include "wv_ioctl.h"

static void WvVerbsConvertQpCreate(uvp_qp_create_t *pVerbsAttr, WV_QP_CREATE *pAttr)
{
	pVerbsAttr->qp_create.qp_type = (ib_qp_type_t) pAttr->QpType;
	pVerbsAttr->qp_create.sq_depth = (UINT32) pAttr->SendDepth;
	pVerbsAttr->qp_create.rq_depth = (UINT32) pAttr->ReceiveDepth;
	pVerbsAttr->qp_create.sq_sge = (UINT32) pAttr->SendSge;
	pVerbsAttr->qp_create.rq_sge = (UINT32) pAttr->ReceiveSge;
	pVerbsAttr->qp_create.sq_max_inline = (UINT32) pAttr->MaxInlineSend;

	pVerbsAttr->qp_create.h_sq_cq = ((CWVCompletionQueue *) pAttr->pSendCq)->m_hVerbsCq;
	pVerbsAttr->qp_create.h_rq_cq = ((CWVCompletionQueue *) pAttr->pReceiveCq)->m_hVerbsCq;
	if (pAttr->pSharedReceiveQueue != NULL) {
		pVerbsAttr->qp_create.h_srq = ((CWVSharedReceiveQueue *)
										pAttr->pSharedReceiveQueue)->m_hVerbsSrq;
	} else {
		pVerbsAttr->qp_create.h_srq = NULL;
	}

	pVerbsAttr->qp_create.sq_signaled = ((pAttr->QpFlags & WV_QP_SIGNAL_SENDS) != 0);

	pVerbsAttr->context = pAttr->Context;
	pVerbsAttr->max_inline_send = (UINT32) pAttr->MaxInlineSend;
	pVerbsAttr->initiator_depth = (UINT32) pAttr->InitiatorDepth;
	pVerbsAttr->responder_resources = (UINT32) pAttr->ResponderResources;
}

static void WvIoConvertQpCreate(WV_IO_QP_CREATE *pIoAttr, WV_QP_CREATE *pAttr)
{
	pIoAttr->SendCqId = ((CWVCompletionQueue *) pAttr->pSendCq)->m_Id;
	pIoAttr->ReceiveCqId = ((CWVCompletionQueue *) pAttr->pReceiveCq)->m_Id;
	if (pAttr->pSharedReceiveQueue != NULL) {
		pIoAttr->SrqId = ((CWVSharedReceiveQueue *) pAttr->pSharedReceiveQueue)->m_Id;
	} else {
		pIoAttr->SrqId = 0;
	}

	pIoAttr->SendDepth = (UINT32) pAttr->SendDepth;
	pIoAttr->SendSge = (UINT32) pAttr->SendSge;
	pIoAttr->ReceiveDepth = (UINT32) pAttr->ReceiveDepth;
	pIoAttr->ReceiveSge = (UINT32) pAttr->ReceiveSge;
	pIoAttr->MaxInlineSend = (UINT32) pAttr->MaxInlineSend;
	pIoAttr->InitiatorDepth = (UINT32) pAttr->InitiatorDepth;
	pIoAttr->ResponderResources = (UINT32) pAttr->ResponderResources;

	pIoAttr->QpType = (UINT8) pAttr->QpType;
	pIoAttr->QpFlags = (UINT8) pAttr->QpFlags;
	pIoAttr->Reserved = 0;
}

static void WvQpAttrConvertIo(WV_QP_ATTRIBUTES *pAttr,
							  WV_IO_QP_ATTRIBUTES *pIoAttr,
							  CWVProtectionDomain *pPd,
							  CWVCompletionQueue *pSendCq,
							  CWVCompletionQueue *pReceiveCq,
							  CWVSharedReceiveQueue *pSharedReceiveQueue)
{
	if (pPd != NULL) {
		pPd->QueryInterface(IID_IWVProtectionDomain, (LPVOID *) &pAttr->pPd);
		pAttr->pPd->Release();
	} else {
		pAttr->pPd = NULL;
	}
	if (pSendCq != NULL) {
		pSendCq->QueryInterface(IID_IWVCompletionQueue,
								(LPVOID *) &pAttr->pSendCq);
		pAttr->pSendCq->Release();
	} else {
		pAttr->pSendCq = NULL;
	}
	if (pReceiveCq != NULL) {
		pReceiveCq->QueryInterface(IID_IWVCompletionQueue,
								   (LPVOID *) &pAttr->pReceiveCq);
		pAttr->pReceiveCq->Release();
	} else {
		pAttr->pReceiveCq = NULL;
	}
	if (pSharedReceiveQueue != NULL) {
		pSharedReceiveQueue->QueryInterface(IID_IWVSharedReceiveQueue,
											(LPVOID *) &pAttr->pSharedReceiveQueue);
		pAttr->pSharedReceiveQueue->Release();
	} else {
		pAttr->pSharedReceiveQueue = NULL;
	}

	pAttr->SendDepth = pIoAttr->SendDepth;
	pAttr->SendSge = pIoAttr->SendSge;
	pAttr->ReceiveDepth = pIoAttr->ReceiveDepth;
	pAttr->ReceiveSge = pIoAttr->ReceiveSge;
	pAttr->MaxInlineSend = pIoAttr->MaxInlineSend;
	pAttr->InitiatorDepth = pIoAttr->InitiatorDepth;
	pAttr->ResponderResources = pIoAttr->ResponderResources;

	pAttr->QpType = (WV_QP_TYPE) pIoAttr->QpType;
	pAttr->CurrentQpState = (WV_QP_STATE) pIoAttr->CurrentQpState;
	pAttr->QpState = (WV_QP_STATE) pIoAttr->QpState;
	pAttr->ApmState = (WV_APM_STATE) pIoAttr->ApmState;
	pAttr->Qpn = pIoAttr->Qpn;
	pAttr->DestinationQpn = pIoAttr->DestinationQpn;
	pAttr->Qkey = pIoAttr->Qkey;
	pAttr->SendPsn = pIoAttr->SendPsn;
	pAttr->ReceivePsn = pIoAttr->ReceivePsn;

	pAttr->QpFlags = pIoAttr->QpFlags;
	pAttr->AccessFlags = pIoAttr->AccessFlags;

	RtlCopyMemory(&pAttr->AddressVector, &pIoAttr->AddressVector,
				  sizeof(pAttr->AddressVector));
	RtlCopyMemory(&pAttr->AlternateAddressVector, &pIoAttr->AlternateAddressVector,
				  sizeof(pAttr->AlternateAddressVector));
	pAttr->PathMtu = pIoAttr->PathMtu;
	pAttr->AlternatePathMtu = pIoAttr->AlternatePathMtu;
	pAttr->PkeyIndex = pIoAttr->PkeyIndex;
	pAttr->AlternatePkeyIndex = pIoAttr->AlternatePkeyIndex;
	pAttr->LocalAckTimeout = pIoAttr->LocalAckTimeout;
	pAttr->AlternateLocalAckTimeout = pIoAttr->AlternateLocalAckTimeout;

	pAttr->RnrNakTimeout = pIoAttr->RnrNakTimeout;
	pAttr->SequenceErrorRetryCount = pIoAttr->SequenceErrorRetryCount;
	pAttr->RnrRetryCount = pIoAttr->RnrRetryCount;
}

static void WvIoConvertQpAttr(WV_IO_QP_ATTRIBUTES *pIoAttr,
							  WV_QP_ATTRIBUTES *pAttr, DWORD Options)
{
	pIoAttr->SendDepth = (UINT32) pAttr->SendDepth;
	pIoAttr->SendSge = (UINT32) pAttr->SendSge;
	pIoAttr->ReceiveDepth = (UINT32) pAttr->ReceiveDepth;
	pIoAttr->ReceiveSge = (UINT32) pAttr->ReceiveSge;
	pIoAttr->MaxInlineSend = (UINT32) pAttr->MaxInlineSend;
	pIoAttr->InitiatorDepth = (UINT32) pAttr->InitiatorDepth;
	pIoAttr->ResponderResources = (UINT32) pAttr->ResponderResources;

	pIoAttr->Options = Options;
	pIoAttr->QpType = (UINT8) pAttr->QpType;
	pIoAttr->CurrentQpState = (UINT8) pAttr->CurrentQpState;
	pIoAttr->QpState = (UINT8) pAttr->QpState;
	pIoAttr->ApmState = (UINT8) pAttr->ApmState;
	pIoAttr->Qpn = pAttr->Qpn;
	pIoAttr->DestinationQpn = pAttr->DestinationQpn;
	pIoAttr->Qkey = pAttr->Qkey;
	pIoAttr->SendPsn = pAttr->SendPsn;
	pIoAttr->ReceivePsn = pAttr->ReceivePsn;

	RtlCopyMemory(&pIoAttr->AddressVector, &pAttr->AddressVector,
				  sizeof(pIoAttr->AddressVector));
	RtlCopyMemory(&pIoAttr->AlternateAddressVector, &pAttr->AlternateAddressVector,
				  sizeof(pIoAttr->AlternateAddressVector));
	pIoAttr->PathMtu = pAttr->PathMtu;
	pIoAttr->AlternatePathMtu = pAttr->AlternatePathMtu;
	pIoAttr->PkeyIndex = pAttr->PkeyIndex;
	pIoAttr->AlternatePkeyIndex = pAttr->AlternatePkeyIndex;
	pIoAttr->LocalAckTimeout = pAttr->LocalAckTimeout;
	pIoAttr->AlternateLocalAckTimeout = pAttr->AlternateLocalAckTimeout;

	pIoAttr->RnrNakTimeout = pAttr->RnrNakTimeout;
	pIoAttr->SequenceErrorRetryCount = pAttr->SequenceErrorRetryCount;
	pIoAttr->RnrRetryCount = pAttr->RnrRetryCount;

	pIoAttr->AccessFlags = (UINT8) pAttr->AccessFlags;
	pIoAttr->QpFlags = (UINT8) pAttr->QpFlags;
	RtlZeroMemory(pIoAttr->Reserved, sizeof pIoAttr->Reserved);
}

CWVQueuePair::CWVQueuePair(CWVProtectionDomain *pPd)
{
	pPd->AddRef();
	m_pPd = pPd;
	m_pSendCq = NULL;
	m_pReceiveCq = NULL;
	m_pSrq = NULL;
	m_pVerbs = pPd->m_pVerbs;
	m_hFile = pPd->m_hFile;
	m_hVerbsQp = NULL;
}

void CWVQueuePair::
AddReferences(WV_QP_CREATE* pAttributes)
{
	if (pAttributes->pSendCq != NULL) {
		pAttributes->pSendCq->AddRef();
		m_pSendCq = (CWVCompletionQueue *) pAttributes->pSendCq;
	}
	if (pAttributes->pReceiveCq != NULL) {
		pAttributes->pReceiveCq->AddRef();
		m_pReceiveCq = (CWVCompletionQueue *) pAttributes->pReceiveCq;
	}	
	if (pAttributes->pSharedReceiveQueue != NULL) {
		pAttributes->pSharedReceiveQueue->AddRef();
		m_pSrq = (CWVSharedReceiveQueue *) pAttributes->pSharedReceiveQueue;
	}
}

void CWVQueuePair::
ReleaseReferences(void)
{
	if (m_pSendCq != NULL) {
		m_pSendCq->Release();
	}
	if (m_pReceiveCq != NULL) {
		m_pReceiveCq->Release();
	}
	if (m_pSrq != NULL) {
		m_pSrq->Release();
	}
}

STDMETHODIMP CWVQueuePair::
Create(WV_QP_CREATE* pAttributes)
{
	WV_IO_QP_CREATE	*pattr;
	DWORD			bytes;
	ib_api_status_t	stat;
	HRESULT			hr;
	uvp_qp_create_t	attr;
	ci_umv_buf_t	verbsData;
	CWVBuffer		buf;

	m_Type = pAttributes->QpType;
	AddReferences(pAttributes);
	WvVerbsConvertQpCreate(&attr, pAttributes);
	stat = m_pVerbs->wv_pre_create_qp(m_pPd->m_hVerbsPd, &attr, &verbsData,
									  &m_hVerbsQp);
	if (stat != IB_SUCCESS) {
		return WvConvertIbStatus(stat);
	}

	bytes = sizeof WV_IO_QP_CREATE + max(verbsData.input_size, verbsData.output_size);
	pattr = (WV_IO_QP_CREATE *) buf.Get(bytes);
	if (pattr == NULL) {
		hr = WV_NO_MEMORY;
		goto post;
	}

	pattr->Id.Id = m_pPd->m_Id;
	pattr->Id.VerbInfo = verbsData.command;
	WvIoConvertQpCreate(pattr, pAttributes);
	RtlCopyMemory(pattr + 1, (void *) (ULONG_PTR) verbsData.p_inout_buf,
				  verbsData.input_size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_QP_CREATE,
						  pattr, sizeof WV_IO_QP_CREATE + verbsData.input_size,
						  pattr, sizeof WV_IO_QP_CREATE + verbsData.output_size,
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
	m_pVerbs->post_create_qp(m_pPd->m_hVerbsPd, (ib_api_status_t) hr,
							 &m_hVerbsQp, &verbsData);
	return hr;
}

CWVQueuePair::~CWVQueuePair()
{
	DWORD	bytes;
	HRESULT	hr;

	if (m_Id != 0) {
		m_pVerbs->pre_destroy_qp(m_hVerbsQp);
		hr = WvDeviceIoControl(m_hFile, WV_IOCTL_QP_DESTROY, &m_Id, sizeof m_Id,
							   NULL, 0, &bytes, NULL) ?
							   WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
		m_pVerbs->post_destroy_qp(m_hVerbsQp, (ib_api_status_t) hr);
	}
	ReleaseReferences();
	m_pPd->Release();
}

STDMETHODIMP CWVQueuePair::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVQueuePair) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVQueuePair::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVQueuePair::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVQueuePair::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_QP_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVQueuePair::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVQueuePair::
Query(WV_QP_ATTRIBUTES* pAttributes)
{
	WV_IO_QP_ATTRIBUTES	attr;
	DWORD				bytes;
	HRESULT				hr;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_QP_QUERY, &m_Id, sizeof m_Id,
						  &attr, sizeof WV_IO_QP_ATTRIBUTES, &bytes, NULL)) {
		hr = WV_SUCCESS;
		WvQpAttrConvertIo(pAttributes, &attr, m_pPd, m_pSendCq,
						  m_pReceiveCq, m_pSrq);
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVQueuePair::
Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options, OVERLAPPED* pOverlapped)
{
	WV_IO_QP_ATTRIBUTES	attr;
	DWORD				bytes;
	HRESULT				hr;
	void				*pout;
	DWORD				size;

	m_pVerbs->nd_modify_qp(m_hVerbsQp, &pout, &size);

	attr.Id.Id = m_Id;
	WvIoConvertQpAttr(&attr, pAttributes, Options);
	if (WvDeviceIoControl(m_hFile, WV_IOCTL_QP_MODIFY, &attr, sizeof attr,
						  pout, size, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVQueuePair::
PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge)
{
	ib_recv_wr_t	wr, *pwr;
	ib_api_status_t	stat;
	HRESULT			hr;

	wr.p_next = NULL;
	wr.wr_id = WrId;
	wr.num_ds = (UINT32) nSge;
	wr.ds_array = WvConvertSgl(pSgl, nSge);

	stat = m_pVerbs->post_recv(m_hVerbsQp, &wr, &pwr);
	if (stat == IB_SUCCESS) {
		hr = WV_SUCCESS;
	} else {
		hr = WvConvertIbStatus(stat);
	}

	return hr;
}

STDMETHODIMP CWVQueuePair::
PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed)
{
	WV_SEND_REQUEST		*wr;
	ib_api_status_t		stat;
	HRESULT				hr;

	for (wr = pSend; wr != NULL; wr = wr->pNext) {
		if (wr->Opcode != WvSend) {
			wr->Wr.Rdma.RemoteAddress = _byteswap_uint64(wr->Wr.Rdma.RemoteAddress);
		}
		WvConvertSgl(wr->pSgl, wr->nSge);
	}

	stat = m_pVerbs->post_send(m_hVerbsQp, (ib_send_wr_t *) pSend,
							   (ib_send_wr_t **) (ppFailed ? ppFailed : &wr));
	if (stat == IB_SUCCESS) {
		hr = WV_SUCCESS;
	} else {
		hr = WvConvertIbStatus(stat);
	}

	for (wr = pSend; wr != NULL; wr = wr->pNext) {
		if (wr->Opcode != WvSend) {
			wr->Wr.Rdma.RemoteAddress = _byteswap_uint64(wr->Wr.Rdma.RemoteAddress);
		}
	}

	return hr;
}


//-----------------------------
// CWVConnectQueuePair routines
//-----------------------------

STDMETHODIMP CWVConnectQueuePair::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IWVConnectQueuePair) {
		return CWVQueuePair::QueryInterface(riid, ppvObj);
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVConnectQueuePair::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVConnectQueuePair::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVConnectQueuePair::
CancelOverlappedRequests(void)
{
	return CWVQueuePair::CancelOverlappedRequests();
}

STDMETHODIMP CWVConnectQueuePair::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVConnectQueuePair::
Query(WV_QP_ATTRIBUTES* pAttributes)
{
	return CWVQueuePair::Query(pAttributes);
}

STDMETHODIMP CWVConnectQueuePair::
Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options, OVERLAPPED* pOverlapped)
{
	return CWVQueuePair::Modify(pAttributes, Options, pOverlapped);
}

STDMETHODIMP CWVConnectQueuePair::
PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge)
{
	return CWVQueuePair::PostReceive(WrId, pSgl, nSge);
}

STDMETHODIMP CWVConnectQueuePair::
PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed)
{
	return CWVQueuePair::PostSend(pSend, ppFailed);
}

STDMETHODIMP CWVConnectQueuePair::
Send(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
	 DWORD Flags, NET32 ImmediateData)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvSend;
	wr.Flags = Flags;
	wr.nSge = (UINT32) nSge;
	wr.pSgl = pSgl;
	wr.ImmediateData =  ImmediateData;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVConnectQueuePair::
Read(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
	 DWORD Flags, NET64 Address, NET32 Rkey)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvRdmaRead;
	wr.Flags = Flags;
	wr.nSge = (UINT32) nSge;
	wr.pSgl = pSgl;

	wr.Wr.Rdma.RemoteAddress = Address;
	wr.Wr.Rdma.Rkey = Rkey;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVConnectQueuePair::
Write(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
	  DWORD Flags, NET32 ImmediateData, NET64 Address, NET32 Rkey)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvRdmaWrite;
	wr.Flags = Flags;
	wr.nSge = (UINT32) nSge;
	wr.pSgl = pSgl;
	wr.ImmediateData = ImmediateData;

	wr.Wr.Rdma.RemoteAddress = Address;
	wr.Wr.Rdma.Rkey = Rkey;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVConnectQueuePair::
CompareExchange(UINT64 WrId, WV_SGE* pSge, DWORD Flags,
				NET64 Compare, NET64 Exchange, NET64 Address, NET32 Rkey)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvCompareExchange;
	wr.Flags = Flags;
	wr.nSge = 1;
	wr.pSgl = pSge;

	wr.Wr.CompareExchange.RemoteAddress = Address;
	wr.Wr.CompareExchange.Rkey = Rkey;
	wr.Wr.CompareExchange.Compare = Compare;
	wr.Wr.CompareExchange.Exchange = Exchange;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVConnectQueuePair::
FetchAdd(UINT64 WrId, WV_SGE* pSge, DWORD Flags,
		 NET64 Value, NET64 Address, NET32 Rkey)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvFetchAdd;
	wr.Flags = Flags;
	wr.nSge = 1;
	wr.pSgl = pSge;

	wr.Wr.FetchAdd.RemoteAddress = Address;
	wr.Wr.FetchAdd.Rkey = Rkey;
	wr.Wr.FetchAdd.Add = Value;
	wr.Wr.FetchAdd.Reserved = 0;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVConnectQueuePair::
BindMemoryWindow(IWVMemoryWindow* pMw, UINT64 WrId,
				 UINT32 Lkey, DWORD AccessFlags, DWORD SendFlags,
				 const VOID* pBuffer, SIZE_T BufferLength, NET32 *pRkey)
{
	UNREFERENCED_PARAMETER(pMw);
	UNREFERENCED_PARAMETER(WrId);
	UNREFERENCED_PARAMETER(Lkey);
	UNREFERENCED_PARAMETER(AccessFlags);
	UNREFERENCED_PARAMETER(SendFlags);
	UNREFERENCED_PARAMETER(pBuffer);
	UNREFERENCED_PARAMETER(BufferLength);
	UNREFERENCED_PARAMETER(pRkey);

	return E_NOTIMPL;
}


//---------------------------
// DatagramQueuePair routines
//---------------------------

STDMETHODIMP CWVDatagramQueuePair::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IWVDatagramQueuePair) {
		return CWVQueuePair::QueryInterface(riid, ppvObj);
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVDatagramQueuePair::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVDatagramQueuePair::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVDatagramQueuePair::
CancelOverlappedRequests(void)
{
	return CWVQueuePair::CancelOverlappedRequests();
}

STDMETHODIMP CWVDatagramQueuePair::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVDatagramQueuePair::
Query(WV_QP_ATTRIBUTES* pAttributes)
{
	return CWVQueuePair::Query(pAttributes);
}

STDMETHODIMP CWVDatagramQueuePair::
Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options, OVERLAPPED* pOverlapped)
{
	return CWVQueuePair::Modify(pAttributes, Options, pOverlapped);
}

STDMETHODIMP CWVDatagramQueuePair::
PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge)
{
	return CWVQueuePair::PostReceive(WrId, pSgl, nSge);
}

STDMETHODIMP CWVDatagramQueuePair::
PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed)
{
	return CWVQueuePair::PostSend(pSend, ppFailed);
}

STDMETHODIMP CWVDatagramQueuePair::
Send(UINT64 WrId, ULONG_PTR AhKey,
	 WV_SGE* pSge, DWORD Flags, NET32 DestinationQpn,
	 NET32 DestinationQkey)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = WrId;
	wr.Opcode = WvSend;
	wr.Flags = Flags;
	wr.nSge = 1;
	wr.pSgl = pSge;

	wr.Wr.Datagram.DestinationQkey = DestinationQkey;
	wr.Wr.Datagram.DestinationQpn = DestinationQpn;
	wr.Wr.Datagram.AhKey = AhKey;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVDatagramQueuePair::
SendMessage(WV_SEND_DATAGRAM* pSend)
{
	WV_SEND_REQUEST	wr;
	HRESULT			hr;

	wr.pNext = NULL;
	wr.WrId = pSend->WrId;
	wr.Opcode = WvSend;
	wr.Flags = pSend->Flags;
	wr.nSge = (UINT32) pSend->nSge;
	wr.pSgl = pSend->pSgl;
	wr.ImmediateData = pSend->ImmediateData;

	wr.Wr.Datagram.DestinationQkey = pSend->DestinationQkey;
	wr.Wr.Datagram.DestinationQpn = pSend->DestinationQpn;
	wr.Wr.Datagram.AhKey = pSend->AhKey;

	hr = PostSend(&wr, NULL);
	return hr;
}

STDMETHODIMP CWVDatagramQueuePair::
AttachMulticast(WV_GID *pGid, NET16 Lid, OVERLAPPED* pOverlapped)
{
	WV_IO_QP_MULTICAST	mc;
	DWORD				bytes;
	HRESULT				hr;

	mc.Id.Id = m_Id;
	mc.Id.Data = (UINT32) Lid;
	RtlCopyMemory(mc.Gid.Raw, pGid->Raw, sizeof mc.Gid.Raw);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_QP_ATTACH, &mc, sizeof mc,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVDatagramQueuePair::
DetachMulticast(WV_GID *pGid, NET16 Lid, OVERLAPPED* pOverlapped)
{
	WV_IO_QP_MULTICAST	mc;
	DWORD				bytes;
	HRESULT				hr;

	mc.Id.Id = m_Id;
	mc.Id.Data = (UINT32) Lid;
	RtlCopyMemory(mc.Gid.Raw, pGid->Raw, sizeof mc.Gid.Raw);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_QP_DETACH, &mc, sizeof mc,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}
