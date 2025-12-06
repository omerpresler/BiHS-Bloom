import re
import ast
import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# CONFIG
# ============================================================

CSV_PATH = "./bloom_stats.csv"
OUTPUT_GRAPH = "bloom_time_series_filtered_200plus.png"

INTS_PER_STATE = 16
BYTES_PER_INT = 4
BYTES_PER_STATE = INTS_PER_STATE * BYTES_PER_INT
KIB_PER_STATE = BYTES_PER_STATE / 1024.0


# ============================================================
# 1. Load CSV
# ============================================================

rows = []
with open(CSV_PATH, "r") as f:
    header = f.readline().strip()

    for line in f:
        line = line.strip()
        if not line:
            continue

        parts = line.split(",", 3)
        if len(parts) != 4:
            continue

        try:
            puzzle = int(parts[0])
            size_kib = int(parts[1])
            k_hash = int(parts[2])
        except ValueError:
            continue

        rows.append({
            "Puzzle": puzzle,
            "Size_KiB": size_kib,
            "K_Hashes": k_hash,
            "Inserted": parts[3]
        })

df = pd.DataFrame(rows)
print(f"[INFO] Loaded {len(df)} rows")


# ============================================================
# 2. Parse Inserted array
# ============================================================

def parse_list(text):
    text = text.strip()
    text = re.sub(r",\s*]", "]", text)
    try:
        parsed = ast.literal_eval(text)
        if isinstance(parsed, list):
            return parsed
    except:
        pass

    stripped = text.strip("[]")
    return [x for x in re.split(r"[,\s]+", stripped) if x]


df["Inserted_List"] = df["Inserted"].apply(parse_list)
df["Inserted_Length"] = df["Inserted_List"].apply(len)


# Filter: only keep runs with >=200 items
df = df[df["Inserted_Length"] >= 200].reset_index(drop=True)
df = df[df["Inserted_List"].apply(lambda lst: int(lst[-1]) > 10)].reset_index(drop=True)

print(f"[INFO] Keeping only runs with Inserted_Length >= 200 → {len(df)} remaining")


# ============================================================
# 3. Build time-series
# ============================================================

records = []

for _, row in df.iterrows():
    puzzle = row["Puzzle"]
    size_kib = row["Size_KiB"]
    k_hash = row["K_Hashes"]

    for iteration, value in enumerate(row["Inserted_List"]):
        try:
            frontier_size = int(value)
        except:
            continue

        frontier_kib = frontier_size * KIB_PER_STATE
        ratio = frontier_kib / size_kib if size_kib > 0 else None

        records.append({
            "Puzzle": puzzle,
            "Size_KiB": size_kib,
            "K_Hashes": k_hash,
            "Iteration": iteration,
            "Ratio": ratio,
            "Frontier_KiB": frontier_kib,
            "Frontier_Size": frontier_size,
        })

time_df = pd.DataFrame(records)
print(f"[INFO] Expanded to {len(time_df)} datapoints")


# ============================================================
# 4. Plot unified graph
# ============================================================

if time_df.empty:
    print("[ERROR] No data meets the filtering criteria. Nothing to plot.")
    exit(0)

plt.figure(figsize=(12, 7))

group_cols = ["Puzzle", "Size_KiB", "K_Hashes"]

for (puz, size, k), sub in time_df.groupby(group_cols):
    sub = sub.sort_values("Iteration")
    label = f"P{puz}|{size}KiB|k={k}"
    plt.plot(sub["Iteration"], sub["Frontier_Size"], alpha=0.6, linewidth=1.4, label=label)

print(f"[INFO] Plotted {time_df.groupby(group_cols).ngroups} runs")


# ============================================================
# 5. Final formatting
# ============================================================

plt.title("Bloom Frontier/Bloom Ratio Over Time (Only Runs With ≥ 200 Iterations)")
plt.xlabel("Iteration Index")
plt.ylabel("Items Inserted")
plt.grid(True, linestyle="--", alpha=0.4)

# Only show legend if small enough
if time_df.groupby(group_cols).ngroups <= 25:
    plt.legend(fontsize=8, ncol=2)

plt.tight_layout()
plt.savefig(OUTPUT_GRAPH, dpi=300)
plt.show()
plt.close()

print(f"[DONE] Saved: {OUTPUT_GRAPH}")
