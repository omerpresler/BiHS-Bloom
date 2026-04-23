import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np
import os

CSV_FILE = "bloom_convergence.csv"
OUT_DIR  = "plots/convergence"
os.makedirs(OUT_DIR, exist_ok=True)

df = pd.read_csv(CSV_FILE)
df["ratio"] = df["ratio"].round(3)   # fix float precision (0.01000000001 → 0.01)

RATIOS      = [0.5, 0.1, 0.01]
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
RATIO_COLS  = {0.5: "#b07aa1", 0.1: "#ff9da7", 0.01: "#9c755f"}

instances = sorted(df["instance"].unique())
print(f"Generating {len(instances)} convergence plots...")

for puzzle_id in instances:
    pdata = df[df["instance"] == puzzle_id]

    fig, axes = plt.subplots(
        2, len(RATIOS),
        figsize=(6 * len(RATIOS), 8),
        squeeze=False
    )
    fig.suptitle(f"Puzzle #{puzzle_id} — Bloom Convergence", fontsize=14, fontweight="bold")

    for col, ratio in enumerate(RATIOS):
        rdata = pdata[pdata["ratio"] == ratio].copy()
        label = RATIO_NAMES.get(ratio, f"{ratio*100:.0f}%")
        color = RATIO_COLS.get(ratio, "steelblue")

        ax_top = axes[0][col]   # n_inserted
        ax_bot = axes[1][col]   # estimated_fp

        if rdata.empty:
            ax_top.set_title(f"Memory {label}", fontsize=11)
            ax_top.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax_top.transAxes)
            ax_bot.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax_bot.transAxes)
            continue

        size_kib = rdata["size_kib"].iloc[0]
        ax_top.set_title(f"Memory {label}  ({size_kib:,} KiB)", fontsize=11)

        depths = sorted(rdata["total_depth"].unique())
        depth_colors = cm.viridis(np.linspace(0.2, 0.9, len(depths)))

        for depth, dcolor in zip(depths, depth_colors):
            ddata = rdata[rdata["total_depth"] == depth].sort_values("iteration")
            if ddata.empty:
                continue

            xs    = ddata["iteration"].values
            n_ins = ddata["n_inserted"].values
            fp    = ddata["estimated_fp"].values

            # converged = last iter is odd (min_items check only runs after backward passes)
            # if last iter is even, it timed out or moved to next depth mid-cycle
            last_iter = ddata["iteration"].iloc[-1]
            converged = (last_iter % 2 == 1)
            line_style = "-" if converged else "--"
            depth_label = f"d={depth}" + ("" if converged else " (no conv.)")

            ax_top.plot(xs, n_ins, marker="o", ms=3, linestyle=line_style,
                        color=dcolor, label=depth_label)
            ax_bot.plot(xs, fp,    marker="o", ms=3, linestyle=line_style,
                        color=dcolor, label=depth_label)

            # mark the last point
            ax_top.scatter(xs[-1], n_ins[-1], color=dcolor, s=60,
                           marker="*" if converged else "x", zorder=5)
            ax_bot.scatter(xs[-1], fp[-1],    color=dcolor, s=60,
                           marker="*" if converged else "x", zorder=5)

        # formatting top axis
        ax_top.set_xlabel("Iteration")
        ax_top.set_ylabel("Items inserted into Bloom")
        ax_top.set_yscale("log")
        ax_top.legend(fontsize=7, loc="upper right")
        ax_top.grid(True, alpha=0.3)

        # formatting bottom axis
        ax_bot.set_xlabel("Iteration")
        ax_bot.set_ylabel("Estimated FP rate")
        ax_bot.set_ylim(-0.05, 1.05)
        ax_bot.axhline(0.5, color="red", linestyle=":", linewidth=1, label="FP=0.5")
        ax_bot.legend(fontsize=7, loc="upper right")
        ax_bot.grid(True, alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    out_path = os.path.join(OUT_DIR, f"puzzle_{puzzle_id:03d}.png")
    plt.savefig(out_path, dpi=120)
    plt.close()

    if puzzle_id % 10 == 0:
        print(f"  ...done puzzle {puzzle_id}")

print(f"\nAll plots saved to {OUT_DIR}/")
