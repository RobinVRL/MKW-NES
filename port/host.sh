#!/bin/bash
# Native x86-64 build of the same sources for testing the renderer on the PC
# (Mesa EGL, headless). Output: build/mkw-host. Not shipped.
source "$(dirname "$0")/env.sh"
CROSS=""
ARCH="-g -fpermissive"
DEFS="$DEFS -DMKW_HOST -include $PORT/host/host_shim.h"
OPT="${OPT:--O1}"
JOBS="${JOBS:-$(nproc)}"
OBJ="$ROOT/build/host-obj"
mkdir -p "$OBJ"
python3 "$PORT/gen.py" > /dev/null
CXXFLAGS="-std=gnu++23 $OPT $ARCH $DEFS $INCS -fno-exceptions -fno-rtti -fno-strict-aliasing -w"
CFLAGS="-std=gnu11 $OPT $ARCH $DEFS $INCS -fno-strict-aliasing -w"
PORTFLAGS="-std=gnu++20 $OPT $ARCH $DEFS $INCS -I$PORT/c3d -I$PORT/ctru/src -fno-strict-aliasing -w"
export CXXFLAGS CFLAGS PORTFLAGS OBJ ROOT
compile() {
  src="$1"; kind="$2"
  o="$OBJ/$(echo "$src" | sed "s#^$ROOT/##; s#/#_#g").o"
  if [ "$o" -nt "$src" ] && [ -z "$FORCE" ]; then echo "$o"; return; fi
  # host only: drop ARM VFP inline asm (main.cpp's FPSCR setup)
  if [ "$kind" = game ] && grep -q 'asm volatile("vm' "$src"; then
    tmp="$OBJ/$(basename "$src")"; sed 's/asm volatile("vm[^;]*;//g' "$src" > "$tmp"
    g++ $CXXFLAGS -I"$(dirname "$src")" -c "$tmp" -o "$o" 2> "$o.log" || { echo "FAILED $src" >&2; return 0; }
    echo "$o"; return
  fi
  case "$kind" in
    game) g++ $CXXFLAGS -c "$src" -o "$o" 2> "$o.log" ;;
    c)    gcc $CFLAGS -c "$src" -o "$o" 2> "$o.log" ;;
    port) g++ $PORTFLAGS -c "$src" -o "$o" 2> "$o.log" ;;
  esac || { echo "FAILED $src" >&2; return 0; }
  echo "$o"
}
export -f compile
{
  (cat "$SRC/docs/MANUAL-PROGRAM-SOURCES.txt"; echo src/overlay.cpp) | tr -d '\r' | grep -v '^\s*$' | sed "s#^#$SRC/#; s#\$# game#"
  echo "$SRC/vendor/cfl-mii/source/cfl_mii.c c"
  echo "$SRC/vendor/citro3d/source/tex3ds.c c"
  for m in "$SRC"/vendor/citro3d/source/maths/*.c; do echo "$m c"; done
  for p in "$PORT"/ctru/src/*.cpp "$PORT"/c3d/*.cpp "$PORT"/host/*.cpp; do echo "$p port"; done
} | xargs -P"$JOBS" -L1 bash -c 'compile "$0" "$1"' > "$OBJ/objects.lst"
WRAPS=""
for n in linearAlloc linearMemAlign linearRealloc linearGetSize linearFree linearSpaceFree fopen; do WRAPS="$WRAPS -Wl,--wrap=$n"; done
# -no-pie + run.sh's malloc tunables keep every address below 4 GB, as the
# citro3d structs (and the 3DS game) store pointers in u32 fields.
g++ $ARCH -no-pie $WRAPS @"$OBJ/objects.lst" -o "$ROOT/build/mkw-host" -lpthread -ldl -lm 2>&1 | grep -oE "undefined reference to \`[^']+'|error.*" | sort | uniq -c | sort -rn | head -40
ls -l "$ROOT/build/mkw-host" 2>/dev/null
