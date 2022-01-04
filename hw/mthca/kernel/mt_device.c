/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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

#include "hca_driver.h"
#if defined(EVENT_TRACING)
#ifdef offsetof
#undef offsetof
#endif
#include "mt_device.tmh"
#endif
#include "ib_verbs.h"
#include "ib_cache.h"

struct ib_client_data {
	struct list_head  list;
	struct ib_client *client;
	void *            data;
};

static LIST_HEAD(device_list);
static LIST_HEAD(client_list);

/*
 * device_mutex protects access to both device_list and client_list.
 * There's no real point to using multiple locks or something fancier
 * like an rwsem: we always access both lists, and we're always
 * modifying one list or the other list.  In any case this is not a
 * hot path so there's no point in trying to optimize.
 */
KMUTEX device_mutex;

static int ib_device_check_mandatory(struct ib_device *device)
{
#define IB_MANDATORY_FUNC(x) { offsetof(struct ib_device, x), #x }
	static const struct {
		size_t offset;
		char  *name;
	} mandatory_table[] = {
		IB_MANDATORY_FUNC(query_device),
		IB_MANDATORY_FUNC(query_port),
		IB_MANDATORY_FUNC(query_pkey_chunk),
		IB_MANDATORY_FUNC(query_gid_chunk),
		IB_MANDATORY_FUNC(alloc_pd),
		IB_MANDATORY_FUNC(dealloc_pd),
		IB_MANDATORY_FUNC(create_ah),
		IB_MANDATORY_FUNC(destroy_ah),
		IB_MANDATORY_FUNC(create_qp),
		IB_MANDATORY_FUNC(modify_qp),
		IB_MANDATORY_FUNC(destroy_qp),
		IB_MANDATORY_FUNC(post_send),
		IB_MANDATORY_FUNC(post_recv),
		IB_MANDATORY_FUNC(create_cq),
		IB_MANDATORY_FUNC(destroy_cq),
		IB_MANDATORY_FUNC(poll_cq),
		IB_MANDATORY_FUNC(req_notify_cq),
		IB_MANDATORY_FUNC(get_dma_mr),
		IB_MANDATORY_FUNC(dereg_mr)
	};
	int i;

	for (i = 0; i < sizeof mandatory_table / sizeof mandatory_table[0]; ++i) {
		if (!*(void **) ((u8 *) device + mandatory_table[i].offset)) {
			HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW,("Device %s is missing mandatory function %s\n",
			       device->name, mandatory_table[i].name));
			return -EINVAL;
		}
	}

	return 0;
}

static struct ib_device *__ib_device_get_by_name(const char *name)
{
	struct ib_device *device;

	list_for_each_entry(device, &device_list, core_list,struct ib_device)
		if (!strncmp(name, device->name, IB_DEVICE_NAME_MAX))
			return device;

	return NULL;
}

static int __extract_number(char *dest_str, const char *format, int *num)
{
	char *ptr;
	UNREFERENCED_PARAMETER(format);
	for (ptr = dest_str; *ptr; ptr++) {
		if (*ptr >= '0' && *ptr <= '9') {
			*num = atoi(ptr);
			return 1;
		}
	}
	return 0;
}
static int alloc_name(char *name)
{
	long *inuse;
	char buf[IB_DEVICE_NAME_MAX];
	struct ib_device *device;
	int i;

	inuse = (long *) get_zeroed_page(GFP_KERNEL);
	if (!inuse)
		return -ENOMEM;

	list_for_each_entry(device, &device_list, core_list,struct ib_device) {
		if (!__extract_number(device->name, name, &i))
			continue;
		if (i < 0 || i >= PAGE_SIZE * 8)
			continue;
		if (RtlStringCbPrintfA(buf, sizeof buf, name, i))
			return -EINVAL;

		if (!strncmp(buf, device->name, IB_DEVICE_NAME_MAX))
			set_bit(i, inuse);
	}

	i = find_first_zero_bit((const unsigned long *)inuse, PAGE_SIZE * 8);
	free_page(inuse);
	if (RtlStringCbPrintfA(buf, sizeof buf, name, i))
			return -EINVAL;


	if (__ib_device_get_by_name(buf))
		return -ENFILE;

	strlcpy(name, buf, IB_DEVICE_NAME_MAX);
	return 0;
}

static int add_client_context(struct ib_device *device, struct ib_client *client)
{
	struct ib_client_data *context;
	SPIN_LOCK_PREP(lh);

	context = kmalloc(sizeof *context, GFP_KERNEL);
	if (!context) {
		HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW,("Couldn't allocate client context for %s/%s\n",
		       device->name, client->name));
		return -ENOMEM;
	}

	context->client = client;
	context->data   = NULL;

	spin_lock_irqsave(&device->client_data_lock, &lh);
	list_add(&context->list, &device->client_data_list);
	spin_unlock_irqrestore(&lh);

	return 0;
}

/**
 * ib_register_device - Register an IB device with IB core
 * @device:Device to register
 *
 * Low-level drivers use ib_register_device() to register their
 * devices with the IB core.  All registered clients will receive a
 * callback for each device that is added. @device must be allocated
 * with ib_alloc_device().
 */
int ib_register_device(struct ib_device *device)
{
	int ret = 0;

	down(&device_mutex);

	if (strchr(device->name, '%')) {
		ret = alloc_name(device->name);
		if (ret)
			goto out;
	}

	if (ib_device_check_mandatory(device)) {
		ret = -EINVAL;
		goto out;
	}

	INIT_LIST_HEAD(&device->event_handler_list);
	INIT_LIST_HEAD(&device->client_data_list);
	spin_lock_init(&device->event_handler_lock);
	spin_lock_init(&device->client_data_lock);

	list_add_tail(&device->core_list, &device_list);

	{
		struct ib_client *client;

		list_for_each_entry(client, &client_list, list,struct ib_client)
			if (client->add && !add_client_context(device, client))
				client->add(device);
	}

 out:
	up(&device_mutex);
	return ret;
}


/**
 * ib_unregister_device - Unregister an IB device
 * @device:Device to unregister
 *
 * Unregister an IB device.  All clients will receive a remove callback.
 */
void ib_unregister_device(struct ib_device *device)
{
	struct ib_client *client;
	struct ib_client_data *context, *tmp;
	SPIN_LOCK_PREP(lh);

	down(&device_mutex);

	list_for_each_entry_reverse(client, &client_list, list,struct ib_client)
		if (client->remove)
			client->remove(device);

	list_del(&device->core_list);

	up(&device_mutex);

	spin_lock_irqsave(&device->client_data_lock, &lh);
	list_for_each_entry_safe(context, tmp, &device->client_data_list, list,struct ib_client_data,struct ib_client_data)
		kfree(context);
	spin_unlock_irqrestore(&lh);

}


/**
 * ib_register_client - Register an IB client
 * @client:Client to register
 *
 * Upper level users of the IB drivers can use ib_register_client() to
 * register callbacks for IB device addition and removal.  When an IB
 * device is added, each registered client's add method will be called
 * (in the order the clients were registered), and when a device is
 * removed, each client's remove method will be called (in the reverse
 * order that clients were registered).  In addition, when
 * ib_register_client() is called, the client will receive an add
 * callback for all devices already registered.
 */
int ib_register_client(struct ib_client *client)
{
	struct ib_device *device;

	down(&device_mutex);

	list_add_tail(&client->list, &client_list);
	list_for_each_entry(device, &device_list, core_list,struct ib_device)
		if (client->add && !add_client_context(device, client))
			client->add(device);

	up(&device_mutex);

	return 0;
}


/**
 * ib_unregister_client - Unregister an IB client
 * @client:Client to unregister
 *
 * Upper level users use ib_unregister_client() to remove their client
 * registration.  When ib_unregister_client() is called, the client
 * will receive a remove callback for each IB device still registered.
 */
void ib_unregister_client(struct ib_client *client)
{
	struct ib_client_data *context, *tmp;
	struct ib_device *device;
	SPIN_LOCK_PREP(lh);

	down(&device_mutex);

	list_for_each_entry(device, &device_list, core_list,struct ib_device) {
		if (client->remove)
			client->remove(device);

		spin_lock_irqsave(&device->client_data_lock, &lh);
		list_for_each_entry_safe(context, tmp, &device->client_data_list, list,struct ib_client_data,struct ib_client_data)
			if (context->client == client) {
				list_del(&context->list);
				kfree(context);
			}
		spin_unlock_irqrestore(&lh);
	}
	list_del(&client->list);

	up(&device_mutex);
}


/**
 * ib_get_client_data - Get IB client context
 * @device:Device to get context for
 * @client:Client to get context for
 *
 * ib_get_client_data() returns client context set with
 * ib_set_client_data().
 */
void *ib_get_client_data(struct ib_device *device, struct ib_client *client)
{
	struct ib_client_data *context;
	void *ret = NULL;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&device->client_data_lock, &lh);
	list_for_each_entry(context, &device->client_data_list, list,struct ib_client_data)
		if (context->client == client) {
			ret = context->data;
			break;
		}
	spin_unlock_irqrestore(&lh);

	return ret;
}


/**
 * ib_set_client_data - Get IB client context
 * @device:Device to set context for
 * @client:Client to set context for
 * @data:Context to set
 *
 * ib_set_client_data() sets client context that can be retrieved with
 * ib_get_client_data().
 */
void ib_set_client_data(struct ib_device *device, struct ib_client *client,
			void *data)
{
	struct ib_client_data *context;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&device->client_data_lock, &lh);
	list_for_each_entry(context, &device->client_data_list, list,struct ib_client_data)
		if (context->client == client) {
			context->data = data;
			goto out;
		}

	HCA_PRINT(TRACE_LEVEL_WARNING ,HCA_DBG_LOW ,("No client context found for %s/%s\n",
	       device->name, client->name));

out:
	spin_unlock_irqrestore(&lh);
}


/**
 * ib_register_event_handler - Register an IB event handler
 * @event_handler:Handler to register
 *
 * ib_register_event_handler() registers an event handler that will be
 * called back when asynchronous IB events occur (as defined in
 * chapter 11 of the InfiniBand Architecture Specification).  This
 * callback may occur in interrupt context.
 */
int ib_register_event_handler  (struct ib_event_handler *event_handler)
{
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&event_handler->device->event_handler_lock, &lh);
	list_add_tail(&event_handler->list,
		      &event_handler->device->event_handler_list);
	spin_unlock_irqrestore(&lh);

	return 0;
}


/**
 * ib_unregister_event_handler - Unregister an event handler
 * @event_handler:Handler to unregister
 *
 * Unregister an event handler registered with
 * ib_register_event_handler().
 */
int ib_unregister_event_handler(struct ib_event_handler *event_handler)
{
	SPIN_LOCK_PREP(lh);
	spin_lock_irqsave(&event_handler->device->event_handler_lock, &lh);
	list_del(&event_handler->list);
	spin_unlock_irqrestore(&lh);

	return 0;
}


/**
 * ib_dispatch_event - Dispatch an asynchronous event
 * @event:Event to dispatch
 *
 * Low-level drivers must call ib_dispatch_event() to dispatch the
 * event to all registered event handlers when an asynchronous event
 * occurs.
 */
void ib_dispatch_event(struct ib_event *event)
{
	struct ib_event_handler *handler;
	SPIN_LOCK_PREP(lh);

	spin_lock_irqsave(&event->device->event_handler_lock, &lh);

	list_for_each_entry(handler, &event->device->event_handler_list, list,struct ib_event_handler)
		handler->handler(handler, event);

	spin_unlock_irqrestore(&lh);
}


/**
 * ib_query_device - Query IB device attributes
 * @device:Device to query
 * @device_attr:Device attributes
 *
 * ib_query_device() returns the attributes of a device through the
 * @device_attr pointer.
 */
int ib_query_device(struct ib_device *device,
		    struct ib_device_attr *device_attr)
{
	return device->query_device(device, device_attr);
}


/**
 * ib_query_port - Query IB port attributes
 * @device:Device to query
 * @port_num:Port number to query
 * @port_attr:Port attributes
 *
 * ib_query_port() returns the attributes of a port through the
 * @port_attr pointer.
 */
int ib_query_port(struct ib_device *device,
		  u8 port_num,
		  struct ib_port_attr *port_attr)
{
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;
	return device->query_port(device, port_num, port_attr);
}


/**
 * ib_query_gid_chunk - Get a chunk of GID table entries
 * @device:Device to query
 * @port_num:Port number to query
 * @index:GID table index to query
 * @gid:Returned GIDs chunk
 *
 * ib_query_gid_chunk() fetches the specified GID table enties chunk.
 */
int ib_query_gid_chunk(struct ib_device *device,
		 u8 port_num, int index, union ib_gid gid[8])
{
	return device->query_gid_chunk(device, port_num, index, gid);
}


/**
 * ib_query_pkey_chunk - Get a chunk of  P_Key table entries
 * @device:Device to query
 * @port_num:Port number to query
 * @index:P_Key table index to query
 * @pkey:Returned P_Keys chunk
 *
 * ib_query_pkey_chunk() fetches the specified P_Key table entries chunk.
 */
int ib_query_pkey_chunk(struct ib_device *device,
		  u8 port_num, u16 index, __be16 pkey[32])
{
	return device->query_pkey_chunk(device, port_num, index, pkey);
}


/**
 * ib_modify_device - Change IB device attributes
 * @device:Device to modify
 * @device_modify_mask:Mask of attributes to change
 * @device_modify:New attribute values
 *
 * ib_modify_device() changes a device's attributes as specified by
 * the @device_modify_mask and @device_modify structure.
 */
int ib_modify_device(struct ib_device *device,
		     int device_modify_mask,
		     struct ib_device_modify *device_modify)
{
	return device->modify_device(device, device_modify_mask,
				     device_modify);
}


/**
 * ib_modify_port - Modifies the attributes for the specified port.
 * @device: The device to modify.
 * @port_num: The number of the port to modify.
 * @port_modify_mask: Mask used to specify which attributes of the port
 *   to change.
 * @port_modify: New attribute values for the port.
 *
 * ib_modify_port() changes a port's attributes as specified by the
 * @port_modify_mask and @port_modify structure.
 */
int ib_modify_port(struct ib_device *device,
		   u8 port_num, int port_modify_mask,
		   struct ib_port_modify *port_modify)
{
	if (port_num < start_port(device) || port_num > end_port(device))
		return -EINVAL;

	return device->modify_port(device, port_num, port_modify_mask,
				   port_modify);
}

int ib_core_init(void)
{
	int ret;

	/* leo: added because there is no static init of semaphore in Windows */
	KeInitializeMutex(&device_mutex,0);
	
	ret = ib_cache_setup();
	if (ret) {
		HCA_PRINT(TRACE_LEVEL_WARNING   ,HCA_DBG_LOW   ,("Couldn't set up InfiniBand P_Key/GID cache\n"));
	}

	return ret;
}

void ib_core_cleanup(void)
{
	ib_cache_cleanup();
}

