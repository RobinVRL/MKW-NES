#!/bin/bash
# Build minimal console test binaries with the same link setup as build.sh.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
S="$ROOT/build/sysroot"; O="$ROOT/build/hello"; mkdir -p "$O"
ARCH="-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard"
LIBS="-Wl,--no-as-needed $S/libpthread.so.0 $S/libdl.so.2 $S/librt.so.1 -Wl,--as-needed -Wl,--allow-shlib-undefined"
WRAPS=""
for n in $(grep -oE '__wrap_[A-Za-z0-9_]+' "$ROOT/port/compat/glibc_compat.c" | sed 's/^__wrap_//' | sort -u); do WRAPS="$WRAPS -Wl,--wrap=$n"; done
arm-linux-gnueabihf-gcc -std=gnu11 -O2 $ARCH -c "$ROOT/port/compat/glibc_compat.c" -o "$O/compat.o"
cat > "$O/c.c" <<'EOF'
#include <stdio.h>
int main(void){ puts("hello c"); return 0; }
EOF
cat > "$O/cxx.cpp" <<'EOF'
#include <cstdio>
#include <thread>
#include <string>
int main(){ std::string s="hello c++"; std::thread t([&]{ puts(s.c_str()); }); t.join(); return 0; }
EOF
arm-linux-gnueabihf-gcc $ARCH "$O/c.c" -o "$O/c-plain" $LIBS
arm-linux-gnueabihf-gcc $ARCH -Wl,--hash-style=both $WRAPS "$O/c.c" "$O/compat.o" -o "$O/c-compat" $LIBS -lm
arm-linux-gnueabihf-g++ $ARCH -Wl,--hash-style=both -static-libstdc++ -static-libgcc $WRAPS "$O/cxx.cpp" "$O/compat.o" -o "$O/cxx" $LIBS -lm
ls -l "$O"
