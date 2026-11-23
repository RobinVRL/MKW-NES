#!/usr/bin/env python3
"""Shrink fb dumps w1..w5.raw (1280x720 BGRA) into one PNG strip: montage.py <dir> <out.png>"""
import sys, zlib, struct
D, out = sys.argv[1], sys.argv[2]
W, H, S = 1280, 720, 4
w, h = W // S, H // S
rows = []
frames = []
for i in range(1, 6):
    try: frames.append(open(fr'{D}\w{i}.raw', 'rb').read())
    except OSError: pass
for y in range(h):
    r = bytearray([0])
    for b in frames:
        base = (y * S) * W * 4
        for x in range(w):
            p = base + x * S * 4
            r += bytes((b[p + 2], b[p + 1], b[p]))
    rows.append(bytes(r))
raw = b''.join(rows)
tw = w * len(frames)
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', tw, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print(out)
