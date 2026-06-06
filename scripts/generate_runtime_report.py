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
    ("10pct", "k1"): "#d36b8a",
    ("10pct", "optk"): "#ff9da7",
    ("1pct", "k1"): "#7f5a3d",
    ("1pct", "optk"): "#9c755f",
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

    for col in df.columns:
        match = re.fullmatch(r"bihs_bloom_time_(50pct|10pct|1pct)(?:_(k1|optk))?", col)
        if not match:
            continue

        ratio_slug = match.group(1)
        k_mode = match.group(2)
        suffix = f"_{k_mode}" if k_mode else ""
        nodes_col = f"bihs_bloom_nodes_{ratio_slug}{suffix}"
        if nodes_col not in df.columns:
            continue

        label = f"BiHS {RATIO_LABELS[ratio_slug]}"
        if k_mode == "k1":
            label += " k=1"
        elif k_mode == "optk":
            label += " opt-k"

        seen_bihs.append((ratio_slug, k_mode or "old", label, col, nodes_col))

    ratio_order = {"50pct": 0, "10pct": 1, "1pct": 2}
    mode_order = {"k1": 0, "optk": 1, "old": 1}
    for ratio_slug, k_mode, label, time_col, nodes_col in sorted(
        seen_bihs,
        key=lambda item: (ratio_order[item[0]], mode_order[item[1]]),
    ):
        algos[label] = (time_col, nodes_col)
        colors[label] = BIHS_COLORS.get((ratio_slug, k_mode), BIHS_COLORS.get((ratio_slug, "optk"), "#9c755f"))

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
    ratio_labels = {0.5: "50%", 0.1: "10%", 0.01: "1%"}
    mode_labels = {"k1": "k=1", "optk": "opt-k"}
    groups_meta = []
    for ratio in [0.5, 0.1, 0.01]:
        ratio_modes = params.loc[params["ratio"] == ratio, "k_mode"].dropna().unique()
        for mode in ["k1", "optk"]:
            if mode in ratio_modes:
                groups_meta.append((ratio, mode, f"{ratio_labels[ratio]}\n{mode_labels[mode]}"))
        if not any(mode in ratio_modes for mode in ["k1", "optk"]) and len(ratio_modes):
            mode = ratio_modes[0]
            groups_meta.append((ratio, mode, ratio_labels[ratio]))

    for ax, metric, title, ylabel in [
        (axes[0], "size_kib", "Bloom Size", "KiB"),
        (axes[1], "k_hashes", "Hash Count k", "k"),
        (axes[2], "fp_rate", "Estimated FP Rate", "FP rate"),
    ]:
        groups = []
        labels = []
        colors = []
        for ratio, mode, label in groups_meta:
            values = params.loc[(params["ratio"] == ratio) & (params["k_mode"] == mode), metric].dropna()
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


def bihs_label_metadata(name):
    match = re.fullmatch(r"BiHS (50%|10%|1%)(?: (k=1|opt-k))?", name)
    if not match:
        return None
    ratio = {"50%": 0.5, "10%": 0.1, "1%": 0.01}[match.group(1)]
    mode_label = match.group(2)
    k_mode = "k1" if mode_label == "k=1" else "optk"
    return ratio, k_mode


def build_k_lookup(params):
    if params.empty:
        return {}
    normalized = params.copy()
    normalized["ratio"] = normalized["ratio"].round(3)
    normalized["k_mode"] = normalized["k_mode"].fillna("optk")
    return {
        (int(row.instance), round(float(row.ratio), 3), str(row.k_mode)): int(row.k_hashes)
        for row in normalized.itertuples(index=False)
    }


def detail_table_html(df, params):
    rows = []
    available = [(name, cols[0]) for name, cols in ALGOS.items() if cols[0] in df.columns]
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
                    ratio, k_mode = meta
                    k_hashes = k_lookup.get((int(row["instance"]), round(ratio, 3), k_mode))
                    if k_hashes is not None:
                        k_text = f'<span class="cell-note">k={k_hashes}</span>'
                cell = f"{time_text}{k_text}"
            detail[name] = cell
        rows.append(detail)

    return pd.DataFrame(rows).to_html(index=False, classes="detail-table", border=0, escape=False)


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
    <section class="table-panel detail-panel">
      <h2>Challenge Runtimes</h2>
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
