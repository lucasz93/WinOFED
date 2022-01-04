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

#include "NdMw.h"
#include "NdAdapter.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdMw.tmh"
#endif


namespace NetworkDirect
{

    CMw::CMw(void) :
        m_nRef( 1 ),
        m_pParent( NULL )
    {
    }

    CMw::~CMw(void)
    {
        if( m_pParent )
            m_pParent->Release();
    }

    HRESULT CMw::Initialize(
        CAdapter* pParent,
        ND_RESULT* pInvalidateResult
        )
    {
        m_pParent = pParent;
        m_pParent->AddRef();

        m_pInvalidateResult = pInvalidateResult;
        return S_OK;
    }

    HRESULT CMw::QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        )
    {
        if( IsEqualIID( riid, IID_IUnknown ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        if( IsEqualIID( riid, IID_INDMemoryWindow ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG CMw::AddRef(void)
    {
        return InterlockedIncrement( &m_nRef );
    }

    ULONG CMw::Release(void)
    {
        ULONG ref = InterlockedDecrement( &m_nRef );
        if( ref == 0 )
            delete this;

        return ref;
    }

    HRESULT CMw::Close(void)
    {
        Release();
        return S_OK;
    }

} // namespace
