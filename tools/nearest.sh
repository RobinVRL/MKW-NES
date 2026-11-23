#!/bin/bash
# nearest.sh <lib> <hex offset>...: nearest exported symbol at or below each offset.
lib="$1"; shift
syms=$(arm-linux-gnueabihf-nm -D --defined-only "$lib" | sort)
for off in "$@"; do
  o=$((16#${off#0x}))
  best=""
  while read -r a t n; do
    [ $((16#$a)) -le $o ] && best="$n+0x$(printf %x $((o - 16#$a)))"
  done <<< "$syms"
  echo "$off -> $best"
done
