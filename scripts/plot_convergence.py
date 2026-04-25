import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import os

CSV_FILE = "bloom_convergence.csv"
OUT_DIR  = "plots/convergence"
os.makedirs(OUT_DIR, exist_ok=True)

df = pd.read_csv(CSV_FILE)
df["ratio"] = df["ratio"].round(3)

RATIOS      = [0.5, 0.1, 0.01]
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}

C_NINS  = "#4e79a7"   # n_inserted  — blue
C_FP    = "#e15759"   # fp_rate     — red
C_BITS  = "#59a14f"   # bits_set    — green

BAR_W   = 0.25        # width of each individual bar

instances = sorted(df["instance"].unique())
print(f"Generating {len(instances)} convergence plots...")

for puzzle_id in instances:
    pdata = df[df["instance"] == puzzle_id]

    fig, axes = plt.subplots(1, len(RATIOS), figsize=(8 * len(RATIOS), 6), squeeze=False)
    fig.suptitle(f"Puzzle #{puzzle_id} — Bloom Convergence", fontsize=14, fontweight="bold")

    for col, ratio in enumerate(RATIOS):
        ax  = axes[0][col]
        ax2 = ax.twinx()

        rdata = pdata[pdata["ratio"] == ratio].copy()
        mem_label = RATIO_NAMES.get(ratio, f"{ratio*100:.0f}%")

        if rdata.empty:
            ax.set_title(f"Memory {mem_label}", fontsize=11)
            ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            continue

        size_kib = rdata["size_kib"].iloc[0]
        ax.set_title(f"Memory {mem_label}  ({size_kib:,} KiB)", fontsize=11)

        # Sort: depth first, then iteration
        rdata = rdata.sort_values(["total_depth", "iteration"]).reset_index(drop=True)

        # X-axis labels: "depth-iter" (iteration displayed 1-indexed)
        xlabels = [f"{row.total_depth}-{int(row.iteration)+1}" for _, row in rdata.iterrows()]
        x = np.arange(len(xlabels))

        n_ins  = rdata["n_inserted"].values.astype(float)
        fp     = rdata["estimated_fp"].values.astype(float)
        b_set  = rdata["bits_set"].values.astype(float) if "bits_set" in rdata.columns else np.zeros(len(rdata))

        # --- Left axis: n_inserted and bits_set (counts) ---
        ax.bar(x - BAR_W, n_ins, BAR_W, color=C_NINS, alpha=0.85,
               edgecolor="black", linewidth=0.4, label="n_inserted")
        ax.bar(x + BAR_W, b_set, BAR_W, color=C_BITS, alpha=0.85,
               edgecolor="black", linewidth=0.4, label="bits_set")
        ax.set_ylabel("Count (log scale)", fontsize=9)
        ax.set_yscale("symlog", linthresh=1)
        ax.yaxis.set_tick_params(labelsize=8)

        # --- Right axis: fp_rate ---
        ax2.bar(x, fp, BAR_W, color=C_FP, alpha=0.75,
                edgecolor="black", linewidth=0.4, label="fp_rate")
        ax2.set_ylabel("FP rate", fontsize=9, color=C_FP)
        ax2.tick_params(axis="y", colors=C_FP, labelsize=8)
        ax2.set_ylim(-0.05, 1.15)
        ax2.axhline(0.5, color=C_FP, linestyle=":", linewidth=0.9, alpha=0.5)

        # --- X axis ---
        ax.set_xticks(x)
        ax.set_xticklabels(xlabels, rotation=65, ha="right", fontsize=7)
        ax.set_xlabel("depth-iteration", fontsize=9)

        # Vertical separators between depth groups
        depths = rdata["total_depth"].unique()
        pos = 0
        for di, depth in enumerate(sorted(depths)):
            cnt = (rdata["total_depth"] == depth).sum()
            if di > 0:
                ax.axvline(pos - 0.5, color="gray", linestyle="--", linewidth=0.8, alpha=0.45)
            pos += cnt

        ax.grid(True, axis="y", alpha=0.2)

        # Combined legend from both axes
        patches = [
            mpatches.Patch(color=C_NINS, label="n_inserted"),
            mpatches.Patch(color=C_FP,   label="fp_rate (right axis)"),
            mpatches.Patch(color=C_BITS, label="bits_set"),
        ]
        ax.legend(handles=patches, fontsize=7, loc="upper right")

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out_path = os.path.join(OUT_DIR, f"puzzle_{puzzle_id:03d}.png")
    plt.savefig(out_path, dpi=120)
    plt.close()

    if puzzle_id % 10 == 0:
        print(f"  ...done puzzle {puzzle_id}")

print(f"\nAll plots saved to {OUT_DIR}/")
