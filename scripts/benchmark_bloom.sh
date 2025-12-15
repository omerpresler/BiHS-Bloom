#!/bin/bash

# Validate arguments
if [ $# -ne 1 ]; then
    echo "ERROR: Incorrect usage."
    echo "Usage: $0 <depth>"
    echo "Example: $0 20"
    exit 1
fi

DEPTH=$1

# Validate arguments are integers
if ! [[ $DEPTH =~ ^[0-9]+$ ]]; then
    echo "ERROR: depth must be a number, received: $DEPTH"
    exit 2
fi

OUTPUT_FILE="STP_distance_$DEPTH"

CMD="../src/bin/release/stp_bihs_bloom --benchmark -d $DEPTH -f $OUTPUT_FILE"

$CMD