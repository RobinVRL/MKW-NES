#!/bin/bash
# Run the x86-64 test build headless: frames go to build/frames/*.ppm
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/build/frames"
export GLIBC_TUNABLES=glibc.malloc.mmap_max=0:glibc.malloc.arena_max=1
export EGL_PLATFORM=surfaceless MKW_HEADLESS=1
export MKW_DUMP="${MKW_DUMP:-$ROOT/build/frames}" MKW_FRAMES="${MKW_FRAMES:-300}"
export MKW_SDMC="${MKW_SDMC:-$ROOT/build/sdmc}"
exec "$ROOT/build/mkw-host" "$@"
