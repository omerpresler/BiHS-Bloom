import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import os

CONV_CSV  = "bloom_convergence.csv"
BENCH_CSV = "benchmark_stp_korf100.csv"
OUT_DIR   = "plots"
os.makedirs(OUT_DIR, exist_ok=True)

# ---------------------------------------------------------------------------
# Load & compute F_fwd, F_bwd per (instance, ratio)
# ---------------------------------------------------------------------------
conv = pd.read_csv(CONV_CSV)
conv["ratio"] = conv["ratio"].round(3)

rows = []
with open(BENCH_CSV) as f:
    for line in f:
        if line.startswith("BIHS_PARAM"):
            p = line.strip().split(",")
            rows.append({"instance": int(p[1]), "ratio": round(float(p[2]), 3),
                         "converged": bool(int(p[8]))})
bench = pd.DataFrame(rows)

records = []
for (inst, ratio), group in conv.groupby(["instance", "ratio"]):
    group  = group.sort_values(["total_depth", "iteration"])
    last_d = group["total_depth"].max()
    ld     = group[group["total_depth"] == last_d]
    m      = ld["size_kib"].iloc[0] * 1024 * 8

    i0 = ld[ld["iteration"] == 0]
    i1 = ld[ld["iteration"] == 1]
    if i0.empty or i1.empty or i0["n_inserted"].iloc[0] == 0:
        continue

    F_fwd = i0["n_inserted"].iloc[0]
    F_bwd = i1["n_inserted"].iloc[0]

    conv_flag = bench[(bench["instance"] == inst) & (bench["ratio"] == ratio)]
    if conv_flag.empty:
        continue

    records.append({
        "instance":  inst,
        "ratio":     ratio,
        "m":         m,
        "F_fwd":     F_fwd,
        "F_bwd":     F_bwd,
        "converged": conv_flag["converged"].iloc[0],
        "cond_max":  max(F_fwd, F_bwd) < m,
        "cond_prod": F_fwd * F_bwd < m ** 2,
    })

df = pd.DataFrame(records)

COLORS = {True: "#4e79a7", False: "#e15759"}
LABELS = {True: "Converged", False: "Timed Out"}

# ---------------------------------------------------------------------------
# Plot 1: F_fwd vs F_bwd scatter — shade regions, mark m boundary
# ---------------------------------------------------------------------------
ratios = sorted(df["ratio"].unique())
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}

fig, axes = plt.subplots(1, len(ratios), figsize=(7 * len(ratios), 6))

for ax, ratio in zip(axes, ratios):
    sub = df[df["ratio"] == ratio]
    m   = sub["m"].iloc[0]

    for conv_val, group in sub.groupby("converged"):
        ax.scatter(group["F_fwd"], group["F_bwd"],
                   color=COLORS[conv_val], label=LABELS[conv_val],
                   edgecolors="black", linewidths=0.6, s=60, zorder=3)

    # m boundary lines
    ax.axvline(m, color="black", linestyle="--", linewidth=1, alpha=0.6, label="F = m")
    ax.axhline(m, color="black", linestyle="--", linewidth=1, alpha=0.6)

    # shade the max(F_fwd, F_bwd) >= m region
    xlim = ax.get_xlim()
    ylim = ax.get_ylim()
    ax.set_xlim(xlim); ax.set_ylim(ylim)
    ax.axvspan(m, xlim[1], color="gray", alpha=0.08, zorder=0)
    ax.axhspan(m, ylim[1], color="gray", alpha=0.08, zorder=0)

    ax.set_title(f"Memory {RATIO_NAMES.get(ratio, ratio)}  (m={m:,} bits)")
    ax.set_xlabel("n_inserted  iter 0  (forward)")
    ax.set_ylabel("n_inserted  iter 1  (backward)")
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
    ax.grid(True, alpha=0.2)
    ax.legend(fontsize=8)

fig.suptitle("F_fwd vs F_bwd — dashed lines mark filter capacity m",
             fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "frontier_scatter.png"), dpi=150)
plt.close()
print("Saved plots/frontier_scatter.png")

# ---------------------------------------------------------------------------
# Plot 2: bar chart — how many cases each condition correctly predicts
# ---------------------------------------------------------------------------
fig, axes = plt.subplots(1, 2, figsize=(10, 5))

for ax, (cond_col, title) in zip(axes, [
    ("cond_prod", "F_fwd × F_bwd < m²"),
    ("cond_max",  "max(F_fwd, F_bwd) < m"),
]):
    categories = ["Condition True\n+ Converged", "Condition True\n+ Timed Out",
                  "Condition False\n+ Converged", "Condition False\n+ Timed Out"]
    counts = [
        ((df[cond_col]) &  df["converged"]).sum(),
        ((df[cond_col]) & ~df["converged"]).sum(),
        (~df[cond_col] &   df["converged"]).sum(),
        (~df[cond_col] &  ~df["converged"]).sum(),
    ]
    bar_colors = [COLORS[True], COLORS[False], "#aec7e8", "#f7b6b6"]

    bars = ax.bar(categories, counts, color=bar_colors, edgecolor="black", linewidth=0.6)
    for bar, count in zip(bars, counts):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.3,
                str(count), ha="center", va="bottom", fontsize=10, fontweight="bold")

    ax.set_title(title, fontsize=11)
    ax.set_ylabel("Number of cases")
    ax.set_ylim(0, max(counts) * 1.2)
    ax.grid(True, axis="y", alpha=0.2)
    plt.setp(ax.get_xticklabels(), fontsize=8)

fig.suptitle("Condition vs Actual Outcome", fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "frontier_conditions.png"), dpi=150)
plt.close()
print("Saved plots/frontier_conditions.png")
