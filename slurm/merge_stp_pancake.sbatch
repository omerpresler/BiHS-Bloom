#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-stp-pancake-merge
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=results/logs/stp_pancake_merge_%j.out
#SBATCH --error=results/logs/stp_pancake_merge_%j.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=END,FAIL

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

python3 slurm/merge_stp_pancake.py
