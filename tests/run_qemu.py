#!/usr/bin/env python3
"""Boot real xv6 under QEMU, assert guest markers, optionally collect CSV data."""
import argparse
import csv
import hashlib
import json
import platform
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
XV6 = ROOT / "xv6-public"

class Guest:
    def __init__(self, cpus, log):
        self.log = log
        self.process = subprocess.Popen([
            "qemu-system-i386", "-accel", "tcg,thread=multi", "-nographic",
            "-monitor", "none", "-serial", "stdio", "-no-reboot", "-smp", f"{cpus},sockets={cpus},cores=1,threads=1",
            "-m", "512", "-drive", "file=fs.img,index=1,format=raw",
            "-drive", "file=xv6.img,index=0,format=raw", "-snapshot"],
            cwd=XV6, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        try:
            boot = self.until_prompt(60)
            assert f"klog: detected {cpus} CPUs" in boot, boot
        except BaseException:
            self.close()
            raise

    def until_prompt(self, timeout):
        data = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for key, _ in self.selector.select(0.25):
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    raise RuntimeError("QEMU exited: " + data.decode(errors="replace"))
                data += chunk
                self.log.write(chunk.decode(errors="replace"))
                self.log.flush()
            text = data.decode(errors="replace").replace("\r", "")
            if "panic:" in text or "KLOG TEST FAIL" in text or "KLOG STRESS FAIL" in text or "BENCH FAIL" in text:
                raise RuntimeError(text)
            if text.endswith("$ "):
                return text
        raise TimeoutError(data.decode(errors="replace"))

    def command(self, command, marker, timeout=120):
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        text = self.until_prompt(timeout)
        if marker not in text:
            raise AssertionError(f"Missing {marker!r}: {text}")
        return text

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.selector.close()
        self.process.stdin.close()
        self.process.stdout.close()

def build(mode, capacity=256, destination=None):
    destination = destination or ROOT / "artifacts" / "qemu"
    destination.mkdir(parents=True, exist_ok=True)
    path = destination / f"build-mode-{mode}.log"
    with path.open("w") as log:
        result = subprocess.run(["make", "-j4", f"KLOG_MODE={mode}",
                                 f"KLOG_GLOBAL_CAPACITY={capacity}", "fs.img", "xv6.img"],
                                cwd=XV6, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print(path.read_text()[-8000:], flush=True)
        result.check_returncode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cpus", type=int, nargs="+", default=[1, 2, 4, 8], choices=[1, 2, 4, 8])
    parser.add_argument("--modes", type=int, nargs="+", choices=[0, 1, 2, 3])
    parser.add_argument("--capacity-policy", choices=["legacy", "matched"], default="matched")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "artifacts" / "qemu")
    args = parser.parse_args()
    modes = args.modes if args.modes is not None else ([0, 1, 2] if args.benchmark else [0, 1, 2, 3])
    if args.repeats < 1 or len(set(args.cpus)) != len(args.cpus) or len(set(modes)) != len(modes):
        parser.error("repeats must be positive and CPU/mode lists must be unique")
    if args.benchmark and 3 in modes:
        parser.error("mode 3 is covered by smoke tests, not the emission benchmark")
    destination = args.output_dir.resolve()
    if destination == ROOT / "benchmarks" or (args.benchmark and (destination / "results.csv").exists()):
        parser.error("choose a fresh output directory; published benchmark results are preserved")
    destination.mkdir(parents=True, exist_ok=True)
    versions = {}
    for name, command in {"qemu": ["qemu-system-i386", "--version"],
                          "gcc": ["gcc", "--version"], "python": ["python3", "--version"]}.items():
        versions[name] = subprocess.check_output(command, text=True).splitlines()[0]
    versions.update(platform=platform.platform(), logical_host_cpus=os.cpu_count(),
                    accelerator="tcg,thread=multi", ram_mib=512,
                    repeats=args.repeats, guest_cpus=args.cpus, modes=modes,
                    benchmark=args.benchmark, capacity_policy=args.capacity_policy,
                    source_sha256={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in list(XV6.glob("*.c")) + list(XV6.glob("*.h")) + [XV6 / "Makefile"]})
    (destination / "environment.json").write_text(json.dumps(versions, indent=2)+"\n")
    rows = []
    complete = False
    try:
        for mode in modes:
            for cpus in args.cpus:
                capacity = cpus*256 if mode == 0 and args.capacity_policy == "matched" else 256
                build(mode, capacity, destination)
                with (destination / f"qemu-mode-{mode}-cpus-{cpus}.log").open("w") as log:
                    guest = Guest(cpus, log)
                    try:
                        output = guest.command("klog_test", "KLOG TEST PASS")
                        assert "KLOG UINT CHECK: 4294967295" in output, output
                        guest.command("klog_stress", "KLOG STRESS PASS")
                        print(f"PASS mode={mode} cpus={cpus}: guest assertions and stress", flush=True)
                        if args.benchmark:
                            for run in range(1, args.repeats+1):
                                for enabled in ([0, 1] if run % 2 else [1, 0]):
                                    started = time.monotonic()
                                    output = guest.command(f"klog_bench {cpus} {enabled}", "BENCH mode=", 180)
                                    wall_seconds = time.monotonic() - started
                                    match = re.search(r"BENCH mode=.*", output)
                                    row = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", match.group())}
                                    assert row["mode"] == mode and row["workers"] == cpus
                                    assert row["enabled"] == enabled and row["events"] == cpus*16384
                                    assert row["capacity"] == (capacity if mode == 0 else cpus*256)
                                    assert row["ticks"] > 0 and row["mean_cycles"] > 0
                                    row.update(cpus=cpus, run=run)
                                    row["wall_seconds"] = round(wall_seconds, 6)
                                    row["events_per_second"] = round(row["events"] / wall_seconds)
                                    rows.append(row)
                                    print(f"  run={run} enabled={enabled} events/s={row['events_per_second']}", flush=True)
                    finally:
                        guest.close()
        if args.benchmark:
            assert len(rows) == len(modes)*len(args.cpus)*args.repeats*2
        complete = True
    finally:
        build(2, 256, destination)
        if rows:
            filename = "results.csv" if complete else "results.partial.csv"
            with (destination / filename).open("w", newline="") as file:
                writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
    print("ALL QEMU CHECKS PASS", flush=True)

if __name__ == "__main__":
    main()
