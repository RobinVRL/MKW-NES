#!/usr/bin/env python3
"""Live view of the NES Mini screen on the PC (screenshare instead of screenshots).

Streams /dev/fb0 over hakchi's USB network link and shows it in a window.
Nothing is installed on the console and the kernel is untouched. Frames are
compressed on the console with its built-in lzop (about 10 fps); --raw sends
uncompressed frames instead (about 1.7 fps).
Usage: python fbstream.py [--host 169.254.13.37] [--scale 0.75] [--raw]
Needs numpy and Pillow.
"""
import argparse, struct, subprocess, threading, time, tkinter as tk
import numpy as np
from PIL import Image, ImageTk

W, H = 1280, 720
FRAME = W * H * 4


def lzo1x_decompress(src, out):
    """Append the LZO1X decompression of src to the bytearray out."""
    ip, n, state = 0, len(src), 0

    def literals(ip, t):
        out.extend(src[ip:ip + t])
        return ip + t

    def zeros(ip, base):
        length = 0
        while src[ip] == 0:
            length += 255
            ip += 1
        return ip + 1, length + base + src[ip]

    if src[0] > 17:
        t = src[0] - 17
        ip = literals(1, t)
        state = t if t < 4 else 4
    while ip < n:
        t = src[ip]; ip += 1
        if t < 16:
            if state == 0:
                if t == 0:
                    ip, t = zeros(ip, 15)
                ip = literals(ip, t + 3)
                state = 4
                continue
            nxt = t & 3
            if state != 4:
                m = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
                length = 2
            else:
                m = len(out) - 0x801 - (t >> 2) - (src[ip] << 2); ip += 1
                length = 3
        elif t >= 64:
            nxt = t & 3
            m = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1
            length = (t >> 5) + 1
        elif t >= 32:
            length = t & 31
            if length == 0:
                ip, length = zeros(ip, 31)
            length += 2
            v = src[ip] | (src[ip + 1] << 8); ip += 2
            m = len(out) - 1 - (v >> 2)
            nxt = v & 3
        else:
            m = len(out) - ((t & 8) << 11)
            length = t & 7
            if length == 0:
                ip, length = zeros(ip, 7)
            length += 2
            v = src[ip] | (src[ip + 1] << 8); ip += 2
            m -= v >> 2
            nxt = v & 3
            if m == len(out):
                return
            m -= 0x4000
        dist = len(out) - m
        if dist >= length:
            out.extend(out[m:m + length])
        else:
            pat = out[m:]
            out.extend((pat * (length // dist + 1))[:length])
        state = nxt
        if nxt:
            ip = literals(ip, nxt)


def read_exact(f, k):
    b = bytearray()
    while len(b) < k:
        c = f.read(k - len(b))
        if not c:
            raise EOFError
        b += c
    return bytes(b)


def read_lzop_frame(f):
    """Read one complete .lzo stream (as written by `lzop -c`) and return its data."""
    u32 = lambda: struct.unpack('>I', read_exact(f, 4))[0]
    if read_exact(f, 9) != b'\x89LZO\x00\r\n\x1a\n':
        raise ValueError('lost sync with lzop stream')
    version = struct.unpack('>H', read_exact(f, 2))[0]
    read_exact(f, 2)                      # library version
    if version >= 0x0940:
        read_exact(f, 2)                  # version needed
    read_exact(f, 1)                      # method
    if version >= 0x0940:
        read_exact(f, 1)                  # level
    flags = u32()
    if flags & 0x800:
        u32()                             # filter
    read_exact(f, 8 if version < 0x0940 else 12)  # mode, mtime
    read_exact(f, read_exact(f, 1)[0])    # file name
    u32()                                 # header checksum
    out = bytearray()
    while True:
        dst_len = u32()
        if dst_len == 0:
            return out
        src_len = u32()
        for bit in (0x1, 0x100):          # adler32 / crc32 of the data
            if flags & bit:
                u32()
        if src_len < dst_len:
            for bit in (0x2, 0x200):      # checksums of the compressed data
                if flags & bit:
                    u32()
        block = read_exact(f, src_len)
        if src_len == dst_len:
            out += block
        else:
            lzo1x_decompress(block, out)


ap = argparse.ArgumentParser()
ap.add_argument('--host', default='169.254.13.37')
ap.add_argument('--scale', type=float, default=0.75)
ap.add_argument('--raw', action='store_true', help='send uncompressed frames')
a = ap.parse_args()

# Runs on the console: send the currently displayed buffer (follows page flips).
pipe = '' if a.raw else ' | lzop -1 -c'
REMOTE = (
    "while :; do "
    "o=$(cut -d, -f2 /sys/class/graphics/fb0/pan 2>/dev/null); o=${o:-0}; "
    f"dd if=/dev/fb0 bs={FRAME} skip=$((o/{H})) count=1 2>/dev/null{pipe}; "
    "done"
)
proc = subprocess.Popen(
    ['ssh', '-o', 'StrictHostKeyChecking=no', '-o', 'Compression=no', f'root@{a.host}', REMOTE],
    stdout=subprocess.PIPE, bufsize=0)

latest = [None]
def reader():
    try:
        while True:
            f = read_exact(proc.stdout, FRAME) if a.raw else read_lzop_frame(proc.stdout)
            if len(f) == FRAME:
                latest[0] = bytes(f)
    except (EOFError, ValueError) as e:
        print('stream ended:', e or 'disconnected')
        latest[0] = 'eof'
threading.Thread(target=reader, daemon=True).start()

root = tk.Tk()
root.title('NES Mini screen')
size = (int(W * a.scale), int(H * a.scale))
label = tk.Label(root, bg='black', width=size[0], height=size[1])
label.pack()
shown, t0 = 0, time.time()

def tick():
    global shown, t0
    f, latest[0] = latest[0], None
    if f == 'eof':
        root.title('NES Mini screen - disconnected')
        return
    if f:
        bgra = np.frombuffer(f, np.uint8).reshape(H, W, 4)
        img = Image.fromarray(bgra[:, :, 2::-1].copy(), 'RGB').resize(size)
        label.img = ImageTk.PhotoImage(img)
        label.configure(image=label.img, width=size[0], height=size[1])
        shown += 1
    if time.time() - t0 >= 1:
        root.title(f'NES Mini screen - {shown / (time.time() - t0):.1f} fps')
        shown, t0 = 0, time.time()
    root.after(10, tick)

root.protocol('WM_DELETE_WINDOW', lambda: (proc.kill(), root.destroy()))
tick()
root.mainloop()
