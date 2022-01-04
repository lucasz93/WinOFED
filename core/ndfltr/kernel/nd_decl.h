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
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE ANd
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#ifndef _ND_DECL_H_
#define _ND_DECL_H_


extern WDFDEVICE    ControlDevice;
extern IBAT_IFC     IbatInterface;

class ND_PARTITION;
class ND_PROVIDER;

typedef void
FN_REQUEST_HANDLER(
    __in ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool InternalDeviceControl
    );

#define ND_RDMA_DEVICE_POOL_TAG 'vddn'

struct ND_RDMA_DEVICE
{
    LIST_ENTRY              Entry;
    LONG                    Ref;
    KEVENT                  Event;
    WDFDEVICE               WdfDev;
    ib_ca_handle_t          hDevice;
    RDMA_INTERFACE_VERBS    Interface;
    // TODO: This should really be an RDMA_INTERFACE_CM, not IB specific.
    INFINIBAND_INTERFACE_CM CmInterface;

    ND2_ADAPTER_INFO        Info;

public:
    ib_ca_attr_t* QueryCaAttributes();

};


template<typename T> class RemoveDeviceFunctor
{
    ND_RDMA_DEVICE* dev;

public:
    RemoveDeviceFunctor(ND_RDMA_DEVICE* pDev) : dev(pDev) {}

    bool operator() (T* obj)
    {
        obj->RemoveHandler(dev);
        return false;
    }
};

#endif // _ND_DECL_H_
