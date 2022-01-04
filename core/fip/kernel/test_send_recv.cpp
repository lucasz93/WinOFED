/*++

Copyright (c) 2005-2012 Mellanox Technologies. All rights reserved.

Module Name:
    send_recv

Abstract:
    This Class is used to Send and Recieve IB messages using UD QP.
    Please note that this class is support single-threaded.
    To change it to support multi-threaded you have to add some locks.
    TO use this class you have to use this way:
        Init(...)
        Send(...) & Recv(...)
        ShutDown(...)

Revision History:

Author:
    Sharon Cohen

Notes:

--*/

#define TEST_MSG    "THIS IS TEST MESSAGE !!!!"
#define NUM_OF_ITER 100

#include "precomp.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "test_send_recv.tmh"
#endif


ib_api_status_t SetAndCreateAV(
                                    IN  ib_pd_handle_t h_pd,
                                    IN  DWORD PortNumber,
                                    IN  ib_net16_t dlid,
                                    OUT ib_av_handle_t *ph_av )
{
    ib_api_status_t ib_status = IB_SUCCESS;
    ib_av_attr_t    av_attr;

    memset(&av_attr, 0, sizeof(av_attr));
    av_attr.port_num = (uint8_t)PortNumber;
    av_attr.grh.hop_limit = 0;
    av_attr.path_bits = 0;
    av_attr.static_rate = IB_PATH_RECORD_RATE_10_GBS;
    av_attr.dlid = dlid;
    av_attr.grh_valid = FALSE;

    ib_status = ib_create_av(h_pd, &av_attr, ph_av);
    if(ib_status != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_create_av failed. statue=0x%x\n",ib_status);
        goto av_err;
    }
    return ib_status;

av_err:
    return ib_status;

}


DWORD PortNumber = 2;


NTSTATUS
test_recv(
    SendRecv *tst_class
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    RcvMsg *rcvmsg = NULL;
    LARGE_INTEGER WaitTimeOut;
    ULONG i = 0, j = 0;
    
    while( (i < NUM_OF_ITER) && (j <= 1000) )
    {
        // Wait for event to occure..
        WaitTimeOut =  TimeFromLong(10000);
        status = KeWaitForSingleObject(&tst_class->m_MsgArrivedEvent, Executive, KernelMode , FALSE, &WaitTimeOut);
        if (status != STATUS_SUCCESS)
        {
            if (status == STATUS_TIMEOUT)
            {
                j++;
                status = STATUS_SUCCESS;
                continue;
            }
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "KeWaitForSingleObject failed. statue=0x%x\n", status);
            status = STATUS_ADAPTER_HARDWARE_ERROR;
            goto recv_err;
        }

        // Recv all the msg
        status = tst_class->Recv(&rcvmsg);
        while( status == STATUS_SUCCESS)
        {
            if (i == (NUM_OF_ITER - 1))
            {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "!!!!!!!!!!!!!! Recv Succeeded(%u) !!!!!!!!!!!!!!\n", i);
                rcvmsg->PrintDbgData();
            }
            i++;
            j++;

            tst_class->ReturnBuffer(rcvmsg);
            rcvmsg = NULL;

            status = tst_class->Recv(&rcvmsg);
        }

        if (status != STATUS_SUCCESS)
        {
            if (status == STATUS_PIPE_EMPTY)
            {
                status = STATUS_SUCCESS;
                break;
            }
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Recv failed(%d). statue=0x%x\n", i, status);
            status = STATUS_ADAPTER_HARDWARE_ERROR;
            goto recv_err;
        }
    }

recv_err:
    return status;
}


NTSTATUS
test_send(
    SendRecv *tst_class
     )
{
    NTSTATUS status = STATUS_SUCCESS;
    ib_api_status_t ib_status = IB_SUCCESS;
    CHAR msg[MAX_UD_MSG];
    ib_av_handle_t h_av;
    ib_net16_t dlid = 0x0200; /* sw493 sends to sw494 */
    ib_net32_t remote_qp = 0x48000000;/* IPoIB - 0x48 */
    LARGE_INTEGER SleepTime;
    
    
    // Prepare the msg
    memset(msg, 0, sizeof(msg));
    memcpy(msg, TEST_MSG, sizeof(TEST_MSG));

    // Create AV Handle
    ib_status = SetAndCreateAV(tst_class->GetHandlePD(), PortNumber, dlid, &h_av);
    if(ib_status != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "SetAndCreateAV failed. statue=0x%x\n",ib_status);
        status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto send_err;
    }

    // Test Send only
    for(ULONG i = 0; i < NUM_OF_ITER; i++)
    {
        ULONG Retry = 0;
        
        // Send the msg
        status = tst_class->Send(h_av, remote_qp, msg, sizeof(TEST_MSG));
        if (status != STATUS_SUCCESS)
        {
            Retry =  (status == STATUS_PIPE_EMPTY) ? (Retry + 1) : Retry;
            SleepTime =  TimeFromLong(10000);
            KeDelayExecutionThread(KernelMode, FALSE, &SleepTime);
            if (Retry > 10)
            {
                FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Send failed(%d). statue=0x%x\n", i, status);
                status = STATUS_ADAPTER_HARDWARE_ERROR;
                goto des_av_err;
            }
        }
        if (i == (NUM_OF_ITER - 1))
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "!!!!!!!!!!!!!! Send Succeeded(%u) !!!!!!!!!!!!!!\n", i);
        }
        
        //        SleepTime =  TimeFromLong(10000);
        //        KeDelayExecutionThread(KernelMode, FALSE, &SleepTime);
    }

des_av_err:
    ib_status = ib_destroy_av(h_av);
    if(ib_status != IB_SUCCESS)
    {        
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_destroy_av failed. statue=0x%x\n",ib_status);
        status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto send_err;
    }

send_err:
    return status;
}


NTSTATUS
test_common(
    ib_al_handle_t h_al,
    ib_net64_t *ca_guid_array,
    ib_net64_t port_guid
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    SendRecv tst_class;
    ib_net32_t qkey = 0x1234;/* IPoIB QKEY given as default from OpenSM */
    uint16_t PkeyIndex = 0;
    ULONG SendBuffSize = 100;
    ULONG RecvBuffSize = 100;
    static int tmp = 1;
    
    RecvBuffSize = 100; // Should 'play' with this size to check bugs
    SendBuffSize = 100; // Should 'play' with this size to check bugs
    
    status = tst_class.Init(h_al,port_guid, SendBuffSize, RecvBuffSize, qkey, PkeyIndex);
    if (status != STATUS_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Init failed. statue=0x%x\n",status);
        status = STATUS_ADAPTER_HARDWARE_ERROR;
        goto tst_snd_err;
    }

    if (tmp == 1)
    { // Send
        status = test_send(&tst_class);
    } 
    else
    { //Recv
        status = test_recv(&tst_class);
    }

tst_snd_err:
    tst_class.ShutDown();
    return status;
}


void test_send_recv( void *ctx )
{
    ib_api_status_t ib_status = IB_SUCCESS;
    ib_al_handle_t h_al;
    LARGE_INTEGER SleepTime;
    size_t guid_count = 0;
    ib_net64_t *ca_guid_array = NULL;
    
    ctx = NULL; // This parameter is not in use
    
    SleepTime.HighPart = 0xffffffff;
    SleepTime.LowPart =  0xffffffff ^ (30 * 10000000);
    KeDelayExecutionThread(KernelMode, FALSE, &SleepTime);  /* Sleep for 20 Sec. until the port is up and ready */
    
    ib_status = ib_open_al(&h_al);
    if(ib_status != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_open_al failed. statue=0x%x\n",ib_status);
        goto tst_err;
    }
    
    ib_status = ib_get_ca_guids(h_al, NULL, &guid_count);
    if (ib_status != IB_INSUFFICIENT_MEMORY)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_get_ca_guids failed. statue=0x%x\n",ib_status);
        goto tst_err;
    }
    
    ca_guid_array = NEW ib_net64_t[guid_count];
    ib_status = ib_get_ca_guids(h_al, ca_guid_array, &guid_count);
    if (ib_status != IB_SUCCESS)
    {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_get_ca_guids failed. statue=0x%x\n",ib_status);
        goto tst_err;
    }

    // Test create-destroy at send
    for(ULONG i = 0; i < NUM_OF_ITER; i++)
    {
        if (test_common(h_al, ca_guid_array, ca_guid_array[0] + ((ib_net64_t)PortNumber << 56)) != STATUS_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "test_send failed.n");
            goto tst_err;
        }
    }

    // Test Recv
    for(ULONG i = 0; i < NUM_OF_ITER; i++)
    {
        if (test_common(h_al, ca_guid_array, ca_guid_array[0] + ((ib_net64_t)PortNumber << 56)) != STATUS_SUCCESS)
        {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "test_send failed.\n");
            goto tst_err;
        }
    }
    

tst_err:
    delete []ca_guid_array;
    ib_status = ib_close_al(h_al);
    if(ib_status != IB_SUCCESS)
    {        
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "ib_close_al failed. statue=0x%x\n",ib_status);
    }
    
    PsTerminateSystemThread(STATUS_SUCCESS);
    //return IB_SUCCESS;
}


NTSTATUS 
test_send_recv_main( VOID )
{
    NTSTATUS rc = STATUS_SUCCESS;
    OBJECT_ATTRIBUTES attr;
    HANDLE ThreadHandle;
    PVOID ThreadObject;

    /* Create a NEW thread, storing both the handle and thread id. */
    InitializeObjectAttributes( &attr, NULL, OBJ_KERNEL_HANDLE, NULL, NULL );
    
    rc = PsCreateSystemThread(
        &ThreadHandle, 
        THREAD_ALL_ACCESS,
        &attr,
        NULL,
        NULL,
        test_send_recv,
        NULL
        );

    if (rc != STATUS_SUCCESS) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "PsCreateSystemThread failed rc = 0x%x\n", rc);
        // The thread wasn't created so we should remove the refferance
        //rc = NtStatusToWsaError(rc);
        goto cleanup_err;
    }

    // Convert the thread into a handle
    rc = ObReferenceObjectByHandle(
          ThreadHandle,
          THREAD_ALL_ACCESS,
          NULL,
          KernelMode,
          &ThreadObject,
          NULL
          );
    ASSERT(rc == STATUS_SUCCESS); // According to MSDN, must succeed if I set the params
    
    rc = ZwClose(ThreadHandle);
    ThreadObject = NULL; // Will be delated when the callback thread is deleted

    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_SEND_RECV, "Thread Started !!!!\n");
    return rc;

cleanup_err:
    return rc;
}


