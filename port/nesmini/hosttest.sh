#!/bin/bash
# Runs the PC test build headless with scripted A presses and reports where
# it is after a while (backtrace of every thread if gdb is available).
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/hosttest"
rm -rf "$OUT"; mkdir -p "$OUT/frames"
cd "$ROOT/build"
export MKW_SDMC=/mnt/c/MKW-3DS-Builder/build/release/sd-card
export MKW_HEADLESS=1 MKW_DUMP="$OUT/frames" MKW_DUMP_EVERY=60
export MKW_INPUT="200:A:5,400:A:5,600:A:5,800:A:5,1000:A:5,1200:A:5,1400:A:5"
./mkw-host > "$OUT/run.log" 2>&1 &
pid=$!
sleep "${WAIT:-60}"
if kill -0 $pid 2>/dev/null; then
  echo "running after ${WAIT:-60}s"
  for t in /proc/$pid/task/*; do echo "tid ${t##*/} $(cut -d' ' -f3,14,15 $t/stat)"; done
  if command -v gdb >/dev/null; then
    gdb -p $pid -batch -ex "thread apply all bt 12" > "$OUT/bt.txt" 2>&1
  fi
  kill $pid
else
  wait $pid; echo "exited code=$?"
fi
tail -15 "$OUT/run.log"
ls "$OUT/frames" | tail -3
