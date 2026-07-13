#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mkdir -p results/logs results/rubik/parts

pdb_job="$(sbatch --parsable --export=ALL,BIHS_BINARY="$ROOT/src/bin/release/stp_bihs_bloom" slurm/prepare_rubik_pdbs.sbatch)"
job_id="$(sbatch --parsable --dependency="afterok:${pdb_job}" slurm/rubik.sbatch)"
echo "Submitted Rubik PDB preparation job: $pdb_job"
echo "Submitted Rubik job: $job_id (starts after PDB preparation)"
