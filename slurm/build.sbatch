#!/bin/bash
#SBATCH --job-name=bihs-build
#SBATCH --cpus-per-task=10
#SBATCH --mem=16G
#SBATCH --time=01:00:00
#SBATCH --output=results/logs/build_%j.out
#SBATCH --error=results/logs/build_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"
mkdir -p results/logs

if [[ -n "${BIHS_MODULES:-}" ]]; then
  module load ${BIHS_MODULES}
fi

make -C src/build/SFML OPENGL=STUB -j "${SLURM_CPUS_PER_TASK}"
