#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/parts results/logs

if [[ ! -x slurm/bin/stp_bihs_bloom ]]; then
  echo "Missing executable slurm/bin/stp_bihs_bloom." >&2
  echo "Build and commit it locally with: bash slurm/prepare_binary.sh" >&2
  exit 1
fi

array_job=$(sbatch --parsable slurm/stp_array.sbatch)
merge_job=$(sbatch --parsable --dependency="afterok:${array_job}" slurm/merge.sbatch)

echo "Submitted array job: ${array_job}"
echo "Submitted merge job: ${merge_job} (runs after the array succeeds)"
