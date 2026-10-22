// Kernel logging subsystem implementation
#ifdef KLOG_HOST_TEST
#include "../tests/klog_host.h"
#else
#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#endif
#include <stdarg.h>
#include "klog.h"

// Per-CPU log buffers
static struct klog_cpu_buf cpu_logs[KLOG_RING_COUNT];

// Global sequence counter
static uint global_seq = 1;
static struct spinlock seq_lock;

// Get high-resolution timestamp (using TSC)
static void
get_timestamp(uint *hi, uint *lo)
{
  uint tsc_lo, tsc_hi;
  asm volatile("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
  *hi = tsc_hi;
  *lo = tsc_lo;
}

// Get next sequence number
// Reserve UINT_MAX as the terminal cursor; stop rather than reusing IDs.
static uint
next_seq(void)
{
  uint seq;
#if KLOG_MODE == 2
  uint observed;
  seq = __atomic_load_n(&global_seq, __ATOMIC_RELAXED);
  for(;;){
    if(seq == KLOG_SEQ_EXHAUSTED)
      return 0;
    observed = __sync_val_compare_and_swap(&global_seq, seq, seq + 1);
    if(observed == seq)
      return seq;
    seq = observed;
  }
#else
  acquire(&seq_lock);
  seq = global_seq == KLOG_SEQ_EXHAUSTED ? 0 : global_seq++;
  release(&seq_lock);
  return seq;
#endif
}

// Initialize kernel logging subsystem
void
klog_init(void)
{
  int i;
  
  initlock(&seq_lock, "klog_seq");
  
  for(i = 0; i < KLOG_RING_COUNT; i++){
    initlock(&cpu_logs[i].lock, "klog_cpu");
    cpu_logs[i].head = 0;
    cpu_logs[i].count = 0;
    cpu_logs[i].dropped = 0;
  }
  
  klog_printf("klog: logging subsystem initialized");
}

// Helper: format integer
static int
snprintf_int(char *buf, int size, int val)
{
  char tmp[16];
  int i = 0, j = 0;
  int neg = val < 0;
  uint magnitude = neg ? 0U - (uint)val : (uint)val;
  
  if(size <= 0)
    return 0;
  
  
  if(magnitude == 0){
    tmp[i++] = '0';
  } else {
    while(magnitude > 0 && i < sizeof(tmp)){
      tmp[i++] = '0' + (magnitude % 10);
      magnitude /= 10;
    }
  }
  
  if(neg && j < size-1)
    buf[j++] = '-';
  
  while(i > 0 && j < size-1)
    buf[j++] = tmp[--i];
  
  buf[j] = 0;
  return j;
}

// Helper: format hex
static int
snprintf_hex(char *buf, int size, uint val)
{
  char tmp[16];
  int i = 0, j = 0;
  static char digits[] = "0123456789abcdef";
  
  if(size <= 2)
    return 0;
  
  buf[j++] = '0';
  buf[j++] = 'x';
  
  if(val == 0){
    tmp[i++] = '0';
  } else {
    while(val > 0 && i < sizeof(tmp)){
      tmp[i++] = digits[val & 0xf];
      val >>= 4;
    }
  }
  
  while(i > 0 && j < size-1)
    buf[j++] = tmp[--i];
  
  buf[j] = 0;
  return j;
}

// Internal logging function with level
static int
klog_printf_internal(int level, const char *fmt, va_list ap)
{
  int cpu_id;
  struct klog_cpu_buf *log;
  struct klog_entry *entry;
  uint idx, seq;
  int pid = 0;
  char buf[64];
  int i;
  char *s;
  
  if(KLOG_MODE == 3)
    return 0;

  // Get current CPU
  pushcli();
  cpu_id = cpuid();
  if(cpu_id < 0 || cpu_id >= NCPU){
    popcli();
    return -1;
  }
  
  log = &cpu_logs[KLOG_MODE == 0 ? 0 : cpu_id];
  
  // Get current process ID if available
  if(myproc())
    pid = myproc()->pid;
  
  // Format the message into buffer
  i = 0;
  for(; *fmt && i < sizeof(buf)-1; fmt++){
    if(*fmt != '%'){
      buf[i++] = *fmt;
      continue;
    }
    fmt++;
    if(*fmt == 0)
      break;
    switch(*fmt){
    case 'd':
      i += snprintf_int(buf + i, sizeof(buf) - i, va_arg(ap, int));
      break;
    case 'x':
      i += snprintf_hex(buf + i, sizeof(buf) - i, va_arg(ap, uint));
      break;
    case 's':
      s = va_arg(ap, char*);
      if(s == 0)
        s = "(null)";
      for(; *s && i < sizeof(buf)-1; s++)
        buf[i++] = *s;
      break;
    case '%':
      buf[i++] = '%';
      break;
    default:
      buf[i++] = '%';
      if(i < sizeof(buf)-1)
        buf[i++] = *fmt;
      break;
    }
  }
  buf[i] = 0;
  
  // Add entry to log buffer
  acquire(&log->lock);
  
  seq = next_seq();
  if(seq == 0){
    release(&log->lock);
    popcli();
    return -1;
  }
  idx = log->head;
  entry = &log->entries[idx];
  
  entry->seq = seq;
  get_timestamp(&entry->timestamp_hi, &entry->timestamp_lo);
  entry->cpu = cpu_id;
  entry->pid = pid;
  entry->level = level;
  
  // Zero padding prevents stale ring contents from reaching user space.
  memset(entry->msg, 0, sizeof(entry->msg));
  // Copy message
  for(i = 0; i < sizeof(entry->msg)-1 && buf[i]; i++)
    entry->msg[i] = buf[i];
  entry->msg[i] = 0;
  
  log->head = (log->head + 1) % KLOG_RING_SIZE;
  if(log->count == KLOG_RING_SIZE)
    log->dropped++;
  else
    log->count++;
  
  release(&log->lock);
  popcli();
  return 0;
}

// Log a formatted message (default INFO level)
void
klog_printf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  klog_printf_internal(KLOG_INFO, fmt, ap);
  va_end(ap);
}

// Log a formatted message with specific level
int
klog_printf_level(int level, const char *fmt, ...)
{
  va_list ap;
  int result;
  va_start(ap, fmt);
  result = klog_printf_internal(level, fmt, ap);
  va_end(ap);
  return result;
}

// Lock order: CPU rings in ascending order, then (writers only) seq_lock.
// Holding every ring gives a point-in-time view of published entries.
static void
lock_logs(void)
{
  int i;
  for(i = 0; i < KLOG_RING_COUNT; i++)
    acquire(&cpu_logs[i].lock);
}

static void
unlock_logs(void)
{
  int i;
  for(i = KLOG_RING_COUNT-1; i >= 0; i--)
    release(&cpu_logs[i].lock);
}

// Merge actual retained sequences, including gaps from independent overwrites.
// UINT_MAX is a terminal cursor; published IDs never wrap.
static int
collect(struct klog_entry *buf, int max, uint *cursor)
{
  uint pos[NCPU], index, seq;
  int cpu, best, count = 0, i;
  struct klog_cpu_buf *log;
  if(max <= 0)
    return 0;
  if(max > KLOG_MAX_ENTRIES)
    max = KLOG_MAX_ENTRIES;
  lock_logs();
  for(cpu = 0; cpu < KLOG_RING_COUNT; cpu++)
    pos[cpu] = 0;
  while(count < max){
    best = -1;
    seq = cursor ? ~0U : 0;
    for(cpu = 0; cpu < KLOG_RING_COUNT; cpu++){
      log = &cpu_logs[cpu];
      while(pos[cpu] < log->count){
        index = cursor ? log->head + KLOG_RING_SIZE - log->count + pos[cpu]
                       : log->head + KLOG_RING_SIZE - 1 - pos[cpu];
        index %= KLOG_RING_SIZE;
        if(cursor && log->entries[index].seq < *cursor){
          pos[cpu]++;
          continue;
        }
        if(best < 0 || (cursor ? log->entries[index].seq < seq
                              : log->entries[index].seq > seq)){
          best = cpu;
          seq = log->entries[index].seq;
        }
        break;
      }
    }
    if(best < 0)
      break;
    log = &cpu_logs[best];
    index = cursor ? log->head + KLOG_RING_SIZE - log->count + pos[best]
                   : log->head + KLOG_RING_SIZE - 1 - pos[best];
    buf[count++] = log->entries[index % KLOG_RING_SIZE];
    pos[best]++;
  }
  if(cursor && count)
    *cursor = buf[count-1].seq + 1;
  unlock_logs();
  if(!cursor){
    for(i = 0; i < count/2; i++){
      struct klog_entry tmp = buf[i];
      buf[i] = buf[count-1-i];
      buf[count-1-i] = tmp;
    }
  }
  return count;
}

int
klog_snapshot(struct klog_entry *buf, int max_entries)
{
  return collect(buf, max_entries, 0);
}

int
klog_read_since(struct klog_entry *buf, int max_entries, uint *cursor)
{
  return collect(buf, max_entries, cursor);
}

// Clear all logs
void
klog_clear(void)
{
  int cpu_id;
  struct klog_cpu_buf *log;
  
  lock_logs();
  for(cpu_id = 0; cpu_id < KLOG_RING_COUNT; cpu_id++){
    log = &cpu_logs[cpu_id];
    log->head = 0;
    log->count = 0;
    log->dropped = 0;
  }
  unlock_logs();
}

// Get total dropped count
uint
klog_get_dropped(void)
{
  int cpu_id;
  uint total = 0;
  struct klog_cpu_buf *log;
  
  lock_logs();
  for(cpu_id = 0; cpu_id < KLOG_RING_COUNT; cpu_id++){
    log = &cpu_logs[cpu_id];
    total += log->dropped;
  }
  
  unlock_logs();
  return total;
}
