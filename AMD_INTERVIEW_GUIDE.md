# Kernel logging: systems interview study guide

This supplementary guide explains the implemented x86 xv6 project through questions relevant to OS and computer architecture discussions. It is not an AMD hiring rubric. Tests and measurements use QEMU TCG on Ubuntu WSL; no physical AMD performance claim is made. The public project overview remains in [README.md](README.md).

## Project explanation

**Pitch:** I improved an existing xv6 kernel logger by correcting memory-safety and retrieval defects, defining independent streaming readers and consistent snapshots, and comparing global versus per-CPU storage with locked versus atomic sequence allocation. The project includes strict sanitizer tests, real guest assertions, repeated emulator benchmarks, and CI. A final audit added safe sequence exhaustion without changing the 88-byte record ABI. Results are reported with controls, observed variability, and explicit emulator limits.

The problem is capturing recent kernel events without printing each one synchronously to the console. Each ring stores fixed-size records with sequence, raw TSC, CPU, PID, severity, and a bounded message. A syscall returns the newest records; a character device walks retained records using a cursor in the open file. Upstream xv6 and the initial logging implementation are inherited work, with attribution retained in Git history and the README.

## Architecture and engineering decisions

Read [klog.h](xv6-public/klog.h), then `klog_printf_internal`, `next_seq`, and `collect` in [klog.c](xv6-public/klog.c). Follow `sys_getklog` and `sys_klogwrite` in [sysproc.c](xv6-public/sysproc.c), then `fileread` in [file.c](xv6-public/file.c) and `klogdev_read` in [klogdev.c](xv6-public/klogdev.c).

Writers disable local interrupts before choosing a CPU, format a bounded message, lock one ring, reserve an ID, populate the entry, and publish head/count before releasing the lock. Readers lock every ring in increasing order and merge actual retained IDs. Device readers allocate one temporary page before taking ring locks; the inode sleep lock protects the shared file cursor. Atomic numbering removes the sequence spinlock but keeps ring locking and shared cache-line traffic.

The original 5,632-byte local device array exceeded a 4,096-byte kernel stack. A page allocation fixed that, with cleanup on error. The sparse-sequence snapshot algorithm needed an actual merge rather than a cutoff based on contiguous IDs. A per-open cursor fixed repeated device records. Excluding log-device reads from instrumentation fixed a feedback loop. The final audit reproduced 32-bit ID wrap, then made ID allocation saturate rather than reuse values.

## Technical questions and code-grounded answers

### 1. Why per-CPU rings rather than one global ring?

Per-CPU rings partition the write lock and storage. Multiple CPUs can format and publish to different rings concurrently. They still share the sequence location, and readers briefly lock every ring. The experiment compares partitioning explicitly rather than assuming it always improves throughput.

### 2. What does disabling local interrupts achieve?

`pushcli` runs before `cpuid` and remains balanced through publication or error return. It prevents timer-driven scheduling and migration from invalidating the chosen CPU's state during that operation. It does not stop another CPU from accessing shared memory, so the ring and sequence synchronization are still necessary.

### 3. Is the logger lock-free?

No. `next_seq` in mode 2 uses a 32-bit atomic compare-and-swap retry loop, but payload publication still takes a ring spinlock. A property of the counter operation does not describe the entire logger or reader path.

### 4. Why did the audit replace fetch-and-add with compare-and-swap?

Unconditional fetch-and-add eventually wraps a 32-bit counter to zero. `next_seq` now checks for the terminal value and uses CAS to change a valid ID to its successor only if the expected counter still matches. On a failed CAS it retries using the observed value. This preserves 32-bit operations and the record ABI while avoiding ID reuse.

### 5. What happens at the sequence boundary?

Published IDs range from 1 through `UINT_MAX-1`; zero represents failed reservation and `UINT_MAX` is the terminal cursor. After `2^32-2` emitted events, further active-mode reservations fail. `klogwrite` returns -1; existing kernel void logging call sites cannot report the failure. Retained records remain readable in order, clear preserves exhaustion, and reboot starts a new volatile epoch. Tests start 31 IDs below exhaustion and race eight host writers.

### 6. Why not just use a 64-bit atomic counter?

The target is 32-bit xv6, whose 88-byte user-visible record contains a 32-bit ID. A wider atomic could require instructions or runtime helpers outside the current target assumptions, and a wider record would change the ABI. Safe saturation is a smaller compatible policy; a future wider or epoch-based protocol needs a separate migration design.

### 7. What makes the payload visible safely?

The ring lock covers sequence assignment, every payload field, message clearing/copying, and head/count publication. The existing xv6 acquire/release implementation supplies synchronization barriers. Atomic uniqueness alone does not publish the rest of the entry.

### 8. Why allocate the ID while holding the ring lock?

Readers acquire every ring before selecting entries. A writer that reserved an ID still holds its ring until its entry is complete, so a reader cannot advance a cursor past that unpublished reservation. Reserving before taking the ring could violate this ordering argument.

### 9. Can lock ordering deadlock?

Writers take one ring and then, in locked-counter modes, the sequence lock. Readers and clear/statistics operations take rings in increasing order and do not take the sequence lock. No sequence-lock holder waits for another ring. Explain this dependency graph; a short critical section alone is not proof against deadlock.

### 10. What consistency guarantee do snapshots provide?

`collect` holds all ring locks while selecting entries. Once the final lock is acquired, the frozen ring states describe one consistent set of published records. It returns the newest bounded selection in ascending ID order. The cost is stopping writers briefly, unlike a weaker independently copied view.

### 11. How does retrieval handle gaps?

`collect` compares real IDs at each ring's traversal position. Independent overwrites can leave an old range on one CPU and a much newer range on another. Adding a skip count to the minimum ID cannot select the newest records reliably; the host test constructs this unbalanced case and checks the actual newest IDs.

### 12. What distinguishes head from count?

Head is always modulo the ring size; count is bounded by capacity. Once full, a write overwrites the oldest slot and increments the ring's overwrite counter. Occupancy is not inferred from a monotonically increasing head, avoiding a separate head-overflow failure.

### 13. Are overwrite counts the same as reader-specific loss?

No. They count displaced retained records, whether or not a particular reader already consumed them. A slow reader can observe ID gaps, and independent rings have different overwrite patterns. Exhaustion rejections are also separate from overwrites; the counter is not a total of every possible logging failure.

### 14. How do independent opens, dup, and fork behave?

Each independent open initializes a different `struct file.off` cursor. `dup` and fork inherit references to the same file, so they share its cursor. The inode sleep lock serializes these reads, including independent opens on the same inode. Guest tests explicitly read in a child and verify that the parent's next read advances beyond it.

### 15. What is EOF for this device?

A read returns zero when no retained ID is at or beyond the current cursor. It is a nonblocking current end, so the same descriptor can read a later event without reopening. Log-device reads are excluded from read instrumentation to avoid generating an event for each observation.

### 16. What happens if copying to userspace fails?

`klogdev_read` uses a local tentative cursor and commits `f->off` only after successful copyout. It frees the temporary page on failure. Some destination bytes may have been written before an error; the cursor remains unchanged, so retrying does not silently skip records. Host tests inject both full and partial copy failure.

### 17. How is the kernel stack protected?

The old 64-record array needed 64 times 88, or 5,632 bytes, exceeding the entire 4,096-byte kernel stack before nested frames. Retrieval now allocates one page and caps returns at 46 whole entries. Strict compilation rejects logging frames over 1,024 bytes; that is a per-frame guard, not a proof of maximum nested stack depth.

### 18. How are user pointers validated?

`sys_getklog` rejects invalid counts and compares requested records against `(process_size-address)/entry_size` before adding lengths. It validates the requested user pages, including guard-page permissions, even for an empty snapshot. `sys_klogwrite` reads at most 64 bytes through `uva2ka` and uses the result as literal text. Zero is mapped in this xv6 layout, so NULL must not be assumed universally unmapped.

### 19. What formatter bugs were addressed?

Every append leaves space for a NUL terminator, including unsupported format specifiers near the end. INT_MIN is converted through unsigned magnitude rather than signed negation. Message padding is cleared so a shorter new message does not expose bytes from the previous record. ASan/UBSan tests exercise the actual formatter and every unknown-format boundary.

### 20. How does a spinlock differ from a sleep lock here?

A spinlock retries without yielding and is used for short, non-sleeping ring updates. A sleep lock lets another process run while its owner is busy. Device operations hold the inode sleep lock, allocate a page, and then take ring spinlocks only for collection. The code does not sleep or allocate while holding a ring lock.

### 21. What is true sharing versus false sharing?

All writers update the same sequence location, which is true sharing. False sharing occurs when unrelated frequently written data shares a cache line. Removing a sequence spinlock does not remove ownership traffic for the atomic location. This project does not measure hardware cache misses or establish a false-sharing improvement.

### 22. Are the latency values physical CPU cycles or nanoseconds?

The CSV field `mean_cycles` is retained for compatibility, but the samples are raw TSC intervals. The utility reads both TSC words and rejects a measured interval larger than the 32-bit sample limit rather than truncating it. They include syscall entry/exit, input validation, and CPUID timing overhead. TSC frequency and cross-CPU behavior must be validated on physical hardware before any conversion or hardware interpretation.

### 23. Is the global-versus-per-CPU benchmark fair?

The new runner sets global capacity to 256 times the emulated CPU count, matching the maximum capacity usable by active per-CPU rings. It verifies the guest CPU count and reported capacity. Per-CPU retention is still partitioned, workers can migrate, and one hot CPU can overwrite while another has space. Equal nominal capacity does not make every retention pattern identical. Historical unequal-capacity results are preserved separately.

### 24. What does P95 mean in this benchmark?

Each worker takes 128 evenly spaced serialized samples. It sorts its own sample array and selects index 121, then the parent averages worker P95 values. The report takes the median across three runs. This is not a pooled P95; explain the aggregation before presenting it as a tail-latency measure.

### 25. How is relative overhead calculated?

For each mode, CPU count, and run ID, enabled wall duration is divided by its corresponding control duration, minus one, then multiplied by 100. The report takes the median of those paired percentages. A control validates the same syscall input without emitting. Wall duration includes warmup and process lifecycle, so this is workload overhead rather than pure insertion cost.

### 26. What should you say about unexpected scaling results?

Use the [current matched-capacity report](benchmarks/audit-final-2026-10-22/README.md) and its observed ranges, not a promised speedup. Historical data showed an atomic improvement in some configurations and a regression at four CPUs. Emulation, scheduler locks, validation, sample timing, and host contention are plausible influences, not isolated causes. More repetitions, controlled host load, profiling, and physical hardware counters would be needed to attribute a regression.

### 27. What do host tests and guest tests each prove?

Host tests execute the actual logger/device sources with pthread and copy/allocator stubs under ASan/UBSan, including forced boundary conditions. They check mutex ownership and balanced interrupt-depth bookkeeping but do not execute hardware interrupt instructions. Guest tests boot the actual xv6 kernel and exercise syscall, file, spinlock, and process paths. Neither proves behavior on physical AMD silicon or every possible interleaving.

### 28. What would production work require?

A deliberate sequence epoch or wider ABI, access control, timestamp calibration, persistence and loss policy, bounded reader interference, and hardware evaluation. Pure kernel insertion performance, a pooled latency distribution, full-kernel allocation fault injection, NUMA effects, and hardware event counters were not measured here. The current design is an educational systems project with explicit contracts.

## Demo and attribution

Use [DEMO.md](DEMO.md), show the detected CPU count and test markers, then explain a safety fix and a benchmark limitation. State which OS and logging code was inherited and which improvements are present in the feature-branch commits. Do not describe the logger as lock-free, persistent, production-ready, or universally faster.
