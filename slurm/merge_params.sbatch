#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-params
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/params_%j.out
#SBATCH --error=results/logs/params_%j.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
domain="${DOMAIN:-stp}"
python3 scripts/merge_split_results.py \
  --mode params \
  --domain "$domain" \
  --params "results/split/params/${domain}_params.csv"
