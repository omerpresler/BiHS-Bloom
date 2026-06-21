# Running the STP benchmark on Slurm

The workflow uses a Linux binary compiled before it is pushed to Git, runs one
Korf instance per Slurm array task, and merges the task CSVs after every task succeeds.

## 1. Compile locally

On a Linux machine or WSL, from the repository root:

```bash
bash slurm/prepare_binary.sh
slurm/bin/stp_bihs_bloom --stp --instance-start 0 --instance-end 1 \
  --benchmark-output /tmp/benchmark_0.csv \
  --convergence-output /tmp/convergence_0.csv
```

The packaged file must be a Linux executable, not a Windows `.exe`. Build on the
same CPU architecture as the cluster. For glibc compatibility, build on the same
Linux distribution as the cluster or on an older compatible distribution.

## 2. Transfer through GitHub

Commit the source and packaged binary, then push them:

```bash
git add src scripts slurm .gitattributes .gitignore
git commit -m "Add prebuilt Slurm benchmark workflow"
git push
```

On a Slurm login node:

```bash
git clone <your-repository-url> BiHS-Bloom
cd BiHS-Bloom
```

For later runs, use `git pull` in that clone. General build products remain
ignored; `slurm/bin/stp_bihs_bloom` is the one tracked binary. Retrieve results
with `scp`, `rsync`, or your cluster's file UI.

## 3. Configure and submit

Edit the `#SBATCH` resource lines in the two `.sbatch` files for your cluster.
In particular, check memory, time, partition/account/QoS, and array concurrency
(`%10` in `stp_array.sbatch`). No compiler or compiler module is needed.

Submit the complete dependency chain:

```bash
bash slurm/submit.sh
```

The command prints the array and merge job IDs. Useful monitoring commands:

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
