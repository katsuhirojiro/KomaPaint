#!/usr/bin/env python3
"""Brush Studio mockup renderer.

Draws a 1600x1000 desktop window at 2x and downsamples, so every edge and
glyph is antialiased. Colour, type and spacing come straight from
ui/themes/KomaTheme.qml ("ink and ember"), which is why the result belongs to
KomaPaint rather than to a generic dark editor.
"""

import math
import random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 2                      # supersample factor
W, H = 1600, 1000          # design size, in points
FDIR = "/tmp/mock/fonts"

# --- tokens (dark theme, KomaTheme.qml) -------------------------------------
class C:
    windowBg      = "#2c2c2c"
    paneBg        = "#383838"
    paneBgAlt     = "#333333"
    paneHeaderBg  = "#2c2c2c"
    panelInsetBg  = "#292929"
    canvasBg      = "#454545"
    canvasEdge    = "#1b1d20"
    previewPaper  = "#dde1e6"
    inputBg       = "#292929"
    buttonBg      = "#404040"
    buttonHover   = "#4a4a4a"
    toolRailBg    = "#292929"
    statusbarBg   = "#2c2c2c"
    toolActive    = "#505050"
    rowSelected   = "#505050"
    transparencyLight = "#828282"
    transparencyDark  = "#666666"
    hairline      = "#5c5c5c"
    border        = "#666666"
    outline       = "#888888"
    text          = "#ededed"
    textMuted     = "#c6c6c6"
    textSubtle    = "#909090"
    textSection   = "#bdbdbd"
    accent        = "#c8c8c8"
    accentWash    = "#505050"
    warning       = "#d2a054"
    success       = "#7fb27f"
    ink           = "#222222"


_fonts = {}
def F(size, weight=400):
    key = (size, weight)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(f"{FDIR}/Inter-{weight}.ttf", int(round(size * S)))
    return _fonts[key]


def rect(d, x, y, w, h, fill=None, outline=None, width=1, radius=0):
    box = [x * S, y * S, (x + w) * S - 1, (y + h) * S - 1]
    if radius:
        d.rounded_rectangle(box, radius=radius * S, fill=fill,
                            outline=outline, width=int(width * S))
    else:
        d.rectangle(box, fill=fill, outline=outline, width=int(width * S) if outline else 0)


def line(d, x1, y1, x2, y2, color, width=1):
    d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=color, width=max(1, int(width * S)))


def text(d, x, y, s, size=11, weight=400, color=C.text, anchor="la"):
    d.text((x * S, y * S), s, font=F(size, weight), fill=color, anchor=anchor)


def text_w(s, size=11, weight=400):
    return F(size, weight).getlength(s) / S


def tracked(d, x, y, s, size=9, weight=600, color=C.textSection, tracking=0.9, anchor="l"):
    """Uppercase micro label with letterspacing, as the theme uses above sections."""
    total = sum(text_w(ch, size, weight) + tracking for ch in s) - tracking
    if anchor == "r":
        x -= total
    cx = x
    for ch in s:
        d.text((cx * S, y * S), ch, font=F(size, weight), fill=color, anchor="ls")
        cx += text_w(ch, size, weight) + tracking
    return total


# --- icons: single-colour line art, 1.5 pt strokes, the KomaPaint icon style -
def icon(d, name, x, y, size, color, alpha_color=None):
    """Draw a 1.5 pt line icon inside a size x size box at (x, y)."""
    col = alpha_color or color
    lw = 1.4
    cx, cy = x + size / 2, y + size / 2

    def L(a, b, c, e):
        line(d, x + a * size, y + b * size, x + c * size, y + e * size, col, lw)

    def E(bx0, by0, bx1, by1, **kw):
        d.ellipse([(x + bx0 * size) * S, (y + by0 * size) * S,
                   (x + bx1 * size) * S, (y + by1 * size) * S], **kw)

    def POLY(points, **kw):
        d.polygon([((x + a * size) * S, (y + b * size) * S) for a, b in points], **kw)

    def ARC(bx0, by0, bx1, by1, a0, a1, **kw):
        d.arc([(x + bx0 * size) * S, (y + by0 * size) * S,
               (x + bx1 * size) * S, (y + by1 * size) * S], a0, a1, **kw)

    if name == "brush":
        L(0.30, 0.78, 0.62, 0.30)
        line(d, x + 0.62 * size, y + 0.30 * size, x + 0.74 * size, y + 0.20 * size, col, lw * 1.7)
        L(0.20, 0.86, 0.30, 0.78)
        L(0.20, 0.86, 0.26, 0.72)
    elif name == "eraser":
        POLY([(0.22, 0.70), (0.55, 0.28), (0.80, 0.46), (0.50, 0.82)],
             outline=col, width=max(1, int(lw * S)))
        L(0.22, 0.70, 0.50, 0.82)
    elif name == "smudge":
        L(0.25, 0.72, 0.62, 0.35)
        L(0.42, 0.78, 0.78, 0.45)
        L(0.25, 0.72, 0.42, 0.78)
    elif name == "shape":
        rect(d, x + 0.18 * size, y + 0.42 * size, 0.42 * size, 0.40 * size, outline=col, width=lw)
        E(0.52, 0.14, 0.86, 0.48, outline=col, width=max(1, int(lw * S)))
    elif name == "lasso":
        ARC(0.16, 0.14, 0.84, 0.66, 200, 20, fill=col, width=max(1, int(lw * S)))
        L(0.24, 0.52, 0.28, 0.80)
        L(0.28, 0.80, 0.40, 0.84)
    elif name == "select":
        for a, b, c, e in ((0.20, 0.20, 0.34, 0.20), (0.66, 0.20, 0.80, 0.20),
                           (0.20, 0.80, 0.34, 0.80), (0.66, 0.80, 0.80, 0.80),
                           (0.20, 0.20, 0.20, 0.34), (0.20, 0.66, 0.20, 0.80),
                           (0.80, 0.20, 0.80, 0.34), (0.80, 0.66, 0.80, 0.80)):
            L(a, b, c, e)
    elif name == "move":
        L(0.50, 0.18, 0.50, 0.82)
        L(0.18, 0.50, 0.82, 0.50)
        L(0.42, 0.26, 0.50, 0.18)
        L(0.58, 0.26, 0.50, 0.18)
        L(0.42, 0.74, 0.50, 0.82)
        L(0.58, 0.74, 0.50, 0.82)
    elif name == "fill":
        POLY([(0.22, 0.52), (0.52, 0.22), (0.80, 0.50), (0.50, 0.80)],
             outline=col, width=max(1, int(lw * S)))
        E(0.62, 0.66, 0.80, 0.86, fill=col)
    elif name == "dropper":
        L(0.26, 0.78, 0.58, 0.44)
        line(d, x + 0.56 * size, y + 0.26 * size, x + 0.76 * size, y + 0.46 * size, col, lw * 1.6)
        L(0.26, 0.78, 0.20, 0.82)
    elif name == "zoom":
        E(0.18, 0.18, 0.66, 0.66, outline=col, width=max(1, int(lw * S)))
        L(0.68, 0.68, 0.86, 0.86)
    elif name == "plus":
        L(0.50, 0.22, 0.50, 0.78)
        L(0.22, 0.50, 0.78, 0.50)
    elif name == "copy":
        rect(d, x + 0.18 * size, y + 0.18 * size, 0.44 * size, 0.50 * size, outline=col, width=lw)
        rect(d, x + 0.36 * size, y + 0.34 * size, 0.46 * size, 0.48 * size, outline=col, width=lw)
    elif name == "import":
        L(0.50, 0.16, 0.50, 0.58)
        L(0.36, 0.44, 0.50, 0.58)
        L(0.64, 0.44, 0.50, 0.58)
        L(0.20, 0.70, 0.20, 0.84)
        L(0.20, 0.84, 0.80, 0.84)
        L(0.80, 0.70, 0.80, 0.84)
    elif name == "export":
        L(0.50, 0.58, 0.50, 0.16)
        L(0.36, 0.30, 0.50, 0.16)
        L(0.64, 0.30, 0.50, 0.16)
        L(0.20, 0.70, 0.20, 0.84)
        L(0.20, 0.84, 0.80, 0.84)
        L(0.80, 0.70, 0.80, 0.84)
    elif name == "save":
        L(0.22, 0.78, 0.22, 0.24)
        L(0.22, 0.24, 0.68, 0.24)
        L(0.68, 0.24, 0.80, 0.36)
        L(0.80, 0.36, 0.80, 0.78)
        L(0.22, 0.78, 0.80, 0.78)
        rect(d, x + 0.36 * size, y + 0.24 * size, 0.30 * size, 0.20 * size, fill=col)
    elif name == "close":
        L(0.26, 0.26, 0.74, 0.74)
        L(0.74, 0.26, 0.26, 0.74)
    elif name == "chevron":
        L(0.38, 0.30, 0.60, 0.50)
        L(0.60, 0.50, 0.38, 0.70)
    elif name == "chevron_down":
        L(0.28, 0.42, 0.50, 0.62)
        L(0.50, 0.62, 0.72, 0.42)
    elif name == "nodes":
        E(0.12, 0.16, 0.36, 0.40, outline=col, width=max(1, int(lw * S)))
        E(0.62, 0.58, 0.86, 0.82, outline=col, width=max(1, int(lw * S)))
        L(0.36, 0.28, 0.70, 0.28)
        L(0.70, 0.28, 0.74, 0.58)
        L(0.36, 0.36, 0.54, 0.66)
        L(0.54, 0.66, 0.62, 0.70)
    elif name == "search":
        E(0.18, 0.18, 0.68, 0.68, outline=col, width=max(1, int(lw * S)))
        L(0.70, 0.70, 0.86, 0.86)
    elif name == "undo":
        ARC(0.20, 0.26, 0.80, 0.86, 200, 20, fill=col, width=max(1, int(lw * S)))
        L(0.20, 0.46, 0.20, 0.26)
        L(0.20, 0.26, 0.38, 0.26)
    elif name == "warning":
        POLY([(0.50, 0.16), (0.88, 0.82), (0.12, 0.82)], outline=col, width=max(1, int(lw * S)))
        L(0.50, 0.40, 0.50, 0.60)
        line(d, x + 0.50 * size, y + 0.68 * size, x + 0.50 * size, y + 0.72 * size, col, lw * 1.6)
    elif name == "eye":
        E(0.12, 0.26, 0.88, 0.74, outline=col, width=max(1, int(lw * S)))
        E(0.42, 0.40, 0.58, 0.60, fill=col)
    elif name == "grid":
        rect(d, x + 0.18 * size, y + 0.18 * size, 0.64 * size, 0.64 * size, outline=col, width=lw)
        L(0.18, 0.50, 0.82, 0.50)
        L(0.50, 0.18, 0.50, 0.82)
    elif name == "dots":
        for i, j in ((0.30, 0.50), (0.50, 0.50), (0.70, 0.50)):
            E(i - 0.05, j - 0.05, i + 0.05, j + 0.05, fill=col)
    elif name == "reset":
        ARC(0.20, 0.20, 0.80, 0.80, 30, 330, fill=col, width=max(1, int(lw * S)))
        L(0.72, 0.20, 0.86, 0.20)
        L(0.86, 0.20, 0.86, 0.34)



# --- composite artwork ------------------------------------------------------
def checker(d, x, y, w, h, cell=8):
    rect(d, x, y, w, h, fill=C.transparencyLight)
    for j in range(int(math.ceil(h / cell))):
        for i in range(int(math.ceil(w / cell))):
            if (i + j) % 2:
                x0, y0 = x + i * cell, y + j * cell
                rect(d, x0, y0, min(cell, x + w - x0), min(cell, y + h - y0),
                     fill=C.transparencyDark)


def bristle_tip(layer, box, seed=7, density=1.0, blur=0.22, ink=(11, 11, 13)):
    """A brush-tip mask, built as a field: silhouette x thickness x bristles.

    Working on a field rather than on drawn lines is what makes it read as a
    loaded bristle imprint instead of a picket fence.
    """
    x, y, w, h = [v * S for v in box]
    wp, hp = max(2, int(w)), max(2, int(h))
    rng = np.random.default_rng(seed)
    fx = np.linspace(0, 1, wp)[None, :]
    fy = np.linspace(0, 1, hp)[:, None]

    ex = np.clip((fx - 0.015) / 0.97, 0, 1)
    ex = np.sin(np.pi * ex) ** 0.28
    ex = ex * (0.84 + 0.16 * np.sin(fx * 6.7 + seed) * np.sin(fx * 3.1 + seed * 0.6))
    half = 0.30 + 0.10 * np.sin(fx * 5.3 + 1.7) + 0.04 * np.sin(fx * 11.0)
    ey = np.exp(-(((fy - 0.5) / half) ** 4) * 2.2)

    freqs = np.array([37, 61, 97, 131, 173, 211, 257, 307, 349], float)
    ph = rng.uniform(0, 2 * np.pi, freqs.size)
    fibre = np.sum(np.sin(fx[0][:, None] * 2 * np.pi * freqs[None, :] + ph[None, :]), axis=1)[None, :] / freqs.size
    fibre = 0.62 + 0.38 * fibre

    cfreq = np.array([5.0, 9.0, 13.0, 21.0, 33.0])
    cph = rng.uniform(0, 2 * np.pi, cfreq.size)
    clump = np.sum(np.sin(fx[0][:, None] * 2 * np.pi * cfreq[None, :] + cph[None, :]), axis=1)[None, :] / cfreq.size
    clump = np.clip(0.72 + 0.45 * clump, 0.10, 1.05)

    # per-column random walk: real bristles bunch unevenly along the stroke
    walk = np.cumsum(rng.normal(0, 0.05, wp))
    walk = np.clip(0.88 + 0.12 * np.tanh(walk), 0.6, 1.1)

    a = ex * ey * fibre * clump * walk * density
    a = np.clip(a, 0, 1) ** 0.62
    # the wet centre of the band carries more pigment than its edges
    a = a * (0.92 + 0.16 * np.exp(-(((fy - 0.5) / 0.22) ** 2)))
    alpha = np.clip(a * 322, 0, 255).astype(np.uint8)
    rgba = np.zeros((hp, wp, 4), np.uint8)
    rgba[..., 0], rgba[..., 1], rgba[..., 2] = ink
    rgba[..., 3] = alpha
    im = Image.fromarray(rgba, "RGBA")
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur * S))
    layer.alpha_composite(im, (int(x), int(y)))


def paint_stroke(layer, pts, w_start, w_end, ink=(26, 30, 38), seed=1,
                 fibre=420, dry=0.0, opacity=240, taper=0.55):
    """A tapered, fibre-split stroke along a polyline - the actual brush look.

    Nothing is ever erased: dry-brush variation comes from thinning the fibres
    in bands, which is what a real brush does when the reservoir runs low.
    """
    d = ImageDraw.Draw(layer)
    rnd = random.Random(seed)
    pts = [(p[0] * S, p[1] * S) for p in pts]
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

    # the comb: a fixed bristle profile per stroke, so the striations run along
    # the stroke instead of shimmering from dab to dab
    comb = [rnd.uniform(0.72, 1.0) for _ in range(11)]
    for _ in range(2):
        comb[rnd.randrange(11)] = rnd.uniform(0.10, 0.26)

    def normal(k):
        a = samples[max(0, k - 1)]
        bb = samples[min(total - 1, k + 1)]
        tx, ty = bb[0] - a[0], bb[1] - a[1]
        ln = math.hypot(tx, ty) or 1.0
        return -ty / ln, tx / ln

    # soft halo under everything
    for k, (px_, py_) in enumerate(samples):
        r = radius(k / max(1, total - 1)) / 2
        d.ellipse([px_ - r * 1.2, py_ - r * 1.2, px_ + r * 1.2, py_ + r * 1.2],
                  fill=ink + (34,))
    # core with bristle striations
    for k, (px_, py_) in enumerate(samples):
        t = k / max(1, total - 1)
        r = radius(t) / 2
        nx, ny = normal(k)
        # a faint base keeps the gaps from cutting the stroke in half
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
        # low-frequency reservoir variation along the stroke
        return 0.62 + 0.38 * math.sin(t * 9.0 + seed) * math.sin(t * 3.7 + seed * 0.5)

    # bristle fibres: they only stray a little outside the core and thin out
    # where the reservoir is low. ImageDraw replaces pixels instead of blending
    # them, so they go on their own layer and are composited in.
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
    """One bristle dab: short radial fibres inside a soft disc."""
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


# ---------------------------------------------------------------------------
img = Image.new("RGB", (W * S, H * S), C.windowBg)
d = ImageDraw.Draw(img)

TITLE_H, CMD_H, STATUS_H, PREVIEW_H = 30, 40, 24, 152
LEFT_W, RIGHT_W, RAIL_W = 248, 300, 34
BODY_Y = TITLE_H + CMD_H
BODY_H = H - BODY_Y - PREVIEW_H - STATUS_H
PREV_Y = BODY_Y + BODY_H
LEFT_X, RIGHT_X = 0, W - RIGHT_W
CENTER_X = LEFT_W
CENTER_W = RIGHT_X - CENTER_X

# --- title bar -------------------------------------------------------------
rect(d, 0, 0, W, TITLE_H, fill=C.windowBg)
line(d, 0, TITLE_H - 0.5, W, TITLE_H - 0.5, "#3a3a3a", 1)
rect(d, 14, 7, 16, 16, fill="#3a3a3a", outline=C.border, radius=3)
rect(d, 18, 11, 8, 8, fill=C.accent, radius=1)
text(d, 38, 8, "Brush Studio", 12, 500, C.text)
text(d, 38 + text_w("Brush Studio", 12, 500) + 8, 9, "— Krita5 prototypes 1", 11, 400, C.textSubtle)
# window controls, drawn rather than typed
for i in range(3):
    bx = W - 26 - i * 24
    if i == 0:
        icon(d, "close", bx - 6, 9, 12, C.textMuted)
    elif i == 1:
        rect(d, bx - 5, 10, 10, 9, outline=C.textSubtle, width=1)
    else:
        line(d, bx - 6, 14, bx + 4, 14, C.textSubtle, 1)

# --- command bar -----------------------------------------------------------
rect(d, 0, TITLE_H, W, CMD_H, fill=C.windowBg)
line(d, 0, TITLE_H + CMD_H - 0.5, W, TITLE_H + CMD_H - 0.5, "#3a3a3a", 1)
cy = TITLE_H + (CMD_H - 24) / 2

rect(d, 14, TITLE_H + 6, 28, 28, fill=C.inputBg, outline=C.border, radius=2)
thumb = Image.new("RGBA", (28 * S, 28 * S), (0, 0, 0, 0))
paint_stroke(thumb, [(13, 20), (21, 13), (25, 8)], 5.0, 1.6, seed=11, fibre=160, opacity=230)
img.paste(thumb.convert("RGB"), (14 * S, (TITLE_H + 6) * S), thumb)

name_x = 50
rect(d, name_x, cy, 190, 24, fill=C.inputBg, outline=C.border, radius=2)
text(d, name_x + 8, cy + 5, "Krita5 prototypes 1", 11, 500, C.text)
icon(d, "chevron_down", name_x + 190 - 18, cy + 5, 14, C.textSubtle)

eng_x = name_x + 190 + 8
seg_w = 236
rect(d, eng_x, cy, seg_w, 24, fill=C.paneBgAlt, outline=C.border, radius=2)
rect(d, eng_x + 1, cy + 1, seg_w / 2 - 1.5, 22, fill=C.toolActive, radius=2)
icon(d, "warning", eng_x + 10, cy + 6, 12, C.warning)
text(d, eng_x + 26, cy + 5, "Pixel Engine", 10, 500, C.text)
text(d, eng_x + seg_w / 2 + 12, cy + 5, "Realistic Engine", 10, 400, C.textSubtle)
note = "Digital brush · 128 settings"
text(d, eng_x + seg_w + 10, cy + 5, note, 10, 400, C.textSubtle)

div_x = eng_x + seg_w + 10 + text_w(note, 10) + 16
line(d, div_x, TITLE_H + 8, div_x, TITLE_H + CMD_H - 8, "#3f3f3f", 1)

btn_y = cy
x = div_x + 12


def cmd_button(x, icon_name, label):
    pad = 9
    w = pad + 14 + 6 + text_w(label, 10, 500) + pad
    rect(d, x, btn_y, w, 24, fill=C.buttonBg, radius=2)
    icon(d, icon_name, x + pad, btn_y + 5, 14, C.textMuted)
    text(d, x + pad + 20, btn_y + 6, label, 10, 500, C.textMuted)
    return x + w + 6


for ic, lbl in (("plus", "New"), ("copy", "Duplicate"), ("import", "Import"), ("export", "Export")):
    x = cmd_button(x, ic, lbl)

close_w, save_w = 74, 96
save_x = W - 14 - close_w - 8 - save_w
line(d, save_x - 14, TITLE_H + 8, save_x - 14, TITLE_H + CMD_H - 8, "#3f3f3f", 1)
rect(d, save_x, btn_y, save_w, 24, fill=C.accent, radius=2)
icon(d, "save", save_x + 10, btn_y + 5, 14, C.ink)
text(d, save_x + 30, btn_y + 6, "Save Brush", 10, 600, C.ink)
rect(d, W - 14 - close_w, btn_y, close_w, 24, fill=None, outline=C.border, radius=2)
icon(d, "close", W - 14 - close_w + 9, btn_y + 6, 12, C.textMuted)
text(d, W - 14 - close_w + 26, btn_y + 6, "Close", 10, 500, C.textMuted)

# --- left panel: brush components -----------------------------------------
rect(d, LEFT_X, BODY_Y, LEFT_W, BODY_H, fill=C.paneBg)
rect(d, LEFT_X, BODY_Y, LEFT_W, 30, fill=C.paneHeaderBg)
tracked(d, 12, BODY_Y + 10, "BRUSH COMPONENTS", 9, 600, C.textSection)
text(d, LEFT_W - 12, BODY_Y + 10, "12", 10, 500, C.textSubtle, anchor="ra")
line(d, LEFT_X, BODY_Y + 30, LEFT_X + LEFT_W, BODY_Y + 30, "#3f3f3f", 1)

chip_y = BODY_Y + 38
cx0 = 12
for lbl, active in (("3 TIPS", True), ("2 TEXTURES", False), ("1 IMAGE", False)):
    wch = text_w(lbl, 9, 600) + 14
    rect(d, cx0, chip_y, wch, 18, fill=C.toolActive if active else C.paneBgAlt, radius=2)
    tracked(d, cx0 + 7, chip_y + 5, lbl, 9, 600, C.text if active else C.textSubtle, 0.7)
    cx0 += wch + 5
text(d, 12, chip_y + 24, "Composited top-down · drag to reorder", 9, 400, C.textSubtle)

components = [
    ("Bristle smear 03", "TIP · 1024 × 320 px", "tip", True, 3),
    ("Fan bristle 01", "TIP · 1024 × 320 px", "tip", False, 5),
    ("Dry edge streak", "TIP · 512 × 512 px", "tip", False, 9),
    ("Grain 02", "TEXTURE · 1024 × 1024", "tex", False, 13),
    ("canvas-weave.png", "IMAGE · 2048 × 2048", "img", False, 17),
    ("Spike scatter", "TIP · 256 × 256 px", "tip", False, 21),
    ("Paper fibre scan", "IMAGE · 4096 × 4096", "img", False, 25),
    ("Grit noise 07", "TEXTURE · 512 × 512", "tex", False, 29),
    ("Smudge residue", "CUSTOM · shape mask", "cus", False, 33),
    ("Wet edge profile", "CUSTOM · curve", "cus", False, 37),
]
row_h = 44
list_y = chip_y + 42
for name, sub, kind, selected, seed in components:
    ry = list_y
    if selected:
        rect(d, LEFT_X, ry, LEFT_W, row_h, fill=C.rowSelected)
        rect(d, LEFT_X, ry, 2, row_h, fill=C.accent)
    else:
        line(d, LEFT_X + 12, ry + row_h - 0.5, LEFT_X + LEFT_W - 12, ry + row_h - 0.5, "#404040", 1)
    tx, ty = 12, ry + 6
    rect(d, tx, ty, 32, 32, fill=C.inputBg, outline="#555555", radius=2)
    if kind == "tip":
        th = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
        bristle_tip(th, (3, 9, 26, 14), seed=seed, density=0.30, ink=(70, 70, 74), blur=0.2)
        img.paste(th.convert("RGB"), (tx * S, ty * S), th)
    elif kind == "cus":
        th = Image.new("RGBA", (32 * S, 32 * S), (0, 0, 0, 0))
        paint_stroke(th, [(6, 24), (13, 12), (21, 19), (26, 9)], 3.2, 1.2, seed=seed, fibre=90)
        img.paste(th.convert("RGB"), (tx * S, ty * S), th)
    elif kind == "tex":
        rnd = random.Random(seed)
        for i in range(9):
            rect(d, tx + 3, ty + 4 + i * 3, 26, 1, fill="#6e6e6e" if i % 2 else "#8a8a8a")
        for _ in range(40):
            rect(d, tx + 3 + rnd.randrange(24), ty + 4 + rnd.randrange(24), 2, 1, fill="#555555")
    else:
        rnd = random.Random(seed)
        for _ in range(70):
            rect(d, tx + 3 + rnd.random() * 24, ty + 3 + rnd.random() * 24, 2, 2,
                 fill="#7d7d7d" if rnd.random() > 0.5 else "#9a9a9a")
    text(d, 52, ry + 8, name, 11, 500, C.text if selected else C.textMuted)
    text(d, 52, ry + 24, sub, 9, 400, C.textSubtle)
    tag = {"tip": "TIP", "tex": "TEX", "img": "IMG", "cus": "CST"}[kind]
    tw_ = text_w(tag, 9, 600) + 10
    rect(d, LEFT_W - 12 - tw_, ry + 13, tw_, 16, fill="#3f3f3f", radius=2)
    tracked(d, LEFT_W - 12 - tw_ + 5, ry + 17, tag, 9, 600, C.textMuted, 0.6)
    list_y += row_h

add_y = BODY_Y + BODY_H - 54
rect(d, 12, add_y, LEFT_W - 24, 26, fill=None, outline="#5f5f5f", radius=2)
aw = text_w("Add component", 10, 500)
icon(d, "plus", 12 + (LEFT_W - 24) / 2 - aw / 2 - 18, add_y + 7, 12, C.textMuted)
text(d, 12 + (LEFT_W - 24) / 2 - aw / 2, add_y + 7, "Add component", 10, 500, C.textMuted)
text(d, 12, add_y + 32, "or drop an image / PSD layer here", 9, 400, C.textSubtle)

# --- centre: brush tip workspace ------------------------------------------
rect(d, CENTER_X, BODY_Y, CENTER_W, BODY_H, fill=C.canvasBg)

rect(d, CENTER_X, BODY_Y, RAIL_W, BODY_H, fill=C.toolRailBg)
line(d, CENTER_X + RAIL_W - 0.5, BODY_Y, CENTER_X + RAIL_W - 0.5, BODY_Y + BODY_H, "#3a3a3a", 1)
rail_tools = ["brush", "eraser", "smudge", "shape", "select", "lasso", "move", "fill", "dropper", "zoom"]
for i, nm in enumerate(rail_tools):
    by = BODY_Y + 6 + i * 34
    if i == 0:
        rect(d, CENTER_X + 1, by, RAIL_W - 2, 34, fill=C.toolActive, radius=2)
        rect(d, CENTER_X + 1, by + 4, 2, 26, fill=C.accent)
    icon(d, nm, CENTER_X + (RAIL_W - 18) / 2, by + 8, 18, C.text if i == 0 else C.textMuted)

tab_y = BODY_Y
rect(d, CENTER_X + RAIL_W, tab_y, CENTER_W - RAIL_W, 32, fill=C.paneHeaderBg)
line(d, CENTER_X + RAIL_W, tab_y + 31.5, RIGHT_X, tab_y + 31.5, "#3f3f3f", 1)
tabs = [("Tip · Bristle smear 03", True), ("Texture · Grain 02", False), ("Spike scatter", False)]
tx_ = CENTER_X + RAIL_W + 1
for lbl, active in tabs:
    tw_ = text_w(lbl, 10, 500) + 36
    if active:
        rect(d, tx_, tab_y + 1, tw_, 31, fill=C.canvasBg)
        rect(d, tx_, tab_y + 1, tw_, 2, fill=C.accent)
    text(d, tx_ + 12, tab_y + 10, lbl, 10, 500, C.text if active else C.textSubtle)
    icon(d, "close", tx_ + tw_ - 19, tab_y + 11, 10, C.textSubtle)
    tx_ += tw_ + 1
rect(d, tx_ + 2, tab_y + 6, 24, 20, fill=None, outline="#4a4a4a", radius=2)
icon(d, "plus", tx_ + 8, tab_y + 10, 12, C.textSubtle)

cv_y = tab_y + 32
cv_h = BODY_H - 32 - 18
rect(d, CENTER_X + RAIL_W, cv_y, CENTER_W - RAIL_W, cv_h, fill=C.canvasBg)

DOC_PX_W, DOC_PX_H = 1024, 320
scale = 0.88
doc_w, doc_h = DOC_PX_W * scale, DOC_PX_H * scale
doc_x = CENTER_X + RAIL_W + (CENTER_W - RAIL_W - doc_w) / 2
doc_y = cv_y + (cv_h - doc_h) / 2 - 2
rect(d, doc_x - 3, doc_y - 3, doc_w + 6, doc_h + 6, fill="#3e3e3e")
checker(d, doc_x, doc_y, doc_w, doc_h, cell=8)
layer = Image.new("RGBA", (int(doc_w * S), int(doc_h * S)), (0, 0, 0, 0))
bristle_tip(layer, (18, doc_h * 0.14, doc_w - 36, doc_h * 0.36), seed=7, density=1.4)
for i, yy in enumerate((0.62, 0.72, 0.82, 0.91)):
    paint_stroke(layer, [(doc_w * 0.10, doc_h * yy), (doc_w * 0.30, doc_h * (yy - 0.035)),
                         (doc_w * 0.56, doc_h * (yy - 0.005)), (doc_w * 0.88, doc_h * (yy - 0.05))],
                 10 - i * 2, 2, ink=(24, 26, 32), seed=300 + i, fibre=760, dry=0.5, opacity=230)
img.paste(layer.convert("RGB"), (int(doc_x * S), int(doc_y * S)), layer)
rect(d, doc_x, doc_y, doc_w, doc_h, outline=C.canvasEdge, width=1)

# selection marquee, marching-ants style
m_x0, m_x1 = doc_x + 8, doc_x + doc_w - 8
m_y0, m_y1 = doc_y + doc_h * 0.11, doc_y + doc_h * 0.96


def ants(x0, y0, x1, y1):
    step = 9
    for xx in range(int(x0), int(x1), step):
        x_end = min(xx + step * 0.55, x1)
        line(d, xx, y0, x_end, y0, "#ffffff", 1)
        line(d, xx, y1, x_end, y1, "#ffffff", 1)
    for yy in range(int(y0), int(y1), step):
        y_end = min(yy + step * 0.55, y1)
        line(d, x0, yy, x0, y_end, "#ffffff", 1)
        line(d, x1, yy, x1, y_end, "#ffffff", 1)


ants(m_x0, m_y0, m_x1, m_y1)
for hx, hy in ((m_x0, m_y0), (m_x1, m_y0), (m_x0, m_y1), (m_x1, m_y1),
               ((m_x0 + m_x1) / 2, m_y0), ((m_x0 + m_x1) / 2, m_y1)):
    rect(d, hx - 3, hy - 3, 6, 6, fill="#ffffff", outline=C.canvasEdge)

info = "Mask · 8-bit · 1024 × 320 px · edited 2 min ago"
iw = text_w(info, 9, 500) + 18
rect(d, doc_x, doc_y - 26, iw, 20, fill="#333333", radius=2)
text(d, doc_x + 9, doc_y - 21, info, 9, 500, C.textMuted)

zp_w = 128
zp_x = RIGHT_X - 14 - zp_w
zp_y = cv_y + cv_h - 30
rect(d, zp_x, zp_y, zp_w, 22, fill="#333333", outline="#4a4a4a", radius=2)
text(d, zp_x + 10, zp_y + 5, "−", 12, 500, C.textMuted)
text(d, zp_x + 28, zp_y + 6, "200%", 10, 500, C.text)
text(d, zp_x + 62, zp_y + 5, "+", 12, 500, C.textMuted)
line(d, zp_x + 80, zp_y + 5, zp_x + 80, zp_y + 17, "#4a4a4a", 1)
text(d, zp_x + 90, zp_y + 6, "Fit", 10, 500, C.textMuted)

# ruler in document pixels
ruler_y = cv_y + cv_h
rect(d, CENTER_X + RAIL_W, ruler_y, CENTER_W - RAIL_W, 18, fill=C.paneBgAlt)
for v in range(0, DOC_PX_W + 1, 32):
    xx = doc_x + v / DOC_PX_W * doc_w
    major = v % 128 == 0
    hgt = 8 if major else 4
    line(d, xx, ruler_y + 18 - hgt, xx, ruler_y + 18, "#6a6a6a", 1)
    if major:
        text(d, xx + 3, ruler_y + 1, str(v), 8, 400, C.textSubtle)

# --- right panel: brush settings ------------------------------------------
rect(d, RIGHT_X, BODY_Y, RIGHT_W, BODY_H, fill=C.paneBg)
rect(d, RIGHT_X, BODY_Y, RIGHT_W, 30, fill=C.paneHeaderBg)
tracked(d, RIGHT_X + 12, BODY_Y + 10, "BRUSH SETTINGS", 9, 600, C.textSection)
icon(d, "search", RIGHT_X + RIGHT_W - 52, BODY_Y + 8, 14, C.textSubtle)
icon(d, "dots", RIGHT_X + RIGHT_W - 30, BODY_Y + 6, 16, C.textSubtle)
line(d, RIGHT_X, BODY_Y + 30, RIGHT_X + RIGHT_W, BODY_Y + 30, "#3f3f3f", 1)

PAD = 12
RX = RIGHT_X + PAD
RW = RIGHT_W - PAD * 2
FIELD_W = 64
row_y = BODY_Y + 38


def value_field(y, value, w=FIELD_W, x=None):
    x = RX + RW - w if x is None else x
    rect(d, x, y - 4, w, 19, fill=C.inputBg, outline="#555555", radius=2)
    text(d, x + 6, y, value, 10, 500, C.text)


def slider_row(y, label, value, frac):
    text(d, RX, y, label, 10, 400, C.textMuted)
    value_field(y, value)
    ty = y + 17
    rect(d, RX, ty, RW, 3, fill=C.inputBg, radius=2)
    rect(d, RX, ty, max(3, RW * frac), 3, fill=C.accent, radius=2)
    kx = RX + RW * frac
    d.ellipse([(kx - 4) * S, (ty - 2.5) * S, (kx + 4) * S, (ty + 5.5) * S],
              fill="#ededed", outline="#2c2c2c")
    return y + 30


def section_expanded(y, title, reset=False):
    icon(d, "chevron_down", RX - 1, y + 3, 12, C.textSubtle)
    text(d, RX + 16, y + 1, title, 11, 500, C.text)
    if reset:
        text(d, RX + RW, y + 2, "Reset", 9, 500, C.textSubtle, anchor="ra")
    return y + 22


def section_collapsed(y, title, summary):
    icon(d, "chevron", RX - 1, y + 3, 12, C.textSubtle)
    text(d, RX + 16, y + 1, title, 11, 400, C.textMuted)
    text(d, RX + RW, y + 2, summary, 9, 400, C.textSubtle, anchor="ra")
    y += 23
    line(d, RX, y - 0.5, RX + RW, y - 0.5, "#424242", 1)
    return y + 4


# Brush Tip (expanded)
row_y = section_expanded(row_y, "Brush Tip", reset=True)
text(d, RX, row_y, "Component", 10, 400, C.textMuted)
comp_w = RW - 54
rect(d, RX, row_y + 14, comp_w, 22, fill=C.inputBg, outline="#555555", radius=2)
text(d, RX + 7, row_y + 19, "Bristle smear 03", 10, 500, C.text)
icon(d, "chevron_down", RX + comp_w - 18, row_y + 19, 12, C.textSubtle)
rect(d, RX + comp_w + 10, row_y + 9, 40, 40, fill=C.inputBg, outline="#555555", radius=2)
t2 = Image.new("RGBA", (40 * S, 40 * S), (0, 0, 0, 0))
paint_stroke(t2, [(7, 29), (16, 18), (24, 22), (33, 11)], 7.5, 2.5, seed=7, fibre=260)
img.paste(t2.convert("RGB"), (int((RX + comp_w + 10) * S), int((row_y + 9) * S)), t2)
row_y += 54
row_y = slider_row(row_y, "Hardness", "62 %", 0.62)
row_y = slider_row(row_y, "Tip density", "88 %", 0.88)
fy = row_y + 1
for i, lbl in enumerate(("Flip X", "Flip Y")):
    bx = RX + i * 76
    rect(d, bx, fy, 12, 12, fill=None, outline="#6a6a6a", radius=2)
    if i == 0:
        line(d, bx + 3, fy + 8.5, bx + 9, fy + 3.5, C.accent, 1.5)
        line(d, bx + 6.5, fy + 3.5, bx + 9, fy + 3.5, C.accent, 1.5)
    text(d, bx + 18, fy - 1, lbl, 10, 400, C.textMuted)
row_y += 22
line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
row_y += 7

# Size (expanded)
row_y = section_expanded(row_y, "Size")
row_y = slider_row(row_y, "Size", "60 px", 0.42)
text(d, RX, row_y, "Pressure mapped to size", 10, 400, C.textMuted)
text(d, RX + RW, row_y, "linear", 9, 400, C.textSubtle, anchor="ra")
py = row_y + 14
rect(d, RX, py, RW, 30, fill=C.inputBg, radius=2)
pts = [(RX + 6, py + 24), (RX + RW * 0.32, py + 19), (RX + RW * 0.62, py + 10), (RX + RW - 6, py + 5)]
for i in range(len(pts) - 1):
    line(d, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], C.accent, 1.3)
for px_, py_ in pts:
    d.ellipse([(px_ - 2.5) * S, (py_ - 2.5) * S, (px_ + 2.5) * S, (py_ + 2.5) * S],
              fill="#ededed", outline="#2c2c2c")
row_y = py + 36
line(d, RX, row_y, RX + RW, row_y, "#424242", 1)
row_y += 6

for title, summary in (
    ("Opacity", "100 % · flow 82 %"),
    ("Stroke", "spacing 1.2 % · taper 40 %"),
    ("Stabilization", "balanced · 12"),
    ("Rotation", "follow direction"),
    ("Scatter", "18 % · 2 per dab"),
    ("Texture", "Grain 02 · overlay 74 %"),
    ("Color", "digital · pigment mixing"),
    ("Blending", "multiply · 1.00"),
    ("Dynamics", "pressure, tilt, speed"),
    ("Rendering", "tip anti-alias · 4×"),
):
    row_y = section_collapsed(row_y, title, summary)

# Advanced holds the optional node workspace rather than putting it on screen
icon(d, "chevron", RX - 1, row_y + 3, 12, C.textSubtle)
text(d, RX + 16, row_y + 1, "Advanced", 11, 400, C.textMuted)
text(d, RX + RW, row_y + 2, "0 overrides", 9, 400, C.textSubtle, anchor="ra")
row_y += 22
rect(d, RX, row_y, RW, 26, fill=C.paneBgAlt, outline="#5f5f5f", radius=2)
icon(d, "nodes", RX + 9, row_y + 6, 14, C.textMuted)
text(d, RX + 31, row_y + 7, "Open node graph editor…", 10, 500, C.textMuted)
text(d, RX + RW - 9, row_y + 7, "Alt N", 9, 400, C.textSubtle, anchor="ra")
text(d, RX, row_y + 33, "Node editing is optional - the settings above cover", 9, 400, C.textSubtle)
text(d, RX, row_y + 45, "every brush; the graph is for layered behaviour.", 9, 400, C.textSubtle)

foot_y = BODY_Y + BODY_H - 34
line(d, RX - PAD, foot_y - 1, RIGHT_X + RIGHT_W, foot_y - 1, "#3f3f3f", 1)
text(d, RX, foot_y + 9, "12 of 128 settings changed", 9, 400, C.textSubtle)
text(d, RX + RW, foot_y + 9, "Reset all", 9, 500, C.textMuted, anchor="ra")
print("right panel used", round(row_y + 26 - BODY_Y, 1), "of", BODY_H - 30)

# --- bottom: live preview -------------------------------------------------
rect(d, 0, PREV_Y, W, PREVIEW_H, fill=C.paneBgAlt)
line(d, 0, PREV_Y + 0.5, W, PREV_Y + 0.5, "#4a4a4a", 1)
rect(d, 0, PREV_Y, W, 30, fill=C.paneHeaderBg)
tracked(d, 12, PREV_Y + 10, "LIVE PREVIEW", 9, 600, C.textSection)
text(d, 12 + 110, PREV_Y + 10, "Pixel engine · actual brush · 8.4 ms", 9, 400, C.textSubtle)

pm_w, pm_seg = 186, 62
pm_x = W - 12 - pm_w - 108
rect(d, pm_x, PREV_Y + 5, pm_w, 20, fill=C.paneBgAlt, outline=C.border, radius=2)
for i, lbl in enumerate(("Stroke", "Stamps", "Grid")):
    if i == 0:
        rect(d, pm_x + 1, PREV_Y + 6, pm_seg - 1, 18, fill=C.toolActive, radius=2)
    text(d, pm_x + i * pm_seg + pm_seg / 2, PREV_Y + 10, lbl, 9, 500,
         C.text if i == 0 else C.textSubtle, anchor="ma")
rect(d, pm_x + pm_w + 8, PREV_Y + 5, 98, 20, fill=None, outline="#5f5f5f", radius=2)
text(d, pm_x + pm_w + 18, PREV_Y + 10, "Compare A/B", 9, 500, C.textMuted)

paper_x, paper_y = 12, PREV_Y + 38
paper_w, paper_h = 940, PREVIEW_H - 48
rect(d, paper_x, paper_y, paper_w, paper_h, fill=C.previewPaper, radius=2)
lay = Image.new("RGBA", (paper_w * S, paper_h * S), (0, 0, 0, 0))
paint_stroke(lay, [(26, 46), (150, 26), (330, 20), (520, 34), (690, 18), (860, 16)],
             27, 7, ink=(28, 32, 40), seed=41, fibre=1700, opacity=250)
paint_stroke(lay, [(28, 68), (190, 78), (370, 90), (580, 80), (840, 62)],
             13, 16, ink=(62, 46, 34), seed=42, fibre=1000, dry=0.4, opacity=220)
paint_stroke(lay, [(54, 98), (230, 92), (420, 104), (620, 96), (860, 90)],
             19, 3, ink=(28, 32, 40), seed=43, fibre=1200, dry=0.15, opacity=240)
img.paste(lay.convert("RGB"), (paper_x * S, paper_y * S), lay)
text(d, paper_x + 12, paper_y + 7, "800 px · pressure 20 to 100 % · three directions",
     9, 400, "#5a626b")

sx2 = paper_x + paper_w + 14
sw2 = W - 12 - sx2
rect(d, sx2, paper_y, sw2, paper_h, fill=C.inputBg, radius=2)
tracked(d, sx2 + 12, paper_y + 9, "STAMPS · SPACING 1.2 %", 9, 600, C.textSection, 0.7)
lay2 = Image.new("RGBA", (sw2 * S, paper_h * S), (0, 0, 0, 0))
for i, (r, press) in enumerate(((8, 0.45), (10, 0.6), (12, 0.75), (13, 0.88), (13, 1.0), (13, 1.0))):
    stamp(lay2, 28 + i * 44, 68, r, seed=60 + i, ink=(232, 236, 242), press=press)
img.paste(lay2.convert("RGB"), (int(sx2 * S), int(paper_y * S)), lay2)
text(d, sx2 + 12, paper_y + 96, "Pressure steps 20 · 40 · 60 · 80 · 90 · 100 %", 9, 400, C.textSubtle)
line(d, sx2 + 12, paper_y + 112, sx2 + sw2 - 12, paper_y + 112, "#3f3f3f", 1)
text(d, sx2 + 12, paper_y + 119, "Realistic engine · water load 62 % · bleed 0.35", 9, 400, C.textSubtle)
text(d, sx2 + 12, paper_y + 133, "Grain 02 locked to canvas · 1:1 texel", 9, 400, C.textSubtle)

# --- status bar -----------------------------------------------------------
rect(d, 0, H - STATUS_H, W, STATUS_H, fill=C.statusbarBg)
line(d, 0, H - STATUS_H + 0.5, W, H - STATUS_H + 0.5, "#3a3a3a", 1)
text(d, 12, H - STATUS_H + 7, "12 components  ·  3 tips  ·  2 textures  ·  1 imported image",
     10, 400, C.textSubtle)
text(d, W / 2, H - STATUS_H + 7, "Engine: Pixel  ·  60 px  ·  Flow 82 %  ·  Spacing 1.2 %",
     10, 400, C.textSubtle, anchor="ma")
d.ellipse([(W - 158) * S, (H - STATUS_H + 9) * S, (W - 151) * S, (H - STATUS_H + 16) * S],
          fill=C.success)
text(d, W - 12, H - STATUS_H + 7, "saved · 8.4 ms preview", 10, 400, C.textSubtle, anchor="ra")

# --- frame ----------------------------------------------------------------
line(d, 0.5, 0, 0.5, H, "#4f4f4f", 1)
line(d, W - 0.5, 0, W - 0.5, H, "#4f4f4f", 1)
line(d, 0, H - 0.5, W, H - 0.5, "#4f4f4f", 1)

out = img.resize((W, H), Image.LANCZOS)
out.save("/tmp/mock/brush-studio.png")
print("wrote /tmp/mock/brush-studio.png", out.size)
