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
 *
 */


#include "ibatp.h"
#include "route.h"


//
// Negative timeout does recursive doubling.
//
// Retry intervals total 10 seconds, and are as follow:
//  250 ms
//  500 ms
//  1000 ms
//  2000 ms
//  3125 ms
//  3125 ms
//
#define IBAT_PATH_QUERY_TIMEOUT (ULONG)-(3125L << 16 | 250L)
#define IBAT_PATH_QUERY_RETRIES 5


//
// Structure used to track requests for routes, while the route hasn't been resolved.
//
struct IBAT_ROUTE_REQUEST
{
    IBAT_ROUTE_REQUEST* pNext;
    FN_IBAT_QUERY_PATH_CALLBACK* callback;
    VOID* completionContext;
};


IbatRoute*
IbatRoute::Create(
    const ib_gid_t* pSrcGid,
    const ib_gid_t* pDestGid,
    UINT16 pkey
    )
{
    IbatRoute* pRoute = static_cast<IbatRoute*>(
        ExAllocatePoolWithTag( NonPagedPool, sizeof(*pRoute), IBAT_POOL_TAG )
        );

    if( pRoute == NULL )
    {
        return NULL;
    }

    pRoute->m_srcGid = *pSrcGid;
    pRoute->m_destGid = *pDestGid;
    pRoute->m_pkey = pkey;

    KeInitializeSpinLock( &pRoute->m_requestLock );
    pRoute->m_requestList = NULL;
    pRoute->m_state = NlnsUnreachable;
    pRoute->m_hQuery = NULL;
    pRoute->m_nRef = 1;

    return pRoute;
}


VOID
IbatRoute::QueryPathCompletion(
    __in NTSTATUS status,
    __in ib_path_rec_t* const pPath
    )
{
    KLOCK_QUEUE_HANDLE hLock;
    KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );
    if( status == STATUS_SUCCESS )
    {
        NT_ASSERT( m_state != NlnsReachable );
        RtlCopyMemory( &m_path, pPath, sizeof(m_path) );
        m_state = NlnsReachable;
    }
    else
    {
        m_state = NlnsUnreachable;
    }

    for( IBAT_ROUTE_REQUEST* pRequest = m_requestList;
        pRequest != NULL;
        pRequest = m_requestList
        )
    {
        m_requestList = pRequest->pNext;
        KeReleaseInStackQueuedSpinLock( &hLock );
        pRequest->callback( pRequest->completionContext, status, pPath );
        ExFreePoolWithTag( pRequest, IBAT_POOL_TAG );
        KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );
    }
    KeReleaseInStackQueuedSpinLock( &hLock );
}


VOID
AL_API
IbatRoute::PathQueryCallback(
    __in ib_query_rec_t* pQueryResult
    )
{
    ib_path_rec_t* pPath;
    NTSTATUS status;
    IbatRoute* pRoute = static_cast<IbatRoute*>(
        const_cast<VOID*>(pQueryResult->query_context)
        );

    InterlockedExchangePointer( reinterpret_cast<PVOID*>(&pRoute->m_hQuery), NULL );

    if( pQueryResult->status != IB_SUCCESS )
    {
        pPath = NULL;
        switch( pQueryResult->status )
        {
        case IB_CANCELED:
            //
            // If there are requests left in the request list when we shutdown,
            // don't complete them as cancelled.  The only reason we would shutdown
            // a route is if:
            //  - it is replaced by a new route, in which case the new route will take
            //    over the request processing
            //  - the port is going down, in which case the network is unreachable.
            //
            // Thus, if there are any requests here, the correct status is
            // STATUS_NETWORK_UNREACHABLE.
            //
            status = STATUS_NETWORK_UNREACHABLE;
            break;

        case IB_TIMEOUT:
            status = STATUS_IO_TIMEOUT;
            break;

        case IB_REMOTE_ERROR:
            NT_ASSERT( pQueryResult->p_result_mad != NULL );
            NT_ASSERT( pQueryResult->p_result_mad->p_mad_buf != NULL );
            if( pQueryResult->p_result_mad->p_mad_buf->status == IB_SA_MAD_STATUS_NO_RESOURCES )
            {
                //
                // SA can't process the request.
                //
                status = STATUS_IO_TIMEOUT;
            }
            else
            {
                status = STATUS_HOST_UNREACHABLE;
            }
            break;

        default:
            status = STATUS_HOST_UNREACHABLE;
        }
    }
    else
    {
        pPath = ib_get_query_path_rec( pQueryResult->p_result_mad, 0 );
        status = STATUS_SUCCESS;
    }

    pRoute->QueryPathCompletion( status, pPath );

    if( pQueryResult->p_result_mad != NULL )
    {
        ib_put_mad( pQueryResult->p_result_mad );
    }

    pRoute->Release();
}


NTSTATUS
IbatRoute::QueryPathUnsafe()
{
    if( m_state != NlnsUnreachable )
    {
        return STATUS_PENDING;
    }

    ib_path_rec_t pathRec;
    pathRec.dgid = m_destGid;
    pathRec.sgid = m_srcGid;
    pathRec.pkey = m_pkey;

    ib_user_query_t userQuery;
    userQuery.method = IB_MAD_METHOD_GET;
    userQuery.attr_id = IB_MAD_ATTR_PATH_RECORD;
    userQuery.attr_size = sizeof(pathRec);
    userQuery.comp_mask = IB_PR_COMPMASK_DGID | IB_PR_COMPMASK_SGID | IB_PR_COMPMASK_PKEY;
    userQuery.p_attr = &pathRec;

    ib_query_req_t query;
    query.query_type = IB_QUERY_USER_DEFINED;
    query.p_query_input = &userQuery;
    query.port_guid = m_srcGid.unicast.interface_id;

    query.timeout_ms = IBAT_PATH_QUERY_TIMEOUT;
    query.retry_cnt = IBAT_PATH_QUERY_RETRIES;
    query.flags = 0;

    query.query_context = this;
    query.pfn_query_cb = IbatRoute::PathQueryCallback;

    AddRef();
    ib_api_status_t ibStatus = ib_query( gh_al, &query, &m_hQuery );
    if( ibStatus != IB_SUCCESS )
    {
        Release();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    m_state = NlnsIncomplete;
    return STATUS_PENDING;
}


//
// Called with the IbatRouter's lock held.
//
NTSTATUS
IbatRoute::Resolve(
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext,
    __out ib_path_rec_t* pPath
    )
{
    KLOCK_QUEUE_HANDLE hLock;
    IBAT_ROUTE_REQUEST* pRequest;

    if( m_state == NlnsReachable )
    {
        RtlCopyMemory( pPath, &m_path, sizeof(*pPath) );
        return STATUS_SUCCESS;
    }

    pRequest = static_cast<IBAT_ROUTE_REQUEST*>(
        ExAllocatePoolWithTag( NonPagedPool, sizeof(*pRequest), IBAT_POOL_TAG )
        );
    if( pRequest == NULL )
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    pRequest->callback = completionCallback;
    pRequest->completionContext = completionContext;

    KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );

    pRequest->pNext = m_requestList;
    m_requestList = pRequest;

    NTSTATUS status = QueryPathUnsafe();
    if( !NT_SUCCESS(status) )
    {
        m_requestList = m_requestList->pNext;
        ExFreePoolWithTag( pRequest, IBAT_POOL_TAG );
    }
    KeReleaseInStackQueuedSpinLock( &hLock );
    return status;
}


//
// Called with the IbatRouter's lock held on a newly created route.
//
VOID
IbatRoute::Resolve(
    __in_opt IBAT_ROUTE_REQUEST* requestList
    )
{
    if( requestList == NULL )
    {
        return;
    }

    KLOCK_QUEUE_HANDLE hLock;
    KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );
    NT_ASSERT( m_requestList == NULL );
    m_requestList = requestList;

    NT_ASSERT( m_state == NlnsUnreachable );

    NTSTATUS status = QueryPathUnsafe();
    KeReleaseInStackQueuedSpinLock( &hLock );
    if( !NT_SUCCESS(status) )
    {
        QueryPathCompletion( status, NULL );
    }
}


NTSTATUS
IbatRoute::CancelResolve(
    __in FN_IBAT_QUERY_PATH_CALLBACK* completionCallback,
    __in VOID* completionContext
    )
{
    KLOCK_QUEUE_HANDLE hLock;
    IBAT_ROUTE_REQUEST* pRequest;
    IBAT_ROUTE_REQUEST** link;
    NTSTATUS status = STATUS_NOT_FOUND;

    KeAcquireInStackQueuedSpinLock( &m_requestLock, &hLock );

    link = &m_requestList;
    for( pRequest = *link; pRequest != NULL; pRequest = *link )
    {
        if( pRequest->callback == completionCallback &&
            pRequest->completionContext == completionContext )
        {
            *link = pRequest->pNext;

            ExFreePoolWithTag( pRequest, IBAT_POOL_TAG );
            status = STATUS_SUCCESS;
        }
        else
        {
            link = &pRequest->pNext;
        }
    }

    KeReleaseInStackQueuedSpinLock( &hLock );
    return status;
}
