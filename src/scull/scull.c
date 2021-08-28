/*
SCULL: Simple Character Utility for Loading Localities

Memory layout:
  Scull device:
    Data -> scull_qset:
      Next -> scull_qset...
      Data -> List[Quantum]
*/
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/uaccess.h>

#include "scull.h"

const int scull_nr_devs = 3;
const char *kModuleName = "scull";
int scull_major = 69;
int scull_minor = 420;
int scull_quantum = 4000;
int scull_qset = 1000;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Bellamy");
MODULE_DESCRIPTION("Scull exercise from LDD3");
MODULE_VERSION("69.420");

struct scull_dev *scull_devices;

struct scull_qset *scull_follow(struct scull_dev *dev, int n) {
  struct scull_qset *qs = dev->data;

  // Allocate the first qset explicitly if needed.
  if (!qs) {
    qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
    if (qs == NULL)
      return NULL;
    memset(qs, 0, sizeof(struct scull_qset));
  }

  // Then follow the linked list.
  while (n--) {
    if (!qs->next) {
      qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
      if (qs->next == NULL)
        return NULL;
      memset(qs->next, 0, sizeof(struct scull_qset));
    }
    qs = qs->next;
  }
  return qs;
};

ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos) {
  // Our device has a linked list of "items" which contain quantum sets.
  // Based on the f_pos/offset we seek to the item, then the quantum set, then
  // the quantum, and read to the end of that quantum.
  struct scull_dev *dev = filp->private_data;
  struct scull_qset *dptr; // the first listitem
  int quantum, qset;
  int itemsize; // how many bytes in the listitem
  int item;     // which item we're seeking to in the linked list
  int s_pos;    // quantum set position
  int q_pos;    // quantum position
  int rest;     // how much is left in the item after the offset
  ssize_t retval = 0;

  if (mutex_lock_interruptible(&dev->mu))
    return -ERESTARTSYS;

  quantum = dev->quantum;
  qset = dev->qset;
  itemsize = quantum * qset;

  if (*f_pos >= dev->size)
    goto out;
  if (*f_pos + count > dev->size)
    count = dev->size - *f_pos;

  // find listitem, qset index, and offset in the quantum
  item = (long)*f_pos / itemsize;
  rest = (long)*f_pos % itemsize;
  s_pos = rest / quantum;
  q_pos = rest % quantum;

  // follow the list up to the right position
  dptr = scull_follow(dev, item);

  if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
    goto out;

  if (count > quantum - q_pos)
    count = quantum - q_pos;

  if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
    retval = -EFAULT;
    goto out;
  }
  *f_pos += count;
  retval = count;

out:
  mutex_unlock(&dev->mu);
  return retval;
};

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos) {
  struct scull_dev *dev = filp->private_data;
  struct scull_qset *dptr = dev->data;
  int quantum = dev->quantum, qset = dev->qset;
  int itemsize = quantum * qset;
  int item, s_pos, q_pos, rest;
  ssize_t retval = -ENOMEM;

  if (mutex_lock_interruptible(&dev->mu))
    return -ERESTARTSYS;

  // find listitem, qset index, and offset in the quantum
  item = (long)*f_pos / itemsize;
  rest = (long)*f_pos % itemsize;
  s_pos = rest / quantum;
  q_pos = rest % quantum;

  // follow the list up to the right position
  dptr = scull_follow(dev, item);

  if (dptr == NULL)
    goto out;

  if (!dptr->data) {
    dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
    if (!dptr->data)
      goto out;
    memset(dptr->data, 0, sizeof(qset * sizeof(char *)));
  }

  if (!dptr->data[s_pos]) {
    dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
    if (!dptr->data[s_pos])
      goto out;
  }

  // write only up to the end of this quantum
  if (count > quantum - q_pos)
    count = quantum - q_pos;

  if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
    retval = -EFAULT;
    goto out;
  }

  *f_pos += count;
  retval = count;

  // Update the device size
  if (dev->size < *f_pos)
    dev->size = *f_pos;

out:
  mutex_unlock(&dev->mu);
  return retval;
}

// Clear out all the data in the quantum set.
static void scull_trim(struct scull_dev *dev) {
  struct scull_qset *next, *dptr;
  int qset = dev->qset;
  int i;

  for (dptr = dev->data; dptr; dptr = next) {
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
};

int scull_open(struct inode *inode, struct file *filp) {
  struct scull_dev *dev; // device information

  dev = container_of(inode->i_cdev, struct scull_dev, cdev);
  filp->private_data = dev;

  // trim device length to 0 if opened write-only
  if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
    scull_trim(dev);
  }

  return 0;
}

int scull_release(struct inode *inode, struct file *filep) { return 0; }

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .read = scull_read,
    .write = scull_write,
    .open = scull_open,
    .release = scull_release,
};

static void scull_setup_cdev(struct scull_dev *dev, int index) {
  int err, devno = MKDEV(scull_major, scull_minor + index);

  cdev_init(&dev->cdev, &scull_fops);
  dev->cdev.owner = THIS_MODULE;
  dev->cdev.ops = &scull_fops;
  err = cdev_add(&dev->cdev, devno, 1 /*count*/);
  if (err) {
    printk(KERN_NOTICE "Error %d adding scull%d", err, index);
    return;
  }
  printk(KERN_NOTICE
         "Successfully setup scull device with major %d and minor %d.",
         scull_major, scull_minor + index);
}

static void __exit scull_cleanup_module(void) {
  int i;
  dev_t devno = MKDEV(scull_major, scull_minor);

  // Get rid of char device entries.
  if (scull_devices) {
    for (i = 0; i < scull_nr_devs; i++) {
      scull_trim(scull_devices + i);
      cdev_del(&scull_devices[i].cdev);
    }
    kfree(scull_devices);
  }

  // cleanup_module is never called if registering failed
  unregister_chrdev_region(devno, scull_nr_devs);
}

static int __init scull_init_module(void) {
  int res, i;
  dev_t dev = 0;

  if (scull_major) {
    dev = MKDEV(scull_major, scull_minor);
    res = register_chrdev_region(dev, scull_minor, kModuleName);
  } else {
    res = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, kModuleName);
    scull_major = MAJOR(dev);
  }
  if (res < 0) {
    printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
    return res;
  }

  // kmalloc isn't great for larger memory allocations, but it's easier to use.
  scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
  if (!scull_devices) {
    res = -ENOMEM;
    goto fail;
  }
  memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

  for (i = 0; i < scull_nr_devs; i++) {
    scull_devices[i].quantum = scull_quantum;
    scull_devices[i].qset = scull_qset;
    mutex_init(&scull_devices[i].mu);
    scull_setup_cdev(&scull_devices[i], i);
  }

  printk(KERN_NOTICE "Init of scull device sucessful.");
  return 0;

fail:
  scull_cleanup_module();
  return res;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
