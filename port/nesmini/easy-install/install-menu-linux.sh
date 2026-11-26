#!/bin/bash
# Linux: add "Mario Kart Wii" to the console's menu over hakchi's USB link.
# The game files are read from a USB drive plugged into the console
# (make it with: bash setup.sh usb).
#
#   bash port/nesmini/easy-install/install-menu-linux.sh [console-ip]   (default 169.254.1.1)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
CONSOLE="${1:-${CONSOLE:-169.254.1.1}}"
SSH=(ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "root@$CONSOLE")

echo "== Mario Kart Wii (NES Mini port) menu entry installer (Linux) =="
[ -f "$ROOT/build/mkw-nesmini.stripped" ] || { echo "build/mkw-nesmini.stripped not found. Run 'bash setup.sh build' first."; exit 1; }
"${SSH[@]}" true || { echo "Console not found at $CONSOLE. Plug it in with hakchi's USB cable, turn it on, and try again."; exit 1; }
echo "Console found at $CONSOLE."

# Menu icon: official cover from GameTDB (not shipped), scaled to hakchi's 228x204 box.
ICON="$HERE/files/icon.png"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
if command -v wget >/dev/null && wget -qO "$TMP/cover.png" https://art.gametdb.com/wii/cover/US/RMCE01.png; then
  if command -v convert >/dev/null; then convert "$TMP/cover.png" -resize 228x204 "$TMP/icon.png"; else cp "$TMP/cover.png" "$TMP/icon.png"; fi
  ICON="$TMP/icon.png"; echo "Using the official cover."
fi

# The console has no scp: stream each file through ssh.
echo "Copying files..."
"${SSH[@]}" "rm -rf /tmp/inst; mkdir -p /tmp/inst"
send() { [ -f "$1" ] && "${SSH[@]}" "cat > /tmp/inst/$2" < "$1"; return 0; }
send "$ROOT/build/mkw-nesmini.stripped" mkw-nesmini
send "$ROOT/port/nesmini/mkw.sh" mkw.sh
send "$ROOT/port/nesmini/S95mkwpc" S95mkwpc
send "$ROOT/port/nesmini/install.sh" install.sh
send "$ROOT/build/padbridge" padbridge
send "$ICON" icon.png

echo "Installing..."
"${SSH[@]}" "cd /tmp/inst && sed -i 's/\r\$//' mkw.sh S95mkwpc install.sh && sh install.sh && rm -rf /tmp/inst && sync"
echo
echo "Done! Plug the USB drive (with mkw/sdmc/3ds/mkw-3ds on it) into the console,"
echo "restart it, and start 'Mario Kart Wii' from the menu."
