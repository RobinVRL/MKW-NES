#!/bin/bash
# Linux/Ubuntu: make the game files from your own Mario Kart Wii (USA) disc
# with the upstream builder (same steps as "src3ds/Build MKW CIA.command",
# non-interactive).
#
#   bash port/cook_content.sh [disc.wbfs|disc.iso]
#
# src3ds is copied to $MKW_BUILDER (default ~/MKW-3DS-Builder) so a Windows
# .venv or a synced folder can't get in the way.
# Output: $MKW_BUILDER/build/release/sd-card/3ds/mkw-3ds
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILDER="${MKW_BUILDER:-$HOME/MKW-3DS-Builder}"
fail() { echo -e "\n$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
pyok() { "$1" -c 'import sys; sys.exit(not ((3, 11) <= sys.version_info[:2] < (3, 15)))' 2>/dev/null; }

# 1. Disc image.
DISC="$1"
if [ -z "$DISC" ]; then
  shopt -s nullglob; discs=("$ROOT"/rom/*.wbfs "$ROOT"/rom/*.iso); shopt -u nullglob
  DISC="${discs[0]}"
fi
[ -f "$DISC" ] || fail "No disc image found. Put your Mario Kart Wii (USA) .wbfs or .iso in $ROOT/rom/ (or pass its path)."
DISC="$(realpath "$DISC")"
echo "Disc:    $DISC"
echo "Builder: $BUILDER"

# 2. Dependencies (apt on Debian/Ubuntu).
if have apt-get; then
  need=()
  have git  || need+=(git)
  have g++  || need+=(g++)
  have node || need+=(nodejs)
  have rsync || need+=(rsync)
  have wget || need+=(wget)
  python3 -c 'import venv, ensurepip' 2>/dev/null || need+=(python3-venv)
  if [ ${#need[@]} -gt 0 ]; then sudo apt-get update -qq; sudo apt-get install -y "${need[@]}"; fi
fi
for t in git g++ node rsync; do have "$t" || fail "$t is needed. Install it with your package manager."; done
NODE_MAJOR="$(node -p 'process.versions.node.split(".")[0]')"
[ "$NODE_MAJOR" -ge 18 ] || fail "Node.js $NODE_MAJOR is too old (need 18+). Install the LTS: https://nodejs.org/en/download/package-manager"

PY=""
for c in python3 python3.14 python3.13 python3.12 python3.11; do have "$c" && pyok "$c" && { PY="$c"; break; }; done
[ -n "$PY" ] || fail "Python 3.11-3.14 is needed (Ubuntu 22.04: sudo add-apt-repository ppa:deadsnakes/ppa && sudo apt install python3.12 python3.12-venv)."

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
if [ ! -d "$DEVKITPRO/devkitARM" ]; then
  have apt-get || fail "devkitPro (3DS tools) is needed: https://devkitpro.org/wiki/Getting_Started"
  echo "Installing devkitPro 3DS tools (needs sudo)..."
  if ! have dkp-pacman; then
    wget -qO /tmp/install-devkitpro-pacman https://apt.devkitpro.org/install-devkitpro-pacman
    chmod +x /tmp/install-devkitpro-pacman
    sudo /tmp/install-devkitpro-pacman
  fi
  sudo dkp-pacman -S --noconfirm --needed 3ds-dev
fi
export DEVKITARM="$DEVKITPRO/devkitARM" PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitARM/bin:$PATH"

# 3. Copy src3ds (keeps the builder's own build output, .venv and vendor checkouts).
mkdir -p "$BUILDER"
rsync -a --update --exclude .venv --exclude /build --exclude /mkw-rom --exclude node_modules "$ROOT/src3ds/" "$BUILDER/"
cd "$BUILDER"

# 4. Python environment.
[ -d vendor/gclib ] || "$PY" tools/bootstrap.py
[ -x .venv/bin/python ] || "$PY" -m venv .venv
pyok .venv/bin/python || fail "$BUILDER/.venv uses an unsupported Python. Remove it and run again."
.venv/bin/python -c 'import numpy, PIL, Crypto, unicorn' 2>/dev/null || .venv/bin/python -m pip install -r requirements.txt

# 5. Build (resumable: finished steps are skipped next time).
echo -e "\nBuilding the game files. The first run takes a few hours...\n"
PYTHONUNBUFFERED=1 .venv/bin/python tools/release/build_cia.py "$DISC" 2>&1 | tee build-cia-log.txt \
  || fail "Build failed. Run again to resume; if it keeps failing, see $BUILDER/build-cia-log.txt"
echo -e "\nGame files: $BUILDER/build/release/sd-card/3ds/mkw-3ds"
