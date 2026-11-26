#!/bin/bash
# Copy the console's own system libraries into build/sysroot. build.sh links
# against them so the binary loads on the console's old glibc (2.23).
#   bash port/nesmini/fetch-sysroot.sh [console-ip]     (default 169.254.1.1)
# The console has no scp, so each file is streamed through ssh.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONSOLE="${1:-${CONSOLE:-169.254.1.1}}"
SYS="$ROOT/build/sysroot"
SSH=(ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "root@$CONSOLE")
mkdir -p "$SYS"
"${SSH[@]}" true || { echo "Can't reach root@$CONSOLE. Plug the console in (hakchi USB), turn it on, and try again."; exit 1; }
for f in libc.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libm.so.6 ld-linux-armhf.so.3 libMali.so; do
  if "${SSH[@]}" "for d in /lib /usr/lib; do [ -f \$d/$f ] && exec cat \$d/$f; done; exit 1" > "$SYS/$f.tmp"; then
    mv "$SYS/$f.tmp" "$SYS/$f"; echo "  $f"
  else
    rm -f "$SYS/$f.tmp"; echo "  $f (not found, skipped)"
  fi
done
echo "Saved to $SYS"
