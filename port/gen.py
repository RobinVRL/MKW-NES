#!/usr/bin/env python3
"""Generate the two build-time headers tools/build.py normally writes.

shader_data.hpp: on the NES Mini there is no PICA200, so the "shbin" is a small
tag blob. port/c3d/shader_gles.cpp's DVLB_ParseFile recognises it and exposes
three DVLEs in the same order picasso would (0 scene, 1 effect, 2 Mii), backed
by GLSL translations of src/mkw.v.pica, src/effect.v.pica and tools/mii_shader.py.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src3ds"
OUT = ROOT / "build/gen"
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(SRC / "tools/release"))
from build_cia import content_fingerprint, legacy_windows_fingerprint  # noqa: E402

news = [l.rstrip() for l in (SRC / "tools/release/WHATS_NEW.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
stamp = (
    "#pragma once\n"
    f'#define MKW_CONTENT "{content_fingerprint()}"\n'
    f'#define MKW_CONTENT_WINDOWS_LEGACY "{legacy_windows_fingerprint()}"\n'
    f'#define MKW_VERSION "{(SRC / "tools/release/VERSION").read_text().strip()}-nesmini"\n'
    '#define MKW_WHATS_NEW "' + "\\n".join(news) + '"\n'
    "#define MKW_QUICK_UPDATE 0\n"
)
(OUT / "content_version.hpp").write_text(stamp)

# magic + DVLE count + first program id (0 scene, 1 effect, 2 Mii, 3 CFL)
tag = b"GLESDVLB\x03\x00\x00\x00\x00\x00\x00\x00"
(OUT / "shader_data.hpp").write_text(
    "#pragma once\n#include <cstdint>\n"
    "alignas(4) static const uint8_t mkw_shader[]={" + ",".join(map(str, tag)) + "};\n"
)
cfl = b"GLESDVLB\x01\x00\x00\x00\x03\x00\x00\x00"
(OUT / "vshader_shbin.h").write_text(
    "#pragma once\n#include <stdint.h>\n"
    "static const uint8_t vshader_shbin[] __attribute__((aligned(4)))={" + ",".join(map(str, cfl)) + "};\n"
    f"static const uint32_t vshader_shbin_size={len(cfl)};\n"
)
print("generated", OUT)
