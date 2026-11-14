#!/usr/bin/env python3
"""Produce the corrected KomaPaint icon PNGs from the untouched uploads.

Reads   ui/assets/icons/originals/<category>/<Upload>.png   (never modified)
Writes  ui/assets/icons/<category>/<name>.png               (used by the app)

Every correction is made in the PNG itself; nothing is compensated in QML.
For each icon the script
  1. converts the artwork to one flat colour, moving any light/dark shading
     into the alpha channel so the icon looks the same once it is tinted,
  2. removes leftovers of neighbouring artwork that touch the canvas edge and
     the faint haze that is not attached to the drawing (both are reported),
  3. measures the real artwork bounds from the alpha channel,
  4. scales uniformly (the width:height ratio is never changed) to the common
     visual size of its category,
  5. centres the artwork, half-way between the centre of its bounds and its
     centre of mass (optical centring), on the canvas.

Requires Pillow and numpy.  Run from the repository root:
    python3 tools/icons/normalize_icons.py            # write files + report
    python3 tools/icons/normalize_icons.py --check    # verify files are current
"""
import json, os, sys
from collections import deque
import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(__file__), "..", "..", "ui", "assets", "icons")

# (category, uploaded file, corrected file)
ICONS = [
    ("tools", "airbrush.png", "airbrush.png"), ("tools", "circle.png", "circle.png"),
    ("tools", "curveline.png", "curveline.png"), ("tools", "eraser.png", "eraser.png"),
    ("tools", "eyedropper.png", "eyedropper.png"), ("tools", "fill.png", "fill.png"),
    ("tools", "gradient.png", "gradient.png"), ("tools", "hand.png", "hand.png"),
    ("tools", "lasso.png", "lasso.png"), ("tools", "line.png", "line.png"),
    ("tools", "move.png", "move.png"), ("tools", "paint.png", "brush.png"),
    ("tools", "pen.png", "pen.png"), ("tools", "pencil.png", "pencil.png"),
    ("tools", "rectangle.png", "rectangle.png"), ("tools", "selection.png", "selection.png"),
    ("tools", "smudge.png", "smudge.png"), ("tools", "text.png", "text.png"),
    ("tools", "transform.png", "transform.png"), ("tools", "wand.png", "wand.png"),
    ("tools", "zoom.png", "zoom.png"),
    ("layers", "Raster.png", "new-raster.png"), ("layers", "Vector.png", "new-vector.png"),
    ("layers", "Folder.png", "folder.png"), ("layers", "Adjlayer.png", "adjustment-layer.png"),
    ("layers", "Mask.png", "mask.png"), ("layers", "Referencelayer.png", "reference.png"),
    ("layers", "Alphalock.png", "lock.png"), ("layers", "Transparencylock.png", "lock-transparency.png"),
    ("layers", "Layermove.png", "move.png"), ("layers", "Merge.png", "merge-down.png"),
    ("layers", "Layerdelete.png", "delete.png"), ("layers", "Layervis.png", "visible.png"),
    ("project-types", "Illustration.png", "illustration.png"),
    ("project-types", "Comic.png", "comic-page.png"),
    ("project-types", "Multicomic.png", "multi-page-comic.png"),
    ("project-types", "Animation.png", "animation.png"),
]

# canvas, live area (longest side), visual-size rule
RULES = {
    # long side <= 104 px and geometric-mean side <= 90 px
    "tools": dict(canvas=128, live=104, mean=90),
    # the layer icons share one "stack of layers" base: make that base (the
    # widest part of each drawing) the same width in every icon
    # (and no icon taller than 96 px)
    "layers": dict(canvas=128, live=104, width=92, height=96),
    # long side <= 200 px and geometric-mean side <= 192 px
    "project-types": dict(canvas=256, live=224, long=200, mean=192),
}
SOLID = 40          # alpha above this counts as drawing
BOUNDS = 16         # alpha above this counts for the bounds
HAZE_RADIUS = 3     # px around the drawing that may keep soft edge pixels


def components(mask):
    h, w = mask.shape
    seen = np.zeros((h, w), bool)
    out = []
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not seen[y, x]:
                queue = deque([(y, x)]); seen[y, x] = True; pix = []
                while queue:
                    cy, cx = queue.popleft(); pix.append((cy, cx))
                    for ny in (cy - 1, cy, cy + 1):
                        for nx in (cx - 1, cx, cx + 1):
                            if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                                seen[ny, nx] = True; queue.append((ny, nx))
                out.append(np.array(pix))
    return out


def dilate(mask, radius):
    h, w = mask.shape
    padded = np.pad(mask, radius)
    out = np.zeros_like(mask)
    for dy in range(2 * radius + 1):
        for dx in range(2 * radius + 1):
            out |= padded[dy:dy + h, dx:dx + w]
    return out


def bounds(alpha, threshold):
    ys, xs = np.where(alpha > threshold)
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def process(category, upload, name):
    rule = RULES[category]
    canvas = rule["canvas"]
    src = Image.open(os.path.join(ROOT, "originals", category, upload)).convert("RGBA")
    info = dict(category=category, upload=upload, file=name, size=list(src.size), notes=[])
    if src.size != (canvas, canvas):
        info["notes"].append(f"canvas is {src.size[0]}x{src.size[1]}, expected {canvas}x{canvas}")
    rgba = np.asarray(src).astype(np.float64)
    alpha = rgba[..., 3]
    lum = rgba[..., :3] @ np.array([0.299, 0.587, 0.114])
    solid = alpha > 200
    dark = np.percentile(lum[solid], 5)
    # One flat colour: a pixel of colour c and alpha a looks, over white, like
    # the flat dark colour with alpha a*(255-c)/(255-dark).
    flat_alpha = np.clip(alpha * (255.0 - lum) / (255.0 - dark), 0, alpha)
    shaded = float((np.abs(flat_alpha - alpha)[alpha > SOLID] > 24).mean())
    if shaded > 0.02:
        info["notes"].append(f"shading moved into transparency ({shaded * 100:.0f}% of the drawing)")
    colour = np.median(rgba[..., :3][solid & (lum < dark + 8)], axis=0).round().astype(int)
    alpha = flat_alpha
    peak = alpha.max()
    if 240 <= peak < 255:
        alpha = np.clip(alpha * 255.0 / peak, 0, 255)

    # leftovers of neighbouring artwork: small pieces touching the canvas edge
    parts = components(alpha > SOLID)
    total = sum(len(p) for p in parts)
    h, w = alpha.shape
    for part in parts:
        ys, xs = part[:, 0], part[:, 1]
        touches = xs.min() == 0 or ys.min() == 0 or xs.max() == w - 1 or ys.max() == h - 1
        if touches and len(part) < 0.05 * total:
            mask = np.zeros_like(alpha, bool); mask[ys, xs] = True
            alpha[dilate(mask, HAZE_RADIUS)] = 0
            info["notes"].append(
                f"removed a {len(part)} px fragment of a neighbouring drawing at the canvas edge "
                f"(x {xs.min()}-{xs.max()}, y {ys.min()}-{ys.max()})")
    # haze: faint pixels that are not attached to the drawing
    keep = dilate(alpha > SOLID, HAZE_RADIUS)
    haze = (~keep) & (alpha > 0)
    if haze.any():
        info["haze_pixels"] = int(haze.sum()); info["haze_max_alpha"] = int(alpha[haze].max())
        alpha[haze] = 0

    l, t, r, b = bounds(alpha, BOUNDS)
    bw, bh = r - l, b - t
    info["original_bounds"] = [l, t, r, b]
    info["original_padding"] = dict(left=l, top=t, right=w - r, bottom=h - b)
    if "width" in rule:
        scale = min(rule["width"] / bw, rule["height"] / bh)
    else:
        scale = min(rule.get("long", rule["live"]) / max(bw, bh), rule["mean"] / (bw * bh) ** 0.5)
    info["scale"] = round(float(scale), 3)

    # optical centre: half-way between the bounds centre and the centre of mass
    ys, xs = np.mgrid[0:h, 0:w]
    mass = alpha.sum()
    cx, cy = (alpha * (xs + 0.5)).sum() / mass, (alpha * (ys + 0.5)).sum() / mass
    limit = canvas * 0.03 / scale          # the optical shift never exceeds 3 % of the canvas
    ax = (l + r) / 2 + float(np.clip((cx - (l + r) / 2) / 2, -limit, limit))
    ay = (t + b) / 2 + float(np.clip((cy - (t + b) / 2) / 2, -limit, limit))

    pad = 3
    cl, ct, cr, cb = max(0, l - pad), max(0, t - pad), min(w, r + pad), min(h, b + pad)
    crop = Image.fromarray(alpha[ct:cb, cl:cr].round().astype(np.uint8), "L")
    nw, nh = max(1, round((cr - cl) * scale)), max(1, round((cb - ct) * scale))
    crop = crop.resize((nw, nh), Image.LANCZOS)
    # where the anchor lands in the resized crop, and the offset that puts it on the canvas centre
    ox = round(canvas / 2 - (ax - cl) * scale)
    oy = round(canvas / 2 - (ay - ct) * scale)
    # never let the drawing leave the live area because of the optical shift
    margin = (canvas - rule["live"]) // 2
    new_w, new_h = bw * scale, bh * scale
    left = ox + (l - cl) * scale; top = oy + (t - ct) * scale
    ox += round(min(max(left, margin), canvas - margin - new_w) - left)
    oy += round(min(max(top, margin), canvas - margin - new_h) - top)
    out_alpha = Image.new("L", (canvas, canvas), 0)
    out_alpha.paste(crop, (ox, oy))
    final = np.asarray(out_alpha)
    fl, ft, fr, fb = bounds(final, BOUNDS)
    info["final_bounds"] = [fl, ft, fr, fb]
    info["final_padding"] = dict(left=fl, top=ft, right=canvas - fr, bottom=canvas - fb)
    info["optical_shift"] = [round(float(((l + r) / 2 - ax) * scale), 1), round(float(((t + b) / 2 - ay) * scale), 1)]
    info["colour"] = "#%02x%02x%02x" % tuple(colour)
    result = np.zeros((canvas, canvas, 4), np.uint8)
    result[..., :3] = colour
    result[..., 3] = final
    return Image.fromarray(result, "RGBA"), info


def main():
    check = "--check" in sys.argv
    report, stale = [], []
    for category, upload, name in ICONS:
        image, info = process(category, upload, name)
        path = os.path.join(ROOT, category, name)
        if check:
            if not os.path.exists(path) or not np.array_equal(np.asarray(Image.open(path).convert("RGBA")), np.asarray(image)):
                stale.append(f"{category}/{name}")
        else:
            image.save(path, optimize=True)
        report.append(info)
    if check:
        print("stale: " + ", ".join(stale) if stale else f"all {len(ICONS)} corrected icons are current")
        return 1 if stale else 0
    with open(os.path.join(ROOT, "corrections.json"), "w") as f:
        json.dump(report, f, indent=1); f.write("\n")
    for i in report:
        p, q = i["original_padding"], i["final_padding"]
        print(f"{i['category']+'/'+i['file']:36s} x{i['scale']:<5} pad L{p['left']:<3}T{p['top']:<3}R{p['right']:<3}B{p['bottom']:<3} -> "
              f"L{q['left']:<3}T{q['top']:<3}R{q['right']:<3}B{q['bottom']:<3} shift {i['optical_shift']} haze {i.get('haze_pixels', 0)} {'; '.join(i['notes'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
