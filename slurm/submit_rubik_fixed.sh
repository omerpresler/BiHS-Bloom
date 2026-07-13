#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p results/split/manifests results/split/run_parts results/split/convergence_parts \
  results/logs results/merged

if [[ ! -x slurm/bin/stp_bihs_bloom ]]; then
  echo "Missing executable slurm/bin/stp_bihs_bloom." >&2
  echo "Build and commit it locally with: bash slurm/prepare_binary.sh" >&2
  exit 1
fi

python3 scripts/generate_split_manifest.py \
  --phase rubik-fixed \
  --domain rubik \
  --instance-start "${INSTANCE_START:-0}" \
  --instance-end "${INSTANCE_END:-1}" \
  --output results/split/manifests/rubik_fixed.csv

row_count=$(($(wc -l < results/split/manifests/rubik_fixed.csv) - 1))
if [[ "$row_count" -le 0 ]]; then
  echo "No fixed Rubik jobs generated." >&2
  exit 1
fi

pdb_job=$(sbatch --parsable slurm/prepare_rubik_pdbs.sbatch)
run_job=$(sbatch --parsable --dependency="afterok:${pdb_job}" --array="0-$((row_count - 1))%${MAX_PARALLEL:-2}" slurm/rubik_fixed.sbatch)
merge_job=$(sbatch --parsable --dependency="afterany:${run_job}" slurm/merge_rubik_fixed.sbatch)

echo "Domain: rubik"
echo "Mode: fixed 128GiB, k=1, no calibration"
echo "Manifest rows: ${row_count}"
echo "Submitted Rubik PDB preparation job: ${pdb_job}"
echo "Submitted fixed Rubik array job: ${run_job}"
echo "Submitted fixed Rubik merge job: ${merge_job} (runs after fixed jobs finish)"
