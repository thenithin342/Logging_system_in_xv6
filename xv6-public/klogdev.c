// Per-open nonblocking binary log stream; fileread holds the inode lock.
#ifdef KLOG_HOST_TEST
#include "../tests/klog_host.h"
#else
#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "mmu.h"
#include "proc.h"
#endif
#include "klog.h"

void
klogdev_init(void)
{
  devsw[KLOG].write = klogdev_write;
}

int
klogdev_read(struct file *f, char *dst, int n)
{
  struct klog_entry *entries;
  int count, bytes;
  uint cursor = f->off;
  if(n == 0)
    return 0;
  if(n < (int)sizeof(struct klog_entry))
    return -1;
  if((entries = (struct klog_entry*)kalloc()) == 0)
    return -1;
  count = klog_read_since(entries, n / sizeof(*entries), &cursor);
  bytes = count * sizeof(*entries);
  if(bytes && copyout(myproc()->pgdir, (uint)dst, entries, bytes) < 0){
    kfree((char*)entries);
    return -1;
  }
  f->off = cursor;
  kfree((char*)entries);
  return bytes;
}

int
klogdev_write(struct inode *ip, char *buf, int n)
{
  (void)ip;
  (void)buf;
  (void)n;
  return -1;
}
