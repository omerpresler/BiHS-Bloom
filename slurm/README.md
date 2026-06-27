# Running the split benchmark on Slurm

This workflow is currently configured as a small smoke test: instance `0` only.
It supports both `stp` and `rubik`. It runs calibration first, derives the
memory/frontier parameters, and then runs each BiHS-Bloom and IDTHSwTrans ratio
as a separate Slurm job.

## 1. Compile locally

On a Linux machine or WSL, from the repository root:

```bash
bash slurm/prepare_binary.sh
slurm/bin/stp_bihs_bloom --stp --phase calibrate --instance 0 --algorithm astar \
  --benchmark-output /tmp/calibration_0.csv

slurm/bin/stp_bihs_bloom --rubik --phase calibrate --instance 0 --algorithm astar \
  --benchmark-output /tmp/rubik_calibration_0.csv
```

The packaged file must be a Linux executable, not a Windows `.exe`.

## 2. Submit the one-instance split workflow

From the repository root on the Slurm login node:

```bash
bash slurm/submit.sh
```

STP is the default. For Rubik, run:

```bash
DOMAIN=rubik bash slurm/submit.sh
```

The dependency chain is:

```text
calibration array -> params merge -> phase-2 manifest -> phase-2 array -> final merge
```

The default calibration manifest contains three jobs for instance `0`:

- `astar`
- `rev_astar`
- `mm`

If calibration does not produce usable `min_memory_items` and `frontier_items`,
the params row is marked `missing_params` and phase 2 receives no work for that
instance.

## 3. Outputs

- `results/split/manifests/`: calibration and phase-2 job manifests
- `results/split/calibration_parts/`: one calibration CSV per calibration job
- `results/split/params/stp_params.csv` or `results/split/params/rubik_params.csv`: merged params/status rows
- `results/split/run_parts/`: one result CSV per phase-2 job
- `results/split/convergence_parts/`: BiHS-Bloom convergence CSVs
- `results/merged/benchmark_stp_korf100.csv`: legacy-compatible benchmark CSV
- `results/merged/bloom_convergence.csv`: merged convergence CSV
- `results/merged/benchmark_rubik_random14_100.csv`: Rubik benchmark CSV
- `results/merged/bloom_convergence_rubik_random14.csv`: Rubik convergence CSV

## 4. Local smoke test

After building `src/bin/release/stp_bihs_bloom`, run:

```bash
bash scripts/run_split_stp_local.sh
```

For Rubik:

```bash
DOMAIN=rubik bash scripts/run_split_stp_local.sh
```

To widen the local calibration range later:

```bash
INSTANCE_START=0 INSTANCE_END=10 bash scripts/run_split_stp_local.sh
DOMAIN=rubik INSTANCE_START=0 INSTANCE_END=10 bash scripts/run_split_stp_local.sh
```

The Slurm array sizes are fixed for the one-instance trial:

- `slurm/stp_calibration.sbatch`: `#SBATCH --array=0-2%3`
- `slurm/stp_run.sbatch`: `#SBATCH --array=0-7%4`

When scaling past instance `0`, regenerate the manifests and expand those array
ranges accordingly.

## 5. Reports

The final merge preserves the existing report inputs:

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
