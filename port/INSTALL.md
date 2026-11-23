# Installing on the NES / SNES Classic Mini

> [!WARNING]
> **Not yet tested on real hardware.** So far the port has only run in the PC
> test build. Treat the first run as an experiment, and keep your hakchi
> kernel backup somewhere safe.

## What you need

| Item | Notes |
|---|---|
| NES or SNES Classic Mini with **hakchi2 CE** | Keep the kernel backup that hakchi makes when you flash it. |
| **USB drive + OTG adapter** (or hakchi's USB-host setup) | The game files are about **2.9 GB**. The console has only about 300 MB free inside. |
| `build/mkw-nesmini.stripped` | The console program (about 6 MB). See [README.md](README.md#building-for-the-console). |
| `port/nesmini/mkw.sh` | The launcher script. |
| **Your own** game files | The `3ds/mkw-3ds` folder made from your own USA disc by the upstream builder. |
| An SSH client | Windows `ssh`/`scp` or WinSCP. hakchi makes the console available as `root@169.254.1.1` over USB, with no password. |

## Option A: menu entry (hakchi2 CE)

1. In hakchi2 CE, add a custom game folder (for example `CLV-I-WYKOJ`) containing:
   - `mkw-nesmini` (the stripped binary, renamed)
   - `mkw.sh`
   - a `.desktop` file with `Exec=/bin/sh /var/games/CLV-I-WYKOJ/mkw.sh`
2. Put the game files on a USB drive at `mkw/sdmc/3ds/mkw-3ds/` (see step 1 below).
3. Sync, then start **Mario Kart Wii** from the console's menu.

The launcher first looks for `sdmc/` next to itself, then for `mkw/sdmc` on a
USB drive under `/media`. It writes its log to `mkw.log` next to itself (or to
`/tmp/mkw.log`).

## Option B: manual install over SSH

### 1. Prepare the USB drive (on the PC)

1. Format the drive as **ext4**. This is recommended because the binary needs
   Unix file permissions. exFAT or FAT32 also work if your hakchi build mounts
   them; the largest file, `race.pak`, is about 1.5 GB, so it fits within FAT32's 4 GB file limit.
2. Make this layout:
   ```
   mkw/
     mkw-nesmini          <- build/mkw-nesmini.stripped, renamed
     mkw.sh               <- port/nesmini/mkw.sh
     sdmc/3ds/mkw-3ds/    <- the whole mkw-3ds folder from the builder
   ```
   Copy `mkw-3ds` **as a whole folder**, including `menu.pak`, `race.pak`,
   `manifest.json`, `audio/`, `models/`, `textures/` and everything else.

### 2. Connect

1. Connect the console to the PC with hakchi's USB cable and turn it on.
2. Plug the USB drive into the console.
3. `ssh root@169.254.1.1`
4. Find where the drive is mounted, then save that path in a variable:
   ```sh
   mount | grep -E '/media|/mnt'
   USB=/media          # use the path you found
   ```

### 3. Make it runnable

```sh
cd $USB/mkw
chmod +x mkw-nesmini mkw.sh
ls sdmc/3ds/mkw-3ds/race.pak     # this file must exist
```

If `chmod` fails, the drive is FAT/exFAT. Copy the two small files to internal
storage instead:

```sh
mkdir -p /var/lib/hakchi/mkw
cp $USB/mkw/mkw-nesmini $USB/mkw/mkw.sh /var/lib/hakchi/mkw/
chmod +x /var/lib/hakchi/mkw/*
MKW_SDMC=$USB/mkw/sdmc /var/lib/hakchi/mkw/mkw.sh
```

### 4. Run it

```sh
$USB/mkw/mkw.sh
```

The launcher stops the console menu (`uistop`), runs the game on the TV, and
starts the menu again (`uistart`) when the game exits. Saves and the game's
`runtime.log` go into `sdmc/3ds/mkw-3ds/`.

**If it hangs:** from SSH, run `killall mkw-nesmini; uistart`.

## Controls

Use a controller. The 3DS buttons map to the nearest controller buttons. Without an analog stick, the
D-pad steers. Touch is only used to switch the lower-screen race view, which
is hidden by default. See the controls table in
[`../src3ds/README.md`](../src3ds/README.md#controls).

## Options

Set these before the launcher, for example `MKW_SHOW=both $USB/mkw/mkw.sh`.

| Variable | Effect |
|---|---|
| `MKW_SHOW=top\|both\|bottom` | Screen layout (default `top`) |
| `MKW_SDMC=<dir>` | Game files folder (default `./sdmc` next to the binary) |
| `MKW_USERNAME=<name>` | Player name |
| `MKW_LINEAR_MB=<n>` | Graphics/audio memory pool (default 64). Lower it if the game runs out of memory. |
| `MKW_FB_WIDTH` / `MKW_FB_HEIGHT` | Override the detected TV resolution |
| `MKW_NO_VSYNC=1`, `MKW_NO_ETC1=1` | Troubleshooting |
| `MKW_HIDLOG=` | Set it empty to stop logging controller events |

## Troubleshooting

When you report a problem, include `mkw.log` and `sdmc/3ds/mkw-3ds/runtime.log`.

| What you see | Likely cause / fix |
|---|---|
| "content: SD files missing" | `MKW_SDMC` points to the wrong folder. It must contain `3ds/mkw-3ds/`. |
| Killed / out of memory | The console has only 256 MB of RAM. Try `MKW_LINEAR_MB=48`. |
| `no EGL config` or a black screen | Mali driver problem. The log has the details. |
| `not found` when starting the binary | The drive is mounted `noexec`. Use the internal-storage method in step 3. |
| Binary won't load (symbol errors) | It was built without `build/sysroot`. Rebuild with the console's libraries. |

## Uninstalling

Delete the `mkw` folder from the drive, plus `/var/lib/hakchi/mkw` if you used
it, or remove the game in hakchi2 CE. Nothing else on the console is changed.
