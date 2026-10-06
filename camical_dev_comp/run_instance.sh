#!/bin/sh

BOUND=$1
FILE=$2
BASE=$(basename "$FILE" .aig)

LOG="../base_logs/${BASE}.log"

./camical/camical --condition=0 --stats=2 "$BOUND" "$FILE" \
	> "$LOG"

