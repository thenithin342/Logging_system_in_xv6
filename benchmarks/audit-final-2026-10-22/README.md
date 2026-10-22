# Capacity-matched engineering audit benchmark

This dataset evaluates the audited logger separately from the [historical pre-audit comparison](../README.md). Old CSVs, plots, metadata, and transcripts remain unchanged and are checked by `tests/check_artifacts.py`.

## Method and interpretation

Mode 0 uses one shared ring with a sequence spinlock; its capacity is set to 256 times the detected guest CPU count. Modes 1 and 2 use 256 records per CPU, with a sequence spinlock or bounded 32-bit CAS allocator respectively. This matches maximum active retention across designs. Actual retention remains partitioned in per-CPU modes: a hot CPU may overwrite its ring while another ring has unused space. Allocated metadata and the storage layout are not identical.

The runner verifies CPU count and the reported capacity for every configuration. It uses 1, 2, 4, and 8 guest CPUs with the same number of workers, three repetitions, and enabled/control calls in alternating order. Workers are not pinned and may migrate. Each warms for 256 calls then performs 16,384 workload calls, sampling 128 evenly spaced TSC intervals. The no-emission control still validates the same user string and crosses the same syscall boundary.

Throughput is workload events divided by host monotonic wall duration from command dispatch until the shell prompt. Warmup, process lifecycle, sample sorting, and coordination are included. This is syscall workload throughput, not isolated kernel insertion performance. Latency fields retain the historical `mean_cycles` name but contain raw serialized TSC ticks, not calibrated physical core cycles or nanoseconds. The final utility reads the full 64-bit TSC and rejects individual intervals that exceed the 32-bit sample representation; oversized host pauses cannot silently alias a low-word wrap.

Each worker selects index 121 of 128 sorted samples as its P95; the parent averages workers' P95 values. The report takes medians across runs. This is not a pooled P95. Paired wall overhead is `(enabled_duration / control_duration - 1) * 100` for the same run ID, summarized by its median. The chart's vertical ranges are observed minima/maxima, not confidence intervals. Three runs are limited evidence and modes execute in fixed order, so host drift and emulator scheduling remain confounders.

Overwrite deltas include warmup and process events. They count displaced retained records rather than reader-specific misses or rejected events at sequence exhaustion. Equal nominal capacity does not imply equal overwrite patterns. No physical AMD, cache-miss, NUMA, or universal atomic-speedup conclusion follows from this experiment.

## Reproduce into a fresh directory

```sh
python3 tests/run_host.py
python3 tests/run_qemu.py --benchmark --capacity-policy matched --repeats 3 --output-dir artifacts/benchmark
python3 benchmarks/summarize.py --input-dir artifacts/benchmark
```

Raw rows, environment/source hashes, guest transcripts, and a validation log live beside this report. The runner only writes `results.csv` on a completed matrix; failed runs retain `results.partial.csv`. Completed CSVs and the historical directory are protected against accidental overwrite. Matplotlib is only needed for chart generation.

## Results

All 72 measurements completed: three architectures, four CPU counts, enabled/control calls, and three repetitions. Guest assertions and stress passed in all 12 measured configurations. The measured kernel and utilities are from commit `3bbf038538fb938853af5630bcd923dfee9e5303`.

![Median workload throughput and observed ranges](throughput.png)

| Guest CPUs | Global ring events/s | Per-CPU + sequence lock events/s | Per-CPU + atomic sequence events/s |
| ---: | ---: | ---: | ---: |
| 1 | 14,978 | 19,229 | 27,108 |
| 2 | 24,655 | 24,618 | 38,816 |
| 4 | 20,001 | 18,449 | 35,688 |
| 8 | 11,650 | 12,649 | 12,275 |

These are medians of enabled end-to-end workload throughput. The atomic variant leads at 1–4 CPUs in this run; the sequence-lock variant has a slightly higher median at 8 CPUs, where observed ranges overlap. All designs slow at 8 CPUs. Controls also vary substantially across configurations, reinforcing the need to consider emulator and host effects. These observations do not isolate synchronization cost or establish hardware speedups.

Atomic-mode median paired wall overhead is 11.77%, 55.87%, 30.88%, and 48.37% at 1/2/4/8 CPUs respectively. It is computed from paired durations, so it need not equal the ratio of separately summarized throughputs. See the [complete table](table.md), [summary CSV](summary.csv), [raw measurements](results.csv), [environment metadata](environment.json), and [execution transcript](validation.log) for controls, latency, overwrites, capacities, and observed ranges. The full-range numeric tests are recorded in [integer-format-tests.log](integer-format-tests.log); host sanitizer results are in [host-tests.log](host-tests.log).
