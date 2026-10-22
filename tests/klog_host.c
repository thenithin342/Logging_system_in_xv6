#define KLOG_HOST_TEST
#include "../xv6-public/klog.c"
#include "../xv6-public/klogdev.c"

static void ordered(struct klog_entry *out, int n) {
  for(int i = 0; i < n; i++) {
    assert(out[i].msg[63] == 0);
    if(i) assert(out[i-1].seq < out[i].seq);
  }
}

static void *writer(void *id) {
  test_cpu = (int)(uintptr_t)id;
  for(int i = 0; i < 10000; i++) klog_info("writer %d %d", test_cpu, i);
  assert(interrupt_depth == 0);
  return 0;
}

int main(void) {
  struct klog_entry out[KLOG_MAX_ENTRIES], second[KLOG_MAX_ENTRIES];
  pthread_t threads[NCPU];
  uint cursor = 0, independent = 0;
  char format[70], number[32];
  klog_init();
  klog_clear();
  assert(klog_snapshot(out, 1) == 0);
  if(KLOG_MODE == 3){
    assert(klog_info("disabled") == 0);
    assert(klog_snapshot(out, KLOG_MAX_ENTRIES) == 0);
    assert(klog_get_dropped() == 0);
    struct file disabled = {0};
    assert(klogdev_read(&disabled, 0, 88) == 0 && disabled.off == 0);
    assert(klogdev_read(&disabled, 0, -1) == -1);
    puts("HOST KLOG PASS: build-time disabled mode");
    return 0;
  }
  assert(klog_snapshot(out, 0) == 0);
  assert(klog_snapshot(out, -1) == 0);
  assert(snprintf_int(number, sizeof(number), (-2147483647-1)) == 11);
  assert(strcmp(number, "-2147483648") == 0);
  for(int i = 0; i < 64; i++) {
    memset(format, 'A', i);
    strcpy(format+i, "%q");
    klog_info(format);
    assert(klog_snapshot(out, 1) == 1);
    assert(strlen(out[0].msg) <= 63);
  }
  klog_info("%d %x %s %%", (-2147483647-1), 0xffffffffU, (char*)0);
  assert(klog_snapshot(out, 1) == 1);
  assert(strcmp(out[0].msg, "-2147483648 0xffffffff (null) %") == 0);
  klog_info("x");
  assert(klog_snapshot(out, 1) == 1);
  for(int i = 2; i < 64; i++) assert(out[0].msg[i] == 0);
  klog_clear();
  // CPU 0 retains old entries while CPU 1 overwrites a distant sequence range.
  test_cpu = 0;
  for(int i = 0; i < 20; i++) klog_info("old %d", i);
  test_cpu = 1;
  for(int i = 0; i < 1000; i++) klog_info("new %d", i);
  assert(klog_get_dropped() == (KLOG_MODE == 0 ?
         (1020 > KLOG_RING_SIZE ? 1020-KLOG_RING_SIZE : 0) : 744));
  assert(klog_snapshot(out, KLOG_MAX_ENTRIES) == KLOG_MAX_ENTRIES);
  ordered(out, KLOG_MAX_ENTRIES);
  assert(out[KLOG_MAX_ENTRIES-1].seq == global_seq-1);
  assert(out[0].seq == global_seq-KLOG_MAX_ENTRIES);
  int n = klog_read_since(out, KLOG_MAX_ENTRIES, &cursor);
  assert(n == KLOG_MAX_ENTRIES);
  assert(klog_read_since(second, KLOG_MAX_ENTRIES, &independent) == n);
  assert(memcmp(out, second, n*sizeof(*out)) == 0);
  uint last = out[n-1].seq;
  while((n = klog_read_since(out, KLOG_MAX_ENTRIES, &cursor)) > 0) {
    ordered(out, n);
    assert(out[0].seq > last);
    last = out[n-1].seq;
  }
  assert(last == global_seq-1);
  for(int i = 0; i < NCPU; i++)
    assert(pthread_create(&threads[i], 0, writer, (void*)(uintptr_t)i) == 0);
  for(int i = 0; i < 1000; i++) {
    n = klog_snapshot(out, KLOG_MAX_ENTRIES);
    ordered(out, n);
  }
  for(int i = 0; i < NCPU; i++) assert(pthread_join(threads[i], 0) == 0);
  n = klog_snapshot(out, KLOG_MAX_ENTRIES);
  assert(n == KLOG_MAX_ENTRIES);
  ordered(out, n);
  assert(out[n-1].seq == global_seq-1);
  struct file f = { 0 };
  klogdev_init();
  assert(devsw[KLOG].write(0, 0, 0) == -1);
  fail_alloc = 1;
  assert(klogdev_read(&f, 0, 88) == -1 && f.off == 0);
  fail_alloc = 0;
  fail_copy = 1;
  assert(klogdev_read(&f, 0, 88) == -1 && f.off == 0 && allocated == 0);
  fail_copy = 2;
  assert(klogdev_read(&f, 0, 88) == -1 && f.off == 0 && allocated == 0);
  fail_copy = 0;
  uint expected = 0;
  assert(klog_read_since(second, 1, &expected) == 1);
  assert(klogdev_read(&f, 0, -1) == -1 && f.off == 0);
  assert(klogdev_read(&f, 0, 87) == -1);
  assert(klogdev_read(&f, 0, 0) == 0);
  assert(klogdev_read(&f, 0, 4096) == 88*KLOG_MAX_ENTRIES);
  assert(f.off > 0 && allocated == 0);
  assert(((struct klog_entry*)copied)->seq == second[0].seq);
  klog_clear();
  assert(klogdev_read(&f, 0, 88) == 0 && allocated == 0);
  assert(klog_get_dropped() == 0);
  assert(klog_snapshot(out, 1) == 0);
  for(uint i = 0; i < KLOG_RING_SIZE; i++) klog_info("full");
  assert(cpu_logs[KLOG_MODE == 0 ? 0 : test_cpu].count == KLOG_RING_SIZE);
  assert(klog_get_dropped() == 0);
  for(int i = 0; i < 5; i++) klog_info("overwrite");
  assert(klog_get_dropped() == 5);
  klog_clear();
  // Only 31 IDs remain; concurrent reservations must saturate, never wrap.
  global_seq = KLOG_SEQ_EXHAUSTED - 31;
  for(int i = 0; i < NCPU; i++)
    assert(pthread_create(&threads[i], 0, writer, (void*)(uintptr_t)i) == 0);
  for(int i = 0; i < NCPU; i++) assert(pthread_join(threads[i], 0) == 0);
  assert(global_seq == KLOG_SEQ_EXHAUSTED);
  n = klog_snapshot(out, KLOG_MAX_ENTRIES);
  assert(n == 31);
  ordered(out, n);
  assert(out[n-1].seq == KLOG_SEQ_EXHAUSTED-1);
  cursor = 0;
  assert(klog_read_since(second, KLOG_MAX_ENTRIES, &cursor) == 31);
  assert(cursor == KLOG_SEQ_EXHAUSTED);
  assert(klog_read_since(second, KLOG_MAX_ENTRIES, &cursor) == 0);
  assert(klog_info("exhausted") == -1);
  klog_clear();
  assert(klog_info("clear preserves exhaustion") == -1);
  assert(klog_snapshot(out, KLOG_MAX_ENTRIES) == 0);
  assert(interrupt_depth == 0 && allocated == 0);
  puts("HOST KLOG PASS: formatter, gaps, wraparound, cursors, 8 concurrent writers");
  return 0;
}
