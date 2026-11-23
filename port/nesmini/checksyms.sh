#!/bin/bash
# Verify every versioned import of a binary exists in the console's own
# libraries (copied to build/sysroot from the NES Mini). Prints missing ones.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${1:-$ROOT/build/mkw-nesmini}"
SYS="$ROOT/build/sysroot"
CROSS=arm-linux-gnueabihf-
have=$(mktemp)
for l in "$SYS"/*.so*; do
  ${CROSS}objdump -T "$l" | awk '$4 != "*UND*" && NF >= 7 { v=$(NF-1); gsub(/[()]/,"",v); print $NF "@" v }'
done | sort -u > "$have"
miss=0
while read -r sym; do
  grep -qxF "$sym" "$have" || { echo "missing on console: $sym"; miss=1; }
done < <(${CROSS}objdump -T "$BIN" | awk '$4 == "*UND*" && $(NF-1) ~ /GLIBC/ { v=$(NF-1); gsub(/[()]/,"",v); print $NF "@" v }' | sort -u)
rm -f "$have"
[ $miss = 0 ] && echo "all imports present on console"
exit $miss
