#!/bin/sh

BOUND=$1
FILE=$2
BASE=$(basename "$FILE" .aig)

LOG="../rs_hack_logs/${BASE}.log"

./camical/camical --restorecompact=1 --condition=0 --stats=2 "$BOUND" "$FILE" \
	> "$LOG"

