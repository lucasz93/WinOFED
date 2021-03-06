/*
 * Copyright (c) 2008 Microsoft Corporation.  All rights reserved.
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

#ifndef _IBAT_H_
#define _IBAT_H_

#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct _IBAT_PATH_BLOB
{
    UINT8 byte[64];

} IBAT_PATH_BLOB;

#ifdef __cplusplus
namespace IBAT
{

HRESULT
IoControl(
  __in         DWORD dwIoControlCode,
  __in_opt     LPVOID lpInBuffer,
  __in         DWORD nInBufferSize,
  __out_opt    LPVOID lpOutBuffer,
  __in         DWORD nOutBufferSize,
  __out_opt    LPDWORD lpBytesReturned
    );


template<typename InT, typename OutT>
HRESULT  IoControl(
    __in  DWORD               dwIoControlCode,
    __in  const InT&          input,
    __out OutT*               pOutput
    )
{
    DWORD cb;
    HRESULT hr = IBAT::IoControl(
                dwIoControlCode,
                (VOID*)&input,
                sizeof(input),
                (VOID*)pOutput,
                sizeof(*pOutput),
                &cb
                );
    if( SUCCEEDED( hr ) )
    {
        if( cb < sizeof( *pOutput ) )
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
}
    }
    return hr;
}


HRESULT
QueryPath(
    __in const struct sockaddr* pSrcAddr,
    __in const struct sockaddr* pDestAddr,
    __out IBAT_PATH_BLOB* pPath
    );

}
#else /* __cplusplus */


HRESULT
IbatQueryPath(
    __in const struct sockaddr* pSrcAddr,
    __in const struct sockaddr* pDestAddr,
    __out IBAT_PATH_BLOB* pPath
    );

#endif /* __cplusplus */

#endif	// _IBAT_H_
