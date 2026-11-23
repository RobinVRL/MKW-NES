#!/usr/bin/env python3
"""Convert a NES Mini /dev/fb0 dump (1280x720 visible, 32-bit BGRA) to PNG."""
import struct, sys, zlib
src = sys.argv[1] if len(sys.argv) > 1 else 'build/fb.raw'
dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit('.', 1)[0] + '.png'
W, H = 1280, 720
d = open(src, 'rb').read()
rows = bytearray()
for y in range(H):
    rows.append(0)
    line = d[y * W * 4:(y + 1) * W * 4]
    for x in range(W):
        b, g, r = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
        rows += bytes((r, g, b))
def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
                      + chunk(b'IDAT', zlib.compress(bytes(rows), 6)) + chunk(b'IEND', b''))
print(dst)
