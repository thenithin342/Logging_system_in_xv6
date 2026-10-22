#!/usr/bin/env python3
"""Check local Markdown links and immutable historical measurements."""
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    count = 0
    for doc in ROOT.rglob("*.md"):
        if any(part in (".git", "artifacts") for part in doc.relative_to(ROOT).parts):
            continue
        for link in re.findall(r"\]\(([^)]+)\)", doc.read_text(encoding="utf-8")):
            if "://" in link or link.startswith("#"):
                continue
            assert (doc.parent / link.split("#")[0]).exists(), f"{doc}: missing {link}"
            count += 1
    manifest = json.loads((ROOT / "benchmarks/historical-manifest.json").read_text())
    for name, expected in manifest["files"].items():
        actual = subprocess.check_output(["git", "hash-object", "--path="+name, name], cwd=ROOT, text=True).strip()
        assert actual == expected, f"Historical evidence changed: {name}"
    print(f"PASS: {count} local Markdown links; {len(manifest['files'])} historical artifacts unchanged")

if __name__ == "__main__":
    main()
