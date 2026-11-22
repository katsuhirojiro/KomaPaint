"""Original marks, grain and test scenes drawn for the review app.

These tiny raster stamps are intentionally not KomaPaint brush assets. The
renderer below uses ordinary Qt image composition to illustrate interactions.
"""
from collections import OrderedDict
from io import BytesIO
from math import atan2, cos, degrees, hypot, pi, sin
from random import Random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps
from PySide6.QtCore import Property, QPointF, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QImage, QLinearGradient, QPainter, QPainterPath, QPen, QTransform
from PySide6.QtQuick import QQuickImageProvider, QQuickPaintedItem

from model import PRESET_INFO

ASSET_SIZE = 112
RESAMPLE = Image.Resampling.LANCZOS
ARGB = QImage.Format.Format_ARGB32_Premultiplied
STUDIO = None


def to_qimage(image):
    rgba = image.convert("RGBA")
    return QImage(rgba.tobytes(), rgba.width, rgba.height, rgba.width * 4,
                  QImage.Format.Format_RGBA8888).convertToFormat(ARGB)


def tinted(alpha, color):
    im = Image.new("RGBA", alpha.size, color)
    im.putalpha(alpha)
    return to_qimage(im)


def soft_noise(size, detail, seed):
    rng = Random(seed)
    sample = Image.new("L", (detail, detail))
    sample.putdata([rng.randrange(256) for _ in range(detail * detail)])
    return sample.resize((size, size), Image.Resampling.BICUBIC)


def touchup(image, blur=.65):
    return image.filter(ImageFilter.GaussianBlur(blur)) if blur else image


def make_mask(key):
    s = ASSET_SIZE
    im = Image.new("L", (s, s))
    d = ImageDraw.Draw(im)
    cloud_shapes = {
        "billow": [(32, 65, 25, 20), (48, 50, 25, 27), (70, 42, 24, 25), (87, 66, 20, 22), (53, 77, 44, 18)],
        "wisp": [(26, 65, 21, 6), (47, 61, 30, 13), (69, 54, 27, 16), (88, 47, 18, 9)],
        "cumulus": [(35, 77, 25, 17), (42, 58, 22, 21), (57, 39, 22, 26), (79, 28, 13, 16), (77, 69, 17, 24), (57, 85, 35, 12)],
        "veil": [(24, 50, 20, 14), (39, 55, 25, 17), (62, 62, 30, 13), (86, 64, 19, 11)]}
    if key in cloud_shapes:
        rng = Random(sum(map(ord, key)))
        for cx, cy, rx, ry in cloud_shapes[key]:
            d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
            for i in range(25):
                a = rng.uniform(0, pi * 2)
                x, y = cx + rx * cos(a) * .92, cy + ry * sin(a) * .91
                r = rng.uniform(1.6, 5.4)
                d.ellipse((x - r, y - r, x + r, y + r), fill=rng.randrange(105, 242))
        body = touchup(im, 2.5 if key != "veil" else 5.)
        clouds = soft_noise(s, 10, 29 + len(cloud_shapes[key]))
        grain = soft_noise(s, 39, 94)
        body.putdata([int(a * (.54 + .45 * n / 255) * (.91 + .09 * g / 255))
                      for a, n, g in zip(body.getdata(), clouds.getdata(), grain.getdata())])
        return body
    if key in ("air", "graphite"):
        rng = Random(89)
        values = []
        for y in range(s):
            for x in range(s):
                radius = ((x - 55.5) / (54 if key == "air" else 48)) ** 2 + ((y - 55.5) / (54 if key == "air" else 33)) ** 2
                if key == "air":
                    a = max(0., 1 - radius) ** 3.25
                else:
                    a = max(0., min(1., (1.04 - radius) * 3.3 + rng.uniform(-.40, .26)))
                    if radius > 1.10 or rng.random() < .18:
                        a = 0
                    a *= .28 + rng.random() * .72
                values.append(int(a * 255))
        im.putdata(values)
        return im
    if key in ("ink", "round"):
        d.ellipse((8, 8, s - 9, s - 9), fill=255)
    elif key == "bristle":
        rng = Random(49)
        for i in range(66):
            y = rng.randrange(22, 89)
            length = rng.randrange(29, 92)
            x0 = (s - length) / 2 + rng.uniform(-10, 10)
            d.line((x0, y, x0 + length, y + rng.choice([-4, -2, 0, 2])), fill=rng.randrange(120, 248), width=rng.randrange(1, 5))
    elif key == "leaf":
        d.polygon([(7, 95), (12, 66), (28, 44), (50, 32), (89, 27), (108, 9),
                   (100, 41), (89, 58), (70, 74), (45, 81), (22, 85)], fill=245)
        d.line((14, 93, 47, 66, 90, 36), fill=103, width=2)
    elif key == "stone":
        d.polygon([(8, 66), (26, 39), (50, 25), (83, 30), (104, 54), (90, 85), (55, 93), (16, 82)], fill=186)
        d.polygon([(26, 39), (50, 25), (83, 30), (67, 51), (36, 56)], fill=241)
        d.polygon([(36, 56), (67, 51), (90, 85), (55, 93)], fill=122)
    elif key == "star":
        d.polygon([(56, 6), (65, 43), (105, 55), (65, 66), (56, 106), (46, 66), (7, 55), (46, 44)], fill=255)
    elif key in ("spatter", "hair"):
        rng = Random(72)
        if key == "spatter":
            for i in range(83):
                a, r = rng.uniform(0, 2 * pi), rng.uniform(2, 48)
                x, y = 55 + r * cos(a), 55 + r * sin(a)
                q = rng.uniform(.75, 3.9)
                d.ellipse((x - q, y - q, x + q, y + q), fill=rng.randrange(90, 249))
        else:
            for i in range(15):
                x = 25 + i * 3.2
                d.arc((x - 20, 3, x + 23, 103), -56, 80, fill=rng.randrange(116, 243), width=2)
    return touchup(im, .55)


def make_grain(key):
    s = ASSET_SIZE
    seeds = {"paper": 46, "cotton": 66, "linen": 20, "mist": 44, "charcoal": 72, "stipple": 91}
    rng = Random(seeds[key])
    if key == "mist":
        return ImageChops.multiply(soft_noise(s, 7, 71), soft_noise(s, 8, 32)).point(lambda x: min(255, x * 2))
    if key == "linen":
        im = Image.new("L", (s, s))
        im.putdata([int(26 + 101 * (cos((x + y / 7) * 2 * pi / 6) ** 6) + 76 * (sin(y * 2 * pi / 7) ** 8)
                        + rng.randrange(37)) for y in range(s) for x in range(s)])
        return im
    im = Image.new("L", (s, s), {"cotton": 46, "charcoal": 63, "paper": 17, "stipple": 7}[key])
    d = ImageDraw.Draw(im)
    for i in range({"cotton": 680, "charcoal": 1200, "paper": 2350, "stipple": 520}[key]):
        x, y = rng.randrange(s), rng.randrange(s)
        r = rng.uniform(.3, 3.) if key == "cotton" else rng.uniform(.2, 1.5)
        d.ellipse((x - r, y - r / 1.5, x + r, y + r / 1.5), fill=rng.randrange(65, 247))
    if key in ("cotton", "charcoal"):
        return ImageChops.screen(im, soft_noise(s, 13, seeds[key]).point(lambda x: x * .45))
    return im


class Assets(QQuickImageProvider):
    def __init__(self):
        super().__init__(QQuickImageProvider.ImageType.Image)
        from model import MARKS, TEXTURES
        self.alpha = {a: make_mask(a) for a, _, _ in MARKS}
        self.grain = {a: make_grain(a) for a, _, _ in TEXTURES}
        self.tints = OrderedDict()
        self.holes = OrderedDict()
        self.thumbs = {}

    def has(self, key):
        return key in self.alpha or key in self.grain

    def requestImage(self, request, size, requestedSize):
        kind, _, key = request.partition("/")
        cache_key = (kind, key)
        if cache_key not in self.thumbs:
            if kind == "texture" and key in self.grain:
                tile = self.grain[key]
                self.thumbs[cache_key] = to_qimage(ImageOps.colorize(tile, "#e8dfce", "#594c40"))
            elif key in self.alpha:
                self.thumbs[cache_key] = tinted(self.alpha[key], "#3d3832")
            else:
                return QImage()
        image = self.thumbs[cache_key]
        size.setWidth(image.width())
        size.setHeight(image.height())
        return image

    @staticmethod
    def _store(cache, key, result, cap):
        cache[key] = result
        cache.move_to_end(key)
        if len(cache) > cap:
            cache.popitem(last=False)
        return result

    def stamp(self, asset, color, tip_grain, placement=None):
        fixed = [config for config in tip_grain if config[4] == "Canvas"] if placement else []
        if fixed:
            base = self.stamp(asset, color, [config for config in tip_grain if config[4] != "Canvas"])
            result = base.copy()
            x, y, angle, diameter, flip_x, flip_y = placement
            s = diameter / ASSET_SIZE
            to_canvas = (QTransform().translate(x, y).rotate(angle).scale(-s if flip_x else s, -s if flip_y else s)
                          .translate(-ASSET_SIZE / 2, -ASSET_SIZE / 2))
            to_stamp, _ok = to_canvas.inverted()
            p = QPainter(result)
            p.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
            for name, strength, zoom, rotation, _anchor, invert in fixed:
                pattern = QBrush(self.grain_holes(name, strength, invert))
                pattern.setTransform(QTransform().rotate(rotation).scale(zoom / 170, zoom / 170) * to_stamp)
                p.fillRect(result.rect(), pattern)
            p.end()
            return result
        key = asset, color, tuple(tip_grain)
        if key in self.tints:
            self.tints.move_to_end(key)
            return self.tints[key]
        mask = self.alpha.get(asset, self.alpha["round"])
        result = tinted(mask, color)
        if tip_grain:
            p = QPainter(result)
            p.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
            for name, strength, zoom, angle, _, invert in tip_grain:
                pattern = QBrush(self.grain_holes(name, strength, invert))
                t = QTransform().rotate(angle).scale(zoom / 100, zoom / 100)
                pattern.setTransform(t)
                p.fillRect(result.rect(), pattern)
            p.end()
        return self._store(self.tints, key, result, 384)

    def grain_holes(self, asset, strength, invert=False):
        key = asset, strength, invert
        if key in self.holes:
            self.holes.move_to_end(key)
            return self.holes[key]
        mask = self.grain.get(asset, self.alpha.get(asset, self.grain["paper"]))
        if invert:
            mask = ImageOps.invert(mask)
        mask = mask.point(lambda x: int(x * strength / 100))
        return self._store(self.holes, key, tinted(mask, "#000000"), 128)

    def add_import(self, key, content, texture):
        try:
            image = Image.open(BytesIO(content))
        except Image.DecompressionBombError as exc:
            raise ValueError("Choose an image below 24 megapixels.") from exc
        with image:
            if image.format not in ("PNG", "JPEG", "WEBP", "BMP"):
                raise ValueError("Use PNG, JPEG, WebP or BMP.")
            if image.width * image.height > 24_000_000:
                raise ValueError("Choose an image below 24 megapixels.")
            im = ImageOps.exif_transpose(image).convert("RGBA")
            if texture:
                base = Image.new("RGBA", im.size, "white")
                base.alpha_composite(im)
                tile = ImageOps.fit(base.convert("L"), (ASSET_SIZE, ASSET_SIZE), RESAMPLE)
                self.grain[key] = ImageOps.invert(tile)
                return
            alpha = im.getchannel("A")
            if alpha.getextrema()[0] == 255:
                luminosity = im.convert("L")
                # Common ink-on-paper imports become a usable monochrome tip.
                alpha = ImageOps.invert(luminosity)
                if not alpha.getbbox():
                    alpha = im.getchannel("A")
            self.alpha[key] = self.fit_mask(alpha)

    @staticmethod
    def fit_mask(mask):
        bounds = mask.getbbox()
        if not bounds:
            raise ValueError("This mark is blank. Try an image with visible ink.")
        mark = ImageOps.contain(mask.crop(bounds), (100, 100), RESAMPLE)
        out = Image.new("L", (ASSET_SIZE, ASSET_SIZE))
        out.paste(mark, ((ASSET_SIZE - mark.width) // 2, (ASSET_SIZE - mark.height) // 2))
        return out

    @staticmethod
    def tip_alpha(strokes):
        im = Image.new("L", (560, 560))
        d = ImageDraw.Draw(im)
        for stroke in strokes:
            width = int(stroke["size"] * 1.4)
            pts = [(x * 560, y * 560) for x, y, _, _, _ in stroke["points"]]
            d.line(pts, fill=255, width=width, joint="curve")
            for x, y in pts:
                d.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=255)
        return Assets.fit_mask(im)

    def add_drawn(self, key, strokes):
        self.alpha[key] = self.tip_alpha(strokes)


def clamp(x, low=0., high=1.):
    return min(high, max(low, x))


def curve(points, x):
    i = min(3, int(clamp(x) * 4))
    a = clamp(x) * 4 - i
    return points[i] * (1 - a) + points[i + 1] * a


def dynamics(brush, pressure, tilt, speed, direction, time):
    curves = brush.get("curves", {})
    raw = dict(Pressure=pressure, Tilt=tilt, Speed=speed,
               Direction=(direction + 180) / 360, Time=clamp(time / 1.8))
    size = 1 - .81 * brush.get("pressureSize", 0) / 100 * (1 - pressure)
    opacity = 1 - .93 * brush.get("pressureOpacity", 0) / 100 * (1 - pressure)
    flow = 1 - .88 * brush.get("pressureFlow", 0) / 100 * (1 - pressure)
    size *= (1 + .65 * tilt * brush.get("tiltAmount", 0) / 100) * (1 - .6 * speed * brush.get("speedAmount", 0) / 100)
    responses = dict(Size=size, Opacity=opacity, Flow=flow, Rotation=0., Scatter=1.)
    for key, points in curves.items():
        source, target = key.split(":", 1)
        if source not in raw or target not in responses:
            continue
        result = curve(points, raw[source])
        if target == "Rotation":
            responses[target] += 180 * (result - .5)
        else:
            responses[target] *= .14 + .86 * result
    return responses


def positions(points, min_step):
    first = points[0]
    direction = degrees(atan2(points[1][1] - first[1], points[1][0] - first[0])) if len(points) > 1 else 0.
    last = 0.
    total = sum(hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))
    step = yield first, direction, 0., 0.
    distance = max(min_step, step)
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = hypot(dx, dy)
        if length < .01:
            continue
        direction = degrees(atan2(dy, dx))
        velocity = clamp(length / (max(.008, b[3] - a[3]) * 1400))
        while distance <= last + length:
            f = (distance - last) / length
            point = tuple(ax + (bx - ax) * f for ax, bx in zip(a, b))
            step = yield point, direction, velocity, distance / max(.01, total)
            distance += max(min_step, step)
        last += length


def paint_pass(painter, brush, marks, textures, masks, points, size, color, seed, width, height, opacity=1.):
    if not marks or not points:
        return
    assets = STUDIO.assets
    rng = Random(seed ^ 0x4152414E)
    choices = Random(seed)
    order = brush.get("order", "In Order")
    spacing = brush.get("spacing", 16) / 100
    brush_size = max(1.2, size * brush.get("size", 100) / 100)
    wet = brush.get("wetness", 0) / 100
    deposit = brush.get("flow", 80) / 100
    density = brush.get("opacity", 100) / 100 * opacity
    buildup = brush.get("buildup", 25) / 100
    edge = brush.get("edge", 0) / 100
    palettes = [QColor(color)]
    if brush.get("colorDrift", 0):
        palettes += [palettes[0].lighter(100 + brush["colorDrift"] // 2), palettes[0].darker(100 + brush["colorDrift"] // 3)]
    for i, c in enumerate(palettes):
        if wet:
            palettes[i] = QColor.fromRgbF(c.redF() * (1 - wet * .22) + wet * .22,
                                         c.greenF() * (1 - wet * .22) + wet * .22,
                                         c.blueF() * (1 - wet * .22) + wet * .22)
    colors = [c.name() for c in palettes]
    spec = [(t["asset"], t["strength"], t["scale"], t["angle"], t["anchor"], t["invert"]) for t in textures]
    tip_grain = [config for config, t in zip(spec, textures) if t["textureMode"] == "Per mark" and t["strength"] > 0]
    fixed_per_mark = any(t[4] == "Canvas" for t in tip_grain)
    stroke_grain = [config for config, t in zip(spec, textures) if t["textureMode"] == "Whole stroke"]
    stroke_grain += [(m["asset"], m["strength"], m["scale"], m["angle"], m["anchor"], not m["invert"]) for m in masks]
    if stroke_grain or density < .999:
        layer = QImage(width, height, ARGB)
        layer.fill(Qt.GlobalColor.transparent)
        p = QPainter(layer)
    else:
        layer, p = None, painter
        p.save()
    p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
    samples = positions(points, max(.85, sum(hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:])) / 1800))
    data = next(samples)
    weights = [m["weight"] for m in marks]
    index = 0
    while data:
        pt, direction, velocity, progress = data
        x, y, pressure, elapsed, tilt = pt
        if order == "Weighted":
            mark = choices.choices(marks, weights=weights, k=1)[0]
        elif order == "Random":
            mark = choices.choice(marks)
        else:
            mark = marks[index % len(marks)]
        index += 1
        response = dynamics(brush, pressure, tilt, velocity, direction, elapsed)
        size_jitter = clamp((brush.get("sizeJitter", 0) + mark["sizeJitter"]) / 100)
        opacity_jitter = clamp((brush.get("opacityJitter", 0) + mark["opacityJitter"]) / 100)
        angle_jitter = brush.get("angleJitter", 0) + mark["angleJitter"]
        a = (mark["angle"] + (direction if mark["follow"] or brush.get("follow") else 0.) +
             response["Rotation"] + rng.uniform(-angle_jitter, angle_jitter))
        scatter = (brush.get("scatter", 0) + mark["scatter"]) * brush_size / 160 * response["Scatter"]
        spread = rng.uniform(-scatter, scatter) * brush.get("width", 100) / 100
        x += cos((direction + 90) * pi / 180) * (spread + brush.get("offset", 0) / 100 * brush_size)
        y += sin((direction + 90) * pi / 180) * (spread + brush.get("offset", 0) / 100 * brush_size)
        taper = 1 - brush.get("taper", 0) / 100 * (1 - clamp(min(progress, 1 - progress) * 6))
        diameter = max(.8, brush_size * mark["size"] / 100 * response["Size"] * rng.uniform(1 - size_jitter * .65, 1 + size_jitter * .65) * taper)
        alpha = clamp(mark["opacity"] / 100 * response["Opacity"] * response["Flow"] *
                      (1.08 * deposit) * (1 - wet * .43) * (1 - opacity_jitter * rng.random()) *
                      (.9 + .35 * buildup))
        stamp = assets.stamp(mark["asset"], rng.choice(colors), tip_grain,
                             (x, y, a, diameter, mark["flipX"], mark["flipY"]) if fixed_per_mark else None)
        p.save()
        p.translate(x, y)
        p.rotate(a)
        p.scale(-1 if mark["flipX"] else 1, -1 if mark["flipY"] else 1)
        rect = QRectF(-diameter / 2, -diameter / 2, diameter, diameter)
        if edge > .22 and mark["asset"] != "air":
            p.setOpacity(alpha * edge * .25)
            soft = assets.stamp("air", colors[0], ())
            p.drawImage(QRectF(-diameter * .71, -diameter * .71, diameter * 1.42, diameter * 1.42), soft)
        p.setOpacity(alpha)
        p.drawImage(rect, stamp)
        p.restore()
        try:
            data = samples.send(max(1, brush_size * spacing * mark.get("spacing", 100) / 100 * (.63 + .37 * response["Size"])))
        except StopIteration:
            data = None
    if layer is not None:
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
        for asset, strength, scale, angle, anchor, invert in stroke_grain:
            pattern = QBrush(assets.grain_holes(asset, strength, invert))
            t = QTransform()
            if anchor == "Stroke":
                x0, y0 = points[0][:2]
                x1, y1 = points[min(3, len(points) - 1)][:2]
                t.translate(x0, y0).rotate(degrees(atan2(y1 - y0, x1 - x0)))
            pattern.setTransform(t.rotate(angle).scale(scale / 170, scale / 170))
            p.fillRect(layer.rect(), pattern)
        p.end()
        painter.save()
        painter.setOpacity(density)
        painter.drawImage(0, 0, layer)
        painter.restore()
    else:
        p.restore()


BLENDS = {"Normal": QPainter.CompositionMode.CompositionMode_SourceOver,
          "Multiply": QPainter.CompositionMode.CompositionMode_Multiply,
          "Screen": QPainter.CompositionMode.CompositionMode_Screen,
          "Soft Light": QPainter.CompositionMode.CompositionMode_SoftLight,
          "Subtract": QPainter.CompositionMode.CompositionMode_DestinationOut}


def draw_stroke(painter, brush, stroke, w, h):
    points = [(x * w, y * h, p, t, tilt) for x, y, p, t, tilt in stroke["points"]]
    if not points:
        return
    # Groups and added brushes are independent, ordered passes for this preview.
    def passes(dest, config, entries, diameter, seed, opacity=1, carry_grain=(), carry_masks=()):
        live = [e for e in entries if e["visible"]]
        grain = [e for e in live if e["kind"] == "texture"]
        masks = [e for e in live if e["kind"] == "mask"]
        common = (*carry_grain, *grain)
        cutouts = (*carry_masks, *masks)
        members = [e for e in live if e["kind"] == "mark"]
        drew_main = False
        for row in live:
            if row["kind"] == "mark" and not drew_main:
                paint_pass(dest, config, members, common, cutouts, points, diameter,
                           stroke["color"], seed, w, h, opacity)
                drew_main = True
            elif row["kind"] in ("brush", "group") and row["children"]:
                sub_image = QImage(w, h, ARGB)
                sub_image.fill(Qt.GlobalColor.transparent)
                sub = QPainter(sub_image)
                sub_config = dict(config)
                for key in ("size", "order", "spacing", "scatter", "opacity", "flow", "curves", "pressureSize",
                            "pressureOpacity", "pressureFlow", "tiltAmount", "speedAmount", "sizeJitter", "opacityJitter",
                            "angleJitter", "follow", "wetness", "edge", "buildup", "colorDrift"):
                    if key in row:
                        sub_config[key] = row[key]
                passes(sub, sub_config, row["children"], diameter, seed + row["seedOffset"],
                       opacity * row["contribution"] / 100, common, cutouts)
                sub.end()
                dest.save()
                dest.setCompositionMode(BLENDS[row["blend"]])
                dest.drawImage(0, 0, sub_image)
                dest.restore()
    entries = brush["entries"]
    root_blend = brush.get("blend", "Normal")
    if root_blend != "Normal" or any(e["kind"] in ("brush", "group") for e in entries):
        composite = QImage(w, h, ARGB)
        composite.fill(Qt.GlobalColor.transparent)
        dest = QPainter(composite)
        passes(dest, brush, entries, stroke["size"], stroke["seed"])
        dest.end()
        painter.save()
        painter.setCompositionMode(BLENDS[root_blend])
        painter.drawImage(0, 0, composite)
        painter.restore()
    else:
        passes(painter, brush, entries, stroke["size"], stroke["seed"])


def motion(function, size, color, seed=38, n=51, ramps=True):
    points = []
    for i in range(n):
        t = i / (n - 1)
        x, y = function(t)
        pressure = .12 + .85 * sin(pi * t) ** .65 if ramps else .79
        points.append((x, y, pressure, t * 1.25, .15 + .65 * t))
    return dict(seed=seed, points=points, size=size, color=color)


def swatch(p, brush, w, h, size=None, color=None, style="swatch"):
    key = brush.get("preset", "scratch")
    _, _, _, diameter, ink = PRESET_INFO[key]
    if key == "cloud" and style != "hero":
        ink = "#637b8f"
    diameter = min(diameter, h * .74) if size is None else size
    if key == "pencil":
        diameter = max(6., diameter * .69)
    if style == "response":
        y = lambda t: .61 - .19 * sin(pi * t) + .12 * sin(2 * pi * t)
    else:
        y = lambda t: .55 + .20 * sin(2.2 * pi * t - .4) - .12 * t
    draw_stroke(p, brush, motion(lambda t: (.095 + .81 * t, y(t)), diameter, color or ink), w, h)


def sky(p, brush, w, h):
    gradient = QLinearGradient(0, 0, 0, h)
    gradient.setColorAt(0., QColor("#839bb1"))
    gradient.setColorAt(.55, QColor("#c1c7ca"))
    gradient.setColorAt(1., QColor("#f0d5bd"))
    p.fillRect(0, 0, w, h, gradient)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor(255, 230, 194, 140))
    r = max(13., min(w, h) * .065)
    p.drawEllipse(QPointF(w * .79, h * .62), r, r)
    for i, (x, y, length, width, color) in enumerate([
            (.06, .42, .62, .29, "#9ba5ba"), (.07, .34, .57, .29, "#e5dce0"),
            (.12, .27, .52, .34, "#fff1dd"), (.11, .24, .30, .29, "#fff6e4"),
            (.47, .51, .42, .15, "#e3ccd3"), (.45, .46, .43, .12, "#fcf0df"),
            (.04, .66, .41, .08, "#efe5df")]):
        samples = motion(lambda t: (x + length * t, y + .05 * sin(t * 4.9 + i * .13) - .09 * t),
                         max(12, width * min(w, h)), color, seed=35 + i, ramps=False)
        draw_stroke(p, brush, samples, w, h)
    for i, color in enumerate(["#a296a1", "#777e8b", "#526575"]):
        ridge = QPainterPath(QPointF(0, h))
        for k in range(58):
            x = k / 57
            y = .86 + i * .039 + .026 * sin(x * 12.1 + i) + .012 * cos(x * 27.9 + i * 5) + .008 * sin(x * 81.2)
            ridge.lineTo(w * x, h * y)
        ridge.lineTo(w, h)
        ridge.closeSubpath()
        p.fillPath(ridge, QColor(color))


def pressure_study(p, brush, w, h):
    rng = Random(37)
    # Retained slow, fast and overlapped examples; all use the same draft.
    ink = {"paint": "#a34b36", "airbrush": "#536a80", "cloud": "#617287"}.get(brush["preset"], "#44392f")
    nominal = PRESET_INFO[brush["preset"]][3]
    for j in range(3):
        draw_stroke(p, brush, motion(lambda t: (.075 + (.67 - .065 * j) * t,
                 .161 + .088 * j + (.066 - .011 * j) * sin(2.25 * pi * t + j * .17) - .035 * t),
                 min(nominal, h * .21) * (1.18 - .24 * j), ink, 102 + j), w, h)
    # A small hand-plotted botanical study leaves the rest of the pad open.
    def specimen(points, size, seed):
        draw_stroke(p, brush, dict(size=size, color=ink, seed=seed,
            points=[(x, y, .34 + .38 * sin(pi * i / max(1, len(points) - 1)), i * .022, .45)
                    for i, (x, y) in enumerate(points)]), w, h)
    if brush["preset"] in ("pencil", "pen", "scratch") and h > 225:
        ox, oy = .28, .77
        angle = -.53
        def stem(a):
            return ox + .18 * a * sin(angle), oy - .28 * a
        specimen([stem(i / 29) for i in range(30)], 4., 107)
        for i in range(6):
            a = .17 + i * .135
            x, y = stem(a)
            sign = -1 if i % 2 == 0 else 1
            span = (.146 - a * .07) * sign
            rise = .089 - .04 * a
            nodes = [(x + span * (j / 24), y - rise * (j / 24) + .034 * sin(pi * j / 24)) for j in range(25)]
            nodes += [(x + span * (1 - j / 24), y - rise * (1 - j / 24) - .037 * sin(pi * j / 24)) for j in range(25)]
            specimen(nodes, 3.0, 147 + i)
            specimen([(x + span * j / 19, y - rise * j / 19) for j in range(20)], 2.4, 180 + i)
            for j in range(7):
                t = .15 + j * .093
                specimen([(x + span * t, y - rise * t),
                          (x + span * (t + .1), y - rise * (t + .1) - .028 * sin(pi * t))], 1.8 + rng.random(), 181 + i * 10 + j)


def paper(p, w, h, plain=False):
    p.fillRect(0, 0, w, h, QColor("#faf7ee"))
    if not plain:
        p.setPen(QColor(129, 106, 80, 32))
        for x in range(18, w, 22):
            for y in range(18, h, 22):
                p.drawPoint(x, y)


def tip_lines(p, strokes, w, h):
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    for stroke in strokes:
        pen = QPen(QColor(stroke["color"]), stroke["size"], Qt.PenStyle.SolidLine,
                   Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
        p.setPen(pen)
        pts = [(x * w, y * h) for x, y, _, _, _ in stroke["points"]]
        if len(pts) == 1:
            p.drawPoint(QPointF(*pts[0]))
        elif pts:
            path = QPainterPath(QPointF(*pts[0]))
            for pt in pts[1:]:
                path.lineTo(QPointF(*pt))
            p.drawPath(path)


class ArtSheet(QQuickPaintedItem):
    """A live, Qt-rendered swatch/pad. No screenshot-backed interactions."""
    appearanceChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAntialiasing(True)
        self._kind = "sample"
        self._sample = ""
        self._cached = None
        self._cached_key = None
        self.appearanceChanged.connect(self.update)
        STUDIO.draftChanged.connect(self.update)
        STUDIO.padChanged.connect(self.update)
        STUDIO.padOptionsChanged.connect(self.update)
        STUDIO.tipChanged.connect(self.update)
        STUDIO.personalChanged.connect(self.update)
        STUDIO.libraryChanged.connect(self.update)

    @Property(str, notify=appearanceChanged)
    def kind(self):
        return self._kind

    @kind.setter
    def kind(self, value):
        self._kind = value
        self.appearanceChanged.emit()

    @Property(str, notify=appearanceChanged)
    def sample(self):
        return self._sample

    @sample.setter
    def sample(self, value):
        self._sample = value
        self.appearanceChanged.emit()

    def paint(self, painter):
        w, h = max(1, int(self.width())), max(1, int(self.height()))
        if w <= 1 or h <= 1:
            return
        s = STUDIO
        draft = s.lookup(self._sample) if self._sample else s._draft
        if draft is None:
            draft = s._draft
        kind = self._kind
        if kind == "imprint":
            entry = s.entry
            if entry.get("kind") in ("mark", "mask"):
                painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
                painter.setOpacity(entry["opacity"] / 100 if entry["kind"] == "mark" else 1.)
                d = min(w, h) * .72 * (entry["size"] / 100) ** .45
                painter.translate(w / 2, h / 2)
                painter.rotate(entry["angle"])
                painter.scale(-1 if entry["flipX"] else 1, -1 if entry["flipY"] else 1)
                painter.drawImage(QRectF(-d / 2, -d / 2, d, d), s.assets.stamp(entry["asset"], "#39332d", ()))
            return
        if kind == "tip":
            paper(painter, w, h)
            tip_lines(painter, s.tip.all_strokes(), w, h)
            return
        if kind == "tipPreview":
            if not s.tip.all_strokes():
                return
            stamp_key = ("tipPreview", s._epochs["tip"])
            if self._cached_key != stamp_key:
                self._cached = tinted(s.assets.tip_alpha(s.tip.all_strokes()), "#292523")
                self._cached_key = stamp_key
            d = min(w, h) * .90
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            painter.drawImage(QRectF((w - d) / 2, (h - d) / 2, d, d), self._cached)
            return
        # Rebuild retained marks only when their brush or the example changes.
        # New pointer samples otherwise paint over a cached paper image.
        working = s.original if kind == "padOriginal" else draft | s.overrides
        key = (kind, self._sample, w, h, id(draft), s._epochs["brush"], s._epochs["personal"],
               s._study, s.pad.strokes and tuple(t["seed"] for t in s.pad.strokes))
        if key != self._cached_key or self._cached is None:
            image = QImage(w, h, ARGB)
            image.fill(Qt.GlobalColor.transparent)
            p = QPainter(image)
            p.setRenderHint(QPainter.RenderHint.Antialiasing)
            if kind in ("pad", "padOriginal"):
                if s._study == "Sky study":
                    sky(p, working, w, h)
                else:
                    paper(p, w, h)
                    if s._study == "Pressure study":
                        pressure_study(p, working, w, h)
                for stroke in s.pad.strokes:
                    draw_stroke(p, working, stroke, w, h)
            elif kind == "sky":
                sky(p, draft, w, h)
            elif kind == "original":
                swatch(p, s.original, w, h, style="response")
            else:
                swatch(p, draft, w, h, style="response" if kind == "response" else "swatch")
            p.end()
            self._cached, self._cached_key = image, key
        painter.drawImage(0, 0, self._cached)
        if kind in ("pad", "padOriginal") and s.pad.active:
            draw_stroke(painter, working, s.pad.active, w, h)
