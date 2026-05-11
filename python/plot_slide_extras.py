#!/usr/bin/env python3
"""
plot_slide_extras.py — Generate slide-ready visualizations:
  1. cache_cold_vs_warm.png  — bar chart, cold 3.90s vs warm 3.11s
  2. topology.png            — 9-node tree overlay, 2-machine split
  3. headline_numbers.png    — 5 KPI tiles for the slide hero
  4. fairness_bars.png       — compact RR vs Greedy comparison
"""

import os
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Rectangle

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIG_DIR = os.path.join(ROOT, "report", "figures")
os.makedirs(FIG_DIR, exist_ok=True)

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.alpha": 0.25,
})


# ── 1. Cache cold vs warm ────────────────────────────────────────────────────
def plot_cache():
    fig, ax = plt.subplots(figsize=(6, 4.2))
    labels = ["Cold\n(scatter-gather)", "Warm\n(LRU cache hit)"]
    times = [3.90, 3.11]
    colors = ["#ef4444", "#10b981"]
    bars = ax.bar(labels, times, color=colors, edgecolor="white", linewidth=2, width=0.55)
    for bar, val in zip(bars, times):
        ax.text(bar.get_x() + bar.get_width() / 2, val + 0.08,
                f"{val:.2f} s", ha="center", va="bottom",
                fontsize=14, fontweight="bold")
    speedup = times[0] / times[1]
    saved = (times[0] - times[1]) / times[0] * 100
    ax.text(0.5, 0.92, f"{saved:.0f}% faster   ({speedup:.2f}× speedup)",
            transform=ax.transAxes, ha="center", va="top",
            fontsize=13, fontweight="bold", color="#065f46",
            bbox=dict(boxstyle="round,pad=0.4", facecolor="#ecfdf5", edgecolor="#10b981"))
    ax.set_ylabel("End-to-End Wall Time (s)", fontsize=11)
    ax.set_title("Request Anticipation — LRU Result Cache\n"
                 "(MANHATTAN, ~4.1M records, full 9-node cluster)",
                 fontsize=11.5)
    ax.set_ylim(0, max(times) * 1.30)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "cache_cold_vs_warm.png")
    plt.savefig(out, dpi=180)
    plt.close()
    print(f"  saved {out}")


# ── 2. Topology diagram ──────────────────────────────────────────────────────
def plot_topology():
    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis("off")

    # m1 box (left)
    m1_box = Rectangle((0.1, 0.3), 4.5, 5.4,
                       linewidth=2, edgecolor="#3b82f6",
                       facecolor="#dbeafe", alpha=0.5)
    ax.add_patch(m1_box)
    ax.text(0.25, 5.45, "Machine 1 (m1)", fontsize=11, fontweight="bold", color="#1e40af")

    # m2 box (right)
    m2_box = Rectangle((5.4, 0.3), 4.5, 5.4,
                       linewidth=2, edgecolor="#10b981",
                       facecolor="#d1fae5", alpha=0.5)
    ax.add_patch(m2_box)
    ax.text(5.55, 5.45, "Machine 2 (m2)", fontsize=11, fontweight="bold", color="#065f46")

    # Node positions
    pos = {
        "A": (1.5, 4.5),  # gateway
        "B": (1.5, 3.0),
        "C": (0.4, 1.4),
        "D": (1.5, 1.4),
        "E": (2.7, 1.4),
        "F": (7.0, 2.7),
        "G": (7.0, 4.5),
        "H": (8.5, 4.5),
        "I": (8.5, 2.7),
    }
    cpp = {"A", "B", "C", "D", "E", "F", "G", "H"}
    gateway = {"A"}

    def draw_node(label, x, y):
        if label in gateway:
            fc, ec = "#fde68a", "#d97706"
            text_color = "#78350f"
        elif label == "I":
            fc, ec = "#fce7f3", "#be185d"
            text_color = "#831843"
        else:
            fc, ec = "#e0f2fe", "#0369a1"
            text_color = "#0c4a6e"
        circle = plt.Circle((x, y), 0.38, facecolor=fc, edgecolor=ec, linewidth=2, zorder=3)
        ax.add_patch(circle)
        ax.text(x, y, label, ha="center", va="center",
                fontsize=15, fontweight="bold", color=text_color, zorder=4)

    for n, (x, y) in pos.items():
        draw_node(n, x, y)

    # Edges (tree as primary, cycles as dashed)
    edges_solid = [("A","B"),("B","C"),("B","D"),("B","E"),("E","F"),
                   ("A","H"),("A","G"),("A","I")]
    edges_dashed = [("E","D"),("E","G")]  # cycle edges
    for a, b in edges_solid:
        x1, y1 = pos[a]; x2, y2 = pos[b]
        ax.plot([x1, x2], [y1, y2], color="#475569", linewidth=2, zorder=1)
    for a, b in edges_dashed:
        x1, y1 = pos[a]; x2, y2 = pos[b]
        ax.plot([x1, x2], [y1, y2], color="#94a3b8", linewidth=1.5,
                linestyle="--", zorder=1, alpha=0.7)

    # Client arrow into A
    ax.annotate("", xy=(1.5, 5.0), xytext=(0.2, 5.0),
                arrowprops=dict(arrowstyle="->", color="#dc2626", lw=2.2))
    ax.text(-0.05, 5.0, "Client", ha="right", va="center",
            fontsize=11, fontweight="bold", color="#dc2626")
    ax.text(0.85, 5.15, "SubmitQuery\nFetchChunk", ha="center", va="bottom",
            fontsize=8, color="#dc2626")

    # Legend (bottom)
    legend_y = 0.05
    ax.plot([0.4, 0.7], [legend_y+0.05, legend_y+0.05], color="#475569", linewidth=2)
    ax.text(0.78, legend_y+0.05, "tree edge", fontsize=9, va="center")
    ax.plot([2.0, 2.3], [legend_y+0.05, legend_y+0.05], color="#94a3b8", linewidth=1.5,
            linestyle="--", alpha=0.7)
    ax.text(2.38, legend_y+0.05, "cycle edge (visited_nodes guard)",
            fontsize=9, va="center")
    ax.add_patch(plt.Circle((5.4, legend_y+0.05), 0.10, facecolor="#fde68a",
                            edgecolor="#d97706", linewidth=1.5))
    ax.text(5.55, legend_y+0.05, "Gateway (A)", fontsize=9, va="center")
    ax.add_patch(plt.Circle((6.8, legend_y+0.05), 0.10, facecolor="#e0f2fe",
                            edgecolor="#0369a1", linewidth=1.5))
    ax.text(6.95, legend_y+0.05, "C++", fontsize=9, va="center")
    ax.add_patch(plt.Circle((7.55, legend_y+0.05), 0.10, facecolor="#fce7f3",
                            edgecolor="#be185d", linewidth=1.5))
    ax.text(7.70, legend_y+0.05, "Python (I)", fontsize=9, va="center")

    ax.set_title("Tree Overlay — 9-Process gRPC Cluster across 2 MacBooks",
                 fontsize=13, fontweight="bold", pad=10)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "topology.png")
    plt.savefig(out, dpi=180, bbox_inches="tight")
    plt.close()
    print(f"  saved {out}")


# ── 3. Headline KPI tiles ────────────────────────────────────────────────────
def plot_headline_numbers():
    fig, axes = plt.subplots(1, 5, figsize=(15, 3.2))
    tiles = [
        ("4.1 M",  "records / 2.7s",        "9-shard scatter-gather", "#3b82f6", "#dbeafe"),
        ("31 %",   "faster small queries", "Round-Robin vs Greedy",   "#8b5cf6", "#ede9fe"),
        ("83 %",   "fewer chunk pushes",   "Adaptive chunking",       "#f59e0b", "#fef3c7"),
        ("3.07×",  "first-chunk speedup",  "Indexed vs Linear scan",  "#10b981", "#d1fae5"),
        ("20 %",   "speedup on repeat",    "LRU result cache",        "#ef4444", "#fee2e2"),
    ]
    for ax, (big, mid, sub, fg, bg) in zip(axes, tiles):
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.axis("off")
        ax.add_patch(FancyBboxPatch((0.02, 0.05), 0.96, 0.90,
                                    boxstyle="round,pad=0.02,rounding_size=0.05",
                                    facecolor=bg, edgecolor=fg, linewidth=2.5))
        ax.text(0.5, 0.66, big, ha="center", va="center",
                fontsize=34, fontweight="bold", color=fg)
        ax.text(0.5, 0.38, mid, ha="center", va="center",
                fontsize=11, fontweight="bold", color="#1f2937")
        ax.text(0.5, 0.20, sub, ha="center", va="center",
                fontsize=9.5, color="#4b5563")
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "headline_numbers.png")
    plt.savefig(out, dpi=180, bbox_inches="tight")
    plt.close()
    print(f"  saved {out}")


# ── 4. Compact fairness comparison ───────────────────────────────────────────
def plot_fairness_compact():
    labels = ["SMALL\n(338K)", "MED\n(766K)", "HUGE\n(3.7M)"]
    rr =     [0.891, 1.641, 3.205]
    greedy = [1.293, 1.224, 3.555]

    x = range(len(labels))
    width = 0.36
    fig, ax = plt.subplots(figsize=(7, 4.2))
    b1 = ax.bar([i - width/2 for i in x], rr, width=width,
                label="Round-Robin", color="#10b981", edgecolor="white", linewidth=1.5)
    b2 = ax.bar([i + width/2 for i in x], greedy, width=width,
                label="Greedy", color="#ef4444", edgecolor="white", linewidth=1.5)
    for bar in list(b1) + list(b2):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.06,
                f"{bar.get_height():.2f}s", ha="center", va="bottom",
                fontsize=9.5, fontweight="bold")
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, fontsize=10)
    ax.set_ylabel("Completion Time (s)", fontsize=11)
    ax.set_title("Scheduler Fairness — 3 concurrent clients\n"
                 "Round-Robin completes SMALL query 31% faster",
                 fontsize=11.5)
    ax.legend(loc="upper left", fontsize=10, frameon=True)
    ax.set_ylim(0, max(greedy) * 1.20)
    plt.tight_layout()
    out = os.path.join(FIG_DIR, "fairness_compact.png")
    plt.savefig(out, dpi=180)
    plt.close()
    print(f"  saved {out}")


if __name__ == "__main__":
    print("Generating slide-ready figures...")
    plot_cache()
    plot_topology()
    plot_headline_numbers()
    plot_fairness_compact()
    print("Done.")
