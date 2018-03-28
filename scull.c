/*
    Copyright: Fraser Cadger (2018) <frasercadger@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Description: Simple Character Utiltity for Loading Localities (scull)
    driver.
*/

/*
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

/* Includes */
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "scull.h"

/* Local constants */
static const char *scull_name = "scull";
/* Currently we're only supporting scull0-scull3 */
static const unsigned int scull_device_count = 4;

/* Static globals */
static int scull_major;
static int scull_minor = 0;
static struct scull_dev *my_scull_dev;
/* XXX: Add function pointers as we go */
static struct file_operations scull_fops = {
	.owner =	THIS_MODULE,
	.open =		scull_open,
	.release =	scull_release,
};

/* Function prototypes */

/* Function definitions */

/* Cleanup function that doubles as module exit function */
static void scull_cleanup(void)
{
	dev_t devno = MKDEV(scull_major, scull_minor);

	if(my_scull_dev)
	{
		/* Free any memory allocated to scull devices */
		kfree(my_scull_dev);
		/* Unregister char devices */
		cdev_del(&my_scull_dev->cdev);
	}

	/* Unregister device number allocation */
	unregister_chrdev_region(devno, scull_device_count);

}

static int __init scull_init(void)
{
	int retval, devno;
	dev_t dev;

	/* Get major and minor numbers via dynamic allocation */
	retval = alloc_chrdev_region(&dev, scull_minor,
				     scull_device_count, scull_name);
	if(retval < 0)
	{
		/* Something went wrong */
		printk(KERN_ERR "Dynamic allocation of major number error\n");
		return retval;
	}

	scull_major = MAJOR(dev);

	/* Register char devices */
	/* XXX: For initial testing register 1 device */

	/* Allocate memory for devices */
	my_scull_dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
	if(!my_scull_dev)
	{
		/* Memory allocation failed */
		printk(KERN_ERR "Error allocating memory for scull device\n");
		retval = -ENOMEM;
		goto fail;
	}
	/* Need to zero allocated memory */
	memset(my_scull_dev, 0, sizeof(struct scull_dev));

	/* Prepare cdev structure */
	cdev_init(&my_scull_dev->cdev, &scull_fops);
	my_scull_dev->cdev.owner = THIS_MODULE;
	my_scull_dev->cdev.ops = &scull_fops;
	devno = MKDEV(scull_major, scull_minor);

	/* Register device */
	retval = cdev_add(&my_scull_dev->cdev, devno, 1);

	/* Check if registration was successful */
	if(retval)
	{
		printk(KERN_ERR "Error: %d adding scull device\n", retval);
		goto fail;
	}

	/* Error handling */
fail:
	/* Cleanup */
	scull_cleanup();
	return retval;

	return 0;
}
module_init(scull_init);
module_exit(scull_cleanup);
