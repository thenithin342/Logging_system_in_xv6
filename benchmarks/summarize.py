#!/usr/bin/env python3
"""Validate the raw benchmark matrix and rebuild its summary and chart."""
import argparse
import csv
import json
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
LABELS = {0: "Global ring", 1: "Per-CPU + sequence lock", 2: "Per-CPU + atomic sequence"}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=HERE)
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    directory = args.input_dir.resolve()
    metadata = json.loads((directory / "environment.json").read_text())
    matched = metadata.get("capacity_policy") == "matched"
    with (directory / "results.csv").open() as file:
        raw = list(csv.DictReader(file))
    groups = {}
    keys = set()
    for row in raw:
        for field in ("mode", "cpus", "workers", "enabled", "run", "events", "ticks",
                      "mean_cycles", "mean_worker_p95", "overwrites", "events_per_second"):
            row[field] = int(row[field])
        expected_capacity = row["cpus"]*256 if matched or row["mode"] != 0 else 256
        row["capacity"] = int(row.get("capacity", expected_capacity))
        assert row["capacity"] == expected_capacity, "capacity policy mismatch"
        key = (row["mode"], row["cpus"], row["enabled"], row["run"])
        assert key not in keys, f"duplicate row: {key}"
        keys.add(key)
        assert row["workers"] == row["cpus"] and row["events"] == row["cpus"]*16384
        assert row["mean_cycles"] > 0 and row["mean_worker_p95"] > 0
        duration = float(row["wall_seconds"])
        assert duration > 0 and abs(row["events_per_second"] - row["events"] / duration) < 2
        groups.setdefault(key[:3], []).append(row)
    assert set(groups) == {(mode, cpus, enabled) for mode in range(3)
                           for cpus in (1, 2, 4, 8) for enabled in (0, 1)}
    repeats = {len(rows) for rows in groups.values()}
    assert len(repeats) == 1 and min(repeats) >= 3, "at least 3 runs per combination required"
    run_count = next(iter(repeats))
    assert all({r["run"] for r in rows} == set(range(1, run_count+1)) for rows in groups.values())
    summary = []
    for (mode, cpus, enabled), rows in sorted(groups.items()):
        rates = [row["events_per_second"] for row in rows]
        summary.append(dict(mode=mode, cpus=cpus, enabled=enabled, runs=len(rows),
                            median_events_per_second=round(statistics.median(rates)),
                            min_events_per_second=min(rates), max_events_per_second=max(rates),
                            median_mean_cycles=round(statistics.median(row["mean_cycles"] for row in rows)),
                            median_mean_worker_p95=round(statistics.median(row["mean_worker_p95"] for row in rows)),
                            median_overwrites=round(statistics.median(row["overwrites"] for row in rows)),
                            active_capacity=rows[0]["capacity"],
                            paired_wall_overhead_percent=round(statistics.median(
                                (float(row["wall_seconds"])/float(next(control["wall_seconds"]
                                 for control in groups[(mode, cpus, 0)] if control["run"] == row["run"]))-1)*100
                                for row in rows), 2) if enabled else 0))
    if args.check_only:
        print(f"Validated {len(raw)} raw measurements and their capacity/control groups")
        return
    with (directory / "summary.csv").open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    table = ["| Variant | CPUs | Events/s median (min–max) | Control events/s | Mean cycles | Mean worker P95 cycles | Overwrites | Active capacity | Paired wall overhead |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in summary:
        if row["enabled"] != 1:
            continue
        control = next(r for r in summary if r["mode"] == row["mode"] and r["cpus"] == row["cpus"] and r["enabled"] == 0)
        table.append(f"| {LABELS[row['mode']]} | {row['cpus']} | {row['median_events_per_second']:,} "
                     f"({row['min_events_per_second']:,}–{row['max_events_per_second']:,}) | "
                     f"{control['median_events_per_second']:,} | {row['median_mean_cycles']:,} | "
                     f"{row['median_mean_worker_p95']:,} | {row['median_overwrites']:,} | "
                     f"{row['active_capacity']:,} | {row['paired_wall_overhead_percent']:.2f}% |")
    (directory / "table.md").write_text("\n".join(table)+"\n")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter
    fig, ax = plt.subplots(figsize=(10, 6), dpi=150)
    for mode, color, marker in ((0, "#586474", "o"), (1, "#156f9c", "s"), (2, "#b65b19", "^")):
        rows = [r for r in summary if r["mode"] == mode and r["enabled"] == 1]
        x = [r["cpus"] for r in rows]
        y = [r["median_events_per_second"] for r in rows]
        ax.plot(x, y, color=color, marker=marker, linewidth=2, label=LABELS[mode])
        for row in rows:
            ax.vlines(row["cpus"], row["min_events_per_second"], row["max_events_per_second"], color=color, alpha=0.5)
    ax.set(title="Logging throughput by emulated CPU count", xlabel="Emulated CPUs (one worker per CPU)",
           ylabel="End-to-end workload throughput (events/second)", xticks=[1, 2, 4, 8], ylim=(0, None))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value/1000:.0f}k"))
    ax.grid(axis="y", alpha=0.2)
    ax.spines[["top", "right"]].set_visible(False)
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.15), ncol=1, frameon=False)
    fig.text(0.07, 0.025, f"QEMU TCG · {'Matched active retention capacity' if matched else 'Historical unequal capacity'} · median and observed range of {run_count} runs\n"
             "Includes syscall, validation, warmup and process lifecycle. Source: results.csv beside this chart. No hardware speedup claim.", fontsize=8)
    fig.subplots_adjust(left=0.12, right=0.97, top=0.9, bottom=0.32)
    fig.savefig(directory / "throughput.png")
    fig.savefig(directory / "throughput.svg")
    plt.close(fig)
    print(f"Validated {len(raw)} measurements; wrote summary.csv, table.md, throughput.png/svg")

if __name__ == "__main__":
    main()
