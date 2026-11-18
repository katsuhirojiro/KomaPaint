#!/usr/bin/env python3
"""KomaPaint — Brush Studio, mockup renderer (v3).

Source of truth is the application itself, not another paint program:

  * colours, type scale and spacing  -> ui/themes/KomaTheme.qml
  * window chrome                   -> ui/workspaces/MainWindow.qml
                                    (menu band, command bar, docks, status bar)
  * tool icons                      -> the real PNGs in ui/assets/icons/tools,
                                       tinted the way KpIcon tints them
  * interface glyphs                -> drawn in the KpGlyph family: 20-unit grid,
                                       1.7-unit stroke, round caps, one weight
  * brush vocabulary                -> resources/brush-presets.json and
                                       src/app/SubToolLibrary.h (Digital /
                                       Realistic engines, Sub-Tool presets)

Rendered at 2x and downsampled so every edge and glyph is antialiased.
"""

import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 2
W, H = 1728, 1080
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", ".."))
ICONS = os.path.join(REPO, "ui", "assets", "icons", "tools")
# Inter 400/500/600, as .ttf (any folder; set KOMAPAINT_MOCKUP_FONTS to override)
FDIR = os.environ.get("KOMAPAINT_MOCKUP_FONTS", "/tmp/mock/fonts")

# --- KomaTheme.qml, dark theme ----------------------------------------------
class C:
    windowBg = "#2c2c2c"
    menubarBg = "#2c2c2c"
    toolbarBg = "#2c2c2c"
    paneBg = "#383838"
    paneBgAlt = "#333333"
    paneHeaderBg = "#2c2c2c"
    panelInsetBg = "#292929"
    canvasBg = "#454545"
    canvasEdge = "#1b1d20"
    previewPaper = "#dde1e6"
    inputBg = "#292929"
    buttonBg = "#404040"
    toolRailBg = "#292929"
    statusbarBg = "#2c2c2c"
    toolActive = "#505050"
    rowSelected = "#505050"
    transparencyLight = "#828282"
    transparencyDark = "#666666"
    hairline = "#5c5c5c"
    border = "#666666"
    text = "#ededed"
    textMuted = "#c6c6c6"
    textSubtle = "#909090"
    textSection = "#bdbdbd"
    accent = "#c8c8c8"
    warning = "#d2a054"
    success = "#7fb27f"
    ink = "#222222"
    iconNormal = "#dedede"
    iconSubtle = "#909090"


_fonts = {}
def F(size, weight=400):
    key = (size, weight)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(f"{FDIR}/Inter-{weight}.ttf", int(round(size * S)))
    return _fonts[key]


def rect(d, x, y, w, h, fill=None, outline=None, width=1, radius=0):
    box = [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1]
    if radius:
        d.rounded_rectangle(box, radius=radius * S, fill=fill, outline=outline, width=int(width * S))
    else:
        d.rectangle(box, fill=fill, outline=outline, width=int(width * S) if outline else 0)


def line(d, x1, y1, x2, y2, color, width=1):
    d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=color, width=max(1, int(width * S)))


def text(d, x, y, s, size=11, weight=400, color=C.text, anchor="la"):
    d.text((x * S, y * S), s, font=F(size, weight), fill=color, anchor=anchor)


def text_w(s, size=11, weight=400):
    return F(size, weight).getlength(s) / S


def tracked(d, x, y, s, size=9, weight=600, color=C.textSection, tracking=0.9, anchor="l"):
    total = sum(text_w(ch, size, weight) + tracking for ch in s) - tracking
    if anchor == "r":
        x -= total
    cx = x
    for ch in s:
        d.text((cx * S, y * S), ch, font=F(size, weight), fill=color, anchor="ls")
        cx += text_w(ch, size, weight) + tracking
    return total


# --- authored PNG tool icons, tinted like KpIcon ----------------------------
_png_cache = {}


def hex_rgba(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def tool_icon(img, name, x, y, size, color):
    """Tint ui/assets/icons/tools/<name>.png with the interface colour."""
    key = (name, size, color)
    if key not in _png_cache:
        src = Image.open(os.path.join(ICONS, name + ".png")).convert("RGBA")
        a = src.split()[-1].resize((int(size * S), int(size * S)), Image.LANCZOS)
        solid = Image.new("RGBA", a.size, hex_rgba(color))
        solid.putalpha(a)
        _png_cache[key] = solid
    img.paste(_png_cache[key], (int(x * S), int(y * S)), _png_cache[key])


# --- KpGlyph family: 20-unit grid, 1.7 stroke, round caps -------------------
def stroke(d, pts, color, width=1.7):
    for i in range(len(pts) - 1):
        line(d, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], color, width)
    for (px, py) in (pts[0], pts[-1]):
        r = width / 2
        d.ellipse([(px - r) * S, (py - r) * S, (px + r) * S, (py + r) * S], fill=color)


def glyph(d, name, x, y, size, color):
    u = size / 20.0  # one grid unit

    def P(a, b):
        return (x + a * u, y + b * u)

    if name == "new":
        stroke(d, [P(5, 3), P(12, 3), P(15, 6), P(15, 17), P(5, 17), P(5, 3)], color)
        stroke(d, [P(10, 7), P(10, 13)], color)
        stroke(d, [P(7, 10), P(13, 10)], color)
    elif name == "open":
        stroke(d, [P(3, 16), P(3, 6), P(8, 6), P(9.5, 8), P(16, 8), P(16, 16), P(3, 16)], color)
    elif name == "save":
        stroke(d, [P(4, 16.5), P(4, 4), P(14, 4), P(16, 6), P(16, 16.5), P(4, 16.5)], color)
        stroke(d, [P(7, 4), P(7, 8.5), P(13, 8.5), P(13, 4)], color)
        stroke(d, [P(7, 16.5), P(7, 12), P(13, 12), P(13, 16.5)], color)
    elif name == "duplicate":
        stroke(d, [P(3.5, 6.5), P(3.5, 16.5), P(13.5, 16.5)], color)
        stroke(d, [P(6.5, 3.5), P(16.5, 3.5), P(16.5, 13.5), P(6.5, 13.5), P(6.5, 3.5)], color)
    elif name == "folder":
        stroke(d, [P(3, 16), P(3, 5), P(8, 5), P(9.5, 7.5), P(17, 7.5), P(17, 16), P(3, 16)], color)
    elif name == "close":
        stroke(d, [P(5.5, 5.5), P(14.5, 14.5)], color)
        stroke(d, [P(14.5, 5.5), P(5.5, 14.5)], color)
    elif name == "plus":
        stroke(d, [P(10, 4.5), P(10, 15.5)], color)
        stroke(d, [P(4.5, 10), P(15.5, 10)], color)
    elif name == "minus":
        stroke(d, [P(4.5, 10), P(15.5, 10)], color)
    elif name == "chevron-down":
        stroke(d, [P(5, 7.5), P(10, 12.5), P(15, 7.5)], color)
    elif name == "chevron-right":
        stroke(d, [P(7.5, 5), P(12.5, 10), P(7.5, 15)], color)
    elif name == "dots-h":
        for cx in (5, 10, 15):
            d.ellipse([(x + cx * u - 1) * S, (y + 10 * u - 1) * S,
                       (x + cx * u + 1) * S, (y + 10 * u + 1) * S], fill=color)
    elif name == "search":
        d.ellipse([(x + 4 * u) * S, (y + 4 * u) * S, (x + 12 * u) * S, (y + 12 * u) * S],
                  outline=color, width=max(1, int(1.7 * S)))
        stroke(d, [P(12.4, 12.4), P(16, 16)], color)
    elif name == "check":
        stroke(d, [P(4.5, 10.5), P(8.5, 14.5), P(15.5, 6)], color)
    elif name == "reset":
        d.arc([(x + 4 * u) * S, (y + 4 * u) * S, (x + 16 * u) * S, (y + 16 * u) * S],
              40, 320, fill=color, width=max(1, int(1.7 * S)))
        stroke(d, [P(14, 3.4), P(15.6, 6.4), P(12.2, 7)], color)
    elif name == "warning":
        stroke(d, [P(10, 3.5), P(17.5, 16.5), P(2.5, 16.5), P(10, 3.5)], color)
        stroke(d, [P(10, 8), P(10, 12)], color)
        d.ellipse([(x + 9 * u) * S, (y + 13.4 * u) * S, (x + 11 * u) * S, (y + 15.4 * u) * S], fill=color)
    elif name == "grid":
        stroke(d, [P(3.5, 3.5), P(16.5, 3.5), P(16.5, 16.5), P(3.5, 16.5), P(3.5, 3.5)], color)
        stroke(d, [P(3.5, 10), P(16.5, 10)], color)
        stroke(d, [P(10, 3.5), P(10, 16.5)], color)
    elif name == "transform":
        stroke(d, [P(4, 4), P(11, 4)], color)
        stroke(d, [P(4, 4), P(4, 11)], color)
        stroke(d, [P(16, 16), P(9, 16)], color)
        stroke(d, [P(16, 16), P(16, 9)], color)
        stroke(d, [P(6.5, 13.5), P(13.5, 6.5)], color)
    elif name == "brush":
        stroke(d, [P(6, 15), P(12.5, 6.5)], color)
        stroke(d, [P(11.4, 5.2), P(13.6, 7.2)], color)
    elif name == "size":
        stroke(d, [P(3.5, 16), P(16, 3.5)], color)
        d.ellipse([(x + 3.5 * u - 1.6 * u) * S, (y + 16 * u - 1.6 * u) * S,
                   (x + 3.5 * u + 1.6 * u) * S, (y + 16 * u + 1.6 * u) * S], fill=color)
        d.ellipse([(x + 16 * u - 2.6 * u) * S, (y + 3.5 * u - 2.6 * u) * S,
                   (x + 16 * u + 2.6 * u) * S, (y + 3.5 * u + 2.6 * u) * S], fill=color)
    elif name == "opacity":
        d.ellipse([(x + 3.5 * u) * S, (y + 3.5 * u) * S, (x + 16.5 * u) * S, (y + 16.5 * u) * S],
                  outline=color, width=max(1, int(1.7 * S)))
        d.pieslice([(x + 3.5 * u) * S, (y + 3.5 * u) * S, (x + 16.5 * u) * S, (y + 16.5 * u) * S],
                   90, 270, fill=color)
    elif name == "nodes":
        d.ellipse([(x + 2.5 * u) * S, (y + 4.5 * u) * S, (x + 8.5 * u) * S, (y + 10.5 * u) * S],
                  outline=color, width=max(1, int(1.7 * S)))
        d.ellipse([(x + 11.5 * u) * S, (y + 9.5 * u) * S, (x + 17.5 * u) * S, (y + 15.5 * u) * S],
                  outline=color, width=max(1, int(1.7 * S)))
        stroke(d, [P(8.5, 7.5), P(13.5, 11)], color)
    elif name == "reset-all":
        d.arc([(x + 4 * u) * S, (y + 4 * u) * S, (x + 16 * u) * S, (y + 16 * u) * S],
              40, 320, fill=color, width=max(1, int(1.7 * S)))
    elif name == "trash":
        stroke(d, [P(4, 6), P(16, 6)], color)
        stroke(d, [P(8, 6), P(8, 4), P(12, 4), P(12, 6)], color)
        stroke(d, [P(5.5, 6.5), P(6.5, 16.5), P(13.5, 16.5), P(14.5, 6.5)], color)
        stroke(d, [P(8.5, 9), P(8.5, 14)], color)
        stroke(d, [P(11.5, 9), P(11.5, 14)], color)
    elif name == "eye":
        d.arc([(x + 3 * u) * S, (y + 6 * u) * S, (x + 17 * u) * S, (y + 14 * u) * S],
              180, 360, fill=color, width=max(1, int(1.7 * S)))
        d.arc([(x + 3 * u) * S, (y + 6 * u) * S, (x + 17 * u) * S, (y + 14 * u) * S],
              0, 180, fill=color, width=max(1, int(1.7 * S)))
        d.ellipse([(x + 8.6 * u) * S, (y + 8.6 * u) * S, (x + 11.4 * u) * S, (y + 11.4 * u) * S],
                  fill=color)
    elif name == "eye-off":
        stroke(d, [P(3.6, 3.6), P(16.4, 16.4)], color)
        d.arc([(x + 3 * u) * S, (y + 6 * u) * S, (x + 17 * u) * S, (y + 14 * u) * S],
              0, 180, fill=color, width=max(1, int(1.7 * S)))
    elif name == "undo":
        d.arc([(x + 4 * u) * S, (y + 5 * u) * S, (x + 16 * u) * S, (y + 17 * u) * S],
              205, 25, fill=color, width=max(1, int(1.7 * S)))
        stroke(d, [P(6.4, 4.0), P(4.0, 7.4), P(7.6, 9.0)], color)
    elif name == "redo":
        d.arc([(x + 4 * u) * S, (y + 5 * u) * S, (x + 16 * u) * S, (y + 17 * u) * S],
              155, 335, fill=color, width=max(1, int(1.7 * S)))
        stroke(d, [P(13.6, 4.0), P(16.0, 7.4), P(12.4, 9.0)], color)
    elif name == "grip":
        for gy2 in (7, 10, 13):
            for gx2 in (8, 12):
                d.ellipse([(x + gx2 * u - 0.65 * u) * S, (y + gy2 * u - 0.65 * u) * S,
                           (x + gx2 * u + 0.65 * u) * S, (y + gy2 * u + 0.65 * u) * S], fill=color)
    elif name == "swap":
        stroke(d, [P(3.5, 7.5), P(16, 7.5)], color)
        stroke(d, [P(13, 4.5), P(16.2, 7.5), P(13, 10.5)], color)
        stroke(d, [P(16.5, 12.5), P(4, 12.5)], color)
        stroke(d, [P(7, 9.5), P(3.8, 12.5), P(7, 15.5)], color)
    elif name == "tile":
        stroke(d, [P(3.5, 3.5), P(9, 3.5), P(9, 9), P(3.5, 9), P(3.5, 3.5)], color)
        stroke(d, [P(11, 3.5), P(16.5, 3.5), P(16.5, 9), P(11, 9), P(11, 3.5)], color)
        stroke(d, [P(3.5, 11), P(9, 11), P(9, 16.5), P(3.5, 16.5), P(3.5, 11)], color)
        stroke(d, [P(11, 11), P(16.5, 11), P(16.5, 16.5), P(11, 16.5), P(11, 11)], color)
    else:
        raise KeyError(name)


# --- composite artwork ------------------------------------------------------
def checker(d, x, y, w, h, cell=8):
    rect(d, x, y, w, h, fill=C.transparencyLight)
    for j in range(int(math.ceil(h / cell))):
        for i in range(int(math.ceil(w / cell))):
            if (i + j) % 2:
                x0, y0 = x + i * cell, y + j * cell
                rect(d, x0, y0, min(cell, x + w - x0), min(cell, y + h - y0),
                     fill=C.transparencyDark)


def _smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def bristle_tip(layer, box, seed=7, density=1.0, blur=0.15, ink=(11, 11, 13),
                taper=0.03):
    """A brush-tip mask: flat opaque core, ragged bristle edges, striated body.

    Built as a field so the striations run with the bristles instead of looking
    like scattered dots, which is what a real tip mask looks like at 1:1.
    """
    x, y, w, h = [v * S for v in box]
    wp, hp = max(2, int(w)), max(2, int(h))
    rng = np.random.default_rng(seed)
    fx = np.linspace(0, 1, wp)[None, :]
    fy = np.linspace(0, 1, hp)[:, None]

    # ends: hold almost to the tip, then bite
    ex = _smoothstep(0.0, taper, fx) * _smoothstep(0.0, taper, 1 - fx)
    # vertical: flat core with a fast, slightly wavy falloff
    half = 0.345 + 0.020 * np.sin(fx * 9.3 + 1.1) + 0.012 * np.sin(fx * 23.0)
    dist = np.abs(fy - 0.5)
    ey = 1.0 - _smoothstep(half - 0.055, half + 0.030, dist)

    # bristles: high frequency across the width
    freqs = np.array([48, 77, 113, 151, 197, 251, 313, 383, 461, 557, 673], float)
    ph = rng.uniform(0, 2 * np.pi, freqs.size)
    fibre = np.sum(np.sin(fx[0][:, None] * 2 * np.pi * freqs[None, :] + ph[None, :]), axis=1)[None, :] / freqs.size
    fibre = np.clip(0.5 + 0.5 * fibre, 0, 1)

    # clumps: where the bristles bunch up, with real gaps between them
    cfreq = np.array([4.0, 7.0, 12.0, 19.0, 31.0])
    cph = rng.uniform(0, 2 * np.pi, cfreq.size)
    clump = np.sum(np.sin(fx[0][:, None] * 2 * np.pi * cfreq[None, :] + cph[None, :]), axis=1)[None, :] / cfreq.size
    clump = np.clip(0.45 + 0.55 * clump, 0.18, 1.0)

    a = ex * ey * (0.10 + 0.90 * fibre ** 1.7) * (0.55 + 0.45 * clump) * density
    alpha = (np.clip(a, 0, 1) ** 0.72 * 255).astype(np.uint8)
    rgba = np.zeros((hp, wp, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = ink
    rgba[..., 3] = alpha
    im = Image.fromarray(rgba, "RGBA")
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur * S))
    layer.alpha_composite(im, (int(x), int(y)))


def smooth_path(pts, per_seg=14):
    """Catmull-Rom through the points: brush strokes curve, they do not kink."""
    if len(pts) < 3:
        return pts
    p = [pts[0]] + list(pts) + [pts[-1]]
    out = []
    for i in range(len(p) - 3):
        p0, p1, p2, p3 = p[i], p[i + 1], p[i + 2], p[i + 3]
        for j in range(per_seg):
            t = j / per_seg
            t2, t3 = t * t, t * t * t
            out.append((
                0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t
                       + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                       + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3),
                0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t
                       + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                       + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3),
            ))
    out.append(pts[-1])
    return out


def paint_stroke(layer, pts, w_start, w_end, ink=(24, 28, 36), seed=1,
                 fibre=420, dry=0.0, opacity=242, taper=0.55, comb_n=11):
    """A tapered, bristle-split stroke. Nothing is erased: dry areas thin out."""
    d = ImageDraw.Draw(layer)
    rnd = random.Random(seed)
    pts = [(p[0] * S, p[1] * S) for p in smooth_path(pts)]
    samples = []
    for i in range(len(pts) - 1):
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
        n = max(2, int(math.hypot(x1 - x0, y1 - y0) / (0.5 * S)))
        for j in range(n):
            t = j / n
            samples.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
    samples.append(pts[-1])
    total = len(samples)

    def radius(t):
        base = w_start + (w_end - w_start) * t
        edge = min(1.0, (t + 0.02) / taper) * min(1.0, (1.02 - t) / taper)
        return (base * (0.30 + 0.70 * edge) + 0.4) * S

    comb = [rnd.uniform(0.72, 1.0) for _ in range(comb_n)]
    for _ in range(2):
        comb[rnd.randrange(comb_n)] = rnd.uniform(0.10, 0.26)

    def normal(k):
        a = samples[max(0, k - 1)]
        b = samples[min(total - 1, k + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(tx, ty) or 1.0
        return -ty / ln, tx / ln

    for k, (px_, py_) in enumerate(samples):
        r = radius(k / max(1, total - 1)) / 2
        d.ellipse([px_ - r * 1.2, py_ - r * 1.2, px_ + r * 1.2, py_ + r * 1.2], fill=ink + (34,))
    for k, (px_, py_) in enumerate(samples):
        t = k / max(1, total - 1)
        r = radius(t) / 2
        nx, ny = normal(k)
        d.ellipse([px_ - r * 0.94, py_ - r * 0.94, px_ + r * 0.94, py_ + r * 0.94],
                  fill=ink + (int(opacity * 0.80),))
        for i, cp in enumerate(comb):
            off = (i / (len(comb) - 1) * 2 - 1) * r * 0.80
            rr = max(0.7 * S, r * 0.30)
            a = int(opacity * (0.58 + 0.42 * cp) * (0.62 + 0.38 * min(1.0, t * 6, (1 - t) * 8)))
            d.ellipse([px_ + nx * off - rr, py_ + ny * off - rr,
                       px_ + nx * off + rr, py_ + ny * off + rr],
                      fill=ink + (max(0, min(255, a)),))

    def band(t):
        return 0.62 + 0.38 * math.sin(t * 9.0 + seed) * math.sin(t * 3.7 + seed * 0.5)

    fibres = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    fd = ImageDraw.Draw(fibres)
    for _ in range(fibre):
        k = rnd.randrange(total)
        px_, py_ = samples[k]
        t = k / max(1, total - 1)
        spread = radius(t) * 0.62
        off = rnd.gauss(0, spread * 0.5)
        if abs(off) > spread * 1.35:
            continue
        wet = band(t) * (1.0 - dry) + dry * 0.18
        if rnd.random() > wet:
            continue
        jx = rnd.uniform(-1.2, 1.2) * S
        jy = rnd.uniform(-1.2, 1.2) * S
        ln = rnd.uniform(0.9, 2.4) * S
        a = int(rnd.uniform(55, 130) * wet * 1.6)
        fd.line([px_ + off + jx, py_ + jy, px_ + off * 1.05 + jx * 0.4, py_ + jy + ln],
                fill=ink + (max(0, min(255, a)),), width=S)
    layer.alpha_composite(fibres.filter(ImageFilter.GaussianBlur(0.28 * S)))


def stamp(layer, x, y, r, seed=3, ink=(30, 34, 42), press=1.0):
    d = ImageDraw.Draw(layer)
    rnd = random.Random(seed)
    cx, cy = x * S, y * S
    rr = r * S
    d.ellipse([cx - rr * 0.90, cy - rr * 0.90, cx + rr * 0.90, cy + rr * 0.90],
              fill=ink + (int(105 * press),))
    fibres = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    fd = ImageDraw.Draw(fibres)
    for _ in range(int(320 * press) + 60):
        ang = rnd.uniform(0, math.tau)
        rad = rr * (rnd.random() ** 0.6)
        px_ = cx + math.cos(ang) * rad
        py_ = cy + math.sin(ang) * rad
        a = int(190 * (1.0 - (rad / rr) ** 1.4) * rnd.uniform(0.4, 1.0) * press)
        ln = rnd.uniform(1.2, 3.6) * S * press
        fd.line([px_, py_, px_ + math.cos(ang) * ln, py_ + math.sin(ang) * ln],
                fill=ink + (a,), width=S)
    layer.alpha_composite(fibres.filter(ImageFilter.GaussianBlur(0.3 * S)))


# =============================================================================
TITLE_H, MENU_H, CMD_H = 30, 30, 40
STATUS_H, PREVIEW_H = 24, 250
BODY_Y = TITLE_H + MENU_H + CMD_H
BODY_H = H - BODY_Y - PREVIEW_H - STATUS_H
PREV_Y = BODY_Y + BODY_H
LEFT_W, RIGHT_W, RAIL_W, RULER = 260, 340, 34, 16
RIGHT_X = W - RIGHT_W
CENTER_X = LEFT_W
CENTER_W = RIGHT_X - CENTER_X
PAD = 14
RX = RIGHT_X + PAD
RW = RIGHT_W - PAD * 2
DASH = (5, 5)

BRUSH = "Wet Bristle"

# Every component the brush carries. Hidden ones stay in the stack; nothing is
# ever silently removed, and there is no cap on how many a brush can hold.
COMPONENTS = [
    ("BRUSH TIPS", [
        ("Bristle Round", "TIP · 1024 × 480 · mask", "tip", True, True, 3),
        ("Dry Fan", "TIP · 1024 × 480 · mask", "tip", False, True, 5),
        ("Chisel Edge", "TIP · 512 × 512 · mask", "tip", False, True, 9),
    ]),
    ("TEXTURES", [
        ("Canvas Grain", "TEX · 1024 × 1024 · grayscale", "tex", False, True, 13),
        ("Paper Fibre", "TEX · 768 × 768 · grayscale", "tex", False, False, 17),
    ]),
    ("IMPORTED IMAGES", [
        ("bristle-scan.png", "IMG · 2048 × 2048 · PNG", "img", False, True, 21),
    ]),
    ("CUSTOM", [
        ("Wet Edge Profile", "CST · curve · 6 points", "cus", False, True, 25),
        ("Scatter Shape", "CST · mask · 256 × 256", "cus", False, True, 29),
    ]),
]


def grain_field(size_px, seed):
    """Seamless canvas grain: periodic value-noise octaves plus per-pixel tooth."""
    rng = np.random.default_rng(seed)
    n = size_px
    yy = np.arange(n) * 1.0
    xx = np.arange(n) * 1.0
    v = np.zeros((n, n))

    def octave(g, weight):
        grid = rng.random((g, g))
        uy = yy * g / n
        ux = xx * g / n
        iy0 = np.floor(uy).astype(int) % g
        ix0 = np.floor(ux).astype(int) % g
        iy1 = (iy0 + 1) % g
        ix1 = (ix0 + 1) % g
        ty = (uy - np.floor(uy))[:, None]
        tx = (ux - np.floor(ux))[None, :]
        a = grid[np.ix_(iy0, ix0)]
        b = grid[np.ix_(iy0, ix1)]
        c = grid[np.ix_(iy1, ix0)]
        d = grid[np.ix_(iy1, ix1)]
        return weight * (a * (1 - ty) * (1 - tx) + b * (1 - ty) * tx
                         + c * ty * (1 - tx) + d * ty * tx)

    for g, w in ((8, 0.10), (16, 0.12), (32, 0.16), (64, 0.22), (150, 0.30)):
        v += octave(g, w)
    v += 0.55 * (rng.random((n, n)) - 0.5)
    lo_p, hi_p = np.percentile(v, 1.0), np.percentile(v, 99.0)
    v = np.clip((v - lo_p) / max(1e-6, hi_p - lo_p), 0, 1)
    v = 0.5 + 0.5 * np.tanh((v - 0.5) * 1.6)
    return np.clip(v, 0, 1)


def paste_grain(img, x, y, tile_px, seed, lo=62, hi=178, ox=0.0, oy=0.0):
    """Paint one opaque grayscale grain tile; v is the seamless field."""
    v = grain_field(int(tile_px * S), seed)
    g = np.clip(lo + (hi - lo) * v, 0, 255).astype(np.uint8)
    rgba = np.zeros(v.shape + (4,), np.uint8)
    rgba[..., 0] = g
    rgba[..., 1] = g
    rgba[..., 2] = np.clip(g.astype(int) + 3, 0, 255)
    rgba[..., 3] = 255
    im = Image.fromarray(rgba, "RGBA")
    img.paste(im, (int((x - ox) * S), int((y - oy) * S)), im)


def render(mode, out_path):
    img = Image.new("RGB", (W * S, H * S), C.windowBg)
    d = ImageDraw.Draw(img)

    # --- title strip ------------------------------------------------------
    rect(d, 0, 0, W, TITLE_H, fill=C.windowBg)
    line(d, 0, TITLE_H - 0.5, W, TITLE_H - 0.5, "#3a3a3a", 1)
    rect(d, 14, 7, 16, 16, fill="#3a3a3a", outline=C.border, radius=3)
    rect(d, 18, 11, 8, 8, fill=C.accent, radius=1)
    text(d, 38, 8, "KomaPaint — Brush Studio", 12, 500, C.text)
    for i in range(3):
        bx = W - 30 - i * 24
        if i == 0:
            glyph(d, "close", bx - 5, 10, 11, C.textMuted)
        elif i == 1:
            rect(d, bx - 5, 10, 10, 9, outline=C.iconSubtle, width=1)
        else:
            line(d, bx - 5, 14, bx + 4, 14, C.iconSubtle, 1)

    # --- menu band --------------------------------------------------------
    rect(d, 0, TITLE_H, W, MENU_H, fill=C.menubarBg)
    line(d, 0, TITLE_H + MENU_H - 0.5, W, TITLE_H + MENU_H - 0.5, C.hairline, 1)
    mx = 12
    for item in ("File", "Edit", "View", "Brush", "Window", "Help"):
        text(d, mx, TITLE_H + 8, item, 12, 400, C.textMuted)
        mx += text_w(item, 12, 400) + 22
    _bc = "Brush Studio"
    _bx = W - 12 - text_w(_bc, 11, 400)
    text(d, _bx, TITLE_H + 8, _bc, 11, 400, C.textSubtle)
    glyph(d, "chevron-right", _bx - 16, TITLE_H + 10, 10, C.textSubtle)
    _ex = _bx - 24 - text_w("Edit", 11, 400)
    text(d, _ex, TITLE_H + 8, "Edit", 11, 400, C.textSubtle)

    # --- command bar: identity | management | actions ---------------------
    rect(d, 0, BODY_Y - CMD_H, W, CMD_H, fill=C.toolbarBg)
    line(d, 0, BODY_Y - 0.5, W, BODY_Y - 0.5, C.hairline, 1)
    cy = BODY_Y - CMD_H + (CMD_H - 24) / 2

    rect(d, 14, cy, 24, 24, fill=C.inputBg, outline=C.border, radius=2)
    thumb = Image.new("RGBA", (24 * S, 24 * S), (0, 0, 0, 0))
    paint_stroke(thumb, [(11, 18), (18, 11), (21, 7)], 4.4, 1.4, seed=11, fibre=150,
                 ink=(206, 208, 212), opacity=232)
    img.paste(thumb.convert("RGB"), (int(14 * S), int(cy * S)), thumb)

    name_x = 44
    rect(d, name_x, cy, 204, 24, fill=C.inputBg, outline=C.border, radius=2)
    text(d, name_x + 8, cy + 5, BRUSH, 11, 500, C.text)
    text(d, name_x + 8 + text_w(BRUSH, 11, 500) + 8, cy + 5, "Sub-Tool", 9, 400, C.textSubtle)
    glyph(d, "chevron-down", name_x + 204 - 17, cy + 5, 14, C.iconSubtle)

    eng_x = name_x + 204 + 10
    seg_w = 212
    rect(d, eng_x, cy, seg_w, 24, fill=C.paneBgAlt, outline=C.border, radius=2)
    rect(d, eng_x + 1, cy + 1, seg_w / 2 - 1.5, 22, fill=C.toolActive, radius=2)
    text(d, eng_x + 16, cy + 5, "Digital", 10, 500, C.text)
    text(d, eng_x + seg_w / 2 + 16, cy + 5, "Realistic", 10, 400, C.textSubtle)

    rule_x = eng_x + seg_w + 14
    line(d, rule_x, BODY_Y - CMD_H + 11, rule_x, BODY_Y - 11, C.hairline, 1)

    def cmd_button(x, glyph_name, label):
        pad = 9
        w = pad + 14 + 6 + text_w(label, 10, 500) + pad
        glyph(d, glyph_name, x + pad, cy + 5, 14, C.iconNormal)
        text(d, x + pad + 20, cy + 6, label, 10, 500, C.textMuted)
        return x + w + 6

    bx = rule_x + 16
    for gname, lbl in (("new", "New"), ("duplicate", "Duplicate"),
                       ("folder", "Import"), ("save", "Export")):
        bx = cmd_button(bx, gname, lbl)

    # actions group, with the destination spelled out
    close_w, save_w = 78, 138
    save_x = W - 14 - close_w - 8 - save_w
    line(d, save_x - 16, BODY_Y - CMD_H + 11, save_x - 16, BODY_Y - 11, C.hairline, 1)
    hint = "Saves into KomaPaint's brush library"
    hint_x = save_x - 30 - text_w(hint, 9, 400)
    glyph(d, "folder", hint_x - 17, cy + 7, 12, C.textSubtle)
    text(d, hint_x, cy + 6, hint, 9, 400, C.textMuted)
    rect(d, save_x, cy, save_w, 24, fill=C.accent, radius=2)
    glyph(d, "save", save_x + 10, cy + 5, 14, C.ink)
    text(d, save_x + 32, cy + 6, "Save to Library", 10, 600, C.ink)
    rect(d, W - 14 - close_w, cy, close_w, 24, fill=None, outline=C.border, radius=2)
    glyph(d, "close", W - 14 - close_w + 9, cy + 6, 12, C.iconNormal)
    text(d, W - 14 - close_w + 27, cy + 6, "Close", 10, 500, C.textMuted)

    # --- left panel: the component stack ----------------------------------
    rect(d, 0, BODY_Y, LEFT_W, BODY_H, fill=C.paneBg)
    rect(d, 0, BODY_Y, LEFT_W, 30, fill=C.paneHeaderBg)
    tracked(d, 12, BODY_Y + 10, "COMPONENTS", 9, 600, C.textSection)
    text(d, LEFT_W - 44, BODY_Y + 10, "8", 10, 500, C.textSubtle, anchor="ra")
    rect(d, LEFT_W - 34, BODY_Y + 6, 22, 18, fill=C.buttonBg, radius=2)
    glyph(d, "plus", LEFT_W - 28, BODY_Y + 9, 12, C.iconNormal)
    line(d, 0, BODY_Y + 30, LEFT_W, BODY_Y + 30, "#3f3f3f", 1)

    sy = BODY_Y + 38
    rect(d, 12, sy, LEFT_W - 24, 26, fill=C.inputBg, outline="#555555", radius=2)
    glyph(d, "search", 19, sy + 6, 13, C.iconSubtle)
    text(d, 40, sy + 7, "Search components", 10, 400, C.textSubtle)
    text(d, 12, sy + 32, "Composited top-down · drag to reorder", 9, 400, C.textSubtle)

    gy = sy + 50
    row_h = 52
    for title, rows in COMPONENTS:
        glyph(d, "chevron-down", 12, gy + 4, 11, C.iconSubtle)
        tracked(d, 28, gy + 5, title, 9, 600, C.textSection, 0.8)
        text(d, LEFT_W - 12, gy + 5, str(len(rows)), 9, 500, C.textSubtle, anchor="ra")
        gy += 22
        for name, meta, kind, selected0, visible, seed in rows:
            selected = (name == "Canvas Grain") if mode == "texture" else \
                       (name == "Bristle Round")
            if selected:
                rect(d, 0, gy, LEFT_W, row_h, fill=C.rowSelected)
                rect(d, 0, gy, 2, row_h, fill=C.accent)
            else:
                line(d, 12, gy + row_h - 0.5, LEFT_W - 12, gy + row_h - 0.5, "#404040", 1)
            # drag handle
            glyph(d, "grip", 8, gy + 17, 12, "#6f6f6f" if selected else "#5a5a5a")
            # thumbnail
            tx, ty = 24, gy + 9
            rect(d, tx, ty, 34, 34, fill=C.inputBg, outline="#555555", radius=2)
            if kind == "tip":
                checker(d, tx + 1, ty + 1, 32, 32, cell=5)
                th = Image.new("RGBA", (34 * S, 34 * S), (0, 0, 0, 0))
                bristle_tip(th, (3, 10, 28, 15), seed=seed, density=0.34)
                img.paste(th.convert("RGB"), (tx * S, ty * S), th)
            elif kind == "cus":
                th = Image.new("RGBA", (34 * S, 34 * S), (0, 0, 0, 0))
                paint_stroke(th, [(7, 26), (14, 13), (22, 20), (28, 10)], 3.4, 1.2,
                             seed=seed, fibre=90, ink=(96, 96, 100))
                img.paste(th.convert("RGB"), (tx * S, ty * S), th)
            elif kind == "tex":
                paste_grain(img, tx + 1, ty + 1, 32, seed)
                rect(d, tx, ty, 34, 34, fill=None, outline="#555555", radius=2)
            else:
                rnd = random.Random(seed)
                for _ in range(80):
                    rect(d, tx + 3 + rnd.random() * 27, ty + 3 + rnd.random() * 27, 2, 2,
                         fill="#7d7d7d" if rnd.random() > 0.5 else "#9a9a9a")
            dim = C.textMuted if visible else C.textSubtle
            text(d, 66, gy + 10, name, 11, 500, C.text if selected else dim)
            text(d, 66, gy + 27, meta, 9, 400, C.textSubtle)
            glyph(d, "eye" if visible else "eye-off", LEFT_W - 28, gy + 18, 14,
                  C.iconNormal if visible else "#6a6a6a")
            gy += row_h
        gy += 4

    add_y = BODY_Y + BODY_H - 62
    aw = text_w("Add component", 10, 500)
    rect(d, 12, add_y, LEFT_W - 24 - 34, 26, fill=None, outline="#5f5f5f", radius=2)
    glyph(d, "plus", 12 + (LEFT_W - 24 - 34) / 2 - aw / 2 - 17, add_y + 7, 12, C.iconNormal)
    text(d, 12 + (LEFT_W - 24 - 34) / 2 - aw / 2, add_y + 7, "Add component", 10, 500, C.textMuted)
    rect(d, LEFT_W - 40, add_y, 28, 26, fill=None, outline="#5f5f5f", radius=2)
    glyph(d, "trash", LEFT_W - 34, add_y + 7, 14, C.iconSubtle)
    text(d, 12, add_y + 32, "or drop an image / PSD layer here", 9, 400, C.textSubtle)

    # --- centre: the editor ------------------------------------------------
    rect(d, CENTER_X, BODY_Y, CENTER_W, BODY_H, fill=C.canvasBg)

    rect(d, CENTER_X, BODY_Y, RAIL_W, BODY_H, fill=C.toolRailBg)
    line(d, CENTER_X + RAIL_W - 0.5, BODY_Y, CENTER_X + RAIL_W - 0.5, BODY_Y + BODY_H,
         "#3a3a3a", 1)
    rail_groups = [
        ["brush", "pencil", "eraser", "smudge"],
        ["line", "rectangle", "circle", "curveline"],
        ["lasso", "wand", "selection"],
        ["transform", "move"],
        ["eyedropper", "zoom", "hand"],
    ]
    active_tool = "brush"
    ry = BODY_Y + 8
    for gi, group in enumerate(rail_groups):
        if gi:
            line(d, CENTER_X + 9, ry + 4, CENTER_X + RAIL_W - 9, ry + 4, "#3f3f3f", 1)
            ry += 10
        for name in group:
            if name == active_tool:
                rect(d, CENTER_X + 1, ry, RAIL_W - 2, 34, fill=C.toolActive, radius=2)
                rect(d, CENTER_X + 1, ry + 5, 2, 24, fill=C.accent)
            tool_icon(img, name, CENTER_X + (RAIL_W - 18) / 2, ry + 8, 18,
                      C.text if name == active_tool else C.iconNormal)
            ry += 34

    # editor tabs
    tab_y = BODY_Y
    rect(d, CENTER_X + RAIL_W, tab_y, CENTER_W - RAIL_W, 32, fill=C.paneHeaderBg)
    line(d, CENTER_X + RAIL_W, tab_y + 31.5, RIGHT_X, tab_y + 31.5, C.hairline, 1)
    tabs = [("Bristle Round", mode == "tip"), ("Canvas Grain", mode == "texture"),
            ("Dry Fan", False)]
    tx_ = CENTER_X + RAIL_W + 1
    for lbl, active in tabs:
        tw_ = text_w(lbl, 10, 500) + 40
        if active:
            rect(d, tx_, tab_y + 1, tw_, 31, fill=C.canvasBg)
            rect(d, tx_, tab_y + 1, tw_, 2, fill=C.accent)
        text(d, tx_ + 12, tab_y + 10, lbl, 10, 500, C.text if active else C.textSubtle)
        glyph(d, "close", tx_ + tw_ - 19, tab_y + 11, 10, C.iconSubtle)
        tx_ += tw_ + 1
    rect(d, tx_ + 2, tab_y + 6, 24, 20, fill=None, outline="#4a4a4a", radius=2)
    glyph(d, "plus", tx_ + 8, tab_y + 10, 12, C.iconNormal)

    # editor options: workspace mode, tool settings, history, zoom
    op_y = tab_y + 32
    rect(d, CENTER_X + RAIL_W, op_y, CENTER_W - RAIL_W, 34, fill=C.paneBgAlt)
    line(d, CENTER_X + RAIL_W, op_y + 33.5, RIGHT_X, op_y + 33.5, "#3f3f3f", 1)
    ox = CENTER_X + RAIL_W + 10
    for lbl, key in (("Tip", "tip"), ("Texture", "texture"), ("Nodes", "nodes")):
        wch = text_w(lbl, 10, 500) + 22
        active = (mode == key)
        rect(d, ox, op_y + 6, wch, 22, fill=C.toolActive if active else C.paneBg, radius=2)
        text(d, ox + 11, op_y + 11, lbl, 10, 500, C.text if active else C.textSubtle)
        ox += wch + 4
    ox += 6
    line(d, ox, op_y + 9, ox, op_y + 25, C.hairline, 1)
    ox += 12

    def chip(x, label, value, w=None):
        text(d, x, op_y + 11, label, 9, 400, C.textSubtle)
        vx = x + text_w(label, 9, 400) + 6
        vw = w or (text_w(value, 10, 500) + 30)
        rect(d, vx, op_y + 6, vw, 22, fill=C.inputBg, outline="#555555", radius=2)
        text(d, vx + 7, op_y + 11, value, 10, 500, C.text)
        glyph(d, "chevron-down", vx + vw - 15, op_y + 11, 10, C.iconSubtle)
        return vx + vw + 14

    if mode == "tip":
        ox = chip(ox, "Size", "60 px")
        ox = chip(ox, "Opacity", "100 %")
        ox = chip(ox, "Flow", "82 %")
        line(d, ox, op_y + 9, ox, op_y + 25, C.hairline, 1)
        ox += 12
        rect(d, ox, op_y + 7, 13, 13, fill=None, outline="#6a6a6a", radius=2)
        glyph(d, "check", ox + 1, op_y + 8, 11, C.accent)
        text(d, ox + 19, op_y + 11, "Symmetry", 10, 400, C.textMuted)
        ox += 19 + text_w("Symmetry", 10, 400) + 16
        rect(d, ox, op_y + 7, 13, 13, fill=None, outline="#6a6a6a", radius=2)
        text(d, ox + 19, op_y + 11, "Grid", 10, 400, C.textMuted)
    else:
        ox = chip(ox, "Scale", "100 %")
        ox = chip(ox, "Rotation", "0 °")
        line(d, ox, op_y + 9, ox, op_y + 25, C.hairline, 1)
        ox += 12
        for lbl, on in (("Tile X", True), ("Tile Y", True)):
            rect(d, ox, op_y + 7, 13, 13, fill=None, outline="#6a6a6a", radius=2)
            if on:
                glyph(d, "check", ox + 1, op_y + 8, 11, C.accent)
            text(d, ox + 19, op_y + 11, lbl, 10, 400, C.textMuted)
            ox += 19 + text_w(lbl, 10, 400) + 14
        rect(d, ox, op_y + 7, 13, 13, fill=None, outline="#6a6a6a", radius=2)
        glyph(d, "check", ox + 1, op_y + 8, 11, C.accent)
        text(d, ox + 19, op_y + 11, "Seamless", 10, 400, C.textMuted)

    # history + zoom cluster, right aligned
    zx = RIGHT_X - 14
    text(d, zx - text_w("Fit", 10, 500), op_y + 11, "Fit", 10, 500, C.textMuted)
    zx -= text_w("Fit", 10, 500) + 12
    glyph(d, "plus", zx - 11, op_y + 11, 11, C.iconNormal)
    zx -= 19
    text(d, zx - text_w("100%", 10, 500), op_y + 11, "100%", 10, 500, C.text)
    zx -= text_w("100%", 10, 500) + 8
    glyph(d, "minus", zx - 11, op_y + 11, 11, C.iconNormal)
    zx -= 26
    line(d, zx, op_y + 9, zx, op_y + 25, C.hairline, 1)
    zx -= 10
    glyph(d, "redo", zx - 14, op_y + 10, 14, C.iconNormal)
    zx -= 26
    glyph(d, "undo", zx - 14, op_y + 10, 14, C.iconNormal)

    # canvas
    cv_y = op_y + 34
    cv_h = BODY_H - 32 - 34 - RULER
    cv_x = CENTER_X + RAIL_W
    cv_w = CENTER_W - RAIL_W
    rect(d, cv_x + RULER, cv_y, cv_w - RULER, cv_h, fill=C.canvasBg)
    # left ruler
    rect(d, cv_x, cv_y, RULER, cv_h, fill=C.paneBgAlt)
    line(d, cv_x + RULER - 0.5, cv_y, cv_x + RULER - 0.5, cv_y + cv_h, "#3f3f3f", 1)

    if mode == "tip":
        DOC_W, DOC_H = 1024, 480
        doc_x = cv_x + RULER + (cv_w - RULER - DOC_W) / 2
        doc_y = cv_y + (cv_h - DOC_H) / 2
        rect(d, doc_x - 3, doc_y - 3, DOC_W + 6, DOC_H + 6, fill="#3e3e3e")
        checker(d, doc_x, doc_y, DOC_W, DOC_H, cell=8)
        layer = Image.new("RGBA", (int(DOC_W * S), int(DOC_H * S)), (0, 0, 0, 0))
        bristle_tip(layer, (26, DOC_H * 0.12, DOC_W - 52, DOC_H * 0.34), seed=7, density=1.85,
                    taper=0.10, blur=0.06)
        for i, yy in enumerate((0.59, 0.68, 0.77, 0.86)):
            paint_stroke(layer, [(DOC_W * 0.09, DOC_H * yy), (DOC_W * 0.28, DOC_H * (yy - 0.03)),
                                 (DOC_W * 0.54, DOC_H * (yy - 0.005)), (DOC_W * 0.90, DOC_H * (yy - 0.045))],
                         11 - i * 2, 2, ink=(22, 24, 30), seed=300 + i, fibre=820, dry=0.5,
                         opacity=232)
        img.paste(layer.convert("RGB"), (int(doc_x * S), int(doc_y * S)), layer)
        rect(d, doc_x, doc_y, DOC_W, DOC_H, outline="#5f5f5f", width=1)

        # symmetry axis: the checkbox is on, so show the guide
        axis_x = doc_x + DOC_W / 2
        for yy in range(int(doc_y + 6), int(doc_y + DOC_H - 6), 12):
            line(d, axis_x, yy, axis_x, min(yy + 6, doc_y + DOC_H - 6), "#8f8f8f", 1)

        # a real selection on the mask, with the transform handles it implies
        m_x0, m_x1 = doc_x + 14, doc_x + DOC_W * 0.46
        m_y0, m_y1 = doc_y + DOC_H * 0.06, doc_y + DOC_H * 0.41

        def ants(x0, y0, x1, y1):
            # two-tone marching ants: readable over dark art and light checker
            step = 10
            for xx in range(int(x0), int(x1), step):
                xe = min(xx + step * 0.5, x1)
                line(d, xx, y0, xe, y0, "#ffffff", 1)
                if xe + 0.5 < x1:
                    line(d, xe, y0, min(xe + step * 0.5, x1), y0, "#1a1a1a", 1)
                line(d, xx, y1, xe, y1, "#ffffff", 1)
                if xe + 0.5 < x1:
                    line(d, xe, y1, min(xe + step * 0.5, x1), y1, "#1a1a1a", 1)
            for yy in range(int(y0), int(y1), step):
                ye = min(yy + step * 0.5, y1)
                line(d, x0, yy, x0, ye, "#ffffff", 1)
                if ye + 0.5 < y1:
                    line(d, x0, ye, x0, min(ye + step * 0.5, y1), "#1a1a1a", 1)
                line(d, x1, yy, x1, ye, "#ffffff", 1)
                if ye + 0.5 < y1:
                    line(d, x1, ye, x1, min(ye + step * 0.5, y1), "#1a1a1a", 1)

        ants(m_x0, m_y0, m_x1, m_y1)
        rect(d, m_x0, m_y0, m_x1 - m_x0, m_y1 - m_y0, fill=None,
             outline=(255, 255, 255, 40), width=1)
        handles = [(m_x0, m_y0), (m_x1, m_y0), (m_x0, m_y1), (m_x1, m_y1),
                   ((m_x0 + m_x1) / 2, m_y0), ((m_x0 + m_x1) / 2, m_y1),
                   (m_x0, (m_y0 + m_y1) / 2), (m_x1, (m_y0 + m_y1) / 2)]
        for hx, hy in handles:
            rect(d, hx - 3, hy - 3, 6, 6, fill="#ffffff", outline=C.canvasEdge)
        # rotate handle above the box
        rot_y = m_y0 - 18
        line(d, (m_x0 + m_x1) / 2, m_y0 - 3, (m_x0 + m_x1) / 2, rot_y + 5, "#ffffff", 1)
        d.ellipse([((m_x0 + m_x1) / 2 - 4) * S, (rot_y - 4) * S,
                   ((m_x0 + m_x1) / 2 + 4) * S, (rot_y + 4) * S],
                  fill="#ffffff", outline="#2c2c2c")

        info = "Tip · Bristle Round · 1024 × 480 · mask 8-bit · 100 %"
        iw = text_w(info, 9, 500) + 18
        rect(d, doc_x, doc_y - 26, iw, 20, fill="#333333", radius=2)
        text(d, doc_x + 9, doc_y - 21, info, 9, 500, C.textMuted)
    else:
        # texture tiling preview: the edited tile with its neighbours around it,
        # clipped by the viewport so the seam is judged the way it will be seen
        TILE = 384
        area_x, area_y = cv_x + RULER, cv_y
        area_w, area_h = cv_w - RULER, cv_h
        grid_x = area_x + (area_w - TILE) / 2
        grid_y = area_y + (area_h - TILE) / 2
        tl = Image.new("RGBA", (int(area_w * S), int(area_h * S)), (0, 0, 0, 0))
        checker(d, area_x, area_y, area_w, area_h, cell=8)
        for gy2 in (-1, 0, 1):
            for gx2 in (-1, 0, 1):
                paste_grain(tl, grid_x + gx2 * TILE, grid_y + gy2 * TILE, TILE, 13,
                            ox=area_x, oy=area_y)
        img.paste(tl.convert("RGB"), (int(area_x * S), int(area_y * S)), tl)
        # the edited tile and its seams
        for xx in (grid_x, grid_x + TILE):
            for yy in range(int(area_y + 6), int(area_y + area_h - 6), 10):
                line(d, xx, yy, xx, min(yy + 6, area_y + area_h - 6), "#ededed", 1)
        for yy in (grid_y, grid_y + TILE):
            for xx in range(int(area_x + 6), int(area_x + area_w - 6), 10):
                line(d, xx, yy, min(xx + 6, area_x + area_w - 6), yy, "#ededed", 1)
        rect(d, grid_x, grid_y, TILE, TILE, outline="#cfcfcf", width=1)
        chip_txt = "Seamless · 384 px tile · 100 % · offset 0, 0"
        cw = text_w(chip_txt, 9, 500) + 30
        rect(d, grid_x + 10, grid_y + 10, cw, 20, fill="#333333", radius=2)
        glyph(d, "tile", grid_x + 18, grid_y + 13, 12, C.iconNormal)
        text(d, grid_x + 36, grid_y + 15, chip_txt, 9, 500, C.textMuted)

    # rulers: document pixels, so the scale stays meaningful at any zoom
    ruler_y = cv_y + cv_h
    rect(d, cv_x, ruler_y, cv_w, RULER, fill=C.paneBgAlt)
    if mode == "tip":
        span = DOC_W
        start = doc_x - cv_x
    else:
        span = TILE
        start = grid_x - cv_x
    for v in range(0, span + 1, 32):
        xx = cv_x + start + v
        major = v % 128 == 0
        hgt = 7 if major else 3
        line(d, xx, ruler_y + RULER - hgt, xx, ruler_y + RULER, "#6a6a6a", 1)
        if major:
            text(d, xx + 3, ruler_y + 1, str(v), 8, 400, C.textSubtle)
    # vertical ruler ticks
    for v in range(0, cv_h + 1, 32):
        major = v % 128 == 0
        hgt = 7 if major else 3
        line(d, cv_x + RULER - hgt, cv_y + v, cv_x + RULER, cv_y + v, "#6a6a6a", 1)
        if major and v > 0:
            d.text(((cv_x + 2) * S, (cv_y + v + 2) * S), str(v), font=F(6.5), fill=C.textSubtle)

    # --- right panel: brush settings --------------------------------------
    rect(d, RIGHT_X, BODY_Y, RIGHT_W, BODY_H, fill=C.paneBg)
    rect(d, RIGHT_X, BODY_Y, RIGHT_W, 30, fill=C.paneHeaderBg)
    tracked(d, RIGHT_X + 12, BODY_Y + 10, "BRUSH SETTINGS", 9, 600, C.textSection)
    glyph(d, "search", RIGHT_X + RIGHT_W - 52, BODY_Y + 8, 14, C.iconSubtle)
    glyph(d, "dots-h", RIGHT_X + RIGHT_W - 28, BODY_Y + 8, 14, C.iconSubtle)
    line(d, RIGHT_X, BODY_Y + 30, RIGHT_X + RIGHT_W, BODY_Y + 30, "#3f3f3f", 1)

    row_y = BODY_Y + 36
    FIELD_W = 70

    def value_field(y, value, w=FIELD_W):
        x = RX + RW - w
        rect(d, x, y - 4, w, 20, fill=C.inputBg, outline="#555555", radius=2)
        text(d, x + 7, y, value, 10, 500, C.text)

    def slider_row(y, label, value, frac):
        text(d, RX, y, label, 10, 400, C.textMuted)
        value_field(y, value)
        ty = y + 23
        rect(d, RX, ty, RW, 3, fill=C.inputBg, radius=2)
        rect(d, RX, ty, max(3, RW * frac), 3, fill=C.accent, radius=2)
        kx = RX + RW * frac
        d.ellipse([(kx - 4) * S, (ty - 2.5) * S, (kx + 4) * S, (ty + 5.5) * S],
                  fill="#ededed", outline="#2c2c2c")
        return y + 34

    def section_open(y, title, reset=False):
        glyph(d, "chevron-down", RX - 2, y + 2, 12, C.iconSubtle)
        text(d, RX + 16, y + 1, title, 11, 500, C.text)
        if reset:
            text(d, RX + RW, y + 2, "Reset", 9, 500, C.textSubtle, anchor="ra")
        return y + 24

    def section_closed(y, title, summary):
        glyph(d, "chevron-right", RX - 2, y + 2, 12, C.iconSubtle)
        text(d, RX + 16, y + 1, title, 11, 400, C.textMuted)
        text(d, RX + RW, y + 2, summary, 9, 400, C.textSubtle, anchor="ra")
        y += 22
        line(d, RX, y - 0.5, RX + RW, y - 0.5, "#424242", 1)
        return y + 4

    def field_row(y, label, value, w=FIELD_W):
        text(d, RX, y, label, 10, 400, C.textMuted)
        value_field(y, value, w)
        return y + 26

    def chips_row(y, label, options, active_index, seg_w=None):
        text(d, RX, y, label, 10, 400, C.textMuted)
        y += 16
        n = len(options)
        cw = seg_w or (RW - (n - 1) * 4) / n
        for i, opt in enumerate(options):
            x = RX + i * (cw + 4)
            on = i == active_index
            rect(d, x, y - 4, cw, 22, fill=C.toolActive if on else C.inputBg,
                 outline=None if on else "#555555", radius=2)
            text(d, x + cw / 2, y + 1, opt, 9, 500 if on else 400,
                 C.text if on else C.textSubtle, anchor="ma")
        return y + 26

    if mode == "tip":
        row_y = section_open(row_y, "Brush Tip", reset=True)
        text(d, RX, row_y, "Component", 10, 400, C.textMuted)
        comp_w = RW - 52
        rect(d, RX, row_y + 15, comp_w, 22, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + 8, row_y + 20, "Bristle Round", 10, 500, C.text)
        glyph(d, "chevron-down", RX + comp_w - 17, row_y + 20, 11, C.iconSubtle)
        checker(d, RX + comp_w + 11, row_y + 11, 38, 30, cell=5)
        rect(d, RX + comp_w + 10, row_y + 10, 40, 32, outline="#555555", radius=2)
        t2 = Image.new("RGBA", (40 * S, 32 * S), (0, 0, 0, 0))
        bristle_tip(t2, (4, 11, 32, 11), seed=7, density=0.45)
        img.paste(t2.convert("RGB"), (int((RX + comp_w + 10) * S), int((row_y + 10) * S)), t2)
        row_y += 50
        row_y = slider_row(row_y, "Hardness", "62 %", 0.62)
        row_y = slider_row(row_y, "Density", "88 %", 0.88)
        fy = row_y + 1
        for i, lbl in enumerate(("Flip X", "Flip Y")):
            bx2 = RX + i * 86
            rect(d, bx2, fy, 13, 13, fill=None, outline="#6a6a6a", radius=2)
            if i == 0:
                glyph(d, "check", bx2 + 1, fy + 1, 11, C.accent)
            text(d, bx2 + 20, fy - 1, lbl, 10, 400, C.textMuted)
        row_y = fy + 26
        line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
        row_y += 8

        # Stroke shows the other control families: chips, a select, a toggle
        row_y = section_open(row_y, "Stroke")
        row_y = slider_row(row_y, "Spacing", "1.2 %", 0.12)
        row_y = chips_row(row_y, "Taper", ("In", "Out", "Both"), 2)
        text(d, RX, row_y - 2, "Smoothing", 10, 400, C.textMuted)
        rect(d, RX + RW - 110, row_y - 6, 110, 22, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + RW - 103, row_y - 1, "Balanced", 10, 500, C.text)
        glyph(d, "chevron-down", RX + RW - 17, row_y - 1, 11, C.iconSubtle)
        row_y += 24
        line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
        row_y += 8

        for title, summary in (
            ("Size", "60 px · minimum 8 px · smooth curve"),
            ("Opacity", "100 % · flow 82 %"),
            ("Stabilization", "balanced · 12"),
            ("Rotation", "follow direction"),
            ("Scatter", "18 % · 2 per dab"),
            ("Texture", "Canvas Grain · 74 %"),
            ("Color", "pigment mixing · load 62 %"),
            ("Blending", "multiply · 1.00"),
            ("Dynamics", "pressure, tilt, speed"),
            ("Rendering", "tip anti-alias · 4×"),
        ):
            row_y = section_closed(row_y, title, summary)
    else:
        row_y = section_open(row_y, "Texture", reset=True)
        text(d, RX, row_y, "Component", 10, 400, C.textMuted)
        comp_w = RW - 52
        rect(d, RX, row_y + 15, comp_w, 22, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + 8, row_y + 20, "Canvas Grain", 10, 500, C.text)
        glyph(d, "chevron-down", RX + comp_w - 17, row_y + 20, 11, C.iconSubtle)
        paste_grain(img, RX + comp_w + 11, row_y + 11, 38, 13)
        rect(d, RX + comp_w + 10, row_y + 10, 40, 32, outline="#555555", radius=2)
        row_y += 50
        row_y = chips_row(row_y, "Blend", ("Multiply", "Overlay", "Height"), 0)
        row_y = slider_row(row_y, "Scale", "100 %", 0.5)
        row_y = slider_row(row_y, "Rotation", "0 °", 0.0)
        text(d, RX, row_y, "Offset", 10, 400, C.textMuted)
        rect(d, RX + RW - 150, row_y - 4, 72, 20, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + RW - 143, row_y, "0 px", 10, 500, C.text)
        rect(d, RX + RW - 74, row_y - 4, 72, 20, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + RW - 67, row_y, "0 px", 10, 500, C.text)
        row_y += 26
        rect(d, RX, row_y, 13, 13, fill=None, outline="#6a6a6a", radius=2)
        glyph(d, "check", RX + 1, row_y + 1, 11, C.accent)
        text(d, RX + 20, row_y - 1, "Seamless tiling", 10, 400, C.textMuted)
        text(d, RX + RW, row_y - 1, "edges verified", 9, 400, C.textSubtle, anchor="ra")
        row_y += 24
        row_y += 8
        line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
        row_y += 8

        row_y = section_open(row_y, "Brush Tip")
        text(d, RX, row_y, "Component", 10, 400, C.textMuted)
        rect(d, RX, row_y + 15, RW, 22, fill=C.inputBg, outline="#555555", radius=2)
        text(d, RX + 8, row_y + 20, "Bristle Round", 10, 500, C.text)
        glyph(d, "chevron-down", RX + RW - 17, row_y + 20, 11, C.iconSubtle)
        row_y += 48
        line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
        row_y += 8

        for title, summary in (
            ("Size", "60 px · minimum 8 px"),
            ("Opacity", "100 % · flow 82 %"),
            ("Stroke", "spacing 1.2 % · taper both"),
            ("Stabilization", "balanced · 12"),
            ("Rotation", "follow direction"),
            ("Scatter", "18 % · 2 per dab"),
            ("Color", "pigment mixing · load 62 %"),
            ("Blending", "multiply · 1.00"),
            ("Dynamics", "pressure, tilt, speed"),
            ("Rendering", "tip anti-alias · 4×"),
        ):
            row_y = section_closed(row_y, title, summary)

    glyph(d, "chevron-right", RX - 2, row_y + 2, 12, C.iconSubtle)
    text(d, RX + 16, row_y + 1, "Advanced", 11, 400, C.textMuted)
    text(d, RX + RW, row_y + 2, "0 overrides", 9, 400, C.textSubtle, anchor="ra")
    row_y += 24
    rect(d, RX, row_y, RW, 26, fill=C.paneBgAlt, outline="#5f5f5f", radius=2)
    glyph(d, "nodes", RX + 9, row_y + 6, 13, C.iconNormal)
    text(d, RX + 30, row_y + 7, "Open node graph editor…", 10, 500, C.textMuted)
    text(d, RX + RW - 9, row_y + 7, "Alt N", 9, 400, C.textSubtle, anchor="ra")
    print(mode, "settings used", round(row_y + 26 - BODY_Y, 1), "of", BODY_H - 30)

    foot_y = BODY_Y + BODY_H - 32
    line(d, RIGHT_X, foot_y - 1, RIGHT_X + RIGHT_W, foot_y - 1, "#3f3f3f", 1)
    text(d, RX, foot_y + 9, "12 of 128 settings changed", 9, 400, C.textSubtle)
    text(d, RX + RW, foot_y + 9, "Reset all", 9, 500, C.textMuted, anchor="ra")

    # --- bottom: live preview ---------------------------------------------
    rect(d, 0, PREV_Y, W, PREVIEW_H, fill=C.paneBgAlt)
    line(d, 0, PREV_Y + 0.5, W, PREV_Y + 0.5, C.hairline, 1)
    rect(d, 0, PREV_Y, W, 30, fill=C.paneHeaderBg)
    tracked(d, 12, PREV_Y + 10, "LIVE PREVIEW", 9, 600, C.textSection)
    text(d, 12 + 108, PREV_Y + 10,
         "Rendered by the brush engine as you paint · not a saved preview", 9, 400, C.textSubtle)

    # preview controls, left to right: clear · background · mode · compare
    ctrl = []
    ctrl.append(("clear", None))
    ctrl += [("bg", "Paper"), ("bg", "Dark"), ("bg", "Checker")]
    ctrl.append(("sep", None))
    ctrl += [("mode", "Stroke"), ("mode", "Stamps"), ("mode", "Pressure")]
    ctrl.append(("sep", None))
    ctrl.append(("ab", None))

    def ctrl_width(kind, label):
        if kind == "clear":
            return text_w("Clear preview", 9, 500) + 30
        if kind == "bg":
            return text_w(label, 9, 500) + 18
        if kind == "mode":
            return text_w(label, 9, 500) + 20
        if kind == "sep":
            return 13
        return text_w("A/B", 9, 500) + 28

    total_w = sum(ctrl_width(k, lb) for k, lb in ctrl) + 4 * (len(ctrl) - 1)
    px = W - 14 - total_w
    for kind, label in ctrl:
        wch = ctrl_width(kind, label)
        if kind == "sep":
            line(d, px + 6, PREV_Y + 9, px + 6, PREV_Y + 25, C.hairline, 1)
        elif kind == "clear":
            rect(d, px, PREV_Y + 5, wch, 20, fill=None, outline="#5f5f5f", radius=2)
            glyph(d, "reset", px + 8, PREV_Y + 8, 12, C.iconNormal)
            text(d, px + 24, PREV_Y + 10, "Clear preview", 9, 500, C.textMuted)
        elif kind == "bg":
            on = label == "Paper"
            rect(d, px, PREV_Y + 5, wch, 20, fill=C.toolActive if on else None,
                 outline=None if on else "#5f5f5f", radius=2)
            text(d, px + wch / 2, PREV_Y + 10, label, 9, 500 if on else 400,
                 C.text if on else C.textSubtle, anchor="ma")
        elif kind == "mode":
            on = label == "Stroke"
            rect(d, px, PREV_Y + 5, wch, 20, fill=C.toolActive if on else None,
                 outline=None if on else "#5f5f5f", radius=2)
            text(d, px + wch / 2, PREV_Y + 10, label, 9, 500 if on else 400,
                 C.text if on else C.textSubtle, anchor="ma")
        else:
            rect(d, px, PREV_Y + 5, wch, 20, fill=C.toolActive, radius=2)
            glyph(d, "swap", px + 8, PREV_Y + 9, 11, C.text)
            text(d, px + 22, PREV_Y + 10, "A/B", 9, 500, C.text)
        px += wch + 4

    paper_x, paper_y = 14, PREV_Y + 38
    paper_w, paper_h = RIGHT_X - 28, PREVIEW_H - 48
    rect(d, paper_x, paper_y, paper_w, paper_h, fill=C.previewPaper, radius=2)

    split = paper_x + paper_w / 2
    text(d, paper_x + 14, paper_y + 8, "CURRENT", 8, 600, "#5a626b")
    text(d, split + 14, paper_y + 8, "SAVED · " + BRUSH.upper(), 8, 600, "#5a626b")
    text(d, paper_x + paper_w - 14, paper_y + 8, "800 px · pressure 20 to 100 % · 100 % zoom",
         8, 400, "#7b838c", anchor="ra")

    X0 = paper_x + 96
    X1 = paper_x + paper_w - 24
    mid = split - paper_x

    # one set of stroke paths, painted twice with different settings: a fair wipe
    PATHS = []
    for i, (yy, w0, w1, tilt) in enumerate(((58, 30, 9, 0), (108, 17, 18, 0),
                                            (156, 11, 4, -16))):
        pts = [(X0 - paper_x, yy)]
        n = 6
        for k in range(1, n):
            px_ = X0 - paper_x + (X1 - X0) * k / (n - 1)
            pts.append((px_, yy - 9 * np.sin(k * 1.1 + i * 0.7) - i * 2 + tilt * k / (n - 1)))
        PATHS.append((pts, w0, w1, i))

    def strokes_layer(dry, seed_base):
        lay = Image.new("RGBA", (paper_w * S, paper_h * S), (0, 0, 0, 0))
        for pts, w0, w1, i in PATHS:
            paint_stroke(lay, pts, w0, w1, ink=(26, 30, 38) if i != 1 else (62, 46, 34),
                         seed=seed_base + i, fibre=900, dry=dry, opacity=244)
        return lay

    text(d, paper_x + 16, paper_y + 46, "60 px", 8, 500, "#5a626b")
    text(d, paper_x + 16, paper_y + 96, "60 px dry", 8, 500, "#5a626b")
    text(d, paper_x + 16, paper_y + 144, "18 px", 8, 500, "#5a626b")

    # before/after is a wipe over the same strokes: both sides stay continuous
    cur = strokes_layer(0.42, 40)   # current settings: drier edge, more scatter
    sved = strokes_layer(0.0, 40)   # saved brush: same path, clean edge
    wipe = cur.copy()
    cut = int(mid * S)
    wipe.paste(sved.crop((cut, 0, int(paper_w * S), int(paper_h * S))), (cut, 0))
    img.paste(wipe.convert("RGB"), (paper_x * S, paper_y * S), wipe)

    # the split itself, with a grab handle
    line(d, split, paper_y + 4, split, paper_y + paper_h - 4, "#9aa1a8", 1)
    rect(d, split - 7, paper_y + paper_h / 2 - 11, 14, 22, fill=C.paneBg, radius=3)
    glyph(d, "swap", split - 5, paper_y + paper_h / 2 - 5, 10, C.textMuted)

    # right column: stamps, sizes, engine note
    rx2 = RIGHT_X
    rw2 = RIGHT_W - 14
    rect(d, rx2, paper_y, rw2, paper_h, fill=C.inputBg, radius=2)
    tracked(d, rx2 + 10, paper_y + 9, "STAMPS · SPACING 1.2 %", 9, 600, C.textSection, 0.6)
    lay2 = Image.new("RGBA", (rw2 * S, paper_h * S), (0, 0, 0, 0))
    for i, (r, press) in enumerate(((6, 0.45), (8, 0.6), (9, 0.75), (10, 0.88), (10, 1.0), (10, 1.0))):
        stamp(lay2, 24 + i * 44, 50, r, seed=60 + i, ink=(232, 236, 242), press=press)
        text(d, rx2 + 20 + i * 44, paper_y + 66, ["20", "40", "60", "80", "90", "100"][i],
             8, 400, C.textSubtle)
    img.paste(lay2.convert("RGB"), (rx2 * S, paper_y * S), lay2)
    line(d, rx2 + 10, paper_y + 88, rx2 + rw2 - 10, paper_y + 88, "#3f3f3f", 1)
    tracked(d, rx2 + 10, paper_y + 96, "SIZES", 9, 600, C.textSection, 0.6)
    lay3 = Image.new("RGBA", (rw2 * S, paper_h * S), (0, 0, 0, 0))
    for i, wd in enumerate((4, 7, 11, 16)):
        yy = 128 + i * 15
        paint_stroke(lay3, [(18, yy), (110, yy - 2), (238, yy)], wd, wd * 0.92,
                     seed=80 + i, fibre=180, ink=(226, 230, 236), opacity=240)
        text(d, rx2 + 250, paper_y + yy - 5, str(wd * 4), 8, 400, C.textSubtle)
    img.paste(lay3.convert("RGB"), (rx2 * S, paper_y * S), lay3)
    text(d, rx2 + 10, paper_y + paper_h - 16,
         "Digital engine · 4× tip anti-alias · " + ("tip" if mode == "tip" else "texture") + " mode",
         9, 400, C.textSubtle)

    # --- status bar -------------------------------------------------------
    rect(d, 0, H - STATUS_H, W, STATUS_H, fill=C.statusbarBg)
    line(d, 0, H - STATUS_H + 0.5, W, H - STATUS_H + 0.5, C.hairline, 1)
    text(d, 12, H - STATUS_H + 7, "Sub-Tool · Brush / Digital / " + BRUSH, 10, 400, C.textSubtle)
    text(d, W / 2, H - STATUS_H + 7,
         "8 components  ·  1 hidden  ·  composited top-down", 10, 400, C.textSubtle, anchor="ma")
    d.ellipse([(W - 158) * S, (H - STATUS_H + 9) * S, (W - 151) * S, (H - STATUS_H + 16) * S],
              fill=C.success)
    text(d, W - 12, H - STATUS_H + 7, "preview 6.1 ms · saved", 10, 400, C.textSubtle, anchor="ra")

    # --- frame ------------------------------------------------------------
    line(d, 0.5, 0, 0.5, H, "#4f4f4f", 1)
    line(d, W - 0.5, 0, W - 0.5, H, "#4f4f4f", 1)
    line(d, 0, H - 0.5, W, H - 0.5, "#4f4f4f", 1)

    out = img.resize((W, H), Image.LANCZOS)
    out.save(out_path)
    print("wrote", out_path, out.size)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    render("tip", os.path.join(here, "brush-studio.png"))
    render("texture", os.path.join(here, "brush-studio-texture.png"))
