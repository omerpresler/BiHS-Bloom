from numpy._core.shape_base import block
import re
import ast
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # needed for 3D projection, even if unused directly

CSV_PATH = "./bloom_stats.csv"

# Optional: base name for outputs
OUTPUT_MEAN   = "bloom_mean_inserted_length.png"
OUTPUT_FREQ   = "bloom_inserted_length_frequency.png"
OUTPUT_RATIO  = "bloom_frontier_to_bloom_ratio.png"

# ---------- Load CSV safely ----------
rows = []
with open(CSV_PATH, "r") as f:
    header = f.readline().strip()

    for line in f:
        line = line.strip()
        if not line:
            continue

        parts = line.split(",", 3)  # split first 3 commas only
        if len(parts) != 4:
            print("Skipping malformed line:", line)
            continue

        puzzle_str, size_str, hash_str, inserted_str = parts
        try:
            puzzle = int(puzzle_str)
            size_kib = int(size_str)
            k_hash = int(hash_str)
        except ValueError:
            print("Skipping malformed numeric row:", line)
            continue

        rows.append({
            "Puzzle": puzzle,
            "Size_KiB": size_kib,
            "K_Hashes": k_hash,
            "Inserted": inserted_str
        })

df = pd.DataFrame(rows)

# ---------- Parse list values ----------
def parse_list(text):
    text = text.strip()
    text = re.sub(r",\s*]", "]", text)  # fix trailing comma inside brackets

    try:
        parsed = ast.literal_eval(text)
        if isinstance(parsed, list):
            return parsed
    except Exception:
        pass

    inner = text.strip("[]")
    return [t for t in re.split(r"[,\s]+", inner) if t]

df["Inserted_List"] = df["Inserted"].apply(parse_list)
df["Inserted_Length"] = df["Inserted_List"].apply(len)

# First value in each Inserted_List = frontier size
def get_frontier_size(lst):
    if not lst:
        return None
    try:
        return int(lst[0])
    except (ValueError, TypeError):
        return None

df["Frontier_Size"] = df["Inserted_List"].apply(get_frontier_size)

# ---------- Frontier size in KiB / Bloom KiB ----------
INTS_PER_STATE = 16
BYTES_PER_INT = 4
BYTES_PER_STATE = INTS_PER_STATE * BYTES_PER_INT  # 64
KIB_PER_STATE = BYTES_PER_STATE / 1024.0          # 0.0625 KiB

df["Frontier_KiB"] = df["Frontier_Size"] * KIB_PER_STATE
df["Frontier_to_Bloom_Ratio"] = df["Frontier_KiB"] / df["Size_KiB"]

# Aggregate: average ratio per number of iterations
ratio_by_iter = (
    df.dropna(subset=["Frontier_to_Bloom_Ratio"])
      .groupby("Inserted_Length")["Frontier_to_Bloom_Ratio"]
      .mean()
      .sort_index()
)

# ---------- Aggregations for existing plots ----------
mean_values = df.groupby("Size_KiB")["Inserted_Length"].mean()

# Only keep lengths that occur > freq_limit
freq_limit = 25
freq = df["Inserted_Length"].value_counts().sort_index()
freq = freq[freq > freq_limit]

# ---------- PLOT 1: mean inserted length per Bloom size ----------
plt.figure(figsize=(8, 6))
plt.bar(mean_values.index.astype(str), mean_values.values)
plt.title("Average Inserted Length per Bloom Filter Size (KiB)")
plt.xlabel("Bloom Filter Size (KiB)")
plt.ylabel("Average Iterations")
plt.grid(axis="y", linestyle="--", alpha=0.5)
plt.tight_layout()
plt.savefig(OUTPUT_MEAN, dpi=300)
print(f"[OK] Saved mean plot to: {OUTPUT_MEAN}")
plt.show(block=False)

# ---------- PLOT 2: frequency of inserted lengths ----------
plt.figure(figsize=(8, 6))
bars = plt.bar(freq.index.astype(str), freq.values)
plt.title(f"Frequency of Inserted List Sizes (Frequency > {freq_limit})")
plt.xlabel("Number of Iterations")
plt.ylabel("Occurrences")
plt.grid(axis="y", linestyle="--", alpha=0.5)

# Add numeric labels above bars
for bar in bars:
    height = bar.get_height()
    plt.annotate(
        f"{int(height)}",
        xy=(bar.get_x() + bar.get_width() / 2, height),
        xytext=(0, 4),
        textcoords="offset points",
        ha="center", va="bottom"
    )

plt.tight_layout()
plt.savefig(OUTPUT_FREQ, dpi=300)
print(f"[OK] Saved frequency plot to: {OUTPUT_FREQ}")
plt.show(block=False)

# ---------- PLOT 3: frontier KiB / Bloom KiB vs number of iterations ----------
plt.figure(figsize=(8, 6))
plt.plot(ratio_by_iter.index, ratio_by_iter.values)
plt.title("Frontier Memory / Bloom Size vs Number of Iterations")
plt.xlabel("Number of Iterations (Inserted_Length)")
plt.ylabel("Frontier KiB / Bloom KiB")
plt.grid(True, linestyle="--", alpha=0.5)
plt.tight_layout()
plt.savefig(OUTPUT_RATIO, dpi=300)
print(f"[OK] Saved ratio plot to: {OUTPUT_RATIO}")
plt.show()
plt.close()

# ---------- Print each puzzle ID once where it had only 1 iteration ----------
one_iter_unique = df[df["Inserted_Length"] == 1].drop_duplicates(subset=["Puzzle"])

if one_iter_unique.empty:
    print("No puzzles had only 1 iteration.")
else:
    print("\nPuzzles (unique) with only 1 iteration:")
    print(one_iter_unique[["Puzzle", "Size_KiB", "Inserted_Length"]].to_string(index=False))

# ---------- Puzzles that didn't manage to stop (Inserted_Length == 101) ----------
STOP_LIMIT = 202  # your "didn't stop" threshold

df_failed = df[df["Inserted_Length"] == STOP_LIMIT]

if df_failed.empty:
    print(f"No puzzles reached Inserted_Length == {STOP_LIMIT}.")
else:
    # 1) Basic counts
    total_rows = len(df_failed)
    unique_puzzles = df_failed["Puzzle"].nunique()
    print(f"\n[FAILED] Total rows with Inserted_Length == {STOP_LIMIT}: {total_rows}")
    print(f"[FAILED] Unique puzzles that didn't stop: {unique_puzzles}")

    # 2) List all puzzle IDs that didn't stop
    print("\nPuzzle IDs that didn't manage to stop:")
    print(sorted(df_failed["Puzzle"].unique()))

    # 3) How many of them are from each Bloom filter size (KiB)
    print("\nCount of FAILED rows per Bloom filter size (KiB):")
    print(df_failed["Size_KiB"].value_counts().sort_index())

    print("\nCount of UNIQUE puzzles per Bloom filter size (KiB):")
    print(df_failed.groupby("Size_KiB")["Puzzle"].nunique())

    # 4) How many k-hash values were used (and how often)
    print("\nCount of FAILED rows per K_Hashes:")
    print(df_failed["K_Hashes"].value_counts().sort_index())

    print("\nCount of UNIQUE puzzles per (Size_KiB, K_Hashes):")
    print(
        df_failed
        .groupby(["Size_KiB", "K_Hashes"])["Puzzle"]
        .nunique()
        .sort_index()
    )
