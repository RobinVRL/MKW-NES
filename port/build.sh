#!/bin/bash
# Build build/mkw-nesmini (ARMv7-A hard-float Linux) from the unmodified
# MKW-3DS sources plus the port's libctru/citro3d replacements.
#   OPT=-O2 (default)  JOBS=$(nproc)  CLEAN=1 to rebuild everything
set -e
source "$(dirname "$0")/env.sh"
OPT="${OPT:--O2}"
JOBS="${JOBS:-$(nproc)}"
OBJ="$ROOT/build/obj"
[ -n "$CLEAN" ] && rm -rf "$OBJ"
mkdir -p "$OBJ"
python3 "$PORT/gen.py" > /dev/null

CXXFLAGS="-std=gnu++23 $OPT $ARCH $DEFS $INCS -fno-exceptions -fno-rtti -fno-strict-aliasing -ffunction-sections -fdata-sections -w"
CFLAGS="-std=gnu11 $OPT $ARCH $DEFS $INCS -fno-strict-aliasing -ffunction-sections -fdata-sections -w"
PORTFLAGS="-std=gnu++20 $OPT $ARCH $DEFS $INCS -I$PORT/c3d -I$PORT/ctru/src -fno-strict-aliasing -Wall -Wno-unused-function"
export CROSS CXXFLAGS CFLAGS PORTFLAGS OBJ

compile() {
  src="$1"; kind="$2"
  o="$OBJ/$(echo "$src" | sed "s#^$ROOT/##; s#/#_#g").o"
  if [ "$o" -nt "$src" ] && [ -z "$FORCE" ]; then echo "$o"; return; fi
  case "$kind" in
    game) ${CROSS}g++ $CXXFLAGS -c "$src" -o "$o" ;;
    c)    ${CROSS}gcc $CFLAGS -c "$src" -o "$o" ;;
    port) ${CROSS}g++ $PORTFLAGS -c "$src" -o "$o" ;;
  esac || { echo "FAILED $src" >&2; return 1; }
  echo "$o"
}
export -f compile
export ROOT

{
  (cat "$SRC/docs/MANUAL-PROGRAM-SOURCES.txt"; echo src/overlay.cpp) | tr -d '\r' | sed "s#^#$SRC/#; s#\$# game#"
  echo "$SRC/vendor/cfl-mii/source/cfl_mii.c c"
  echo "$SRC/vendor/citro3d/source/tex3ds.c c"
  for m in "$SRC"/vendor/citro3d/source/maths/*.c; do echo "$m c"; done
  for p in "$PORT"/ctru/src/*.cpp "$PORT"/c3d/*.cpp "$PORT"/host/crash.cpp; do echo "$p port"; done
} | grep -v '^\s*$' | xargs -P"$JOBS" -L1 bash -c 'compile "$0" "$1"' > "$OBJ/objects.lst"

# The console's glibc (2.22) keeps pthread/dl/rt functions in their own
# libraries; glibc >= 2.34 moved them into libc. Linking the console's copies
# (build/sysroot, pulled from the NES Mini) ahead of libc binds each symbol to
# the library that has it there.
SYSROOT="$ROOT/build/sysroot"
CONSOLE_LIBS=""
if [ -f "$SYSROOT/libpthread.so.0" ]; then
  CONSOLE_LIBS="-Wl,--no-as-needed $SYSROOT/libpthread.so.0 $SYSROOT/libdl.so.2 $SYSROOT/librt.so.1 -Wl,--as-needed -Wl,--allow-shlib-undefined"
else
  echo "warning: build/sysroot missing; the binary will not load on the console" >&2
fi
WRAPS=""
for n in linearAlloc linearMemAlign linearRealloc linearGetSize linearFree linearSpaceFree fopen; do WRAPS="$WRAPS -Wl,--wrap=$n"; done
# glibc 2.23 compatibility (port/compat/glibc_compat.c)
COMPAT="$PORT/compat/glibc_compat.c"
${CROSS}gcc -std=gnu11 -O2 $ARCH -c "$COMPAT" -o "$OBJ/glibc_compat.o"
for n in $(grep -oE '__wrap_[A-Za-z0-9_]+' "$COMPAT" | sed 's/^__wrap_//' | sort -u); do WRAPS="$WRAPS -Wl,--wrap=$n"; done
WRAPS="$WRAPS $(grep -oE '^PASS\([^,]+, *[A-Za-z0-9_]+' "$COMPAT" | sed -E 's/.*, *//' | sed 's/^/-Wl,--wrap=/' | tr '\n' ' ')"
${CROSS}g++ $ARCH $OPT -Wl,--gc-sections -Wl,--hash-style=both -static-libstdc++ -static-libgcc $WRAPS \
  @"$OBJ/objects.lst" "$OBJ/glibc_compat.o" -o "$ROOT/build/mkw-nesmini" $CONSOLE_LIBS -lpthread -ldl -lm
# Fail if anything still needs a glibc newer than the console's 2.23.
if ${CROSS}objdump -T "$ROOT/build/mkw-nesmini" | grep -E 'GLIBC_2\.(2[4-9]|[3-9][0-9])'; then
  echo "error: symbols above need glibc > 2.23" >&2; exit 1
fi
if [ -f "$SYSROOT/libc.so.6" ]; then bash "$PORT/nesmini/checksyms.sh" "$ROOT/build/mkw-nesmini" || exit 1; fi
${CROSS}strip -o "$ROOT/build/mkw-nesmini.stripped" "$ROOT/build/mkw-nesmini"
ls -l "$ROOT/build/mkw-nesmini.stripped"
