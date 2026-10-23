#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
out="${1:-results}"
bench=build/release/bin/bench
cpu=$(($(nproc) - 1))

rm -rf build/release
make --no-print-directory bench
mkdir -p "$out"
rm -f "$out/results.csv" "$out/load_factor.csv"

{
    echo "date:     $(date -u '+%Y-%m-%d %H:%M UTC')"
    echo "commit:   $(git describe --always --dirty)"
    echo "cpu:      $(lscpu | sed -n 's/^Model name: *//p' | head -n 1)"
    echo "cores:    $(nproc), benchmark pinned to CPU $cpu"
    echo "memory:   $(free -h | awk '/^Mem:/ {print $2}')"
    echo "os:       $(sed -n 's/^PRETTY_NAME="\(.*\)"$/\1/p' /etc/os-release), kernel $(uname -r)"
    echo "compiler: $(${CC:-gcc} --version | head -n 1)"
    echo "flags:    $(sed -n 's/^RELEASE_FLAGS := //p' Makefile)"
    echo "clock:    $(cat /sys/devices/system/clocksource/clocksource0/current_clocksource 2>/dev/null || echo unknown)"
} >"$out/env.txt"
cat "$out/env.txt"

for workload in insert get_hit get_miss mixed; do
    for keys in 10000 100000 1000000; do
        taskset -c "$cpu" "$bench" -w "$workload" -k "$keys" -o "$out/results.csv"
    done
done

for load in 0.25 0.5 1 2 4; do
    for workload in get_hit get_miss; do
        taskset -c "$cpu" "$bench" -w "$workload" -k 1000000 -l "$load" -o "$out/load_factor.csv"
    done
done

echo "Done: $out/results.csv, $out/load_factor.csv and $out/env.txt"
