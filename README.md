# MKW-NES: Mario Kart Wii 3DS on the NES / SNES Classic Mini

This is a port of the fan-made [Mario Kart Wii 3DS](https://github.com/MiniBandit/MKW-3DS)
project to the **NES Classic Mini** and **SNES Classic Mini**, which run Linux.
The original 3DS game code isn't changed. Instead, this project swaps out the 3DS
system libraries (libctru and citro3d) for versions that run on the console's
Linux system and Mali-400 GPU.

> [!IMPORTANT]
> **This repository has no Nintendo files in it**: no disc image, textures,
> models, sounds, logos, icons or map markers. You make the game files yourself
> from **your own** Mario Kart Wii (USA) disc. Please don't open issues or pull
> requests that contain game files, and don't share the files you build.

## Repository layout

| Path | What it is |
|---|---|
| [`port/`](port/README.md) | The Linux port: libctru/citro3d replacements, build scripts, console launcher and test tools |
| [`port/INSTALL.md`](port/INSTALL.md) | Step-by-step guide to installing it on the console |
| [`src3ds/`](src3ds/README.md) | The upstream Mario Kart Wii 3DS sources and the game-file builder |
| `ref_libctru/` | A reference copy of libctru, used to check API behaviour (not compiled) |
| `tools/` | Small debugging helpers (screenshot montages, symbol stacks, framebuffer scans) |
| `rom/` | *(not in Git)* Put your own `.wbfs` / `.iso` here |
| `build/` | *(not in Git)* Build output |

## Quick start

1. **Make the game files** from your disc using the upstream builder. See
   [src3ds/README.md](src3ds/README.md#building), or on Windows run
   `port/cook_content.ps1`. You get a `3ds/mkw-3ds/` folder of about 3 GB.
2. **Build the console binary** in WSL or Linux with an ARM hard-float cross compiler:
   ```bash
   sudo apt install g++-arm-linux-gnueabihf python3
   bash port/build.sh          # -> build/mkw-nesmini.stripped
   ```
3. **Install it** on a hakchi CE console by following [port/INSTALL.md](port/INSTALL.md).

To test on a PC without the console, `bash port/host.sh` builds `build/mkw-host`.
It's a native x86-64 version that renders headlessly through Mesa EGL. See
[port/README.md](port/README.md#pc-test-build).

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
