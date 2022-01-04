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

#include <mt_l2w.h>
#include "mthca_dev.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_cache.tmh"
#endif
#include <ib_cache.h>

#include "ib_cache.h"


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

int ib_get_cached_gid(struct ib_device *device,
		      u8                port_num,
		      int               index,
		      union ib_gid     *gid)
{
	union ib_gid cgid;
	struct ib_gid_cache *cache;
	SPIN_LOCK_PREP(lh);

	// sanity checks
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;
	if (!device->cache.gid_cache)
		return -EFAULT;

	read_lock_irqsave(&device->cache.lock, &lh);

	cache = device->cache.gid_cache[port_num - start_port(device)];

	if (index < 0 || index >= cache->table_len) {
		read_unlock_irqrestore(&lh);
		return -EINVAL;
	}

	cgid = cache->table[index];
	read_unlock_irqrestore(&lh);
	*gid = cgid;

	return 0;
}

int ib_find_cached_gid(struct ib_device *device,
		       union ib_gid	*gid,
		       u8               *port_num,
		       u16              *index)
{
	struct ib_gid_cache *cache;
	int i;
	u8 p;
	SPIN_LOCK_PREP(lh);

	read_lock_irqsave(&device->cache.lock, &lh);

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		cache = device->cache.gid_cache[p];
		for (i = 0; i < cache->table_len; ++i) {
			if (!memcmp(gid, &cache->table[i], sizeof *gid)) {
				goto found;
			}
		}
	}

	read_unlock_irqrestore(&lh);
	*port_num = (u8)-1;
	if (index)
		*index = (u16)-1;
	return -ENOENT;

found:
	read_unlock_irqrestore(&lh);
	*port_num = p + start_port(device);
	if (index)
		*index = (u16)i;

	return 0;
}

int ib_get_cached_pkey(struct ib_device *device,
		       u8                port_num,
		       int               index,
		       __be16           *pkey)
{
	struct ib_pkey_cache *cache;
	__be16 cpkey;
	SPIN_LOCK_PREP(lh);

	// sanity checks
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;
	if (!device->cache.gid_cache)
		return -EFAULT;

	read_lock_irqsave(&device->cache.lock, &lh);

	cache = device->cache.pkey_cache[port_num - start_port(device)];

	if (index < 0 || index >= cache->table_len) {
		read_unlock_irqrestore(&lh);
		return -EINVAL;
	}

	cpkey = cache->table[index];
	read_unlock_irqrestore(&lh);
	*pkey = cpkey;

	return 0;
}

int ib_find_cached_pkey(struct ib_device *device,
			u8                port_num,
			__be16            pkey,
			u16              *index)
{
	struct ib_pkey_cache *cache;
	int i;
	SPIN_LOCK_PREP(lh);

	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	read_lock_irqsave(&device->cache.lock, &lh);

	cache = device->cache.pkey_cache[port_num - start_port(device)];

	for (i = 0; i < cache->table_len; ++i)
		if ((cache->table[i] & 0x7fff) == (pkey & 0x7fff)) {
			goto found;
		}

	read_unlock_irqrestore(&lh);
	*index = (u16)-1;
	return -ENOENT;

found:
	read_unlock_irqrestore(&lh);
	*index = (u16)i;
	return 0;
}

static void ib_cache_update(struct ib_device *device,
			    u8                port)
{
	struct ib_port_attr       *tprops = NULL;
	struct ib_pkey_cache      *pkey_cache = NULL, *old_pkey_cache;
	struct ib_gid_cache       *gid_cache = NULL, *old_gid_cache;
	int                        i;
	int                        ret;
	SPIN_LOCK_PREP(lh);

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		return;

	ret = ib_query_port(device, port, tprops);
	if (ret) {
		HCA_PRINT(TRACE_LEVEL_WARNING,HCA_DBG_LOW,("ib_query_port failed (%d) for %s, port %d\n",
		       ret, device->name, port));
		goto err;
	}

	pkey_cache = kmalloc(sizeof *pkey_cache + tprops->pkey_tbl_len *
			     sizeof *pkey_cache->table, GFP_KERNEL);
	if (!pkey_cache)
		goto err;

	pkey_cache->table_len = tprops->pkey_tbl_len;

	gid_cache = kmalloc(sizeof *gid_cache + tprops->gid_tbl_len *
			    sizeof *gid_cache->table, GFP_KERNEL);
	if (!gid_cache)
		goto err;

	gid_cache->table_len = tprops->gid_tbl_len;

	for (i = 0; i < pkey_cache->table_len; i+=32) {
		__be16 pkey_chunk[32];
		int size;
		ret = ib_query_pkey_chunk(device, port, (u16)i, pkey_chunk);
		if (ret) {
			HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW,("ib_query_pkey_chunk failed (%d) for %s (index %d)\n",
			       ret, device->name, i));
			goto err;
		}
		size = min(32, pkey_cache->table_len - i);
		RtlCopyMemory(pkey_cache->table + i, pkey_chunk, size*sizeof(u16));
	}

	for (i = 0; i < gid_cache->table_len; i+=8) {
		union ib_gid gid_chunk[8];
		int size;
		ret = ib_query_gid_chunk(device, port, i, gid_chunk);
		if (ret) {
			HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW,("ib_query_gid_chunk failed (%d) for %s (index %d)\n",
			       ret, device->name, i));
			goto err;
		}
		size = min(8, gid_cache->table_len - i);
		RtlCopyMemory(gid_cache->table + i, gid_chunk, size*sizeof(union ib_gid));
	}

	write_lock_irq(&device->cache.lock, &lh);

	old_pkey_cache = device->cache.pkey_cache[port - start_port(device)];
	old_gid_cache  = device->cache.gid_cache [port - start_port(device)];

	device->cache.pkey_cache[port - start_port(device)] = pkey_cache;
	device->cache.gid_cache [port - start_port(device)] = gid_cache;

	write_unlock_irq(&lh);

	kfree(old_pkey_cache);
	kfree(old_gid_cache);
	kfree(tprops);
	return;

err:
	kfree(pkey_cache);
	kfree(gid_cache);
	kfree(tprops);
}

static void ib_cache_task(void *work_ptr)
{
	struct ib_update_work *work = work_ptr;

	ib_cache_update(work->device, work->port_num);
}

/* leo: wrapper for Linux work_item callback */
VOID
  ib_work_item (
    IN PDEVICE_OBJECT  DeviceObject,
    IN PVOID  Context 
    )
{
	struct ib_update_work *work = (struct ib_update_work *)Context;
	UNREFERENCED_PARAMETER(DeviceObject);
	ib_cache_task(Context);
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
	    event->event == IB_EVENT_SM_CHANGE) {
		work = kmalloc(sizeof *work, GFP_ATOMIC);
		//TODO: what will happen on allocation failure ?
		if (work) {
			work->device   = event->device;
			work->port_num = event->element.port_num;

			{ // schedule a work item to work
				// get PDO
				PDEVICE_OBJECT pdo = handler->device->mdev->ext->cl_ext.p_self_do;

				// allocate work item
				work->work_item = IoAllocateWorkItem(pdo);
				if (work->work_item == NULL) {
					HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_LOW,("Failed to allocate workitem for cache update.\n"));
				}
				else { // schedule the work
					IoQueueWorkItem(
							work->work_item,
							ib_work_item,
							DelayedWorkQueue,
							work
							);
				}
			}
			
		}
		else
		{
			HCA_PRINT(TRACE_LEVEL_ERROR ,HCA_DBG_MEMORY,("Failed to memory for workitem.\n"));
		}
	}
}

static void ib_cache_setup_one(struct ib_device *device)
{
	u8 p;

	rwlock_init(&device->cache.lock);
	INIT_IB_EVENT_HANDLER(&device->cache.event_handler,
			      device, ib_cache_event);
	ib_register_event_handler(&device->cache.event_handler);

	device->cache.pkey_cache =
		kmalloc(sizeof *device->cache.pkey_cache *
			(end_port(device) - start_port(device) + 1), GFP_KERNEL);
	device->cache.gid_cache =
		kmalloc(sizeof *device->cache.gid_cache *
			(end_port(device) - start_port(device) + 1), GFP_KERNEL);

	if (!device->cache.pkey_cache || !device->cache.gid_cache) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW,("Couldn't allocate cache "
		       "for %s\n", device->name));
		goto err;
	}

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		device->cache.pkey_cache[p] = NULL;
		device->cache.gid_cache [p] = NULL;
		ib_cache_update(device, p + start_port(device));
	}

	return;

err:
	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
}

static void ib_cache_cleanup_one(struct ib_device *device)
{
	int p;

	ib_unregister_event_handler(&device->cache.event_handler);
	//TODO: how to do that ?
	// LINUX: flush_scheduled_work();

	for (p = 0; p <= end_port(device) - start_port(device); ++p) {
		kfree(device->cache.pkey_cache[p]);
		kfree(device->cache.gid_cache[p]);
	}

	kfree(device->cache.pkey_cache);
	kfree(device->cache.gid_cache);
}

static struct ib_client cache_client = { "cache", ib_cache_setup_one, ib_cache_cleanup_one };

int ib_cache_setup(void)
{
	return ib_register_client(&cache_client);
}

void ib_cache_cleanup(void)
{
	ib_unregister_client(&cache_client);
}

