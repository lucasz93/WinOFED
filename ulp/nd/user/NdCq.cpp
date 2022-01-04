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

#include "NdCq.h"
#include "NdAdapter.h"
#include "al_cq.h"
#include "al_dev.h"
#include "al.h"
#include "al_verbs.h"
#pragma warning( push, 3 )
#include "winternl.h"
#pragma warning( pop )
#include "limits.h"
#include "nddebug.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "NdCq.tmh"
#endif


namespace NetworkDirect
{

    CCq::CCq(void) :
        m_nRef( 1 ),
        m_pParent( NULL ),
        m_hCq( 0 ),
        m_uCq( NULL )
    {
    }

    CCq::~CCq(void)
    {
        if( m_hCq )
            CloseCq();

        if( m_pParent )
            m_pParent->Release();
    }

    HRESULT CCq::Initialize(
        CAdapter* pParent,
        SIZE_T nEntries )
    {
        if( nEntries > UINT_MAX )
            return ND_INVALID_PARAMETER;

        m_pParent = pParent;
        pParent->AddRef();

        return CreateCq( (UINT32)nEntries );
    }

    HRESULT CCq::QueryInterface(
        REFIID riid,
        LPVOID FAR* ppvObj
        )
    {
        if( IsEqualIID( riid, IID_IUnknown ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        if( IsEqualIID( riid, IID_INDCompletionQueue ) )
        {
            *ppvObj = this;
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG CCq::AddRef(void)
    {
        return InterlockedIncrement( &m_nRef );
    }

    ULONG CCq::Release(void)
    {
        ULONG ref = InterlockedDecrement( &m_nRef );
        if( ref == 0 )
            delete this;

        return ref;
    }

    // *** INDOverlapped methods ***
    HRESULT CCq::CancelOverlappedRequests(void)
    {
        ND_ENTER( ND_DBG_NDI );

        DWORD BytesRet;
        DeviceIoControl(
            m_pParent->m_hSync,
            UAL_NDI_CANCEL_CQ,
            &m_hCq,
            sizeof(m_hCq),
            NULL,
            0,
            &BytesRet,
            NULL
            );

        return S_OK;
    }

    HRESULT CCq::GetOverlappedResult(
        __inout OVERLAPPED *pOverlapped,
        __out SIZE_T *pNumberOfBytesTransferred,
        __in BOOL bWait
        )
    {
        ND_ENTER( ND_DBG_NDI );

        *pNumberOfBytesTransferred = 0;
        ::GetOverlappedResult(
            m_pParent->GetFileHandle(),
            pOverlapped,
            (DWORD*)pNumberOfBytesTransferred,
            bWait );
        ND_PRINT( TRACE_LEVEL_VERBOSE, ND_DBG_NDI,
            ("==> %s, result %#x, bytes %d\n", __FUNCTION__, (int)pOverlapped->Internal, (int)*pNumberOfBytesTransferred ));
        return (HRESULT)pOverlapped->Internal;
    }

    // *** INDCompletionQueue methods ***
    HRESULT CCq::Resize(
        __in SIZE_T nEntries
        )
    {
        ND_ENTER( ND_DBG_NDI );

        if( nEntries > UINT_MAX )
            return ND_INVALID_PARAMETER;

        ib_api_status_t			status;

        /* Clear the IOCTL buffer */
        ual_modify_cq_ioctl_t	cq_ioctl;
        RtlZeroMemory( &cq_ioctl, sizeof(cq_ioctl) );

        /* Call the uvp pre call if the vendor library provided a valid ca handle */
        if( m_uCq && m_pParent->m_Ifc.user_verbs.pre_resize_cq )
        {
            /* Pre call to the UVP library */
            status = m_pParent->m_Ifc.user_verbs.pre_resize_cq(
                m_uCq, (uint32_t*)&nEntries, &cq_ioctl.in.umv_buf );
            if( status != IB_SUCCESS )
                goto exit;
        }

        cq_ioctl.in.h_cq = m_hCq;
        cq_ioctl.in.size = (DWORD)nEntries;

        DWORD BytesRet;
        BOOL fSuccess = DeviceIoControl(
            m_pParent->m_hSync,
            UAL_MODIFY_CQ,
            &cq_ioctl.in,
            sizeof(cq_ioctl.in),
            &cq_ioctl.out,
            sizeof(cq_ioctl.out),
            &BytesRet,
            NULL
            );

        if( fSuccess != TRUE || BytesRet != sizeof(cq_ioctl.out) )
            status = IB_ERROR;
        else
            status = cq_ioctl.out.status;

        /* Post uvp call */
        if( m_uCq && m_pParent->m_Ifc.user_verbs.post_resize_cq )
        {
            m_pParent->m_Ifc.user_verbs.post_resize_cq(
                m_uCq, status, cq_ioctl.out.size, &cq_ioctl.out.umv_buf );
        }

exit:
        switch( status )
        {
        case IB_INVALID_CQ_SIZE:
            return ND_INVALID_PARAMETER;

        case IB_SUCCESS:
            return S_OK;

        case IB_INSUFFICIENT_RESOURCES:
            return ND_INSUFFICIENT_RESOURCES;

        default:
            return ND_UNSUCCESSFUL;
        }
    }

    HRESULT CCq::Notify(
        __in DWORD Type,
        __inout OVERLAPPED* pOverlapped
        )
    {
//        ND_ENTER( ND_DBG_NDI );

        ual_ndi_notify_cq_ioctl_in_t ioctl;
        ioctl.h_cq = m_hCq;
        ioctl.notify_comps = (boolean_t)Type;
        pOverlapped->Internal = ND_PENDING;
        HRESULT hr = g_NtDeviceIoControlFile(
            m_pParent->GetFileHandle(),
            pOverlapped->hEvent,
            NULL,
            (ULONG_PTR)pOverlapped->hEvent & 1 ? NULL : pOverlapped,
            (IO_STATUS_BLOCK*)&pOverlapped->Internal,
            UAL_NDI_NOTIFY_CQ,
            &ioctl,
            sizeof(ioctl),
            NULL,
            0 );

        if( hr == ND_PENDING && Type != ND_CQ_NOTIFY_ERRORS )
        {
            m_pParent->m_Ifc.user_verbs.rearm_cq(
                m_uCq,
                (Type == ND_CQ_NOTIFY_SOLICITED) ? TRUE : FALSE
                );
            ND_PRINT( TRACE_LEVEL_VERBOSE, ND_DBG_NDI,
                ("==> %s, rearming with Type %d\n", __FUNCTION__, Type));
        }
        else
        {
            ND_PRINT( TRACE_LEVEL_ERROR, ND_DBG_NDI,
                ("==> %s failed: hr %#x, notify_type %d \n", __FUNCTION__, hr, Type ));
        }
        return hr;
    }

    SIZE_T CCq::GetResults(
        __out_ecount(nResults) ND_RESULT* pResults[],
        __in SIZE_T nResults
        )
    {
#if DBG    
        if (!(++g.c_cnt % 100000000))        //  || !(rcv_pkts % 1000) || !(snd_pkts % 1000)
            ND_PRINT( TRACE_LEVEL_VERBOSE, ND_DBG_NDI,
                ("==> %s, cnt %I64d, rcv: %I64d:%I64d:%I64d, snd %I64d:%I64d:%I64d\n", 
                __FUNCTION__, g.c_cnt, 
                g.c_rcv_pkts, g.c_rcv_bytes, g.c_rcv_pkts_err,
                g.c_snd_pkts, g.c_snd_bytes, g.c_snd_pkts_err));
#endif                
        SIZE_T i = 0;

        while( nResults-- )
        {

            uvp_wc_t wc;
            int n = m_pParent->m_Ifc.user_verbs.poll_cq_array( m_uCq, 1, &wc );
            if( n <= 0 )
            {
                break;
            }

            pResults[i] = (ND_RESULT*)wc.wr_id;
            if( wc.wc_type == IB_WC_RECV )
                pResults[i]->BytesTransferred = wc.length;

            if( wc.recv.conn.recv_opt & IB_RECV_OPT_IMMEDIATE )
            {
                // Emulated receive with invalidate - the immediate
                // data holds the RKey that is supposed to be invalidated.

                //TODO: We need the QP handle (or context) so we can force an
                // error if we don't find a matching MW for the given RKEY.
                // We also need to change the receive status in this case to
                // ND_INVALIDATION_ERROR;
            }

            switch( wc.status )
            {
            case IB_WCS_SUCCESS:
                pResults[i]->Status = ND_SUCCESS;
                break;
            case IB_WCS_LOCAL_LEN_ERR:
                pResults[i]->Status = ND_LOCAL_LENGTH;
                break;
            case IB_WCS_LOCAL_OP_ERR:
            case IB_WCS_LOCAL_ACCESS_ERR:
            case IB_WCS_GENERAL_ERR:
            default:
                pResults[i]->Status = ND_INTERNAL_ERROR;
                break;
            case IB_WCS_LOCAL_PROTECTION_ERR:
            case IB_WCS_MEM_WINDOW_BIND_ERR:
                pResults[i]->Status = ND_ACCESS_VIOLATION;
                break;
            case IB_WCS_WR_FLUSHED_ERR:
                pResults[i]->Status = ND_CANCELED;
                break;
            case IB_WCS_REM_INVALID_REQ_ERR:
                pResults[i]->Status = ND_BUFFER_OVERFLOW;
                break;
            case IB_WCS_REM_ACCESS_ERR:
            case IB_WCS_REM_OP_ERR:
            case IB_WCS_BAD_RESP_ERR:
                pResults[i]->Status = ND_REMOTE_ERROR;
                break;
            case IB_WCS_RNR_RETRY_ERR:
            case IB_WCS_TIMEOUT_RETRY_ERR:
                pResults[i]->Status = ND_TIMEOUT;
                break;
            }
            i++;
            // leo
#if DBG
            {
                if (wc.wc_type == IB_WC_RECV)
                {    
                    if (!wc.status)
                    {
                        ++g.c_rcv_pkts;
                        g.c_rcv_bytes += wc.length;
                    }
                    else
                        ++g.c_rcv_pkts_err;
                }
                else
                {    
                    if (!wc.status)
                    {
                        ++g.c_snd_pkts;
                        g.c_snd_bytes += wc.length;
                    }
                    else
                        ++g.c_snd_pkts_err;
                }
            }
#endif
            continue;
        }
        return i;
    }

    HRESULT CCq::CreateCq(
        __in UINT32 nEntries )
    {
        ND_ENTER( ND_DBG_NDI );

        /* Clear the IOCTL buffer */
        ual_create_cq_ioctl_t cq_ioctl;
        RtlZeroMemory( &cq_ioctl, sizeof(cq_ioctl) );

        /* Pre call to the UVP library */
        ib_api_status_t status;
        if( m_pParent->m_uCa && m_pParent->m_Ifc.user_verbs.pre_create_cq )
        {
            status = m_pParent->m_Ifc.user_verbs.pre_create_cq(
                m_pParent->m_uCa,
                &nEntries,
                &cq_ioctl.in.umv_buf,
                (ib_cq_handle_t*)(ULONG_PTR)&m_uCq
                );
            if( status != IB_SUCCESS )
                goto done;
        }

        cq_ioctl.in.h_ca = m_pParent->m_hCa;
        cq_ioctl.in.size = nEntries;
        cq_ioctl.in.h_wait_obj = NULL;
        cq_ioctl.in.context = (ULONG_PTR)this;
        cq_ioctl.in.ev_notify = FALSE;

        DWORD BytesRet;
        BOOL fSuccess = DeviceIoControl(
            m_pParent->m_hSync,
            UAL_NDI_CREATE_CQ,
            &cq_ioctl.in,
            sizeof(cq_ioctl.in),
            &cq_ioctl.out,
            sizeof(cq_ioctl.out),
            &BytesRet,
            NULL
            );

        if( fSuccess != TRUE || BytesRet != sizeof(cq_ioctl.out) )
            status = IB_ERROR;
        else
            status = cq_ioctl.out.status;

        m_hCq = cq_ioctl.out.h_cq;

        /* Post uvp call */
        if( m_pParent->m_uCa && m_pParent->m_Ifc.user_verbs.post_create_cq )
        {
            m_pParent->m_Ifc.user_verbs.post_create_cq(
                m_pParent->m_uCa,
                status,
                cq_ioctl.out.size,
                (ib_cq_handle_t*)(ULONG_PTR)&m_uCq,
                &cq_ioctl.out.umv_buf );
        }

done:
        switch( status )
        {
        case IB_INVALID_CQ_SIZE:
            return ND_INVALID_PARAMETER;

        case IB_INSUFFICIENT_RESOURCES:
            return ND_INSUFFICIENT_RESOURCES;

        case IB_INSUFFICIENT_MEMORY:
            return ND_NO_MEMORY;

        case IB_SUCCESS:
            return S_OK;

        default:
            return ND_UNSUCCESSFUL;
        }
    }

    void CCq::CloseCq(void)
    {
        ND_ENTER( ND_DBG_NDI );

        ib_api_status_t			status;

        if( m_uCq && m_pParent->m_Ifc.user_verbs.pre_destroy_cq )
        {
            /* Pre call to the UVP library */
            status = m_pParent->m_Ifc.user_verbs.pre_destroy_cq( m_uCq );
            if( status != IB_SUCCESS )
                return;
        }

        DWORD BytesRet;
        BOOL fSuccess = DeviceIoControl(
            m_pParent->m_hSync,
            UAL_DESTROY_CQ,
            &m_hCq,
            sizeof(m_hCq),
            &status,
            sizeof(status),
            &BytesRet,
            NULL
            );

        if( fSuccess != TRUE || BytesRet != sizeof(status) )
            status = IB_ERROR;

        if( m_uCq && m_pParent->m_Ifc.user_verbs.post_destroy_cq )
            m_pParent->m_Ifc.user_verbs.post_destroy_cq( m_uCq, status );
    }

} // namespace
