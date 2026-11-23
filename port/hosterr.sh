#!/bin/bash
# First errors of every failed host-build TU.
cd "$(dirname "$0")/../build/host-obj"
for f in $(grep -l 'error' *.log); do echo "== $f"; grep -m${N:-3} 'error' "$f" | cut -c1-300; done
