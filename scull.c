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
#include <asm/uaccess.h>

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "scull.h"

/* Local constants */
#define SCULL_NAME "scull"
/* Currently we're only supporting scull0-scull3 */
#define SCULL_DEV_COUNT 4
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
	.read =		scull_read,
	.write =	scull_write,
};
static int scull_quantum = DEFAULT_SCULL_QUANTUM;
static int scull_qset = DEFAULT_SCULL_QSET;

/* Function prototypes */
/* XXX: Assume all calls to static functions already hold device lock */
static int scull_trim(struct scull_dev *dev);
static struct scull_qset *scull_follow(struct scull_dev *dev, int n);
static void scull_cleanup(void);
static int scull_register_cdev(struct scull_dev *dev, int minor);
static int __init scull_init(void);

/* Function definitions */

/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */
static int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;   /* "dev" is not-null */
	int i;

	for (dptr = dev->data; dptr; dptr = next) { /* all the list items */
		if (dptr->data) {
			for (i = 0; i < qset; i++)
				kfree(dptr->data[i]);
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	return 0;
}

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
		if(mutex_lock_interruptible(&dev->lock))
		{
			return -ERESTARTSYS;
		}
		scull_trim(dev);
		mutex_unlock(&dev->lock);
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

/*
 * Follow the list
 */
static struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;

        /* Allocate first qset explicitly if need be */
	if (! qs) {
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;  /* Never mind */
		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* Then follow the list */
	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;  /* Never mind */
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
		   loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *pqset;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int item_size = quantum * qset;
	int list_item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	/* Acquire device lock */
	if(mutex_lock_interruptible(&dev->lock))
	{
#if SCULL_DEBUG
		printk(KERN_INFO "Error reading: can't acquire device lock\n");
#endif
		return -ERESTARTSYS;
	}
	/* Check for EOF */
	if(*f_pos >= dev->size)
	{
#if SCULL_DEBUG
		printk(KERN_INFO "Error reading: EOF reached\n");
#endif
		goto out;
	}
	/* If count exceeds file size, set count to remaining bytes */
	if(*f_pos + count > dev->size)
	{
		count = dev->size - *f_pos;
	}

	/* Find listitem, qset index, and offset in the quantum */
	list_item = (long) *f_pos / item_size;
	rest = (long) *f_pos % item_size;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	/* Follow the list until we reach the right position */
	pqset = scull_follow(dev, list_item);

	/* Make sure there is data at the position */
	if(pqset == NULL || !pqset->data || ! pqset->data[s_pos])
	{
#if SCULL_DEBUG
		printk(KERN_INFO "Error reading: no data at requested \
		       permission\n");
#endif
		goto out;
	}

	/* Read to the end of the quantum
	 * I.e. only transfer a single quantum.
	 * */
	if(count > quantum - q_pos)
	{
		count = quantum - q_pos;
	}

	/* Transfer the bytes to userspace */
	if(copy_to_user(buf, pqset->data[s_pos] + q_pos, count))
	{
#if SCULL_DEBUG
		printk(KERN_INFO "Error reading: copy to userspace failed\n");
#endif
		retval = -EFAULT;
		goto out;
	}
#if SCULL_DEBUG
	printk(KERN_INFO "Read successful\n");
#endif
	*f_pos += count;
	retval = count;

	/* Common exit; release device lock and return result */
out:
	mutex_unlock(&dev->lock);
	return retval;
}


ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
		   loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *pqset;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int item_size = quantum * qset;
	int list_item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;

	/* Lock device */
	if(mutex_lock_interruptible(&dev->lock))
	{
		return -ERESTARTSYS;
	}

	/* Find listitem, qset offset, and index in quantum */
	list_item = (long) *f_pos / item_size;
	rest = (long) *f_pos % item_size;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	/* Follow the list to the right qset */
	pqset = scull_follow(dev, list_item);
	if(pqset == NULL)
	{
		goto out;
	}

	/* The qset's quantum array may be uninitialised if it hasn't been written to before */
	if(!pqset->data)
	{
		/* If unintialised, allocate and zero memory for quantum array*/
		pqset->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if(!pqset->data)
		{
			printk(KERN_INFO "Error allocating qset array memory \
			       \n");
			goto out;
		}
		memset(pqset->data, 0, qset * sizeof(char *));
	}
	/* Likewise, the quantum we are writing to may be unintialised */
	if(!pqset->data[s_pos])
	{
		pqset->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if(!pqset->data[s_pos])
		{
			printk(KERN_INFO "Error allocating quantum memory\n");
			goto out;
		}
		/* XXX: We don't zero the kmalloc'd memory. Is this safe? */
	}

	/* We're only writing to the end of a single quantum so adjust count if necessary */
	if(count > quantum - q_pos)
	{
		count = quantum - q_pos;
	}

	/* Read data from userspace buffer into the quantum at position q_pos */
	if(copy_from_user(pqset->data[s_pos]+q_pos, buf, count))
	{
		printk(KERN_INFO "Error: copy from userspace failed\n");
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

	/* Adjust file size as we've written to it */
	if(dev->size < *f_pos)
	{
		dev->size = *f_pos;
	}

/* Common exit */
out:
	mutex_unlock(&dev->lock);
	return retval;
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
		for(i = 0; i < SCULL_DEV_COUNT; ++i)
		{
			/* Unregister char devices */
			cdev_del(&scull_devs[i].cdev);
		}

		/* Free any memory allocated to scull devices */
		kfree(scull_devs);
	}

	/* Unregister device number allocation */
	unregister_chrdev_region(devno, SCULL_DEV_COUNT);
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
				     SCULL_DEV_COUNT, SCULL_NAME);
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
	scull_devs = kmalloc(SCULL_DEV_COUNT * sizeof(struct scull_dev),
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
	for(i = 0; i < SCULL_DEV_COUNT; ++i)
	{
		/* Initialize quantum and qset lengths */
		scull_devs[i].quantum = scull_quantum;
		scull_devs[i].qset = scull_qset;
		/* Intialize mutex lock */
		mutex_init(&scull_devs[i].lock);
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
