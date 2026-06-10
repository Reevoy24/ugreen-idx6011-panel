#!/usr/bin/env python3
"""Convert the render-mock .raw framebuffers (XRGB8888, 258x960) to PNGs
and build a side-by-side overview image. Pure stdlib, no PIL needed."""
import glob
import os
import struct
import sys
import zlib

W, H = 258, 960
GAP = 14


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def write_png(path, width, height, rgb_rows):
    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def raw_to_rows(data):
    rows = []
    for y in range(H):
        line = bytearray()
        off = y * W * 4
        for x in range(W):
            b, g, r = data[off + x * 4], data[off + x * 4 + 1], data[off + x * 4 + 2]
            line += bytes((r, g, b))
        rows.append(bytes(line))
    return rows


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else "mockups"
    raws = sorted(glob.glob(os.path.join(src_dir, "page_*.raw")))
    if not raws:
        print("no page_*.raw found in", src_dir, file=sys.stderr)
        return 1

    all_rows = []
    for path in raws:
        with open(path, "rb") as f:
            data = f.read()
        rows = raw_to_rows(data)
        out = path[:-4] + ".png"
        write_png(out, W, H, rows)
        print("wrote", out)
        all_rows.append(rows)

    n = len(all_rows)
    ov_w = n * W + (n - 1) * GAP
    gap_px = b"\x20\x20\x24" * GAP
    ov_rows = []
    for y in range(H):
        line = bytearray()
        for i, rows in enumerate(all_rows):
            if i:
                line += gap_px
            line += rows[y]
        ov_rows.append(bytes(line))
    ov_path = os.path.join(src_dir, "overview.png")
    write_png(ov_path, ov_w, H, ov_rows)
    print("wrote", ov_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
