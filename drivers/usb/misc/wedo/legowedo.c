/*
 * USB Driver for LEGO WeDo
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

#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/wedo/wedo_port.h>
#include <linux/wedo/wedo_hub.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.1"
#define DRIVER_AUTHOR "Ralph Hempel <rhempel@hempeldesigngroup.com>"
#define DRIVER_DESC "USB WEDO Driver"
#define DRIVER_LICENSE "GPL"

#define WEDO_MINOR_BASE		0

/* table of devices that work with this driver */
static const struct usb_device_id wedo_table [] = {
	{ USB_DEVICE(0x0694, 0x0003) },
	{ },
};

MODULE_DEVICE_TABLE (usb, wedo_table);

/* Structure to hold all of our device specific stuff */
struct usb_wedo {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */

	struct wedo_hub_device  *whd;			/* the wedo hub class for this device */

	dma_addr_t		in_dma;
	unsigned char		*in_buf;		/* The read data buffer */
	struct urb		*in_urb;		/* the urb to read data with */

	struct usb_ctrlrequest	*cr;  			/* The USB Control Request */

	dma_addr_t		ctl_dma;
	unsigned char		*ctl_buf;		/* The control data buffer */
	struct urb		*ctl_urb;		/* the urb to write data with */

	spinlock_t		io_lock;		/* lock for io operations */

	bool			update_output;		/* Output module requested an output change */
	bool			output_pending;		/* Control URB has been submitted */
	bool			io_halt;		/* IO to the WeDo hub must stop */
};


static int wedo_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* The WeDo motor only has the 5V drive voltage of a USB port
 * and no ability to regulate the speed. The port accepts values
 * from 0 to 127 for output level, but the motor won't actually
 * start turning until the value is around 30.
 *
 * To make the power setting calculation simple, we'll set a
 * value of 127 for a duty cycle of 100, and 28 for a duty
 * cycle of 1 - there is no lower useful duty cycle.
 *
 * A duty cycle of 0 gets handled differently depending on what
 * the motor command is set to - brake forces the output to
 * 0x80 while coast or run forces the output to 0x00
 *
 * Note well that the power level to the motor is contained in
 * the lower 7 bits of the value that is sent to the hub. The
 * high bit is the direction when the motor is running and the
 * brake bit when the duty cycle is 0.
 */

static unsigned char wedo_update_output_value( struct wedo_port_device *wpd )
{
	int output;
	int duty_cycle = wpd->duty_cycle;

	switch (wpd->command) {

	case WEDO_MOTOR_COMMAND_COAST:
		output = 0x00;
		break;

	case WEDO_MOTOR_COMMAND_BRAKE:
		output = 0x80;
		break;

	case WEDO_MOTOR_COMMAND_RUN:
		if (0 == duty_cycle) {
			output = 0x00;
		}
		else if (0 < duty_cycle) {
			if (WEDO_MOTOR_POLARITY_NORMAL == wpd->polarity)
				output = duty_cycle + 27;
			else
				output = -duty_cycle - 27;
		}
		else if (0 > duty_cycle) {
			if (WEDO_MOTOR_POLARITY_NORMAL == wpd->polarity)
				output = duty_cycle - 27;
			else
				output = -duty_cycle + 27;
		}
		break;

	default:
		output =  0x00;
		break;
	}
	return (unsigned char)output;
}

static void wedo_in_callback (struct urb *urb)
{
	struct usb_wedo *dev = urb->context;
	int status = urb->status;
	struct wedo_hub_device *whd = dev->whd;
	unsigned long flags;

	if (status) {
		dev_dbg(&dev->udev->dev, "%s: nonzero status received: %d\n", __func__, status);
		goto err_in_urb;
	}

	/* No need to lock access to the input URB results - the wedo_hub_class
	 * is the only reader, and each byte of data is atomic.
	 */

	if (urb->actual_length == 8) {
		whd->from_hub.status.error	= dev->in_buf[0] & 0x80;
		whd->from_hub.status.high_power	= dev->in_buf[0] & 0x40;
		whd->from_hub.status.echo_bit	= dev->in_buf[0] & 0x01;

		whd->from_hub.voltage	= dev->in_buf[1];

		wedo_hub_update_status( whd );

		if (whd->wpd[WEDO_PORT_1]) {
			whd->wpd[WEDO_PORT_1]->input = dev->in_buf[2];
			whd->wpd[WEDO_PORT_1]->id    = dev->in_buf[3];

			if (0 == whd->wpd[WEDO_PORT_1]->duty_cycle) 
				wedo_port_update_status ( whd->wpd[WEDO_PORT_1] );
		}

		if (whd->wpd[WEDO_PORT_2]) {
			whd->wpd[WEDO_PORT_2]->input = dev->in_buf[4];
			whd->wpd[WEDO_PORT_2]->id    = dev->in_buf[5];

			if (0 == whd->wpd[WEDO_PORT_2]->duty_cycle) 
				wedo_port_update_status ( whd->wpd[WEDO_PORT_2] );
		}
	}

	if (dev->update_output && !dev->output_pending) {

		spin_lock_irqsave (&dev->io_lock, flags);

		dev->ctl_buf[0] = 0x00;

		dev->ctl_buf[0] |= (whd->to_hub.status.clear_error ? 0x80 : 0x00);
		dev->ctl_buf[0] |= (whd->to_hub.status.high_power  ? 0x40 : 0x20);
		dev->ctl_buf[0] |= (whd->to_hub.status.shut_down   ? 0x10 : 0x00);
		dev->ctl_buf[0] |= (whd->to_hub.status.reset	   ? 0x08 : 0x00);
		dev->ctl_buf[0] |= (whd->to_hub.status.echo_bit    ? 0x01 : 0x00);

		dev->ctl_buf[1] = wedo_update_output_value (whd->wpd[WEDO_PORT_1]);
		dev->ctl_buf[2] = wedo_update_output_value (whd->wpd[WEDO_PORT_2]);
		dev->ctl_buf[3] = 0x00;
		dev->ctl_buf[4] = 0x00;
		dev->ctl_buf[5] = 0x00;
		dev->ctl_buf[6] = 0x00;
		dev->ctl_buf[7] = 0x00;

		dev->update_output = false;
		dev->output_pending = true;

		spin_unlock_irqrestore (&dev->io_lock, flags);
		
		usb_submit_urb (dev->ctl_urb, GFP_ATOMIC);
	}

err_in_urb:
	usb_submit_urb (urb, GFP_ATOMIC);
}


static void wedo_ctrl_callback (struct urb *urb)
{
	struct usb_wedo *dev = urb->context;
	int status = urb->status;

	if (status) {
		dev_dbg(&dev->udev->dev, "%s: nonzero ctl status received: %d\n", __func__, status);
		goto err_ctrl_urb;
	}

	dev->output_pending = false;
	return;

err_ctrl_urb:
	usb_submit_urb (urb, GFP_ATOMIC);
	return;
}

void wedo_ctrl_event (struct wedo_hub_device *whd)
{
	struct usb_wedo *wd = whd->wd;
	unsigned long flags;

	spin_lock_irqsave (&wd->io_lock, flags);

	if (!wd->io_halt)
		wd->update_output = true;

	spin_unlock_irqrestore (&wd->io_lock, flags);

	return;
}

static const struct file_operations wedo_fops = {
	.owner =	THIS_MODULE,
	.release =	wedo_release,
};

static struct usb_class_driver wedo_usb_class = {
	.name =         "wedo%d",
	.fops =         &wedo_fops,
	.minor_base =   WEDO_MINOR_BASE,
};

static int wedo_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct wedo_hub_device *whd;
	struct usb_wedo *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Cannot allocate a usb_wedo device\n");
		goto err_alloc_usb_wedo;
	}

	/* allocate memory for our wedo hub device and initialize it */
	whd = kzalloc(sizeof(*whd), GFP_KERNEL);
	if (!whd) {
		dev_err(&interface->dev, "Cannot allocate a wedo_hub_device\n");
		goto err_alloc_wedo_hub;
	}

	dev->udev = usb_get_dev(interface_to_usbdev(interface));

	dev->interface = interface;

	dev->cr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!dev->cr)
		goto err_alloc_cr;

	dev->cr->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT;
	dev->cr->bRequest = HID_REQ_SET_REPORT;
	dev->cr->wValue = cpu_to_le16(0x0200);
	dev->cr->wIndex = cpu_to_le16(0x00);
	dev->cr->wLength = cpu_to_le16(0x08);

	dev->ctl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->ctl_urb)
		goto err_alloc_ctl_urb;

	dev->ctl_buf = usb_alloc_coherent(dev->udev, 8, GFP_KERNEL, &dev->ctl_dma);
	if (!dev->ctl_buf)
		goto err_alloc_ctl_buf;

        usb_fill_control_urb(dev->ctl_urb, dev->udev, usb_sndctrlpipe(dev->udev, 0), (void *) dev->cr,
			dev->ctl_buf, 8, wedo_ctrl_callback, dev);

	dev->ctl_urb->transfer_dma = dev->ctl_dma;
	dev->ctl_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */

	dev_err(&interface->dev, "bNumConfigurations is %d\n", dev->udev->descriptor.bNumConfigurations);

	dev_err(&interface->dev, "num_altsettings is %d\n", interface->num_altsetting);

	iface_desc = interface->cur_altsetting;

	dev_err(&interface->dev, "bNumEndpoints is %d\n", iface_desc->desc.bNumEndpoints);

	/* We send data to the LEGO WEDO on Endpoint 0 (ctl) and receive data
	 * from endpoint 1 (in) - there's no sense looping through the endpoints
	 * but it makes sense to check that the first endpoint is an interrupt
	 * type, and that it's an input
	 *
	 * The control endpoint is available on every USB device, the first
	 * enumerated endpoint on the interface (array index 0) is endpoint 1
	 */

	endpoint = &iface_desc->endpoint[0].desc;

	if (!usb_endpoint_xfer_int(endpoint) && !usb_endpoint_dir_in(endpoint))
		goto err_no_int_in_endpoint;

	dev_err(&interface->dev, "Found xfer_int endpoint - num %d interval %d\n",
			usb_endpoint_num(endpoint),
			endpoint->bInterval);

	dev->in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->in_urb)
		goto err_alloc_in_urb;

	dev->in_buf = usb_alloc_coherent(dev->udev, 8, GFP_KERNEL, &dev->in_dma);
	if (!dev->in_buf)
		goto err_alloc_in_buf;

	usb_fill_int_urb(dev->in_urb, dev->udev,
			 usb_rcvintpipe(dev->udev, endpoint->bEndpointAddress),
			 dev->in_buf, 8, wedo_in_callback, dev, 32 );

	dev->in_urb->transfer_dma = dev->in_dma;
	dev->in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &wedo_usb_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto err_usb_register_dev;
	}

	retval = register_wedo_hub(whd, &interface->dev);

	dev_err(&interface->dev,
			"Interface->dev %08x usb_wedo %08x whd->wd %08x\n", &interface->dev, dev, whd->wd );

	if (retval) {
		/* something prevented us from registering the wedo hub class */
		dev_err(&interface->dev,
			"Not able to register the wedo_hub.\n");
		goto err_register_wedo_hub;
	}

	whd->wd = dev;

	dev->whd = whd;

	spin_lock_init( &dev->io_lock );

	whd->event_callback = wedo_ctrl_event;

	dev_info(&interface->dev, "--- whd %08x callback ptr to %08x <- %08x ---\n", whd, whd->event_callback, wedo_ctrl_event );

	dev_info(&interface->dev, "--- New Device Summary ---");
	dev_info(&interface->dev, "device  init_name -> %s", interface->dev.init_name);
	dev_info(&interface->dev, "usb_dev init_name -> %s", interface->usb_dev->init_name);

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "LEGO WEDO Hub #%d now attached to wedo",
		 interface->minor);

	retval = usb_submit_urb( dev->in_urb, GFP_ATOMIC);

	return 0;

err_register_wedo_hub:
	usb_deregister_dev(interface, &wedo_usb_class);
err_usb_register_dev:
	usb_free_coherent (dev->udev, 8, dev->in_buf, dev->in_dma);
err_alloc_in_buf:
	usb_free_urb (dev->in_urb);
err_alloc_in_urb:
err_no_int_in_endpoint:
	usb_free_coherent (dev->udev, 8, dev->ctl_buf, dev->ctl_dma);
err_alloc_ctl_buf:
	usb_free_urb (dev->ctl_urb);
err_alloc_ctl_urb:
	kfree (dev->cr);
err_alloc_cr:
	usb_put_dev (dev->udev);
	kfree (whd);
err_alloc_wedo_hub:
	kfree (dev);
err_alloc_usb_wedo:
	return retval;
}

static void wedo_disconnect(struct usb_interface *interface)
{
	struct usb_wedo *dev;
	int minor = interface->minor;
	unsigned long flags;

	dev = usb_get_intfdata(interface);

	spin_lock_irqsave (&dev->io_lock, flags);
	dev->io_halt = true;
	spin_unlock_irqrestore (&dev->io_lock, flags);

	do {
	} while (dev->update_output || dev->output_pending);

	unregister_wedo_hub( dev->whd );

	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &wedo_usb_class);

	/* prevent more I/O from starting */
//	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
//	mutex_unlock(&dev->io_mutex);

//	dev_info(&interface->dev, "Killing URBs");
//	usb_kill_anchored_urbs(&dev->submitted);

	dev_info(&interface->dev, "Freeing inbuf and URB");
	usb_free_coherent (dev->udev, 8, dev->in_buf, dev->in_dma);
	usb_free_urb (dev->in_urb);
	dev_info(&interface->dev, "Freeing outbuf and URB");
	usb_free_coherent (dev->udev, 8, dev->ctl_buf, dev->ctl_dma);
	usb_free_urb (dev->ctl_urb);

	dev_info(&interface->dev, "Killing URBs");
//	usb_kill_anchored_urbs(&dev->submitted);

	dev_info(&interface->dev, "Freeing cr");
	kfree (dev->cr);
	dev_info(&interface->dev, "putting device");
	usb_put_dev (dev->udev);
	dev_info(&interface->dev, "Freeing whd");
	kfree (dev->whd);
	dev_info(&interface->dev, "Freeing dev");
	kfree (dev);

	dev_info(&interface->dev, "LEGO WEDO Hub #%d now disconnected", minor);
}

static void wedo_draw_down(struct usb_wedo *dev)
{
	int time;

//	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
//	if (!time)
//		usb_kill_anchored_urbs(&dev->submitted);
//	usb_kill_urb(dev->bulk_in_urb);
}

static int wedo_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_wedo *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	wedo_draw_down(dev);
	return 0;
}

static int wedo_resume(struct usb_interface *intf)
{
	return 0;
}

// static int wedo_pre_reset(struct usb_interface *intf)
// {
// 	struct usb_wedo *dev = usb_get_intfdata(intf);
// 
// 	mutex_lock(&dev->io_mutex);
// 	wedo_draw_down(dev);
// 
// 	return 0;
// }

// static int wedo_post_reset(struct usb_interface *intf)
// {
// 	struct usb_wedo *dev = usb_get_intfdata(intf);
// 
// 	/* we are sure no URBs are active - no locking needed */
// 	dev->errors = -EPIPE;
// 	mutex_unlock(&dev->io_mutex);
// 
// 	return 0;
// }

static struct usb_driver wedo_driver = {
	.name =		"wedo_usb",
	.probe =	wedo_probe,
	.disconnect =	wedo_disconnect,
	.suspend =	wedo_suspend,
	.resume =	wedo_resume,
//	.pre_reset =	wedo_pre_reset,
//	.post_reset =	wedo_post_reset,
	.id_table =	wedo_table,
	.supports_autosuspend = 1,
};

static int __init usb_wedo_init(void)
{
	int retval = 0;

        retval = usb_register (&wedo_driver);
	if (retval) 
		goto err_usb_register;

	retval = bus_register (&wedo_bus_type);
	if (retval) 
		goto err_bus_register;

	return 0;

err_bus_register:
        usb_deregister(&wedo_driver);
err_usb_register:
	return retval;
}

static void __exit usb_wedo_exit(void)
{
        bus_unregister (&wedo_bus_type);
        usb_deregister (&wedo_driver);
}

module_init(usb_wedo_init);
module_exit(usb_wedo_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE(DRIVER_LICENSE);
