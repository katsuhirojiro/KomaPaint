"""Deterministic marks, grains and surface tiles for the isolated review.

These are design-review images, not production resources.  Each mark is just an
alpha image, which is the point: imported, drawn and built-in marks travel the
same path, and the review never grows a special cloud/leaf/hair shape system.
"""
from functools import lru_cache
from math import cos, pi, sin
from pathlib import Path
from random import Random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps
from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QImage
from PySide6.QtQuick import QQuickImageProvider

RES = 192
RGBA = QImage.Format.Format_RGBA8888
ARGB = QImage.Format.Format_ARGB32_Premultiplied


def bound(x, low=0.0, high=1.0):
    return max(low, min(high, x))


def blank():
    image = QImage(1, 1, ARGB)
    image.fill(Qt.GlobalColor.transparent)
    return image


def qimage(pil):
    im = pil.convert("RGBA")
    return QImage(im.tobytes(), im.width, im.height, im.width * 4, RGBA).convertToFormat(ARGB)


def noise(size, frequency, seed):
    rng = Random(seed)
    image = Image.new("L", (frequency, frequency))
    image.putdata([rng.randrange(256) for _ in range(frequency * frequency)])
    return image.resize((size, size), Image.Resampling.BICUBIC)


def leaf_boundary(x, y, xv, yv, bow):
    dx, dy = xv - x, yv - y
    norm = max(1e-6, (dx * dx + dy * dy) ** 0.5)
    nx, ny = -dy / norm, dx / norm
    border = []
    for side in (1, -1):
        for i in (range(26) if side > 0 else range(25, -1, -1)):
            t = i / 25
            shape = (sin(pi * t) ** 0.85) * (1.15 - 0.62 * t) * bow * side
            border.append((x + dx * t + nx * shape, y + dy * t + ny * shape))
    return border


def organic(key):
    lobes = {
        "billow": [(55, 122, 46, 38), (81, 92, 42, 46), (119, 72, 36, 44), (140, 117, 40, 40), (105, 132, 67, 31)],
        "broken": [(42, 118, 32, 22), (65, 106, 34, 28), (101, 93, 42, 32), (126, 75, 28, 39), (151, 99, 27, 27)],
        "wisp": [(36, 120, 32, 12), (71, 106, 51, 16), (119, 88, 36, 22), (148, 75, 25, 16)],
        "rake": [(32, 125, 26, 11), (63, 102, 29, 15), (93, 87, 38, 16), (128, 87, 35, 25), (153, 86, 24, 12)],
        "ribbon": [(41, 117, 38, 7), (79, 102, 46, 12), (116, 87, 35, 12), (153, 79, 29, 7)],
        "veil": [(45, 98, 40, 19), (86, 90, 39, 21), (129, 102, 44, 19)],
    }[key]
    seed = sum(map(ord, key))
    fields = [noise(RES, frequency, seed + i).tobytes() for i, frequency in enumerate((8, 14, 30, 52))]
    out = []
    for y in range(RES):
        for x in range(RES):
            i = y * RES + x
            a, b, c, d = (field[i] / 255 for field in fields)
            px, py = x + 26 * (a - 0.5), y + 20 * (b - 0.5)
            distance = max(1 - ((px - cx) / rx) ** 2 - ((py - cy) / ry) ** 2 for cx, cy, rx, ry in lobes)
            edge = bound(distance * 1.9 + 0.7 * (c - 0.5) + 0.38 * (a - 0.5))
            k = 0.27 + 0.38 * a + 0.27 * b + 0.15 * c + 0.1 * d
            out.append(round(255 * edge ** 1.28 * bound(k)))
    image = Image.new("L", (RES, RES))
    image.putdata(out)
    return image


def mark_image(key):
    if key in ("billow", "broken", "wisp", "rake", "ribbon", "veil"):
        return organic(key)
    image = Image.new("L", (RES, RES))
    pen = ImageDraw.Draw(image)
    rng = Random(379)
    if key in ("nib", "round"):
        pen.ellipse((12, 12, RES - 12, RES - 12), fill=255)
    elif key in ("air", "graphite"):
        air = key == "air"
        for y in range(RES):
            for x in range(RES):
                ratio = ((x - 95.5) / (92 if air else 80)) ** 2 + ((y - 95.5) / (92 if air else 53)) ** 2
                density = bound(1 - ratio) ** 3.3 if air else bound((0.99 - ratio) * 2.8) * rng.uniform(0.16, 0.92)
                if not air and rng.random() < 0.21:
                    density = 0
                image.putpixel((x, y), int(density * 255))
    elif key == "bristle":
        for _i in range(130):
            x = rng.randrange(10, 58)
            y = rng.randrange(32, 160)
            pen.line((x, y, RES - rng.randrange(10, 70), y + rng.randint(-8, 8)),
                     fill=rng.randint(46, 246), width=rng.randint(1, 5))
    elif key == "spatter":
        for _i in range(260):
            t, distance = rng.uniform(0, 2 * pi), 84 * rng.random() ** 0.61
            x, y, radius = 96 + distance * cos(t), 96 + distance * sin(t), rng.uniform(0.6, 2.9)
            pen.ellipse((x - radius, y - radius, x + radius, y + radius), fill=rng.randint(56, 230))
    elif key == "leaf":
        path = leaf_boundary(16, 154, 170, 34, 30)
        pen.polygon(path, fill=228)
        pen.line((18, 152, 166, 36), fill=113, width=2)
    elif key == "chip":
        pen.polygon([(13, 118), (39, 64), (74, 50), (137, 64), (176, 94), (140, 150), (63, 157)], fill=182)
        pen.polygon([(39, 64), (74, 50), (137, 64), (102, 102), (55, 97)], fill=238)
    elif key == "hatch":
        for i in range(13):
            x = 28 + i * 11
            pen.arc((x - 18, 12, x + 62, RES - 8), -76, 80, fill=rng.randint(116, 244), width=2)
    elif key == "scrape":
        for i in range(9):
            y = 22 + i * 18
            pen.line((rng.randint(8, 30), y, RES - rng.randint(8, 35), y + rng.randint(-9, 9)),
                     fill=rng.randint(90, 230), width=rng.randint(1, 3))
    else:
        pen.ellipse((12, 12, RES - 12, RES - 12), fill=255)
    return image.filter(ImageFilter.GaussianBlur(0.42))


def grain_image(key):
    if key in ("mist", "breakup"):
        return noise(RES, 9 if key == "mist" else 15, 515).point(
            lambda v: int(bound(v / 255 * 1.85 - 0.34) * 255))
    rng = Random(sum(map(ord, key)))
    image = Image.new("L", (RES, RES), 12)
    pen = ImageDraw.Draw(image)
    counts = {"paper": 4300, "rough": 3400, "cotton": 1250, "linen": 1100,
              "flecks": 820, "surface": 2100}
    for _i in range(counts.get(key, 2000)):
        x, y = rng.randrange(RES), rng.randrange(RES)
        radius = rng.uniform(0.55, 4.8 if key in ("cotton", "rough") else 1.6)
        pen.ellipse((x - radius, y - radius * 0.6, x + radius, y + radius * 0.6),
                    fill=rng.randint(35, 250))
    if key == "linen":
        for i in range(2, RES, 8):
            pen.line((i, 0, i, RES), fill=135)
            pen.line((0, i - 2, RES, i - 2), fill=110)
    if key == "surface":
        for i in range(0, RES, 13):
            pen.line((i, 0, i, RES), fill=90)
            pen.line((0, i, RES, i), fill=78)
    return image


def fit_mark(image):
    box = image.getbbox()
    if not box:
        raise ValueError("The image has no visible ink.")
    body = ImageOps.contain(image.crop(box), (RES - 18, RES - 18), Image.Resampling.LANCZOS)
    out = Image.new("L", (RES, RES))
    out.paste(body, ((RES - body.width) // 2, (RES - body.height) // 2))
    return out


MARKS = {
    "graphite": "Graphite point", "nib": "Round nib", "bristle": "Dry bristles",
    "air": "Soft round", "round": "Solid round", "billow": "Billow",
    "broken": "Broken edge", "wisp": "Torn wisp", "rake": "Raked edge",
    "ribbon": "Ribbon", "veil": "Frayed veil", "spatter": "Fine dots",
    "leaf": "Willow leaf", "chip": "Shale chip", "hatch": "Fine strands",
    "scrape": "Scraped edge",
}
GRAINS = {
    "paper": "Paper tooth", "rough": "Rough tooth", "cotton": "Cotton tooth",
    "mist": "Fine mist", "flecks": "Flecks", "linen": "Linen weave",
    "breakup": "Edge breakup", "surface": "Canvas weave",
}
SURFACES = {
    "Cold press": "paper", "Rough paper": "rough", "Hot press": "mist",
    "Smooth paper": "cotton", "Linen canvas": "linen",
    "Cotton canvas": "surface", "Non-absorbent palette": "flecks",
}


class Assets(QQuickImageProvider):
    def __init__(self, icons=None):
        super().__init__(QQuickImageProvider.ImageType.Image)
        self.marks = {key: mark_image(key) for key in MARKS}
        self.grains = {key: grain_image(key) for key in GRAINS}
        self.icons = Path(icons) if icons else None
        self.custom = {}

    @lru_cache(maxsize=1024)
    def stamp(self, key, ink, hardness=74, threshold=0, texture="", strength=0,
              contrast=100, scale=100):
        alpha = self.marks.get(key)
        if alpha is None:
            return blank()
        soft = 1.0 - bound(hardness / 100.0)
        cut = threshold / 100.0 * 0.72
        lut = []
        for value in range(256):
            a = value / 255.0
            if cut > 0:
                a = bound((a - cut) / max(0.01, 1 - cut))
            if soft > 0.001:
                a = bound((a - soft * 0.48) / max(0.02, 1 - soft * 0.48))
            lut.append(round(a * 255))
        shaped = alpha.point(lut)
        if texture and strength > 0:
            tooth = self.grains.get(texture, self.grains["paper"])
            if contrast != 100:
                tooth = tooth.point(lambda v: int(bound(128 + (v - 128) * contrast / 100.0) * 1))
            step = max(8, int(RES / max(0.2, scale / 100.0)))
            tooth = tooth.resize((step, step), Image.Resampling.BILINEAR)
            tile = Image.new("L", shaped.size)
            for y in range(0, RES, step):
                for x in range(0, RES, step):
                    tile.paste(tooth, (x, y))
            shaped = Image.blend(shaped, ImageChops.multiply(shaped, tile),
                                 bound(strength))
        image = Image.new("RGBA", shaped.size, ink)
        image.putalpha(shaped)
        return qimage(image)

    @lru_cache(maxsize=32)
    def surface(self, key, ink="#e9e6de", contrast=1.0):
        base = self.grains.get(key, self.grains["paper"])
        if contrast != 1.0:
            base = base.point(lambda v: int(bound(128 + (v - 128) * contrast) * 1))
        image = ImageOps.colorize(base, "#c9c5bb", ink)
        image = image.convert("RGBA")
        return qimage(image)

    @lru_cache(maxsize=128)
    def grain(self, key, contrast=100, invert=False, smooth=True):
        base = self.grains.get(key, self.grains["paper"])
        factor = contrast / 100.0
        if factor != 1.0:
            base = base.point(lambda v: int(bound(128 + (v - 128) * factor) * 1))
        if smooth:
            base = base.filter(ImageFilter.GaussianBlur(0.25))
        if invert:
            base = ImageOps.invert(base)
        image = Image.new("RGBA", base.size)
        image.putalpha(base)
        return qimage(image)

    @lru_cache(maxsize=256)
    def thumb(self, key):
        kind, _, ident = key.partition("/")
        if ident in self.marks:
            ink = "#efece5" if kind == "pale" else "#2b2b2e"
            return self.stamp(ident, ink, 88, 0)
        if ident in self.grains:
            return qimage(ImageOps.colorize(self.grains[ident], "#d8d4ca", "#4c4b4d"))
        if ident in self.custom:
            image = self.custom[ident]
            ink = "#efece5" if kind == "pale" else "#2b2b2e"
            out = Image.new("RGBA", image.size, ink)
            out.putalpha(image)
            return qimage(out)
        return blank()

    def requestImage(self, key, size, requested):
        image = self.thumb(key)
        size.setWidth(image.width())
        size.setHeight(image.height())
        return image

    def add_image(self, key, path, kind):
        with Image.open(path) as original:
            if original.width * original.height > 40_000_000:
                raise ValueError("Use an image up to 40 megapixels.")
            image = ImageOps.exif_transpose(original).convert("RGBA")
            alpha = image.getchannel("A")
            if alpha.getextrema() != (255, 255):
                ink = alpha
            else:
                ink = ImageOps.invert(image.convert("L"))
                if ink.getextrema() == (0, 0):
                    raise ValueError("Opaque white contains no ink. Use darker pixels or transparency.")
            if kind == "mark":
                self.marks[key] = fit_mark(ink)
                MARKS[key] = key.replace("_", " ").title()
            else:
                if not ink.getbbox():
                    raise ValueError("The image has no visible texture.")
                self.grains[key] = ink.resize((RES, RES), Image.Resampling.LANCZOS)
                GRAINS[key] = key.replace("_", " ").title()
        self.stamp.cache_clear()
        self.grain.cache_clear()

    @staticmethod
    def drawn(strokes, side=420):
        image = Image.new("L", (side, side))
        draw = ImageDraw.Draw(image)
        for stroke in strokes:
            points = [(x * side, y * side) for x, y, *_ in stroke["points"]]
            if not points:
                continue
            width = max(1, round(stroke.get("size", 12) * side / 420))
            if len(points) == 1:
                x, y = points[0]
                draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=255)
            else:
                draw.line(points, fill=255, width=width, joint="curve")
                for x, y in (points[0], points[-1]):
                    draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=255)
        return fit_mark(image)

    def add_drawn(self, key, strokes):
        self.marks[key] = self.drawn(strokes)
        MARKS[key] = key.replace("_", " ").title()
        self.stamp.cache_clear()
