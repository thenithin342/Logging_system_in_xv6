// Kernel logging subsystem header
#ifndef KLOG_H
#define KLOG_H

#include "types.h"

// Log entry structure
struct klog_entry {
  uint seq;           // Global sequence number
  uint timestamp_hi;  // High 32 bits of raw TSC cycles
  uint timestamp_lo;  // Low 32 bits of raw TSC cycles
  uint cpu;           // CPU ID
  uint pid;           // Process ID (0 for kernel)
  uint level;         // Log level (DEBUG, INFO, WARN, ERROR)
  char msg[64];       // Log message
};

#define KLOG_BUF_SIZE 256  // Per-CPU retained records
#define KLOG_MAX_ENTRIES (4096 / sizeof(struct klog_entry))
#define KLOG_DEBUG 0
#define KLOG_INFO  1
#define KLOG_WARN  2
#define KLOG_ERROR 3

#ifndef KLOG_MODE
#define KLOG_MODE 2
#endif

#ifndef KLOG_GLOBAL_CAPACITY
#define KLOG_GLOBAL_CAPACITY KLOG_BUF_SIZE
#endif
#if KLOG_MODE < 0 || KLOG_MODE > 3
#error Unsupported KLOG_MODE
#endif
#if KLOG_GLOBAL_CAPACITY < 1 || KLOG_GLOBAL_CAPACITY > 2048
#error KLOG_GLOBAL_CAPACITY must be between 1 and 2048
#endif
#if KLOG_MODE == 0
#define KLOG_RING_COUNT 1
#define KLOG_RING_SIZE KLOG_GLOBAL_CAPACITY
#else
#define KLOG_RING_COUNT NCPU
#define KLOG_RING_SIZE KLOG_BUF_SIZE
#endif
#define KLOG_SEQ_EXHAUSTED (~0U)

#ifndef KLOG_USER
#include "spinlock.h"

// Per-CPU log buffer
struct klog_cpu_buf {
  struct spinlock lock;
  struct klog_entry entries[KLOG_RING_SIZE];
  uint head;          // Next write position
  uint count;         // Retained entries, bounded by KLOG_RING_SIZE
  uint dropped;       // Count of dropped entries due to overflow
};


// Kernel logging functions
void klog_init(void);
void klog_printf(const char *fmt, ...);
int klog_printf_level(int level, const char *fmt, ...);
int klog_snapshot(struct klog_entry *buf, int max_entries);
int klog_read_since(struct klog_entry*, int, uint*);
void klog_clear(void);
uint klog_get_dropped(void);

// Convenience macros
#define klog_debug(fmt, ...) klog_printf_level(KLOG_DEBUG, fmt, ##__VA_ARGS__)
#define klog_info(fmt, ...)  klog_printf_level(KLOG_INFO, fmt, ##__VA_ARGS__)
#define klog_warn(fmt, ...)  klog_printf_level(KLOG_WARN, fmt, ##__VA_ARGS__)
#define klog_error(fmt, ...) klog_printf_level(KLOG_ERROR, fmt, ##__VA_ARGS__)

#endif // !KLOG_USER

#endif // KLOG_H
