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
#include "ndspi.h"


namespace NetworkDirect
{

    class CAdapter;

    class CMw :
        public INDMemoryWindow
    {
    public:
        CMw(void);
        ~CMw(void);

        HRESULT Initialize(
            CAdapter* pParent,
            ND_RESULT* pInvalidateResult );

        // *** IUnknown methods ***
        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID riid,
            LPVOID FAR* ppvObj
            );

        ULONG STDMETHODCALLTYPE AddRef(void);

        ULONG STDMETHODCALLTYPE Release(void);

        // *** INDMemoryWindow methods ***
        HRESULT STDMETHODCALLTYPE Close(void);

        //operator ND_RESULT*(){ return m_pInvalidateResult; };

    private:
        volatile LONG m_nRef;

        CAdapter* m_pParent;

        ND_RESULT* m_pInvalidateResult;
    };

} // namespace