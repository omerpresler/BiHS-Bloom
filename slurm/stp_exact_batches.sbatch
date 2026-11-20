#!/usr/bin/env bash
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --output=logs/stp_exact_batches_20261120/%x_%A_%a.out
#SBATCH --error=logs/stp_exact_batches_20261120/%x_%A_%a.err
set -euo pipefail
cd "${SLURM_SUBMIT_DIR}"
srun --ntasks=1 --cpus-per-task=1 --cpu-bind=cores python3 \
    scripts/run_stp_exact_batches.py "${SLURM_ARRAY_TASK_ID:?}" "$@"
