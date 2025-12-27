import ast
from collections import defaultdict
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


def check_file(path):
    total = 0
    stable_count = 0
    unstable_count = 0

    breakdown = defaultdict(lambda: {"stable": 0, "unstable": 0})
    non_stable_rows = []

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split(",", 3)
            if len(parts) != 4:
                continue

            puzzle_str, size_str, hash_str, list_str = parts

            if not puzzle_str.isdigit():  # skip header
                continue

            puzzle = int(puzzle_str)
            size = int(size_str)
            k_hash = int(hash_str)
            seq = ast.literal_eval(list_str)

            total += 1
            stabilized = is_stabilized(seq)

            if stabilized:
                stable_count += 1
                breakdown[(size, k_hash)]["stable"] += 1
            else:
                unstable_count += 1
                breakdown[(size, k_hash)]["unstable"] += 1
                non_stable_rows.append((puzzle, size, k_hash, seq))

    # ---- SUMMARY ----
    print("\n========== SUMMARY ==========")
    print(f"Total rows:       {total}")
    print(f"Stabilized:       {stable_count}")
    print(f"Not stabilized:   {unstable_count}")
    if total > 0:
        print(f"Stabilization rate: {stable_count/total:.2%}")

    # ---- Build DataFrame for visualization ----
    data = []
    for (size, kh), v in breakdown.items():
        t = v["stable"] + v["unstable"]
        rate = (v["stable"] / t) * 100 if t > 0 else 0
        data.append({"Size": size, "K_Hash": kh, "Total": t, "Stable": v["stable"], "Stab_Percent": rate})

    df = pd.DataFrame(data)
    print("\nGenerated DataFrame:")
    print(df)

    print("\n========== NON-STABILIZED ROWS ==========")

    if not non_stable_rows:
        print("All rows stabilized.")
    else:
        print("\nNon-stabilized rows:")
        for puzzle, size, k, seq in non_stable_rows:
            print(f"Puzzle={puzzle}, Size={size}, k={k}, Length={len(seq)}, Values={seq}")

    # ---- VISUALIZATIONS ----

    # 1) Heatmap pivot: Size vs K-hash stabilization %
    pivot = df.pivot(index="Size", columns="K_Hash", values="Stab_Percent")

    plt.figure(figsize=(10, 6))
    sns.heatmap(pivot, annot=True, fmt=".1f", cmap="Blues")
    plt.title("Stabilization % Heatmap (Size vs K-Hash)")
    plt.xlabel("k-hash")
    plt.ylabel("Bloom Size")
    plt.tight_layout()
    plt.show()

    # 2) Scatter plot of Size/K vs stabilization ratio
    # Sort by size so the line is ordered
    df_sorted = df.sort_values("Size")

    plt.figure(figsize=(8, 5))
    plt.plot(df_sorted["Size"], df_sorted["Stab_Percent"], linewidth=2)
    plt.title("Stabilization % by Bloom Size")
    plt.xlabel("Bloom Size (KiB)")
    plt.ylabel("Stabilization %")
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    check_file("bloom_with_set_stats.csv")
