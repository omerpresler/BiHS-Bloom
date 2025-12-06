#!/bin/bash

# Validate arguments
if [ $# -ne 2 ]; then
    echo "ERROR: Incorrect usage."
    echo "Usage: $0 <depth> <num_states>"
    echo "Example: $0 20 100"
    exit 1
fi

DEPTH=$1
COUNT=$2
OUTPUT_FILE="STP_distance_${DEPTH}"

# Validate arguments are integers
if ! [[ $DEPTH =~ ^[0-9]+$ ]]; then
    echo "ERROR: depth must be a number, received: $DEPTH"
    exit 2
fi

if ! [[ $COUNT =~ ^[0-9]+$ ]]; then
    echo "ERROR: num_states must be a number, received: $COUNT"
    exit 3
fi

CMD="../src/bin/release/stp_bihs_bloom --generate -d $DEPTH -n $COUNT -f $OUTPUT_FILE"

echo "Running:"
echo "$CMD"
echo

$CMD

