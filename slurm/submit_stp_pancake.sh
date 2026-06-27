#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p results/stp_pancake/parts results/logs results/merged

if [[ ! -x slurm/bin/stp_bihs_bloom ]]; then
  echo "Missing executable slurm/bin/stp_bihs_bloom." >&2
  echo "Build and commit it locally with: bash slurm/prepare_binary.sh" >&2
  exit 1
fi

array_limit="${MAX_PARALLEL:-10}"
export FULL_BASELINES="${FULL_BASELINES:-0}"
run_job=$(sbatch --parsable --array="0-199%${array_limit}" slurm/stp_pancake_array.sbatch)
merge_job=$(sbatch --parsable --dependency="afterany:${run_job}" slurm/merge_stp_pancake.sbatch)

echo "Submitted combined STP+Pancake array job: ${run_job}"
echo "Submitted merge job: ${merge_job} (runs after array finishes)"
echo "Array: 0-99 STP, 100-199 Pancake"
echo "Full baselines: ${FULL_BASELINES}"
