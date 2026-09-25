#!/usr/bin/env python3
"""Make !Sprites for !MIDISynth: a quaver above piano keys.
32bpp new-format sprites (RISC OS 3.5+) with a 1bpp mask."""
import struct, sys

def draw(n):
    # returns n*n list of (r,g,b) or None (transparent), drawn on a 34 grid
    s = n / 34.0
    px = [[None] * n for _ in range(n)]
    def rect(x0, y0, x1, y1, c):
        for y in range(int(y0 * s), int(y1 * s)):
            for x in range(int(x0 * s), int(x1 * s)):
                if 0 <= x < n and 0 <= y < n:
                    px[y][x] = c
    def ellipse(cx, cy, rx, ry, c):
        for y in range(n):
            for x in range(n):
                dx = ((x + .5) / s - cx) / rx; dy = ((y + .5) / s - cy) / ry
                if dx * dx + dy * dy <= 1:
                    px[y][x] = c
    black, white, blue = (20, 20, 30), (250, 250, 250), (30, 70, 190)
    # keyboard (y grows downwards here)
    rect(2, 20, 32, 33, black)
    for i in range(6):
        rect(3 + i * 5, 21, 7 + i * 5, 32, white)
    for i in (0, 1, 3, 4):
        rect(6 + i * 5, 21, 9 + i * 5, 27, black)
    # quaver
    ellipse(12, 15, 4.2, 3.2, blue)
    rect(15, 2, 17, 15, blue)
    rect(17, 2, 23, 4, blue)
    rect(21, 4, 23, 9, blue)
    return px

def sprite(name, n):
    px = draw(n)
    img = b''.join(struct.pack('<I', 0 if p is None else p[0] | p[1] << 8 | p[2] << 16)
                   for row in px for p in row)
    mwords = (n + 31) // 32
    mask = b''
    for row in px:
        bits = 0
        for x, p in enumerate(row):
            if p is not None:
                bits |= 1 << x
        mask += bits.to_bytes(mwords * 4, 'little')
    mode = (6 << 27) | (90 << 14) | (90 << 1) | 1   # 32bpp, 90 dpi
    hdr = 44
    body = img + mask
    return struct.pack('<I12s7I', hdr + len(body), name.encode().ljust(12, b'\0'),
                       n - 1, n - 1, 0, 31, hdr, hdr + len(img), mode) + body

sprites = [sprite('!midisynth', 34), sprite('sm!midisynth', 17)]
data = b''.join(sprites)
out = struct.pack('<3I', len(sprites), 16, 16 + len(data)) + data
open(sys.argv[1], 'wb').write(out)
