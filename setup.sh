#!/bin/bash
# One-stop setup for Ubuntu/Debian Linux (and WSL, which setup.bat uses).
#
#   bash setup.sh              do every step that isn't done yet
#   bash setup.sh check        show what's ready and what's missing
#   bash setup.sh game-files   make the game files from your disc (rom/*.wbfs|iso)
#   bash setup.sh deps         install the cross compiler and Python (apt)
#   bash setup.sh sysroot      copy the console's libraries to build/sysroot
#   bash setup.sh build        build build/mkw-nesmini.stripped (+ build/padbridge)
#   bash setup.sh usb [opts]   copy the program + game files to a USB drive
#   bash setup.sh install      add "Mario Kart Wii" to the console's menu (SSH)
#   bash setup.sh host         build the x86-64 PC test build (build/mkw-host)
#
# Settings: CONSOLE=169.254.1.1  MKW_BUILDER=~/MKW-3DS-Builder
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
CONSOLE="${CONSOLE:-169.254.1.1}"
export MKW_BUILDER="${MKW_BUILDER:-$HOME/MKW-3DS-Builder}"
GAME="$MKW_BUILDER/build/release/sd-card/3ds/mkw-3ds"

say()  { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
have() { command -v "$1" >/dev/null 2>&1; }
console_up() { ssh -o ConnectTimeout=3 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "root@$CONSOLE" true 2>/dev/null; }
is_wsl() { grep -qi microsoft /proc/version 2>/dev/null; }

deps() {
  say "Installing build tools"
  local need=()
  have arm-linux-gnueabihf-g++ || need+=(g++-arm-linux-gnueabihf)
  have python3 || need+=(python3)
  have g++ || need+=(g++)
  have ssh || need+=(openssh-client)
  if [ ${#need[@]} -eq 0 ]; then echo "Already installed."; return; fi
  have apt-get || { echo "Please install: ${need[*]} (cross compiler: arm-linux-gnueabihf-g++)"; exit 1; }
  sudo apt-get update -qq
  sudo apt-get install -y "${need[@]}"
}

game_files() { say "Making the game files from your disc"; bash port/cook_content.sh "$@"; }
sysroot()    { say "Copying the console's libraries (build/sysroot)"; bash port/nesmini/fetch-sysroot.sh "$CONSOLE"; }

build() {
  have arm-linux-gnueabihf-g++ || deps
  if [ ! -f build/sysroot/libc.so.6 ]; then
    if ! is_wsl && console_up; then sysroot
    else echo -e "\nNote: build/sysroot is missing (console not reachable). The build works but may not\nload on the console. Plug the console in and run: bash setup.sh sysroot && bash setup.sh build"
    fi
  fi
  say "Building the console program"
  bash port/build.sh
  bash port/nesmini/build-padbridge.sh >/dev/null
}

install_menu() { say "Installing the menu entry on the console"; bash port/nesmini/easy-install/install-menu-linux.sh "$CONSOLE"; }

check() {
  ok()  { printf '  \033[32m[ok]\033[0m      %s\n' "$*"; }
  bad() { printf '  \033[33m[missing]\033[0m %s\n' "$*"; }
  say "Status"
  compgen -G 'rom/*.wbfs' >/dev/null || compgen -G 'rom/*.iso' >/dev/null && ok "disc image in rom/" || bad "disc image   -> copy your own Mario Kart Wii (USA) .wbfs/.iso into rom/"
  [ -f "$GAME/race.pak" ] && ok "game files ($GAME)" || bad "game files   -> bash setup.sh game-files"
  have arm-linux-gnueabihf-g++ && ok "cross compiler" || bad "cross compiler -> bash setup.sh deps"
  [ -f build/sysroot/libc.so.6 ] && ok "build/sysroot" || bad "build/sysroot -> plug in the console, bash setup.sh sysroot"
  [ -f build/mkw-nesmini.stripped ] && ok "console program" || bad "console program -> bash setup.sh build"
  console_up && ok "console reachable at $CONSOLE" || bad "console      -> plug it in with hakchi's USB cable and turn it on"
}

case "${1:-all}" in
  all)
    [ -f "$GAME/race.pak" ] && echo "Game files already made ($GAME)." || game_files
    deps
    build
    say "Next steps"
    echo "  1. Plug a USB drive into this PC and run:   bash setup.sh usb"
    echo "  2. Plug the console in and run:             bash setup.sh install"
    echo "  3. Move the USB drive to the console and restart it."
    check ;;
  check)      check ;;
  game-files) shift; game_files "$@" ;;
  deps)       deps ;;
  sysroot)    sysroot ;;
  build)      build ;;
  usb)        shift; [ -f build/mkw-nesmini.stripped ] || build; bash port/nesmini/easy-install/install-linux.sh "$@" ;;
  install)    [ -f build/mkw-nesmini.stripped ] || build; install_menu ;;
  host)       bash port/host.sh ;;
  *) sed -n '2,15p' "$0"; exit 1 ;;
esac
