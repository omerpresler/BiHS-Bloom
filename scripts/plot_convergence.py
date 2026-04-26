import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import os

CSV_FILE  = "bloom_convergence.csv"
BENCH_CSV = "benchmark_stp_korf100.csv"
OUT_DIR   = "plots/convergence"
os.makedirs(OUT_DIR, exist_ok=True)

df = pd.read_csv(CSV_FILE)
df["ratio"] = df["ratio"].round(3)

# Load converged flag per (instance, ratio) from the benchmark CSV
converged_map = {}
if os.path.exists(BENCH_CSV):
    with open(BENCH_CSV) as f:
        for line in f:
            if line.startswith("BIHS_PARAM"):
                parts = line.strip().split(",")
                key = (int(parts[1]), round(float(parts[2]), 3))
                converged_map[key] = bool(int(parts[8]))

RATIOS      = [0.5, 0.1, 0.01]
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
RATIO_SLUGS = {0.5: "50pct", 0.1: "10pct", 0.01: "1pct"}

HAS_BITS = "bits_set" in df.columns

ITER_CMAP = plt.get_cmap("tab10")

COLORS = {
    "n_inserted": "#4e79a7",
    "fp_rate":    "#e15759",
    "bits_set":   "#59a14f",
}

instances = sorted(df["instance"].unique())
print(f"Generating {len(instances)} convergence plots...")

for puzzle_id in instances:
    pdata = df[df["instance"] == puzzle_id]

    # -----------------------------------------------------------------------
    # Main figure: n_inserted + bits_set (left) and fp_rate (right) per ratio
    # -----------------------------------------------------------------------
    BAR_W = 0.25
    fig, axes = plt.subplots(1, len(RATIOS), figsize=(8 * len(RATIOS), 6), squeeze=False)
    fig.suptitle(f"Puzzle #{puzzle_id} — Bloom Convergence", fontsize=14, fontweight="bold")

    for col, ratio in enumerate(RATIOS):
        ax  = axes[0][col]
        ax2 = ax.twinx()

        rdata = pdata[pdata["ratio"] == ratio].copy()
        mem_label = RATIO_NAMES[ratio]

        if rdata.empty:
            ax.set_title(f"Memory {mem_label}")
            ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            continue

        size_kib = rdata["size_kib"].iloc[0]
        ax.set_title(f"Memory {mem_label}  ({size_kib:,} KiB)")

        rdata = rdata.sort_values(["total_depth", "iteration"]).reset_index(drop=True)
        xlabels = [f"{row.total_depth}-{int(row.iteration)+1}" for _, row in rdata.iterrows()]
        x = np.arange(len(xlabels))

        m_bits   = rdata["size_kib"].iloc[0] * 1024 * 8
        n_ins    = rdata["n_inserted"].values.astype(float)
        fp       = rdata["estimated_fp"].values.astype(float)
        fill_pct = (rdata["bits_set"].values.astype(float) / m_bits * 100) if HAS_BITS else np.zeros(len(rdata))

        ax.bar(x, n_ins, BAR_W, color=COLORS["n_inserted"],
               edgecolor="black", linewidth=0.6, label="n_inserted")
        ax.set_ylabel("n_inserted (log scale)")
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{int(v):,}"))

        ax2.bar(x - BAR_W, fp * 100, BAR_W, color=COLORS["fp_rate"],
                edgecolor="black", linewidth=0.6, label="fp rate %")
        ax2.bar(x + BAR_W, fill_pct, BAR_W, color=COLORS["bits_set"],
                edgecolor="black", linewidth=0.6, label="fill %")
        ax2.set_ylabel("%", color="black")
        ax2.set_ylim(-2, 115)
        ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:.0f}%"))
        ax2.axhline(50, color=COLORS["fp_rate"], linestyle=":", linewidth=0.9, alpha=0.5)

        ax.set_xticks(x)
        ax.set_xticklabels(xlabels, rotation=65, ha="right", fontsize=7)
        ax.set_xlabel("depth-iteration")

        depths = rdata["total_depth"].unique()
        pos = 0
        for di, depth in enumerate(sorted(depths)):
            cnt = (rdata["total_depth"] == depth).sum()
            if di > 0:
                ax.axvline(pos - 0.5, color="gray", linestyle="--", linewidth=0.8, alpha=0.45)
            pos += cnt

        ax.grid(True, axis="y", alpha=0.2)
        ax.legend(fontsize=7, loc="upper right")

    plt.tight_layout()
    plt.savefig(os.path.join(OUT_DIR, f"puzzle_{puzzle_id:03d}.png"), dpi=150)
    plt.close()

    # -----------------------------------------------------------------------
    # Focus figures: one per ratio, 3 subplots (fp / bits_set / n_inserted)
    # x-axis = total_depth, one colored line per iteration
    # -----------------------------------------------------------------------
    METRICS = [
        ("estimated_fp", "FP rate",      False),
        ("bits_set",     "Bits set",     True),
        ("n_inserted",   "n inserted",   True),
    ]

    for ratio in RATIOS:
        rdata = pdata[pdata["ratio"] == ratio].copy()
        mem_label = RATIO_NAMES[ratio]
        slug      = RATIO_SLUGS[ratio]

        conv = converged_map.get((puzzle_id, ratio))
        conv_tag = ""
        if conv is True:
            conv_tag = "  ✓ Converged"
        elif conv is False:
            conv_tag = "  ✗ Timed Out"

        fig, axes = plt.subplots(1, 3, figsize=(18, 5), squeeze=False)
        fig.suptitle(
            f"Puzzle #{puzzle_id} — Memory {mem_label}  ({rdata['size_kib'].iloc[0]:,} KiB){conv_tag}" if not rdata.empty
            else f"Puzzle #{puzzle_id} — Memory {mem_label}{conv_tag}",
            fontsize=14, fontweight="bold",
        )

        if rdata.empty:
            for ax in axes[0]:
                ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            plt.tight_layout()
            plt.savefig(os.path.join(OUT_DIR, f"puzzle_{puzzle_id:03d}_{slug}.png"), dpi=150)
            plt.close()
            continue

        rdata = rdata.sort_values(["total_depth", "iteration"]).reset_index(drop=True)
        if HAS_BITS:
            m_bits = rdata["size_kib"].iloc[0] * 1024 * 8
            rdata["fill_pct"] = rdata["bits_set"] / m_bits * 100
        iterations = sorted(rdata["iteration"].unique())
        depths     = sorted(rdata["total_depth"].unique())

        METRICS = [
            ("estimated_fp", "FP rate",  False),
            ("fill_pct",     "Fill %",   False),
            ("n_inserted",   "n inserted", True),
        ]

        for col, (metric, ylabel, use_log) in enumerate(METRICS):
            ax = axes[0][col]

            if metric == "fill_pct" and not HAS_BITS:
                ax.set_title(ylabel)
                ax.text(0.5, 0.5, "no data\n(bits_set not in CSV)",
                        ha="center", va="center", transform=ax.transAxes, fontsize=9)
                ax.set_xlabel("iteration")
                continue

            for di, depth in enumerate(depths):
                ddata = rdata[rdata["total_depth"] == depth]
                color = ITER_CMAP(di % 10)
                y = ddata.set_index("iteration")[metric].reindex(iterations)
                ax.plot(iterations, y.values, marker="o", markersize=4,
                        linewidth=1.6, color=color, label=f"depth {depth}")

            ax.set_title(ylabel)
            ax.set_xlabel("iteration")
            ax.set_ylabel(ylabel)
            if use_log:
                ax.set_yscale("log")
                ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
            elif metric == "fill_pct":
                ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:.1f}%"))
            elif metric == "estimated_fp":
                ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x*100:.1f}%"))
            ax.grid(True, alpha=0.2)
            ax.legend(fontsize=7, loc="upper left", bbox_to_anchor=(1.01, 1), borderaxespad=0)

        plt.tight_layout()
        plt.savefig(os.path.join(OUT_DIR, f"puzzle_{puzzle_id:03d}_{slug}.png"), dpi=150)
        plt.close()

    if puzzle_id % 10 == 0:
        print(f"  ...done puzzle {puzzle_id}")

print(f"\nAll plots saved to {OUT_DIR}/")
