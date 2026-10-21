// User-space kernel log viewer tool
#include "types.h"
#include "stat.h"
#include "user.h"

static const char* level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

int
main(int argc, char *argv[])
{
  struct klog_entry *entries;
  int count, i;
  const char *level;
  
  // Allocate buffer on heap instead of stack
  entries = (struct klog_entry*)malloc(KLOG_MAX_ENTRIES * sizeof(struct klog_entry));
  if(entries == 0){
    printf(2, "malloc failed\n");
    exit();
  }
  
  // Get kernel log snapshot
  count = getklog(entries, KLOG_MAX_ENTRIES);
  
  if(count < 0){
    printf(2, "getklog failed\n");
    free(entries);
    exit();
  }
  
  printf(1, "Kernel Log (%d entries):\n", count);
  printf(1, "----------------------------------------\n");
  
  for(i = 0; i < count; i++){
    level = entries[i].level < 4 ? level_names[entries[i].level] : "?";
    printf(1, "[%u] %s CPU%d PID%d: %s\n",
           entries[i].seq,
           level,
           entries[i].cpu,
           entries[i].pid,
           entries[i].msg);
  }
  
  free(entries);
  exit();
}
