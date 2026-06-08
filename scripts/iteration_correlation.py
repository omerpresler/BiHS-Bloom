import os

import matplotlib.pyplot as plt
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
                "time": float(p[5]),
                "nodes": int(p[6]),
                "converged": bool(int(p[8])),
                "k_mode": p[9] if len(p) > 9 else "",
                "split_mode": p[10] if len(p) > 10 else "",
            })
    return pd.DataFrame(rows)


conv = pd.read_csv(CONV_CSV)
conv["ratio"] = conv["ratio"].round(3)
bench = load_bihs_params(BENCH_CSV)

records = []
RUN_KEYS = ["instance", "ratio", "k_mode", "split_mode"]

for key, group in conv.groupby(RUN_KEYS):
    inst, ratio, k_mode, split_mode = key
    group = group.sort_values(["total_depth", "iteration"])
    last_depth = group["total_depth"].max()
    last_depth_rows = group[group["total_depth"] == last_depth]

    iter0 = last_depth_rows[last_depth_rows["iteration"] == 0]
    iter1 = last_depth_rows[last_depth_rows["iteration"] == 1]
    if iter0.empty or iter1.empty:
        continue

    Ff = int(iter0["n_inserted"].iloc[0])
    Fb = int(iter1["n_inserted"].iloc[0])
    if Ff == 0 or Fb == 0:
        continue

    bench_row = bench[
        (bench["instance"] == inst) &
        (bench["ratio"] == ratio) &
        (bench["k_mode"] == k_mode) &
        (bench["split_mode"] == split_mode)
    ]
    if bench_row.empty:
        continue

    m_bits = int(last_depth_rows["size_kib"].iloc[0]) * 1024 * 8
    m_states = m_bits / STP_STATE_BITS
    min_frontier = min(Ff, Fb)
    max_frontier = max(Ff, Fb)

    depth_attempts = group["total_depth"].nunique()
    final_depth_iterations = int(last_depth_rows["iteration"].max()) + 1
    total_iterations = len(group)

    records.append({
        "instance": inst,
        "ratio": ratio,
        "k_mode": k_mode,
        "split_mode": split_mode,
        "converged": bool(bench_row["converged"].iloc[0]),
        "time": float(bench_row["time"].iloc[0]),
        "nodes": int(bench_row["nodes"].iloc[0]),
        "total_depth": int(last_depth),
        "depth_attempts": depth_attempts,
        "final_depth_iterations": final_depth_iterations,
        "total_iterations": total_iterations,
        "m_states": m_states,
        "Ff": Ff,
        "Fb": Fb,
        "min_frontier": min_frontier,
        "max_frontier": max_frontier,
        "min_over_m": min_frontier / m_states,
        "max_over_m": max_frontier / m_states,
        "geo_over_m": ((Ff * Fb) ** 0.5) / m_states,
        "prod_over_m2": (Ff * Fb) / (m_states ** 2),
        "imbalance": max_frontier / min_frontier,
        "iter0_fill_ratio": float(iter0["fill_ratio"].iloc[0]),
        "iter1_fill_ratio": float(iter1["fill_ratio"].iloc[0]),
    })

df = pd.DataFrame(records)
if df.empty:
    raise SystemExit("No complete records found.")

out_csv = os.path.join(OUT_DIR, "iteration_correlation.csv")
df.to_csv(out_csv, index=False)
print(f"Saved {out_csv}")

features = [
    "m_states",
    "Ff",
    "Fb",
    "min_frontier",
    "max_frontier",
    "min_over_m",
    "max_over_m",
    "geo_over_m",
    "prod_over_m2",
    "imbalance",
    "iter0_fill_ratio",
    "iter1_fill_ratio",
]
targets = ["final_depth_iterations", "total_iterations", "depth_attempts"]

print("\n=== Spearman correlation with iteration counts ===")
for target in targets:
    print(f"\n{target}")
    corrs = []
    for feature in features:
        corrs.append((feature, df[feature].corr(df[target], method="spearman")))
    for feature, corr in sorted(corrs, key=lambda item: abs(item[1]), reverse=True):
        print(f"  {feature}: {corr:.3f}")

print("\n=== Median iteration counts by min_over_m band ===")
bins = [0, 1, 10, 20, 36, 60, 100, float("inf")]
labels = ["<1", "1..10", "10..20", "20..36", "36..60", "60..100", ">=100"]
df["min_load_band"] = pd.cut(df["min_over_m"], bins=bins, labels=labels, right=False)
summary = (
    df.groupby("min_load_band", observed=False)
      .agg(
          cases=("instance", "count"),
          converged=("converged", "sum"),
          median_final_iters=("final_depth_iterations", "median"),
          max_final_iters=("final_depth_iterations", "max"),
          median_total_iters=("total_iterations", "median"),
          max_total_iters=("total_iterations", "max"),
      )
)
summary["timeout"] = summary["cases"] - summary["converged"]
print(summary.to_string())

colors = {True: "#4e79a7", False: "#e15759"}
for x_col, out_name, xlabel in [
    ("min_over_m", "iterations_vs_min_load.png", "min(Ff, Fb) / m"),
    ("geo_over_m", "iterations_vs_geo_load.png", "sqrt(Ff * Fb) / m"),
]:
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    for converged, group in df.groupby("converged"):
        axes[0].scatter(group[x_col], group["final_depth_iterations"], color=colors[converged],
                        edgecolors="black", linewidths=0.4, alpha=0.75, label=str(converged))
        axes[1].scatter(group[x_col], group["total_iterations"], color=colors[converged],
                        edgecolors="black", linewidths=0.4, alpha=0.75, label=str(converged))
    for ax in axes:
        ax.set_xscale("log")
        ax.grid(True, alpha=0.25)
        ax.set_xlabel(xlabel)
        ax.legend(title="converged", fontsize=8)
    axes[0].set_ylabel("iterations at final attempted depth")
    axes[1].set_ylabel("total Bloom iterations")
    fig.suptitle(f"Iteration count vs {xlabel}")
    plt.tight_layout()
    path = os.path.join(OUT_DIR, out_name)
    plt.savefig(path, dpi=150)
    plt.close()
    print(f"Saved {path}")
