MARIO KART WII (NES/SNES CLASSIC MINI PORT) - EASY MENU INSTALL
================================================================

This package contains NO Nintendo game files. You need your own game files,
built from your own Mario Kart Wii (USA) disc with the MKW-3DS builder
(from the repository: setup.bat game-files, or bash setup.sh game-files).

WINDOWS (game files stay on this PC)
  1. Mod the console with hakchi2 CE, plug it into this PC with its USB
     cable, and turn it on.
  2. Right-click Setup-PC-Share.ps1 > Run with PowerShell. Point it at the
     folder that contains 3ds\mkw-3ds:
         Setup-PC-Share.ps1 -Path C:\MKW-3DS-Builder\build\release\sd-card
     Restart Windows if it tells you to (first time only).
  3. Double-click Install.bat and type your Windows user name and password.
  4. Restart the console: "Mario Kart Wii" is in the menu.
  Keep the PC on and connected while you play.

LINUX (game files on a USB drive)
  From the repository root:
      bash setup.sh usb        copy the program + game files to a USB drive
      bash setup.sh install    add the menu entry over the USB cable
  Plug the drive into the console and restart it.

WHAT IT DOES
  - Copies the game program to the console's permanent storage
    (/var/lib/hakchi/mkw), so it survives power-offs.
  - Windows only: adds a boot script (/etc/init.d/S95mkwpc) that mounts the
    PC share at /media/pc each time the console starts.
  - Adds the menu entry (code CLV-I-WYKOJ). Its icon is the official cover,
    downloaded from GameTDB (art.gametdb.com) while installing. It is not
    included here. Offline, a generic icon is used instead.

NOTES
  - Syncing games in hakchi2 CE can remove the menu entry. Just install again.
  - Different share name?  Install.bat -Share NAME
  - Log files on the console: /tmp/mkw-mount.log and mkw.log

UNINSTALL (over SSH: ssh root@169.254.1.1)
  rm -rf /var/lib/hakchi/mkw /etc/init.d/S95mkwpc /etc/mkw-pc.conf \
         /var/lib/hakchi/games/nes-usa/000/CLV-I-WYKOJ
