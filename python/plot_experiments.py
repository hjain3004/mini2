#!/usr/bin/env python3
"""
plot_experiments.py — Generate figures from experiment CSV data.

Reads from experiments/{chunk_size,local_vs_distributed,linear_vs_indexed}/results.csv
Outputs figures to report/figures/.
"""

import os
import csv
import sys
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
except ImportError:
    print("ERROR: matplotlib not installed. Run: pip3 install matplotlib", file=sys.stderr)
    sys.exit(1)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FIG_DIR = os.path.join(ROOT, "report", "figures")
os.makedirs(FIG_DIR, exist_ok=True)

# ─── Helpers ─────────────────────────────────────────────────────────────────

def load_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(row)
    return rows

def avg(vals):
    return sum(vals) / len(vals) if vals else 0

def set_style():
    plt.rcParams.update({
        "font.family": "sans-serif",
        "font.size": 11,
        "axes.titlesize": 13,
        "axes.labelsize": 12,
        "figure.facecolor": "white",
        "axes.facecolor": "#f8f8f8",
        "axes.grid": True,
        "grid.alpha": 0.3,
    })

# ─── Experiment 1: Chunk Size ────────────────────────────────────────────────

def plot_chunk_size():
    path = os.path.join(ROOT, "experiments", "chunk_size", "results.csv")
    if not os.path.exists(path):
        print("  skipping chunk_size (no data)")
        return
    rows = load_csv(path)

    # Group by chunk_records
    groups = defaultdict(list)
    for r in rows:
        cr = int(r["chunk_records"])
        groups[cr].append(float(r["total_ms"]))

    sizes = sorted(groups.keys())
    means = [avg(groups[s]) for s in sizes]
    labels = [str(s) for s in sizes]

    set_style()
    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar(labels, means, color=["#3b82f6", "#6366f1", "#8b5cf6", "#a855f7", "#c084fc"],
                  edgecolor="white", linewidth=1.5)

    for bar, val in zip(bars, means):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 30,
                f"{val:.0f}", ha="center", va="bottom", fontsize=10, fontweight="bold")

    ax.set_xlabel("Chunk Size (records per chunk)")
    ax.set_ylabel("Total Query Time (ms)")
    ax.set_title("§20.1 — Effect of Chunk Size on Query Latency\n(9-node distributed, borough=MANHATTAN)")
    ax.set_ylim(0, max(means) * 1.2)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "chunk_size.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ─── Experiment 3: Local vs Distributed ──────────────────────────────────────

def plot_local_vs_distributed():
    path = os.path.join(ROOT, "experiments", "local_vs_distributed", "results.csv")
    if not os.path.exists(path):
        print("  skipping local_vs_distributed (no data)")
        return
    rows = load_csv(path)

    local_times = [float(r["total_ms"]) for r in rows if r["label"].startswith("local")]
    dist_times = [float(r["total_ms"]) for r in rows if r["label"].startswith("distributed")]
    local_recs = [int(r["records"]) for r in rows if r["label"].startswith("local")]
    dist_recs = [int(r["records"]) for r in rows if r["label"].startswith("distributed")]

    set_style()
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    # Latency
    means = [avg(local_times), avg(dist_times)]
    colors = ["#f59e0b", "#10b981"]
    bars = ax1.bar(["Local\n(A only)", "Distributed\n(9 nodes)"], means,
                   color=colors, edgecolor="white", linewidth=1.5, width=0.5)
    for bar, val in zip(bars, means):
        ax1.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 20,
                 f"{val:.0f} ms", ha="center", va="bottom", fontsize=11, fontweight="bold")
    ax1.set_ylabel("Total Query Time (ms)")
    ax1.set_title("Query Latency")
    ax1.set_ylim(0, max(means) * 1.3)

    # Records
    rec_means = [avg(local_recs), avg(dist_recs)]
    bars2 = ax2.bar(["Local\n(A only)", "Distributed\n(9 nodes)"], rec_means,
                    color=colors, edgecolor="white", linewidth=1.5, width=0.5)
    for bar, val in zip(bars2, rec_means):
        ax2.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 5000,
                 f"{val:,.0f}", ha="center", va="bottom", fontsize=11, fontweight="bold")
    ax2.set_ylabel("Records Returned")
    ax2.set_title("Result Completeness")
    ax2.set_ylim(0, max(rec_means) * 1.2)
    ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x/1e6:.1f}M"))

    fig.suptitle("§20.4 — Local vs Distributed Query\n(borough=MANHATTAN, chunk_records=1000)",
                 fontsize=13, fontweight="bold", y=1.02)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "local_vs_distributed.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  saved {out}")

# ─── Experiment 4: Linear vs Indexed ─────────────────────────────────────────

def plot_linear_vs_indexed():
    # Prefer v2 (selective predicate, full dataset). Fall back to v1.
    path_v2 = os.path.join(ROOT, "experiments", "linear_vs_indexed", "results_v2.csv")
    path_v1 = os.path.join(ROOT, "experiments", "linear_vs_indexed", "results.csv")
    path = path_v2 if os.path.exists(path_v2) else path_v1
    if not os.path.exists(path):
        print("  skipping linear_vs_indexed (no data)")
        return
    rows = load_csv(path)
    is_v2 = path == path_v2

    idx_times = [float(r["total_ms"]) for r in rows if r["label"].startswith("indexed")]
    lin_times = [float(r["total_ms"]) for r in rows if r["label"].startswith("linear")]

    set_style()
    fig, ax = plt.subplots(figsize=(7, 5))

    means = [avg(idx_times), avg(lin_times)]
    colors = ["#10b981", "#ef4444"]
    idx_label = ("Indexed\n(complaint_type idx)" if is_v2
                 else "Indexed\n(borough index)")
    bars = ax.bar([idx_label, "Linear Scan\n(full table scan)"], means,
                  color=colors, edgecolor="white", linewidth=1.5, width=0.5)
    for bar, val in zip(bars, means):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 20,
                f"{val:.0f} ms", ha="center", va="bottom", fontsize=12, fontweight="bold")

    if means[1] > 0 and means[0] > 0:
        speedup = means[1] / means[0]
        ax.text(0.95, 0.95, f"Index speedup: {speedup:.1f}×",
                transform=ax.transAxes, ha="right", va="top",
                fontsize=12, fontweight="bold",
                bbox=dict(boxstyle="round,pad=0.4", facecolor="#ecfdf5", edgecolor="#10b981"))

    ax.set_ylabel("Total Query Time (ms)")
    title_pred = ('complaint_type="Noise - Helicopter", full dataset'
                  if is_v2 else "borough=MANHATTAN")
    ax.set_title(f"§20.5 — Indexed vs Linear Scan\n(A-only, {title_pred}, chunk_records=1000)")
    ax.set_ylim(0, max(means) * 1.25)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "linear_vs_indexed.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ─── Fairness: parse fairness test output ────────────────────────────────────

def plot_fairness():
    rr_path = os.path.join(ROOT, "experiments", "fairness", "round_robin.txt")
    gr_path = os.path.join(ROOT, "experiments", "fairness", "greedy.txt")

    if not os.path.exists(rr_path) or not os.path.exists(gr_path):
        print("  skipping fairness (no data)")
        return

    def parse_fairness(path):
        """Parse the output of fairness_test.py into a dict of label -> total_s."""
        results = {}
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("[") or line.startswith("-") or line.startswith("label"):
                    continue
                parts = line.split()
                if len(parts) >= 5:
                    try:
                        label = parts[0]
                        total_s = float(parts[4])
                        results[label] = total_s
                    except (ValueError, IndexError):
                        continue
        return results

    rr = parse_fairness(rr_path)
    gr = parse_fairness(gr_path)

    if not rr or not gr:
        print("  skipping fairness (could not parse data)")
        return

    # Common labels
    labels = sorted(set(rr.keys()) & set(gr.keys()))
    if not labels:
        print("  skipping fairness (no common labels)")
        return

    set_style()
    fig, ax = plt.subplots(figsize=(10, 5))

    x = range(len(labels))
    width = 0.35
    rr_vals = [rr.get(l, 0) * 1000 for l in labels]
    gr_vals = [gr.get(l, 0) * 1000 for l in labels]

    bars1 = ax.bar([i - width/2 for i in x], rr_vals, width, label="Round Robin",
                   color="#6366f1", edgecolor="white", linewidth=1.5)
    bars2 = ax.bar([i + width/2 for i in x], gr_vals, width, label="Greedy",
                   color="#f59e0b", edgecolor="white", linewidth=1.5)

    for bars in [bars1, bars2]:
        for bar in bars:
            h = bar.get_height()
            ax.text(bar.get_x() + bar.get_width() / 2, h + 20,
                    f"{h:.0f}", ha="center", va="bottom", fontsize=9, fontweight="bold")

    # Shorter labels for display
    short = [l.replace("-", "\n") for l in labels]
    ax.set_xticks(list(x))
    ax.set_xticklabels(short, fontsize=10)
    ax.set_ylabel("Total Time (ms)")
    ax.set_title("§20.2 — Scheduler Fairness: Round Robin vs Greedy\n(3 concurrent clients, chunk_records=1000)")
    ax.legend(loc="upper left")
    ax.set_ylim(0, max(max(rr_vals), max(gr_vals)) * 1.25)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "fairness.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")


# ─── Main ────────────────────────────────────────────────────────────────────

def main():
    print("Generating experiment figures...")
    plot_chunk_size()
    plot_fairness()
    plot_local_vs_distributed()
    plot_linear_vs_indexed()
    print(f"\nAll figures saved to {FIG_DIR}")

if __name__ == "__main__":
    main()
