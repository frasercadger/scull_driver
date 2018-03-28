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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "scull.h"

/* Local constants */
static const char *scull_name = "scull";
/* Currently we're only supporting scull0-scull3 */
static const unsigned int scull_dev_count = 4;
#define DEFAULT_SCULL_QUANTUM 4000
#define DEFAULT_SCULL_QSET 1000

/* Static globals */
static int scull_major;
static int scull_minor = 0;
static struct scull_dev *scull_devs;
/* XXX: Add function pointers as we go */
static struct file_operations scull_fops = {
	.owner =	THIS_MODULE,
	.open =		scull_open,
	.release =	scull_release,
};
static int scull_quantum = DEFAULT_SCULL_QUANTUM;
static int scull_qset = DEFAULT_SCULL_QSET;

/* Function prototypes */
static void scull_cleanup(void);
static int scull_register_cdev(struct scull_dev *dev, int minor);
static int __init scull_init(void);

/* Function definitions */

int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	/* Find out which device we're dealing with */
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	/* Add scull_dev pointer to struct file so other functions can identify
	 * dev.
	 * */
	filp->private_data = dev;

	/* Trim file length to 0 if open was write only */
	if((filp->f_flags & O_ACCMODE) == O_WRONLY)
	{
		/* TODO: Need to implement scull_trim() function */
	}

	return 0;
}

int scull_release(struct inode *inode, struct file *filp)
{
	/* XXX: LDD Chapter 3 has this function empty, so I'll stick with that for
	 * now.
	 */
	return 0;
}

/* Cleanup function that doubles as module exit function */
static void scull_cleanup(void)
{
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);

#if SCULL_DEBUG
	printk(KERN_INFO "Scull cleanup/exit\n");
#endif

	if(scull_devs)
	{
		for(i = 0; i < scull_dev_count; ++i)
		{
			/* Unregister char devices */
			cdev_del(&scull_devs[i].cdev);
		}

		/* Free any memory allocated to scull devices */
		kfree(scull_devs);
	}

	/* Unregister device number allocation */
	unregister_chrdev_region(devno, scull_dev_count);
}

static int scull_register_cdev(struct scull_dev *dev, int minor)
{
	int retval, devno;
	/* Prepare cdev structure */
	cdev_init(&dev->cdev, &scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &scull_fops;

	/* Register device */
	devno = MKDEV(scull_major, minor);
	retval = cdev_add(&dev->cdev, devno, 1);

	return retval;
}

static int __init scull_init(void)
{
	int retval, i;
	dev_t dev;

#if SCULL_DEBUG
	printk(KERN_INFO "Intialising scull device\n");
#endif

	/* Get major and minor numbers via dynamic allocation */
	retval = alloc_chrdev_region(&dev, scull_minor,
				     scull_dev_count, scull_name);
	if(retval < 0)
	{
		/* Something went wrong */
		printk(KERN_ERR "Dynamic allocation of major number error\n");
		return retval;
	}

	scull_major = MAJOR(dev);

#if SCULL_DEBUG
	printk(KERN_INFO "Allocated major number: %d\n", scull_major);
#endif

	/* Register char devices */

	/* Allocate memory for devices */
	scull_devs = kmalloc(scull_dev_count * sizeof(struct scull_dev),
			     GFP_KERNEL);
	if(!scull_devs)
	{
		/* Memory allocation failed */
		printk(KERN_ERR "Error allocating memory for scull device\n");
		retval = -ENOMEM;
		goto fail;
	}

#if SCULL_DEBUG
	printk(KERN_INFO "Device memory allocation successful\n");
#endif

	/* Need to zero allocated memory */
	memset(scull_devs, 0, sizeof(struct scull_dev));

	/* Initialize all devices */
	for(i = 0; i < scull_dev_count; ++i)
	{
		/* Initialize quantum and qset lengths */
		scull_devs[i].quantum = scull_quantum;
		scull_devs[i].qset = scull_qset;
		/* TODO: Intialize mutex */
		/* Register device */
		retval = scull_register_cdev(&scull_devs[i], scull_minor + i);
		/* Check if registration was successful */
		if(retval)
		{
			printk(KERN_ERR "Error: %d adding scull device\n",
			       retval);
			goto fail;
		}
	}


#if SCULL_DEBUG
	printk(KERN_INFO "cdev registration successful\n");
#endif

	return 0;

	/* Error handling */
fail:
	/* Cleanup */
	scull_cleanup();
	return retval;
}
module_init(scull_init);
module_exit(scull_cleanup);
