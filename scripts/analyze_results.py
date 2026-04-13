#!/usr/bin/env python3
"""
Analyze benchmark CSVs produced by the BiHS-Bloom driver and display
comparison plots between IDA* and BiHS-Bloom.

Usage:
    python analyze_results.py                          # auto-detect available CSVs
    python analyze_results.py --stp   benchmark_stp_korf100.csv
    python analyze_results.py --pancake benchmark_pancake16_100.csv
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import pandas as pd


# ── helpers ──────────────────────────────────────────────────────────────────

def speedup(base, target):
    """Element-wise speedup: how many times faster is target vs base."""
    return base / target


def summary_table(df, label, time_cols):
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")
    print(df[time_cols].describe().to_string())
    for col in time_cols:
        total = df[col].sum()
        print(f"  Total {col}: {total:.4f}s")


# ── STP plots ─────────────────────────────────────────────────────────────────

def plot_stp(path):
    df = pd.read_csv(path)
    required = {"instance", "solution_length", "ida_time", "rev_ida_time", "bihs_bloom_time"}
    if not required.issubset(df.columns):
        sys.exit(f"[ERROR] {path} is missing columns. Found: {list(df.columns)}")

    time_cols = ["ida_time", "rev_ida_time", "bihs_bloom_time"]
    summary_table(df, "Sliding Tile Puzzle — Korf 100", time_cols)

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle("STP Korf-100: IDA* vs BiHS-Bloom", fontsize=15, fontweight="bold")

    x = df["instance"]

    # ── per-instance solve times ──────────────────────────────────────────
    ax = axes[0, 0]
    ax.plot(x, df["ida_time"],       label="IDA*",         alpha=0.8, linewidth=1.2)
    ax.plot(x, df["rev_ida_time"],   label="Reverse IDA*", alpha=0.8, linewidth=1.2, linestyle="--")
    ax.plot(x, df["bihs_bloom_time"],label="BiHS-Bloom",   alpha=0.8, linewidth=1.2)
    ax.set_title("Solve Time per Instance")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # ── speedup of BiHS-Bloom over IDA* ──────────────────────────────────
    ax = axes[0, 1]
    su = speedup(df["ida_time"], df["bihs_bloom_time"])
    colors = ["green" if v >= 1 else "red" for v in su]
    ax.bar(x, su, color=colors, alpha=0.7, width=0.8)
    ax.axhline(1, color="black", linewidth=1, linestyle="--")
    ax.set_title("Speedup: BiHS-Bloom vs IDA*\n(>1 = BiHS-Bloom faster)")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Speedup factor")
    ax.grid(True, axis="y", alpha=0.3)

    # ── cumulative time ───────────────────────────────────────────────────
    ax = axes[1, 0]
    ax.plot(x, df["ida_time"].cumsum(),       label="IDA*",         linewidth=1.5)
    ax.plot(x, df["rev_ida_time"].cumsum(),   label="Reverse IDA*", linewidth=1.5, linestyle="--")
    ax.plot(x, df["bihs_bloom_time"].cumsum(),label="BiHS-Bloom",   linewidth=1.5)
    ax.set_title("Cumulative Solve Time")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Cumulative Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # ── time vs solution length scatter ──────────────────────────────────
    ax = axes[1, 1]
    ax.scatter(df["solution_length"], df["ida_time"],        label="IDA*",       alpha=0.6, s=25)
    ax.scatter(df["solution_length"], df["bihs_bloom_time"], label="BiHS-Bloom", alpha=0.6, s=25, marker="^")
    ax.set_title("Solve Time vs Solution Length")
    ax.set_xlabel("Solution Length (moves)")
    ax.set_ylabel("Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = "plot_stp_korf100.png"
    plt.savefig(out, dpi=150)
    print(f"\nSTP plot saved to {out}")
    plt.show()


# ── Pancake plots ─────────────────────────────────────────────────────────────

def plot_pancake(path):
    df = pd.read_csv(path)
    required = {"instance", "solution_length", "ida_time", "bihs_bloom_time"}
    if not required.issubset(df.columns):
        sys.exit(f"[ERROR] {path} is missing columns. Found: {list(df.columns)}")

    time_cols = ["ida_time", "bihs_bloom_time"]
    summary_table(df, "Pancake-16 — 100 instances", time_cols)

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle("Pancake-16: IDA* vs BiHS-Bloom", fontsize=15, fontweight="bold")

    x = df["instance"]

    # ── per-instance solve times ──────────────────────────────────────────
    ax = axes[0, 0]
    ax.plot(x, df["ida_time"],        label="IDA*",       alpha=0.8, linewidth=1.2)
    ax.plot(x, df["bihs_bloom_time"], label="BiHS-Bloom", alpha=0.8, linewidth=1.2)
    ax.set_title("Solve Time per Instance")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # ── speedup ───────────────────────────────────────────────────────────
    ax = axes[0, 1]
    su = speedup(df["ida_time"], df["bihs_bloom_time"])
    colors = ["green" if v >= 1 else "red" for v in su]
    ax.bar(x, su, color=colors, alpha=0.7, width=0.8)
    ax.axhline(1, color="black", linewidth=1, linestyle="--")
    ax.set_title("Speedup: BiHS-Bloom vs IDA*\n(>1 = BiHS-Bloom faster)")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Speedup factor")
    ax.grid(True, axis="y", alpha=0.3)

    # ── cumulative time ───────────────────────────────────────────────────
    ax = axes[1, 0]
    ax.plot(x, df["ida_time"].cumsum(),        label="IDA*",       linewidth=1.5)
    ax.plot(x, df["bihs_bloom_time"].cumsum(), label="BiHS-Bloom", linewidth=1.5)
    ax.set_title("Cumulative Solve Time")
    ax.set_xlabel("Instance #")
    ax.set_ylabel("Cumulative Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # ── time vs solution length scatter ──────────────────────────────────
    ax = axes[1, 1]
    ax.scatter(df["solution_length"], df["ida_time"],        label="IDA*",       alpha=0.6, s=25)
    ax.scatter(df["solution_length"], df["bihs_bloom_time"], label="BiHS-Bloom", alpha=0.6, s=25, marker="^")
    ax.set_title("Solve Time vs Solution Length")
    ax.set_xlabel("Solution Length (moves)")
    ax.set_ylabel("Time (s)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = "plot_pancake16_100.png"
    plt.savefig(out, dpi=150)
    print(f"\nPancake plot saved to {out}")
    plt.show()


# ── entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Analyze BiHS-Bloom benchmark results")
    parser.add_argument("--stp",     metavar="CSV", help="Path to STP results CSV")
    parser.add_argument("--pancake", metavar="CSV", help="Path to Pancake results CSV")
    args = parser.parse_args()

    # Auto-detect if no flags given
    if not args.stp and not args.pancake:
        defaults = {
            "stp":     "benchmark_stp_korf100.csv",
            "pancake": "benchmark_pancake16_100.csv",
        }
        for key, fname in defaults.items():
            if os.path.exists(fname):
                setattr(args, key, fname)
                print(f"Auto-detected: {fname}")

    if not args.stp and not args.pancake:
        sys.exit("No CSV files found. Run the solver first, or pass --stp / --pancake.")

    if args.stp:
        plot_stp(args.stp)
    if args.pancake:
        plot_pancake(args.pancake)


if __name__ == "__main__":
    main()
