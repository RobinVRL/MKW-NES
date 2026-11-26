# MKW-NES: Mario Kart Wii 3DS on the NES / SNES Classic Mini

This is a port of the fan-made [Mario Kart Wii 3DS](https://github.com/MiniBandit/MKW-3DS)
project to the **NES Classic Mini** and **SNES Classic Mini**, which run Linux.
The original 3DS game code isn't changed. Instead, this project swaps out the 3DS
system libraries (libctru and citro3d) for versions that run on the console's
Linux system and Mali-400 GPU.

> [!WARNING]
> This port is **AI Assisted**
> Not everything is functional currently. There is no online play.

> [!IMPORTANT]
> **This repository has no Nintendo files in it**: no disc image, textures,
> models, sounds, logos, icons or map markers. You make the game files yourself
> from **your own** Mario Kart Wii (USA) disc. Please don't open issues or pull
> requests that contain game files, and don't share the files you build.

## Repository layout

| Path | What it is |
|---|---|
| `setup.bat` / `setup.sh` | One-step setup for Windows / Linux |
| [`port/`](port/README.md) | The Linux port: libctru/citro3d replacements, build scripts, console launcher and test tools |
| [`port/INSTALL.md`](port/INSTALL.md) | Step-by-step guide to installing it on the console |
| [`src3ds/`](src3ds/README.md) | The upstream Mario Kart Wii 3DS sources and the game-file builder |
| `ref_libctru/` | A reference copy of libctru, used to check API behaviour (not compiled) |
| `tools/` | Small debugging helpers (screenshot montages, symbol stacks, framebuffer scans) |
| `rom/` | *(not in Git)* Put your own `.wbfs` / `.iso` here |
| `build/` | *(not in Git)* Build output |

## Quick start

You need: a console modded with **hakchi2 CE**, its USB cable, and your own
**Mario Kart Wii (USA)** disc image (`.wbfs` or `.iso`). Copy the disc image
into the `rom/` folder first.

### Windows

Double-click **`setup.bat`**. It:

1. makes the game files from your disc (the first run takes a few hours),
2. builds the console program (in WSL, which it uses automatically),
3. shares the game files with the console over the USB cable, and
4. adds **Mario Kart Wii** to the console's menu.

The console plays the game files straight from your PC, so keep the PC on
while you play. You need [WSL](https://learn.microsoft.com/windows/wsl/install)
(`wsl --install -d Ubuntu`), [devkitPro](https://devkitpro.org/wiki/Getting_Started),
Python 3.11–3.14, Node.js LTS and Git. `setup.bat check` lists anything
that's missing and how to fix it.

### Ubuntu / Debian Linux

```bash
bash setup.sh            # game files + console program (installs what it needs with apt)
bash setup.sh usb        # copy everything to a USB drive plugged into this PC
bash setup.sh install    # add the menu entry (console plugged in over USB)
```

Then plug the USB drive into the console and restart it. On Linux the game
files go on a USB drive rather than a PC share.

### Individual steps

Each step can be run on its own. Finished steps are skipped or resumed.

| Step | Windows | Linux |
|---|---|---|
| Show what's ready | `setup.bat check` | `bash setup.sh check` |
| Make game files | `setup.bat game-files` | `bash setup.sh game-files` |
| Build console program | `setup.bat build` | `bash setup.sh build` |
| Share game files with console | `setup.bat share` | — (use `usb`) |
| Copy to USB drive | — | `bash setup.sh usb` |
| Add menu entry | `setup.bat install` | `bash setup.sh install` |
| PC test build (x86-64) | — | `bash setup.sh host` |

The game files are built in `C:\MKW-3DS-Builder` (Windows) or
`~/MKW-3DS-Builder` (Linux), outside synced folders like OneDrive. Set
`MKW_BUILDER` to use another folder. See [port/INSTALL.md](port/INSTALL.md)
for manual installs, options and troubleshooting.

## Credits

All of the game work comes from the upstream project and the people credited in
[src3ds/README.md](src3ds/README.md#credits): dudewhat, MiniBandit, Epic, the
Kinoko developers, noclip.website, gclib, devkitPro/libctru/citro3d and the CFL
contributors. This repository adds the Linux platform layer in `port/`.

## Legal

This is an unofficial fan project. It isn't affiliated with or endorsed by
Nintendo. *Mario Kart* and *Nintendo* are trademarks of Nintendo. The code is
covered by the licences in [`src3ds/LICENSE`](src3ds/LICENSE),
[`src3ds/NOTICE`](src3ds/NOTICE) and `src3ds/licenses/`.
