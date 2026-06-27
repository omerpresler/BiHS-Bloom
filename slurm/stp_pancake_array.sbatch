#!/bin/bash
#SBATCH --partition=main
#SBATCH --job-name=bihs-stp-pancake
#SBATCH --array=0-199%10
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=64G
#SBATCH --time=1-00:00:00
#SBATCH --output=results/logs/stp_pancake_%A_%a.out
#SBATCH --error=results/logs/stp_pancake_%A_%a.err
#SBATCH --mail-user=omerpr@post.bgu.ac.il
#SBATCH --mail-type=FAIL

set -euo pipefail

ROOT="${SLURM_SUBMIT_DIR:-$PWD}"
cd "$ROOT"
mkdir -p results/stp_pancake/parts results/logs

binary="slurm/bin/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Missing executable $binary. Package and commit it from a Linux build machine." >&2
  exit 1
fi

task="${SLURM_ARRAY_TASK_ID}"
if [[ "$task" -lt 100 ]]; then
  domain="stp"
  instance="$task"
  domain_flag="--stp"
else
  domain="pancake"
  instance="$((task - 100))"
  domain_flag="--pancake"
fi

end="$((instance + 1))"
extra_args=()
if [[ "${FULL_BASELINES:-0}" == "1" ]]; then
  extra_args+=(--full)
fi

srun "$binary" "$domain_flag" "${extra_args[@]}" \
  --instance-start "$instance" --instance-end "$end" \
  --benchmark-output "results/stp_pancake/parts/benchmark_${domain}_${instance}.csv" \
  --convergence-output "results/stp_pancake/parts/convergence_${domain}_${instance}.csv"
