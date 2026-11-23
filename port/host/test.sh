#!/bin/bash
# Build + run the x86-64 test against the cooked game files; symbolize crashes.
#   FRAMES=600 EVERY=60 SECS=150 bash port/host/test.sh
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
bash port/host.sh > build/host.log 2>&1 || true
bash port/hosterr.sh
[ -x build/mkw-host ] || { tail build/host.log; exit 1; }
rm -rf build/frames
MKW_SDMC="${MKW_SDMC:-/mnt/c/MKW-3DS-Builder/build/release/sd-card}" MKW_FRAMES="${FRAMES:-600}" MKW_DUMP_EVERY="${EVERY:-60}" \
  timeout -s KILL "${SECS:-150}" bash port/host/run.sh > build/run.log 2>&1
echo "exit=$?"
grep -vE '^build/mkw-host\(' build/run.log | tail -${TAIL:-25}
# resolve "build/mkw-host(+0x...)" or "[0x...]" frames (non-PIE: absolute addresses)
grep -oE '\[0x[0-9a-f]+\]' build/run.log | tr -d '[]' | head -20 | xargs -r addr2line -Cfip -e build/mkw-host | sed 's#/mnt/c/Users/thedi/OneDrive/Pictures/Documents/MKW-NES/##' | head -20
ls build/frames 2>/dev/null | tr '\n' ' '
