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

#ifndef _ND_DRIVER_H_
#define _ND_DRIVER_H_


class ND_DRIVER
{
    LIST_ENTRY                  m_DevList;
    HANDLE_TABLE<ND_PARTITION>  m_PartitionTable;
    HANDLE_TABLE<ND_PROVIDER>   m_ProvTable;
    KGUARDED_MUTEX              m_Lock;
    ND_PARTITION                m_Partition0;

public:
    ND_DRIVER();
    ~ND_DRIVER();

    ND_RDMA_DEVICE* GetRdmaDevice(UINT64 AdapterId);
    ND_PROVIDER* GetProvider(UINT64 Handle);
    ND_PARTITION* GetPartition(UINT64 Handle, bool InternalDeviceControl);
    NTSTATUS FreePartition(UINT64 Handle);

    NTSTATUS ResolveAddress(const SOCKADDR_INET& Addr, const GUID& DriverId, UINT64* pId);
    void FreeProvider(WDFFILEOBJECT FileObject);
    NTSTATUS AddDevice(ND_RDMA_DEVICE* pDev);

    void RemoveHandler(ND_RDMA_DEVICE* pDev);

public:
    static FN_REQUEST_HANDLER InitProvider;
    static FN_REQUEST_HANDLER CreatePartition;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ND_DRIVER, NdDriverGetContext);

ND_RDMA_DEVICE* NdRdmaDeviceGet( UINT64 AdapterId );
void NdRdmaDevicePut(ND_RDMA_DEVICE *pDevice);

void NdDisableRemove();
void NdEnableRemove();
void NdLockRemove();

static inline void NdInitVerbsData(ci_umv_buf_t *pVerbsData,
                                   SIZE_T InputLength, SIZE_T OutputLength,
                                   void *pBuffer)
{
    pVerbsData->command = TRUE;
    pVerbsData->input_size = (UINT32) InputLength;
    pVerbsData->output_size = (UINT32) OutputLength;
    pVerbsData->p_inout_buf = (ULONG_PTR) pBuffer;
    pVerbsData->status = 0;
}

void NdCompleteRequests(WDFQUEUE Queue, NTSTATUS ReqStatus);
void NdFlushQueue(WDFQUEUE Queue, NTSTATUS ReqStatus);
void NdCompleteRequestsWithInformation(WDFQUEUE Queue, NTSTATUS ReqStatus);

#endif // _ND_DRIVER_H_
