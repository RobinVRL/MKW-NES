# Installing on the NES / SNES Classic Mini

> [!WARNING]
> **Not yet tested on real hardware.** So far the port has only run in the PC
> test build. Treat the first run as an experiment, and keep your hakchi
> kernel backup somewhere safe.

## What you need

- A NES or SNES Classic Mini with **hakchi2 CE**, connected with hakchi's USB cable.
  hakchi makes the console available as `root@169.254.1.1` with no password.
- Your own **Mario Kart Wii (USA)** disc image (`RMCE01`, `.wbfs` or `.iso`) in `rom/`.
  Convert RVZ/NKit dumps to ISO in Dolphin first.
- About 30 GB free for the build. The finished game files are about 3 GB.
- **Linux only:** a USB drive (3 GB or more, ext4 recommended) and an OTG adapter for the console.

## The easy way

### Windows: game files stay on the PC

```bat
setup.bat
```

That's it. When it finishes, restart the console and pick **Mario Kart Wii**
from the menu. The console reads the game files from your PC over the USB
cable, so the PC has to be on and connected while you play.

What happens:

| Step | Command | What it does |
|---|---|---|
| Game files | `setup.bat game-files` | Copies `src3ds` to `C:\MKW-3DS-Builder` and runs the upstream builder on `rom\*.wbfs`. Resumable. |
| Build | `setup.bat build` | Copies the console's libraries to `build\sysroot` (if it's plugged in), then builds in WSL. Installs the cross compiler the first time. |
| Share | `setup.bat share` | Asks for admin rights and shares `C:\MKW-3DS-Builder\build\release\sd-card` as `\\PC\MKW`. Also marks the USB link as a private network and turns on SMB1, which the console's kernel needs. **Restart Windows the first time** if it says so. |
| Install | `setup.bat install` | Asks for your Windows user name and password (so the console can open the share), then adds the menu entry and a boot script that mounts the share. |

To give the installer to someone else, run `setup.bat package`. It creates
`dist\MKW-NES-Menu-Installer.zip`, which they can unzip and double-click
`Install.bat`. They still need to make their own game files.

### Ubuntu / Debian Linux: game files on a USB drive

```bash
bash setup.sh            # makes the game files and builds the program
bash setup.sh usb        # copies the program and game files to a USB drive
bash setup.sh install    # adds the menu entry over SSH
```

Plug the USB drive into the console and restart it. The launcher finds the
game files at `mkw/sdmc/3ds/mkw-3ds` on any USB drive.

`bash setup.sh usb` options:

| Option | Meaning |
|---|---|
| `--device /dev/sdX` | Which drive to use (default: asks) |
| `--format-ext4` | Wipe the drive and format it as ext4 first, then run again without it |
| `--game-dir DIR` | Folder containing `3ds/mkw-3ds` (default: the builder's `sd-card`) |

## The manual way (SSH, any OS)

Use this if the scripts don't fit your setup.

1. Make this layout on a USB drive (ext4 recommended):
   ```
   mkw/
     mkw-nesmini          <- build/mkw-nesmini.stripped, renamed
     mkw.sh               <- port/nesmini/mkw.sh
     sdmc/3ds/mkw-3ds/    <- the whole mkw-3ds folder from the builder
   ```
2. Plug the drive into the console, then `ssh root@169.254.1.1`.
3. Find the drive and run the game:
   ```sh
   mount | grep -E '/media|/mnt'     # find where the drive is mounted
   USB=/media/sda1                   # use the path you found
   chmod +x $USB/mkw/mkw-nesmini $USB/mkw/mkw.sh
   $USB/mkw/mkw.sh
   ```

If `chmod` fails or you get `not found`, the drive is FAT/exFAT or mounted
`noexec`. Copy the two small files to internal storage instead:

```sh
mkdir -p /var/lib/hakchi/mkw
cp $USB/mkw/mkw-nesmini $USB/mkw/mkw.sh /var/lib/hakchi/mkw/
chmod +x /var/lib/hakchi/mkw/*
/var/lib/hakchi/mkw/mkw.sh
```

The launcher stops the console menu, runs the game, and starts the menu again
when the game exits. **If it hangs:** run `killall mkw-nesmini; uistart` over SSH.

## Where the launcher looks for game files

In this order, using the first one that contains `3ds/mkw-3ds`:
`$MKW_SDMC`, `sdmc/` next to the launcher, the PC share (`/media/pc`), then
`mkw/sdmc` on any USB drive under `/media`.

## Controls

Use a controller. The 3DS buttons map to the nearest controller buttons, and
the D-pad steers. See the controls table in
[`../src3ds/README.md`](../src3ds/README.md#controls).

## Options

Set these before the launcher, for example `MKW_SHOW=both /var/lib/hakchi/mkw/mkw.sh`.

| Variable | Effect |
|---|---|
| `MKW_SHOW=top\|both\|bottom` | Screen layout (default `top`) |
| `MKW_SDMC=<dir>` | Game files folder (must contain `3ds/mkw-3ds`) |
| `MKW_USERNAME=<name>` | Player name |
| `MKW_LINEAR_MB=<n>` | Graphics/audio memory pool (default 64). Lower it if the game runs out of memory. |
| `MKW_FB_WIDTH` / `MKW_FB_HEIGHT` | Override the detected TV resolution |
| `MKW_NO_VSYNC=1`, `MKW_NO_ETC1=1` | Troubleshooting |
| `MKW_HIDLOG=` | Set it empty to stop logging controller events |

## Troubleshooting

Run `setup.bat check` or `bash setup.sh check` first. When you report a
problem, include `mkw.log`, `/tmp/mkw-mount.log` and `3ds/mkw-3ds/runtime.log`.

| What you see | Likely cause / fix |
|---|---|
| "Console not found" | Plug in hakchi's USB cable, turn the console on, wait 30 seconds. |
| Menu entry missing after a hakchi sync | Syncing can remove it. Run the install step again. |
| "game files not found" in `mkw.log` | Windows: the PC is off, or the share didn't mount (see `/tmp/mkw-mount.log`; restart Windows once after the share step). Linux: the drive isn't plugged in, or the layout isn't `mkw/sdmc/3ds/mkw-3ds`. |
| Killed / out of memory | The console has only 256 MB of RAM. Try `MKW_LINEAR_MB=48`. |
| `no EGL config` or a black screen | Mali driver problem. The log has the details. |
| Binary won't load (symbol errors) | It was built without `build/sysroot`. Plug the console in and run the build step again. |
| Game-file build stops | Run it again: it resumes. Otherwise see `build-cia-log.txt` in the builder folder. |

## Uninstalling

Over SSH (`ssh root@169.254.1.1`):

```sh
rm -rf /var/lib/hakchi/mkw /etc/init.d/S95mkwpc /etc/mkw-pc.conf \
       /var/lib/hakchi/games/nes-usa/000/CLV-I-WYKOJ /var/saves/CLV-I-WYKOJ
```

On Windows, also remove the share: `Remove-SmbShare -Name MKW` (admin PowerShell).
