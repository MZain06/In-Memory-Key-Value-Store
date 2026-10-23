#!/usr/bin/env python3
"""Draw the benchmark charts from a results folder and print the numbers behind them."""

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.ticker import FuncFormatter, LogLocator, NullFormatter, NullLocator

SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
COLOURS = {"get_hit": "#2a78d6", "get_miss": "#eb6834", "insert": "#1baf7a", "mixed": "#eda100"}
WORKLOADS = ["get_hit", "get_miss", "insert", "mixed"]
# glibc rounds the entry (48 B), key (17 B) and value (32 B) allocations up to 64 + 32 + 48 bytes.
BYTES_PER_ENTRY = 144

plt.rcParams.update(
    {
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "savefig.facecolor": SURFACE,
        "axes.edgecolor": AXIS,
        "axes.linewidth": 0.8,
        "axes.grid": True,
        "axes.axisbelow": True,
        "grid.color": GRID,
        "grid.linewidth": 0.8,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.labelcolor": INK_2,
        "xtick.color": INK_2,
        "ytick.color": INK_2,
        "text.color": INK,
        "font.size": 10,
        "legend.frameon": False,
    }
)


def summarise(path):
    df = pd.read_csv(path)
    df["mops"] = df["throughput_ops_s"] / 1e6
    summary = df.groupby(["workload", "keys", "max_load"], as_index=False).agg(
        mops=("mops", "median"),
        mops_min=("mops", "min"),
        mops_max=("mops", "max"),
        p50=("p50_ns", "median"),
        p50_min=("p50_ns", "min"),
        p50_max=("p50_ns", "max"),
        p99=("p99_ns", "median"),
        p99_min=("p99_ns", "min"),
        p99_max=("p99_ns", "max"),
        capacity=("capacity", "first"),
    )
    return summary, int(df["run"].max())


def ns_text(ns):
    if ns >= 1e6:
        return f"{ns / 1e6:.1f} ms"
    if ns >= 1e3:
        return f"{ns / 1e3:.1f} µs"
    return f"{ns:.0f} ns"


def ns_axis(value, _):
    for scale, unit in ((1e6, "ms"), (1e3, "µs")):
        if value >= scale:
            return f"{value / scale:g} {unit}"
    return f"{value:g} ns"


def keys_text(keys):
    return f"{keys // 1_000_000}M" if keys >= 1_000_000 else f"{keys // 1000}k"


def table_mb(keys, capacity):
    return (keys * BYTES_PER_ENTRY + capacity * 8) / 1e6


def mib(capacity):
    return capacity * 8 / 2**20


def spread_pct(r):
    return max(r.mops_max - r.mops, r.mops - r.mops_min) / r.mops * 100


def titled_figure(title, subtitle, columns, height, sharey=True, left=0.08):
    fig, axes = plt.subplots(1, columns, figsize=(10, height), sharey=sharey, squeeze=False)
    fig.subplots_adjust(
        left=left, right=0.98, top=0.76, bottom=0.2, wspace=0.12 if sharey else 0.25
    )
    fig.text(left, 0.95, title, fontsize=13, fontweight="bold", va="top")
    fig.text(left, 0.875, subtitle, fontsize=10, color=INK_2, va="top")
    return fig, axes[0]


def whiskers(ax, x, row, column, colour):
    low = row[column] - row[f"{column}_min"]
    high = row[f"{column}_max"] - row[column]
    ax.errorbar(
        [x], [row[column]], yerr=[[low], [high]], fmt="none", ecolor=colour, elinewidth=1, capsize=3
    )


def throughput_chart(main, runs, out):
    fig, axes = titled_figure(
        "Throughput by workload and table size",
        f"Median of {runs} runs in million operations per second; "
        "whiskers show the slowest and fastest run",
        4,
        3.8,
    )
    for ax, workload in zip(axes, WORKLOADS):
        rows = main[main.workload == workload].sort_values("keys")
        positions = list(range(len(rows)))
        ax.bar(positions, rows.mops, width=0.55, color=COLOURS[workload])
        for x, (_, row) in zip(positions, rows.iterrows()):
            whiskers(ax, x, row, "mops", INK_2)
            ax.annotate(
                f"{row.mops:.1f}",
                (x, row.mops_max),
                xytext=(0, 4),
                textcoords="offset points",
                ha="center",
                fontsize=9,
            )
        ax.set_xticks(positions, [keys_text(k) for k in rows["keys"]])
        ax.set_title(workload, fontsize=11, loc="left", color=INK)
        ax.grid(axis="x", visible=False)
    axes[0].set_ylim(0, main.mops_max.max() * 1.15)
    axes[0].set_ylabel("M ops/s")
    fig.supxlabel("keys in the table", fontsize=10, color=INK_2)
    fig.savefig(out / "throughput.png", dpi=150)
    plt.close(fig)


def latency_chart(main, runs, out):
    fig, axes = titled_figure(
        "Latency as the table grows",
        f"Median p50 and p99 of {runs} runs, log scale; whiskers show the run-to-run range. "
        "mixed (90% get_hit) is left out to keep it readable",
        2,
        4.2,
    )
    sizes = main[main.workload == "get_hit"].sort_values("keys")
    ticks = []
    for _, row in sizes.iterrows():
        mb = table_mb(row["keys"], row.capacity)
        ticks.append(
            f"{keys_text(row['keys'])} keys\n≈{mb:.1f} MB"
            if mb < 10
            else f"{keys_text(row['keys'])} keys\n≈{mb:.0f} MB"
        )
    for ax, column, name in zip(
        axes, ["p50", "p99"], ["p50 (the typical operation)", "p99 (1 in 100 is slower)"]
    ):
        for workload in ["get_hit", "get_miss", "insert"]:
            rows = main[main.workload == workload].sort_values("keys")
            ax.plot(
                rows["keys"],
                rows[column],
                color=COLOURS[workload],
                lw=2,
                marker="o",
                ms=5,
                label=workload,
            )
            for _, row in rows.iterrows():
                whiskers(ax, row["keys"], row, column, COLOURS[workload])
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xticks(list(sizes["keys"]), ticks)
        ax.xaxis.set_minor_locator(NullLocator())
        ax.set_xlim(sizes["keys"].min() / 1.6, sizes["keys"].max() * 1.6)
        ax.set_title(name, fontsize=11, loc="left", color=INK)
    axes[0].yaxis.set_major_locator(LogLocator(base=10, subs=(1.0, 2.0, 5.0)))
    axes[0].yaxis.set_minor_formatter(NullFormatter())
    axes[0].yaxis.set_major_formatter(FuncFormatter(ns_axis))
    axes[0].set_ylabel("time per operation")
    fig.legend(
        *axes[0].get_legend_handles_labels(), loc="upper right", bbox_to_anchor=(0.98, 0.99), ncol=3
    )
    fig.savefig(out / "latency.png", dpi=150)
    plt.close(fig)


def load_factor_chart(sweep, runs, default_load, out):
    fig, (left, right) = titled_figure(
        "What the load factor trades: speed for memory",
        f"1M keys. Left: median lookup throughput of {runs} runs. "
        "Right: memory used by the bucket array",
        2,
        4.0,
        sharey=False,
    )
    lines = {w: sweep[sweep.workload == w].sort_values("load") for w in ["get_hit", "get_miss"]}
    upper = max(lines, key=lambda w: lines[w].iloc[0].mops)
    for workload, rows in lines.items():
        left.plot(rows.load, rows.mops, color=COLOURS[workload], lw=2, marker="o", ms=5)
        for _, row in rows.iterrows():
            whiskers(left, row.load, row, "mops", COLOURS[workload])
        first = rows.iloc[0]
        offset, align = ((8, 6), "bottom") if workload == upper else ((8, -6), "top")
        left.annotate(
            workload,
            (first.load, first.mops),
            xytext=offset,
            textcoords="offset points",
            va=align,
            fontsize=9,
        )
    left.axvline(default_load, color=AXIS, lw=1, zorder=0)
    left.annotate(
        "default",
        (default_load, 0),
        xytext=(4, 4),
        textcoords="offset points",
        xycoords=("data", "axes fraction"),
        fontsize=9,
        color=INK_2,
    )
    left.set_ylim(bottom=0)
    left.set_ylabel("M ops/s")

    loads = lines["get_hit"]
    sizes = mib(loads.capacity)
    is_default = [abs(load - default_load) < 1e-9 for load in loads.load]
    shades = [INK_2 if d else MUTED for d in is_default]
    right.bar(loads.load, sizes, width=loads.load * 0.35, color=shades)
    for load, value, default in zip(loads.load, sizes, is_default):
        label = f"{value:.0f} MiB (default)" if default else f"{value:.0f} MiB"
        right.annotate(
            label, (load, value), xytext=(0, 4), textcoords="offset points", ha="center", fontsize=9
        )
    right.set_ylim(0, sizes.max() * 1.15)
    right.set_ylabel("bucket array (MiB)")
    right.grid(axis="x", visible=False)
    for ax in (left, right):
        ax.set_xscale("log", base=2)
        ax.set_xticks(list(loads.load), [f"{v:.2f}" for v in loads.load])
        ax.xaxis.set_minor_locator(NullLocator())
        ax.set_xlabel("actual load (keys ÷ buckets)")
    fig.savefig(out / "load_factor.png", dpi=150)
    plt.close(fig)


def resize_points(keys, max_load=0.75, capacity=8):
    points = []
    while capacity * max_load < keys:
        points.append(int(capacity * max_load))
        capacity *= 2
    return points


def timeline_chart(timeline, out):
    keys = int(timeline.first_insert.max()) + 1000
    fig, axes = titled_figure(
        f"Inserting {keys_text(keys)} keys into an empty table: the slowest insert in every 1,000",
        "Log scale. Grey lines mark the inserts that make the table double and move every entry",
        1,
        4.0,
        left=0.1,
    )
    ax = axes[0]
    for index in resize_points(keys):
        if index >= keys // 100:
            ax.axvline(index, color=AXIS, lw=1, zorder=0)
    ax.plot(timeline.first_insert, timeline.max_ns, color=COLOURS["insert"], lw=1)
    for _, row in timeline.nlargest(3, "max_ns").iterrows():
        ax.annotate(
            ns_text(row.max_ns),
            (row.first_insert, row.max_ns),
            xytext=(-6, 0),
            textcoords="offset points",
            ha="right",
            va="center",
            fontsize=9,
        )
    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(FuncFormatter(ns_axis))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: keys_text(int(v)) if v else "0"))
    ax.set_xlim(0, keys)
    ax.set_xlabel("insert number")
    ax.set_ylabel("slowest insert")
    ax.grid(axis="x", visible=False)
    fig.savefig(out / "insert_timeline.png", dpi=150)
    plt.close(fig)


def print_numbers(main, sweep, timeline, default_load):
    def row(workload, keys):
        return main[(main.workload == workload) & (main["keys"] == keys)].iloc[0]

    big, small = main["keys"].max(), main["keys"].min()
    hit, miss, hit_small = row("get_hit", big), row("get_miss", big), row("get_hit", small)
    size, small_size = keys_text(big), keys_text(small)
    print(
        f"get_hit at {size} keys: {hit.mops:.2f} M ops/s, p50 {ns_text(hit.p50)}, "
        f"p99 {ns_text(hit.p99)}, every run within ±{spread_pct(hit):.1f}%"
    )
    print(
        f"get_miss vs get_hit at {size} keys: {miss.mops:.2f} vs {hit.mops:.2f} M ops/s "
        f"({miss.mops / hit.mops:.2f}x)"
    )
    small_mb, big_mb = table_mb(small, hit_small.capacity), table_mb(big, hit.capacity)
    print(
        f"get_hit p50 from {small_size} to {size} keys: "
        f"{ns_text(hit_small.p50)} -> {ns_text(hit.p50)} ({hit.p50 / hit_small.p50:.1f}x), "
        f"table ≈{small_mb:.1f} MB -> ≈{big_mb:.0f} MB"
    )
    for workload in ["get_hit", "get_miss"]:
        rows = sweep[sweep.workload == workload].sort_values("load")
        low, high = rows.iloc[0], rows.iloc[-1]
        change = (high.mops / low.mops - 1) * 100
        print(
            f"{workload} at load {low.load:.2f} -> {high.load:.2f}: "
            f"{low.mops:.2f} -> {high.mops:.2f} M ops/s ({change:+.0f}%), "
            f"bucket array {mib(low.capacity):.0f} -> {mib(high.capacity):.0f} MiB"
        )
    hits = sweep[sweep.workload == "get_hit"].sort_values("load")
    roomiest, default = hits.iloc[0], hits[(hits.load - default_load).abs() < 1e-9].iloc[0]
    share = default.mops / roomiest.mops * 100
    print(
        f"get_hit at the default load {default.load:.2f}: {default.mops:.2f} M ops/s, "
        f"{share:.0f}% of the speed at {roomiest.load:.2f}, "
        f"with {mib(default.capacity):.0f} MiB of buckets instead of {mib(roomiest.capacity):.0f}"
    )
    worst = timeline.loc[timeline.max_ns.idxmax()]
    typical = ns_text(timeline.max_ns.median())
    print(
        f"slowest insert: {ns_text(worst.max_ns)} in the window from insert "
        f"{int(worst.first_insert):,}; typical window's slowest: {typical}"
    )
    largest = max(spread_pct(r) for _, r in main.iterrows())
    print(f"largest run-to-run spread in results.csv: ±{largest:.1f}%")


def write_summary(main, sweep, timeline, runs, out):
    lines = [
        "# Benchmark results",
        "",
        f"Medians of {runs} runs. Spread is how far the furthest run strayed from the median. "
        "The machine and build settings are in [env.txt](env.txt).",
        "",
        "| Workload | Keys | M ops/s | Spread | p50 | p99 |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for workload in WORKLOADS:
        for _, r in main[main.workload == workload].sort_values("keys").iterrows():
            lines.append(
                f"| {workload} | {r['keys']:,} | {r.mops:.2f} | ±{spread_pct(r):.1f}% "
                f"| {ns_text(r.p50)} | {ns_text(r.p99)} |"
            )
    lines += [
        "",
        "Load-factor sweep at 1M keys:",
        "",
        "| Max load | Actual load | Bucket array | get_hit M ops/s | get_miss M ops/s |",
        "| ---: | ---: | ---: | ---: | ---: |",
    ]
    hits = sweep[sweep.workload == "get_hit"].sort_values("load")
    misses = sweep[sweep.workload == "get_miss"].sort_values("load")
    for (_, h), (_, m) in zip(hits.iterrows(), misses.iterrows()):
        lines.append(
            f"| {h.max_load:g} | {h.load:.2f} | {mib(h.capacity):.0f} MiB "
            f"| {h.mops:.2f} | {m.mops:.2f} |"
        )
    worst = timeline.loc[timeline.max_ns.idxmax()]
    typical = ns_text(timeline.max_ns.median())
    lines += [
        "",
        f"Insert timeline: the slowest insert took {ns_text(worst.max_ns)} "
        f"(in the window from insert {int(worst.first_insert):,}); "
        f"the slowest insert in a typical window of 1,000 took {typical}.",
    ]
    (out / "summary.md").write_text("\n".join(lines) + "\n")


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "results")
    main_results, runs = summarise(folder / "results.csv")
    sweep, sweep_runs = summarise(folder / "load_factor.csv")
    sweep["load"] = sweep["keys"] / sweep.capacity
    timeline = pd.read_csv(folder / "insert_timeline.csv")
    default = main_results[
        (main_results.workload == "get_hit") & (main_results["keys"] == 1_000_000)
    ].iloc[0]

    throughput_chart(main_results, runs, folder)
    latency_chart(main_results, runs, folder)
    load_factor_chart(sweep, sweep_runs, default["keys"] / default.capacity, folder)
    timeline_chart(timeline, folder)
    write_summary(main_results, sweep, timeline, runs, folder)
    print_numbers(main_results, sweep, timeline, default["keys"] / default.capacity)
    print(f"Charts and summary.md written to {folder}/")


if __name__ == "__main__":
    main()
