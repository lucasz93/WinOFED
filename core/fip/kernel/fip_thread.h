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
    fip_thread.h

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/


#pragma once

const INT MAX_SLEEP = 1000*1000; // The maximum time our thread will sleep


class FipWorkerThread {
public:


    ~FipWorkerThread()
    {
        ASSERT(m_ThreadExit == true);
    }


    NTSTATUS Init();

    VOID Shutdown();

    VOID ThreadFunction();

    VOID AddPnpMessage(ib_pnp_rec_t *p_pnp_rec);

    VOID AddMulticastMessage(FipThreadMessage *pFipThreadMessage);
   
    VOID HandlePnpMessage(
        PDEVICE_OBJECT pDeviceObj,
        ib_pnp_rec_t *p_pnp_rec
        );

    VOID HandleMutlicastMessage(McastMessageData *pMcastMessageData);

    VOID RemoveThreadMessage(FipThreadMessage *pFipThreadMessage);

    VOID FlushMessages();

    VOID AddThreadMessage(FipThreadMessage *pFipThreadMessage);


private:

    VOID AddPort(
        PDEVICE_OBJECT pDeviceObj,
        ib_net64_t guid
        );

    VOID RemovePort(ib_net64_t guid);


    

    FipPort m_ports[MAX_PORTS];



    KEVENT                  m_ThreadWaitEvent;
    bool                    m_ThreadExit;
    PVOID                   m_ThreadObject;


    LinkedList              m_PnpMessages;
    CSpinLock               m_PnpMessagesLock;
    bool                    m_ShutDown;


};


extern FipWorkerThread* g_pFipWorkerThread;

