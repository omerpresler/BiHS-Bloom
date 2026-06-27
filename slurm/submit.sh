#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p results/split/manifests results/split/calibration_parts results/split/params \
  results/split/run_parts results/split/convergence_parts results/logs results/merged

if [[ ! -x slurm/bin/stp_bihs_bloom ]]; then
  echo "Missing executable slurm/bin/stp_bihs_bloom." >&2
  echo "Build and commit it locally with: bash slurm/prepare_binary.sh" >&2
  exit 1
fi

python3 scripts/generate_split_manifest.py \
  --phase calibrate \
  --instance-start "${INSTANCE_START:-0}" \
  --instance-end "${INSTANCE_END:-1}" \
  --output results/split/manifests/calibration.csv

calibration_job=$(sbatch --parsable slurm/stp_calibration.sbatch)
params_job=$(sbatch --parsable --dependency="afterok:${calibration_job}" slurm/merge_params.sbatch)
manifest_job=$(sbatch --parsable --dependency="afterok:${params_job}" slurm/stp_run_manifest.sbatch)
run_job=$(sbatch --parsable --dependency="afterok:${manifest_job}" slurm/stp_run.sbatch)
merge_job=$(sbatch --parsable --dependency="afterany:${run_job}" slurm/merge.sbatch)

echo "Submitted calibration array job: ${calibration_job}"
echo "Submitted params merge job: ${params_job}"
echo "Submitted run manifest job: ${manifest_job}"
echo "Submitted phase-2 array job: ${run_job}"
echo "Submitted final merge job: ${merge_job} (runs after phase 2 finishes)"
