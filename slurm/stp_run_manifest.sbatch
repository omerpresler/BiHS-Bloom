#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-run-manifest
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/run_manifest_%j.out
#SBATCH --error=results/logs/run_manifest_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
domain="${DOMAIN:-stp}"
python3 scripts/generate_split_manifest.py \
  --phase run \
  --domain "$domain" \
  --params "results/split/params/${domain}_params.csv" \
  --output results/split/manifests/run.csv
