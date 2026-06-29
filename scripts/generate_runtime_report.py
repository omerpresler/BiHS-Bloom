import argparse
import base64
import html
import os
import re
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import pandas as pd


CSV_FILE = "benchmark_stp_korf100.csv"
OUT_DIR = Path("plots") / "runtime_report_assets"
OUT_HTML = Path("plots") / "runtime_report.html"
TIMEOUT = -1.0
OUT_OF_MEMORY = -2.0
SKIPPED = -3.0

BASE_ALGOS = {
    "A*": ("a_star_time", "a_star_nodes"),
    "Rev-A*": ("rev_a_star_time", "rev_a_star_nodes"),
    "BAE*": ("bae_time", "bae_nodes"),
    "NBS": ("nbs_time", "nbs_nodes"),
    "MM": ("mm_time", "mm_nodes"),
    "IDA*": ("ida_time", "ida_nodes"),
    "Rev-IDA*": ("rev_ida_time", "rev_ida_nodes"),
}

ALGOS = dict(BASE_ALGOS)

BASE_COLORS = {
    "A*": "#4e79a7",
    "Rev-A*": "#59a14f",
    "BAE*": "#f28e2b",
    "NBS": "#8cd17d",
    "MM": "#e15759",
    "IDA*": "#76b7b2",
    "Rev-IDA*": "#edc948",
}

COLORS = dict(BASE_COLORS)

BIHS_COLORS = {
    ("50pct", "k1"): "#7b68a6",
    ("50pct", "optk"): "#b07aa1",
    ("50pct", "rootk"): "#4c78a8",
    ("10pct", "k1"): "#d36b8a",
    ("10pct", "optk"): "#ff9da7",
    ("10pct", "rootk"): "#f58518",
    ("1pct", "k1"): "#7f5a3d",
    ("1pct", "optk"): "#9c755f",
    ("1pct", "rootk"): "#54a24b",
}

IDTHS_COLORS = {
    "50pct": "#5f8fbd",
    "10pct": "#67a772",
    "1pct": "#c58b43",
}

RATIO_LABELS = {
    "50pct": "50%",
    "10pct": "10%",
    "1pct": "1%",
}


def load_results(path):
    rows_main = []
    rows_param = []
    with open(path, encoding="utf-8") as f:
        header = f.readline().strip().split(",")
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("BIHS_PARAM"):
                parts = line.split(",")
                rows_param.append(
                    {
                        "instance": int(parts[1]),
                        "ratio": float(parts[2]),
                        "size_kib": int(parts[3]),
                        "k_hashes": int(parts[4]),
                        "time": float(parts[5]),
                        "nodes": int(parts[6]),
                        "fp_rate": float(parts[7]) if len(parts) > 7 else np.nan,
                        "converged": int(parts[8]) if len(parts) > 8 else np.nan,
                        "k_mode": parts[9] if len(parts) > 9 else "optk",
                        "split_mode": parts[10] if len(parts) > 10 else "fixed",
                    }
                )
            else:
                rows_main.append(dict(zip(header, line.split(","))))

    df = pd.DataFrame(rows_main).apply(pd.to_numeric, errors="coerce")
    params = pd.DataFrame(rows_param)
    return df, params


def discover_algorithms(df):
    algos = dict(BASE_ALGOS)
    colors = dict(BASE_COLORS)
    seen_bihs = []
    seen_idths = []

    for col in df.columns:
        match = re.fullmatch(r"bihs_bloom_time_(50pct|10pct|1pct)(?:_(k1|optk|rootk))?(?:_(fixed|dynamic))?", col)
        if match:
            ratio_slug = match.group(1)
            k_mode = match.group(2)
            split_mode = match.group(3) or "fixed"
            suffix = f"_{k_mode}" if k_mode else ""
            if match.group(3):
                suffix += f"_{split_mode}"
            nodes_col = f"bihs_bloom_nodes_{ratio_slug}{suffix}"
            if nodes_col not in df.columns:
                continue

            label = f"BiHS {RATIO_LABELS[ratio_slug]}"
            if k_mode == "k1":
                label += " k1"
            elif k_mode == "optk":
                label += " optk"
            elif k_mode == "rootk":
                label += " rootk"

            seen_bihs.append((ratio_slug, k_mode or "old", split_mode, label, col, nodes_col))
            continue

        match = re.fullmatch(r"idths_trans_time_(50pct|10pct|1pct)", col)
        if match:
            ratio_slug = match.group(1)
            nodes_col = f"idths_trans_nodes_{ratio_slug}"
            if nodes_col in df.columns:
                seen_idths.append((ratio_slug, f"IDTHSwTrans {RATIO_LABELS[ratio_slug]}", col, nodes_col))

    ratio_order = {"50pct": 0, "10pct": 1, "1pct": 2}
    mode_order = {"k1": 0, "optk": 1, "rootk": 2, "old": 1}
    split_order = {"fixed": 0, "dynamic": 1}
    for ratio_slug, k_mode, split_mode, label, time_col, nodes_col in sorted(
        seen_bihs,
        key=lambda item: (ratio_order[item[0]], mode_order.get(item[1], 9), split_order.get(item[2], 9)),
    ):
        algos[label] = (time_col, nodes_col)
        colors[label] = BIHS_COLORS.get((ratio_slug, k_mode), BIHS_COLORS.get((ratio_slug, "optk"), "#9c755f"))

    for ratio_slug, label, time_col, nodes_col in sorted(seen_idths, key=lambda item: ratio_order[item[0]]):
        algos[label] = (time_col, nodes_col)
        colors[label] = IDTHS_COLORS.get(ratio_slug, "#5f8fbd")

    return algos, colors


def clean_times(series):
    return series.replace([TIMEOUT, OUT_OF_MEMORY, SKIPPED], np.nan)


def algo_frame(df):
    rows = []
    for name, (time_col, nodes_col) in ALGOS.items():
        if time_col not in df.columns:
            continue
        for _, row in df.iterrows():
            time = row[time_col]
            nodes = row[nodes_col] if nodes_col in df.columns else np.nan
            status = "solved"
            if time == TIMEOUT:
                status = "timeout"
            elif time == OUT_OF_MEMORY:
                status = "oom"
            elif time == SKIPPED:
                status = "skipped"
            rows.append(
                {
                    "instance": int(row["instance"]),
                    "solution_length": row["solution_length"],
                    "algorithm": name,
                    "time": np.nan if status != "solved" else time,
                    "nodes": np.nan if nodes == 0 else nodes,
                    "status": status,
                }
            )
    return pd.DataFrame(rows)


def fmt_num(value, digits=2):
    if pd.isna(value):
        return "-"
    if abs(value) >= 1_000_000:
        return f"{value:.2e}"
    if abs(value) >= 100:
        return f"{value:,.0f}"
    return f"{value:.{digits}f}"


def save_fig(fig, filename):
    path = OUT_DIR / filename
    fig.savefig(path, dpi=170, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    return path


def img_data_uri(path):
    encoded = base64.b64encode(path.read_bytes()).decode("ascii")
    return f"data:image/png;base64,{encoded}"


def build_summary(long_df):
    grouped = long_df.groupby("algorithm", sort=False)
    rows = []
    total = long_df["instance"].nunique()
    best = (
        long_df.dropna(subset=["time"])
        .sort_values(["instance", "time"])
        .groupby("instance")
        .first()
    )
    win_counts = best["algorithm"].value_counts()

    for name in ALGOS:
        if name not in grouped.groups:
            continue
        sub = grouped.get_group(name)
        solved = sub["time"].notna().sum()
        times = sub["time"].dropna()
        nodes = sub["nodes"].dropna()
        ns_per_node = (sub["time"] / sub["nodes"] * 1e9).replace([np.inf, -np.inf], np.nan).dropna()
        rows.append(
            {
                "Algorithm": name,
                "Solved": f"{solved}/{total}",
                "Median time (s)": times.median(),
                "Mean time (s)": times.mean(),
                "P90 time (s)": times.quantile(0.9),
                "Max time (s)": times.max(),
                "Median nodes": nodes.median(),
                "Mean ns/node": ns_per_node.mean(),
                "Fastest wins": int(win_counts.get(name, 0)),
            }
        )
    return pd.DataFrame(rows)


def make_runtime_bar(summary):
    fig, ax = plt.subplots(figsize=(12, 5.8))
    names = summary["Algorithm"].tolist()
    values = summary["Median time (s)"].to_numpy()
    bars = ax.bar(names, values, color=[COLORS[n] for n in names], edgecolor="#20242a", linewidth=0.7)
    ax.set_yscale("log")
    ax.set_ylabel("Median runtime (seconds, log scale)")
    ax.set_title("Median Runtime by Algorithm", pad=14, fontweight="bold")
    ax.grid(True, axis="y", alpha=0.18)
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.tick_params(axis="x", rotation=25)
    for bar, value in zip(bars, values):
        if not np.isfinite(value) or value <= 0:
            continue
        ax.text(bar.get_x() + bar.get_width() / 2, value, fmt_num(value), ha="center", va="bottom", fontsize=8)
    return save_fig(fig, "median_runtime.png")


def make_boxplot(long_df):
    fig, ax = plt.subplots(figsize=(12, 6))
    data = []
    labels = []
    colors = []
    for name in ALGOS:
        times = long_df.loc[long_df["algorithm"] == name, "time"].dropna()
        if times.empty:
            continue
        data.append(times)
        labels.append(name)
        colors.append(COLORS[name])
    bp = ax.boxplot(data, labels=labels, patch_artist=True, showfliers=True)
    for patch, color in zip(bp["boxes"], colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.74)
    ax.set_yscale("log")
    ax.set_ylabel("Runtime (seconds, log scale)")
    ax.set_title("Runtime Distribution", pad=14, fontweight="bold")
    ax.grid(True, axis="y", alpha=0.18)
    ax.tick_params(axis="x", rotation=25)
    return save_fig(fig, "runtime_distribution.png")


def make_instance_heatmap(df):
    time_cols = [cols[0] for cols in ALGOS.values() if cols[0] in df.columns]
    names = [name for name, cols in ALGOS.items() if cols[0] in df.columns]
    matrix = df[time_cols].replace([TIMEOUT, OUT_OF_MEMORY, SKIPPED], np.nan).to_numpy(dtype=float)
    best = np.nanmin(matrix, axis=1)
    ratios = matrix / best[:, None]
    order = np.argsort(df["solution_length"].to_numpy())
    ratios = ratios[order]
    instances = df["instance"].to_numpy()[order]

    fig, ax = plt.subplots(figsize=(12, max(5.5, len(instances) * 0.16)))
    im = ax.imshow(np.log10(ratios), aspect="auto", cmap="viridis", vmin=0, vmax=np.nanpercentile(np.log10(ratios), 95))
    ax.set_xticks(np.arange(len(names)))
    ax.set_xticklabels(names, rotation=25, ha="right")
    ax.set_yticks(np.arange(len(instances)))
    ax.set_yticklabels(instances, fontsize=7)
    ax.set_xlabel("Algorithm")
    ax.set_ylabel("Instance, sorted by solution length")
    ax.set_title("Relative Runtime per Instance", pad=14, fontweight="bold")
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label("log10(time / fastest for instance)")
    return save_fig(fig, "relative_runtime_heatmap.png")


def make_time_vs_length(long_df):
    fig, ax = plt.subplots(figsize=(11, 6))
    for name in ALGOS:
        sub = long_df[(long_df["algorithm"] == name) & long_df["time"].notna()]
        if sub.empty:
            continue
        ax.scatter(
            sub["solution_length"],
            sub["time"],
            label=name,
            color=COLORS[name],
            alpha=0.68,
            s=28,
            edgecolors="none",
        )
    ax.set_yscale("log")
    ax.set_xlabel("Solution length")
    ax.set_ylabel("Runtime (seconds, log scale)")
    ax.set_title("Runtime vs Solution Length", pad=14, fontweight="bold")
    ax.grid(True, alpha=0.18)
    ax.legend(ncol=2, fontsize=8)
    return save_fig(fig, "runtime_vs_solution_length.png")


def make_bihs_params(params):
    if params.empty:
        return None
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.5))
    params = params.copy()
    params["ratio"] = params["ratio"].round(3)
    params["k_mode"] = params["k_mode"].fillna("optk")
    params["split_mode"] = params["split_mode"].fillna("fixed") if "split_mode" in params.columns else "fixed"
    ratio_labels = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
    mode_labels = {"k1": "k1", "optk": "optk", "rootk": "rootk"}
    groups_meta = []
    for ratio in [0.5, 0.1, 0.01]:
        ratio_rows = params[params["ratio"] == ratio]
        ratio_modes = ratio_rows["k_mode"].dropna().unique()
        for mode in ["k1", "optk", "rootk"]:
            mode_rows = ratio_rows[ratio_rows["k_mode"] == mode]
            for split_mode in ["fixed", "dynamic"]:
                if split_mode in mode_rows["split_mode"].dropna().unique():
                    groups_meta.append((ratio, mode, split_mode, f"{ratio_labels[ratio]}\n{mode_labels[mode]}"))
        if not any(mode in ratio_modes for mode in ["k1", "optk", "rootk"]) and len(ratio_modes):
            mode = ratio_modes[0]
            groups_meta.append((ratio, mode, "fixed", ratio_labels[ratio]))

    for ax, metric, title, ylabel in [
        (axes[0], "size_kib", "Bloom Size", "KiB"),
        (axes[1], "k_hashes", "Hash Count k", "k"),
        (axes[2], "fp_rate", "Estimated FP Rate", "FP rate"),
    ]:
        groups = []
        labels = []
        colors = []
        for ratio, mode, split_mode, label in groups_meta:
            values = params.loc[
                (params["ratio"] == ratio) &
                (params["k_mode"] == mode) &
                (params["split_mode"] == split_mode),
                metric
            ].dropna()
            if values.empty:
                continue
            groups.append(values)
            labels.append(label)
            ratio_slug = {0.5: "50pct", 0.1: "10pct", 0.01: "1pct"}[ratio]
            colors.append(BIHS_COLORS.get((ratio_slug, mode), "#9c755f"))
        bp = ax.boxplot(groups, labels=labels, patch_artist=True)
        for patch, color in zip(bp["boxes"], colors):
            patch.set_facecolor(color)
            patch.set_alpha(0.76)
        ax.set_title(title, fontweight="bold")
        ax.set_ylabel(ylabel)
        ax.grid(True, axis="y", alpha=0.18)
        if metric in {"size_kib", "fp_rate"}:
            ax.set_yscale("log")
    return save_fig(fig, "bihs_parameters.png")


def table_html(summary):
    display = summary.copy()
    for col in ["Median time (s)", "Mean time (s)", "P90 time (s)", "Max time (s)", "Median nodes", "Mean ns/node"]:
        display[col] = display[col].map(fmt_num)
    return display.to_html(index=False, classes="summary-table", border=0, escape=False)


def bihs_summary_table_html(summary):
    bihs = summary[summary["Algorithm"].astype(str).str.startswith("BiHS ")].copy()
    if bihs.empty:
        return '<p class="note">No BiHS-Bloom runs were found.</p>'
    return table_html(bihs)


def bihs_label_metadata(name):
    match = re.fullmatch(r"BiHS (50%|10%|1%)(?: (k1|optk|rootk|k=1|opt-k|root-k))?(?: (fixed|dynamic))?", name)
    if not match:
        return None
    ratio = {"50%": 0.5, "10%": 0.1, "1%": 0.01}[match.group(1)]
    mode_label = match.group(2)
    if mode_label in {"k1", "k=1"}:
        k_mode = "k1"
    elif mode_label in {"rootk", "root-k"}:
        k_mode = "rootk"
    else:
        k_mode = "optk"
    split_mode = match.group(3)
    return ratio, k_mode, split_mode


def build_k_lookup(params):
    if params.empty:
        return {}
    normalized = params.copy()
    normalized["ratio"] = normalized["ratio"].round(3)
    normalized["k_mode"] = normalized["k_mode"].fillna("optk")
    normalized["split_mode"] = normalized["split_mode"].fillna("fixed") if "split_mode" in normalized.columns else "fixed"
    return {
        (int(row.instance), round(float(row.ratio), 3), str(row.k_mode), str(row.split_mode)): int(row.k_hashes)
        for row in normalized.itertuples(index=False)
    }


def detail_table_html(df, params):
    return comparison_table_html(df, params, [name for name, cols in ALGOS.items() if cols[0] in df.columns])


def bihs_detail_table_html(df, params):
    return comparison_table_html(
        df,
        params,
        [name for name, cols in ALGOS.items() if name.startswith("BiHS ") and cols[0] in df.columns],
    )


def comparison_table_html(df, params, algorithm_names):
    rows = []
    available = [(name, ALGOS[name][0]) for name in algorithm_names if name in ALGOS and ALGOS[name][0] in df.columns]
    if not available:
        return '<p class="note">No matching algorithm columns were found.</p>'
    k_lookup = build_k_lookup(params)

    for _, row in df.sort_values(["solution_length", "instance"]).iterrows():
        times = {
            name: row[time_col]
            for name, time_col in available
            if row[time_col] not in {TIMEOUT, OUT_OF_MEMORY, SKIPPED} and not pd.isna(row[time_col])
        }
        fastest = min(times.values()) if times else np.nan
        detail = {
            "Instance": int(row["instance"]),
            "Solution": int(row["solution_length"]),
        }

        for name, time_col in available:
            value = row[time_col]
            if value == TIMEOUT:
                cell = '<span class="status timeout">timeout</span>'
            elif value == OUT_OF_MEMORY:
                cell = '<span class="status oom">oom</span>'
            elif value == SKIPPED:
                cell = '<span class="status skipped">skipped</span>'
            elif pd.isna(value):
                cell = "-"
            else:
                cls = "best-time" if not pd.isna(fastest) and value == fastest else ""
                time_text = f'<span class="{cls}">{fmt_num(value, 3)}</span>'
                k_text = ""
                meta = bihs_label_metadata(name)
                if meta is not None:
                    ratio, k_mode, split_mode = meta
                    if split_mode is None:
                        k_hashes = (
                            k_lookup.get((int(row["instance"]), round(ratio, 3), k_mode, "dynamic"))
                            or k_lookup.get((int(row["instance"]), round(ratio, 3), k_mode, "fixed"))
                        )
                    else:
                        k_hashes = k_lookup.get((int(row["instance"]), round(ratio, 3), k_mode, split_mode))
                    if k_hashes is not None:
                        k_text = f'<span class="cell-note">k={k_hashes}</span>'
                cell = f"{time_text}{k_text}"
            detail[name] = cell
        rows.append(detail)

    return pd.DataFrame(rows).to_html(index=False, classes="detail-table", border=0, escape=False)


def idths_vs_ida_algorithms():
    return [
        name
        for name in ["IDA*", "Rev-IDA*", "IDTHSwTrans 50%", "IDTHSwTrans 10%", "IDTHSwTrans 1%"]
        if name in ALGOS
    ]


def bihs_vs_idths_algorithms():
    names = []
    for ratio in ["50%", "10%", "1%"]:
        bihs_names = [name for name in ALGOS if name.startswith(f"BiHS {ratio}")]
        names.extend(bihs_names)
        idths_name = f"IDTHSwTrans {ratio}"
        if idths_name in ALGOS:
            names.append(idths_name)
    return names


def solved_time(value):
    if pd.isna(value) or value in {TIMEOUT, OUT_OF_MEMORY, SKIPPED}:
        return np.nan
    return value


def bihs_vs_idths_winner_table_html(df):
    rows = []
    totals = {
        "both_solved": 0,
        "bihs_wins": 0,
        "idths_wins": 0,
        "ties": 0,
        "bihs_only": 0,
        "idths_only": 0,
    }
    speedups = []

    for ratio in ["50%", "10%", "1%"]:
        idths_name = f"IDTHSwTrans {ratio}"
        if idths_name not in ALGOS:
            continue
        idths_col = ALGOS[idths_name][0]
        for bihs_name in [name for name in ALGOS if name.startswith(f"BiHS {ratio}")]:
            bihs_col = ALGOS[bihs_name][0]
            both_solved = 0
            bihs_wins = 0
            idths_wins = 0
            ties = 0
            bihs_only = 0
            idths_only = 0
            row_speedups = []

            for _, result in df.iterrows():
                bihs_time = solved_time(result[bihs_col])
                idths_time = solved_time(result[idths_col])
                bihs_solved = not pd.isna(bihs_time)
                idths_solved = not pd.isna(idths_time)

                if bihs_solved and idths_solved:
                    both_solved += 1
                    if np.isclose(bihs_time, idths_time):
                        ties += 1
                    elif bihs_time < idths_time:
                        bihs_wins += 1
                    else:
                        idths_wins += 1
                    row_speedups.append(idths_time / bihs_time)
                elif bihs_solved:
                    bihs_only += 1
                elif idths_solved:
                    idths_only += 1

            for key, value in [
                ("both_solved", both_solved),
                ("bihs_wins", bihs_wins),
                ("idths_wins", idths_wins),
                ("ties", ties),
                ("bihs_only", bihs_only),
                ("idths_only", idths_only),
            ]:
                totals[key] += value
            speedups.extend(row_speedups)

            bihs_score = bihs_wins + bihs_only
            idths_score = idths_wins + idths_only
            if bihs_score > idths_score:
                winner = "BiHS-Bloom"
            elif idths_score > bihs_score:
                winner = "IDTHSwTrans"
            else:
                winner = "Tie"

            rows.append(
                {
                    "Comparison": f"{bihs_name} vs {idths_name}",
                    "Winner": winner,
                    "Both solved": both_solved,
                    "BiHS faster": bihs_wins,
                    "IDTHS faster": idths_wins,
                    "Ties": ties,
                    "BiHS solved only": bihs_only,
                    "IDTHS solved only": idths_only,
                    "Median IDTHS/BiHS speedup": np.median(row_speedups) if row_speedups else np.nan,
                }
            )

    if rows:
        bihs_score = totals["bihs_wins"] + totals["bihs_only"]
        idths_score = totals["idths_wins"] + totals["idths_only"]
        if bihs_score > idths_score:
            winner = "BiHS-Bloom"
        elif idths_score > bihs_score:
            winner = "IDTHSwTrans"
        else:
            winner = "Tie"
        rows.append(
            {
                "Comparison": "Overall",
                "Winner": f"<strong>{winner}</strong>",
                "Both solved": totals["both_solved"],
                "BiHS faster": totals["bihs_wins"],
                "IDTHS faster": totals["idths_wins"],
                "Ties": totals["ties"],
                "BiHS solved only": totals["bihs_only"],
                "IDTHS solved only": totals["idths_only"],
                "Median IDTHS/BiHS speedup": np.median(speedups) if speedups else np.nan,
            }
        )

    display = pd.DataFrame(rows)
    if display.empty:
        return "<p class=\"note\">No matched BiHS-Bloom and IDTHSwTrans columns were found.</p>"
    display["Median IDTHS/BiHS speedup"] = display["Median IDTHS/BiHS speedup"].map(
        lambda value: "-" if pd.isna(value) else f"{value:.2f}x"
    )
    return display.to_html(index=False, classes="summary-table", border=0, escape=False)


def bihs_win_pattern_table_html(df):
    rows = []
    ratios = [
        ("50%", "bihs_bloom_time_50pct_optk_dynamic", "idths_trans_time_50pct", "idths_trans_storage_states_50pct"),
        ("10%", "bihs_bloom_time_10pct_optk_dynamic", "idths_trans_time_10pct", "idths_trans_storage_states_10pct"),
        ("1%", "bihs_bloom_time_1pct_optk_dynamic", "idths_trans_time_1pct", "idths_trans_storage_states_1pct"),
    ]

    for label, bihs_col, idths_col, storage_col in ratios:
        if bihs_col not in df.columns or idths_col not in df.columns:
            continue
        sub = df[(df[bihs_col] > 0) & (df[idths_col] > 0)].copy()
        if sub.empty:
            continue
        sub["winner"] = np.where(sub[bihs_col] < sub[idths_col], "BiHS wins", "IDTHS wins")
        sub["idths_bihs_speedup"] = sub[idths_col] / sub[bihs_col]

        for winner in ["BiHS wins", "IDTHS wins"]:
            group = sub[sub["winner"] == winner]
            if group.empty:
                continue
            rows.append(
                {
                    "Ratio": label,
                    "Group": winner,
                    "Count": len(group),
                    "Median solution length": group["solution_length"].median(),
                    "Median A* nodes": group["a_star_nodes"].median(),
                    "Median Rev-A* nodes": group["rev_a_star_nodes"].median(),
                    "Median IDA* nodes": group["ida_nodes"].median(),
                    "Median IDTHS storage": group[storage_col].median() if storage_col in group.columns else np.nan,
                    "Median IDTHS/BiHS speedup": group["idths_bihs_speedup"].median(),
                }
            )

    display = pd.DataFrame(rows)
    if display.empty:
        return "<p class=\"note\">No solved BiHS-Bloom and IDTHSwTrans pairs were found.</p>"
    for col in ["Median solution length", "Median A* nodes", "Median Rev-A* nodes", "Median IDA* nodes", "Median IDTHS storage"]:
        display[col] = display[col].map(fmt_num)
    display["Median IDTHS/BiHS speedup"] = display["Median IDTHS/BiHS speedup"].map(lambda value: f"{value:.2f}x")
    return display.to_html(index=False, classes="summary-table", border=0, escape=False)


def bihs_win_length_bins_html(df):
    rows = []
    bins = [0, 50, 55, 60, 65, 70, 100]
    ratios = [
        ("50%", "bihs_bloom_time_50pct_optk_dynamic", "idths_trans_time_50pct"),
        ("10%", "bihs_bloom_time_10pct_optk_dynamic", "idths_trans_time_10pct"),
        ("1%", "bihs_bloom_time_1pct_optk_dynamic", "idths_trans_time_1pct"),
    ]
    comparisons = []
    for label, bihs_col, idths_col in ratios:
        if bihs_col not in df.columns or idths_col not in df.columns:
            continue
        sub = df[(df[bihs_col] > 0) & (df[idths_col] > 0)].copy()
        sub["Ratio"] = label
        sub["Winner"] = np.where(sub[bihs_col] < sub[idths_col], "BiHS", "IDTHS")
        comparisons.append(sub[["solution_length", "Ratio", "Winner"]])

    if not comparisons:
        return ""
    combined = pd.concat(comparisons)
    combined["Length bin"] = pd.cut(combined["solution_length"], bins=bins, include_lowest=True)
    counts = pd.crosstab(combined["Length bin"], combined["Winner"])
    for winner in ["BiHS", "IDTHS"]:
        if winner not in counts.columns:
            counts[winner] = 0
    counts = counts[["BiHS", "IDTHS"]].reset_index()
    counts["Length bin"] = counts["Length bin"].astype(str)
    return counts.to_html(index=False, classes="summary-table", border=0, escape=False)


def render_html(df, params, summary, image_paths):
    fastest = summary.sort_values("Median time (s)").iloc[0]
    most_wins = summary.sort_values("Fastest wins", ascending=False).iloc[0]
    total_instances = len(df)
    solved_all = int((summary["Solved"] == f"{total_instances}/{total_instances}").sum())
    generated = pd.Timestamp.now().strftime("%Y-%m-%d %H:%M")

    cards = [
        ("Instances", str(total_instances), "Korf STP rows included in the benchmark CSV."),
        ("Best Median", html.escape(fastest["Algorithm"]), f"{fmt_num(fastest['Median time (s)'])} seconds."),
        ("Most Wins", html.escape(most_wins["Algorithm"]), f"{int(most_wins['Fastest wins'])} fastest instances."),
        ("Solved All", str(solved_all), "Algorithms with no timeout/OOM marker in this file."),
    ]

    card_html = "\n".join(
        f"""
        <section class="card">
          <div class="card-label">{label}</div>
          <div class="card-value">{value}</div>
          <p>{detail}</p>
        </section>
        """
        for label, value, detail in cards
    )

    figures = "\n".join(
        f"""
        <figure>
          <img src="{img_data_uri(path)}" alt="{html.escape(title)}">
          <figcaption>{html.escape(title)}</figcaption>
        </figure>
        """
        for title, path in image_paths
        if path is not None
    )

    bihs_note = ""
    if not params.empty:
        params_for_note = params.copy()
        params_for_note["k_mode"] = params_for_note["k_mode"].fillna("optk")
        params_for_note["split_mode"] = params_for_note["split_mode"].fillna("fixed") if "split_mode" in params_for_note.columns else "fixed"
        k_summary = params_for_note.groupby(["ratio", "k_mode"])["k_hashes"].median().sort_index(ascending=False)
        parts = [f"{int(r * 100)}% {mode} median k={int(v)}" for (r, mode), v in k_summary.items()]
        bihs_note = "BiHS parameter medians: " + ", ".join(parts) + "."

    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>STP Runtime Comparison Report</title>
  <style>
    :root {{
      --bg: #f6f7f9;
      --ink: #20242a;
      --muted: #68707d;
      --panel: #ffffff;
      --line: #d9dee7;
      --accent: #3e6fb6;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      background: var(--bg);
      color: var(--ink);
      font-family: Inter, Segoe UI, Arial, sans-serif;
      line-height: 1.45;
    }}
    header {{
      padding: 34px 42px 24px;
      background: #ffffff;
      border-bottom: 1px solid var(--line);
    }}
    h1 {{
      margin: 0;
      font-size: 30px;
      letter-spacing: 0;
    }}
    .subtitle {{
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 880px;
    }}
    main {{
      padding: 28px 42px 42px;
      max-width: 1420px;
      margin: 0 auto;
    }}
    .cards {{
      display: grid;
      grid-template-columns: repeat(4, minmax(0, 1fr));
      gap: 14px;
      margin-bottom: 24px;
    }}
    .card, .table-panel, figure {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      box-shadow: 0 1px 2px rgba(32, 36, 42, 0.04);
    }}
    .card {{
      padding: 18px;
      min-height: 132px;
    }}
    .card-label {{
      color: var(--muted);
      font-size: 13px;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }}
    .card-value {{
      font-size: 28px;
      font-weight: 750;
      margin-top: 8px;
    }}
    .card p {{
      color: var(--muted);
      margin: 8px 0 0;
      font-size: 14px;
    }}
    .grid {{
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 18px;
      align-items: start;
    }}
    figure {{
      margin: 0;
      padding: 14px;
    }}
    figure img {{
      display: block;
      width: 100%;
      height: auto;
    }}
    figcaption {{
      color: var(--muted);
      font-size: 13px;
      margin-top: 10px;
    }}
    .wide {{
      grid-column: 1 / -1;
    }}
    .table-panel {{
      margin: 24px 0;
      padding: 18px;
      overflow-x: auto;
    }}
    .detail-panel {{
      max-height: 680px;
      overflow: auto;
    }}
    h2 {{
      font-size: 18px;
      margin: 0 0 12px;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      font-size: 14px;
    }}
    th, td {{
      padding: 10px 11px;
      border-bottom: 1px solid var(--line);
      text-align: right;
      white-space: nowrap;
    }}
    th:first-child, td:first-child {{
      text-align: left;
    }}
    th {{
      color: var(--muted);
      font-weight: 700;
      background: #fbfcfe;
    }}
    .detail-table th {{
      position: sticky;
      top: 0;
      z-index: 2;
    }}
    .detail-table td:first-child,
    .detail-table th:first-child {{
      position: sticky;
      left: 0;
      z-index: 1;
      background: #ffffff;
    }}
    .detail-table th:first-child {{
      z-index: 3;
      background: #fbfcfe;
    }}
    .best-time {{
      display: inline-block;
      padding: 2px 7px;
      border-radius: 999px;
      color: #17492d;
      background: #dff4e8;
      font-weight: 750;
    }}
    .status {{
      color: var(--muted);
      font-size: 12px;
      text-transform: uppercase;
      letter-spacing: 0.03em;
    }}
    .cell-note {{
      display: block;
      color: var(--muted);
      font-size: 11px;
      margin-top: 2px;
    }}
    .timeout {{ color: #9b3131; }}
    .oom {{ color: #7b4f18; }}
    .skipped {{ color: #586579; }}
    .note {{
      color: var(--muted);
      font-size: 14px;
      margin-top: 12px;
    }}
    footer {{
      color: var(--muted);
      font-size: 13px;
      margin-top: 22px;
    }}
    @media (max-width: 980px) {{
      header, main {{ padding-left: 18px; padding-right: 18px; }}
      .cards, .grid {{ grid-template-columns: 1fr; }}
    }}
  </style>
</head>
<body>
  <header>
    <h1>STP Runtime Comparison Report</h1>
    <p class="subtitle">Runtime and node-expansion comparison across baseline algorithms and BiHS-Bloom memory settings. Generated from <code>{html.escape(CSV_FILE)}</code> on {generated}.</p>
  </header>
  <main>
    <section class="cards">{card_html}</section>
    <section class="table-panel">
      <h2>Summary</h2>
      {table_html(summary)}
      <p class="note">Timeout and out-of-memory markers are excluded from runtime aggregates. {html.escape(bihs_note)}</p>
    </section>
    <section class="table-panel">
      <h2>BiHS-Bloom Summary</h2>
      {bihs_summary_table_html(summary)}
      <p class="note">Only BiHS-Bloom variants are shown here.</p>
    </section>
    <section class="table-panel">
      <h2>BiHS-Bloom vs IDTHSwTrans Winner</h2>
      {bihs_vs_idths_winner_table_html(df)}
      <p class="note">The winner is based on solved-only advantages plus faster runtimes on instances both methods solved. Median speedup is IDTHSwTrans time divided by BiHS-Bloom time, so values above 1.00x favor BiHS-Bloom.</p>
    </section>
    <section class="table-panel">
      <h2>BiHS Win Pattern</h2>
      {bihs_win_pattern_table_html(df)}
      <p class="note">Pattern: BiHS-Bloom wins skew toward easier instances: shorter median solution lengths, fewer baseline A*/Rev-A*/IDA* expansions, and smaller IDTHSwTrans storage budgets. IDTHSwTrans gains ground as instances become larger and the transposition table has more useful states to reuse.</p>
      {bihs_win_length_bins_html(df)}
    </section>
    <section class="table-panel detail-panel">
      <h2>IDTHSwTrans vs IDA*</h2>
      {comparison_table_html(df, params, idths_vs_ida_algorithms())}
      <p class="note">Rows are sorted by solution length. Green pills mark the fastest algorithm among IDA*, Rev-IDA*, and the IDTHSwTrans runs for that challenge.</p>
    </section>
    <section class="table-panel detail-panel">
      <h2>BiHS-Bloom vs IDTHSwTrans</h2>
      {comparison_table_html(df, params, bihs_vs_idths_algorithms())}
      <p class="note">Each BiHS-Bloom memory ratio is shown next to the matching IDTHSwTrans storage ratio. BiHS cells include the actual k used for that run.</p>
    </section>
    <section class="table-panel detail-panel">
      <h2>Full BiHS-Bloom Runtimes</h2>
      {bihs_detail_table_html(df, params)}
      <p class="note">Rows are sorted by solution length. Green pills mark the fastest BiHS-Bloom variant for that challenge. Cells include the actual k used for that run.</p>
    </section>
    <section class="table-panel detail-panel">
      <h2>All Challenge Runtimes</h2>
      {detail_table_html(df, params)}
      <p class="note">Rows are sorted by solution length. Green pills mark the fastest algorithm for that challenge. BiHS cells include the actual k used for that run.</p>
    </section>
    <section class="grid">{figures}</section>
    <footer>Report generated by <code>scripts/generate_runtime_report.py</code>.</footer>
  </main>
</body>
</html>
"""


def main():
    global CSV_FILE, OUT_DIR, OUT_HTML
    parser = argparse.ArgumentParser(description="Generate the STP runtime report.")
    parser.add_argument("--input", default=CSV_FILE, help="Merged benchmark CSV")
    parser.add_argument("--output-dir", default=str(OUT_DIR), help="Directory for report assets")
    parser.add_argument("--output-html", default=str(OUT_HTML), help="Output HTML file")
    args = parser.parse_args()
    CSV_FILE = args.input
    OUT_DIR = Path(args.output_dir)
    OUT_HTML = Path(args.output_html)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_HTML.parent.mkdir(parents=True, exist_ok=True)
    df, params = load_results(CSV_FILE)
    global ALGOS, COLORS
    ALGOS, COLORS = discover_algorithms(df)
    long_df = algo_frame(df)
    summary = build_summary(long_df)

    images = [
        ("Median runtime across algorithms", make_runtime_bar(summary)),
        ("Runtime distribution across instances", make_boxplot(long_df)),
        ("Relative runtime heatmap: each row is normalized to that instance's fastest solver", make_instance_heatmap(df)),
        ("Runtime as solution length increases", make_time_vs_length(long_df)),
        ("BiHS-Bloom memory, k, and estimated false-positive parameters", make_bihs_params(params)),
    ]

    OUT_HTML.write_text(render_html(df, params, summary, images), encoding="utf-8")


if __name__ == "__main__":
    main()
