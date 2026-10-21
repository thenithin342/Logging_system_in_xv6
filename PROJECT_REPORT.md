# Kernel logging: implementation and validation report

## Provenance and changes

Starting point: repository `thenithin342/Logging_system_in_xv6`, commit `ee36653`, whose imported commits are attributed to Krishna Chaitanya. Existing xv6 and logging code are inherited work. The improvements in this version repair that implementation and add deterministic tests, a user logging syscall, a stress workload, and reproducible benchmark variants. Git history identifies the starting point and subsequent changes.

## Safety and data format

`struct klog_entry` is shared by kernel and userspace through `klog.h`, keeping its 88-byte layout. `KLOG_MAX_ENTRIES` is one 4 KiB page divided by that layout: 46 records. Both snapshot and device retrieval allocate one page, avoiding the original 5,632-byte stack array. They free it on success and copy failure and return an error on allocation failure.

The formatter uses standard variadic argument handling and unsigned magnitude arithmetic for `INT_MIN`. Supported forms are `%d`, `%x`, `%s`, and `%%`; an unknown form is copied literally if space permits. Every append reserves room for the terminator. Record message padding is cleared to avoid returning bytes left by a previous longer message.

`getklog` validates positive counts before arithmetic and compares the count with `(process_size - address) / entry_size`, avoiding address addition overflow. `copyout` enforces user page permission, including rejecting guard pages. Zero entries returns zero; oversized requests are rejected explicitly. The new `klogwrite` interface scans at most 64 bytes using user page mappings and never treats user text as a format string.

## Ring invariants and ordering

Each ring maintains `head` modulo 256 and `count` in `[0, 256]`. A full-ring insertion increments `dropped`; clearing resets head/count/dropped but preserves the sequence epoch. Separating the bounded position from the count prevents the position counter from eventually resetting occupancy through integer wrap.

Writers disable interrupts before choosing the current CPU. They take exactly one ring lock and allocate the next sequence while holding it. Mode 0 uses ring 0 for all CPUs; modes 1/2 use the current CPU's ring. Mode 1 uses the original sequence spinlock; mode 2 uses GCC's x86 atomic fetch-and-add. This does not remove ring locking or shared-counter cache traffic.

Readers take every ring lock in CPU order. No writer holding a ring waits for another ring. A writer holding the sequence lock never waits for another ring. This ordering avoids lock cycles. With all rings held, every in-flight writer has completed publishing before selection proceeds, so the snapshot corresponds to a consistent set of published records. The selection algorithm merges actual sequence values, including gaps from independently overwritten rings. Snapshots traverse newest first then reverse the selected page; streams traverse oldest first starting at their cursor.

The ABI uses 32-bit sequence numbers. Ordering and cursor comparison do not support wrap. Reboot before `2^32` events or extend the ABI for a long-running deployment. Atomic global allocation also remains a potential multicore bottleneck.

## Reader behavior

`fileread` routes the logging device to a file-aware reader while retaining the normal xv6 inode sleep lock. `file.off` holds the next unread sequence for that open file. `open` initializes it to zero; `dup` and `fork` share the file and cursor. Other device interfaces are unchanged. The log device is read-only, and an inode-only `readi` call cannot provide an independent reader cursor.

On overwritten data, a reader resumes at the oldest retained sequence at or beyond its cursor. No synthetic loss record is inserted; gaps and the aggregate overwrite counter indicate loss. Reading at the current end returns zero immediately and can resume on the same descriptor when data arrives. This differs from a blocking continuous stream. Logging reads are excluded from the read instrumentation so readers can reach the end.

## Verification and measurement

`tests/run_host.py` tests the actual ring and device sources with simulated platform primitives, ASan, UBSan, and eight pthread writers. It verifies exact overwrite accounting, sparse retained ranges, sorting, truncation, `INT_MIN`, zero padding, cursor independence, clear behavior, and failure cleanup. Host stubs do not validate xv6 interrupt masking or hardware spinlock behavior.

`tests/run_qemu.py` supplies that integration coverage: boot CPU-count checks, invalid/guard-pointer syscall tests, independent opens, shared `dup` cursors, whole record reads, EOF/resume, literal messages, and a stress test combining six writers with two readers and periodic snapshots. Failure markers and panics fail the runner. The benchmark matrix repeats integration tests for the global-ring, per-CPU-spinlock, and atomic-sequence variants at 1/2/4/8 emulated CPUs.

`klog_bench` forks one worker per requested CPU, releases workers through a pipe barrier, warms each for 256 calls, and performs 16,384 calls per worker. It samples 128 evenly spaced serialized TSC intervals. It reports mean sample latency and each worker's P95, averaged over workers. Level `-1` supplies the no-emission control through the same validated syscall. The Python runner alternates control/emission order, repeats three times, records raw rows, and measures end-to-end wall throughput. Warmup, process startup, sampling, sorting, and teardown are included in that wall duration; it is not pure kernel insertion throughput. Guest ticks are retained without assuming their calibration.

Read [the measured report](benchmarks/README.md) for results and variability. The QEMU measurements do not establish performance on native hardware. Native hardware testing, persistent storage, access control, and sequence-epoch support remain outside this educational implementation.
