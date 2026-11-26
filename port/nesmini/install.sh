#!/bin/sh
# Run on the console (from the folder holding mkw-nesmini, mkw.sh, S95mkwpc):
#   PCUSER=you PCPASS=secret [PC=169.254.x.x] [PCSHARE=MKW] sh install.sh   (PC share)
#   sh install.sh                                                          (USB drive)
# Makes the setup survive power-offs:
#   - program + launcher in /var/lib/hakchi/mkw (NAND, persistent)
#   - /etc/init.d/S95mkwpc mounts the PC share at every boot
#   - "Mario Kart Wii" entry in the console menu
set -e
SRC="$(cd "$(dirname "$0")" && pwd)"
DEST=/var/lib/hakchi/mkw
CODE=CLV-I-WYKOJ
GAMES=/var/lib/hakchi/games/nes-usa/000

mkdir -p "$DEST"
cp "$SRC/mkw-nesmini" "$SRC/mkw.sh" "$DEST/"
chmod 755 "$DEST/mkw-nesmini" "$DEST/mkw.sh"
# Optional: virtual gamepad fed from a PC controller (padsend.py).
[ -f "$SRC/padbridge" ] && cp "$SRC/padbridge" "$DEST/" && chmod 755 "$DEST/padbridge"

# PC share is optional: without PCUSER the game files are read from a USB
# drive (/media/*/mkw/sdmc) and no boot script is installed.
if [ -n "$PCUSER" ]; then
  cp "$SRC/S95mkwpc" /etc/init.d/S95mkwpc
  chmod 755 /etc/init.d/S95mkwpc
  ( umask 077
    printf 'PCUSER=%s\nPCPASS=%s\nPCSHARE=%s\nPC=%s\n' "$PCUSER" "$PCPASS" "${PCSHARE:-MKW}" "$PC" > /etc/mkw-pc.conf )
  chmod 600 /etc/mkw-pc.conf
fi

mkdir -p "$GAMES/$CODE"
cat > "$GAMES/$CODE/$CODE.desktop" <<EOF
[Desktop Entry]
Type=Application
Exec=/bin/sh $DEST/mkw.sh
Path=/var/saves/$CODE
Name=Mario Kart Wii
Icon=$GAMES/$CODE/$CODE.png

[X-CLOVER Game]
Code=$CODE
TestID=0
ID=0
Players=1
Simultaneous=0
ReleaseDate=2008-04-10
SaveCount=0
SortRawTitle=mario kart wii
SortRawPublisher=
Copyright=
EOF
[ -f "$SRC/icon.png" ] && cp "$SRC/icon.png" "$GAMES/$CODE/$CODE.png"
mkdir -p "/var/saves/$CODE"
chmod 755 "$GAMES/$CODE"; chmod 644 "$GAMES/$CODE"/*
sync
if [ -x /etc/init.d/S95mkwpc ] && [ -n "$PCUSER" ]; then
  echo "installed; mounting the PC share now..."
  /etc/init.d/S95mkwpc once || echo "(PC share not reachable yet; it will retry at boot)"
else
  echo "installed; game files will be read from a USB drive (mkw/sdmc/3ds/mkw-3ds)"
fi
