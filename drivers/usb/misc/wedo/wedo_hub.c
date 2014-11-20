/*
 * Hub functions for LEGO WeDo
 *
 * Copyright (C) 2014 Ralph Hempel <rhemple@hempeldesigngroup.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Version Information
 */

/*
 * -----------------------------------------------------------------------------
 * This file provides components for interfacing with a LEGO WeDo hub as a
 * new bus type
 * -----------------------------------------------------------------------------
 */

#include <linux/device.h>

#include <linux/wedo/wedo_hub.h>
#include <linux/wedo/wedo_port.h>

static ssize_t clear_error_store(struct device *dev,
				 struct device_attribute *attr,
			  	 const char *buf, size_t count)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value < 0 || value > 1)
		return -EINVAL;
	whd->to_hub.status.clear_error = value;
	return count;
}
 
static ssize_t high_power_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value < 0 || value > 1)
		return -EINVAL;
	whd->to_hub.status.high_power = value;

	return count;
}

static ssize_t high_power_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);

	return sprintf(buf, "%d\n", whd->from_hub.status.high_power);
}

static ssize_t shut_down_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value < 0 || value > 1)
		return -EINVAL;
	whd->to_hub.status.shut_down = value;

	return count;
}

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);
	int value;

	if (sscanf(buf, "%d", &value) != 1 || value < 0 || value > 1)
		return -EINVAL;
	whd->to_hub.status.reset = value;

	return count;
}

static ssize_t status_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);

	return sprintf(buf, "%d\n", whd->from_hub.status);
}

static ssize_t voltage_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct wedo_hub_device *whd = to_wedo_hub_device(dev);

	return sprintf(buf, "%d %d\n", whd->from_hub.voltage, whd->from_hub.voltage * 49);
}

static DEVICE_ATTR_WO(clear_error);
static DEVICE_ATTR_RW(high_power);
static DEVICE_ATTR_WO(shut_down);
static DEVICE_ATTR_WO(reset);

static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RO(voltage);

static struct attribute *wedo_hub_device_attrs[] = {
	&dev_attr_clear_error.attr,
	&dev_attr_high_power.attr,
	&dev_attr_shut_down.attr,
	&dev_attr_reset.attr,
	&dev_attr_status.attr,
	&dev_attr_voltage.attr,
	NULL
};

static struct attribute_group wedo_hub_attribute_group = {
	.attrs = wedo_hub_device_attrs
};

void wedo_hub_update_status(struct wedo_hub_device *whd)
{
}

static int wedo_bus_match(struct device *dev, struct device_driver *drv)
{
	/* otherwise regular name matching using type name */
	return !strcmp(dev->type->name, drv->name);
}

static int wedo_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return 0;
}

struct bus_type wedo_bus_type = {
	.name		= "wedo",
	.match		= wedo_bus_match,
	.uevent		= wedo_bus_uevent,
};

static void wedo_hub_release(struct device *dev)
{
}

static unsigned wedo_hub_id = 0;

int register_wedo_hub(struct wedo_hub_device *whd, struct device *parent)
{
	int err;
	int i = 0;

//	if (!whd || !whd->port_name || !parent)
#warning "Does the whd->port_name need to get set?"
	if (!whd ||                    !parent)
		return -EINVAL;

 	whd->dev.release = wedo_hub_release;
 	whd->dev.parent = parent;
 	whd->dev.bus = &wedo_bus_type;
#warning "Does the hub id need to get set as a fixed value from a pool?"
 	dev_set_name(&whd->dev, "hub%d", wedo_hub_id++);
 
 	err = device_register(&whd->dev);
 	if (err)
 		return err;
 
 	dev_info(&whd->dev, "Bound   '%s' to '%s'\n", dev_name(&whd->dev), dev_name(parent));

	err = sysfs_create_group(&whd->dev.kobj, &wedo_hub_attribute_group);
	if (err)
		goto err_sysfs_create_group;

	do {
		whd->wpd[i] = register_wedo_port( i, whd );
		if (IS_ERR(whd->wpd[i])) {
			err = PTR_ERR(whd->wpd[i]);
			goto err_register_wedo_ports;
		}

	} while (++i < WEDO_PORT_MAX);

 	return 0;

err_register_wedo_ports:
	while (i--)
		unregister_wedo_port(whd->wpd[i]);

	sysfs_remove_group(&whd->dev.kobj, &wedo_hub_attribute_group);
err_sysfs_create_group:
	device_unregister(&whd->dev);
	return err;
}

void unregister_wedo_hub(struct wedo_hub_device *whd)
{
	int i;

	for (i=0; i<WEDO_PORT_MAX; ++i) {
		unregister_wedo_port(whd->wpd[i]);
	};

	sysfs_remove_group(&whd->dev.kobj, &wedo_hub_attribute_group);

	dev_info(&whd->dev, "Unregistered\n");
	device_unregister(&whd->dev);
}
