#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-rubik-fixed-merge
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/rubik_fixed_merge_%j.out
#SBATCH --error=results/logs/rubik_fixed_merge_%j.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"

python3 scripts/merge_split_results.py --mode rubik-fixed
