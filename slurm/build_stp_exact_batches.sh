#!/usr/bin/env bash
set -euo pipefail
module load anaconda/anaconda
source "$(conda info --base)/etc/profile.d/conda.sh"
conda activate bihs-build

case "${1:-}" in
    "") root=results/stp_exact_batches_20261120 ;;
    --half-bloom) root=results/stp_half_bloom_20261120 ;;
    *) echo "Usage: $0 [--half-bloom]" >&2; exit 2 ;;
esac
source_dir=paper/stp_bihs_bloom/aem
mkdir -p "$root/bin"
for target in tests comparison; do
    name="${root##*/}_${target}"
    if [[ "$target" == tests ]]; then
        source_file=AEMTests.cpp
    else
        source_file=AEMDriver.cpp
    fi
    make -j2 -C src/build/SFML/paper/stp_bihs_bloom \
        -f Makefile -f Makefile.stp-exact-batches OPENGL=STUB \
        PILOT_NAME="$name" PILOT_SOURCE="$source_dir/$source_file" \
        CXX='g++ -O2 -DPAPER_BUCKET_BASELINE' stp-exact-batches
    install -m755 "src/bin/release/$name" "$root/bin/$target"
    if [[ "$target" == tests ]]; then "$root/bin/tests"; fi
done
sha256sum "$root"/bin/*
file "$root"/bin/*
