#!/bin/bash

# Validate arguments, vebose is optional
if [ $# -ne 1 ]; then
    echo "ERROR: Incorrect usage."
    echo "Usage: $0 <depth> <verbose?>"
    echo "Example: $0 20 true"
    exit 1
fi

DEPTH=$1
VERBOSE=false

if [ $# -eq 2 ]; then
    VERBOSE=$2
fi

# Validate arguments are integers
if ! [[ $DEPTH =~ ^[0-9]+$ ]]; then
    echo "ERROR: depth must be a number, received: $DEPTH"
    exit 2
fi

OUTPUT_FILE="STP_distance_$DEPTH"

CMD="../src/bin/release/stp_bihs_bloom --benchmark -d $DEPTH -f $OUTPUT_FILE"

if [ "$VERBOSE" = "true" ]; then
    $CMD --verbose
else
    $CMD
fi