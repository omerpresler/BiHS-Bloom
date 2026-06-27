#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-rubik-fixed
#SBATCH --array=0-1%2
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=160G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/rubik_fixed_%A_%a.out
#SBATCH --error=results/logs/rubik_fixed_%A_%a.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/split/run_parts results/split/convergence_parts results/logs

binary="slurm/bin/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Package and commit it from a Linux build machine." >&2
  exit 1
fi

manifest="results/split/manifests/rubik_fixed.csv"
row=$(awk -F, -v id="$SLURM_ARRAY_TASK_ID" 'NR > 1 && $1 == id { print; exit }' "$manifest")
if [[ -z "$row" ]]; then
  echo "No fixed Rubik manifest row for task $SLURM_ARRAY_TASK_ID; skipping."
  exit 0
fi

IFS=, read -r job_id domain instance phase algorithm ratio <<< "$row"
srun "$binary" "--${domain}" --phase "$phase" \
  --instance "$instance" \
  --algorithm "$algorithm" \
  --ratio "$ratio" \
  --benchmark-output "results/split/run_parts/result_rubik_fixed_${job_id}.csv" \
  --convergence-output "results/split/convergence_parts/convergence_rubik_fixed_${job_id}.csv"
