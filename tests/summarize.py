#!/usr/bin/env python3
"""Render existing results into a concise GitHub job summary."""
import json
from common import OUT

for path in sorted(OUT.glob("*/results.json")):
    results = json.loads(path.read_text())
    counts = {status: sum(r["status"] == status for r in results)
              for status in ("pass", "fail", "error", "blocked")}
    print(f"### {path.parent.name}\n")
    print(", ".join(f"{count} {status}" for status, count in counts.items()) + "\n")
    for result in results:
        if result["status"] != "pass":
            print(f'- **{result["status"]}**: `{result["name"]}`')
    coverage = path.parent/"coverage-summary.txt"
    if coverage.exists():
        print("\nCoverage is limited to linked production sources; it is not whole-OS coverage.\n")
        print("```text\n"+coverage.read_text().strip()+"\n```\n")
