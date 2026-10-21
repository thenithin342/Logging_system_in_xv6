# Explaining this project in an AMD interview

This guide connects the implemented code to OS and computer architecture concepts. It does not claim knowledge of AMD's interview rubric or performance on AMD hardware. The measurements were collected in QEMU TCG on Ubuntu WSL running on an Intel host.

## A 60-second explanation

> I improved an existing kernel logging subsystem in x86 xv6. It records structured events in per-CPU circular buffers and exposes them through a snapshot syscall and a character device. I first repaired correctness problems: an oversized kernel-stack allocation, unsafe message formatting, snapshots that mishandled overwritten sequence gaps, and readers that repeatedly returned the same records. I added per-open cursors and consistent snapshots, then compared a shared ring, per-CPU rings with a sequence spinlock, and per-CPU rings with atomic sequence allocation. The project passed sanitizer checks and concurrency tests across 12 QEMU configurations. I collected 72 benchmark measurements. Atomic sequencing helped in some configurations and regressed in another, so I report the variability and emulator limits instead of claiming a universal hardware speedup.

Use this explanation after you can walk through the implementation. Describe the initial OS and logger as inherited code and identify the changes in this version.

## A three-minute source walkthrough

| Step | Source | What to explain |
| --- | --- | --- |
| 1. Define the record | [klog.h](xv6-public/klog.h) | An 88-byte record holds sequence, raw TSC, CPU, PID, level, and a bounded message. The same layout is shared with userspace. |
| 2. Capture an event | [klog.c](xv6-public/klog.c) | Disable local interrupts, choose the CPU's ring, format a bounded message, lock the ring, allocate a sequence, fill the slot, and publish head/count before unlocking. |
| 3. Handle capacity | [klog.c](xv6-public/klog.c) | Head stays modulo 256. Count saturates at 256. Each overwrite increments the loss counter. Entry storage for eight rings totals 176 KiB, plus metadata. |
| 4. Take a snapshot | [klog.c](xv6-public/klog.c) | Lock all rings in CPU order and merge actual retained IDs. Copy the newest records in increasing sequence order. |
| 5. Cross the syscall boundary | [sysproc.c](xv6-public/sysproc.c) | Validate count/address arithmetic, allocate one page, and use copyout. Literal user messages are checked through user page mappings. |
| 6. Stream independently | [file.c](xv6-public/file.c), [klogdev.c](xv6-public/klogdev.c) | Store the next sequence in the open file's offset. Independent opens have different cursors; dup/fork share one cursor. Commit it after a successful copy. |
| 7. Demonstrate evidence | [tests](tests), [benchmark report](benchmarks/README.md) | Separate host sanitizer checks from actual xv6 integration tests and explain what each proves. |

## Three bug stories worth practicing

### Kernel stack safety

The original device reader declared 64 entries locally. At 88 bytes each, that is 5,632 bytes on a 4,096-byte xv6 kernel stack, before accounting for other frames. The fix allocates one page, caps retrieval at 46 records, and frees it on success or copy failure. Host tests inject allocation/copy failures; the logging sources also passed a compile check rejecting frames larger than 1,024 bytes. A frame-size check is not a proof of maximum nested stack usage.

### Ordering with overwritten rings

Suppose one CPU retains IDs 1–20 while another retains 765–1020 after overwriting older events. A cutoff computed as minimum ID plus the number of records to skip does not identify the newest 46 records. The implementation merges actual sequence values, so the retained gaps are harmless. The host test constructs this unbalanced case and checks that the result contains the latest IDs.

### Reader progress and observation effects

The original device returned a fresh snapshot on every read and repeated old records. A cursor per open file fixes progress. Logging a log-device read creates another event, so reading can keep generating more work; the read instrumentation now excludes the logging device. Tests verify independent readers, shared dup cursors, EOF, and resuming after a later event.

## Questions about synchronization and architecture

**Why use per-CPU rings?** Each CPU normally updates a different ring and lock, reducing contention for the ring itself. Local interrupts are disabled so the writer cannot migrate while selecting and updating its CPU state. A shared sequence counter still creates cross-CPU traffic.

**Does atomic fetch-and-add make the logger lock-free?** No. It removes the sequence allocator's spinlock acquisition. Ring spinlocks still protect payload publication. Atomic read-modify-write operations on one shared location can also serialize through cache coherence and cache-line ownership transfers. This project does not measure those hardware events directly.

**What publishes the entry safely?** Ring locking protects the payload, sequence, and head/count updates as one operation. The atomic sequence number supplies uniqueness; it does not by itself make a partially filled record safe for a reader.

**Why allocate the sequence while holding the ring lock?** A writer keeps its ring locked from sequence assignment through publication. When a snapshot has acquired every ring, all entries assigned by those writers have finished publishing. Moving sequence reservation before ring locking could let a reader advance past an older reserved ID before its record becomes visible.

**Can the lock ordering deadlock?** Writers hold one ring and may then take the sequence lock. Readers take rings in increasing CPU order and never take the sequence lock. No writer holds the sequence lock while waiting for another ring. Explain this wait graph rather than assuming that small critical sections cannot deadlock.

**Why lock every ring during retrieval?** It establishes a consistent view of published entries. The cost is that snapshots briefly pause writers. A design that copied each ring independently would need a weaker consistency contract or a more complex publication/retry protocol.

**What is false sharing?** Independent variables can share one cache line, causing ownership traffic when different CPUs update them. The atomic counter in this project is true sharing because all writers update the same value. Ring alignment and padding would require separate measurement before claiming a false-sharing improvement.

**Spinlock versus sleep lock?** A spinlock retries without yielding and suits short critical sections where sleeping is forbidden. A sleep lock blocks and lets other work run. Here, ring updates use spinlocks, while xv6's inode sleep lock serializes device reads. The temporary page allocation occurs before acquiring ring locks.

**Why raw TSC values?** RDTSC supplies a counter on this x86 target, but its ticks are not automatically nanoseconds or core clock cycles. Conversion requires calibration; cross-CPU comparability depends on the platform. Records are ordered by sequence IDs. The benchmark uses CPUID serialization and reports raw TSC intervals, including timing overhead.

**What about NUMA or cache misses on AMD processors?** Per-CPU placement and a shared atomic location are relevant concepts, but QEMU results do not establish NUMA effects, cache miss rates, or interconnect behavior on AMD silicon. A physical-machine study would need controlled placement and hardware counters. Treat that as future work.

**What happens after a full ring or sequence wrap?** Full rings overwrite oldest entries and increment counters. Slow readers skip unavailable IDs. The current 32-bit sequence ABI does not support wrap; it assumes fewer than 2^32 events per boot. A longer-lived system needs a wider ID or an epoch design.

## Explain the benchmark without overclaiming

The variants change buffer partitioning and sequence allocation. Mode 0 has one shared 256-record ring and a sequence lock. Mode 1 has 256 records per CPU and the shared sequence lock. Mode 2 keeps the per-CPU rings and uses atomic numbering. Mode 0 therefore has less retention capacity; overwrite counts are not an equal-capacity comparison.

For each 1/2/4/8-CPU configuration, the runner starts the same number of workers, uses a pipe start barrier, warms them, and runs 16,384 workload calls per worker. Each worker samples 128 call latencies. The control validates the same message without emitting an event. Control and enabled ordering alternates across three repetitions.

At two CPUs, the observed median enabled throughput was 42,364 events/s for atomic numbering versus 28,293 for the per-CPU sequence-lock variant. At four CPUs, atomic numbering was lower: 24,927 versus 29,037 events/s. At eight CPUs, every variant slowed down. These are observations from this emulator run, not proof of a universal atomic benefit or AMD hardware scaling.

Throughput includes syscall validation, warmup, process lifecycle, and host/emulator scheduling. The reported worker P95 is averaged across workers, not a pooled P95. Read the [observed ranges and controls](benchmarks/table.md) before citing a number. Three runs do not establish statistical significance or identify the cause of a slowdown.

A good answer to "Why did performance get worse?" is: "The data shows a regression in that configuration. The controls also vary, and I have not isolated the cause. I would repeat under a controlled host load and profile the scheduler and synchronization paths before attributing it to the sequence allocator."

## Evidence you can show

- [Host sanitizer transcript](benchmarks/host-tests.log): actual logging/device sources with platform stubs, eight concurrent writers, formatter boundaries, sparse IDs, overwrite accounting, and allocation/copy failure cleanup.
- [Full QEMU run](benchmarks/validation.log): 12 configurations, each with syscall/device assertions and an 18,000-write stress workload.
- [Raw measurements](benchmarks/results.csv) and [environment/source hashes](benchmarks/environment.json): 72 rows with the compiler/emulator environment.
- [A fresh demo transcript](benchmarks/demo-run.log): four CPUs detected, test pass markers, log viewing, and a completed 65,536-event benchmark.

Host stubs do not prove interrupt or real hardware behavior. QEMU does not prove physical AMD performance. No forced full-kernel allocator exhaustion test was run; device allocation failure was injected in the host suite.

## Accurate resume wording

- Improved an x86 xv6 kernel logging subsystem by repairing stack-safety and formatting defects, implementing consistent ordered snapshots and per-open reader cursors, and tracking ring overwrites.
- Compared global-ring, per-CPU spinlock, and atomic-sequence designs using sanitizer checks, stress tests across 12 QEMU configurations, and 72 benchmark measurements at 1–8 emulated CPUs.

Avoid claiming an entire OS built from scratch, a lock-free logger, nanosecond timestamps, persistent logs, or an AMD hardware speedup. Be ready to explain every design and test you list.

## Five-minute demo

Follow [DEMO.md](DEMO.md). Start with the four-CPU boot message, run the assertion suite and stress test, show the viewer, compare enabled/control calls, then explain one non-monotonic benchmark result and one current limitation.
