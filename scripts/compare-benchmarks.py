#!/usr/bin/env python3
import argparse
import json
import math
import statistics
from pathlib import Path


def read(directory: Path):
    rows = [json.loads(path.read_text(encoding="utf-8")) for path in sorted(directory.glob("round-*.json"))]
    if not rows:
        raise ValueError(f"no measured rounds in {directory}")
    for row in rows:
        if row.get("schema_version") != 2 or row.get("mode") != "closed_loop_acceptance":
            raise ValueError("incompatible benchmark schema/measurement mode")
        if row.get("setup_errors", 0) or not row.get("accepted_messages", 0):
            raise ValueError("failed setup or zero successful messages is not a performance baseline")
        if row.get("failed_messages") != 0 or row.get("error_rate") != 0:
            raise ValueError("failed or incomplete message accounting is not a performance baseline")
        if row.get("attempted_messages") != row["accepted_messages"]:
            raise ValueError("attempted and accepted message counts must match")
    return rows


def metric(rows, name):
    values = [float(row[name]) for row in rows]
    if not all(math.isfinite(value) and value >= 0 for value in values):
        raise ValueError(f"invalid metric {name}")
    return statistics.mean(values), statistics.stdev(values) if len(values) > 1 else 0.0


parser = argparse.ArgumentParser()
parser.add_argument("baseline", type=Path)
parser.add_argument("candidate", type=Path)
parser.add_argument("--output", type=Path, default=Path("build/bench/comparison.md"))
args = parser.parse_args()
baseline, candidate = read(args.baseline), read(args.candidate)
if len(baseline) != len(candidate):
    raise ValueError("baseline and candidate must contain the same number of rounds")
for before, after in zip(baseline, candidate):
    for key in ("connections", "configured_rate", "batch", "seed"):
        if before[key] != after[key]:
            raise ValueError(f"incomparable workload: {key}")
names = ["throughput", "accept_p50_ms", "accept_p95_ms", "accept_p99_ms", "error_rate"]
lines = ["# 基线与候选版本性能对比", "", "| 指标 | baseline mean ± sd | candidate mean ± sd | 变化 |",
         "|---|---:|---:|---:|"]
for name in names:
    before, before_sd = metric(baseline, name); after, after_sd = metric(candidate, name)
    change = f"{(after - before) / before * 100:+.2f}%" if before else "N/A (baseline is zero)"
    lines.append(f"| {name} | {before:.3f} ± {before_sd:.3f} | {after:.3f} ± {after_sd:.3f} | {change} |")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(args.output)
