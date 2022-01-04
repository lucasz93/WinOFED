/*
 * 
 * Copyright (c) 2011-2012 Mellanox Technologies. All rights reserved.
 * 
 * This software is licensed under the OpenFabrics BSD license below:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *  conditions are met:
 *
 *       - Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *       - Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

/*


Module Name:
    fip_thread.cpp

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/
#include "precomp.h"


#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif

#include "fip_thread.tmh"
#endif


FipWorkerThread *g_pFipWorkerThread;


VOID FipThread(VOID *pContext)
{
    FipWorkerThread *pFipWorkerThread = (FipWorkerThread*)pContext;
    pFipWorkerThread->ThreadFunction();
}


NTSTATUS FipWorkerThread::Init()
{
    
    m_ShutDown = false;
    NTSTATUS Status = STATUS_SUCCESS;
    m_ThreadExit = false;

    KeInitializeEvent(&m_ThreadWaitEvent, SynchronizationEvent, FALSE);

    m_PnpMessages.Init();

    m_ThreadObject = GuCreateThread(::FipThread, this);
    if (m_ThreadObject == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, ("CreateSystemThread failed\n"));
        Status = STATUS_THREAD_NOT_IN_PROCESS;
        goto Cleanup;
    }

Cleanup:
    return Status;
}


VOID FipWorkerThread::Shutdown()
{

    NTSTATUS Status;
    int i;
    m_ShutDown = true;

    m_ThreadExit = true;
    KeSetEvent(&m_ThreadWaitEvent, IO_NO_INCREMENT, FALSE);
    Status = KeWaitForSingleObject(
            m_ThreadObject,
            Executive,
            KernelMode,
            FALSE,
            NULL
            );
    ASSERT(Status == STATUS_SUCCESS);

    // The worker thread is now dead. Due to IBAL bugs, shutdown might be called before we get a notification about all ports being removed.
    // we now go over all ports and close them ...
    for (i=0; i < MAX_PORTS; i++) {
        if (m_ports[i].m_PortGuid != 0) {           
           RemovePort(m_ports[i].m_PortGuid);
        }
    }
    
}


VOID FipWorkerThread::AddPnpMessage(ib_pnp_rec_t *p_pnp_rec)
{
    // TODO: Since here we know which ports we have, Make sure you allocate enough messages for the port notification. (tzachid 24/11/2011)
    FipThreadMessage *pFipThreadMessage = NEW FipThreadMessage;
    if (pFipThreadMessage == NULL) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, ("new FipThreadMessage failed \n"));
        ASSERT(FALSE);
        // TODO: Make sure that you mark the ports with error.
        return;        
    }
    
    pFipThreadMessage->Data.ib_pnp_rec = *p_pnp_rec;
    pFipThreadMessage->MessageType = MESSAGE_TYPE_PNP;
    pFipThreadMessage->DeleteAfterUse = true;

    AddThreadMessage(pFipThreadMessage);

}

// pFipThreadMessage have been prealloacted, and we don't have to free them ourselves
VOID FipWorkerThread::AddMulticastMessage(FipThreadMessage *pFipThreadMessage)
{
    pFipThreadMessage->MessageType = MESSAGE_TYPE_MULTICAST;
    pFipThreadMessage->DeleteAfterUse = false;
    AddThreadMessage(pFipThreadMessage);
}


VOID FipWorkerThread::AddThreadMessage(FipThreadMessage *pFipThreadMessage)
{
    m_PnpMessagesLock.Lock();
    m_PnpMessages.InsertTailList(&pFipThreadMessage->ListEntry);
    m_PnpMessagesLock.Unlock();
    KeSetEvent(&m_ThreadWaitEvent, 0, FALSE);
}


VOID FipWorkerThread::RemoveThreadMessage(FipThreadMessage *pFipThreadMessage)
{
    m_PnpMessagesLock.Lock();
    m_PnpMessages.RemoveEntryList(&pFipThreadMessage->ListEntry);
    InitializeListHead(&pFipThreadMessage->ListEntry);
    m_PnpMessagesLock.Unlock();
}


VOID FipWorkerThread::FlushMessages()
{

    FipThreadMessage ThreadMessage;
    NTSTATUS Status;

    KeInitializeEvent(&ThreadMessage.Data.ThreadWaitEvent, SynchronizationEvent, FALSE);
    ThreadMessage.MessageType = MESSAGE_TYPE_FLUSH;
    ThreadMessage.DeleteAfterUse = false;
    AddThreadMessage(&ThreadMessage);

    Status = KeWaitForSingleObject(&ThreadMessage.Data.ThreadWaitEvent, Executive, KernelMode, FALSE, NULL);

    ASSERT(Status == STATUS_SUCCESS);


}


VOID FipWorkerThread::HandlePnpMessage(
    PDEVICE_OBJECT pDeviceObj,
    ib_pnp_rec_t *p_pnp_rec
    )
{

    switch( p_pnp_rec->pnp_event ) {
        case  IB_PNP_PORT_ADD:
            AddPort(pDeviceObj, p_pnp_rec->guid);
            break;

        case  IB_PNP_PORT_REMOVE:
            RemovePort(p_pnp_rec->guid);
            break;

            break;
        default :

            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV ,
                "Received unhandled PnP event 0x%x (%s)\n",
                p_pnp_rec->pnp_event, ib_get_pnp_event_str(p_pnp_rec->pnp_event ) );
            
            ASSERT(FALSE);

    }

}


VOID FipWorkerThread::HandleMutlicastMessage(McastMessageData *pMcastMessageData)
{
    Vnic *pVnicClass = NULL;
    FipPort *pPortClass = NULL;

    switch (pMcastMessageData->ClassType) {
    case PORT_CLASS:
        pPortClass = (FipPort *)pMcastMessageData->pFipClass;
        pPortClass->JoinMcastCallBack(pMcastMessageData);
        break;

    case VNIC_CLASS:
        pVnicClass = (Vnic *)pMcastMessageData->pFipClass;
        pVnicClass->JoinMcastCallBack(pMcastMessageData);
        break;

    default:
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Class Type (%s) isn't supported\n", GetClassTypeStr(pMcastMessageData->ClassType));
        ASSERT(FALSE);
        break;
    }
}



VOID FipWorkerThread::AddPort(
    PDEVICE_OBJECT pDeviceObj,
    ib_net64_t guid
    )
{

    int i;
    int FreeLocation = -1;
    // First find out if this port already exists
    for (i=0; i < MAX_PORTS; i++) {
        if (m_ports[i].m_PortGuid == guid) {
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Port Already exists - 0x%I64x\n", guid);
            ASSERT(FALSE);
            return;
        }
        if ((FreeLocation == -1) && (m_ports[i].m_PortGuid == 0)) {
            FreeLocation = i;
        }
    }

    if (FreeLocation == -1) {
        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, ("No free place to add the new port\n"));
        ASSERT(FALSE);
        return;
    }

    // Mark the port as used 
    m_ports[FreeLocation].PreInit();
    m_ports[FreeLocation].m_PortGuid = guid;
    m_ports[FreeLocation].Init(pDeviceObj, &m_ThreadWaitEvent);
}


// TODO: we are still having some race with notifications here
VOID FipWorkerThread::RemovePort(ib_net64_t guid)
{

    int i;
    // First find out if this port already exists
    for (i=0; i < MAX_PORTS; i++) {
        if (m_ports[i].m_PortGuid == guid) {           
            FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Port is being removed - 0x%I64x\n", guid);
            m_ports[i].Shutdown();
            m_ports[i].m_PortGuid = 0;
            return;
        }
    }

    // Port was not found, we have some confusion
    FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Port does not exist - 0x%I64x\n", guid);
    ASSERT(FALSE);

}


VOID FipWorkerThread::ThreadFunction()
{
        NTSTATUS Status = STATUS_SUCCESS;
    
        FIP_PRINT(TRACE_LEVEL_INFORMATION, FIP_DBG_DRV, ("RingThread called \n"));
        int i;
        DWORD SleepTimeMS = MAX_SLEEP;
    
        for(; ;) {

            for (i=0; i < MAX_PORTS; i++) {
                if (m_ports[i].m_PortGuid != 0) {
                    DWORD PortTime = m_ports[i].NextCallTime();
                    SleepTimeMS = min(SleepTimeMS, PortTime);
                }
            }

            LARGE_INTEGER  Timeout = TimeFromLong(SleepTimeMS * 10000);

            
            ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
            Status = KeWaitForSingleObject(
                &m_ThreadWaitEvent,
                Executive,
                KernelMode,
                FALSE,
                &Timeout
                );
                                    
            ASSERT(Status == STATUS_SUCCESS || Status == STATUS_TIMEOUT);
            FIP_PRINT(TRACE_LEVEL_VERBOSE, FIP_DBG_DRV, "Thread wake up Status = %d\n", Status);
   
            if (m_ThreadExit) {
                // the driver going down
                break;
            }

            // Startr doing the real work...
            
            m_PnpMessagesLock.Lock();
            while (m_PnpMessages.Size() > 0) {
                LIST_ENTRY* curr_entry = m_PnpMessages.RemoveHeadList();
                InitializeListHead(curr_entry);
                m_PnpMessagesLock.Unlock();


                FipThreadMessage* pFipThreadMessage = CONTAINING_RECORD(curr_entry, FipThreadMessage, ListEntry);
                // Message type is being copied since after the set event, the pFipThreadMessage might not exist any more!!!
                MessageTypes MessageType = pFipThreadMessage->MessageType;
                bool DeleteAfterUse = pFipThreadMessage->DeleteAfterUse;

                switch (MessageType) {
                    case MESSAGE_TYPE_PNP :
                        HandlePnpMessage(
                            pFipThreadMessage->pDeviceObj,
                            &pFipThreadMessage->Data.ib_pnp_rec);
                        break;

                    case MESSAGE_TYPE_MULTICAST :
                        HandleMutlicastMessage(&pFipThreadMessage->Data.Mcast);
                        break;

                    case MESSAGE_TYPE_FLUSH :
                        KeSetEvent(&pFipThreadMessage->Data.ThreadWaitEvent, IO_NO_INCREMENT, FALSE);
                        break;

                    default:
                        FIP_PRINT(TRACE_LEVEL_ERROR, FIP_DBG_DRV, "Unknown Message Type %d \n", pFipThreadMessage->MessageType);
                        ASSERT(FALSE);

                };

                if (DeleteAfterUse) {
                    delete pFipThreadMessage;
                }
                ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
                
                m_PnpMessagesLock.Lock();
                
            }
            
            m_PnpMessagesLock.Unlock();

            
            for (i=0; i < MAX_PORTS; i++) {
                if (m_ports[i].m_PortGuid != 0) {
                    m_ports[i].PortSM();
                }
            }
                
        }
    
        PsTerminateSystemThread(STATUS_SUCCESS);

}


