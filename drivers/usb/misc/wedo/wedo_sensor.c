/*
 * Sensor Definitions for LEGO WeDo
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

#include "linux/wedo/wedo_sensor.h"
#include "linux/wedo/wedo_hub.h"

static void wedo_raw_cb(void *context)
{
 	struct wedo_sensor_data *wsd = context;

	wsd->ms.mode_info[wsd->mode].raw_data[0] = wsd->wpd->input;
}

enum wedo_tilt_status_id {
	WEDO_TILT_STATUS_UNKNOWN,
	WEDO_TILT_STATUS_BACK,
	WEDO_TILT_STATUS_RIGHT,
	WEDO_TILT_STATUS_LEVEL,
	WEDO_TILT_STATUS_FRONT,
	WEDO_TILT_STATUS_LEFT,
	WEDO_TILT_STATUS_MAX,
};

struct wedo_tilt_status_info {
	unsigned char max;
	unsigned char *name;
};

/* The max fields in this table must be in ascending order for the
 * state calculation to work
 */

static const struct wedo_tilt_status_info wedo_tilt_status_infos[] = {
	[WEDO_TILT_STATUS_UNKNOWN]	= {   0, "unknown" },
	[WEDO_TILT_STATUS_BACK]		= {  48, "back"    },
	[WEDO_TILT_STATUS_RIGHT]	= {  99, "right"   },
	[WEDO_TILT_STATUS_LEVEL]	= { 153, "level"   },
	[WEDO_TILT_STATUS_FRONT]	= { 204, "front"   },
	[WEDO_TILT_STATUS_LEFT]		= { 255, "left"    },
};

#define WEDO_TILT_STATUS_DEBOUNCE 4

static enum wedo_tilt_status_id wedo_update_tilt_status( struct wedo_sensor_data *wsd )
{
	enum wedo_tilt_status_id id;
	int  rawval = wsd->wpd->input;

	for (id=0; id<WEDO_TILT_STATUS_MAX; ++id )
		if ( rawval <= wedo_tilt_status_infos[id].max )
			break;

	if (id != wsd->debounce_status) {
		wsd->debounce_count = 0;
		wsd->debounce_status = id;
	}
	else if (WEDO_TILT_STATUS_DEBOUNCE > wsd->debounce_count ) {
		wsd->debounce_count++;
	}
	else if (WEDO_TILT_STATUS_DEBOUNCE == wsd->debounce_count ) {
		/* Here's where we'd schedule a notification task */
		wsd->debounce_count++;
		wsd->status = id;
	} 	

	return wsd->status;
}

static void wedo_tilt_axis_cb(void *context)
{
 	struct wedo_sensor_data *wsd = context;
	
	switch (wedo_update_tilt_status (wsd))
	{
	case WEDO_TILT_STATUS_BACK:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 0;
		wsd->ms.mode_info[wsd->mode].raw_data[1] = -1;
		break;

	case WEDO_TILT_STATUS_RIGHT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 1;
		wsd->ms.mode_info[wsd->mode].raw_data[1] = 0;
		break;

	case WEDO_TILT_STATUS_FRONT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 0;
		wsd->ms.mode_info[wsd->mode].raw_data[1] = 1;
		break;

	case WEDO_TILT_STATUS_LEFT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = -1;
		wsd->ms.mode_info[wsd->mode].raw_data[1] = 0;
		break;

	case WEDO_TILT_STATUS_LEVEL:
	case WEDO_TILT_STATUS_UNKNOWN:
	default:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 0;
		wsd->ms.mode_info[wsd->mode].raw_data[1] = 0;
		break;
	}
}

static void wedo_tilt_status_cb(void *context)
{
 	struct wedo_sensor_data *wsd = context;
	
	switch (wedo_update_tilt_status (wsd))
	{
	case WEDO_TILT_STATUS_BACK:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 2;
		break;

	case WEDO_TILT_STATUS_RIGHT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 4;
		break;

	case WEDO_TILT_STATUS_FRONT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 1;
		break;

	case WEDO_TILT_STATUS_LEFT:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 3;
		break;

	case WEDO_TILT_STATUS_LEVEL:
	case WEDO_TILT_STATUS_UNKNOWN:
	default:
		wsd->ms.mode_info[wsd->mode].raw_data[0] = 0;
		break;
	}
}

const struct wedo_sensor_info wedo_sensor_defs[] = {
	[WEDO_TILT_SENSOR] = {
 		/**
 		 * @vendor_part_name: LEGO WeDo Tilt Sensor
 		 */
 		.name = "wedo-tilt",
 		.num_modes = 3,
 		.ms_mode_info = {
 			[0] = {
 				/**
 				 * @description: Raw analog value
 				 * @value0: Tilt (0 - 255)
				 * @units_description: Tilt
				 */
 				.name = "WEDO-TILT-RAW",
 				.units = "",
 				.raw_max = 255,
 				.si_max = 255,
 				.data_sets = 1,
 				.data_type = MSENSOR_DATA_U8,
 			},
 			[1] = {
 				/**
 				 * @description: Tilt around 2 separate axes
 				 * @value0: Tilt left/level/right (-1/0/1)
 				 * @value1: Tilt back/level/front (-1/0/1)
				 * @units_description: Tilt
				 */
 				.name = "WEDO-TILT-AXIS",
 				.units = "",
 				.raw_min = -1,
 				.raw_max =  1,
 				.si_min  = -1,
 				.si_max  =  1,
 				.data_sets = 2,
 				.data_type = MSENSOR_DATA_S8,
 			},
 			[2] = {
 				/**
 				 * @description: Tilt status for all axes
 				 * @value0: Tilt level/front/back/left/right (0/1/2/3/4)
				 * @units_description: Tilt
				 */
 				.name = "WEDO-TILT-STATUS",
 				.units = "",
 				.raw_max = 4,
 				.si_max = 4,
 				.data_sets = 1,
 				.data_type = MSENSOR_DATA_U8,
 			},
		},
 		.wedo_mode_info = {
 			[0] = {
 				.analog_cb = wedo_raw_cb,
 			},
 			[1] = {
 				.analog_cb = wedo_tilt_axis_cb,
 			},
 			[2] = {
 				.analog_cb = wedo_tilt_status_cb,
 			},
		}
 	},
	[WEDO_MOTION_SENSOR] = {
 		/**
 		 * @vendor_part_name: LEGO WeDo Motion Sensor
 		 */
 		.name = "wedo-motion",
 		.num_modes = 1,
 		.ms_mode_info = {
 			[0] = {
 				/**
 				 * @description: Raw analog value
 				 * @value0: Motion (0 - 255)
				 * @units_description: Motion
				 */
 				.name = "WEDO-MOTION-RAW",
 				.units = "",
 				.raw_max = 255,
 				.si_max = 255,
 				.data_sets = 1,
 				.data_type = MSENSOR_DATA_U8,
 			},
		},
 		.wedo_mode_info = {
 			[0] = {
 				.analog_cb = wedo_raw_cb,
 			},
		}
 	},
};

