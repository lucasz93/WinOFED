/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mthca_catas.tmh"
#endif

enum {
	MTHCA_CATAS_POLL_INTERVAL	= 5 * HZ,

	MTHCA_CATAS_TYPE_INTERNAL	= 0,
	MTHCA_CATAS_TYPE_UPLINK		= 3,
	MTHCA_CATAS_TYPE_DDR		= 4,
	MTHCA_CATAS_TYPE_PARITY		= 5,
};

static spinlock_t catas_lock;

static void handle_catas(struct mthca_dev *dev)
{
	struct ib_event event;
	const char *type;
	int i;

	event.device = &dev->ib_dev;
	event.event  = IB_EVENT_DEVICE_FATAL;
	event.element.port_num = 0;

	ib_dispatch_event(&event);

	switch (_byteswap_ulong(readl(dev->catas_err.map)) >> 24) {
	case MTHCA_CATAS_TYPE_INTERNAL:
		type = "internal error";
		break;
	case MTHCA_CATAS_TYPE_UPLINK:
		type = "uplink bus error";
		break;
	case MTHCA_CATAS_TYPE_DDR:
		type = "DDR data error";
		break;
	case MTHCA_CATAS_TYPE_PARITY:
		type = "internal parity error";
		break;
	default:
		type = "unknown error";
		break;
	}

	HCA_PRINT(TRACE_LEVEL_ERROR  ,HCA_DBG_LOW  ,("Catastrophic error detected: %s\n", type));
	for (i = 0; i < (int)dev->catas_err.size; ++i)
		HCA_PRINT(TRACE_LEVEL_ERROR,HCA_DBG_LOW,("  buf[%02x]: %08x\n",
			  i, _byteswap_ulong(readl(dev->catas_err.map + i))));
}

static void poll_catas(struct mthca_dev *dev)
{
	int i;
	SPIN_LOCK_PREP(lh);

	for (i = 0; i < (int)dev->catas_err.size; ++i)
		if (readl(dev->catas_err.map + i)) {
			handle_catas(dev);
			return;
		}

	spin_lock_dpc(&catas_lock, &lh);
	if (!dev->catas_err.stop) {
		KeSetTimerEx( &dev->catas_err.timer, dev->catas_err.interval, 
			0, &dev->catas_err.timer_dpc );
	}
	spin_unlock_dpc(&lh);

	return;
}

static void  timer_dpc(
    IN struct _KDPC  *Dpc,
    IN PVOID  DeferredContext,
    IN PVOID  SystemArgument1,
    IN PVOID  SystemArgument2
    )
{
	struct mthca_dev *dev = (struct mthca_dev *)DeferredContext;
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	poll_catas( dev );
}


void mthca_start_catas_poll(struct mthca_dev *dev)
{
	u64 addr;

	dev->catas_err.stop = 0;
	dev->catas_err.map  = NULL;

	addr = pci_resource_start(dev, HCA_BAR_TYPE_HCR) +
		((pci_resource_len(dev, HCA_BAR_TYPE_HCR) - 1) &
		 dev->catas_err.addr);

	dev->catas_err.map = ioremap(addr, dev->catas_err.size * 4, &dev->catas_err.map_size );
	if (!dev->catas_err.map) {
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_LOW, ("couldn't map catastrophic error region "
			   "at 0x%I64x/0x%x\n", addr, dev->catas_err.size * 4));
		return;
	}

	spin_lock_init( &catas_lock );
	KeInitializeDpc(  &dev->catas_err.timer_dpc, timer_dpc, dev );
	KeInitializeTimer( &dev->catas_err.timer );
	dev->catas_err.interval.QuadPart  = (-10)* (__int64)MTHCA_CATAS_POLL_INTERVAL;
	KeSetTimerEx( &dev->catas_err.timer, dev->catas_err.interval, 
		0, &dev->catas_err.timer_dpc );
}

void mthca_stop_catas_poll(struct mthca_dev *dev)
{
	SPIN_LOCK_PREP(lh);
	
	spin_lock_irq(&catas_lock, &lh);
	dev->catas_err.stop = 1;
	spin_unlock_irq(&lh);

	KeCancelTimer(&dev->catas_err.timer);
	KeFlushQueuedDpcs();

	if (dev->catas_err.map) {
		iounmap(dev->catas_err.map, dev->catas_err.map_size);
	}
}
