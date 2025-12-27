import argparse
import ast
import csv
from collections import defaultdict
import matplotlib

matplotlib.use("Agg")

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns   # if seaborn is not installed: `pip install seaborn`

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
    return len(seq) <= MAX_LEN or has_alternating_tail(seq)


def load_rows(path):
    rows = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        has_mode = "Mode" in reader.fieldnames
        for row in reader:
            seq = ast.literal_eval(row["Inserted"])
            stabilized = is_stabilized(seq)
            data = {
                "Puzzle": int(row["Puzzle"]),
                "Size_KiB": int(row["Size_KiB"]),
                "K_Hashes": int(row["K_Hashes"]),
                "Inserted_List": seq,
                "Stabilized": stabilized,
            }
            if has_mode:
                data.update(
                    {
                        "Mode": row["Mode"],
                        "Set_Ratio": float(row["Set_Ratio"]),
                        "Set_Limit": int(row["Set_Limit"]),
                        "Set_Size": int(row["Set_Size"]),
                        "Loop_Count": int(row["Loop_Count"]),
                        "Final_Inserted": int(row["Final_Inserted"]),
                        "Termination": row["Termination"],
                    }
                )
            else:
                data.update(
                    {
                        "Mode": "unknown",
                        "Set_Ratio": 0.0,
                        "Set_Limit": 0,
                        "Set_Size": 0,
                        "Loop_Count": len(seq),
                        "Final_Inserted": seq[-1] if seq else 0,
                        "Termination": "unknown",
                    }
                )
            rows.append(data)
    return rows


def summarize(rows):
    total = len(rows)
    stable_count = sum(1 for row in rows if row["Stabilized"])
    unstable_count = total - stable_count

    print("\n========== SUMMARY ==========")
    print(f"Total rows:       {total}")
    print(f"Stabilized:       {stable_count}")
    print(f"Not stabilized:   {unstable_count}")
    if total > 0:
        print(f"Stabilization rate: {stable_count/total:.2%}")

    breakdown = defaultdict(lambda: {"stable": 0, "unstable": 0})
    for row in rows:
        key = (row["Size_KiB"], row["K_Hashes"])
        if row["Stabilized"]:
            breakdown[key]["stable"] += 1
        else:
            breakdown[key]["unstable"] += 1

    data = []
    for (size, kh), value in breakdown.items():
        total_count = value["stable"] + value["unstable"]
        rate = (value["stable"] / total_count) * 100 if total_count > 0 else 0
        data.append(
            {
                "Size": size,
                "K_Hash": kh,
                "Total": total_count,
                "Stable": value["stable"],
                "Stab_Percent": rate,
            }
        )

    df = pd.DataFrame(data)
    print("\nGenerated DataFrame:")
    print(df)

    return df


def summarize_set_limits(rows):
    df = pd.DataFrame(rows)
    grouped = (
        df.groupby(["Mode", "Set_Limit"], as_index=False)["Stabilized"]
        .agg(Total="size", Stable="sum")
        .assign(Stab_Percent=lambda frame: frame["Stable"] / frame["Total"] * 100)
    )

    print("\n========== SET LIMIT STABILIZATION ==========")
    print(grouped.sort_values(["Mode", "Set_Limit"]))
    return grouped


def visualize_set_limits(grouped, output_path):
    plt.figure(figsize=(10, 6))
    sns.barplot(data=grouped, x="Set_Limit", y="Stab_Percent", hue="Mode")
    plt.title("Stabilization Rate by Set Limit")
    plt.xlabel("Set Limit")
    plt.ylabel("Stabilization Rate (%)")
    plt.grid(True, axis="y", linestyle="--", alpha=0.4)
    plt.tight_layout()
    plt.savefig(output_path, dpi=150)
    plt.close()


def check_file(path, output_path):
    rows = load_rows(path)
    summarize(rows)
    grouped = summarize_set_limits(rows)
    visualize_set_limits(grouped, output_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analyze Bloom loop stabilization and set limit effects."
    )
    parser.add_argument(
        "csv_path",
        nargs="?",
        default="scripts/bloom_stats.csv",
        help="Path to bloom stats CSV (default: scripts/bloom_stats.csv)",
    )
    parser.add_argument(
        "--output",
        default="scripts/set_limit_stabilization.png",
        help="Path to save the stabilization plot.",
    )
    args = parser.parse_args()
    check_file(args.csv_path, args.output)
