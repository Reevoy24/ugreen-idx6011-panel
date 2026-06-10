#!/usr/bin/env python3
"""Generate the built-in wallpapers (258x960 PNG, pure stdlib).

Design rules: dark and modern, darkest in the top third (clock + values
must stay readable), no busy detail behind text areas."""
import math
import os
import struct
import sys
import zlib

W, H = 258, 960
OUT = os.path.join(os.path.dirname(__file__), "..", "packaging", "wallpapers")


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, pix):
    rows = b"".join(b"\x00" + bytes(pix[y]) for y in range(H))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)
    print("wrote", path)


def clamp(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


def top_darken(y):
    """Fade everything towards black in the top third (text area)."""
    t = y / H
    if t > 0.40:
        return 1.0
    return 0.45 + 0.55 * (t / 0.40)


def glow(x, y, cx, cy, radius, strength):
    d2 = ((x - cx) ** 2 + (y - cy) ** 2) / (radius * radius)
    return strength * math.exp(-d2)


def gen_aurora():
    pix = []
    for y in range(H):
        row = bytearray()
        t = y / H
        base_r = 5 + 14 * t
        base_g = 6 + 16 * t
        base_b = 12 + 40 * t
        for x in range(W):
            r, g, b = base_r, base_g, base_b
            gl = glow(x, y, 40, 700, 240, 1.0)
            r += 8 * gl; g += 66 * gl; b += 78 * gl
            gl = glow(x, y, 230, 880, 260, 1.0)
            r += 46 * gl; g += 18 * gl; b += 88 * gl
            gl = glow(x, y, 150, 520, 190, 0.55)
            r += 10 * gl; g += 36 * gl; b += 60 * gl
            k = top_darken(y)
            row += bytes((clamp(r * k), clamp(g * k), clamp(b * k)))
        pix.append(row)
    return pix


def gen_dusk():
    pix = []
    for y in range(H):
        row = bytearray()
        t = y / H
        base_r = 6 + 36 * (t ** 2)
        base_g = 6 + 10 * t
        base_b = 16 + 30 * t
        for x in range(W):
            r, g, b = base_r, base_g, base_b
            gl = glow(x, y, 129, 980, 300, 1.0)
            r += 120 * gl; g += 48 * gl; b += 14 * gl
            gl = glow(x, y, 50, 760, 200, 0.5)
            r += 60 * gl; g += 16 * gl; b += 40 * gl
            k = top_darken(y)
            row += bytes((clamp(r * k), clamp(g * k), clamp(b * k)))
        pix.append(row)
    return pix


def gen_carbon():
    pix = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            base = 11
            stripe = (x + y) % 14
            if stripe < 7:
                base += 2
            d = math.hypot(x - W / 2, y - H * 0.62) / (H * 0.75)
            v = base * (1.0 - 0.35 * d)
            k = top_darken(y)
            row += bytes((clamp(v * k), clamp(v * k), clamp((v + 2) * k)))
        pix.append(row)
    return pix


def gen_ocean():
    pix = []
    for y in range(H):
        row = bytearray()
        t = y / H
        base_r = 4 + 6 * t
        base_g = 12 + 18 * t
        base_b = 24 + 36 * t
        for x in range(W):
            r, g, b = base_r, base_g, base_b
            for i, (cy, amp, period, width, strength) in enumerate(
                    ((600, 16, 300.0, 50.0, 0.40),
                     (740, 22, 380.0, 60.0, 0.60),
                     (890, 18, 320.0, 55.0, 0.50))):
                wy = cy + amp * math.sin(x / period * 2 * math.pi + i * 2.1)
                d = abs(y - wy) / width
                gl = strength * math.exp(-d * d)
                r += 6 * gl; g += 46 * gl; b += 58 * gl
            k = top_darken(y)
            row += bytes((clamp(r * k), clamp(g * k), clamp(b * k)))
        pix.append(row)
    return pix


def main():
    os.makedirs(OUT, exist_ok=True)
    write_png(os.path.join(OUT, "aurora.png"), gen_aurora())
    write_png(os.path.join(OUT, "dusk.png"), gen_dusk())
    write_png(os.path.join(OUT, "carbon.png"), gen_carbon())
    write_png(os.path.join(OUT, "ocean.png"), gen_ocean())
    return 0


if __name__ == "__main__":
    sys.exit(main())
