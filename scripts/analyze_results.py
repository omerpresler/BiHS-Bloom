import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import sys
import os

CSV_FILE = "benchmark_stp_korf100.csv"

# ---------------------------------------------------------------------------
# Load & split
# ---------------------------------------------------------------------------

def load(path):
    rows_main, rows_param = [], []
    with open(path) as f:
        header = f.readline().strip().split(",")
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("BIHS_PARAM"):
                parts = line.split(",")
                rows_param.append({
                    "instance": int(parts[1]),
                    "ratio":    float(parts[2]),
                    "size_kib": int(parts[3]),
                    "k_hashes": int(parts[4]),
                    "time":     float(parts[5]),
                    "nodes":    int(parts[6]),
                })
            else:
                vals = line.split(",")
                rows_main.append(dict(zip(header, vals)))

    df = pd.DataFrame(rows_main).apply(pd.to_numeric, errors="coerce")
    params = pd.DataFrame(rows_param)
    return df, params

df, params = load(CSV_FILE)

ALGOS = {
    "A*":        ("a_star_time",    "a_star_nodes"),
    "Rev-A*":    ("rev_a_star_time","rev_a_star_nodes"),
    "BAE*":      ("bae_time",       "bae_nodes"),
    "MM":        ("mm_time",        "mm_nodes"),
    "IDA*":      ("ida_time",       "ida_nodes"),
    "Rev-IDA*":  ("rev_ida_time",   "rev_ida_nodes"),
    "BiHS-50%":  ("bihs_bloom_time_50pct", "bihs_bloom_nodes_50pct"),
    "BiHS-10%":  ("bihs_bloom_time_10pct", "bihs_bloom_nodes_10pct"),
    "BiHS-1%":   ("bihs_bloom_time_1pct",  "bihs_bloom_nodes_1pct"),
}

TIMEOUT = -1.0   # value written when timed out

# ---------------------------------------------------------------------------
# Summary table
# ---------------------------------------------------------------------------

def summary_table(df):
    rows = []
    for name, (t_col, n_col) in ALGOS.items():
        if t_col not in df.columns:
            continue
        times = df[t_col].replace(TIMEOUT, np.nan).dropna()
        nodes = df[n_col].replace(0, np.nan).dropna() if n_col in df.columns else pd.Series(dtype=float)
        solved = (df[t_col] != TIMEOUT).sum()
        total  = len(df)
        ns_per_node = (times / nodes * 1e9).mean() if len(nodes) > 0 else np.nan
        rows.append({
            "Algorithm":     name,
            "Solved":        f"{solved}/{total}",
            "Mean time (s)": f"{times.mean():.3f}" if len(times) else "—",
            "Median time":   f"{times.median():.3f}" if len(times) else "—",
            "Max time":      f"{times.max():.3f}"  if len(times) else "—",
            "Mean nodes":    f"{nodes.mean():.2e}" if len(nodes) else "—",
            "ns/node":       f"{ns_per_node:.1f}"  if not np.isnan(ns_per_node) else "—",
        })
    result = pd.DataFrame(rows).set_index("Algorithm")
    print("\n=== Summary Table ===")
    print(result.to_string())
    return result

summary_table(df)

# ---------------------------------------------------------------------------
# Plots
# ---------------------------------------------------------------------------

os.makedirs("plots", exist_ok=True)

COLORS = {
    "A*":       "#4e79a7",
    "Rev-A*":   "#59a14f",
    "BAE*":     "#f28e2b",
    "MM":       "#e15759",
    "IDA*":     "#76b7b2",
    "Rev-IDA*": "#edc948",
    "BiHS-50%": "#b07aa1",
    "BiHS-10%": "#ff9da7",
    "BiHS-1%":  "#9c755f",
}

# --- 1. Mean time bar chart ---
fig, ax = plt.subplots(figsize=(11, 5))
names, means, errs = [], [], []
for name, (t_col, _) in ALGOS.items():
    if t_col not in df.columns:
        continue
    times = df[t_col].replace(TIMEOUT, np.nan).dropna()
    if len(times) == 0:
        continue
    names.append(name)
    means.append(times.mean())
    errs.append(times.std())

bars = ax.bar(names, means, yerr=errs, capsize=4,
              color=[COLORS[n] for n in names], edgecolor="black", linewidth=0.6)
ax.set_ylabel("Mean solving time (s)")
ax.set_title("Mean Solving Time ± Std Dev")
ax.set_yscale("log")
ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
plt.xticks(rotation=20, ha="right")
plt.tight_layout()
plt.savefig("plots/time_bar.png", dpi=150)
plt.close()
print("Saved plots/time_bar.png")

# --- 2. Nodes expanded bar chart ---
fig, ax = plt.subplots(figsize=(11, 5))
names2, means2 = [], []
for name, (_, n_col) in ALGOS.items():
    if n_col not in df.columns:
        continue
    nodes = df[n_col].replace(0, np.nan).dropna()
    if len(nodes) == 0:
        continue
    names2.append(name)
    means2.append(nodes.mean())

ax.bar(names2, means2, color=[COLORS[n] for n in names2], edgecolor="black", linewidth=0.6)
ax.set_ylabel("Mean nodes expanded")
ax.set_title("Mean Nodes Expanded")
ax.set_yscale("log")
ax.yaxis.set_major_formatter(ticker.ScalarFormatter())
plt.xticks(rotation=20, ha="right")
plt.tight_layout()
plt.savefig("plots/nodes_bar.png", dpi=150)
plt.close()
print("Saved plots/nodes_bar.png")

# --- 3. ns/node bar chart ---
fig, ax = plt.subplots(figsize=(11, 5))
ns_names, ns_vals = [], []
for name, (t_col, n_col) in ALGOS.items():
    if t_col not in df.columns or n_col not in df.columns:
        continue
    times = df[t_col].replace(TIMEOUT, np.nan)
    nodes = df[n_col].replace(0, np.nan)
    ratio = (times / nodes * 1e9).dropna()
    if len(ratio) == 0:
        continue
    ns_names.append(name)
    ns_vals.append(ratio.mean())

ax.bar(ns_names, ns_vals, color=[COLORS[n] for n in ns_names], edgecolor="black", linewidth=0.6)
ax.set_ylabel("ns per node")
ax.set_title("Average Cost per Node Expansion")
plt.xticks(rotation=20, ha="right")
plt.tight_layout()
plt.savefig("plots/ns_per_node.png", dpi=150)
plt.close()
print("Saved plots/ns_per_node.png")

# --- 4. Time vs solution length scatter ---
fig, ax = plt.subplots(figsize=(10, 6))
highlight = ["IDA*", "BiHS-50%", "BiHS-10%", "BiHS-1%"]
for name in highlight:
    t_col, _ = ALGOS[name]
    if t_col not in df.columns:
        continue
    mask = df[t_col] != TIMEOUT
    ax.scatter(df.loc[mask, "solution_length"], df.loc[mask, t_col],
               label=name, alpha=0.7, s=40, color=COLORS[name])

ax.set_xlabel("Solution length (moves)")
ax.set_ylabel("Solving time (s)")
ax.set_title("Solving Time vs Solution Length")
ax.set_yscale("log")
ax.legend()
plt.tight_layout()
plt.savefig("plots/time_vs_length.png", dpi=150)
plt.close()
print("Saved plots/time_vs_length.png")

# --- 5. Box plot of times ---
fig, ax = plt.subplots(figsize=(12, 6))
box_data, box_labels, box_colors = [], [], []
for name, (t_col, _) in ALGOS.items():
    if t_col not in df.columns:
        continue
    times = df[t_col].replace(TIMEOUT, np.nan).dropna()
    if len(times) == 0:
        continue
    box_data.append(times.values)
    box_labels.append(name)
    box_colors.append(COLORS[name])

bp = ax.boxplot(box_data, patch_artist=True, labels=box_labels)
for patch, color in zip(bp["boxes"], box_colors):
    patch.set_facecolor(color)
    patch.set_alpha(0.7)
ax.set_ylabel("Solving time (s)")
ax.set_title("Solving Time Distribution")
ax.set_yscale("log")
plt.xticks(rotation=20, ha="right")
plt.tight_layout()
plt.savefig("plots/time_boxplot.png", dpi=150)
plt.close()
print("Saved plots/time_boxplot.png")

# --- 6. BiHS-Bloom timeout rate by percentage ---
if len(params) > 0:
    fig, ax = plt.subplots(figsize=(7, 4))
    for ratio, color in [(0.5, COLORS["BiHS-50%"]), (0.1, COLORS["BiHS-10%"]), (0.01, COLORS["BiHS-1%"])]:
        sub = params[params["ratio"] == ratio]
        if len(sub) == 0:
            continue
        timeout_pct = (sub["time"] == TIMEOUT).mean() * 100
        ax.bar(f"{int(ratio*100)}%", timeout_pct, color=color, edgecolor="black", linewidth=0.6)
    ax.set_ylabel("Timeout rate (%)")
    ax.set_title("BiHS-Bloom Timeout Rate by Memory %")
    ax.set_ylim(0, 105)
    plt.tight_layout()
    plt.savefig("plots/bihs_timeout_rate.png", dpi=150)
    plt.close()
    print("Saved plots/bihs_timeout_rate.png")

print("\nDone.")
