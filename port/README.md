# port/: Linux platform layer

This folder builds the unmodified Mario Kart Wii 3DS sources (`../src3ds`) as an
**ARMv7-A hard-float Linux** program for the NES/SNES Classic Mini
(Allwinner R16, Cortex-A7, Mali-400, glibc 2.23).

The game is written for the 3DS libraries libctru and citro3d. This folder
provides replacements that keep the same function names:

| Folder | Replaces | How |
|---|---|---|
| `ctru/` | libctru | OS, threads, files/RomFS, HID through Linux `evdev`, NDSP audio |
| `c3d/` | citro3d | The citro3d API rebuilt on **GLES2/EGL**, including PICA200 TEV → GLSL shader generation, texture untiling and ETC1 |
| `include/` | — | GLES2, EGL and KHR headers |
| `compat/` | — | `glibc_compat.c` sends newer glibc symbols to the console's glibc 2.23 |
| `host/` | — | Shims and a crash handler for the PC test build |
| `nesmini/` | — | Console launcher (`mkw.sh`), controller bridge, framebuffer capture and symbol checks |
| `tests/` | — | Small probes for the backend |

## Requirements

- Linux or WSL (Ubuntu recommended)
- `arm-linux-gnueabihf-g++` (`sudo apt install g++-arm-linux-gnueabihf`)
- Python 3, used by `gen.py` to generate headers into `build/gen`
- **Optional but strongly recommended:** `build/sysroot/` holding
  `libc.so.6`, `libpthread.so.0`, `libdl.so.2` and `librt.so.1` copied from
  your own console. Without them the binary links, but it won't load on the
  console. The build prints a warning if they're missing.

## Building for the console

```bash
bash port/build.sh            # -> build/mkw-nesmini and build/mkw-nesmini.stripped
```

| Variable | Default | Meaning |
|---|---|---|
| `OPT` | `-O2` | Optimisation flags |
| `JOBS` | `nproc` | Parallel compile jobs |
| `CLEAN=1` | | Delete the objects and rebuild everything |
| `FORCE=1` | | Recompile even if objects are up to date |
| `CROSS` | `arm-linux-gnueabihf-` | Toolchain prefix |

libstdc++ and libgcc are linked statically. **The build fails if any symbol
needs a glibc newer than 2.23**, so a binary that builds will also load on
the console.

## PC test build

```bash
bash port/host.sh             # -> build/mkw-host (x86-64, Mesa EGL, headless)
bash port/host/run.sh         # run it against your game files
```

This build exists only to test the renderer on a desktop. It isn't shipped.

## Game files

The binary reads the same `3ds/mkw-3ds/` folder as the 3DS version. Make it
from your own disc with the upstream builder (see
[`../src3ds/README.md`](../src3ds/README.md#building)). On Windows,
`cook_content.ps1` runs that builder non-interactively. **No Nintendo assets
are included in this repository.**

## Running

See **[INSTALL.md](INSTALL.md)** for the full console guide. In short:

```sh
MKW_SDMC=/path/to/sdmc ./mkw-nesmini     # sdmc must contain 3ds/mkw-3ds/
```

By default the 3DS **upper screen** is scaled to fill the TV. The lower screen
is still rendered, so you can show it with `MKW_SHOW`.

| Variable | Effect |
|---|---|
| `MKW_SDMC` | Folder that contains `3ds/mkw-3ds/` |
| `MKW_ROMFS` | Override the RomFS root |
| `MKW_SHOW=top\|bottom\|both` | Screen layout (default `top`) |
| `MKW_FB_WIDTH` / `MKW_FB_HEIGHT` | Override the detected output resolution |
| `MKW_LINEAR_MB` | Size of the graphics/audio memory pool (default 64) |
| `MKW_USERNAME` | Player name |
| `MKW_NO_VSYNC=1`, `MKW_NO_ETC1=1` | Troubleshooting switches |
| `MKW_HIDLOG` | Log controller events (the launcher turns this on) |

## Debugging helpers

- `nesmini/fbstream.py`, `nesmini/fb2png.py`: capture the console framebuffer
- `nesmini/padbridge.c`, `nesmini/padsend.py`: send PC keyboard input to the console
- `nesmini/checksyms.sh`, `undefs.sh`: find missing or too-new symbols
- `probe_all.sh`, `tests/probe.cpp`: backend probes
