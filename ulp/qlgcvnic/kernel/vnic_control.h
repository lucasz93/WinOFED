/*
 * Copyright (c) 2007 QLogic Corporation.  All rights reserved.
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

#ifndef _VNIC_CONTROL_H_
#define _VNIC_CONTROL_H_

#include "vnic_controlpkt.h"
#include "vnic_util.h"

typedef enum {
	TIMER_IDLE,
	TIMER_ACTIVE,
	TIMER_EXPIRED
}timerstate_t;

typedef struct Control {
	struct _viport       *p_viport;
	struct ControlConfig *p_conf;
	IbRegion_t           region;
	IbQp_t              qp;
	uint8_t              *pLocalStorage;
	uint16_t             majVer;
	uint16_t			 minVer;
	Inic_LanSwitchAttributes_t lanSwitch;
	SendIo_t             sendIo;
	RecvIo_t             *pRecvIos;

	timerstate_t		timerstate;
	cl_timer_t			timer;
	uint8_t				reqRetryCounter;
	uint8_t				seqNum;
	uint32_t			reqOutstanding;
	uint32_t			rspExpected;
	RecvIo_t			*pResponse;
	RecvIo_t			*pInfo;
	LIST_ENTRY			failureList;
	KSPIN_LOCK			ioLock;

#ifdef VNIC_STATISTIC
	struct {
		uint64_t     requestTime; /* Intermediate value */
		uint64_t     responseTime;
		uint32_t     responseNum;
		uint64_t     responseMax;
		uint64_t     responseMin;
		uint32_t     timeoutNum;
	} statistics;
#endif /* VNIC_STATISTIC */
} Control_t;

void
control_construct(
	IN		Control_t			*pControl,
	IN		struct _viport		*pViport );

ib_api_status_t control_init(Control_t *pControl, struct _viport *pViport,
                     struct ControlConfig *p_conf, uint64_t guid);
void    control_cleanup(Control_t *pControl);
void    control_processAsync(Control_t *pControl);
ib_api_status_t control_initInicReq(Control_t *pControl);
BOOLEAN control_initInicRsp(Control_t *pControl, uint32_t *pFeatures,
		            uint8_t *pMacAddress, uint16_t *pNumAddrs, uint16_t *pVlan);
ib_api_status_t control_configDataPathReq(Control_t *pControl, uint64_t pathId,
                                  struct Inic_RecvPoolConfig *pHost,
				  struct Inic_RecvPoolConfig *pEioc);
BOOLEAN control_configDataPathRsp(Control_t *pControl,
                                  struct Inic_RecvPoolConfig *pHost,
				  struct Inic_RecvPoolConfig *pEioc,
                                  struct Inic_RecvPoolConfig *pMaxHost,
				  struct Inic_RecvPoolConfig *pMaxEioc,
                                  struct Inic_RecvPoolConfig *pMinHost,
				  struct Inic_RecvPoolConfig *pMinEioc);
ib_api_status_t control_exchangePoolsReq(Control_t *pControl, uint64_t addr, uint32_t rkey);
BOOLEAN control_exchangePoolsRsp(Control_t *pControl, uint64_t *pAddr,
		                 uint32_t *pRkey);
ib_api_status_t control_configLinkReq(Control_t *pControl, uint8_t flags, uint16_t mtu);
BOOLEAN control_configLinkRsp(Control_t *pControl, uint8_t *pFlags, uint16_t *pMtu);
ib_api_status_t control_configAddrsReq(Control_t *pControl, Inic_AddressOp_t *pAddrs,
			       uint16_t num, int32_t *pAddrQueryDone);
BOOLEAN control_configAddrsRsp(Control_t *pControl);
ib_api_status_t control_reportStatisticsReq(Control_t *pControl);
BOOLEAN control_reportStatisticsRsp(Control_t *pControl,
				    struct Inic_CmdReportStatisticsRsp *pStats);
ib_api_status_t control_resetReq( Control_t *pControl );
BOOLEAN control_resetRsp(Control_t *pControl);
ib_api_status_t control_heartbeatReq(Control_t *pControl, uint32_t hbInterval);
BOOLEAN control_heartbeatRsp(Control_t *pControl);

#define control_packet(pIo) (Inic_ControlPacket_t *)(LONG_PTR)((pIo)->dsList.vaddr )
#define control_lastReq(pControl) control_packet(&(pControl)->sendIo)
#define control_features(pControl) (pControl)->featuresSupported
#define control_getMacAddress(pControl,addr) \
        memcpy(addr,(pControl)->lanSwitch.hwMacAddress,MAC_ADDR_LEN)

#endif /* _VNIC_CONTROL_H_ */
