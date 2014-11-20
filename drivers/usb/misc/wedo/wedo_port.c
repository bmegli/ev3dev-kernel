/*
 * Port functions for LEGO WeDo
 *
 * Copyright (C) 2014 Ralph Hempel <rhemple@hempeldesigngroup.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/slab.h>

#include <linux/wedo/wedo_hub.h>
#include <linux/wedo/wedo_port.h>
#include <linux/wedo/wedo_sensor.h>
#include <linux/wedo/wedo_motor.h>

/*
 * -----------------------------------------------------------------------------
 * This file provides components for interfacing with the input and output
 * ports on the LEGO WeDo USB brick.
 *
 * Each port has its own device node.
 * -----------------------------------------------------------------------------
 */

struct wedo_id_info {
	unsigned char max;
	unsigned char *name;
};

/* The max fields in this table must be in ascending order for the
 * state calculation to work
 */

const struct wedo_id_info wedo_id_infos[] = {
	[WEDO_TYPE_SHORTLO]	= {   9 , "shortlo"	},
	[WEDO_TYPE_BEND]	= {  27 , "bend"	},
	[WEDO_TYPE_TILT]	= {  47 , "tilt"	},
	[WEDO_TYPE_FUTURE]	= {  67 , "future"	},
	[WEDO_TYPE_RAW]		= {  87 , "raw"		},
	[WEDO_TYPE_TOUCH]	= { 109 , "touch"	},
	[WEDO_TYPE_SOUND]	= { 131 , "sound"	},
	[WEDO_TYPE_TEMP]	= { 152 , "temp"	},
	[WEDO_TYPE_LIGHT]	= { 169 , "light"	},
	[WEDO_TYPE_MOTION]	= { 190 , "motion"	},
	[WEDO_TYPE_LIGHTBRICK]	= { 211 , "lightbrick"	},
	[WEDO_TYPE_22]		= { 224 , "22"		},
	[WEDO_TYPE_OPEN]	= { 233 , "open"	},
	[WEDO_TYPE_MOTOR]	= { 246 , "motor"	},
	[WEDO_TYPE_SHORTHI] 	= { 255 , "shorthi"	},
};

/*
 * These functions handle registering msensor devices on WeDo ports
 * as well as the mode callbacks
 */

static u8 wedo_sensor_get_mode(void *context)
{
	struct wedo_sensor_data *wsd = context;

	return wsd->mode;
}

static int wedo_sensor_set_mode(void *context, u8 mode)
{
	struct wedo_sensor_data *wsd = context;

	if (mode >= wsd->info.num_modes)
		return -EINVAL;

	wsd->mode = mode;

	return 0;
}

static int register_wedo_sensor (struct wedo_port_device *wpd, enum wedo_sensor_types type)
{
	struct wedo_sensor_data *wsd = dev_get_drvdata(&wpd->dev);
	int err;

	if (wsd)
		return -EINVAL;

	wsd = kzalloc(sizeof(struct wedo_sensor_data), GFP_KERNEL);
	if (!wsd)
		return -ENOMEM;

	wsd->wpd = wpd;

	memcpy(&wsd->info, &wedo_sensor_defs[type], sizeof(struct wedo_sensor_info));

	strncpy(wsd->ms.name, wsd->info.name, MSENSOR_NAME_SIZE);
	strncpy(wsd->ms.port_name, dev_name(&wpd->dev),	MSENSOR_NAME_SIZE);

 	dev_info(&wpd->dev, "name %s port_name %s\n", wsd->ms.name, wsd->ms.port_name );

	wsd->ms.num_modes	= wsd->info.num_modes;
	wsd->ms.mode_info	= wsd->info.ms_mode_info;
	wsd->ms.get_mode	= wedo_sensor_get_mode;
	wsd->ms.set_mode	= wedo_sensor_set_mode;

	wsd->ms.context		= wsd;

	err = register_msensor(&wsd->ms, &wpd->dev);
	if (err)
		goto err_register_msensor;

	dev_set_drvdata(&wpd->dev, wsd);

	wedo_sensor_set_mode(wsd, 0);

	return 0;

err_register_msensor:
	kfree(wsd);

	return err;	
}

static void unregister_wedo_sensor (struct wedo_port_device *wpd)
{
	struct wedo_sensor_data *wsd = dev_get_drvdata(&wpd->dev);

	if (!wsd)
		return;

	unregister_msensor(&wsd->ms);
	dev_set_drvdata(&wpd->dev, NULL);
	kfree(wsd);
}

/*
 * These functions handle registering dc_motor devices on WeDo ports
 */

static int register_wedo_motor (struct wedo_port_device *wpd)
{
	struct wedo_motor_data *wmd = dev_get_drvdata(&wpd->dev);
	int err;

	if (wmd)
		return -EINVAL;

	wmd = kzalloc(sizeof(struct wedo_motor_data), GFP_KERNEL);
	if (!wmd)
		return -ENOMEM;

	wmd->wpd = wpd;

	strncpy(wmd->md.name, "wedo-motor", DC_MOTOR_NAME_SIZE);
	strncpy(wmd->md.port_name, dev_name(&wpd->dev), DC_MOTOR_NAME_SIZE);
	
	memcpy(&wmd->md.ops, &wedo_motor_ops, sizeof(struct dc_motor_ops));

	wmd->md.ops.context = wmd;

	err = register_dc_motor(&wmd->md, &wpd->dev);
	if (err)
		goto err_register_dc_motor;

	dev_set_drvdata(&wpd->dev, wmd);

	return 0;

err_register_dc_motor:
	kfree(wmd);

	return err;
}

static void unregister_wedo_motor (struct wedo_port_device *wpd)
{
	struct wedo_motor_data *wmd = dev_get_drvdata(&wpd->dev);

	if (!wmd)
		return;

	unregister_dc_motor(&wmd->md);
	dev_set_drvdata(&wpd->dev, NULL);
	kfree(wmd);
}

/*
 * These functions handle registering devices on WeDo ports.
 *
 * There are only two generic types if devices that we handle:
 *
 * Input device ids get registered as msensor class devices
 * Output device ids get registered as dc_motor class devices
 * 
 * Currently we only have the tilt and motion sensors for testing
 */

static int register_wedo_device (struct wedo_port_device *wpd, enum wedo_type_id id)
{
	int err = -EINVAL;

	wpd->type_id = id;

	switch( wpd->type_id ) {

	case WEDO_TYPE_TILT:
		err = register_wedo_sensor (wpd, WEDO_TILT_SENSOR);
		break;
	case WEDO_TYPE_MOTION:
		err = register_wedo_sensor (wpd, WEDO_MOTION_SENSOR);
		break;
	case WEDO_TYPE_MOTOR:
		err = register_wedo_motor (wpd);
		break;
	default:
		break;
	}

	return err;
}

static void unregister_wedo_device (struct wedo_port_device *wpd)
{	
	switch( wpd->type_id ) {

	case WEDO_TYPE_TILT:
	case WEDO_TYPE_MOTION:
		unregister_wedo_sensor( wpd );
		break;
	case WEDO_TYPE_MOTOR:
		unregister_wedo_motor (wpd);
		break;
	default:
		break;
	}
}

/*
 * Finally, we're at the public driver functions that register the WeDo
 * port devices for each hub.
 */

static void wedo_port_release(struct device *dev)
{
}

struct wedo_port_device *register_wedo_port(unsigned port_num, struct wedo_hub_device *whd)
{
	int err;
	struct wedo_port_device *wpd;

	#warning "Add a mechanism here for setting the port name"

	if (WEDO_PORT_MAX <= port_num)
		return ERR_PTR(-EINVAL);

	/* allocate memory for our new port_device, and initialize it */
	wpd = kzalloc(sizeof(struct wedo_port_device), GFP_KERNEL);
	if (!wpd)
		return ERR_PTR(-ENOMEM);

 	wpd->dev.release = wedo_port_release;
 	wpd->dev.parent = &whd->dev;
 	dev_set_name(&wpd->dev, "port%d", port_num);

 	err = device_register(&wpd->dev);
 	if (err) {
		dev_err(&wpd->dev, "Failed to register device.\n");
 		goto err_wedo_port_register;
	}

	return wpd;

err_wedo_port_register:
	kfree(wpd);

	return ERR_PTR(err);
}

void unregister_wedo_port(struct wedo_port_device *wpd)
{
	if (!wpd)
		return;

	unregister_wedo_device( wpd );
 	device_unregister(&wpd->dev);
	kfree(wpd);
}

/* Here's where we update the status of the devices connected to the
 * LEGO WeDo hub - this function is called when the wedo driver has
 * received a complete packet
 *
 * NOTE: only process ID changes if the output value is 0x00 or 0x80
 */
#define WEDO_PORT_TYPE_DEBOUNCE 	100

void wedo_port_update_status(struct wedo_port_device *wpd)
{
	int err = 0;
	enum wedo_type_id id;

	struct wedo_sensor_data *wsd = NULL;
	struct wedo_motor_data  *wmd = NULL;

	switch( wpd->type_id ) {

	case WEDO_TYPE_TILT:
	case WEDO_TYPE_MOTION:
		wsd = dev_get_drvdata(&wpd->dev);

		if (wsd) {
			if (wsd->info.wedo_mode_info[wsd->mode].analog_cb) {
				wsd->info.wedo_mode_info[wsd->mode].analog_cb (wsd);
			}
		}

		break;
	case WEDO_TYPE_MOTOR:
		wmd = dev_get_drvdata(&wpd->dev);
		break;
	default:
		break;
	}

	for (id=0; id<WEDO_TYPE_MAX; ++id )
		if ( wpd->id <= wedo_id_infos[id].max )
			break;

	if (id != wpd->temp_type_id) {
		wpd->type_debounce = 0;
		wpd->temp_type_id = id;
	 	dev_info(&wpd->dev, "Reset ID debounce raw %03d type %02d\n", wpd->id, id );
	}
	else if (WEDO_PORT_TYPE_DEBOUNCE > wpd->type_debounce ) {
		wpd->type_debounce++;
	}
	else if (WEDO_PORT_TYPE_DEBOUNCE == wpd->type_debounce ) {

	 	dev_info(&wpd->dev, "Unregistering device type_id %d from '%s'\n", wpd->type_id, dev_name(&wpd->dev));

		unregister_wedo_device( wpd );

	 	dev_info(&wpd->dev, "Registering device type_id %d to '%s'\n", id, dev_name(&wpd->dev));

		err = register_wedo_device( wpd, id );

		#warning "Do we need to handle an error here? No alloc to free up - failure is no-nothing"

		wpd->type_debounce++;
	}
}
