#!/bin/bash
#SBATCH --job-name=bihs-merge
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=results/logs/merge_%j.out
#SBATCH --error=results/logs/merge_%j.e

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
python3 slurm/merge_results.py
