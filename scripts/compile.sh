#!/bin/bash

# Change the working directory to the build directory from the directory of the script
cd "$(dirname "$0")/../src/build/SFML" || exit 1

# Constants
J=10  # Number of parallel jobs for 'make -j'

# Compile only the headless release artifacts used by Slurm. The default target
# also builds debug binaries and tests, which are unnecessary for packaging and
# can fail independently on older cluster toolchains.
make OPENGL=STUB release -j "$J"
