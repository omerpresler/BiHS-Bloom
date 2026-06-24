#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-rubik
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=64G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/rubik_%j.out
#SBATCH --error=results/logs/rubik_%j.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail

echo "SLURM_JOB_ID=${SLURM_JOB_ID}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST}"

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/logs results/rubik

if ! command -v conda >/dev/null 2>&1; then
  echo "Error: conda is not available on PATH." >&2
  exit 1
fi

# Slurm starts a non-interactive shell, so initialize Conda explicitly.
eval "$(conda shell.bash hook)"
conda activate "${CONDA_ENV_NAME:-bihs-build}"

binary="$ROOT/src/bin/release/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Run: bash scripts/conda_compile.sh" >&2
  exit 1
fi

# The Rubik benchmark currently processes all 10 Korf instances in one run.
# Run from its result directory because its output filenames are fixed.
cd results/rubik
srun "$binary" --rubik
