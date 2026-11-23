#!/bin/sh
# NES Mini launcher for the MKW-3DS port (hakchi CE).
# Put this next to mkw-nesmini; the game files go in ./sdmc/3ds/mkw-3ds.
DIR="$(cd "$(dirname "$0")" && pwd)"
export MKW_SDMC="${MKW_SDMC:-$DIR/sdmc}"
export MKW_HIDLOG="${MKW_HIDLOG-1}" # log button events to mkw.log (set empty to silence)
cd "$DIR" || exit 1
uistop 2>/dev/null
"$DIR/mkw-nesmini" > "$DIR/mkw.log" 2>&1
uistart 2>/dev/null
