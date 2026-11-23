# Building from your disc

Everything Nintendo-made is generated on your own computer from **your own**
Mario Kart Wii (USA, `RMCE01`) disc. None of it is stored in this repository.

## Steps

1. Read the [README build guide](../README.md#building) for the one-click builders.
2. For a step-by-step build, follow [Manual build operations](MANUAL-BUILD.md).
3. Look up individual stages in the [resource-stage reference](MANUAL-RESOURCE-STAGES.md).

The game program, shaders, libraries, resources and CIA are built in separate
manual steps. There is no automatic download or single build-everything command.

## Files you supply yourself

| File | Where it goes | Notes |
|---|---|---|
| Disc image (`.iso` / `.wbfs`) | `mkw-rom/` or a path you choose | USA `RMCE01` only |
| `CFL_Res.dat` | next to the builder | Your own console's Mii resource |
| `icon.png`, `logo.png`, `banneraudio.wav` | `tools/release/artwork/` | HOME Menu icon, logo and banner sound. Not included in this repository. |
| Map marker PNGs | `tools/release/mk7_markers/` | See that folder's README. Not included in this repository. |

Generate the HOME banner model from your disc as described in
[HOME-BANNER.md](HOME-BANNER.md).

> The manual procedure hasn't been fully re-checked from a fresh disc yet.
