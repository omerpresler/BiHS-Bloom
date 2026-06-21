#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/parts results/logs

build_job=$(sbatch --parsable slurm/build.sbatch)
array_job=$(sbatch --parsable --dependency="afterok:${build_job}" slurm/stp_array.sbatch)
merge_job=$(sbatch --parsable --dependency="afterok:${array_job}" slurm/merge.sbatch)

echo "Submitted build job: ${build_job}"
echo "Submitted array job: ${array_job} (runs after the build succeeds)"
echo "Submitted merge job: ${merge_job} (runs after the array succeeds)"
