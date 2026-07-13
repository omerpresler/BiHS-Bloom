#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=rubik-pdb
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/rubik_pdb_%j.out
#SBATCH --error=results/logs/rubik_pdb_%j.err

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/logs "${RUBIK_PDB_DIR:-results/pdb/rubik}"

binary="${BIHS_BINARY:-slurm/bin/stp_bihs_bloom}"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary." >&2
  exit 1
fi

if [[ "$binary" == *src/bin/release/stp_bihs_bloom ]]; then
  if ! command -v conda >/dev/null 2>&1; then
    echo "Error: conda is required for $binary but is not available on PATH." >&2
    exit 1
  fi
  eval "$(conda shell.bash hook)"
  conda activate "${CONDA_ENV_NAME:-bihs-build}"
fi

srun "$binary" --rubik --prepare-rubik-pdbs \
  --rubik-pdb-dir "${RUBIK_PDB_DIR:-results/pdb/rubik}"
