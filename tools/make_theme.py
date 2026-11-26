#!/usr/bin/env python3
"""MANDELBROT look for LMMS: recolours the default theme and draws the branding.

  python3 tools/make_theme.py <lmms source dir>

* style.css: LMMS's green accents become MANDELBROT cyan, the greys a deeper blue-black
* every theme PNG: green pixels are hue-shifted to the same cyan (play button, LEDs, meters...)
* splash.png, the window/installer icons (icon.ico, project.ico) and the installer banner are
  drawn from the Mandelbrot set
Needs Pillow (CI installs it).
"""
import colorsys
import glob
import math
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ACCENT_HUE = 192 / 360.0  # cyan

CSS_MAP = {
    # greens -> cyan
    "#43ff82": "#5ee7ff", "#21a14f": "#1f9fc4", "#0bd556": "#18c8f0", "#17793b": "#155f86",
    "#3dd671": "#4ad8ff", "#3ac467": "#3bbde6", "#2fcc71": "#2fc4ec", "#1de276": "#28dcff",
    # greys -> deeper blue-black
    "#262b30": "#1b1e26", "#3b424a": "#2b303b", "#343840": "#262a34", "#101213": "#0c0e13",
    "#1e2226": "#161920", "#3f4750": "#313746", "#111314": "#0d0f15", "#141616": "#10121a",
    "#1d2122": "#151820", "#1b1f22": "#141720", "#181b1f": "#12141b", "#3c444c": "#2d3340",
    "#373d48": "#2a2f3b", "#3f4750": "#313746",
}
RGB_MAP = [
    (r"rgb\(\s*0\s*,\s*255\s*,\s*128\s*\)", "rgb(40, 220, 255)"),
    (r"rgba\(\s*60\s*,\s*255\s*,\s*130\s*,", "rgba(60, 220, 255,"),
    (r"rgba\(\s*39\s*,\s*171\s*,\s*95\s*,", "rgba(39, 150, 205,"),
    (r"rgba\(\s*28\s*,\s*73\s*,\s*51\s*,", "rgba(18, 44, 70,"),
    (r"rgba\(\s*17\s*,\s*136\s*,\s*71\s*,", "rgba(20, 120, 175,"),
    (r"rgba\(\s*9\s*,\s*171\s*,\s*160\s*,", "rgba(9, 160, 210,"),
]


def recolour_css(path):
    s = open(path, encoding="utf-8").read()
    n = 0
    for k, v in CSS_MAP.items():
        s, c = re.subn(re.escape(k), v, s, flags=re.I)
        n += c
    for pat, rep in RGB_MAP:
        s, c = re.subn(pat, rep, s, flags=re.I)
        n += c
    s = s.replace("* LMMS style sheet *", "* MANDELBROT style *")
    open(path, "w", encoding="utf-8").write(s)
    return n


def shift_png(path):
    """Moves green hues (80..170 deg) to the accent hue; leaves everything else alone."""
    try:
        im = Image.open(path)
    except Exception:
        return False
    if im.mode not in ("RGBA", "RGB", "P", "LA", "L"):
        return False
    if im.mode in ("L", "LA"):
        return False
    im = im.convert("RGBA")
    px = im.load()
    changed = False
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            hh, ll, ss = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)
            if ss > 0.18 and 80 / 360.0 <= hh <= 170 / 360.0:
                nr, ng, nb = colorsys.hls_to_rgb(ACCENT_HUE, ll, ss)
                px[x, y] = (int(nr * 255), int(ng * 255), int(nb * 255), a)
                changed = True
    if changed:
        im.save(path)
    return changed


def mandel(w, h, cx=-0.6, cy=0.0, span=3.0, iters=80):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            c = complex(cx + (x - w / 2) * span / w, cy + (y - h / 2) * span / w)
            z = 0j
            k = 0
            while k < iters and abs(z) < 4:
                z = z * z + c
                k += 1
            if k >= iters:
                px[x, y] = (6, 7, 12)
            else:
                t = (k - math.log2(max(1e-9, math.log(max(abs(z), 1.0001))))) / iters
                t = max(0.0, min(1.0, t))
                hue = (ACCENT_HUE + 0.35 * t) % 1.0
                rr, gg, bb = colorsys.hls_to_rgb(hue, 0.08 + 0.55 * t ** 0.6, 0.85)
                px[x, y] = (int(rr * 255), int(gg * 255), int(bb * 255))
    return im


def font(size, bold=True):
    for f in ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf", "LiberationSans-Bold.ttf", "Arial Bold.ttf"):
        for d in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation", "/usr/share/fonts"):
            p = os.path.join(d, f)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def splash(path, version_text):
    w, h = 640, 300
    im = mandel(w, h, -0.7445, 0.1215, 0.075, 260).convert("RGBA")
    shade = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(shade)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=(5, 6, 10, int(170 * (y / h) ** 1.5)))
    im = Image.alpha_composite(im, shade)
    d = ImageDraw.Draw(im)
    d.text((28, h - 92), "MANDELBROT", font=font(54), fill=(235, 250, 255, 255))
    d.text((32, h - 34), version_text, font=font(15, False), fill=(130, 215, 240, 255))
    im.convert("RGB").save(path)


def icon_image(size):
    im = mandel(size * 2, size * 2, -0.65, 0.0, 2.9, 60).resize((size, size), Image.LANCZOS)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=size // 5, fill=255)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(im, (0, 0), mask)
    return out


def main():
    src = sys.argv[1]
    theme = os.path.join(src, "data", "themes", "default")
    print("css replacements:", recolour_css(os.path.join(theme, "style.css")))
    n = sum(1 for p in glob.glob(os.path.join(theme, "*.png")) if shift_png(p))
    print("theme images recoloured:", n)
    splash(os.path.join(theme, "splash.png"), "generative music workstation  -  built on LMMS")
    big = icon_image(256)
    for name in ("icon.png", "icon_small.png"):
        p = os.path.join(theme, name)
        if os.path.exists(p):
            sz = Image.open(p).size
            big.resize(sz, Image.LANCZOS).save(p)
    nsis = os.path.join(src, "cmake", "nsis")
    if os.path.isdir(nsis):
        sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
        big.save(os.path.join(nsis, "icon.ico"), sizes=sizes)
        big.save(os.path.join(nsis, "project.ico"), sizes=sizes)
        # installer banner (150x57 BMP)
        b = mandel(300, 114, -0.745, 0.11, 1.2, 100).resize((150, 57), Image.LANCZOS)
        ImageDraw.Draw(b).text((8, 18), "MANDELBROT", font=font(17), fill=(235, 250, 255))
        b.save(os.path.join(nsis, "nsis_branding.bmp"))
    print("branding drawn")


if __name__ == "__main__":
    main()
