#!/bin/bash
# Build build/padbridge for the NES Mini (run in WSL).
set -e
source "$(dirname "$0")/../env.sh"
# Static, so it doesn't depend on the console's old glibc (2.23).
${CROSS}gcc -O2 $ARCH -Wall -static -o "$ROOT/build/padbridge" "$PORT/nesmini/padbridge.c"
${CROSS}strip "$ROOT/build/padbridge"
ls -l "$ROOT/build/padbridge"
