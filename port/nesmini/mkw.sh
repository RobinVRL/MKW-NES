#!/bin/sh
# NES Mini launcher for the MKW-3DS port (hakchi CE).
# Put this next to mkw-nesmini. Game files are looked up in order:
#   $MKW_SDMC, ./sdmc, the PC share (/media/pc, /media/pc/mkw/sdmc),
#   USB drives (/media/*/mkw/sdmc)
DIR="$(cd "$(dirname "$0")" && pwd)"
LOG="$DIR/mkw.log"; touch "$LOG" 2>/dev/null || LOG=/tmp/mkw.log

find_sdmc() {
  for d in "$MKW_SDMC" "$DIR/sdmc" /media/pc /media/pc/sdmc /media/pc/mkw/sdmc /media/*/mkw/sdmc /media/mkw/sdmc; do
    [ -n "$d" ] && [ -d "$d/3ds/mkw-3ds" ] && { echo "$d"; return 0; }
  done
  return 1
}

SDMC="$(find_sdmc)"
# PC share not up yet (e.g. started right after boot): try mounting it now.
if [ -z "$SDMC" ] && [ -x /etc/init.d/S95mkwpc ]; then
  /etc/init.d/S95mkwpc once >/dev/null 2>&1
  SDMC="$(find_sdmc)"
fi
[ -z "$SDMC" ] && { echo "mkw: game files not found (no 3ds/mkw-3ds)" > "$LOG"; exit 1; }

export MKW_SDMC="$SDMC"
export MKW_HIDLOG="${MKW_HIDLOG-1}" # log button events (set empty to silence)
cd "$DIR" || exit 1
uistop 2>/dev/null
"$DIR/mkw-nesmini" > "$LOG" 2>&1
uistart 2>/dev/null
