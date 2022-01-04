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
    fip_messages.h

Abstract:
    This module contains miniport receive routines

Revision History:

Notes:

--*/


#pragma once

const int INITIAL_ARRAY_SIZE    = 4;
const int MAX_PORTS             = 64;

enum MessageTypes {
    MESSAGE_TYPE_PNP,
    MESSAGE_TYPE_MULTICAST,
    MESSAGE_TYPE_FLUSH
};


class FipPort;

struct McastMessageData {
    ClassType_t         ClassType;
    PVOID               pFipClass;

    ib_api_status_t     status;
    ib_net16_t          error_status;

    ib_member_rec_t     member_rec;
};


union MessagesData {
    ib_pnp_rec_t        ib_pnp_rec;
    McastMessageData    Mcast;
    KEVENT              ThreadWaitEvent;
};


struct FipThreadMessage {
    MessageTypes        MessageType;
    bool                DeleteAfterUse;
    LIST_ENTRY          ListEntry;
    MessagesData        Data;
    PDEVICE_OBJECT      pDeviceObj;
};


VOID
DetachMcast(
    bool m_McastTableConnected,
    SendRecv *pSendRecv,
    al_attach_handle_t *m_McastTableAattach
    );


VOID
DestroyMcast(
    bool                *pfMcastConnected,
    FipThreadMessage    *pMcastData,
    mcast_mgr_request_t **ppMcastMgr,
    class FipWorkerThread *pFipWorkerThread
    );



