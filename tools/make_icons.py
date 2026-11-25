#!/usr/bin/env python3
"""Draws MANDELBROT's icons (the Mandelbrot set) as PNG files, with no dependencies.

  python3 tools/make_icons.py <theme dir>   -> fractal_tab.png (14x14), fractal_window.png (32x32)
"""
import os
import struct
import sys
import zlib


def mandel_rgba(size, colour, ss=4):
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    cx = -2.1 + 2.7 * (x * ss + sx + 0.5) / (size * ss)
                    cy = -1.35 + 2.7 * (y * ss + sy + 0.5) / (size * ss)
                    zx = zy = 0.0
                    k = 0
                    while k < 60 and zx * zx + zy * zy < 4.0:
                        zx, zy = zx * zx - zy * zy + cx, 2 * zx * zy + cy
                        k += 1
                    a = 1.0 if k >= 60 else (min(1.0, (k - 8) / 30.0) if k > 8 else 0.0)
                    for i in range(3):
                        acc[i] += colour[i] * a
                    acc[3] += a
            n = ss * ss
            alpha = acc[3] / n
            rgb = [int(acc[i] / max(acc[3], 1e-9)) if acc[3] > 0 else 0 for i in range(3)]
            row.append(tuple(rgb) + (int(255 * alpha),))
        out.append(row)
    return out


def write_png(path, pixels):
    h, w = len(pixels), len(pixels[0])
    raw = b"".join(b"\x00" + b"".join(struct.pack("BBBB", *p) for p in row) for row in pixels)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    write_png(os.path.join(out, "fractal_tab.png"), mandel_rgba(14, (255, 255, 255)))
    write_png(os.path.join(out, "fractal_window.png"), mandel_rgba(32, (40, 220, 255)))
    print("icons written to", out)


if __name__ == "__main__":
    main()
