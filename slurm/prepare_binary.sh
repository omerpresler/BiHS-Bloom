#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

bash scripts/conda_compile.sh

binary="src/bin/release/stp_bihs_bloom"
if [[ ! -x "$binary" ]]; then
  echo "Build did not produce $binary" >&2
  exit 1
fi

mkdir -p slurm/bin
cp "$binary" slurm/bin/stp_bihs_bloom
chmod 755 slurm/bin/stp_bihs_bloom

echo "Packaged Linux binary at slurm/bin/stp_bihs_bloom"
echo "Commit it with: git add slurm/bin/stp_bihs_bloom"
