#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

static int
reader(int fd)
{
  struct klog_entry entries[16];
  uint last = 0;
  int i, j, n, count;
  for(i = 0; i < 300; i++){
    n = read(fd, entries, sizeof(entries));
    if(n < 0 || n % sizeof(entries[0]))
      return 0;
    count = n / sizeof(entries[0]);
    for(j = 0; j < count; j++){
      if(entries[j].seq <= last || entries[j].msg[63] != 0)
        return 0;
      last = entries[j].seq;
    }
    if(i % 20 == 0)
      sleep(1);
  }
  return 1;
}

int
main(void)
{
  int fd, p[2], i, j, pid, workers = 8;
  uint dropped;
  char ok, result[8];
  static struct klog_entry entries[KLOG_MAX_ENTRIES];
  mknod("klog", KLOG, 0);
  if(pipe(p) < 0)
    goto fail;
  dropped = klogdropped();
  for(i = 0; i < workers; i++){
    pid = fork();
    if(pid < 0)
      goto fail;
    if(pid == 0){
      close(p[0]);
      ok = 1;
      fd = open("klog", O_RDONLY);
      if(fd < 0)
        ok = 0;
      if(i < 2){
        if(fd >= 0)
          ok = reader(fd);
      } else {
        for(j = 0; j < 3000; j++){
          if(klogwrite(1, "stress writer") < 0)
            ok = 0;
          if(j % 100 == 0){
            int n = getklog(entries, KLOG_MAX_ENTRIES), k;
            if(n < 0)
              ok = 0;
            for(k = 1; k < n; k++)
              if(entries[k-1].seq >= entries[k].seq)
                ok = 0;
          }
        }
      }
      if(fd >= 0)
        close(fd);
      write(p[1], &ok, 1);
      close(p[1]);
      exit();
    }
  }
  close(p[1]);
  for(i = 0; i < workers; i++){
    if(read(p[0], result+i, 1) != 1 || result[i] != 1)
      goto fail;
  }
  close(p[0]);
  for(i = 0; i < workers; i++)
    wait();
  if(KLOG_MODE == 3 ? klogdropped() != dropped : klogdropped() <= dropped)
    goto fail;
  printf(1, "KLOG STRESS PASS: 18000 writes, concurrent snapshots and readers, overwrites=%u\n",
         klogdropped()-dropped);
  exit();
fail:
  printf(1, "KLOG STRESS FAIL\n");
  exit();
}
