#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mkdir -p results/logs results/rubik/parts

job_id="$(sbatch --parsable slurm/rubik.sbatch)"
echo "Submitted Rubik job: $job_id"
