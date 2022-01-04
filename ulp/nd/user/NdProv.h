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

#pragma once
#include <iba/ib_at_ioctl.h>

#ifdef __cplusplus
namespace NetworkDirect
{


///////////////////////////////////////////////////////////////////////////////
//
// HPC Pack 2008 Beta 2 SPI
//
///////////////////////////////////////////////////////////////////////////////

class CProvider :
    public INDProvider
{
    friend class CClassFactory;

public:
    CProvider(void);
    ~CProvider(void);

    // IUnknown Methods
    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppObject
        );

    ULONG STDMETHODCALLTYPE AddRef(void);
    ULONG STDMETHODCALLTYPE Release(void);

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

private:
    volatile LONG m_nRef;
};


class CClassFactory : public IClassFactory
{
public:
    CClassFactory(void);
    ~CClassFactory(void);

    // IUnknown Methods.
    HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppObject );
    ULONG STDMETHODCALLTYPE AddRef(void);
    ULONG STDMETHODCALLTYPE Release(void);

    // IClassFactory Methods.
    HRESULT STDMETHODCALLTYPE CreateInstance( IUnknown* pUnkOuter, REFIID riid, void** ppObject );
    HRESULT STDMETHODCALLTYPE LockServer( BOOL fLock );

private:
    volatile LONG m_nRef;
};

} // namespace

void* __cdecl operator new(
    size_t count
    );

void __cdecl operator delete(
    void* object
    );

#endif // __cplusplus
