#!/usr/bin/env bash
set -euo pipefail

# Build using only tools supplied by Conda. No compiler module or system GCC is
# required. Override these values when needed, for example:
#   CONDA_ENV_NAME=my-build JOBS=16 bash scripts/conda_compile.sh
CONDA_ENV_NAME="${CONDA_ENV_NAME:-bihs-build}"
JOBS="${JOBS:-${SLURM_CPUS_PER_TASK:-10}}"

if ! command -v conda >/dev/null 2>&1; then
  echo "Error: conda is not available on PATH." >&2
  exit 1
fi

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: JOBS must be a positive integer (received: $JOBS)." >&2
  exit 1
fi

# Load Conda into this non-interactive shell. This is needed in Slurm jobs,
# where .bashrc is commonly not sourced.
eval "$(conda shell.bash hook)"

if conda env list | awk -v name="$CONDA_ENV_NAME" '$1 == name { found=1 } END { exit !found }'; then
  echo "Updating Conda build environment: $CONDA_ENV_NAME"
  conda install --name "$CONDA_ENV_NAME" --yes --channel conda-forge \
    make cxx-compiler
else
  echo "Creating Conda build environment: $CONDA_ENV_NAME"
  conda create --name "$CONDA_ENV_NAME" --yes --channel conda-forge \
    make cxx-compiler
fi

conda activate "$CONDA_ENV_NAME"

# The project Makefiles assign g++/gcc directly. Command-line assignments take
# precedence and ensure the Conda compiler wrappers are used.
: "${CXX:?Conda did not configure a C++ compiler}"
: "${CC:?Conda did not configure a C compiler}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../src/build/SFML"

echo "Building release binary with CXX=$CXX, CC=$CC, jobs=$JOBS"
make --directory "$BUILD_DIR" release OPENGL=STUB -j "$JOBS" \
  CXX="$CXX" CC="$CC"

echo "Build complete: $SCRIPT_DIR/../src/bin/release/stp_bihs_bloom"
