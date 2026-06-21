# Running the STP benchmark on Slurm

The workflow builds the Linux binary on the cluster, runs one Korf instance per
Slurm array task, and merges the task CSVs after every task succeeds.

## 1. Compile locally

On Linux or WSL, from the repository root:

```bash
bash scripts/compile.sh
src/bin/release/stp_bihs_bloom --stp --instance-start 0 --instance-end 1 \
  --benchmark-output /tmp/benchmark_0.csv \
  --convergence-output /tmp/convergence_0.csv
```

The cluster build remains necessary when the local machine is Windows or has a
different Linux ABI. `slurm/build.sbatch` performs that build automatically.

## 2. Transfer through GitHub

Commit and push the source and `slurm/` files locally. On a Slurm login node:

```bash
git clone <your-repository-url> BiHS-Bloom
cd BiHS-Bloom
```

For later runs, use `git pull` in that clone. Build products and `results/` are
ignored by Git; retrieve results with `scp`, `rsync`, or your cluster's file UI.

## 3. Configure and submit

Edit the `#SBATCH` resource lines in the three `.sbatch` files for your cluster.
In particular, check memory, time, partition/account/QoS, and array concurrency
(`%10` in `stp_array.sbatch`). If compiler modules are required:

```bash
export BIHS_MODULES="gcc/13.2.0"
```

Submit the complete dependency chain:

```bash
bash slurm/submit.sh
```

The command prints the build, array, and merge job IDs. Useful monitoring commands:

```bash
squeue -u "$USER"
sacct -j <job-id> --format=JobID,State,Elapsed,MaxRSS,ExitCode
```

Outputs are placed in:

- `results/logs/`: Slurm stdout/stderr
- `results/parts/`: one benchmark and convergence CSV per instance
- `results/merged/`: final CSVs consumed by the reporting scripts

To rerun only selected instances, submit an array override such as:

```bash
sbatch --array=4,17,59 slurm/stp_array.sbatch
```

Then rerun `python3 slurm/merge_results.py`; duplicate instances are replaced
deterministically during merging.

## 4. Generate reports

Run these from the repository root, either on the cluster (if matplotlib/pandas
are available) or after copying `results/merged/` back locally:

```bash
python3 scripts/generate_runtime_report.py \
  --input results/merged/benchmark_stp_korf100.csv \
  --output-dir results/reports/runtime_assets \
  --output-html results/reports/runtime_report.html

python3 scripts/plot_convergence.py \
  --input results/merged/bloom_convergence.csv \
  --benchmark results/merged/benchmark_stp_korf100.csv \
  --output-dir results/reports/convergence
```
