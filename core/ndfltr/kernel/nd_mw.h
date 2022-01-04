/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
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

#ifndef _ND_MW_H_
#define _ND_MW_H_

#define ND_MEMORY_WINDOW_TAG 'wmdn'

class ND_MEMORY_WINDOW
    : public RdmaResource<ND_PROTECTION_DOMAIN, ib_mw_handle_t, ci_interface_t>
{
    typedef RdmaResource<ND_PROTECTION_DOMAIN, ib_mw_handle_t, ci_interface_t> _Base;

    net32_t                                 m_RKey;

private:
    ND_MEMORY_WINDOW(__in ND_PROTECTION_DOMAIN* pPd);
    ~ND_MEMORY_WINDOW();
    NTSTATUS Initialize(
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData
        );

public:
    static
    NTSTATUS
    Create(
        __in ND_PROTECTION_DOMAIN* pPd,
        KPROCESSOR_MODE RequestorMode,
        __inout ci_umv_buf_t* pVerbsData,
        __out ND_MEMORY_WINDOW** ppMw
        );

    void Dispose();

    void RemoveHandler();
};

FN_REQUEST_HANDLER NdMwCreate;
FN_REQUEST_HANDLER NdMwFree;

#endif // _ND_MW_H_
