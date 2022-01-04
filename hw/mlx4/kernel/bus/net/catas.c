/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include "mlx4.h"
#include "mlx4_debug.h"
#include "stat.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "catas.tmh"
#endif


enum {
	MLX4_CATAS_POLL_INTERVAL	= 5 * HZ,
};

static DEFINE_SPINLOCK(catas_lock);
static LIST_HEAD(catas_list);

int mlx4_dispatch_reset_event(struct ib_device *ibdev, enum ib_event_type type)
{
	unsigned long flags;
	UNUSED_PARAM(flags);
	struct ib_event event;
	struct ib_event_handler *handler;
	int num_notified_childs = 0;

	event.device = ibdev;
	event.event = type;

	spin_lock_irqsave(&ibdev->event_handler_lock, &flags);

	list_for_each_entry(handler, &ibdev->event_handler_list, list, struct ib_event_handler)
	{
		// notify only soft reset handlers
		if ( handler->flags & IB_IVH_RESET_CB )
			// notify only those, that are not yet notified
			if ( !(handler->flags & IB_IVH_NOTIFIED) ) {
				// notify only those that are ready to get the notification
				if ( handler->flags & IB_IVH_NOTIF_READY ) {
					// insure not to notify once more 
					handler->flags |= IB_IVH_NOTIFIED;
					handler->flags &= ~(IB_IVH_NOTIF_READY | 
						IB_IVH_RESET_D_PENDING | IB_IVH_RESET_C_PENDING);
					handler->handler(handler, &event);
					num_notified_childs++;
				}
				else {
					// pend the notification
					if (type == IB_EVENT_RESET_DRIVER) 
						handler->flags |= IB_IVH_RESET_D_PENDING;
					else 
						handler->flags |= IB_IVH_RESET_C_PENDING;
				}
			}
	}

	spin_unlock_irqrestore(&ibdev->event_handler_lock, flags);
	return num_notified_childs;
}

/**
 * get_event_handlers - return list of handlers of the device
 * @device:device
 * @tlist:list
 *
 * get_event_handlers() remove all the device event handlers and put them in 'tlist'
 */
static void get_event_handlers(struct ib_device *device, struct list_head *tlist)
{
	unsigned long flags;
	UNUSED_PARAM(flags);
	struct ib_event_handler *handler, *thandler;

	spin_lock_irqsave(&device->event_handler_lock, &flags);

	list_for_each_entry_safe(handler, thandler, &device->event_handler_list, 
		list, struct ib_event_handler, struct ib_event_handler)
	{
		// take out only reset callbacks
		if ( handler->flags & IB_IVH_RESET_CB ) {
			list_del( &handler->list );
			list_add_tail( &handler->list, tlist );
		}
	}

	spin_unlock_irqrestore(&device->event_handler_lock, flags);
}


static void dump_err_buf(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	u32 i;
    
    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: Internal error detected:\n", dev->pdev->name));
	st_et_event("Bus: Internal error detected:\n\n", dev->pdev->name);
    
	for (i = 0; i < priv->fw.catas_size; ++i) {
        MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,
                  ("  buf[%02x]: %08x\n",i , swab32(readl(priv->catas_err.map + i))));
		st_et_event("Bus:  buf[%02x]: %08x\n",i , swab32(readl(priv->catas_err.map + i)));
	}
}

static IO_WORKITEM_ROUTINE catas_reset_wi;
static VOID
catas_reset_wi(
	IN				DEVICE_OBJECT*				p_dev_obj,
	IN				PVOID						context)
{

    ASSERT(context != NULL);
	struct mlx4_dev * dev = (struct mlx4_dev *) context;
	UNUSED_PARAM(p_dev_obj);

	dump_err_buf(dev);

#if 0

    long do_reset;
    NTSTATUS status;



On a catastrophic error, we dont do a reset for the hw.
The reason is this:
for ethenet only cards reset will happen due to the fact that we dispatch the IB_EVENT_RESET_DRIVER driver.
For IB devices, we can not currently do a reset to IBAL. so a BSOD will happen in any case.

    
	do_reset = InterlockedCompareExchange(&dev->reset_pending, 1, 0);
	if (do_reset == 0) {
		if ( !mlx4_is_slave(dev) ) {
			status = mlx4_reset(dev);
			if ( !NT_SUCCESS( status ) ) {
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                    ("%s: Failed to reset HCA, aborting.(status %#x)\n", dev->pdev->name, status));
			}
			else            
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,
                    ("%s: catas_reset_wi: HCA has been reset.\n", dev->pdev->name));
		}
		
		dev->flags |= MLX4_FLAG_RESET_DRIVER;	// bar the device
	}
#endif
	mlx4_dispatch_event(dev, (mlx4_dev_event)MLX4_EVENT_TYPE_LOCAL_CATAS_ERROR, 0);
	if (dev->pdev->ib_dev) {
		
		st_et_event("Bus: catas_reset_wi sent IB_EVENT_RESET_DRIVER\n");
		mlx4_dispatch_reset_event(dev->pdev->ib_dev, IB_EVENT_RESET_DRIVER);
	}
}

/* polling on DISPATCH_LEVEL */
static void poll_catas(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	if (readl(priv->catas_err.map)) {
		
		debug_busy_wait(dev);
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Detected catastrophic error on mdev %p\n", 
			dev->pdev->name, dev));
		IoQueueWorkItem( priv->catas_err.catas_work, (PIO_WORKITEM_ROUTINE)catas_reset_wi, DelayedWorkQueue, dev );
	} else {
		spin_lock_dpc(&catas_lock);
		if (!priv->catas_err.stop) {
			KeSetTimerEx( &priv->catas_err.timer, priv->catas_err.interval, 
				0, &priv->catas_err.timer_dpc );
		}
		spin_unlock_dpc(&catas_lock);
	}
}
#ifdef NTDDI_WIN8
static KDEFERRED_ROUTINE timer_dpc;
#endif
static void  timer_dpc(
	IN struct _KDPC  *Dpc,
	IN PVOID  DeferredContext,
	IN PVOID  SystemArgument1,
	IN PVOID  SystemArgument2
	)
{
	struct mlx4_dev *dev = (struct mlx4_dev *)DeferredContext;
	UNREFERENCED_PARAMETER(Dpc);
	UNREFERENCED_PARAMETER(SystemArgument1);
	UNREFERENCED_PARAMETER(SystemArgument2);
	poll_catas( dev );
}

int mlx4_start_catas_poll(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	u64 addr;
	int err;

	priv->catas_err.map = NULL;

	addr = pci_resource_start(dev->pdev, priv->fw.catas_bar) +
		priv->fw.catas_offset;

	priv->catas_err.map = (u32*)ioremap(addr, priv->fw.catas_size * 4, MmNonCached);
	if (!priv->catas_err.map) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to map internal error buffer at 0x%I64x\n",
			  dev->pdev->name, addr));
		err = -ENOMEM;
		goto err_map;
	}
	
	priv->catas_err.catas_work = IoAllocateWorkItem( dev->pdev->p_self_do );
	if (!priv->catas_err.catas_work) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "%s: Failed to allocate work item from polling thread\n", dev->pdev->name));
		err = -EFAULT;
		goto err_alloc;
	}

	priv->catas_err.stop = 0;
	spin_lock_init( &catas_lock );
	KeInitializeDpc(  &priv->catas_err.timer_dpc, timer_dpc, dev );
	KeInitializeTimer( &priv->catas_err.timer );
	priv->catas_err.interval.QuadPart  = (-10)* (__int64)MLX4_CATAS_POLL_INTERVAL;
	KeSetTimerEx( &priv->catas_err.timer, priv->catas_err.interval, 
		0, &priv->catas_err.timer_dpc );
	return 0;


err_alloc:
	iounmap(priv->catas_err.map, priv->fw.catas_size * 4);
err_map:
	return err;
}

void mlx4_stop_catas_poll(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);

	spin_lock_irq(&catas_lock);
	if (priv->catas_err.stop) {
		spin_unlock_irq(&catas_lock);
		return;
	}
	priv->catas_err.stop = 1;
	spin_unlock_irq(&catas_lock);

	KeCancelTimer(&priv->catas_err.timer);
	KeFlushQueuedDpcs();
	if (priv->catas_err.map)
		iounmap(priv->catas_err.map, priv->fw.catas_size * 4);

	if (priv->catas_err.catas_work) 
		IoFreeWorkItem( priv->catas_err.catas_work );
}

static int wait4reset(struct ib_event_handler *event_handler)
{
	int n_not_ready = 0;
	unsigned long flags;
	UNUSED_PARAM(flags);
	struct ib_event_handler *handler;
	struct ib_device *ibdev = event_handler->device;

	spin_lock_irqsave(&ibdev->event_handler_lock, &flags);

	// mark this handler (=client) reset-ready
	event_handler->flags |= IB_IVH_RESET_READY;

	// check the number of still not ready client
	
	list_for_each_entry(handler, &ibdev->event_handler_list, list, struct ib_event_handler)
		if ( (handler->flags & IB_IVH_RESET_CB) && (handler->flags & IB_IVH_NOTIFIED) )
			if ( !(handler->flags & IB_IVH_RESET_READY) ) 
				++n_not_ready;
	
	spin_unlock_irqrestore(&ibdev->event_handler_lock, flags);

	st_et_event("Bus: wait4reset called for handler %p and returned %d\n", event_handler, n_not_ready);
	return n_not_ready;
}

int mlx4_reset_ready( struct ib_event_handler *event_handler )
{
	unsigned long flags;
	UNUSED_PARAM(flags);
	struct ib_device *ibdev = event_handler->device;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	
	spin_lock_irqsave(&ibdev->event_handler_lock, &flags);
	event_handler->flags |= IB_IVH_NOTIF_READY;
	spin_unlock_irqrestore(&ibdev->event_handler_lock, flags);
	if (event_handler->flags & IB_IVH_RESET_D_PENDING) {
		st_et_event("Bus: mlx4_reset_ready sent IB_EVENT_RESET_DRIVER\n");
		mlx4_dispatch_reset_event(ibdev, IB_EVENT_RESET_DRIVER);
	}
	else
	if (event_handler->flags & IB_IVH_RESET_C_PENDING) {
		st_et_event("Bus: mlx4_reset_ready sent IB_EVENT_RESET_CLIENT\n");
		mlx4_dispatch_reset_event(ibdev, IB_EVENT_RESET_CLIENT);
	}
	return 0;
}

int mlx4_reset_execute( struct ib_event_handler *event_handler )
{
	int err = 0;
	struct ib_event event;
	struct list_head tlist;
	struct ib_event_handler *handler, *thandler;
	struct ib_device *ibdev = event_handler->device;
	struct mlx4_dev *dev = ibdev->dma_device;
	struct pci_dev *pdev = dev->pdev;

	st_et_event("Bus: mlx4_reset_execute started for handler %p\n", event_handler);

	// mark client as "ready for reset" and check whether we can do reset
	if (wait4reset(event_handler)) {
		return 0;
	}

	// get old handler list	
	INIT_LIST_HEAD(&tlist);
	get_event_handlers(ibdev, &tlist);
	
	if (!pdev->is_reset_prohibited) {
		
		// fully bar the device
		dev->flags |= MLX4_FLAG_RESET_STARTED;

		if ( mlx4_is_slave(dev) && (dev->flags & MLX4_FLAG_RESET_4_RMV))
		{ // prepare for device remove
			mlx4_dev_remove(dev);
			event.event = IB_EVENT_RESET_FAILED;
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\n HCA remove finished. Notifying the clients ... \n\n"));
		} else {
		    if (mlx4_is_mfunc(dev))
		    {                
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\nHCA restart has been skipped, because Multi function doesn't support it. \n\n"));
                event.event = IB_EVENT_RESET_END;
                dev->flags &= (~MLX4_FLAG_RESET_CLIENT); //clear this flag that was set at mlx4_reset_request
				dev->flags &= ~MLX4_FLAG_RESET_STARTED;
		    }
            else
            {
    			// restart the device
                MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\n Performing HCA restart ... \n\n"));
    			WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_INFO_RESET_START, 0, 0, 0 );
    			err = mlx4_restart_one(pdev);
    			if (err) {
    				event.event = IB_EVENT_RESET_FAILED;
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\n HCa restart failed. \n\n"));
    			}
    			else {
    				// recreate interfaces
    				fix_bus_ifc(pdev);
    				event.event = IB_EVENT_RESET_END;
                    MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\n HCA restart finished. Notifying the clients ... \n\n"));
    				WriteEventLogEntryData( pdev->p_self_do, (ULONG)EVENT_MLX4_INFO_RESET_END, 0, 0, 0 );
    			}
    		}
		}
	} 
	else {		
        MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("\nHCA restart has been skipped, because IB stack doesn't support it. \n\n"));
		event.event = IB_EVENT_RESET_END;
		dev->flags &= (~MLX4_FLAG_RESET_CLIENT); //clear this flag that was set at mlx4_reset_request
	}
	
	// notify the clients
	list_for_each_entry_safe(handler, thandler, &tlist, 
		list, struct ib_event_handler, struct ib_event_handler)
	{
		// because 'handler' will be re-registered during the next call
		list_del( &handler->list );
		handler->handler(handler, &event);
	}
	
	st_et_event("Bus: mlx4_reset_execute ended for handler %p with err %d\n", event_handler, err);
	return err;
}


static IO_WORKITEM_ROUTINE card_reset_wi;
static VOID
card_reset_wi(
	IN				PDEVICE_OBJECT				p_dev_obj,
    IN 				PVOID 						context)
{
    ASSERT(context != NULL);
	struct ib_event_handler *	event_handler = (struct ib_event_handler *) context;
	struct ib_device *ibdev = event_handler->device;

	UNUSED_PARAM(p_dev_obj);
	IoFreeWorkItem( (PIO_WORKITEM)event_handler->rsrv_ptr );

	// notify the clients
	st_et_event("Bus: card_reset_wi sent IB_EVENT_RESET_CLIENT\n");
	mlx4_dispatch_reset_event(ibdev, IB_EVENT_RESET_CLIENT);
}

int mlx4_reset_request( struct ib_event_handler *event_handler )
{
	struct ib_device *ibdev;
	struct mlx4_dev *dev;

	unsigned long flags;
	UNUSED_PARAM(flags);

	st_et_event("Bus: mlx4_reset_request called for handler %p \n", event_handler);

	ibdev = event_handler->device;
	if (ibdev == NULL)
		return -EFAULT;

	dev = ibdev->dma_device;
	if (ibdev == NULL)
		return -EFAULT;

	spin_lock_irqsave(&ibdev->event_handler_lock, &flags);

	// set device to RESET_PENDING mode
	if (!(dev->flags & (MLX4_FLAG_RESET_CLIENT | MLX4_FLAG_RESET_DRIVER))) {
		PIO_WORKITEM reset_work;

		// bar the device
		dev->flags |= MLX4_FLAG_RESET_CLIENT;

		// delay reset to a system thread
		// to allow for end of operations that are in progress
		reset_work = IoAllocateWorkItem( dev->pdev->p_self_do );
		if (!reset_work) {
			spin_unlock_irqrestore(&ibdev->event_handler_lock, flags);			
            MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,("%s: mlx4_reset_request IoAllocateWorkItem failed, reset will not be propagated\n", dev->pdev->name));
			return -EFAULT;
		}
		event_handler->rsrv_ptr = reset_work;
		IoQueueWorkItem( reset_work, (PIO_WORKITEM_ROUTINE)card_reset_wi, DelayedWorkQueue, event_handler );
	}

	spin_unlock_irqrestore(&ibdev->event_handler_lock, flags);


	return 0;
}

int mlx4_reset_cb_register( struct ib_event_handler *event_handler )
{
//	st_et_event("Bus: mlx4_reset_cb_register called for handler %p \n", event_handler);

	if (mlx4_is_in_reset(event_handler->device->dma_device))
		return -EBUSY;

	return ib_register_event_handler(event_handler);
}

int mlx4_reset_cb_unregister( struct ib_event_handler *event_handler )
{
//	st_et_event("Bus: mlx4_reset_cb_unregister called for handler %p \n", event_handler);
	return ib_unregister_event_handler(event_handler);
}


