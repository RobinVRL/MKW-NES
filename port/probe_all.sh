#!/bin/bash
# Compile every game translation unit for ARMv7 and summarise which fail and why.
source "$(dirname "$0")/env.sh"
export ROOT PORT SRC CROSS ARCH INCS DEFS
mkdir -p "$ROOT/build/probe"
cd "$SRC"
one() {
  f="$1"; o="$ROOT/build/probe/$(echo "$f" | tr '/' '_').o"
  if ${CROSS}g++ -std=gnu++23 -O0 $ARCH $DEFS $INCS -fno-exceptions -fno-rtti -fno-strict-aliasing -w -c "$f" -o "$o" 2> "$o.log"; then
    echo "OK   $f"
  else
    echo "FAIL $f :: $(grep -m1 -E 'error|fatal' "$o.log")"
  fi
}
export -f one
(cat docs/MANUAL-PROGRAM-SOURCES.txt; echo src/overlay.cpp) | tr -d '\r' | xargs -n1 -P"$(nproc)" bash -c 'one "$0"' | sort > "$ROOT/build/probe/summary.txt"
grep -c '^OK' "$ROOT/build/probe/summary.txt"; grep -c '^FAIL' "$ROOT/build/probe/summary.txt"
