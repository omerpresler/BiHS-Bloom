import pandas as pd
import matplotlib.pyplot as plt

def plot_times_by_sample(
    file_path: str,
    cols=("A_Star_Time", "IDAStar_Time", "Bloom_Time"),
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

if __name__ == "__main__":
    # Simple (loads whole file):
    # plot_times_by_sample("bloom_stats.csv", chunksize=None)

    # Huge file (stream in chunks):
    plot_times_by_sample("bloom_stats.csv", chunksize=200_000, max_points=200_000)
