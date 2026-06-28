#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-stp-run
#SBATCH --array=0-15%8
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=128G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/stp_run_%A_%a.out
#SBATCH --error=results/logs/stp_run_%A_%a.err

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/split/run_parts results/split/convergence_parts results/logs

binary="slurm/bin/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Package and commit it from a Linux build machine." >&2
  exit 1
fi

manifest="results/split/manifests/run.csv"
row=$(awk -F, -v id="$SLURM_ARRAY_TASK_ID" 'NR > 1 && $1 == id { print; exit }' "$manifest")
if [[ -z "$row" ]]; then
  echo "No phase-2 manifest row for task $SLURM_ARRAY_TASK_ID; skipping."
  exit 0
fi

IFS=, read -r job_id domain instance phase algorithm ratio k_mode <<< "$row"
srun "$binary" "--${domain}" --phase run \
  --instance "$instance" \
  --algorithm "$algorithm" \
  --ratio "$ratio" \
  --k-mode "$k_mode" \
  --params-input "results/split/params/${domain}_params.csv" \
  --benchmark-output "results/split/run_parts/result_${domain}_${job_id}.csv" \
  --convergence-output "results/split/convergence_parts/convergence_${domain}_${job_id}.csv"
