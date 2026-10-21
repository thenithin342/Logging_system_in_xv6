#!/usr/bin/env python3
"""Compile the actual logging/device code with pthread platform stubs and sanitizers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="xv6-klog-") as temp:
    for mode in (0, 1, 2):
        binary = str(Path(temp) / f"host-{mode}")
        subprocess.run(["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                        "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                        "-fsanitize=address,undefined", "-pthread", f"-DKLOG_MODE={mode}",
                        str(root / "tests/klog_host.c"), "-o", binary], check=True)
        subprocess.run([binary], check=True)
        print(f"PASS host mode={mode}, including device allocation/copy failure cleanup", flush=True)
