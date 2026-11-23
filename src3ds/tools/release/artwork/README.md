# HOME Menu artwork

The packaging step reads these files from this folder. **They aren't included in
this repository**, because they're Nintendo-owned art. Provide your own:

| File | Purpose |
|---|---|
| `icon.png` | HOME Menu icon |
| `logo.png` | Banner logo |
| `banneraudio.wav` | Banner sound |
| `manifest.json` | SHA-256 fingerprints and audio format details (included). If you use different files, the fingerprints won't match; update or regenerate the manifest. |

Don't add CGFX models, banner binaries, texture dumps, Mii resources
(`CFL_Res.dat`) or saves here. Generate the banner model from your own disc
outside the repository; see [docs/HOME-BANNER.md](../../../docs/HOME-BANNER.md).
