import ast
from collections import defaultdict
import csv
import os
import math
import argparse

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# -------------------------
# Original constants/logic
# -------------------------
MAX_LEN = 200
MIN_TAIL_LEN = 4


def has_alternating_tail(seq, min_tail_len=MIN_TAIL_LEN):
    if len(seq) < min_tail_len:
        return False

    tail = seq[-min_tail_len:]
    a, b = tail[0], tail[1]

    if a == b:
        return False

    for i, val in enumerate(tail):
        if (i % 2 == 0 and val != a) or (i % 2 == 1 and val != b):
            return False

    return True


def is_stabilized(seq):
    # Backward-compat only (if Termination is missing)
    return len(seq) <= MAX_LEN or has_alternating_tail(seq)


def parse_inserted(value: str):
    """
    Accepts:
      - old: "[1, 2, 3]" (Python list literal)
      - new: "1, 2, 3"   (comma-separated ints)
      - CSV quoted: "\"1, 2, 3\""
    Returns: list[int]
    """
    if value is None:
        return []

    s = str(value).strip()

    # Strip one layer of quotes (CSV)
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1].strip()

    if not s:
        return []

    # Old list-literal
    if s.startswith("[") and s.endswith("]"):
        seq = ast.literal_eval(s)
        return [int(x) for x in seq]

    # New format: comma-separated ints
    parts = [p.strip() for p in s.split(",")]
    out = []
    for p in parts:
        if not p:
            continue
        out.append(int(p))
    return out


# -------------------------
# Helpers
# -------------------------
def safe_int(x, default=None):
    try:
        if x is None:
            return default
        return int(str(x).strip())
    except Exception:
        return default


def safe_float(x, default=None):
    try:
        if x is None:
            return default
        s = str(x).strip()
        if s == "":
            return default
        return float(s)
    except Exception:
        return default


def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)
    return path


def sorted_numeric_unique(values):
    vals = []
    for v in values:
        try:
            vals.append(float(v))
        except Exception:
            continue
    return sorted(set(vals))


# -------------------------
# Core analysis
# -------------------------
def analyze_csv(path: str):
    """
    Reads the new CSV schema you showed:
    Puzzle,Size_KiB,K_Hashes,Mode,Set_Ratio,Set_Limit,Set_Size,Loop_Count,Final_Inserted,Termination,Inserted

    Produces:
      - df_cell: aggregated per (Size, K_Hash, Set_Ratio)
      - df_rows: per-row parsed data (optional for debugging)
      - non_stable_rows: list of non-stabilized row details
    """
    total = 0
    stable_count = 0
    unstable_count = 0

    # breakdown[(size, k_hash, set_ratio)] -> stats
    breakdown = defaultdict(lambda: {
        "stable": 0,
        "unstable": 0,
        "total": 0,
        "sum_loop": 0,
        "sum_final_inserted": 0,
        "sum_set_size": 0,
    })

    non_stable_rows = []
    rows_out = []

    with open(path, "r", newline="", encoding="utf-8", errors="ignore") as f:
        reader = csv.DictReader(f)

        for row in reader:
            if not row:
                continue

            # Required keys
            if "Puzzle" not in row or "Size_KiB" not in row or "K_Hashes" not in row:
                continue

            puzzle = safe_int(row.get("Puzzle"), default=None)
            size = safe_int(row.get("Size_KiB"), default=None)
            k_hash = safe_int(row.get("K_Hashes"), default=None)
            set_ratio = safe_float(row.get("Set_Ratio"), default=0.0)

            if puzzle is None or size is None or k_hash is None or set_ratio is None:
                continue

            seq = parse_inserted(row.get("Inserted", ""))

            # Termination is source of truth if present
            term = (row.get("Termination") or "").strip().lower()
            if term:
                stabilized = (term == "stabilized")
            else:
                stabilized = is_stabilized(seq)

            loop_count = safe_int(row.get("Loop_Count"), default=0)
            final_inserted = safe_int(row.get("Final_Inserted"), default=0)
            set_size = safe_int(row.get("Set_Size"), default=0)

            total += 1
            key = (size, k_hash, set_ratio)
            breakdown[key]["total"] += 1
            breakdown[key]["sum_loop"] += loop_count
            breakdown[key]["sum_final_inserted"] += final_inserted
            breakdown[key]["sum_set_size"] += set_size

            if stabilized:
                stable_count += 1
                breakdown[key]["stable"] += 1
            else:
                unstable_count += 1
                breakdown[key]["unstable"] += 1
                non_stable_rows.append((puzzle, size, k_hash, set_ratio, len(seq), term or "inferred", seq))

            rows_out.append({
                "Puzzle": puzzle,
                "Size": size,
                "K_Hash": k_hash,
                "Set_Ratio": set_ratio,
                "Loop_Count": loop_count,
                "Final_Inserted": final_inserted,
                "Set_Size": set_size,
                "Termination": term if term else ("stabilized" if stabilized else "not_stabilized"),
            })

    # Build aggregated DF
    data = []
    for (size, kh, sr), v in breakdown.items():
        t = v["total"]
        stable = v["stable"]
        rate = (stable / t) * 100 if t > 0 else 0.0
        avg_loop = v["sum_loop"] / t if t > 0 else 0.0
        avg_final_inserted = v["sum_final_inserted"] / t if t > 0 else 0.0
        avg_set_size = v["sum_set_size"] / t if t > 0 else 0.0

        data.append({
            "Size": size,
            "K_Hash": kh,
            "Set_Ratio": sr,
            "Total": t,
            "Stable": stable,
            "Unstable": v["unstable"],
            "Stab_Percent": rate,
            "Avg_Loop": avg_loop,
            "Avg_Final_Inserted": avg_final_inserted,
            "Avg_Set_Size": avg_set_size,
        })

    df_cell = pd.DataFrame(data)
    df_rows = pd.DataFrame(rows_out)

    print("\n========== SUMMARY ==========")
    print(f"Total rows:         {total}")
    print(f"Stabilized:         {stable_count}")
    print(f"Not stabilized:     {unstable_count}")
    if total > 0:
        print(f"Stabilization rate: {stable_count/total:.2%}")

    print("\nAggregated cells:", 0 if df_cell.empty else len(df_cell))
    print("Unique Set_Ratio:", [] if df_cell.empty else sorted_numeric_unique(df_cell["Set_Ratio"].unique()))

    return df_cell, df_rows, non_stable_rows


# -------------------------
# Plotting upgrades
# -------------------------
def pivot_metric(df_cell: pd.DataFrame, metric: str, set_ratio: float):
    sub = df_cell[df_cell["Set_Ratio"] == set_ratio]
    if sub.empty:
        return None
    return sub.pivot(index="Size", columns="K_Hash", values=metric)


def pivot_total(df_cell: pd.DataFrame, set_ratio: float):
    return pivot_metric(df_cell, "Total", set_ratio)


def mask_low_samples(pivot_values: pd.DataFrame, pivot_total: pd.DataFrame, min_samples: int):
    if pivot_values is None or pivot_total is None:
        return None
    masked = pivot_values.copy()
    masked[pivot_total < min_samples] = float("nan")
    return masked


def add_iso_contours(ax, pivot_values: pd.DataFrame, levels):
    """
    Draw contour lines over a heatmap. Assumes pivot_values is a 2D grid with numeric index/columns.
    """
    if pivot_values is None or pivot_values.empty:
        return

    # Need numeric grids
    xs = list(pivot_values.columns)
    ys = list(pivot_values.index)

    # If not numeric, skip
    try:
        xs_f = [float(x) for x in xs]
        ys_f = [float(y) for y in ys]
    except Exception:
        return

    Z = pivot_values.values
    if Z.size == 0:
        return

    # contour expects increasing coords; also needs meshgrid
    import numpy as np
    X, Y = np.meshgrid(xs_f, ys_f)

    # Only contour where finite
    if not np.isfinite(Z).any():
        return

    cs = ax.contour(X, Y, Z, levels=levels, linewidths=1.2)
    ax.clabel(cs, inline=True, fontsize=8, fmt=lambda v: f"{int(v)}%")


def plot_heatmaps_by_set_ratio(df_cell, out_dir, min_samples=1, contour_levels=(50, 80, 95)):
    ensure_dir(out_dir)

    ratios = sorted_numeric_unique(df_cell["Set_Ratio"].unique())
    if not ratios:
        print("No Set_Ratio values found; skipping ratio heatmaps.")
        return

    # Multi-panel stabilization heatmaps (same scale)
    fig, axes = plt.subplots(1, len(ratios), figsize=(5 * len(ratios), 6), sharey=True)
    if len(ratios) == 1:
        axes = [axes]

    for ax, r in zip(axes, ratios):
        pv = pivot_metric(df_cell, "Stab_Percent", r)
        pt = pivot_total(df_cell, r)
        pv = mask_low_samples(pv, pt, min_samples)

        if pv is None:
            ax.set_title(f"Set Ratio = {r} (no data)")
            ax.axis("off")
            continue

        sns.heatmap(
            pv,
            annot=True,
            fmt=".1f",
            cmap="Blues",
            vmin=0,
            vmax=100,
            ax=ax
        )

        title = "No Set (Baseline)" if float(r) == 0.0 else f"Set Ratio = {r}"
        ax.set_title(title)
        ax.set_xlabel("K_Hashes")
        ax.set_ylabel("Size_KiB")

        # Contours
        add_iso_contours(ax, pv, list(contour_levels))

    fig.suptitle(f"Stabilization % Heatmaps by Set Ratio (masked: Total < {min_samples})", fontsize=14)
    plt.tight_layout()
    fig_path = os.path.join(out_dir, "stab_heatmaps_by_set_ratio.png")
    plt.savefig(fig_path, dpi=200)
    plt.show()
    print("Saved:", fig_path)


def plot_delta_heatmaps_vs_baseline(df_cell, out_dir, min_samples=1):
    ensure_dir(out_dir)

    ratios = sorted_numeric_unique(df_cell["Set_Ratio"].unique())
    if 0.0 not in ratios:
        print("Baseline Set_Ratio=0 not found; skipping delta heatmaps.")
        return

    base = df_cell[df_cell["Set_Ratio"] == 0.0].set_index(["Size", "K_Hash"])

    for r in ratios:
        if float(r) == 0.0:
            continue

        cur = df_cell[df_cell["Set_Ratio"] == r].set_index(["Size", "K_Hash"])

        # Align on same index; only compare where both exist
        joined = cur[["Stab_Percent", "Total"]].join(
            base[["Stab_Percent", "Total"]],
            how="inner",
            lsuffix="_cur",
            rsuffix="_base"
        )

        if joined.empty:
            continue

        # Mask if either side has low samples
        joined.loc[(joined["Total_cur"] < min_samples) | (joined["Total_base"] < min_samples), "Stab_Percent_cur"] = math.nan
        joined.loc[(joined["Total_cur"] < min_samples) | (joined["Total_base"] < min_samples), "Stab_Percent_base"] = math.nan

        joined["Delta"] = joined["Stab_Percent_cur"] - joined["Stab_Percent_base"]
        delta = joined["Delta"].reset_index().pivot(index="Size", columns="K_Hash", values="Delta")

        plt.figure(figsize=(7, 6))
        sns.heatmap(delta, annot=True, fmt="+.1f", cmap="RdBu", center=0)
        plt.title(f"Δ Stabilization vs Baseline (Set_Ratio 0 → {r}) (masked < {min_samples})")
        plt.xlabel("K_Hashes")
        plt.ylabel("Size_KiB")
        plt.tight_layout()

        fig_path = os.path.join(out_dir, f"delta_vs_baseline_ratio_{r}.png")
        plt.savefig(fig_path, dpi=200)
        plt.show()
        print("Saved:", fig_path)


def compute_optimal_set_ratio(df_cell, target_percent=95.0, min_samples=1):
    """
    For each (Size, K_Hash):
      - find the smallest Set_Ratio achieving Stab_Percent >= target_percent with Total >= min_samples
      - if none achieves target, pick the Set_Ratio with maximum Stab_Percent (tie-breaker: smaller ratio)
    Returns df_opt with columns:
      Size, K_Hash, Optimal_Set_Ratio, Optimal_Stab_Percent, Achieves_Target (bool)
    """
    df = df_cell.copy()
    df = df[df["Total"] >= min_samples]

    if df.empty:
        return pd.DataFrame(columns=["Size", "K_Hash", "Optimal_Set_Ratio", "Optimal_Stab_Percent", "Achieves_Target"])

    out = []
    for (size, kh), g in df.groupby(["Size", "K_Hash"]):
        g_sorted = g.sort_values("Set_Ratio")

        achievers = g_sorted[g_sorted["Stab_Percent"] >= target_percent]
        if not achievers.empty:
            best = achievers.iloc[0]
            out.append({
                "Size": size,
                "K_Hash": kh,
                "Optimal_Set_Ratio": float(best["Set_Ratio"]),
                "Optimal_Stab_Percent": float(best["Stab_Percent"]),
                "Achieves_Target": True,
            })
        else:
            # pick maximum stabilization; tie-breaker: smallest ratio
            g_best = g_sorted.sort_values(["Stab_Percent", "Set_Ratio"], ascending=[False, True]).iloc[0]
            out.append({
                "Size": size,
                "K_Hash": kh,
                "Optimal_Set_Ratio": float(g_best["Set_Ratio"]),
                "Optimal_Stab_Percent": float(g_best["Stab_Percent"]),
                "Achieves_Target": False,
            })

    return pd.DataFrame(out)


def plot_regime_map(df_opt, out_dir):
    """
    Heatmap of Optimal_Set_Ratio per (Size, K_Hash).
    """
    ensure_dir(out_dir)

    if df_opt.empty:
        print("Optimal ratio DF empty; skipping regime map.")
        return

    pv = df_opt.pivot(index="Size", columns="K_Hash", values="Optimal_Set_Ratio")

    plt.figure(figsize=(10, 6))
    sns.heatmap(pv, annot=True, fmt=".3g", cmap="viridis")
    plt.title("Optimal Set_Ratio Regime Map (per Size, K_Hashes)")
    plt.xlabel("K_Hashes")
    plt.ylabel("Size_KiB")
    plt.tight_layout()

    fig_path = os.path.join(out_dir, "optimal_set_ratio_regime_map.png")
    plt.savefig(fig_path, dpi=200)
    plt.show()
    print("Saved:", fig_path)


def plot_metric_heatmaps_by_ratio(df_cell, out_dir, metric, min_samples=1):
    """
    Extra upgrade: analogous heatmaps for Avg_Loop / Avg_Final_Inserted / Avg_Set_Size per Set_Ratio.
    """
    ensure_dir(out_dir)

    ratios = sorted_numeric_unique(df_cell["Set_Ratio"].unique())
    if not ratios:
        return

    fig, axes = plt.subplots(1, len(ratios), figsize=(5 * len(ratios), 6), sharey=True)
    if len(ratios) == 1:
        axes = [axes]

    for ax, r in zip(axes, ratios):
        pv = pivot_metric(df_cell, metric, r)
        pt = pivot_total(df_cell, r)
        pv = mask_low_samples(pv, pt, min_samples)

        if pv is None:
            ax.set_title(f"{metric} @ {r} (no data)")
            ax.axis("off")
            continue

        sns.heatmap(pv, annot=True, fmt=".2f", cmap="Blues", ax=ax)
        title = "No Set (Baseline)" if float(r) == 0.0 else f"Set Ratio = {r}"
        ax.set_title(title)
        ax.set_xlabel("K_Hashes")
        ax.set_ylabel("Size_KiB")

    fig.suptitle(f"{metric} Heatmaps by Set Ratio (masked: Total < {min_samples})", fontsize=14)
    plt.tight_layout()
    fig_path = os.path.join(out_dir, f"{metric.lower()}_heatmaps_by_set_ratio.png")
    plt.savefig(fig_path, dpi=200)
    plt.show()
    print("Saved:", fig_path)


# -------------------------
# Main entry
# -------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="bloom_stats.csv", help="Path to bloom_stats CSV")
    ap.add_argument("--out", default="plots", help="Output directory for plots + tables")
    ap.add_argument("--min-samples", type=int, default=1, help="Mask cells with Total < min_samples")
    ap.add_argument("--target", type=float, default=95.0, help="Target stabilization percent for optimal ratio selection")
    ap.add_argument("--contours", default="50,80,95", help="Comma-separated contour levels for stabilization heatmaps")
    ap.add_argument("--export-rows", action="store_true", help="Export per-row parsed table")
    args = ap.parse_args()

    out_dir = ensure_dir(args.out)

    # Parse contour levels
    contour_levels = []
    for x in str(args.contours).split(","):
        x = x.strip()
        if not x:
            continue
        try:
            contour_levels.append(float(x))
        except Exception:
            pass
    if not contour_levels:
        contour_levels = [50.0, 80.0, 95.0]

    # Run analysis
    df_cell, df_rows, non_stable_rows = analyze_csv(args.csv)

    if df_cell.empty:
        print("\nNo aggregated data to plot.")
        return

    # Export aggregated table
    agg_path = os.path.join(out_dir, "aggregated_cells.csv")
    df_cell.sort_values(["Set_Ratio", "Size", "K_Hash"]).to_csv(agg_path, index=False)
    print("Saved:", agg_path)

    if args.export_rows:
        rows_path = os.path.join(out_dir, "parsed_rows.csv")
        df_rows.to_csv(rows_path, index=False)
        print("Saved:", rows_path)

    # Print non-stabilized rows (optional)
    print("\n========== NON-STABILIZED ROWS (sample) ==========")
    for item in non_stable_rows[:20]:
        puzzle, size, kh, sr, seq_len, src, seq = item
        print(f"Puzzle={puzzle}, Size={size}, k={kh}, Set_Ratio={sr}, Len={seq_len}, Source={src}")

    # ---- Upgraded plots ----
    plot_heatmaps_by_set_ratio(df_cell, out_dir, min_samples=args.min_samples, contour_levels=contour_levels)
    plot_delta_heatmaps_vs_baseline(df_cell, out_dir, min_samples=args.min_samples)

    # Additional upgraded metric heatmaps (optional but useful)
    plot_metric_heatmaps_by_ratio(df_cell, out_dir, metric="Avg_Loop", min_samples=args.min_samples)
    plot_metric_heatmaps_by_ratio(df_cell, out_dir, metric="Avg_Final_Inserted", min_samples=args.min_samples)
    plot_metric_heatmaps_by_ratio(df_cell, out_dir, metric="Avg_Set_Size", min_samples=args.min_samples)

    # Optimal Set_Ratio regime
    df_opt = compute_optimal_set_ratio(df_cell, target_percent=args.target, min_samples=args.min_samples)
    opt_path = os.path.join(out_dir, "optimal_set_ratio_table.csv")
    df_opt.sort_values(["Size", "K_Hash"]).to_csv(opt_path, index=False)
    print("Saved:", opt_path)

    plot_regime_map(df_opt, out_dir)

    print("\nDone.")


if __name__ == "__main__":
    main()
