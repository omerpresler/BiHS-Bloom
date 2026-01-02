import pandas as pd
import matplotlib.pyplot as plt

def plot_times_by_sample(
    file_path: str,
    cols=("A_Star_Time", "IDAStar_Time", "Bloom_Time", "New_BiHS_Time"),
    chunksize: int | None = None,
    max_points: int | None = 200_000,  # cap for plotting; set None to plot all
    output_png: str = "time_by_sample.png",
):
    """
    X axis: sample number (row order in the CSV, starting at 1)
    Y axis: time values from cols
    Supports chunked reading for huge files.
    """

    def downsample(df, max_n):
        if max_n is None or len(df) <= max_n:
            return df
        step = max(1, len(df) // max_n)
        return df.iloc[::step].copy()

    if chunksize is None:
        df = pd.read_csv(file_path, usecols=list(cols))
        df = df.reset_index(drop=True)
        df = downsample(df, max_points)
        x = df.index + 1

        plt.figure(figsize=(14, 6))
        for c in cols:
            plt.plot(x, df[c], linewidth=1, label=c)
    else:
        # Chunked mode: read and plot incrementally
        plt.figure(figsize=(14, 6))
        offset = 0
        buffered = []

        for chunk in pd.read_csv(file_path, usecols=list(cols), chunksize=chunksize):
            chunk = chunk.reset_index(drop=True)

            # build global sample index for this chunk
            x = (chunk.index + 1 + offset)

            buffered.append((x, chunk.copy()))
            offset += len(chunk)

            # Optional: limit memory while still plotting big files
            # If max_points is set, keep only ~max_points total by downsampling buffer.
            if max_points is not None and offset > max_points * 5:
                # merge buffer, downsample, and keep as one buffer
                merged = pd.concat([b[1] for b in buffered], ignore_index=True)
                merged = downsample(merged, max_points)
                new_x = merged.index + 1
                buffered = [(new_x, merged)]

        # final plot from buffered data (downsampled if needed)
        merged = pd.concat([b[1] for b in buffered], ignore_index=True)
        merged = downsample(merged, max_points)
        x = merged.index + 1

        for c in cols:
            plt.plot(x, merged[c], linewidth=1, label=c)

    plt.xlabel("Sample Number (row order)")
    plt.ylabel("Time (seconds)")
    plt.title("Execution Time per Sample")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(output_png, dpi=200)
    plt.show()

    print(f"Saved: {output_png}")

def print_statistics(file_path: str):
    print(f"--- Statistics for {file_path} ---")
    try:
        df = pd.read_csv(file_path)
    except Exception as e:
        print(f"Error reading CSV for statistics: {e}")
        return

    required_cols = ["A_Star_Time", "IDAStar_Time", "Bloom_Time", "New_BiHS_Time"]
    for col in required_cols:
        if col not in df.columns:
            print(f"Missing column '{col}' in CSV. Cannot compute full statistics.")
            return

    # Calculate averages
    avg_a_star = df["A_Star_Time"].mean()
    avg_ida = df["IDAStar_Time"].mean()
    avg_bloom = df["Bloom_Time"].mean()
    avg_new_bihs = df["New_BiHS_Time"].mean()

    print(f"Average A* Time:       {avg_a_star:.6f} s")
    print(f"Average IDA* Time:     {avg_ida:.6f} s")
    print(f"Average Old BiHS Time: {avg_bloom:.6f} s")
    print(f"Average New BiHS Time: {avg_new_bihs:.6f} s")
    print("-" * 30)

    # Compare Old vs New BiHS
    if avg_bloom > 0:
        improvement_pct = ((avg_bloom - avg_new_bihs) / avg_bloom) * 100
        print(f"New BiHS is {improvement_pct:.2f}% faster than Old BiHS on average.")
    else:
        print("Old BiHS average time is 0, cannot calculate percentage improvement.")

    # Compare New BiHS vs IDA*
    if avg_new_bihs > 0:
        if avg_ida >= avg_new_bihs:
            speedup_ida = avg_ida / avg_new_bihs
            print(f"New BiHS is {speedup_ida:.2f}x faster than IDA*.")
        else:
            slowdown_ida = avg_new_bihs / avg_ida
            print(f"New BiHS is {slowdown_ida:.2f}x slower than IDA*.")
    else:
         print("New BiHS average time is 0, cannot calculate IDA* speedup.")

    # Compare New BiHS vs A*
    if avg_a_star > 0 and avg_new_bihs > 0:
        if avg_a_star >= avg_new_bihs:
            speedup_astar = avg_a_star / avg_new_bihs
            print(f"New BiHS is {speedup_astar:.2f}x faster than A*.")
        else:
            slowdown_astar = avg_new_bihs / avg_a_star
            print(f"New BiHS is {slowdown_astar:.2f}x slower than A*.")
    elif avg_a_star == 0:
        print("A* average time is 0, cannot calculate speedup/slowdown.")
    elif avg_new_bihs == 0:
        print("New BiHS average time is 0, cannot calculate speedup/slowdown.")
    print("-" * 30)

if __name__ == "__main__":
    # Simple (loads whole file):
    # plot_times_by_sample("bloom_stats.csv", chunksize=None)

    # Huge file (stream in chunks):
    plot_times_by_sample("bloom_stats.csv", chunksize=200_000, max_points=200_000)
    
    # Specific comparison: Old vs New
    plot_times_by_sample(
        "bloom_stats.csv", 
        cols=("Bloom_Time", "New_BiHS_Time"), 
        chunksize=200_000, 
        max_points=200_000, 
        output_png="comparison_old_vs_new.png"
    )

    print_statistics("bloom_stats.csv")
