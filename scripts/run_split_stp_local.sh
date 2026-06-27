#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

binary="${BIHS_BINARY:-src/bin/release/stp_bihs_bloom}"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Build it first." >&2
  exit 1
fi

mkdir -p results/split/manifests results/split/calibration_parts \
  results/split/params results/split/run_parts results/split/convergence_parts results/merged

python3 scripts/generate_split_manifest.py \
  --phase calibrate \
  --instance-start "${INSTANCE_START:-0}" \
  --instance-end "${INSTANCE_END:-1}" \
  --output results/split/manifests/calibration.csv

tail -n +2 results/split/manifests/calibration.csv | while IFS=, read -r job_id domain instance phase algorithm ratio; do
  "$binary" --stp --phase calibrate --instance "$instance" --algorithm "$algorithm" \
    --benchmark-output "results/split/calibration_parts/calibration_${job_id}.csv"
done

python3 scripts/merge_split_results.py --mode params

python3 scripts/generate_split_manifest.py \
  --phase run \
  --params results/split/params/stp_params.csv \
  --output results/split/manifests/run.csv

tail -n +2 results/split/manifests/run.csv | while IFS=, read -r job_id domain instance phase algorithm ratio; do
  "$binary" --stp --phase run --instance "$instance" --algorithm "$algorithm" --ratio "$ratio" \
    --params-input results/split/params/stp_params.csv \
    --benchmark-output "results/split/run_parts/result_${job_id}.csv" \
    --convergence-output "results/split/convergence_parts/convergence_${job_id}.csv"
done

python3 scripts/merge_split_results.py --mode final
