#!/usr/bin/env python3
"""Summarise one benchmark CSV, or compare the median throughput of two."""
import csv
import statistics
import sys


def load(path):
    groups = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            key = (row["workload"], int(row["keys"]), float(row["max_load"]))
            groups.setdefault(key, []).append(row)
    summary = {}
    for key, rows in groups.items():
        throughput = [float(r["throughput_ops_s"]) / 1e6 for r in rows]
        middle = statistics.median(throughput)
        summary[key] = {
            "mops": middle,
            "spread": max(abs(t - middle) for t in throughput) / middle * 100,
            "p50": statistics.median(float(r["p50_ns"]) for r in rows),
            "p99": statistics.median(float(r["p99_ns"]) for r in rows),
        }
    return summary


def show_one(summary):
    print(f"{'workload':<9}{'keys':>9}{'load':>6}{'M ops/s':>9}{'spread':>8}{'p50 ns':>8}{'p99 ns':>8}")
    for (workload, keys, load), s in sorted(summary.items()):
        print(
            f"{workload:<9}{keys:>9}{load:>6g}{s['mops']:>9.2f}{s['spread']:>7.1f}%"
            f"{s['p50']:>8.0f}{s['p99']:>8.0f}"
        )


def show_two(first, second):
    print(f"{'workload':<9}{'keys':>9}{'load':>6}{'A M ops/s':>11}{'B M ops/s':>11}{'change':>8}")
    largest = 0.0
    for key in sorted(first.keys() & second.keys()):
        a, b = first[key]["mops"], second[key]["mops"]
        change = (b - a) / a * 100
        largest = max(largest, abs(change))
        flag = "  <- over 5%" if abs(change) > 5 else ""
        workload, keys, load = key
        print(f"{workload:<9}{keys:>9}{load:>6g}{a:>11.2f}{b:>11.2f}{change:>+7.1f}%{flag}")
    print(f"largest change: {largest:.1f}%")


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: compare.py results.csv [other.csv]")
    summaries = [load(path) for path in sys.argv[1:]]
    if len(summaries) == 1:
        show_one(summaries[0])
    else:
        show_two(*summaries)


if __name__ == "__main__":
    main()
