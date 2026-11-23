#!/usr/bin/env python3
"""Convert build/frames/*.ppm (test-build frame dumps) to PNG, stdlib only."""
import glob, struct, sys, zlib
from pathlib import Path

def chunk(tag, body):
    return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body))

def convert(src: Path):
    data = src.read_bytes()
    magic, dims, _maxval, px = data.split(b'\n', 3)
    w, h = map(int, dims.split())
    rows = b''.join(b'\0' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b'')
    src.with_suffix('.png').write_bytes(png)

folder = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'build' / 'frames')
for f in sorted(folder.glob('*.ppm')):
    convert(f)
print(len(list(folder.glob('*.png'))), 'png')
