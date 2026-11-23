#!/usr/bin/env python3
"""Mean brightness of the top area and the inset area of fb dumps f1..f12.raw
(1280x720 BGRA) - big jumps between consecutive grabs mean flicker."""
import sys
D = sys.argv[1] if len(sys.argv) > 1 else r'D:\mkw'
W, H = 1280, 720
def mean(buf, x0, y0, x1, y1):
    s = n = 0
    for y in range(y0, y1, 8):
        row = buf[(y * W) * 4:(y * W + W) * 4]
        for x in range(x0, x1, 8):
            p = x * 4
            s += row[p] + row[p + 1] + row[p + 2]; n += 3
    return s / n
for i in range(1, 13):
    try:
        b = open(fr'{D}\f{i}.raw', 'rb').read()
    except OSError:
        continue
    print(f'f{i:2}: main {mean(b, 60, 300, 900, 700):6.1f}   inset {mean(b, 925, 20, 1260, 270):6.1f}')
