#!/bin/bash
# Link the probe objects and list unresolved symbols (the shim's work list).
source "$(dirname "$0")/env.sh"
cd "$ROOT/build/probe"
${CROSS}gcc -std=gnu11 -O0 $ARCH $DEFS $INCS -w -c "$SRC/vendor/cfl-mii/source/cfl_mii.c" -o cfl_mii.c.o 2> cfl.log || head -20 cfl.log
for m in "$SRC"/vendor/citro3d/source/maths/*.c; do ${CROSS}gcc -std=gnu11 -O0 $ARCH $DEFS $INCS -w -c "$m" -o "maths_$(basename "$m").o"; done
${CROSS}g++ $ARCH *.o $EXTRA -o /tmp/mkw.elf -lm -lpthread 2>&1 | grep -oE "undefined reference to \`[^']+'" | sed -E "s/undefined reference to \`//; s/'$//" | sort -u > "$ROOT/build/undefs.txt"
wc -l < "$ROOT/build/undefs.txt"
