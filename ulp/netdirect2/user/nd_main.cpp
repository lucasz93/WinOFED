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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <windows.h>
#include <ws2spi.h>
#include <stdio.h>
#include "nd_provider.h"

HANDLE g_hHeap;
volatile LONG g_nRef;

extern "C" {

extern BOOL APIENTRY
_DllMainCRTStartupForGS(HINSTANCE h_module, DWORD ul_reason_for_call,
						LPVOID lp_reserved);


BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		if (_DllMainCRTStartupForGS(hInstance, dwReason, lpReserved)) {
			return FALSE;
		}

		g_hHeap = HeapCreate(0, 0, 0);
		return (g_hHeap != NULL);
	case DLL_PROCESS_DETACH:
		if (g_hHeap != NULL) {
			HeapDestroy(g_hHeap);
		}
		return _DllMainCRTStartupForGS(hInstance, dwReason, lpReserved);
	default:
		return FALSE;
	}
}

STDAPI DllCanUnloadNow(void)
{
	return (g_nRef == 0) ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
	UNREFERENCED_PARAMETER(rclsid);

	if (riid != IID_INDProvider) {
		*ppv = NULL;
		return E_NOINTERFACE;
	}

	return CNDProvider::CreateInstance(ppv);
}

int WSPStartup(WORD wVersionRequested, LPWSPDATA lpWSPData,
			   LPWSAPROTOCOL_INFOW lpProtocolInfo,
			   WSPUPCALLTABLE UpcallTable, LPWSPPROC_TABLE lpProcTable)
{
	UNREFERENCED_PARAMETER(wVersionRequested);
	UNREFERENCED_PARAMETER(lpWSPData);
	UNREFERENCED_PARAMETER(lpProtocolInfo);
	UNREFERENCED_PARAMETER(UpcallTable);
	UNREFERENCED_PARAMETER(lpProcTable);
	return WSASYSNOTREADY;
}

} // extern "C"