// TSC samples include syscall and serialization overhead; compare with level=-1.
#include "types.h"
#include "stat.h"
#include "user.h"
#define EVENTS 16384
#define SAMPLES 128

struct result { uint mean, p95; };

static unsigned long long
cycles(void)
{
  uint lo, hi, leaf = 0;
  asm volatile("cpuid" : "+a"(leaf) : : "ebx", "ecx", "edx", "memory");
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
  return ((unsigned long long)hi << 32) | lo;
}

int
main(int argc, char **argv)
{
  int workers = argc > 1 ? atoi(argv[1]) : 1;
  int enabled = argc > 2 ? atoi(argv[2]) : 1;
  int start[2], out[2], i, j, pid;
  uint dropped, begin, elapsed;
  uint samples[SAMPLES], t, mean = 0, p95 = 0;
  unsigned long long sum, started, interval;
  struct result r;
  char token = 1;
  if(workers < 1 || workers > 8 || (enabled != 0 && enabled != 1) ||
     pipe(start) < 0 || pipe(out) < 0){
    printf(1, "BENCH FAIL: usage klog_bench [1..8 workers] [0|1 enabled]\n");
    exit();
  }
  for(i = 0; i < workers; i++){
    pid = fork();
    if(pid < 0){
      printf(1, "BENCH FAIL: fork\n");
      exit();
    }
    if(pid == 0){
      close(start[1]);
      close(out[0]);
      if(read(start[0], &token, 1) != 1)
        exit();
      for(j = 0; j < 256; j++)
        klogwrite(enabled ? KLOG_INFO : -1, "benchmark event");
      for(j = 0; j < EVENTS; j++){
        if(j % (EVENTS/SAMPLES) == 0){
          started = cycles();
          if(klogwrite(enabled ? KLOG_INFO : -1, "benchmark event") < 0)
            exit();
          interval = cycles() - started;
          if(interval > ~0U){
            printf(1, "BENCH FAIL: timing sample exceeds 32-bit range\n");
            exit();
          }
          samples[j/(EVENTS/SAMPLES)] = interval;
        } else if(klogwrite(enabled ? KLOG_INFO : -1, "benchmark event") < 0)
          exit();
      }
      sum = 0;
      for(j = 0; j < SAMPLES; j++)
        sum += samples[j];
      r.mean = sum >> 7;
      for(j = 1; j < SAMPLES; j++){
        int k = j;
        t = samples[j];
        while(k > 0 && samples[k-1] > t){
          samples[k] = samples[k-1];
          k--;
        }
        samples[k] = t;
      }
      r.p95 = samples[121];
      write(out[1], &r, sizeof(r));
      close(start[0]);
      close(out[1]);
      exit();
    }
  }
  close(start[0]);
  close(out[1]);
  dropped = klogdropped();
  begin = uptime();
  for(i = 0; i < workers; i++)
    write(start[1], &token, 1);
  close(start[1]);
  for(i = 0; i < workers; i++){
    if(read(out[0], &r, sizeof(r)) != sizeof(r)){
      printf(1, "BENCH FAIL: worker result\n");
      exit();
    }
    mean += r.mean / workers;
    p95 += r.p95 / workers;
  }
  elapsed = uptime() - begin;
  dropped = klogdropped() - dropped;
  close(out[0]);
  for(i = 0; i < workers; i++)
    wait();
  printf(1, "BENCH mode=%d workers=%d enabled=%d events=%d capacity=%d ticks=%u mean_cycles=%u mean_worker_p95=%u overwrites=%u\n",
         KLOG_MODE, workers, enabled, workers*EVENTS, KLOG_MODE == 0 ? KLOG_GLOBAL_CAPACITY : workers*KLOG_BUF_SIZE, elapsed, mean, p95, dropped);
  exit();
}
