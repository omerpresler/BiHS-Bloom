import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Set the style for the plots
sns.set_theme(style="whitegrid")

def analyze_bloom_stats(file_path):
    # Load the dataset
    try:
        df = pd.read_csv(file_path)
    except FileNotFoundError:
        print(f"Error: The file '{file_path}' was not found.")
        return

    # 1. Basic Summary Statistics
    # Calculates mean, std, min, max, and quartiles for the time columns
    stats = df[['A_Star_Time', 'Bloom_Time']].describe()
    print("--- Summary Statistics ---")
    print(stats)
    print("\n")

    # 2. Performance Comparison
    mean_a_star = df['A_Star_Time'].mean()
    mean_bloom = df['Bloom_Time'].mean()
    
    # Calculate how many times one is faster than the other
    if mean_a_star < mean_bloom:
        ratio = mean_bloom / mean_a_star
        print(f"On average, A* is {ratio:.2f}x faster than the Bloom approach.")
    else:
        ratio = mean_a_star / mean_bloom
        print(f"On average, the Bloom approach is {ratio:.2f}x faster than A*.")

    # 3. Visualization
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    # Plot 1: Distribution of Execution Times (KDE Plot)
    sns.kdeplot(df['A_Star_Time'], ax=axes[0], fill=True, label='$A^{*}$ Time')
    sns.kdeplot(df['Bloom_Time'], ax=axes[0], fill=True, label='Bloom Time')
    axes[0].set_title('Distribution of Execution Times')
    axes[0].set_xlabel('Time (seconds)')
    axes[0].set_ylabel('Density')
    axes[0].legend()

    # Plot 2: Scatter Plot Comparison
    # A point above the red dashed line means A* was faster for that puzzle
    axes[1].scatter(df['A_Star_Time'], df['Bloom_Time'], alpha=0.5, s=10)
    max_val = max(df['A_Star_Time'].max(), df['Bloom_Time'].max())
    axes[1].plot([0, max_val], [0, max_val], color='red', linestyle='--', label='y = x (Equal Speed)')
    axes[1].set_title('$A^{*}$ Time vs. Bloom Time')
    axes[1].set_xlabel('$A^{*}$ Time (s)')
    axes[1].set_ylabel('Bloom Time (s)')
    axes[1].legend()

    plt.tight_layout()
    plt.savefig('bloom_analysis_plots.png')
    print("\nAnalysis complete. Visualization saved as 'bloom_analysis_plots.png'.")
    plt.show()

if __name__ == "__main__":
    analyze_bloom_stats('bloom_stats.csv')