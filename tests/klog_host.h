#ifndef KLOG_HOST_H
#define KLOG_HOST_H
// Only platform primitives are mocked; tests execute the real klog.c.
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xv6-public/types.h"
#define NCPU 8
#define SPINLOCK_H
struct spinlock { pthread_mutex_t mutex; };
static _Thread_local int test_cpu, interrupt_depth;
static struct proc { int pid; void *pgdir; } process = { 42, 0 };
static void pushcli(void) { interrupt_depth++; }
static void popcli(void) { assert(interrupt_depth > 0); interrupt_depth--; }
static int cpuid(void) { return test_cpu; }
static struct proc *myproc(void) { return &process; }
static void initlock(struct spinlock *lock, char *name) {
  (void)name;
  pthread_mutexattr_t attr;
  assert(pthread_mutexattr_init(&attr) == 0);
  assert(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) == 0);
  assert(pthread_mutex_init(&lock->mutex, &attr) == 0);
  assert(pthread_mutexattr_destroy(&attr) == 0);
}
static void acquire(struct spinlock *lock) { pushcli(); assert(pthread_mutex_lock(&lock->mutex) == 0); }
static void release(struct spinlock *lock) { assert(pthread_mutex_unlock(&lock->mutex) == 0); popcli(); }

#define KLOG 2
struct file { uint off; };
struct inode { int unused; };
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};
static struct devsw devsw[10];
static int fail_alloc, fail_copy, allocated;
static char copied[4096];
static char *kalloc(void) {
  if(fail_alloc) return 0;
  char *p = malloc(4096);
  assert(p);
  allocated++;
  return p;
}
static void kfree(char *p) { free(p); allocated--; }
static int copyout(void *pgdir, uint dst, void *src, uint len) {
  (void)pgdir; (void)dst;
  assert(len <= sizeof(copied));
  if(fail_copy){
    if(fail_copy == 2) memcpy(copied, src, len/2);
    return -1;
  }
  memcpy(copied, src, len);
  return 0;
}
int klogdev_write(struct inode*, char*, int);
#endif
