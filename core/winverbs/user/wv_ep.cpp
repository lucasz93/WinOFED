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

#include "wv_memory.h"
#include "wv_ep.h"
#include "wv_ioctl.h"
#include "wv_qp.h"

static void WvIoConvertConnParam(WV_IO_CONNECT_PARAM *pIoParam,
								 WV_CONNECT_PARAM *pParam)
{
	pIoParam->DataLength = (UINT8) pParam->DataLength;
	pIoParam->ResponderResources = pParam->ResponderResources;
	pIoParam->InitiatorDepth = pParam->InitiatorDepth;
	pIoParam->RetryCount = pParam->RetryCount;
	pIoParam->RnrRetryCount = pParam->RnrRetryCount;
	RtlZeroMemory(pIoParam->Reserved, sizeof pIoParam->Reserved);
	RtlCopyMemory(pIoParam->Data, pParam->Data, pParam->DataLength);
}

CWVConnectEndpoint::CWVConnectEndpoint(CWVProvider *pProvider)
{
	pProvider->AddRef();
	m_pProvider = pProvider;
	m_hFile = pProvider->m_hFile;
	m_Socket = INVALID_SOCKET;
	m_pQp = NULL;
}

STDMETHODIMP CWVConnectEndpoint::
Allocate(void)
{
	UINT64			EpType;
	DWORD			bytes;
	HRESULT			hr;

	EpType = WV_IO_EP_TYPE_CONNECT;
	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_CREATE, &EpType, sizeof EpType,
						  &m_Id, sizeof m_Id, &bytes, NULL)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

CWVConnectEndpoint::~CWVConnectEndpoint()
{
	DWORD	bytes;

	if (m_pQp != NULL) {
		m_pQp->Release();
	}

	if (m_Id != 0) {
		WvDeviceIoControl(m_hFile, WV_IOCTL_EP_DESTROY, &m_Id, sizeof m_Id,
						  NULL, 0, &bytes, NULL);
	}

	if (m_Socket != INVALID_SOCKET) {
		closesocket(m_Socket);
	}
	m_pProvider->Release();
}

STDMETHODIMP CWVConnectEndpoint::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVEndpoint &&
		riid != IID_IWVConnectEndpoint) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVConnectEndpoint::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVConnectEndpoint::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVConnectEndpoint::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_EP_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVConnectEndpoint::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVConnectEndpoint::
Modify(DWORD Option, const VOID* pOptionData, SIZE_T OptionLength)
{
	WV_IO_ID			*pId;
	DWORD				bytes;
	HRESULT				hr;
	CWVBuffer			buf;

	bytes = sizeof WV_IO_ID + OptionLength;
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		return WV_NO_MEMORY;
	}

	pId->Id = m_Id;
	pId->Data = Option;
	RtlCopyMemory(pId + 1, pOptionData, OptionLength);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_MODIFY, pId, bytes,
						  NULL, 0, &bytes, NULL)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	buf.Put();
	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
BindAddress(SOCKADDR* pAddress)
{
	WV_IO_EP_BIND		attr;
	BOOLEAN				any;
	DWORD				bytes;
	int					len;
	HRESULT				hr;

	if (pAddress->sa_family == AF_INET) {
		any = (((SOCKADDR_IN *) pAddress)->sin_addr.S_un.S_addr == INADDR_ANY);
		bytes = sizeof(SOCKADDR_IN);
	} else {
		any = IN6ADDR_ISANY((SOCKADDR_IN6 *) pAddress);
		bytes = sizeof(SOCKADDR_IN6);
	}

	if (any) {
		RtlZeroMemory(&attr.Device, sizeof attr.Device);
	} else {
		hr = m_pProvider->TranslateAddress(pAddress, (WV_DEVICE_ADDRESS *) &attr.Device);
		if (FAILED(hr)) {
			return hr;
		}
	}

	m_Socket = socket(pAddress->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (m_Socket == INVALID_SOCKET) {
		return WvConvertWSAStatus(WSAGetLastError());
	}

	hr = bind(m_Socket, pAddress, bytes);
	if (FAILED(hr)) {
		goto get_err;
	}

	attr.Id = m_Id;
	len = sizeof attr.Address;
	hr = getsockname(m_Socket, (sockaddr *) &attr.Address, &len);
	if (FAILED(hr)) {
		goto get_err;
	}

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_EP_BIND, &attr, sizeof attr,
						   &attr, sizeof attr, &bytes, NULL)) {
		hr = HRESULT_FROM_WIN32(GetLastError());
		goto err;
	}

	return WV_SUCCESS;

get_err:
	hr = WvConvertWSAStatus(WSAGetLastError());
err:
	closesocket(m_Socket);
	m_Socket = INVALID_SOCKET;
	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Listen(SIZE_T Backlog)
{
	WV_IO_EP_LISTEN		attr;
	DWORD				bytes;
	HRESULT				hr;

	attr.Id = m_Id;
	attr.Backlog = Backlog;

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_EP_LISTEN, &attr, sizeof attr,
						   &attr, sizeof attr, &bytes, NULL)) {
		hr = HRESULT_FROM_WIN32(GetLastError());
	} else {
		hr = WV_SUCCESS;
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Reject(const VOID* pUserData, SIZE_T UserDataLength)
{
	WV_IO_ID			*pId;
	DWORD				bytes;
	HRESULT				hr;
	CWVBuffer			buf;

	bytes = sizeof WV_IO_ID + UserDataLength;
	pId = (WV_IO_ID *) buf.Get(bytes);
	if (pId == NULL) {
		return WV_NO_MEMORY;
	}

	pId->Id = m_Id;
	RtlCopyMemory(pId + 1, pUserData, UserDataLength);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_REJECT, pId, bytes,
						  NULL, 0, &bytes, NULL)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	buf.Put();
	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
GetRequest(IWVConnectEndpoint* pEndpoint, OVERLAPPED* pOverlapped)
{
	WV_IO_EP_GET_REQUEST	req;
	DWORD					bytes;
	HRESULT					hr;

	req.Id = m_Id;
	req.EpId = ((CWVConnectEndpoint *) pEndpoint)->m_Id;
	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_GET_REQUEST,
						  &req, sizeof req, NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Connect(IWVConnectQueuePair* pQp, const SOCKADDR* pAddress,
		WV_CONNECT_PARAM* pParam, OVERLAPPED* pOverlapped)
{
	WV_IO_EP_CONNECT	attr;
	DWORD				bytes;
	HRESULT				hr;

	m_pQp = (CWVConnectQueuePair *) pQp;
	m_pQp->AddRef();
	attr.Id = m_Id;
	attr.QpId = m_pQp->m_Id;

	if (pAddress->sa_family == AF_INET) {
		RtlCopyMemory(&attr.PeerAddress, pAddress, sizeof(SOCKADDR_IN));
	} else {
		RtlCopyMemory(&attr.PeerAddress, pAddress, sizeof(SOCKADDR_IN6));
	}
	WvIoConvertConnParam(&attr.Param, pParam);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_CONNECT, &attr, sizeof attr,
						  NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Accept(IWVConnectQueuePair* pQp, WV_CONNECT_PARAM* pParam, OVERLAPPED* pOverlapped)
{
	WV_IO_EP_ACCEPT		attr;
	DWORD				bytes;
	HRESULT				hr;
	void				*pout;
	DWORD				size;

	if (m_pQp == NULL) {
		m_pQp = (CWVConnectQueuePair *) pQp;
		m_pQp->AddRef();
	}

	attr.Id = m_Id;
	attr.QpId = m_pQp->m_Id;
	m_pQp->m_pVerbs->nd_modify_qp(m_pQp->m_hVerbsQp, &pout, &size);
	WvIoConvertConnParam(&attr.Param, pParam);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_ACCEPT, &attr, sizeof attr,
						  pout, size, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Disconnect(OVERLAPPED* pOverlapped)
{
	WV_IO_EP_DISCONNECT	attr;
	DWORD				bytes;
	HRESULT				hr;
	void				*pout;
	DWORD				size;

	attr.Id = m_Id;
	attr.QpId = m_pQp->m_Id;
	m_pQp->m_pVerbs->nd_modify_qp(m_pQp->m_hVerbsQp, &pout, &size);

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_DISCONNECT,
						  &attr, sizeof attr, pout, size, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
NotifyDisconnect(OVERLAPPED* pOverlapped)
{
	DWORD		bytes;
	HRESULT		hr;

	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_DISCONNECT_NOTIFY,
						  &m_Id, sizeof m_Id, NULL, 0, &bytes, pOverlapped)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

STDMETHODIMP CWVConnectEndpoint::
Query(WV_CONNECT_ATTRIBUTES* pAttributes)
{
	WV_IO_EP_ATTRIBUTES	attr;
	DWORD				bytes;
	HRESULT				hr;

	if (!WvDeviceIoControl(m_hFile, WV_IOCTL_EP_QUERY, &m_Id, sizeof m_Id,
						   &attr, sizeof attr, &bytes, NULL)) {
		return HRESULT_FROM_WIN32(GetLastError());
	}

	RtlCopyMemory(&pAttributes->LocalAddress, &attr.LocalAddress,
				  sizeof pAttributes->LocalAddress);
	RtlCopyMemory(&pAttributes->PeerAddress, &attr.PeerAddress,
				  sizeof pAttributes->PeerAddress);
	RtlCopyMemory(&pAttributes->Device, &attr.Device, sizeof pAttributes->Device);
	pAttributes->Param.DataLength = attr.Param.Connect.DataLength;
	pAttributes->Param.ResponderResources = (SIZE_T) attr.Param.Connect.ResponderResources;
	pAttributes->Param.InitiatorDepth = (SIZE_T) attr.Param.Connect.InitiatorDepth;
	pAttributes->Param.RetryCount = attr.Param.Connect.RetryCount;
	pAttributes->Param.RnrRetryCount = attr.Param.Connect.RnrRetryCount;
	RtlCopyMemory(pAttributes->Param.Data, attr.Param.Connect.Data, attr.Param.Connect.DataLength);

	return WV_SUCCESS;
}


CWVDatagramEndpoint::CWVDatagramEndpoint(CWVProvider *pProvider)
{
	pProvider->AddRef();
	m_pProvider = pProvider;
	m_hFile = pProvider->m_hFile;
}
	
STDMETHODIMP CWVDatagramEndpoint::
Allocate(void)
{
	UINT64			EpType;
	DWORD			bytes;
	HRESULT			hr;

	EpType = WV_IO_EP_TYPE_DATAGRAM;
	if (WvDeviceIoControl(m_hFile, WV_IOCTL_EP_CREATE, &EpType, sizeof EpType,
						  &m_Id, sizeof m_Id, &bytes, NULL)) {
		hr = WV_SUCCESS;
	} else {
		hr = HRESULT_FROM_WIN32(GetLastError());
	}

	return hr;
}

CWVDatagramEndpoint::~CWVDatagramEndpoint()
{
	DWORD	bytes;

	if (m_Id != 0) {
		WvDeviceIoControl(m_hFile, WV_IOCTL_EP_DESTROY, &m_Id, sizeof m_Id,
						  NULL, 0, &bytes, NULL);
	}
	m_pProvider->Release();
}

STDMETHODIMP CWVDatagramEndpoint::
QueryInterface(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IUnknown && riid != IID_IWVEndpoint &&
		riid != IID_IWVDatagramEndpoint) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	*ppvObj = this;
	AddRef();
	return WV_SUCCESS;
}

STDMETHODIMP_(ULONG) CWVDatagramEndpoint::
AddRef(void)
{
	return CWVBase::AddRef();
}

STDMETHODIMP_(ULONG) CWVDatagramEndpoint::
Release(void)
{
	return CWVBase::Release();
}

STDMETHODIMP CWVDatagramEndpoint::
CancelOverlappedRequests(void)
{
	DWORD	bytes;

	return WvDeviceIoControl(m_hFile, WV_IOCTL_EP_CANCEL, &m_Id, sizeof m_Id,
							 NULL, 0, &bytes, NULL) ?
							 WV_SUCCESS : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP CWVDatagramEndpoint::
GetOverlappedResult(OVERLAPPED *pOverlapped,
					DWORD *pNumberOfBytesTransferred, BOOL bWait)
{
	::GetOverlappedResult(m_hFile, pOverlapped, pNumberOfBytesTransferred, bWait);
	return (HRESULT) pOverlapped->Internal;
}

STDMETHODIMP CWVDatagramEndpoint::
Modify(DWORD Option, const VOID* pOptionData, SIZE_T OptionLength)
{
	UNREFERENCED_PARAMETER(Option);
	UNREFERENCED_PARAMETER(pOptionData);
	UNREFERENCED_PARAMETER(OptionLength);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
BindAddress(SOCKADDR* pAddress)
{
	UNREFERENCED_PARAMETER(pAddress);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
Listen(SIZE_T Backlog)
{
	UNREFERENCED_PARAMETER(Backlog);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
Reject(const VOID* pUserData, SIZE_T UserDataLength)
{
	UNREFERENCED_PARAMETER(pUserData);
	UNREFERENCED_PARAMETER(UserDataLength);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
GetRequest(IWVDatagramEndpoint* pEndpoint, OVERLAPPED* pOverlapped)
{
	UNREFERENCED_PARAMETER(pEndpoint);
	UNREFERENCED_PARAMETER(pOverlapped);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
Lookup(const SOCKADDR* pAddress, const VOID* pUserData,
	   SIZE_T UserDataLength, OVERLAPPED* pOverlapped)
{
	UNREFERENCED_PARAMETER(pAddress);
	UNREFERENCED_PARAMETER(pUserData);
	UNREFERENCED_PARAMETER(UserDataLength);
	UNREFERENCED_PARAMETER(pOverlapped);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
Accept(WV_DATAGRAM_PARAM* pParam, OVERLAPPED* pOverlapped)
{
	UNREFERENCED_PARAMETER(pParam);
	UNREFERENCED_PARAMETER(pOverlapped);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
JoinMulticast(const SOCKADDR* pAddress, OVERLAPPED* pOverlapped)
{
	UNREFERENCED_PARAMETER(pAddress);
	UNREFERENCED_PARAMETER(pOverlapped);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
LeaveMulticast(const SOCKADDR* pAddress, OVERLAPPED* pOverlapped)
{
	UNREFERENCED_PARAMETER(pAddress);
	UNREFERENCED_PARAMETER(pOverlapped);

	return E_NOTIMPL;
}

STDMETHODIMP CWVDatagramEndpoint::
Query(WV_DATAGRAM_ATTRIBUTES* pAttributes)
{
	UNREFERENCED_PARAMETER(pAttributes);

	return E_NOTIMPL;
}
