#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-rubik
#SBATCH --array=0-9%10
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
# A* and MM on Korf Rubik instances can exceed 64 GiB even with the PDB.
# Match the fixed workflow's allocation so the per-instance sequence has room.
#SBATCH --mem=160G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/rubik_%A_%a.out
#SBATCH --error=results/logs/rubik_%A_%a.err

set -euo pipefail

echo "SLURM_JOB_ID=${SLURM_JOB_ID}"
echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST}"

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/logs results/rubik/parts

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

instance="${SLURM_ARRAY_TASK_ID}"
end="$((instance + 1))"

srun "$binary" --rubik \
  --rubik-pdb-dir "${RUBIK_PDB_DIR:-results/pdb/rubik}" \
  --instance-start "$instance" --instance-end "$end" \
  --benchmark-output "results/rubik/parts/benchmark_${instance}.csv" \
  --convergence-output "results/rubik/parts/convergence_${instance}.csv"
