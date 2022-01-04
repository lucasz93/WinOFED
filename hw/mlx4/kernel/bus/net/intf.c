/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
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
#include "driver.h"

struct mlx4_device_context {
	struct list_head	list;
	struct mlx4_interface  *intf;
	void		       *context;
};

static LIST_HEAD(intf_list);
static LIST_HEAD(dev_list);
static DEFINE_MUTEX(intf_mutex);

static int mlx4_add_device(struct mlx4_interface *intf, struct mlx4_priv *priv)
{
	struct mlx4_device_context *dev_ctx;

	dev_ctx = (mlx4_device_context *)kmalloc(sizeof *dev_ctx, GFP_KERNEL);
	if (!dev_ctx)
		return -EFAULT;

	dev_ctx->intf    = intf;
	dev_ctx->context = intf->add(&priv->dev);
	priv->dev.pdev->ib_dev = (ib_device *)dev_ctx->context;

	if (dev_ctx->context) {
		spin_lock_irq(&priv->ctx_lock);
		list_add_tail(&dev_ctx->list, &priv->ctx_list);
		spin_unlock_irq(&priv->ctx_lock);
	} else {
		kfree(dev_ctx);
		return -EFAULT;
	}
	return 0;
}

static void mlx4_remove_device(struct mlx4_interface *intf, struct mlx4_priv *priv)
{
	struct mlx4_device_context *dev_ctx;

	list_for_each_entry(dev_ctx, &priv->ctx_list, list, struct mlx4_device_context)
		if (dev_ctx->intf == intf) {
			spin_lock_irq(&priv->ctx_lock);
			list_del(&dev_ctx->list);
			spin_unlock_irq(&priv->ctx_lock);

			intf->remove(&priv->dev, dev_ctx->context);
			kfree(dev_ctx);
			priv->dev.pdev->ib_dev = NULL;
			return;
		}
}

int mlx4_register_interface(struct mlx4_interface *intf)
{
	struct mlx4_priv *priv;
	int err = 0;

	if (!intf->add || !intf->remove)
		return -EINVAL;

	mutex_lock(&intf_mutex);

	list_add_tail(&intf->list, &intf_list);
	list_for_each_entry(priv, &dev_list, dev_list, struct mlx4_priv) {
		if (mlx4_add_device(intf, priv)) {
			err = -EFAULT;
			goto end;
		}
	}

end:
	mutex_unlock(&intf_mutex);
	return err;
}

void mlx4_unregister_interface(struct mlx4_interface *intf)
{
	struct mlx4_priv *priv;

	mutex_lock(&intf_mutex);

	list_for_each_entry(priv, &dev_list, dev_list, struct mlx4_priv)
		mlx4_remove_device(intf, priv);

	list_del(&intf->list);

	mutex_unlock(&intf_mutex);
}

void mlx4_dispatch_event(struct mlx4_dev *dev, enum mlx4_dev_event type,
			 int port)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_device_context *dev_ctx;

	spin_lock(&priv->ctx_lock);

	list_for_each_entry(dev_ctx, &priv->ctx_list, list, struct mlx4_device_context)
		if (dev_ctx->intf->event)
			dev_ctx->intf->event(dev, dev_ctx->context, type, port);

	spin_unlock(&priv->ctx_lock);
}

int mlx4_register_device(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_interface *intf;
	int err = 0;

	mutex_lock(&intf_mutex);

	list_add_tail(&priv->dev_list, &dev_list);
	list_for_each_entry(intf, &intf_list, list, struct mlx4_interface) {
		if (mlx4_add_device(intf, priv)) {
			list_del(&priv->dev_list);
			err = -EFAULT;
			goto end;
		}
	}

end:
	mutex_unlock(&intf_mutex);
	if (!err) {
		if (!mlx4_is_slave(dev))
			err = mlx4_start_catas_poll(dev);
	}

	return err;
}

void mlx4_unregister_device(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_interface *intf;

	if (!mlx4_is_slave(dev))
		mlx4_stop_catas_poll(dev);
	mutex_lock(&intf_mutex);

	list_for_each_entry(intf, &intf_list, list, struct mlx4_interface)
		mlx4_remove_device(intf, priv);

	list_del(&priv->dev_list);

	mutex_unlock(&intf_mutex);
}

void mlx4_intf_init()
{
	mutex_init(&intf_mutex);
}
