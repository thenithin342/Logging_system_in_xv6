#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

_Static_assert(__builtin_types_compatible_p(__typeof__(klogdropped()), uint),
               "overwrite counts use the full unsigned syscall word");

static void
check(int ok, char *what)
{
  if(!ok){
    printf(1, "KLOG TEST FAIL: %s\n", what);
    exit();
  }
}

int
main(void)
{
  static struct klog_entry entries[KLOG_MAX_ENTRIES], a, b;
  int fd, other, shared, count, i, n, inherited[2], child;
  char longmsg[65];
  char *guard = (char*)(((uint)&fd & ~4095U) - 4096);
  check(sizeof(a) == 88, "entry ABI");
  printf(1, "KLOG UINT CHECK: %u\n", ~0U);
  check(getklog(entries, 0) == 0, "zero length");
  check(getklog(entries, -1) == -1, "negative length");
  check(getklog(entries, KLOG_MAX_ENTRIES+1) == -1, "explicit page limit");
  check(getklog((void*)0xfffffff0, 1) == -1, "overflow pointer");
  check(getklog((void*)sbrk(0), 1) == -1, "past process end");
  check(getklog((void*)guard, 1) == -1, "guard snapshot destination");
  check(klogwrite(1, guard) == -1, "guard source string");
  memset(longmsg, 'x', sizeof(longmsg));
  longmsg[64] = 0;
  check(klogwrite(1, longmsg) == -1, "overlong string");
  check(klogwrite(4, "bad") == -1, "invalid level");
  check(klogwrite(1, (void*)0xffffffff) == -1, "invalid string");
  check(klogwrite(1, "literal %d %s") == 0, "literal message");
  count = getklog(entries, KLOG_MAX_ENTRIES);
  if(KLOG_MODE == 3){
    check(count == 0 && klogdropped() == 0, "disabled emission");
    mknod("klog", KLOG, 0);
    fd = open("klog", O_RDONLY);
    check(fd >= 0 && read(fd, &a, sizeof(a)) == 0, "disabled device EOF");
    close(fd);
    printf(1, "KLOG TEST PASS: disabled mode\n");
    exit();
  }
  check(count > 0, "snapshot nonempty");
  for(i = 1; i < count; i++)
    check(entries[i-1].seq < entries[i].seq, "ordered unique snapshot");
  check(strcmp(entries[count-1].msg, "literal %d %s") == 0, "no format injection");
  mknod("klog", KLOG, 0);
  fd = open("klog", O_RDONLY);
  other = open("klog", O_RDONLY);
  check(fd >= 0 && other >= 0, "open independent readers");
  check(read(fd, &a, 0) == 0, "zero device read");
  check(read(fd, &a, sizeof(a)-1) == -1, "short device read");
  check(read(fd, (void*)0xfffffff0, sizeof(a)) == -1, "invalid device destination");
  check(read(fd, &a, sizeof(a)) == sizeof(a), "first read");
  check(read(other, &b, sizeof(b)) == sizeof(b), "independent first read");
  check(a.seq == b.seq, "independent cursors");
  shared = dup(fd);
  check(shared >= 0, "dup reader");
  check(read(shared, &b, sizeof(b)) == sizeof(b) && b.seq > a.seq, "dup shares cursor");
  a = b;
  check(read(fd, &b, sizeof(b)) == sizeof(b) && b.seq > a.seq, "cursor advances");
  check(pipe(inherited) == 0, "fork result pipe");
  child = fork();
  check(child >= 0, "fork reader");
  if(child == 0){
    close(inherited[0]);
    check(read(fd, &a, sizeof(a)) == sizeof(a), "inherited descriptor read");
    check(write(inherited[1], &a, sizeof(a)) == sizeof(a), "report child cursor");
    close(inherited[1]);
    exit();
  }
  close(inherited[1]);
  check(read(inherited[0], &a, sizeof(a)) == sizeof(a), "receive child cursor");
  close(inherited[0]);
  check(wait() == child, "wait reader");
  check(read(fd, &b, sizeof(b)) == sizeof(b) && b.seq > a.seq,
        "fork shares cursor with parent");
  for(i = 0; i < 64; i++){
    n = read(fd, entries, sizeof(entries));
    check(n >= 0 && n % sizeof(a) == 0, "whole records");
    if(n == 0)
      break;
  }
  check(i < 64, "log reads reach end without generating logs");
  check(klogwrite(1, "after eof") == 0, "append after eof");
  check(read(fd, &a, sizeof(a)) == sizeof(a) && strcmp(a.msg, "after eof") == 0,
        "read resumes after eof");
  close(shared);
  close(fd);
  close(other);
  printf(1, "KLOG TEST PASS\n");
  exit();
}
