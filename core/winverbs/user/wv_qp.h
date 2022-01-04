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

#pragma once

#ifndef _WV_QP_H_
#define _WV_QP_H_

#include <rdma\winverbs.h>
#include "wv_pd.h"
#include "wv_cq.h"
#include "wv_srq.h"
#include "wv_base.h"

class CWVQueuePair : IWVQueuePair, public CWVBase
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 DWORD *pNumberOfBytesTransferred, BOOL bWait);

	// IWVQueuePair methods
	STDMETHODIMP Query(WV_QP_ATTRIBUTES* pAttributes);
	STDMETHODIMP Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options,
						OVERLAPPED* pOverlapped);
	STDMETHODIMP PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge);
	STDMETHODIMP PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed);

	CWVQueuePair(CWVProtectionDomain *pPd);
	~CWVQueuePair();
	virtual void Delete() {};

	CWVProtectionDomain		*m_pPd;
	CWVCompletionQueue		*m_pSendCq;
	CWVCompletionQueue		*m_pReceiveCq;
	CWVSharedReceiveQueue	*m_pSrq;
	uvp_interface_t			*m_pVerbs;
	ib_qp_handle_t			m_hVerbsQp;
	WV_QP_TYPE				m_Type;

protected:
	STDMETHODIMP Create(WV_QP_CREATE* pAttributes);
	void AddReferences(WV_QP_CREATE* pAttributes);
	void ReleaseReferences();
};

class CWVConnectQueuePair : IWVConnectQueuePair, public CWVQueuePair
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 DWORD *pNumberOfBytesTransferred, BOOL bWait);

	// IWVQueuePair methods
	STDMETHODIMP Query(WV_QP_ATTRIBUTES* pAttributes);
	STDMETHODIMP Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options,
						OVERLAPPED* pOverlapped);
	STDMETHODIMP PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge);
	STDMETHODIMP PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed);

	// IWVConnectQueuePair methods
	STDMETHODIMP Send(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
					  DWORD Flags, NET32 ImmediateData);
	STDMETHODIMP Read(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
					  DWORD Flags, NET64 Address, NET32 Rkey);
	STDMETHODIMP Write(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge,
					   DWORD Flags, NET32 ImmediateData, NET64 Address,
					   NET32 Rkey);
	STDMETHODIMP CompareExchange(UINT64 WrId, WV_SGE* pSge, DWORD Flags,
								 NET64 Compare, NET64 Exchange, NET64 Address,
								 NET32 Rkey);
	STDMETHODIMP FetchAdd(UINT64 WrId, WV_SGE* pSge, DWORD Flags,
						  NET64 Value, NET64 Address, NET32 Rkey);
	STDMETHODIMP BindMemoryWindow(IWVMemoryWindow* pMw, UINT64 WrId,
								  UINT32 Lkey, DWORD AccessFlags, DWORD SendFlags,
								  const VOID* pBuffer, SIZE_T BufferLength,
								  NET32 *pRkey);

	CWVConnectQueuePair(CWVProtectionDomain *pPd) : CWVQueuePair(pPd) {};
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProtectionDomain *pPd, WV_QP_CREATE* pAttributes,
				   IWVConnectQueuePair** ppQp)
	{
		HRESULT hr;
		CWVConnectQueuePair *qp;

		qp = new CWVConnectQueuePair(pPd);
		if (qp == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = qp->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = qp->Create(pAttributes);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppQp = qp;
		return WV_SUCCESS;

	err2:
		qp->Release();
	err1:
		*ppQp = NULL;
		return hr;
	}
};

class CWVDatagramQueuePair : IWVDatagramQueuePair, public CWVQueuePair
{
public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, LPVOID FAR* ppvObj);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	// IWVOverlapped methods
	STDMETHODIMP CancelOverlappedRequests();
	STDMETHODIMP GetOverlappedResult(OVERLAPPED *pOverlapped,
									 DWORD *pNumberOfBytesTransferred, BOOL bWait);

	// IWVQueuePair methods
	STDMETHODIMP Query(WV_QP_ATTRIBUTES* pAttributes);
	STDMETHODIMP Modify(WV_QP_ATTRIBUTES* pAttributes, DWORD Options,
						OVERLAPPED* pOverlapped);
	STDMETHODIMP PostReceive(UINT64 WrId, WV_SGE* pSgl, SIZE_T nSge);
	STDMETHODIMP PostSend(WV_SEND_REQUEST *pSend, WV_SEND_REQUEST **ppFailed);

	// IWVDatagramQueuePair Methods
	STDMETHODIMP Send(UINT64 WrId, ULONG_PTR AhKey,
					  WV_SGE* pSge, DWORD Flags, NET32 DestinationQpn,
					  NET32 DestinationQkey);
	STDMETHODIMP SendMessage(WV_SEND_DATAGRAM* pSend);
	STDMETHODIMP AttachMulticast(WV_GID *pGid, NET16 Lid,
								 OVERLAPPED* pOverlapped);
	STDMETHODIMP DetachMulticast(WV_GID *pGid, NET16 Lid,
								 OVERLAPPED* pOverlapped);

	CWVDatagramQueuePair(CWVProtectionDomain *pPd) : CWVQueuePair(pPd) {};
	void Delete() {delete this;}
	static STDMETHODIMP
	CreateInstance(CWVProtectionDomain *pPd, WV_QP_CREATE* pAttributes,
				   IWVDatagramQueuePair** ppQp)
	{
		HRESULT hr;
		CWVDatagramQueuePair *qp;

		qp = new CWVDatagramQueuePair(pPd);
		if (qp == NULL) {
			hr = WV_NO_MEMORY;
			goto err1;
		}

		hr = qp->Init();
		if (FAILED(hr)) {
			goto err2;
		}

		hr = qp->Create(pAttributes);
		if (FAILED(hr)) {
			goto err2;
		}

		*ppQp = qp;
		return WV_SUCCESS;

	err2:
		qp->Release();
	err1:
		*ppQp = NULL;
		return hr;
	}
};

#endif // _WV_QP_H_