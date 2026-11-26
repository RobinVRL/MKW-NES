#!/bin/bash
# USB drive setup (Linux): builds the mkw/ folder tree that the NES/SNES
# Classic Mini launcher expects on a USB stick (see port/INSTALL.md,
# "Option B: manual install over SSH", step 1).
#
# Before running:
#   - Run 'bash port/build.sh' first (needs build/mkw-nesmini.stripped).
#   - Game files built by the MKW-3DS builder from your own disc, e.g. at
#     ~/mkw/sdmc/3ds/mkw-3ds/
#   - Plug the USB drive into this PC (at least 3 GB free; ext4 recommended).
#
# Run from the repository root:
#   bash port/nesmini/easy-install/install-linux.sh [options]
#
# Options:
#   --device DEV        USB device to use, e.g. /dev/sdb or /dev/sdb1
#                       (default: pick from a list of plugged-in drives)
#   --game-dir DIR      folder containing 3ds/mkw-3ds
#                       (default: ~/MKW-3DS-Builder/build/release/sd-card)
#   --format-ext4       Format the whole drive as ext4 first (WIPES ALL DATA!)
set -euo pipefail

DEVICE='' GAME_DIR="${MKW_BUILDER:-$HOME/MKW-3DS-Builder}/build/release/sd-card" FORMAT=0
while [ $# -gt 0 ]; do
  case "$1" in
    --device)     DEVICE="$2"; shift 2 ;;
    --game-dir)   GAME_DIR="$2"; shift 2 ;;
    --format-ext4) FORMAT=1; shift ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"                 # repository root

echo "== Mario Kart Wii (NES Mini port) - USB drive setup (Linux) =="
echo
[ -f "$ROOT/build/mkw-nesmini.stripped" ] || { echo "build/mkw-nesmini.stripped not found. Run 'bash port/build.sh' first."; exit 1; }
# Accept a folder holding 3ds/mkw-3ds (the builder's sd-card) or sdmc/3ds/mkw-3ds.
[ -d "$GAME_DIR/sdmc/3ds/mkw-3ds" ] && GAME_DIR="$GAME_DIR/sdmc"
[ -d "$GAME_DIR/3ds/mkw-3ds" ] || { echo "Game files not found at $GAME_DIR/3ds/mkw-3ds"; echo "Run 'bash setup.sh game-files' first, or use --game-dir <folder containing 3ds/mkw-3ds>."; exit 1; }

# 1. Pick a USB drive (removable, or one the user names).
list_usb() {
  lsblk -rno NAME,RM,TYPE,SIZE,MOUNTPOINT | awk '$3=="disk" && $2==1 {print "/dev/"$1, $4}'
}
if [ -z "$DEVICE" ]; then
  DRIVES="$(list_usb)"
  [ -n "$DRIVES" ] || { echo "No removable drives found. Plug the USB drive in and try again, or pass --device /dev/sdX."; exit 1; }
  echo "Removable drives:"
  echo "$DRIVES" | awk '{print "  " $0}'
  echo
  read -rp "Which one? (e.g. sdb): " PICK
  DEVICE="/dev/$PICK"
fi
[ -b "$DEVICE" ] || { echo "$DEVICE is not a block device."; exit 1; }

# 2. Optional: format as ext4 (the binary needs Unix file permissions).
if [ "$FORMAT" = 1 ]; then
  echo
  echo "WARNING: this will ERASE EVERYTHING on $DEVICE."
  read -rp "Type the device name again to confirm (e.g. $DEVICE): " CONFIRM
  [ "$CONFIRM" = "$DEVICE" ] || { echo "Aborted."; exit 1; }
  echo "Formatting $DEVICE as ext4 (needs sudo)..."
  sudo wipefs -a "$DEVICE"
  sudo parted -s "$DEVICE" mklabel msdos mkpart primary ext4 1MiB 100%
  sudo mkfs.ext4 -F -L MKW "${DEVICE}1"
  echo "Formatting done. Unplug and replug the drive, then re-run WITHOUT --format-ext4."
  exit 0
fi

# 3. Find where its partitions are mounted; pick the first mountpoint.
MNT="$(lsblk -rno NAME,TYPE,MOUNTPOINT "$DEVICE" | awk '$2=="part" && $3!="" {print $3; exit}')"
if [ -z "$MNT" ]; then
  MNT="$(lsblk -rno MOUNTPOINT "$DEVICE" | awk 'NF && $1!="[SWAP]" {print; exit}')"
fi
if [ -z "$MNT" ]; then
  PARTS="$(lsblk -rno NAME,TYPE "$DEVICE" | awk '$2=="part"{print "/dev/"$1}')"
  [ -n "$PARTS" ] || { echo "$DEVICE has no partitions. Re-run with --format-ext4 to create one."; exit 1; }
  echo "Partitions of $DEVICE are not mounted. Mounting the first one (needs sudo)..."
  FIRST="$(echo "$PARTS" | head -1)"
  MNT="/media/$USER/mkw-usb"
  sudo mkdir -p "$MNT"
  sudo mount "$FIRST" "$MNT"
  echo "Mounted $FIRST at $MNT."
fi
echo "Using $DEVICE mounted at $MNT."
FSTYPE="$(lsblk -rno FSTYPE "$DEVICE" | awk 'NF{print; exit}')"
echo "File system: ${FSTYPE:-unknown}"

# 4. Copy the files: the layout from port/INSTALL.md.
echo
echo "Copying (about 3 GB; this takes a while)..."
mkdir -p "$MNT/mkw"
cp "$ROOT/build/mkw-nesmini.stripped" "$MNT/mkw/mkw-nesmini"
cp "$ROOT/port/nesmini/mkw.sh" "$MNT/mkw/mkw.sh"
[ -f "$ROOT/build/padbridge" ] && cp "$ROOT/build/padbridge" "$MNT/mkw/"
mkdir -p "$MNT/mkw/sdmc/3ds"
if [ ! -d "$MNT/mkw/sdmc/3ds/mkw-3ds" ]; then
  cp -R "$GAME_DIR/3ds/mkw-3ds" "$MNT/mkw/sdmc/3ds/"
else
  echo "(sdmc/3ds/mkw-3ds already on the drive; not overwriting.)"
fi

# 5. Permissions (only possible on ext4; on FAT the console-side fallback is
#    copying the two small files to internal storage - INSTALL.md step 3).
chmod 755 "$MNT/mkw/mkw-nesmini" "$MNT/mkw/mkw.sh" "$MNT/mkw/padbridge" 2>/dev/null \
  || echo "(chmod failed: FAT/exFAT drive. On the console, use the internal-storage copy in INSTALL.md step 3.)"

sync
echo
echo "Checking the result..."
ls -lh "$MNT/mkw" | sed 's/^/  /'
[ -f "$MNT/mkw/sdmc/3ds/mkw-3ds/race.pak" ] && echo "  race.pak: OK" || echo "  WARNING: race.pak missing!"

echo
echo "Done. Eject the drive safely (sync, then unplug)."
if [ "$FSTYPE" = ext4 ]; then
  echo "On the console (ssh root@169.254.1.1):"
  echo "  mount | grep -E '/media|/mnt'    # find the drive"
  echo "  /media/*/mkw/mkw.sh              # run the game"
else
  echo "FAT/exFAT drive: follow port/INSTALL.md 'Option B' step 3 (copy mkw-nesmini"
  echo "and mkw.sh to /var/lib/hakchi/mkw on the console) - chmod doesn't work on FAT."
fi
