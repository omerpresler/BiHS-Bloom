#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-run-manifest
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:10:00
#SBATCH --output=results/logs/run_manifest_%j.out
#SBATCH --error=results/logs/run_manifest_%j.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
python3 scripts/generate_split_manifest.py \
  --phase run \
  --params results/split/params/stp_params.csv \
  --output results/split/manifests/run.csv
