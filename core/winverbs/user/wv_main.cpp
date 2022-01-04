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

#include "wv_memory.h"
#include "wv_provider.h"
#include "wv_base.h"

volatile LONG WvRef;
HANDLE heap;

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(lpReserved);

	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		heap = HeapCreate(0, 0, 0);
		if (heap == NULL) {
			return FALSE;
		}
		break;
	case DLL_PROCESS_DETACH:
		HeapDestroy(heap);
		break;
	default:
		break;
	}

	return TRUE;
}

STDAPI DllCanUnloadNow(void)
{
	return WvRef ? S_FALSE : S_OK;
}

__declspec(dllexport) HRESULT WvGetObject(REFIID riid, LPVOID FAR* ppvObj)
{
	if (riid != IID_IWVProvider) {
		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

	return CWVProvider::CreateInstance((IWVProvider **) ppvObj);
}

HRESULT WvConvertIbStatus(ib_api_status_t status)
{
	switch (status) {
	case IB_SUCCESS:				return WV_SUCCESS;
	case IB_INSUFFICIENT_RESOURCES:	return WV_INSUFFICIENT_RESOURCES;
	case IB_INSUFFICIENT_MEMORY:	return WV_NO_MEMORY;
	case IB_INVALID_PARAMETER:		return WV_INVALID_PARAMETER;
	case IB_INVALID_SETTING:		return WV_INVALID_PARAMETER;
	case IB_NOT_FOUND:				return WV_INVALID_ADDRESS;
	case IB_TIMEOUT:				return WV_TIMEOUT;
	case IB_CANCELED:				return WV_CANCELLED;
	case IB_INTERRUPTED:			return WV_CANCELLED;
	case IB_INVALID_PERMISSION:		return WV_ACCESS_VIOLATION;
	case IB_UNSUPPORTED:			return WV_NOT_SUPPORTED;
	case IB_OVERFLOW:				return WV_BUFFER_OVERFLOW;
	case IB_MAX_MCAST_QPS_REACHED:	return WV_INSUFFICIENT_RESOURCES;
	case IB_INVALID_QP_STATE:		return WV_INVALID_PARAMETER;
	case IB_INVALID_APM_STATE:		return WV_INVALID_PARAMETER;
	case IB_INVALID_PORT_STATE:		return WV_INVALID_PARAMETER;
	case IB_INVALID_STATE:			return WV_INVALID_PARAMETER;
	case IB_RESOURCE_BUSY:			return WV_DEVICE_BUSY;
	case IB_INVALID_PKEY:			return WV_INVALID_HANDLE;
	case IB_INVALID_LKEY:			return WV_INVALID_HANDLE;
	case IB_INVALID_RKEY:			return WV_INVALID_HANDLE;
	case IB_INVALID_MAX_WRS:		return WV_INSUFFICIENT_RESOURCES;
	case IB_INVALID_MAX_SGE:		return WV_INSUFFICIENT_RESOURCES;
	case IB_INVALID_CQ_SIZE:		return WV_INSUFFICIENT_RESOURCES;
	case IB_INVALID_SRQ_SIZE:		return WV_INSUFFICIENT_RESOURCES;
	case IB_INVALID_SERVICE_TYPE:	return WV_NOT_SUPPORTED;
	case IB_INVALID_GID:			return WV_INVALID_ADDRESS;
	case IB_INVALID_LID:			return WV_INVALID_ADDRESS;
	case IB_INVALID_GUID:			return WV_INVALID_ADDRESS;
	case IB_INVALID_GUID_MASK:		return WV_INVALID_ADDRESS;
	case IB_INVALID_CA_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_AV_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_CQ_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_QP_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_SRQ_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_PD_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_MR_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_FMR_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_MW_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_MCAST_HANDLE:	return WV_INVALID_HANDLE;
	case IB_INVALID_CALLBACK:		return WV_INVALID_PARAMETER;
	case IB_INVALID_AL_HANDLE:		return WV_INVALID_HANDLE;
	case IB_INVALID_HANDLE:			return WV_INVALID_HANDLE;
	case IB_ERROR:					return WV_UNKNOWN_ERROR;
	case IB_REMOTE_ERROR:			return WV_REMOTE_OP_ERROR;
	case IB_VERBS_PROCESSING_DONE:	return WV_SUCCESS;
	case IB_INVALID_WR_TYPE:		return WV_INVALID_PARAMETER;
	case IB_QP_IN_TIMEWAIT:			return WV_INVALID_PARAMETER;
	case IB_EE_IN_TIMEWAIT:			return WV_INVALID_PARAMETER;
	case IB_INVALID_PORT:			return WV_INVALID_ADDRESS;
	case IB_NOT_DONE:				return WV_PENDING;
	case IB_INVALID_INDEX:			return WV_INVALID_PARAMETER;
	case IB_NO_MATCH:				return WV_INVALID_PARAMETER;
	case IB_PENDING:				return WV_PENDING;
	default:						return WV_UNKNOWN_ERROR;
	}
}

HRESULT WvConvertWSAStatus(int status)
{
	switch (status) {
	case 0:							return WV_SUCCESS;
	case WSAEADDRINUSE:				return WV_ADDRESS_ALREADY_EXISTS;
	case WSAEADDRNOTAVAIL:			return WV_INVALID_ADDRESS;
	case WSAENETDOWN:				return WV_HOST_UNREACHABLE;
	case WSAENETUNREACH:			return WV_HOST_UNREACHABLE;
	case WSAECONNABORTED:			return WV_CONNECTION_ABORTED;
	case WSAEISCONN:				return WV_CONNECTION_ACTIVE;
	case WSAENOTCONN:				return WV_CONNECTION_INVALID;
	case WSAETIMEDOUT:				return WV_IO_TIMEOUT;
	case WSAECONNREFUSED:			return WV_CONNECTION_REFUSED;
	case WSAEHOSTUNREACH:			return WV_HOST_UNREACHABLE;
	case WSAEACCES:					return WV_ADDRESS_ALREADY_EXISTS;
	default:						return WV_UNKNOWN_ERROR;
	}
}