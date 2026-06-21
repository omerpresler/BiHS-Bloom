#!/bin/bash
#SBATCH --job-name=bihs-stp
#SBATCH --array=0-99%10
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/stp_%A_%a.out
#SBATCH --error=results/logs/stp_%A_%a.err

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/parts results/logs

# Customize this in your cluster shell before sbatch if a compiler module is needed:
#   export BIHS_MODULES="gcc/13.2.0"
if [[ -n "${BIHS_MODULES:-}" ]]; then
  module load ${BIHS_MODULES}
fi

start="${SLURM_ARRAY_TASK_ID}"
end="$((start + 1))"
srun src/bin/release/stp_bihs_bloom --stp \
  --instance-start "$start" --instance-end "$end" \
  --benchmark-output "results/parts/benchmark_${start}.csv" \
  --convergence-output "results/parts/convergence_${start}.csv"
