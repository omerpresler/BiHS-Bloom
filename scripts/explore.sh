#!/bin/bash

# Validate arguments, vebose is optional
if [ $# -ne 2 ]; then
    echo "ERROR: Incorrect usage."
    echo "Usage: $0 <depth> <puzzle ID>"
    echo "Example: $0 20 18"
    exit 1
fi

DEPTH=$1
PUZZLE_ID=$2

# Validate arguments are integers
if ! [[ $DEPTH =~ ^[0-9]+$ ]]; then
    echo "ERROR: depth must be a number, received: $DEPTH"
    exit 2
fi

if ! [[ $PUZZLE_ID =~ ^[0-9]+$ ]]; then
    echo "ERROR: puzzle ID must be a number, received: $PUZZLE_ID"
    exit 2
fi

OUTPUT_FILE="STP_distance_$DEPTH"

CMD="../src/bin/release/stp_bihs_bloom --explore -d $DEPTH -p $PUZZLE_ID -f $OUTPUT_FILE"

$CMD --verbose