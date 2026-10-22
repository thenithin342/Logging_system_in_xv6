# Kernel logging demonstration


From Linux/WSL, run `cd xv6-public && make qemu-nox CPUS=4`.
The boot line must say `klog: detected 4 CPUs` before discussing multicore results.

1. Run `klog_test`. Explain the assertions behind `KLOG TEST PASS`: invalid pointers and guard pages, explicit page limit, independent opens, shared `dup` cursor, EOF, and resuming after a new event.
2. Run `klog_stress`. Show `KLOG STRESS PASS`: 18,000 explicit writes, concurrent readers, snapshots, wraparound, and overwrite accounting.
3. Run `ulog_tool`. Explain sequence, CPU, PID, level, and message. Sequence numbers establish order; TSC fields hold raw cycles.
4. Run `klog_bench 4 0`, then `klog_bench 4 1`. The first validates input without emitting. Explain that both include syscall and timing overhead.
5. Show `benchmarks/results.csv` and `benchmarks/throughput.png`. Discuss variability and QEMU limits. Use the recorded data rather than promising linear scaling.
6. Exit QEMU with Ctrl+A, then X.

For a reproducible external run, use `python3 tests/run_host.py` and `python3 tests/run_qemu.py --benchmark` from the repository root. See the root README for prerequisites.

## Design notes

Per-CPU rings partition write locking and storage. The global sequence counter still creates cross-CPU traffic, and consistent snapshots briefly lock all rings. Bounded atomic CAS sequence allocation removes the shared sequence spinlock acquisition; ring updates remain protected by spinlocks.

Each full ring overwrites its oldest entry and increments its overwrite count. Retrieval merges actual retained sequence IDs, so independently overwritten rings can leave gaps without breaking ordering. Independent opens have separate cursors; duplicated and inherited descriptors share the same cursor.

Timestamps contain raw TSC values rather than calibrated wall time. The benchmark includes syscall and timing overhead, and its results describe the tested QEMU configuration. See [the technical report](PROJECT_REPORT.md) for synchronization details and [the benchmark report](benchmarks/README.md) for methodology and limitations.

Sequence IDs stop safely at exhaustion rather than wrapping; active user writes then return an error. For current fair-comparison results, see [the capacity-matched report](benchmarks/audit-final-2026-10-22/README.md).
