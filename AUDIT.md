# Engineering audit

Baseline: default branch `main`, commit `dc1a0f5`; remote HEAD verified before edits. Work is on `audit/kernel-logging`, with no default-branch merge authorized.

## Verified baseline defects

1. A boundary probe set `global_seq` to `UINT_MAX-1`. Three writes streamed IDs `4294967294, 4294967295, 0` and moved the cursor back to 1. Unsigned numeric ordering and cursor comparisons cannot survive wrap.
2. Calling `klogdev_read` with `n=-1` returned 0 rather than -1. The comparison with unsigned `sizeof` bypassed the intended rejection. The normal syscall path already rejects negative lengths, so this is a helper-level validation defect.
3. GCC `-Wall -Wextra -Werror` rejected all three unused parameters of `klogdev_write`.
4. `tests/run_qemu.py` writes environment metadata and logs directly into the published historical benchmark directory, even for a smoke-only run. A partial benchmark also replaces `results.csv` in its finally block.
5. Mode 3 is advertised but was excluded from the host and guest suites. Fork-inherited reader cursor behavior was implemented but not directly asserted.
6. The benchmark CPUID inline assembly declares EAX only as an input although CPUID changes it. This is an invalid compiler contract even where the adjacent RDTSC currently masks the effect.

## Already-correct implementations, subject to rerun

The page-backed device/snapshot buffers avoid the original kernel stack overflow. Formatter appends reserve a terminator and handle INT_MIN via unsigned magnitude. Record padding is cleared. Writers hold a CPU ring from sequence assignment through publication; all-ring acquisition gives a consistent published snapshot. Ring lock order is ascending, with the sequence lock taken only by writers already holding one ring. Reader cursors commit only after copyout succeeds. Independent opens use independent file offsets; dup/fork share the file. Log reads are excluded from read instrumentation. The 46-record syscall limit is explicit and pointer arithmetic is bounded before copyout.

## Risks and scope

Snapshot readers pause writers and inode locking serializes reads. The shared atomic sequence location still contends. The original global-ring mode has less retention capacity than per-CPU modes. RDTSC values are uncalibrated counter ticks; measurements include syscall, lifecycle, and emulator costs. Host mutex stubs do not prove hardware spinlock behavior. Physical hardware, exhaustive OS verification, and full-kernel allocator fault injection remain outside the available environment.

The audit follows logging call sites through process/exec hooks, file operations, syscall argument dispatch, page mapping/copyout, interrupt masking and spinlocks, user utilities, Makefile rules, host/guest tests, benchmark generation, and public documentation. It is not a claim that every inherited xv6 subsystem is production hardened.

## Additional verified numeric defect

UBSan reproduced signed overflow in the inherited console `printint(INT_MIN, ...)`: `x = -xx` negated INT_MIN before assigning to an unsigned variable. The same expression existed in the user formatter. Both now compute unsigned magnitude without signed negation. The final formatter regression compiles the exact `printint` bodies with output sinks and ASan/UBSan, checks INT_MIN and unsigned hexadecimal output, and also checks full-range unsigned decimal in the user formatter. Actual guest tests assert `KLOG UINT CHECK: 4294967295`, exercising the real user printf parser.

The numeric review also corrected full-range display and interpretation: `klogdropped` has an unsigned user-side return type, the viewer prints unsigned IDs, and benchmark/stress output uses unsigned counters. The syscall still returns one 32-bit word in the same register; no syscall numbers, arguments, or record layout changed. Source clients should rebuild with the shared header rather than rely on signed comparisons. The benchmark now reads the full TSC and rejects oversized individual intervals instead of silently aliasing low-word wrap; guest tick differences use unsigned arithmetic.

## Fixes and regression coverage

| Area | Implementation | Evidence |
| --- | --- | --- |
| Sequence exhaustion | `klog.c: next_seq`, `klog_printf_internal`, `klog_printf_level`; `klog.h` | Eight concurrent host writers race over the last 31 IDs; terminal cursor, subsequent failure, and clear behavior are asserted. |
| Device lengths and cleanup | `klogdev.c: klogdev_read`, `klogdev_write` | Negative/short lengths, allocation failure, full/partial copy failure, retry cursor, ownership, interrupt-depth balance, and no leaked pages. |
| Consistent statistics | `klog.c: klog_get_dropped` | Ring acquisition follows the same global order and no longer sums different clear epochs. |
| User pointers | `sysproc.c: sys_getklog`, `sys_klogwrite` | Positive-length empty snapshots also validate mapped user pages. Guest guard-pointer, invalid address/count, literal/overlong message tests run in enabled and disabled builds. |
| Descriptor semantics | `klog_test.c` | Independent opens, shared dup cursors, inherited fork cursor, EOF, resume, whole records, and no read-feedback loop. |
| Numeric rendering | `console.c`, `printf.c`, `user.h`, `ulog_tool.c`, benchmark/stress programs | ASan/UBSan arithmetic tests, unsigned-return static assertion, real guest unsigned-output assertion. |
| Timing | `klog_bench.c: cycles` and sampling loop | CPUID declares EAX read/write; full TSC intervals are bounded before storing 32-bit samples. Real enabled/control execution checks output and event totals. |
| Modes and capacity | `klog.h`, `Makefile`, host/guest runners | Host modes 0–3 plus 2048-entry global storage; global retention set to 256 times guest CPU count for fairer comparisons. |
| Evidence preservation | `run_qemu.py`, `test_tools.py`, `check_artifacts.py` | Refuse historical/completed-result overwrite; partial results named separately; 20 original artifacts protected by canonical Git-blob hashes; malformed rows rejected. |

The original ABI record remains 88 bytes. New architecture choices and the bounded-CAS change are explained in the README and technical report. No existing regression was removed. Two tracked Finder `.DS_Store` metadata files were removed and ignored; generated OS binaries remain excluded, while useful historical documents and measured plot assets are retained.

## CI implementation and observed status

[The workflow](.github/workflows/ci.yml) installs Ubuntu build/QEMU prerequisites, validates documentation and immutable historical evidence, runs strict ASan/UBSan host tests, checks console/user numeric boundaries, performs a clean x86 build, and boots all four modes at 1 and 2 CPUs. It runs real guest assertions and stress, not just compilation. A failing command fails its step; an error summary and logs are retained as artifacts. Actions have read-only repository permissions and are pinned to verified commits of [checkout](https://github.com/actions/checkout) and [upload-artifact](https://github.com/actions/upload-artifact).

The optional manual `full_benchmark` job is gated behind successful fast validation and runs the full 1/2/4/8 CPU repeated comparison. It is not run on every push. Hosted verification succeeded for both the initial audit changes and the final numeric fixes: [final code CI run](https://github.com/thenithin342/Logging_system_in_xv6/actions/runs/39679881958). The comprehensive hosted benchmark job was skipped; local full benchmark execution is reported separately. Manual dispatch of a newly introduced workflow may require it to exist on the default branch; this PR does not merge it automatically.

## Measurement provenance

The 72 original pre-audit measurements and their figures remain unchanged. A completed intermediate capacity-matched dataset is retained under `benchmarks/audit-2026-10-22/`; it predates the later numeric fixes. An attempted loaded-section comparison did not establish equivalence, so the entire benchmark was repeated against the final code rather than treating the intermediate data as final performance. The intermediate dataset is explicitly labelled and no speedup is inferred between those runs.

Final measurements, summaries, variability, controls, and caveats are in [the final report](benchmarks/audit-final-2026-10-22/README.md). Raw TSC intervals are not physical core cycles or nanoseconds. The P95 statistic is a mean of worker P95s, not a pooled distribution. Matching active retention does not remove partitioning, migration, scheduler, or emulator effects. Three runs and fixed mode order do not establish statistical significance or a universal atomic advantage.

## Commands and outcomes

Commands below are shown from the repository root inside Ubuntu WSL. Output-directory paths passed by orchestration were absolute paths to the same directories.

| Command | Outcome |
| --- | --- |
| `git fetch origin` and `git ls-remote --symref origin HEAD` | Verified `main` at `dc1a0f5` before creating the feature branch. |
| Baseline temporary host probe including unchanged `klog.c`/`klogdev.c` | Reproduced ID wrap/cursor reset and negative-length acceptance; strict compilation exposed unused parameters. |
| `python3 tests/run_host.py` | Strict ASan/UBSan checks for modes 0–3 and enlarged global capacity; see final `host-tests.log`. |
| `python3 tests/test_integer_format.py` before fixes | Expected UBSan failure on signed INT_MIN negation; fixed-source rerun passes both formatters. |
| `make -C xv6-public clean` | Removed generated targets before the repeated clean build. |
| `python3 tests/run_qemu.py --cpus 1 4` | Initial audit smoke/stress checks passed for all four modes before the later numeric changes. |
| `python3 tests/run_qemu.py --benchmark --capacity-policy matched --output-dir benchmarks/audit-final-2026-10-22` | Final repeated workload and real guest checks; see `validation.log` and raw rows. |
| `python3 tests/run_qemu.py --modes 3 --cpus 1 2 4 8 --output-dir artifacts/disabled-audit` | All four configurations passed guest assertions and stress; see final `disabled-tests.log`. |
| `python3 benchmarks/summarize.py --input-dir benchmarks/audit-final-2026-10-22` | Validated 72 final rows and generated the summary, table, and inspected chart. |
| `python3 benchmarks/summarize.py --input-dir benchmarks/audit-2026-10-22 --check-only` | Validated 72 intermediate rows without rewriting evidence. |
| `python3 tests/test_tools.py` | All four tests passed: historical protection, invalid input, read-only validation, duplicate rows, and capacity mismatch. |
| `python3 tests/check_artifacts.py` | Checks local Markdown links and original artifact preservation. |

The final matrix completed all 72 measurements and guest checks in 12 enabled configurations. Median atomic-mode throughput was 27,108 / 38,816 / 35,688 / 12,275 events/s at 1/2/4/8 CPUs. All three variants slowed at 8 CPUs; overlapping ranges and variable controls prevent a universal speedup claim. The [complete table](benchmarks/audit-final-2026-10-22/table.md) includes all architectures, controls, observed ranges, and paired overheads.

Delivery uses feature branch `audit/kernel-logging`, with separate commits `dfd0cdf` (sequence/capacity correctness), `df6db6a` (CI and evidence protection), and `3bbf038` (numeric boundaries), followed by the documentation and measured-evidence commit. The final C/header/Makefile sources match the metadata captured for the final matrix. The feature branch is submitted for review against `main`; no default-branch merge is authorized or performed. The pull request description records the final delivery commit and CI status.

## Remaining limits and assessment

This is a bounded educational logger, not a production tracing system. Saturation stops logging until reboot; retained logs remain readable. Old void kernel hooks cannot propagate exhaustion. Counter statistics cover overwrites rather than every rejected event. Readers pause writers, the sequence location remains shared, and there is no access-control or persistence policy. RDTSC still needs platform validation; long timing intervals are rejected. Full-kernel allocator exhaustion, a pooled P95, isolated insertion cost, physical AMD/NUMA/cache-counter measurements, and exhaustive inherited xv6 verification were not performed. Inherited executable-stack/RWX linker warnings remain outside the logging-specific compiler warning cleanup.

The project supports a code-grounded systems interview discussion, especially memory safety, publication ordering, cursor ownership, loss accounting, and measurement limits. The supplementary guide contains 28 detailed questions. It is not evidence of native AMD performance or a guarantee of interview outcomes.
