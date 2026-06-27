#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-stp-cal
#SBATCH --array=0-2%3
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=08:00:00
#SBATCH --output=results/logs/stp_cal_%A_%a.out
#SBATCH --error=results/logs/stp_cal_%A_%a.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/split/calibration_parts results/logs

binary="slurm/bin/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Package and commit it from a Linux build machine." >&2
  exit 1
fi

manifest="results/split/manifests/calibration.csv"
row=$(awk -F, -v id="$SLURM_ARRAY_TASK_ID" 'NR > 1 && $1 == id { print; exit }' "$manifest")
if [[ -z "$row" ]]; then
  echo "No calibration manifest row for task $SLURM_ARRAY_TASK_ID" >&2
  exit 1
fi

IFS=, read -r job_id domain instance phase algorithm ratio <<< "$row"
srun "$binary" --stp --phase calibrate \
  --instance "$instance" \
  --algorithm "$algorithm" \
  --benchmark-output "results/split/calibration_parts/calibration_${job_id}.csv"
