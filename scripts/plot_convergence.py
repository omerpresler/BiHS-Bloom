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
df["_csv_order"] = np.arange(len(df))
if "k_mode" not in df.columns:
    df["k_mode"] = "optk"
else:
    df["k_mode"] = df["k_mode"].fillna("optk")

# Load converged flag and k per (instance, ratio, k_mode) from the benchmark CSV
converged_map = {}
k_hashes_map = {}
if os.path.exists(BENCH_CSV):
    with open(BENCH_CSV) as f:
        for line in f:
            if line.startswith("BIHS_PARAM"):
                parts = line.strip().split(",")
                k_mode = parts[9] if len(parts) > 9 else "optk"
                key = (int(parts[1]), round(float(parts[2]), 3), k_mode)
                converged_map[key] = bool(int(parts[8]))
                k_hashes_map[key] = int(parts[4])

RATIOS      = [0.5, 0.1, 0.01]
RATIO_NAMES = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
RATIO_SLUGS = {0.5: "50pct", 0.1: "10pct", 0.01: "1pct"}
K_MODE_NAMES = {"k1": "k=1", "optk": "opt-k"}

available_runs = {
    (float(row.ratio), str(row.k_mode))
    for row in df[["ratio", "k_mode"]].drop_duplicates().itertuples(index=False)
}
RUNS = [
    (ratio, k_mode)
    for ratio in RATIOS
    for k_mode in ["k1", "optk"]
    if (ratio, k_mode) in available_runs
]
if not RUNS:
    RUNS = [(ratio, "optk") for ratio in RATIOS]

HAS_BITS = "bits_set" in df.columns
HAS_FILL = "fill_ratio" in df.columns
HAS_MATERIALIZED = {
    "materialized_forward",
    "materialized_backward",
    "materialized_total",
}.issubset(df.columns)
HAS_TYPE_COLUMNS = {"phase", "type_index", "type_count"}.issubset(df.columns)

ITER_CMAP = plt.get_cmap("tab10")
LOG_Y_MIN = 0.1

def format_log_tick(value, _):
    return f"{value:g}" if value < 1 else f"{int(value):,}"

def format_count(value):
    return f"{int(value):,}"

def run_title_label(puzzle_id, ratio, k_mode, rdata):
    mem_label = f"{RATIO_NAMES[ratio]} {K_MODE_NAMES.get(k_mode, k_mode)}"
    k_hashes = None
    if "k_hashes" in rdata.columns and not rdata.empty:
        k_hashes = int(rdata["k_hashes"].iloc[0])
    else:
        k_hashes = k_hashes_map.get((puzzle_id, ratio, k_mode))
    if k_hashes is not None:
        mem_label = f"{mem_label} (k={k_hashes})"
    return mem_label

def get_last_actual_run(rdata):
    if not HAS_MATERIALIZED or rdata.empty:
        return None
    actual = rdata[rdata["materialized_total"] > 0]
    if actual.empty:
        return None
    sort_cols = ["total_depth", "_csv_order"] if "_csv_order" in actual.columns else ["total_depth", "iteration"]
    return actual.sort_values(sort_cols).iloc[-1]

def add_inferred_type_split_labels(rdata):
    rdata = rdata.sort_values(["total_depth", "_csv_order"]).reset_index(drop=True)
    rdata["plot_step"] = rdata.groupby("total_depth").cumcount()
    if HAS_TYPE_COLUMNS:
        rdata["phase"] = rdata["phase"].fillna("iter")
        rdata["type_index"] = rdata["type_index"].fillna(0).astype(int)
        rdata["type_count"] = rdata["type_count"].fillna(1).astype(int)
        rdata["step_label"] = rdata.apply(
            lambda row: (
                f"t{int(row['type_index'])}/{int(row['type_count'])}"
                if row["phase"] == "type"
                else f"i{int(row['iteration'])}"
            ),
            axis=1,
        )
        return rdata

    rdata["phase"] = "iter"

    for _, indexes in rdata.groupby("total_depth", sort=False).groups.items():
        prev_iteration = None
        split_start = None
        for pos, idx in enumerate(indexes):
            iteration = int(rdata.at[idx, "iteration"])
            if prev_iteration is not None and iteration <= prev_iteration:
                split_start = pos
                break
            prev_iteration = iteration

        if split_start is not None:
            split_indexes = list(indexes)[split_start:]
            rdata.loc[split_indexes, "phase"] = "type"

    rdata["step_label"] = rdata.apply(
        lambda row: f"t{int(row['iteration'])}" if row["phase"] == "type" else f"i{int(row['iteration'])}",
        axis=1,
    )
    return rdata

def infer_plot_step_labels(rdata):
    labels = []
    max_step = int(rdata["plot_step"].max())
    for step in range(max_step + 1):
        step_rows = rdata[rdata["plot_step"] == step]
        typed = step_rows[step_rows["phase"] == "type"]
        source = typed.iloc[0] if not typed.empty else step_rows.iloc[0]
        labels.append(source["step_label"])
    return labels

def add_actual_summary(ax, last_actual):
    if last_actual is None:
        return
    text = (
        f"actual forward: {format_count(last_actual['materialized_forward'])}\n"
        f"actual backward: {format_count(last_actual['materialized_backward'])}"
    )
    ax.text(
        0.03, 0.97, text,
        transform=ax.transAxes,
        ha="left", va="top",
        fontsize=9, fontweight="bold",
        bbox=dict(boxstyle="round,pad=0.35", facecolor="white", edgecolor="black", alpha=0.88),
        zorder=6,
    )

def annotate_actual_point(ax, x, y, label, xytext):
    if y <= 0:
        return
    ax.annotate(
        label,
        xy=(x, y),
        xytext=xytext,
        textcoords="offset points",
        ha="center",
        va="bottom",
        fontsize=8,
        fontweight="bold",
        bbox=dict(boxstyle="round,pad=0.2", facecolor="white", edgecolor="none", alpha=0.78),
        zorder=7,
    )

def save_convergence_plot(filename):
    os.makedirs(OUT_DIR, exist_ok=True)
    plt.savefig(os.path.join(OUT_DIR, filename), dpi=150)

COLORS = {
    "n_inserted":            "#4e79a7",
    "bits_set":              "#59a14f",
    "materialized_forward":  "#e15759",
    "materialized_backward": "#f28e2b",
}

instances = sorted(df["instance"].unique())
print(f"Generating {len(instances)} convergence plots...")

for puzzle_id in instances:
    pdata = df[df["instance"] == puzzle_id]

    # -----------------------------------------------------------------------
    # Main figure: n_inserted (left) and fill percentage (right) per ratio
    # -----------------------------------------------------------------------
    BAR_W = 0.25
    fig, axes = plt.subplots(1, len(RUNS), figsize=(8 * len(RUNS), 6), squeeze=False)
    fig.suptitle(f"Puzzle #{puzzle_id} — Bloom Convergence", fontsize=14, fontweight="bold")

    for col, (ratio, k_mode) in enumerate(RUNS):
        ax  = axes[0][col]
        ax2 = ax.twinx()

        rdata = pdata[(pdata["ratio"] == ratio) & (pdata["k_mode"] == k_mode)].copy()
        mem_label = run_title_label(puzzle_id, ratio, k_mode, rdata)

        if rdata.empty:
            ax.set_title(f"Memory {mem_label}")
            ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            continue

        size_kib = rdata["size_kib"].iloc[0]
        ax.set_title(f"Memory {mem_label}  ({size_kib:,} KiB)")

        rdata = add_inferred_type_split_labels(rdata)
        xlabels = [f"{row.total_depth}-{row.step_label}" for _, row in rdata.iterrows()]
        x = np.arange(len(xlabels))

        m_bits   = rdata["size_kib"].iloc[0] * 1024 * 8
        n_ins    = rdata["n_inserted"].values.astype(float)
        if HAS_FILL:
            fill_pct = rdata["fill_ratio"].values.astype(float) * 100
        else:
            fill_pct = (rdata["bits_set"].values.astype(float) / m_bits * 100) if HAS_BITS else np.zeros(len(rdata))

        ax.bar(x, n_ins, BAR_W, color=COLORS["n_inserted"],
               edgecolor="black", linewidth=0.6, label="n_inserted")
        last_actual = get_last_actual_run(rdata)
        if last_actual is not None:
            actual_index = int(last_actual.name)
            actual_forward = float(last_actual["materialized_forward"])
            actual_backward = float(last_actual["materialized_backward"])
            if actual_forward > 0:
                ax.scatter([actual_index], [actual_forward],
                           color=COLORS["materialized_forward"], marker="^", s=70,
                           edgecolor="black", linewidth=0.5, zorder=4,
                           label="actual forward")
                annotate_actual_point(ax, actual_index, actual_forward,
                                      format_count(actual_forward), (0, 8))
            if actual_backward > 0:
                ax.scatter([actual_index], [actual_backward],
                           color=COLORS["materialized_backward"], marker="v", s=70,
                           edgecolor="black", linewidth=0.5, zorder=4,
                           label="actual backward")
                annotate_actual_point(ax, actual_index, actual_backward,
                                      format_count(actual_backward), (0, -18))
            add_actual_summary(ax, last_actual)
        ax.set_ylabel("n_inserted (log scale)")
        ax.set_yscale("log")
        ax.set_ylim(bottom=LOG_Y_MIN)
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(format_log_tick))

        ax2.bar(x + BAR_W, fill_pct, BAR_W, color=COLORS["bits_set"],
                edgecolor="black", linewidth=0.6, label="fill %")
        ax2.set_ylabel("%", color="black")
        ax2.set_ylim(-2, 115)
        ax2.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:.0f}%"))

        ax.set_xticks(x)
        ax.set_xticklabels(xlabels, rotation=65, ha="right", fontsize=7)
        ax.set_xlabel("depth-step")

        depths = rdata["total_depth"].unique()
        pos = 0
        for di, depth in enumerate(sorted(depths)):
            cnt = (rdata["total_depth"] == depth).sum()
            if di > 0:
                ax.axvline(pos - 0.5, color="gray", linestyle="--", linewidth=0.8, alpha=0.45)
            pos += cnt

        ax.grid(True, axis="y", alpha=0.2)
        h1, l1 = ax.get_legend_handles_labels()
        h2, l2 = ax2.get_legend_handles_labels()
        ax.legend(h1 + h2, l1 + l2, fontsize=7, loc="upper right")

    plt.tight_layout()
    save_convergence_plot(f"puzzle_{puzzle_id:03d}.png")
    plt.close()

    # -----------------------------------------------------------------------
    # Focus figures: one per ratio, 2 subplots (fill / n_inserted)
    # x-axis = total_depth, one colored line per iteration
    # -----------------------------------------------------------------------
    for ratio, k_mode in RUNS:
        rdata = pdata[(pdata["ratio"] == ratio) & (pdata["k_mode"] == k_mode)].copy()
        mem_label = run_title_label(puzzle_id, ratio, k_mode, rdata)
        slug      = f"{RATIO_SLUGS[ratio]}_{k_mode}"

        conv = converged_map.get((puzzle_id, ratio, k_mode))
        conv_tag = ""
        if conv is True:
            conv_tag = "  ✓ Converged"
        elif conv is False:
            conv_tag = "  ✗ Timed Out"

        fig, axes = plt.subplots(1, 2, figsize=(12, 5), squeeze=False)
        fig.suptitle(
            f"Puzzle #{puzzle_id} — Memory {mem_label}  ({rdata['size_kib'].iloc[0]:,} KiB){conv_tag}" if not rdata.empty
            else f"Puzzle #{puzzle_id} — Memory {mem_label}{conv_tag}",
            fontsize=14, fontweight="bold",
        )

        if rdata.empty:
            for ax in axes[0]:
                ax.text(0.5, 0.5, "no data", ha="center", va="center", transform=ax.transAxes)
            plt.tight_layout()
            save_convergence_plot(f"puzzle_{puzzle_id:03d}_{slug}.png")
            plt.close()
            continue

        rdata = add_inferred_type_split_labels(rdata)
        if HAS_FILL:
            rdata["fill_pct"] = rdata["fill_ratio"] * 100
        elif HAS_BITS:
            m_bits = rdata["size_kib"].iloc[0] * 1024 * 8
            rdata["fill_pct"] = rdata["bits_set"] / m_bits * 100
        plot_steps = list(range(int(rdata["plot_step"].max()) + 1))
        plot_step_labels = infer_plot_step_labels(rdata)
        depths     = sorted(rdata["total_depth"].unique())

        METRICS = [
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
                y = ddata.set_index("plot_step")[metric].reindex(plot_steps)
                ax.plot(plot_steps, y.values, marker="o", markersize=4,
                        linewidth=1.6, color=color, label=f"depth {depth}")

            ax.set_title(ylabel)
            ax.set_xlabel("step")
            ax.set_xticks(plot_steps)
            ax.set_xticklabels(plot_step_labels)
            ax.set_ylabel(ylabel)
            if use_log:
                ax.set_yscale("log")
                ax.set_ylim(bottom=LOG_Y_MIN)
                ax.yaxis.set_major_formatter(ticker.FuncFormatter(format_log_tick))
                if metric == "n_inserted" and HAS_MATERIALIZED:
                    last_actual = get_last_actual_run(rdata)
                    if last_actual is not None:
                        actual_step = int(last_actual["plot_step"]) if "plot_step" in last_actual.index else int(last_actual["iteration"])
                        actual_forward = float(last_actual["materialized_forward"])
                        actual_backward = float(last_actual["materialized_backward"])
                        if actual_forward > 0:
                            ax.scatter([actual_step], [actual_forward],
                                       color=COLORS["materialized_forward"], marker="^", s=70,
                                       edgecolor="black", linewidth=0.5, zorder=4,
                                       label="actual forward")
                            annotate_actual_point(ax, actual_step, actual_forward,
                                                  format_count(actual_forward), (0, 8))
                        if actual_backward > 0:
                            ax.scatter([actual_step], [actual_backward],
                                       color=COLORS["materialized_backward"], marker="v", s=70,
                                       edgecolor="black", linewidth=0.5, zorder=4,
                                       label="actual backward")
                            annotate_actual_point(ax, actual_step, actual_backward,
                                                  format_count(actual_backward), (0, -18))
                        add_actual_summary(ax, last_actual)
            elif metric == "fill_pct":
                ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x:.1f}%"))
            ax.grid(True, alpha=0.2)
            ax.legend(fontsize=7, loc="upper left", bbox_to_anchor=(1.01, 1), borderaxespad=0)

        plt.tight_layout()
        save_convergence_plot(f"puzzle_{puzzle_id:03d}_{slug}.png")
        plt.close()

    if puzzle_id % 10 == 0:
        print(f"  ...done puzzle {puzzle_id}")

print(f"\nAll plots saved to {OUT_DIR}/")
