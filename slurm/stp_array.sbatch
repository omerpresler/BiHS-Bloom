#!/bin/bash
#SBATCH --job-name=bihs-stp
#SBATCH --array=0-99%10
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/stp_%A_%a.out
#SBATCH --error=results/logs/stp_%A_%a.er

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/parts results/logs

binary="slurm/bin/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Package and commit it from a Linux build machine." >&2
  exit 1
fi

start="${SLURM_ARRAY_TASK_ID}"
end="$((start + 1))"
srun "$binary" --stp \
  --instance-start "$start" --instance-end "$end" \
  --benchmark-output "results/parts/benchmark_${start}.csv" \
  --convergence-output "results/parts/convergence_${start}.csv"
