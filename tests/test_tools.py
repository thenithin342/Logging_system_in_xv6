#!/usr/bin/env python3
"""Regression tests for evidence preservation and result validation."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ToolTests(unittest.TestCase):
    def invoke(self, script, *args):
        return subprocess.run([sys.executable, str(ROOT/script), *map(str, args)],
                              cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_refuse_historical_overwrite(self):
        path = ROOT/"benchmarks/environment.json"
        before = path.read_bytes()
        result = self.invoke("tests/run_qemu.py", "--output-dir", ROOT/"benchmarks")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertEqual(path.read_bytes(), before)

    def test_invalid_requests_fail_before_build(self):
        for args in (("--benchmark", "--modes", "3"), ("--repeats", "0"), ("--cpus", "1", "1")):
            result = self.invoke("tests/run_qemu.py", *args)
            self.assertEqual(result.returncode, 2, result.stdout)

    def test_validate_historical_measurements_without_rewriting(self):
        path = ROOT/"benchmarks/summary.csv"
        before = path.read_bytes()
        result = self.invoke("benchmarks/summarize.py", "--check-only")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(path.read_bytes(), before)

    def test_duplicate_or_capacity_mismatch_is_rejected(self):
        with (ROOT/"benchmarks/results.csv").open() as file:
            rows = list(csv.DictReader(file))
        for malformed, policy, reason in ((rows + [rows[0]], "legacy", "duplicate row"),
                                          ([dict(row, capacity=1) for row in rows], "matched", "capacity policy mismatch")):
            with tempfile.TemporaryDirectory() as temp:
                directory = Path(temp)
                (directory/"environment.json").write_text(json.dumps({"capacity_policy":policy}))
                with (directory/"results.csv").open("w", newline="") as file:
                    writer = csv.DictWriter(file, fieldnames=list(malformed[0]))
                    writer.writeheader()
                    writer.writerows(malformed)
                result = self.invoke("benchmarks/summarize.py", "--input-dir", directory, "--check-only")
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(reason, result.stdout)
                self.assertFalse((directory/"summary.csv").exists())

if __name__ == "__main__":
    unittest.main()
