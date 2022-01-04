/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
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

#include "ib\mlx4_ib.h"
#include "ib_cache.h"
#include <mlx4_debug.h>
#include "mlx4.h"

#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "cache.tmh"
#endif

#pragma warning( disable : 4200)
struct ib_pkey_cache {
	int             table_len;
	__be16          table[0];
};

struct ib_gid_cache {
	int             table_len;
	union ib_gid    table[0];
};
#pragma warning( default  : 4200)

struct ib_update_work {
	PIO_WORKITEM work_item;
	struct ib_device  *device;
	u8                 port_num;
};

static inline int start_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ? 0 : 1;
}

static inline int end_port(struct ib_device *device)
{
	return (device->node_type == RDMA_NODE_IB_SWITCH) ?
		0 : device->phys_port_cnt;
}

int ib_get_cached_gid(struct ib_device *device,
		      u8                port_num,
		      int               index,
		      union ib_gid     *gid)
{
	union ib_gid cgid;
	struct ib_gid_cache *cache;
	unsigned long flags;
	UNUSED_PARAM(flags);
	
	if (mlx4_is_barred(device->dma_device))
		return -EFAULT;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, &flags);

	cache = device->cache.gid_cache[port_num - start_port(device)];

	if (index < 0 || index >= cache->table_len) {
		read_unlock_irqrestore(&device->cache.lock, flags);
		return -EINVAL;
	}
	
	cgid = cache->table[index];
	read_unlock_irqrestore(&device->cache.lock, flags);
	*gid = cgid;

	return 0;
}
EXPORT_SYMBOL(ib_get_cached_gid);

int ib_find_cached_gid(struct ib_device *device,
		       union ib_gid	*gid,
		       u8               *port_num,
		       u16              *index)
{
	struct ib_gid_cache *cache;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int p, i;

	if (mlx4_is_barred(device->dma_device))
		return -EFAULT;

	read_lock_irqsave(&device->cache.lock, &flags);

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		cache = device->cache.gid_cache[p];
		for (i = 0; i < cache->table_len; ++i) {
			if (!memcmp(gid, &cache->table[i], sizeof *gid)) {
				goto found;
			}
		}
	}

	read_unlock_irqrestore(&device->cache.lock, flags);
	*port_num = (u8)-1;
	if (index)
		*index = (u16)-1;
	return -ENOENT;

found:
	read_unlock_irqrestore(&device->cache.lock, flags);
	*port_num = (u8)(p + start_port(device));
	if (index)
		*index = (u16)i;

	return 0;
}
EXPORT_SYMBOL(ib_find_cached_gid);

int ib_get_cached_pkey(struct ib_device *device,
		       u8                port_num,
		       int               index,
		       __be16           *pkey)
{
	struct ib_pkey_cache *cache;
	__be16 cpkey;
	unsigned long flags;
	UNUSED_PARAM(flags);

	if (mlx4_is_barred(device->dma_device))
		return -EFAULT;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, &flags);

	cache = device->cache.pkey_cache[port_num - start_port(device)];

	if (index < 0 || index >= cache->table_len) {
		read_unlock_irqrestore(&device->cache.lock, flags);
		return -EINVAL;
	}

	cpkey = cache->table[index];
	read_unlock_irqrestore(&device->cache.lock, flags);
	*pkey = cpkey;

	return 0;
}
EXPORT_SYMBOL(ib_get_cached_pkey);

int ib_find_cached_pkey(struct ib_device *device,
			u8                port_num,
			__be16            pkey,
			u16              *index)
{
	struct ib_pkey_cache *cache;
	unsigned long flags;
	UNUSED_PARAM(flags);
	int i;

	if (mlx4_is_barred(device->dma_device))
		return -EFAULT;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, &flags);

	cache = device->cache.pkey_cache[port_num - start_port(device)];

	*index = (u16)-1;

	for (i = 0; i < cache->table_len; ++i)
		if ((cache->table[i] & 0x7fff) == (pkey & 0x7fff)) {
			goto found;
		}

	read_unlock_irqrestore(&device->cache.lock, flags);
	*index = (u16)-1;
	return -ENOENT;

found:
	read_unlock_irqrestore(&device->cache.lock, flags);
	*index = (u16)i;
	return 0;
}
EXPORT_SYMBOL(ib_find_cached_pkey);

int ib_get_cached_lmc(struct ib_device *device,
		      u8                port_num,
		      u8                *lmc)
{
	unsigned long flags;
	UNUSED_PARAM(flags);
	u8 clmc;
	int ret = 0;

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, &flags);
	clmc = device->cache.lmc_cache[port_num - start_port(device)];
	read_unlock_irqrestore(&device->cache.lock, flags);
	*lmc = clmc;

	return ret;
}
EXPORT_SYMBOL(ib_get_cached_lmc);

static int ib_cache_update(struct ib_device *device,
			    u8                port)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL, *old_pkey_cache;
	struct ib_gid_cache       *gid_cache = NULL, *old_gid_cache;
	int                        i;
	int                        ret;

	if (mlx4_get_effective_port_type(device->dma_device, port) != MLX4_PORT_TYPE_IB)
		return 0;

	tprops = (ib_port_attr *)kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops) {
		ret = -ENOMEM;
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "tprops malloc failed (%d) for %s. port_num %d (%d,%d)\n",
			ret, device->name, (int)port, start_port(device), end_port(device) ));
		goto err;
	}


	ret = ib_query_port(device, port, tprops);
	if (ret) {
		MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "ib_query_port failed (%d) for %s. port_num %d (%d,%d)\n",
			ret, device->name, (int)port, start_port(device), end_port(device) ));
		goto err;
	}

	pkey_cache = (ib_pkey_cache *)kmalloc(sizeof *pkey_cache + tprops->pkey_tbl_len *
			     sizeof *pkey_cache->table, GFP_KERNEL);
	if (!pkey_cache) {
		ret = -ENOMEM;
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "pkey_cache malloc failed (%d) for %s. port_num %d (%d,%d)\n",
			ret, device->name, (int)port, start_port(device), end_port(device) ));
		goto err;
	}

	pkey_cache->table_len = tprops->pkey_tbl_len;

	gid_cache = (ib_gid_cache *)kmalloc(sizeof *gid_cache + tprops->gid_tbl_len *
			    sizeof *gid_cache->table, GFP_KERNEL);
	if (!gid_cache) {
		ret = -ENOMEM;
		MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "gid_cache malloc failed (%d) for %s. port_num %d (%d,%d)\n",
			ret, device->name, (int)port, start_port(device), end_port(device) ));
		goto err;
	}

	gid_cache->table_len = tprops->gid_tbl_len;

	for (i = 0; i < pkey_cache->table_len; i+=32) {
		int size = min(32, pkey_cache->table_len - i);
		ret = ib_query_pkey_chunk(device, port, (u16)i, pkey_cache->table + i, size );
		if (ret) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "ib_query_pkey_chunk failed (%d) for %s (index %d)\n",
			       ret, device->name, i));
			goto err;
		}
	}

	for (i = 0; i < gid_cache->table_len; i+=8) {
		int size = min(8, gid_cache->table_len - i);
		ret = ib_query_gid_chunk(device, port, i, gid_cache->table + i, size);
		if (ret) {
			MLX4_PRINT(TRACE_LEVEL_WARNING, MLX4_DBG_DRV,( "ib_query_gid_chunk failed (%d) for %s (index %d)\n",
			       ret, device->name, i));
			goto err;
		}
	}

	write_lock_irq(&device->cache.lock);

	old_pkey_cache = device->cache.pkey_cache[port - start_port(device)];
	old_gid_cache  = device->cache.gid_cache [port - start_port(device)];

	device->cache.pkey_cache[port - start_port(device)] = pkey_cache;
	device->cache.gid_cache [port - start_port(device)] = gid_cache;

	device->cache.lmc_cache[port - start_port(device)] = tprops->lmc;

	write_unlock_irq(&device->cache.lock);

	kfree(old_pkey_cache);
	kfree(old_gid_cache);
	kfree(tprops);
	return 0;

err:
	kfree(pkey_cache);
	kfree(gid_cache);
	kfree(tprops);
	return ret;
}

static void ib_cache_task(void *work_ptr)
{
	struct ib_update_work *work = (ib_update_work *)work_ptr;

	ib_cache_update(work->device, work->port_num);
}

static IO_WORKITEM_ROUTINE ib_work_item;
static void ib_work_item (
	IN PDEVICE_OBJECT  DeviceObject,
	IN PVOID  Context 
	)
{   
    ASSERT(Context != NULL);
	struct ib_update_work *work = (struct ib_update_work *)Context;
	UNREFERENCED_PARAMETER(DeviceObject);
	ib_cache_task(Context);
	shutter_loose( &work->device->cache.x.work_thread );
	IoFreeWorkItem(work->work_item);
	kfree(Context);
}

static void ib_cache_event(struct ib_event_handler *handler,
			   struct ib_event *event)
{
	struct ib_update_work *work;

	if (event->event == IB_EVENT_PORT_ERR    ||
		event->event == IB_EVENT_PORT_ACTIVE ||
		event->event == IB_EVENT_LID_CHANGE  ||
		event->event == IB_EVENT_PKEY_CHANGE ||
		event->event == IB_EVENT_SM_CHANGE   ||
		event->event == IB_EVENT_CLIENT_REREGISTER) {

		work = (ib_update_work *)kmalloc(sizeof *work, GFP_ATOMIC);
		if (work) {
			PDEVICE_OBJECT pdo = to_mdev(handler->device)->dev->pdev->p_self_do;

			// allocate work item
			work->work_item = IoAllocateWorkItem(pdo);
			if (work->work_item == NULL) {
				//TODO: at least - print error. Need to return code, but the function is void 
				kfree( work );
				return;
			}

			// check whether we are in shutdown
			if (shutter_use( &event->device->cache.x.work_thread ) <= 0) {
				//TODO why can't we check this beforehand ?
				IoFreeWorkItem(work->work_item);
				kfree( work );
				return;
			}

			// schedule the work
			work->device   = event->device;
			work->port_num = event->element.port_num;
			IoQueueWorkItem( work->work_item, ib_work_item, DelayedWorkQueue, work );
		}
	}
}

static int ib_cache_setup_one(struct ib_device *device)
{
	int p;
	int port_num;
	int ret;
	
	shutter_init( &device->cache.x.work_thread );
	rwlock_init(&device->cache.lock);
	INIT_IB_EVENT_HANDLER(&device->cache.event_handler,
			      device, ib_cache_event, NULL, NULL, 0, MLX4_PORT_TYPE_IB);
	ib_register_event_handler(&device->cache.event_handler);

	port_num = end_port(device) - start_port(device) + 1;
	if (port_num > 0 ) { 
		// if port_num ==0   ==> there are no IB ports
		device->cache.pkey_cache =
			(ib_pkey_cache **)kmalloc(sizeof *device->cache.pkey_cache * port_num, GFP_KERNEL);
		device->cache.gid_cache =
			(ib_gid_cache **)kmalloc(sizeof *device->cache.gid_cache * port_num, GFP_KERNEL);
		device->cache.lmc_cache = (u8*)kmalloc(sizeof *device->cache.lmc_cache *
			port_num, GFP_KERNEL);

		if (!device->cache.pkey_cache || !device->cache.gid_cache ||
			!device->cache.lmc_cache) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't allocate cache "
				"for %s\n", device->name));
			ret = -ENOMEM;
			goto err;
		}
	}

	for (p = 0; p < port_num; ++p) {
		device->cache.pkey_cache[p] = NULL;
		device->cache.gid_cache [p] = NULL;
		ret = ib_cache_update(device, (u8)(p + start_port(device)));
		if ( ret ) {
			MLX4_PRINT(TRACE_LEVEL_ERROR, MLX4_DBG_DRV,( "Couldn't update cache "
				"for %s, ret %d\n", device->name, ret));
			goto err;
		}
	}

	return 0;

err:
	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
	kfree(device->cache.lmc_cache);
	ib_unregister_event_handler(&device->cache.event_handler);
	return ret;
}

// calling sequence:
// EvtReleaseHardware ==> mlx4_ib_cleanup ==> mlx4_unregister_interface ==>
// ==> mlx4_remove_device ==> mlx4_ib_remove ==> ib_unregister_device ==>
// ==> ib_cache_cleanup
static void ib_cache_cleanup_one(struct ib_device *device)
{
	int p;

	ASSERT(device->cache.event_handler.device);
	ib_unregister_event_handler(&device->cache.event_handler);
	// instead of Linux flush_scheduled_work(): wait for them to quit
	shutter_shut( &device->cache.x.work_thread );

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		kfree(device->cache.pkey_cache[p]);
		kfree(device->cache.gid_cache[p]);
	}

	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
	kfree(device->cache.lmc_cache);
}

static struct ib_client cache_client = { "cache", ib_cache_setup_one, ib_cache_cleanup_one };

int __init ib_cache_setup(void)
{
	return ib_register_client(&cache_client);
}

void __exit ib_cache_cleanup(void)
{
	ib_unregister_client(&cache_client);
}
