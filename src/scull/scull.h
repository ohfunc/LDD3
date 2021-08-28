#ifndef _SCULL_H
#define _SCULL_H

#include <linux/cdev.h>
#include <linux/semaphore.h>

struct scull_qset {
  void **data;
  struct scull_qset *next;
};

struct scull_dev {
  struct scull_qset *data; // pointer to first quantum set
  int quantum;             // the current quantum size
  int qset;                // the current array size
  unsigned long size;      // amount of data stored here
  unsigned int access_key; // used by sculluid and scullpriv
  struct mutex mu;
  struct cdev cdev;
};

#endif // _SCULL_H