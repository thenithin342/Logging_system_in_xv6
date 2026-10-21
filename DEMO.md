# Demo and interview preparation

For the project pitch, source walkthrough, AMD systems topics, and resume wording, read [AMD_INTERVIEW_GUIDE.md](AMD_INTERVIEW_GUIDE.md).

From Linux/WSL, run `cd xv6-public && make qemu-nox CPUS=4`.
The boot line must say `klog: detected 4 CPUs` before discussing multicore results.

1. Run `klog_test`. Explain the assertions behind `KLOG TEST PASS`: invalid pointers and guard pages, explicit page limit, independent opens, shared `dup` cursor, EOF, and resuming after a new event.
2. Run `klog_stress`. Show `KLOG STRESS PASS`: 18,000 explicit writes, concurrent readers, snapshots, wraparound, and overwrite accounting.
3. Run `ulog_tool`. Explain sequence, CPU, PID, level, and message. Sequence numbers establish order; TSC fields hold raw cycles.
4. Run `klog_bench 4 0`, then `klog_bench 4 1`. The first validates input without emitting. Explain that both include syscall and timing overhead.
5. Show `benchmarks/results.csv` and `benchmarks/throughput.png`. Discuss variability and QEMU limits. Use the recorded data rather than promising linear scaling.
6. Exit QEMU with Ctrl+A, then X.

For a reproducible external run, use `python3 tests/run_host.py` and `python3 tests/run_qemu.py --benchmark` from the repository root. See the root README for prerequisites.

## Questions to prepare

**Why per-CPU rings?** They partition the write lock and storage. A shared sequence counter still creates cross-CPU traffic, and consistent snapshots briefly lock all rings.

**Spinlock versus mutex?** A spinlock waits by retrying and is appropriate for short kernel critical sections where sleeping is forbidden. A sleep lock yields while waiting. xv6 ring locks disable local interrupts; device reads are serialized under the inode sleep lock, allowing temporary allocation and normal filesystem locking behavior.

**What happens at capacity?** A ring overwrites its oldest entry and increments its overwrite count. Readers can miss entries and observe gaps; it does not block producers to preserve every log.

**How do snapshots handle gaps?** They merge real retained IDs across locked rings. Counting how many records to skip and adding that count to a minimum ID is wrong when independently overwritten rings leave gaps.

**Why is the original device array unsafe?** 64 records times 88 bytes is 5,632 bytes, larger than xv6's 4,096-byte kernel stack before other frames are considered. Retrieval now uses one allocated page.

**How does reader independence work?** Each open file has a cursor. Independent opens have separate files; `dup` and `fork` share one file/cursor. Reads commit the cursor only after successful copyout.

**Is this lock-free?** No. Atomic sequence allocation removes one spinlock but writers still lock their ring. Atomic instructions also contend for cache-line ownership.

**Why RDTSC?** It provides a cheap raw cycle counter on this x86 target. It is not wall time, needs calibration for time units, and cannot be assumed synchronized across arbitrary physical CPUs. The benchmark serializes measurements and compares identical no-emission calls.

**Where is the next bottleneck?** Measure before deciding: the shared sequence counter, long reader critical sections, formatting/input validation, scheduler locks, and emulator scheduling all contribute. More CPUs may reduce throughput under QEMU.

**What did you contribute?** State the inherited baseline and name your repairs, tests, and measurements. Start with the stack-safety bug, cursor semantics, sparse-sequence ordering, and measured sequence allocator comparison. Do not claim the original xv6 implementation or unmeasured hardware speedups.

**What would you improve for production?** A wider sequence/epoch ABI, access policy, bounded reader lock hold time, hardware timestamp calibration, and an explicit persistence/loss policy. These need separate requirements and tests.
