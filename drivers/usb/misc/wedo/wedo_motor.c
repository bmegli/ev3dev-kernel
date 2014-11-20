/*
 * Motor Definitions for LEGO WeDo
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

#include <linux/wedo/wedo_hub.h>
#include <linux/wedo/wedo_port.h>
#include <linux/wedo/wedo_motor.h>

unsigned wedo_get_supported_commands (void* context)
{
	return BIT(DC_MOTOR_COMMAND_RUN) | BIT(DC_MOTOR_COMMAND_COAST)
		| BIT(DC_MOTOR_COMMAND_BRAKE);
}

void wedo_update_output(struct wedo_port_device *wpd)
{
	struct wedo_hub_device *whd = to_wedo_hub_device (wpd->dev.parent);

	whd->event_callback (whd);
}

static enum wedo_motor_command to_wedo_motor_command[NUM_DC_MOTOR_COMMANDS] = {
	[DC_MOTOR_COMMAND_RUN]	= WEDO_MOTOR_COMMAND_RUN,
	[DC_MOTOR_COMMAND_COAST]= WEDO_MOTOR_COMMAND_COAST,
	[DC_MOTOR_COMMAND_BRAKE]= WEDO_MOTOR_COMMAND_BRAKE,
};

unsigned wedo_get_command (void* context)
{
 	struct wedo_motor_data *wmd = context;

	return wmd->command;
}

int wedo_set_command (void* context, unsigned command)
{
 	struct wedo_motor_data *wmd = context;

	if (wmd->command == command)
		return 0;

	wmd->command = command;

	if (NUM_DC_MOTOR_COMMANDS > command)
		wmd->wpd->command = to_wedo_motor_command[command];

	wedo_update_output( wmd->wpd );

	return 0;
}

static enum wedo_motor_polarity to_wedo_motor_polarity[NUM_DC_MOTOR_POLARITY] = {
	[DC_MOTOR_POLARITY_NORMAL]	= WEDO_MOTOR_POLARITY_NORMAL,
	[DC_MOTOR_POLARITY_INVERTED]	= WEDO_MOTOR_POLARITY_INVERTED,
};

unsigned wedo_get_polarity (void *context)
{
 	struct wedo_motor_data *wmd = context;

	return wmd->polarity;
}

int wedo_set_polarity (void *context, unsigned polarity)
{
 	struct wedo_motor_data *wmd = context;

	if (wmd->polarity == polarity)
		return 0;

	wmd->polarity = polarity;

	if (NUM_DC_MOTOR_POLARITY > polarity)
		wmd->wpd->polarity = to_wedo_motor_polarity[polarity];

	wedo_update_output( wmd->wpd );

	return 0;
}

int wedo_get_duty_cycle (void *context)
{
 	struct wedo_motor_data *wmd = context;

	return wmd->duty_cycle;
}

int wedo_set_duty_cycle (void *context, int duty_cycle)
{
 	struct wedo_motor_data *wmd = context;

	if ((duty_cycle > 100) || (duty_cycle < -100))
		return -EINVAL;

	if (wmd->duty_cycle == duty_cycle)
		return 0;

	wmd->duty_cycle = duty_cycle;
	wmd->wpd->duty_cycle = duty_cycle;

	wedo_update_output( wmd->wpd );

	return 0;
}

const struct dc_motor_ops wedo_motor_ops = {
	.get_supported_commands	= wedo_get_supported_commands,
	.get_command		= wedo_get_command,
	.set_command		= wedo_set_command,
	.get_polarity		= wedo_get_polarity,
	.set_polarity		= wedo_set_polarity,
	.get_duty_cycle		= wedo_get_duty_cycle,
	.set_duty_cycle		= wedo_set_duty_cycle,
	.context		= NULL,
};
