import os

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import pandas as pd

CONV_CSV = "bloom_convergence.csv"
BENCH_CSV = "benchmark_stp_korf100.csv"
OUT_DIR = "plots"
STP_STATE_BITS = 45

os.makedirs(OUT_DIR, exist_ok=True)


def load_bihs_params(path):
    rows = []
    with open(path) as f:
        for line in f:
            if not line.startswith("BIHS_PARAM"):
                continue
            p = line.strip().split(",")
            rows.append({
                "instance": int(p[1]),
                "ratio": round(float(p[2]), 3),
                "size_kib": int(p[3]),
                "k_hashes": int(p[4]),
                "converged": bool(int(p[8])),
            })
    return pd.DataFrame(rows)


conv = pd.read_csv(CONV_CSV)
conv["ratio"] = conv["ratio"].round(3)
bench = load_bihs_params(BENCH_CSV)

records = []
for (inst, ratio), group in conv.groupby(["instance", "ratio"]):
    group = group.sort_values(["total_depth", "iteration"])
    last_depth = group["total_depth"].max()
    last_depth_rows = group[group["total_depth"] == last_depth]

    iter0 = last_depth_rows[last_depth_rows["iteration"] == 0]
    iter1 = last_depth_rows[last_depth_rows["iteration"] == 1]
    if iter0.empty or iter1.empty:
        continue

    forward_frontier = int(iter0["n_inserted"].iloc[0])
    backward_frontier = int(iter1["n_inserted"].iloc[0])
    if forward_frontier == 0 or backward_frontier == 0:
        continue

    bench_row = bench[(bench["instance"] == inst) & (bench["ratio"] == ratio)]
    if bench_row.empty:
        continue

    m_bits = int(last_depth_rows["size_kib"].iloc[0]) * 1024 * 8
    m_states = m_bits / STP_STATE_BITS
    min_frontier = min(forward_frontier, backward_frontier)
    max_frontier = max(forward_frontier, backward_frontier)

    records.append({
        "instance": inst,
        "ratio": ratio,
        "total_depth": int(last_depth),
        "m_bits": m_bits,
        "m_states": m_states,
        "F_fwd": forward_frontier,
        "F_bwd": backward_frontier,
        "min_frontier": min_frontier,
        "max_frontier": max_frontier,
        "min_over_m": min_frontier / m_states,
        "max_over_m": max_frontier / m_states,
        "prod_over_m2": (forward_frontier * backward_frontier) / (m_states ** 2),
        "converged": bool(bench_row["converged"].iloc[0]),
        "cond_min": min_frontier < m_states,
        "cond_max": max_frontier < m_states,
        "cond_prod": forward_frontier * backward_frontier < m_states ** 2,
    })

df = pd.DataFrame(records)
if df.empty:
    raise SystemExit("No complete frontier records found. Need both iteration 0 and 1 rows per instance/ratio.")

df.to_csv(os.path.join(OUT_DIR, "frontier_memory_conditions.csv"), index=False)
print("Saved plots/frontier_memory_conditions.csv")

colors = {True: "#4e79a7", False: "#e15759"}
labels = {True: "Converged", False: "Timed Out"}
ratio_names = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
ratios = sorted(df["ratio"].unique())

fig, axes = plt.subplots(1, len(ratios), figsize=(7 * len(ratios), 6))
if len(ratios) == 1:
    axes = [axes]

for ax, ratio in zip(axes, ratios):
    sub = df[df["ratio"] == ratio]
    m_states = sub["m_states"].iloc[0]

    for converged, group in sub.groupby("converged"):
        ax.scatter(
            group["F_fwd"],
            group["F_bwd"],
            color=colors[converged],
            label=labels[converged],
            edgecolors="black",
            linewidths=0.6,
            s=60,
            zorder=3,
        )

    ax.axvline(m_states, color="black", linestyle="--", linewidth=1, alpha=0.6, label="F = memory slots")
    ax.axhline(m_states, color="black", linestyle="--", linewidth=1, alpha=0.6)

    xlim = ax.get_xlim()
    ylim = ax.get_ylim()
    ax.set_xlim(xlim)
    ax.set_ylim(ylim)
    ax.axvspan(m_states, xlim[1], color="gray", alpha=0.08, zorder=0)
    ax.axhspan(m_states, ylim[1], color="gray", alpha=0.08, zorder=0)

    ax.set_title(f"Memory {ratio_names.get(ratio, ratio)} (m={m_states:,.0f} states)")
    ax.set_xlabel("n_inserted iteration 0 (forward)")
    ax.set_ylabel("n_inserted iteration 1 (backward)")
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{int(x):,}"))
    ax.grid(True, alpha=0.2)
    ax.legend(fontsize=8)

fig.suptitle("First forward vs backward frontier sizes", fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "frontier_scatter.png"), dpi=150)
plt.close()
print("Saved plots/frontier_scatter.png")

fig, axes = plt.subplots(1, 3, figsize=(15, 5))
conditions = [
    ("cond_min", "min(F_fwd, F_bwd) < m"),
    ("cond_max", "max(F_fwd, F_bwd) < m"),
    ("cond_prod", "F_fwd * F_bwd < m^2"),
]

for ax, (cond_col, title) in zip(axes, conditions):
    categories = [
        "True\nConverged",
        "True\nTimed Out",
        "False\nConverged",
        "False\nTimed Out",
    ]
    counts = [
        int((df[cond_col] & df["converged"]).sum()),
        int((df[cond_col] & ~df["converged"]).sum()),
        int((~df[cond_col] & df["converged"]).sum()),
        int((~df[cond_col] & ~df["converged"]).sum()),
    ]
    bar_colors = [colors[True], colors[False], "#aec7e8", "#f7b6b6"]
    bars = ax.bar(categories, counts, color=bar_colors, edgecolor="black", linewidth=0.6)
    for bar, count in zip(bars, counts):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height() + max(counts) * 0.02,
            str(count),
            ha="center",
            va="bottom",
            fontsize=10,
            fontweight="bold",
        )
    ax.set_title(title, fontsize=11)
    ax.set_ylabel("Cases")
    ax.set_ylim(0, max(counts) * 1.2 if max(counts) else 1)
    ax.grid(True, axis="y", alpha=0.2)

fig.suptitle("Frontier/memory conditions vs actual outcome", fontsize=13, fontweight="bold")
plt.tight_layout()
plt.savefig(os.path.join(OUT_DIR, "frontier_conditions.png"), dpi=150)
plt.close()
print("Saved plots/frontier_conditions.png")

print("\n=== Frontier / memory condition accuracy ===")
for cond_col, label in conditions:
    tp = int((df[cond_col] & df["converged"]).sum())
    fp = int((df[cond_col] & ~df["converged"]).sum())
    fn = int((~df[cond_col] & df["converged"]).sum())
    tn = int((~df[cond_col] & ~df["converged"]).sum())
    acc = (tp + tn) / len(df)
    print(
        f"{label}: accuracy={acc:.3f}, "
        f"true+converged={tp}, true+timed_out={fp}, "
        f"false+converged={fn}, false+timed_out={tn}"
    )

print("\n=== Correlation with convergence ===")
corr_df = df.copy()
corr_df["converged_int"] = corr_df["converged"].astype(int)
for col in ["min_over_m", "max_over_m", "prod_over_m2", "min_frontier", "max_frontier", "m_states"]:
    pearson = corr_df[col].corr(corr_df["converged_int"], method="pearson")
    spearman = corr_df[col].corr(corr_df["converged_int"], method="spearman")
    print(f"{col}: pearson={pearson:.3f}, spearman={spearman:.3f}")

print("\n=== Frontier ratios to memory slots ===")
print(
    df.groupby("converged")[["min_over_m", "max_over_m", "prod_over_m2"]]
      .agg(["min", "median", "max"])
      .round(3)
      .to_string()
)
