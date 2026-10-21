# xv6 Kernel Logging System

A bounded kernel event log with per-CPU rings, ordered snapshots, independent streaming readers, automated correctness tests, and reproducible contention benchmarks.

Built on the x86 version of xv6, this project explores kernel memory safety, multicore synchronization, device interfaces, and performance measurement. It compares a global ring, per-CPU rings with a shared sequence lock, and per-CPU rings with atomic sequence allocation.

**Verified:** sanitizer tests for all three implementations, syscall/device and stress tests in 12 QEMU configurations, and 72 benchmark measurements across 1, 2, 4, and 8 emulated CPUs. [Test evidence](benchmarks/validation.log) · [Measured results](benchmarks/README.md) · [Demo transcript](benchmarks/demo-run.log)

**Documentation:** [Live demo walkthrough](DEMO.md) · [Technical report](PROJECT_REPORT.md) · [Original xv6 documentation](xv6-public/README)

## Project origin and contributions

The starting point is [thenithin342/Logging_system_in_xv6](https://github.com/thenithin342/Logging_system_in_xv6) at commit `ee36653`. The inherited commits are attributed to Krishna Chaitanya. This version preserves that history and improves the existing implementation through memory-safety repairs, consistent retrieval, per-open cursors, overwrite accounting, atomic sequence allocation, automated tests, and measured comparisons. The original xv6 OS and initial logging subsystem are inherited work.

## Run it

Use Linux or Ubuntu WSL. Prerequisites: GCC with x86 `-m32` code generation, binutils, make, Perl, Python 3, and QEMU x86. On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential qemu-system-x86 python3
git clone https://github.com/thenithin342/Logging_system_in_xv6.git
cd Logging_system_in_xv6/xv6-public
make qemu-nox CPUS=4
```

At the xv6 shell:

```text
klog_test
klog_stress
ulog_tool
klog_bench 4 0
klog_bench 4 1
```

Exit QEMU with **Ctrl+A, then X**. `klog_test` creates the read-only `klog` device node in xv6's root directory. Each independent `open("klog", O_RDONLY)` starts at the oldest retained entry. No `/dev` directory is required.

On Windows, open Ubuntu using `wsl -d Ubuntu`, navigate to your checkout under `/mnt/c/...`, and run the same build command. This is an x86 teaching kernel; Windows GCC alone is not the supported build environment.

## What was fixed

| Original problem | Current behavior |
| --- | --- |
| Device reader put 5,632 bytes of entries on a 4,096-byte kernel stack | One allocated page; allocation/copy failures release resources |
| Device reads repeated old entries | A sequence cursor lives in each open file; `dup` and `fork` share it |
| Snapshot cutoff assumed contiguous IDs | Merge actual retained sequences across rings, newest 46 at most |
| Two-pass snapshots raced writers | Lock all rings in CPU order while selecting a consistent view |
| Logging reads generated more logging | Reads of the log device do not emit read events |
| Overwrites were never counted | Each overwritten record increments its ring's counter |
| Unsupported formats could overrun the message | Bound every append; test truncation and `INT_MIN`; zero unused message bytes |
| Requests were silently reduced to one page | Reject requests above the shared `KLOG_MAX_ENTRIES` limit (46) |
| Documentation claimed lock-free/nanoseconds/persistence | Ring spinlocks, raw TSC cycles, volatile memory; limitations below |

## Architecture

```mermaid
flowchart LR
  E[Kernel events / bounded klogwrite syscall] --> F[Format at most 63 bytes]
  F --> R[CPU ring + spinlock]
  S[Global atomic sequence counter] --> R
  R --> M[Ordered merge under all ring locks]
  M --> G[getklog: newest entries]
  M --> D[klog device: oldest unread entries]
  D --> C[Cursor in open file]
```

Each entry is 88 bytes: six 32-bit fields (sequence, TSC high/low, CPU, PID, level) and a 64-byte message. There are 256 records per CPU, statically allocated for `NCPU=8`. Writers disable local interrupts to keep CPU selection stable, format a bounded message, lock their ring, allocate an ID, fill the record, and publish the new head/count before unlocking.

Readers acquire all ring locks in increasing CPU order and release them in reverse order. This gives a point-in-time view of published entries and prevents a lower sequence from appearing after a reader advances past it. Retrieval merges the real sequence IDs rather than inferring contiguous ranges. It costs roughly `O(NCPU * returned_entries)` for snapshots; a stream may also scan skipped retained records. Snapshots block writers briefly, an intentional correctness/performance tradeoff.

`getklog(entries, n)` returns the newest records in increasing sequence order for `1 <= n <= 46`; zero returns zero, invalid counts or destinations return `-1`. The stream returns whole records, at most 46 per call, and returns zero when currently caught up. Reads can resume after later events arrive. An undersized nonzero buffer returns `-1`. Slow readers skip overwritten records; gaps in IDs and `klogdropped()` expose loss. An independent open has its own cursor; duplicated/inherited descriptors share the cursor. xv6's inode sleep lock serializes reads, including independent readers of the same device inode.

The new `klogwrite(level, literal)` syscall validates up to 64 bytes through user page mappings, rejects inaccessible or overlong strings, and treats the text literally. Levels 0–3 emit an event; level `-1` validates the same input without emitting, for the benchmark control. This is an educational OS API available to every process, with no log access control.

## Tests

From the repository root in Linux/WSL:

```sh
python3 tests/run_host.py
python3 tests/run_qemu.py
python3 tests/run_qemu.py --benchmark --repeats 3
```

The host suite runs the actual `klog.c` and `klogdev.c` with pthread/memory-copy platform stubs under AddressSanitizer and UndefinedBehaviorSanitizer. It checks integer/formatter boundaries, empty snapshots, gaps, exact overwrite counts, newest-entry selection, independent cursors, concurrent writers, and allocation/copy failure cleanup.

The QEMU suite boots xv6, asserts the requested number of CPUs is detected, runs syscall/device assertions and an 18,000-write stress workload with concurrent readers and snapshots, and fails on missing success markers, a panic, or a timeout. Its default CPU matrix is 1, 2, 4, and 8. A benchmark run repeats those checks in all three implementations. QEMU uses temporary disk overlays, so the runner does not change the base filesystem image. Python and QEMU are test tools, not kernel dependencies.

## Performance comparison

| Build option | Buffer | Sequence allocation |
| --- | --- | --- |
| `KLOG_MODE=0` | One shared 256-entry ring | Spinlock protected counter |
| `KLOG_MODE=1` | 256 entries per CPU | Shared sequence spinlock |
| `KLOG_MODE=2` (default) | 256 entries per CPU | Atomic fetch-and-add |

Mode 2 removes acquisition of the shared sequence spinlock. It still has ring spinlocks and a shared cache line for the sequence counter. Mode 0 keeps the same sequence allocator as mode 1 to isolate the change in buffer partitioning. Its retained capacity is smaller; overwrite counts must be interpreted with that difference in mind. `make KLOG_MODE=...` rebuilds the mode-sensitive objects automatically. Mode 3 is also available to disable emission at build time; it is not part of the correctness or performance comparison matrix.

See [measured results and methodology](benchmarks/README.md), [raw measurements](benchmarks/results.csv), and [test transcripts](benchmarks/validation.log). These are QEMU/WSL measurements, with no claim about native hardware speed or production workloads.

![Measured enabled logging throughput versus emulated CPU count](benchmarks/throughput.png)

## What the measurements show

Median enabled workload throughput from three QEMU runs per configuration:

| Emulated CPUs | Global ring | Per-CPU + sequence lock | Per-CPU + atomic sequence |
| ---: | ---: | ---: | ---: |
| 1 | 24,518 events/s | 22,831 events/s | 26,876 events/s |
| 2 | 30,359 events/s | 28,293 events/s | 42,364 events/s |
| 4 | 29,354 events/s | 29,037 events/s | 24,927 events/s |
| 8 | 9,178 events/s | 8,760 events/s | 10,893 events/s |

Atomic sequencing improves the observed median in some configurations and regresses at four CPUs. All variants slow down at eight emulated CPUs. This is a useful systems result: changing one synchronization mechanism does not guarantee linear scaling or a universal speedup. The [full table](benchmarks/table.md) includes controls, observed ranges, latency, and overwrites. Throughput includes syscall validation, warmup, process lifecycle, and emulator scheduling; it is not isolated kernel insertion throughput.


## Scope and limits

- All records are in RAM and disappear on reboot; a full ring overwrites its oldest record.
- The 32-bit sequence API assumes fewer than `2^32` events per boot. Sequence wrap is not supported; long-running use needs a wider ABI or explicit epoch handling.
- Timestamps are raw, uncalibrated `RDTSC` values. Ordering uses sequence IDs, and timestamps must not be interpreted as nanoseconds or assumed synchronized on arbitrary physical CPUs.
- Retrieval allocates one page and returns `-1` if allocation fails. It can copy some bytes before a later invalid destination page fails; on device failure the cursor is not advanced.
- Snapshot locking pauses writers; this is not a lock-free implementation or a production tracing subsystem.
- The benchmark includes syscall, input validation, and timing overhead. Its P95 is the average of workers' sample P95 values, not a pooled P95.

Read [the technical report](PROJECT_REPORT.md), [demo walkthrough](DEMO.md), and the original [xv6 README](xv6-public/README). `Documentation.pdf` is an inherited historical document and does not describe these changes; the current Markdown files and executable tests are authoritative.
