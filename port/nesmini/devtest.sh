#!/bin/sh
# Dev test on the console: run /tmp/mkw-nesmini for $1 seconds with the menu
# stopped, grab the framebuffer to /tmp/fb.raw, print the log and exit code.
SECS=${1:-20}
cd /tmp || exit 1
mkdir -p /tmp/sdmc
uistop >/dev/null 2>&1
MKW_SDMC=${MKW_SDMC:-/tmp/sdmc} ./mkw-nesmini > /tmp/mkw.log 2>&1 &
PID=$!
i=0
while [ $i -lt $SECS ] && kill -0 $PID 2>/dev/null; do sleep 1; i=$((i+1)); done
dd if=/dev/fb0 of=/tmp/fb.raw bs=1280 count=5760 2>/dev/null
if kill -0 $PID 2>/dev/null; then echo "still running after ${SECS}s"; kill $PID; sleep 1; kill -9 $PID 2>/dev/null; fi
wait $PID
echo "exit code $?"
uistart >/dev/null 2>&1 &
echo "--- log"
tail -30 /tmp/mkw.log
