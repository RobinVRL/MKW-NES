#!/bin/bash
# Compile the given port sources (default: all) and print the first errors of each.
source "$(dirname "$0")/env.sh"
PORTFLAGS="-std=gnu++20 -O0 $ARCH $DEFS $INCS -I$PORT/c3d -I$PORT/ctru/src -fno-strict-aliasing -Wall -Wno-unused-function"
files="$@"; [ -z "$files" ] && files="$(ls "$PORT"/ctru/src/*.cpp "$PORT"/c3d/*.cpp)"
for f in $files; do
  out=$(${CROSS}g++ $PORTFLAGS -fsyntax-only "$f" 2>&1 | grep -E 'error|warning' | head -${N:-12})
  [ -n "$out" ] && { echo "== $(basename "$f")"; echo "$out" | sed "s#$ROOT/##g"; }
done
true
