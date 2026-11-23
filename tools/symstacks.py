#!/usr/bin/env python3
"""Turn raw thread stack dumps from the console (stk.sh) into likely call chains.

Scans each stack for words that point into mkw-nesmini's code and resolves
them with addr2line against the unstripped build. Heuristic: stale values on
the stack show up too, but the first hits from the top are usually the frames.
"""
import struct, subprocess, sys
D = sys.argv[1] if len(sys.argv) > 1 else r'D:\mkw'
BIN = '/mnt/c/Users/thedi/OneDrive/Pictures/Documents/MKW-NES/build/mkw-nesmini'
maps = []
for l in open(D + r'\maps.txt'):
    f = l.split()
    a, b = (int(x, 16) for x in f[0].split('-'))
    maps.append((a, b, int(f[2], 16), f[-1] if len(f) > 5 else '[anon]'))
game = [m for m in maps if m[3].endswith('mkw-nesmini')][0]
def where(v):
    for a, b, off, name in maps:
        if a <= v < b:
            return name.rsplit('/', 1)[-1], v - a + off
    return None
threads, wanted = [], set()
for l in open(D + r'\stacks.txt'):
    tid, st, sp, pc = l.split(); sp, pc = int(sp), int(pc)
    try:
        raw = open(fr'{D}\stk_{tid}.bin', 'rb').read()
    except OSError:
        continue
    raw = raw[sp % 4096:]
    hits = []
    for i in range(0, min(len(raw), 12288) - 3, 4):
        v = struct.unpack_from('<I', raw, i)[0]
        w = where(v & ~1)
        if w and w[0] == 'mkw-nesmini':
            hits.append(w[1] - 4)   # return address -> the call
            wanted.add(w[1] - 4)
    threads.append((tid, st, where(pc), hits[:14]))
names = {}
if wanted:
    order = sorted(wanted)
    out = subprocess.run(['wsl', '-e', 'arm-linux-gnueabihf-addr2line', '-f', '-C', '-s', '-e', BIN] + [hex(x) for x in order],
                         capture_output=True, text=True).stdout.splitlines()
    for i, x in enumerate(order):
        fn, loc = out[2 * i], out[2 * i + 1]
        names[x] = f'{fn[:70]} ({loc})'
for tid, st, pc, hits in threads:
    print(f'== thread {tid} [{st}] pc in {pc[0]}+{pc[1]:#x}' if pc else f'== thread {tid} [{st}]')
    seen = []
    for h in hits:
        n = names.get(h, '?')
        if n.startswith('??') or (seen and seen[-1] == n):
            continue
        seen.append(n)
        print('   ', n)
