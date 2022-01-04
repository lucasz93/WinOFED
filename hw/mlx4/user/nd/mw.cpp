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

#include "precomp.h"

namespace ND
{
namespace v2
{

// {6A148B1D-A8F8-4935-863D-F9F85D40399C}
const GUID Mw::_Guid = 
{ 0x6a148b1d, 0xa8f8, 0x4935, { 0x86, 0x3d, 0xf9, 0xf8, 0x5d, 0x40, 0x39, 0x9c } };

Mw::Mw(Adapter& adapter, Mr& mr)
    : m_Adapter(adapter),
    m_hMw(0),
    m_uMw(NULL),
    m_Mr(mr),
    m_pBoundMr(NULL)
{
    m_Adapter.AddRef();
    m_Mr.AddRef();
#if DBG
    InterlockedIncrement(&g_nRef[NdMw]);
#endif
}


Mw::~Mw(void)
{
    if( m_pBoundMr != NULL )
    {
        Invalidate();
    }
    m_Mr.Release();

    if( m_hMw != 0 )
    {
        m_Adapter.GetProvider().FreeHandle(m_hMw, IOCTL_ND_MW_FREE);

        //
        // TODO: Clean up user-space MW context.
        //
    }

    m_Adapter.Release();
#if DBG
    InterlockedDecrement(&g_nRef[NdMw]);
#endif
}


HRESULT Mw::Initialize()
{
    //
    // TODO: Once the hardware supports MWs, this code will need to be modified to setup user
    // mode context for the memory window, similar to how CQs, PDs, QPs, etc work.
    //
    union _createMw
    {
        ND_HANDLE   in;
        UINT64      out;
    } createMw;

    createMw.in.Version = ND_IOCTL_VERSION;
    createMw.in.Reserved = 0;
    createMw.in.Handle = m_Adapter.GetPdHandle();

    ULONG cbOut = sizeof(createMw);
    HRESULT hr = m_Adapter.GetProvider().Ioctl(
        IOCTL_ND_MW_CREATE,
        &createMw,
        sizeof(createMw),
        &createMw,
        &cbOut
        );
    if( SUCCEEDED(hr) )
    {
        m_hMw = createMw.out;
    }

    return hr;
}


void* Mw::GetInterface(REFIID riid)
{
    if( riid == IID_IND2MemoryWindow )
    {
        return static_cast<IND2MemoryWindow*>(this);
    }

    if( riid == _Guid )
    {
        return this;
    }

    return _Base::GetInterface(riid);
}


HRESULT Mw::Bind(Mr* pMr, __in_bcount(cbBuffer) const void* pBuffer, SIZE_T cbBuffer, ULONG Flags)
{
    if( m_pBoundMr != NULL )
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    HRESULT hr = pMr->BindMr(&m_Mr, pBuffer, cbBuffer, Flags);
    if( SUCCEEDED(hr) )
    {
        pMr->AddRef();
        m_pBoundMr = pMr;
    }
    return hr;
}


HRESULT Mw::Invalidate()
{
    if( m_pBoundMr == NULL )
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    HRESULT hr = m_pBoundMr->InvalidateMr(&m_Mr);
    if( SUCCEEDED(hr) )
    {
        m_pBoundMr->Release();
        m_pBoundMr = NULL;
    }
    return hr;
}

} // namespace ND::v2

namespace v1
{

Mw::Mw(INDAdapter& adapter, IND2MemoryWindow& mw, ND_RESULT* pInvalidateResult)
    : m_Adapter(adapter),
    m_Mw(mw),
    m_pInvalidateResult(pInvalidateResult)
{
    m_Adapter.AddRef();
    m_Mw.AddRef();
}


Mw::~Mw(void)
{
    m_Mw.Release();
    m_Adapter.Release();
}


void* Mw::GetInterface(REFIID riid)
{
    if( riid == IID_INDMemoryWindow )
    {
        return static_cast<INDMemoryWindow*>(this);
    }

    return _Base::GetInterface(riid);
}

} // namespace ND::v1
} // namespace ND
