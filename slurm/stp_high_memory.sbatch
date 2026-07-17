#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-stp-highmem
#SBATCH --array=59,81,87%3
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=128G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/stp_highmem_%A_%a.out
#SBATCH --error=results/logs/stp_highmem_%A_%a.err

set -euo pipefail

echo "SLURM_JOB_ID=${SLURM_JOB_ID}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST}"

module load anaconda/anaconda
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate bihs-build

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
