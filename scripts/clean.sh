#!/bin/bash

# Change the working directory to the build directory from the directory of the script
cd "$(dirname "$0")/../src/build/SFML" || exit 1

make clean
