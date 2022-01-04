/*
 * Copyright (c) Microsoft Corporation.  All rights reserved.
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

#pragma once


namespace ND
{
namespace v2
{

class Provider
    : public Unknown<Provider,IND2Provider>
{
    typedef Unknown<Provider,IND2Provider> _Base;

    HANDLE  m_hProvider;

private:
    Provider(void);

public:
    static HRESULT Create(__in REFIID riid, __out void** ppProvider);
    ~Provider(void);

public:
    // IND2Provider Methods
    HRESULT STDMETHODCALLTYPE QueryAddressList(
        __out_bcount_part_opt(*pcbAddressList, *pcbAddressList) SOCKET_ADDRESS_LIST* pAddressList,
        __inout ULONG* pcbAddressList
        );

    HRESULT STDMETHODCALLTYPE ResolveAddress(
        __in_bcount(cbAddress) const struct sockaddr* pAddress,
        ULONG cbAddress,
        __out UINT64* pAdapterId
        );

    HRESULT STDMETHODCALLTYPE OpenAdapter(
        __in REFIID iid,
        UINT64 adapterId,
        __deref_out VOID** ppAdapter
        );

public:
    void* GetInterface(REFIID riid);

    HRESULT Ioctl(
        __in ULONG IoControlCode,
        __in_bcount(cbInput) void* pInput,
        __in ULONG cbInput,
        __out_bcount_part_opt(*pcbOutput,*pcbOutput) void* pOutput,
        __inout ULONG* pcbOutput
        );

    HRESULT Ioctl(
        __in ULONG IoControlCode,
        __in_bcount(cbInput) void* pInput,
        __in ULONG cbInput
        );

    HANDLE GetFileHandle() const { return m_hProvider; }

    void FreeHandle( UINT64 handle, ULONG ctlCode );
};

} // namespace ND::v2

namespace v1
{

class Provider
    : public Unknown<Provider,INDProvider>
{
    typedef Unknown<Provider,INDProvider> _Base;

    IND2Provider* m_pProvider;

private:
    Provider(void);

public:
    static HRESULT Create(__out void** ppProvider);
    ~Provider(void);

public:
    // INDProvider Methods
    HRESULT STDMETHODCALLTYPE QueryAddressList(
        __out_bcount_part_opt(*pBufferSize, *pBufferSize) SOCKET_ADDRESS_LIST* pAddressList,
        __inout SIZE_T* pBufferSize
        );

    HRESULT STDMETHODCALLTYPE OpenAdapter(
        __in_bcount(AddressLength) const struct sockaddr* pAddress,
        __in SIZE_T AddressLength,
        __deref_out INDAdapter** ppAdapter
        );

public:
    void* GetInterface(REFIID iid);
};

} // namespace ND::v1
} // namespace ND
