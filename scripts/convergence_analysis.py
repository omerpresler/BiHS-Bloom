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
# Load convergence CSV — keep only iteration 0 (the full unfiltered frontier)
# ---------------------------------------------------------------------------
conv = pd.read_csv(CONV_CSV)
conv["ratio"] = conv["ratio"].round(3)
iter0 = conv[conv["iteration"] == 0].copy()

HAS_BITS = "bits_set" in iter0.columns
m_bits   = iter0["size_kib"] * 1024 * 8

iter0["true_fp"] = (iter0["bits_set"] / m_bits) if HAS_BITS else np.nan
iter0["fill"]    = iter0["true_fp"]   # same thing for k=1

# ---------------------------------------------------------------------------
# Load benchmark CSV — parse BIHS_PARAM rows for converged flag
# ---------------------------------------------------------------------------
rows = []
with open(BENCH_CSV) as f:
    for line in f:
        if line.startswith("BIHS_PARAM"):
            p = line.strip().split(",")
            rows.append({
                "instance":  int(p[1]),
                "ratio":     round(float(p[2]), 3),
                "size_kib":  int(p[3]),
                "k_hashes":  int(p[4]),
                "converged": bool(int(p[8])),
            })
bench = pd.DataFrame(rows)

# ---------------------------------------------------------------------------
# Join
# ---------------------------------------------------------------------------
# For each (instance, ratio), use the last depth's iteration-0 row
# (that's where the real challenge is — the hardest depth attempted)
last_depth = (
    iter0.groupby(["instance", "ratio"])["total_depth"].max().reset_index()
)
iter0_last = iter0.merge(last_depth, on=["instance", "ratio", "total_depth"])

df = iter0_last.merge(bench[["instance", "ratio", "k_hashes", "converged"]],
                      on=["instance", "ratio"])

df["true_fp"] = (df["bits_set"] / (df["size_kib"] * 1024 * 8)) ** df["k_hashes"]

COLORS = {True: "#4e79a7", False: "#e15759"}
LABELS = {True: "Converged", False: "Timed Out"}
RATIOS = sorted(df["ratio"].unique())
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
MARKERS = {0.5: "o", 0.1: "s", 0.01: "^"}

# ---------------------------------------------------------------------------
# Plot 1: estimated_fp vs true_fp at last depth, iter 0 — coloured by outcome
# ---------------------------------------------------------------------------
fig, ax = plt.subplots(figsize=(8, 6))

for conv_val, group in df.groupby("converged"):
    ax.scatter(group["estimated_fp"], group["true_fp"],
               color=COLORS[conv_val], label=LABELS[conv_val],
               alpha=0.75, edgecolors="black", linewidths=0.5, s=50)

ax.set_xlabel("estimated_fp (formula using n_inserted — counts duplicates)")
ax.set_ylabel("true_fp  (bits_set / m)^k  — unique states only")
ax.set_title("Theoretical vs Real FP Rate at Last Depth, Iter 0")
ax.legend()
ax.grid(True, alpha=0.2)
# diagonal reference line
lim = max(ax.get_xlim()[1], ax.get_ylim()[1])
ax.plot([0, lim], [0, lim], "k--", linewidth=0.8, alpha=0.4, label="y=x")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "fp_estimated_vs_true.png"), dpi=150)
plt.close()
print("Saved plots/fp_estimated_vs_true.png")

# ---------------------------------------------------------------------------
# Plot 2: true_fp at iter 0 — box plot by ratio, split converged / timed-out
# ---------------------------------------------------------------------------
fig, axes = plt.subplots(1, len(RATIOS), figsize=(5 * len(RATIOS), 5), sharey=True)

for ax, ratio in zip(axes, RATIOS):
    sub = df[df["ratio"] == ratio]
    data_conv    = sub[sub["converged"]  == True ]["true_fp"].dropna().values
    data_timeout = sub[sub["converged"]  == False]["true_fp"].dropna().values

    bp = ax.boxplot([data_conv, data_timeout],
                    patch_artist=True,
                    labels=["Converged", "Timed Out"],
                    widths=0.5)
    bp["boxes"][0].set_facecolor(COLORS[True])
    bp["boxes"][1].set_facecolor(COLORS[False])
    for box in bp["boxes"]:
        box.set_alpha(0.75)

    ax.set_title(f"Memory {RATIO_NAMES.get(ratio, ratio)}")
    ax.set_ylabel("true_fp  (bits_set / m)^k" if ax == axes[0] else "")
    ax.grid(True, axis="y", alpha=0.2)

fig.suptitle("True FP Rate at Last Depth, Iter 0 — by Ratio & Outcome",
             fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "true_fp_boxplot.png"), dpi=150)
plt.close()
print("Saved plots/true_fp_boxplot.png")

# ---------------------------------------------------------------------------
# Plot 3: fill % vs n_inserted — shows duplicate inflation per outcome
# ---------------------------------------------------------------------------
df["fill_pct"] = df["bits_set"] / (df["size_kib"] * 1024 * 8) * 100

fig, axes = plt.subplots(1, len(RATIOS), figsize=(6 * len(RATIOS), 5))

for ax, ratio in zip(axes, RATIOS):
    sub = df[df["ratio"] == ratio]
    for conv_val, group in sub.groupby("converged"):
        ax.scatter(group["n_inserted"], group["fill_pct"],
                   color=COLORS[conv_val], label=LABELS[conv_val],
                   alpha=0.75, edgecolors="black", linewidths=0.5, s=50)

    ax.set_title(f"Memory {RATIO_NAMES.get(ratio, ratio)}")
    ax.set_xlabel("n_inserted (DFS hits, with duplicates)")
    ax.set_ylabel("bits set / m  (%)" if ax == axes[0] else "")
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:.1f}%"))
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
    ax.grid(True, alpha=0.2)
    if ax == axes[0]:
        ax.legend(fontsize=8)

fig.suptitle("Filter Fill % vs DFS Hits (n_inserted) at Last Depth, Iter 0",
             fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "bits_set_vs_n_inserted.png"), dpi=150)
plt.close()
print("Saved plots/bits_set_vs_n_inserted.png")

# ---------------------------------------------------------------------------
# Summary table — fill % ranges per ratio / outcome
# ---------------------------------------------------------------------------
summary_fill = (
    df.groupby("converged")["fill_pct"]
    .agg(min="min", median="median", max="max")
    .round(2)
)
summary_fill.columns = ["min %", "median %", "max %"]
print("\n=== Fill % (bits_set / m) at last depth, iter 0 ===")
print(summary_fill.to_string())

summary_fp = (
    df.groupby("converged")["estimated_fp"]
    .agg(min="min", median="median", max="max")
    .round(4)
)
summary_fp.columns = ["min", "median", "max"]
print("\n=== Estimated FP at last depth, iter 0 ===")
print(summary_fp.to_string())
