#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-merge
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:15:00
#SBATCH --output=results/logs/merge_%j.out
#SBATCH --error=results/logs/merge_%j.e
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=END,FAIL

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
domain="${DOMAIN:-stp}"
python3 scripts/merge_split_results.py \
  --mode final \
  --domain "$domain" \
  --params "results/split/params/${domain}_params.csv"
