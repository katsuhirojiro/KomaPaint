"""Small deterministic preview compositor. Not the proposed production engine."""
from functools import lru_cache
from math import atan2, cos, degrees, hypot, pi, radians, sin
from random import Random

from PIL import Image, ImageDraw, ImageFilter, ImageOps
from PySide6.QtCore import Property, QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QBrush, QColor, QImage, QLinearGradient, QPainter, QPainterPath, QPen, QTransform
from PySide6.QtQuick import QQuickImageProvider, QQuickPaintedItem

from recipes import CONTAINERS, GRAINS, MODES, PRESENTATION, SOURCES

STUDIO = None
RES = 144
RGBA = QImage.Format.Format_RGBA8888
ARGB = QImage.Format.Format_ARGB32_Premultiplied
SMOOTH = QPainter.RenderHint.SmoothPixmapTransform
BLEND = {k: getattr(QPainter.CompositionMode, 'CompositionMode_' + v) for k, v in
         [('Normal', 'SourceOver'), ('Multiply', 'Multiply'), ('Screen', 'Screen'),
          ('Soft Light', 'SoftLight'), ('Cut Away', 'DestinationOut')]}


def bound(x, low=0., high=1.):
    return max(low, min(high, x))


def blank_thumb():
    img = QImage(1, 1, ARGB)
    img.fill(Qt.GlobalColor.transparent)
    return img


def qimage(pil):
    im = pil.convert('RGBA')
    return QImage(im.tobytes(), im.width, im.height, im.width * 4, RGBA).convertToFormat(ARGB)


def gray(size, freq, seed):
    rng = Random(seed)
    img = Image.new('L', (freq, freq))
    img.putdata([rng.randrange(256) for _ in range(freq * freq)])
    return img.resize((size, size), Image.Resampling.BICUBIC)


def organic(key):
    # Each named source is just an alpha image. Shapes can be swapped for imports.
    lobes = {
        'billow': [(41, 91, 34, 28), (60, 69, 31, 34), (88, 54, 27, 33), (104, 87, 30, 30), (78, 98, 50, 23)],
        'broken': [(31, 88, 24, 16), (48, 79, 25, 21), (75, 69, 31, 24), (94, 56, 21, 29), (113, 74, 20, 20)],
        'wisp': [(27, 89, 24, 9), (53, 79, 38, 12), (89, 66, 27, 16), (111, 56, 19, 12)],
        'rake': [(24, 93, 19, 8), (47, 76, 22, 11), (69, 65, 28, 12), (96, 65, 26, 19), (115, 64, 18, 9)],
        'ribbon': [(31, 87, 28, 5), (59, 76, 34, 9), (87, 65, 26, 9), (115, 59, 22, 5)],
        'veil': [(34, 73, 30, 14), (64, 67, 29, 16), (96, 76, 33, 14)],
    }[key]
    seed = sum(map(ord, key))
    noise = [gray(RES, freq, seed + i).tobytes() for i, freq in enumerate((8, 14, 30, 52))]
    out = []
    for y in range(RES):
        for x in range(RES):
            i = y * RES + x
            a, b, c, d = (n[i] / 255 for n in noise)
            px, py = x + 19 * (a - .5), y + 15 * (b - .5)
            distance = max(1 - ((px - cx) / rx) ** 2 - ((py - cy) / ry) ** 2 for cx, cy, rx, ry in lobes)
            edge = bound(distance * 1.9 + .7 * (c - .5) + .38 * (a - .5))
            k = (.27 + .38 * a + .27 * b + .15 * c + .1 * d)
            out.append(round(255 * edge ** 1.28 * bound(k)))
    im = Image.new('L', (RES, RES))
    im.putdata(out)
    return im


def shape(key):
    if key in ('billow', 'broken', 'wisp', 'rake', 'ribbon', 'veil'):
        return organic(key)
    im = Image.new('L', (RES, RES))
    pen = ImageDraw.Draw(im)
    rng = Random(379)
    if key in ('nib', 'round'):
        pen.ellipse((10, 10, 134, 134), fill=255)
    elif key in ('air', 'graphite'):
        air = key == 'air'
        for y in range(RES):
            for x in range(RES):
                r = ((x - 71.5) / (69 if air else 60)) ** 2 + ((y - 71.5) / (69 if air else 40)) ** 2
                d = bound(1 - r) ** 3.3 if air else bound((.99 - r) * 2.8) * rng.uniform(.16, .92)
                if not air and rng.random() < .21:
                    d = 0
                im.putpixel((x, y), int(d * 255))
    elif key == 'bristle':
        for _i in range(100):
            x = rng.randrange(8, 44)
            y = rng.randrange(25, 119)
            pen.line((x, y, RES - rng.randrange(8, 55), y + rng.randint(-6, 6)), fill=rng.randint(46, 246), width=rng.randint(1, 4))
    elif key == 'spatter':
        for _i in range(186):
            t, d = rng.uniform(0, 2*pi), 64 * rng.random() ** .61
            x, y, r = 72 + d * cos(t), 72 + d * sin(t), rng.uniform(.52, 2.3)
            pen.ellipse((x-r, y-r, x+r, y+r), fill=rng.randint(56, 219))
    elif key == 'leaf':
        path = leaf_boundary(13, 116, 127, 24, 23)
        pen.polygon(path, fill=228)
        pen.line((15, 114, 124, 26), fill=113, width=2)
    elif key == 'chip':
        pen.polygon([(11, 88), (29, 49), (56, 38), (102, 49), (131, 71), (105, 113), (48, 117)], fill=182)
        pen.polygon([(29, 49), (56, 38), (102, 49), (77, 77), (41, 73)], fill=238)
    else:
        for i in range(11):
            x = 23 + i * 8
            pen.arc((x - 13, 11, x + 45, 127), -76, 80, fill=rng.randint(116, 244), width=2)
    return im.filter(ImageFilter.GaussianBlur(.42))


def texture(key):
    if key in ('mist', 'breakup'):
        return gray(RES, 9 if key == 'mist' else 15, 515).point(lambda v: int(bound(v / 255 * 1.85 - .34) * 255))
    rng = Random(sum(map(ord, key)))
    im = Image.new('L', (RES, RES), 12)
    pen = ImageDraw.Draw(im)
    for _i in range({'paper': 3700, 'cotton': 1070, 'linen': 980, 'flecks': 750}[key]):
        x, y = rng.randrange(RES), rng.randrange(RES)
        r = rng.uniform(.55, 4.4 if key == 'cotton' else 1.45)
        pen.ellipse((x-r, y-r*.60, x+r, y+r*.60), fill=rng.randint(35, 250))
    if key == 'linen':
        for i in range(2, RES, 7):
            pen.line((i, 0, i, RES), fill=135)
            pen.line((0, i-2, RES, i-2), fill=110)
    return im


def fit(img):
    bounds = img.getbbox()
    if not bounds:
        raise ValueError('The image has no visible ink.')
    body = ImageOps.contain(img.crop(bounds), (RES-18, RES-18), Image.Resampling.LANCZOS)
    out = Image.new('L', (RES, RES))
    out.paste(body, ((RES - body.width)//2, (RES-body.height)//2))
    return out


class Assets(QQuickImageProvider):
    def __init__(self, icons):
        super().__init__(QQuickImageProvider.ImageType.Image)
        self.marks = {key: shape(key) for key in SOURCES}
        self.grains = {key: texture(key) for key in GRAINS}
        self.icons = icons

    @lru_cache(maxsize=224)
    def stamp(self, key, ink):
        alpha = self.marks[key]
        im = Image.new('RGBA', alpha.size, ink)
        im.putalpha(alpha)
        return qimage(im)

    @lru_cache(maxsize=100)
    def cutout(self, key, invert):
        alpha = self.grains[key]
        im = Image.new('RGBA', alpha.size)
        im.putalpha(ImageOps.invert(alpha) if invert else alpha)
        return qimage(im)

    @lru_cache(maxsize=192)
    def thumb(self, key):
        kind, _, ident = key.partition('/')
        if kind == 'icon':
            try:
                with Image.open(self.icons / (ident + '.png')) as file:
                    alpha = file.convert('RGBA').getchannel('A')
                im = Image.new('RGBA', alpha.size, '#e1e1e1')
                im.putalpha(alpha)
                return qimage(im)
            except (OSError, ValueError):
                return blank_thumb()
        if ident in self.marks:
            return self.stamp(ident, '#e2e0dc' if kind == 'pale' else '#343436')
        if ident in self.grains:
            return qimage(ImageOps.colorize(self.grains[ident], '#d8d7d4', '#414146'))
        return blank_thumb()

    def requestImage(self, key, size, requested):
        img = self.thumb(key)
        size.setWidth(img.width())
        size.setHeight(img.height())
        return img

    def add_file(self, key, path, kind):
        try:
            with Image.open(path) as original:
                if original.width * original.height > 40_000_000:
                    raise ValueError('Use an image up to 40 megapixels for this mockup.')
                image = ImageOps.exif_transpose(original).convert('RGBA')
                alpha = image.getchannel('A')
                if alpha.getextrema() != (255, 255):
                    ink = alpha
                else:
                    ink = ImageOps.invert(image.convert('L'))
                    if ink.getextrema() == (0, 0):
                        raise ValueError('Opaque white has no ink. Use darker pixels or transparency.')
                if kind == 'mark':
                    self.marks[key] = fit(ink)
                elif ink.getbbox():
                    self.grains[key] = ink.resize((RES, RES), Image.Resampling.LANCZOS)
                else:
                    raise ValueError('The image has no visible ink.')
        except Image.DecompressionBombError as exc:
            raise ValueError('Use an image up to 40 megapixels for this mockup.') from exc

    @staticmethod
    def drawn(strokes):
        side = 500
        im = Image.new('L', (side, side))
        d = ImageDraw.Draw(im)
        for s in strokes:
            points = [(x*side, y*side) for x, y, *_ in s['points']]
            width = max(1, round(s['size'] * side / s.get('extent', 400)))
            d.line(points, fill=255, width=width, joint='curve')
            for x, y in (points[0], points[-1]):
                r = width / 2
                d.ellipse((x-r, y-r, x+r, y+r), fill=255)
        return fit(im)

    def add_drawn(self, key, strokes):
        self.marks[key] = self.drawn(strokes)


def selector(node, children, seed):
    rng = Random(seed ^ node['seed'] ^ 98213)
    mode = node['mode']
    if mode == MODES[2]:
        return lambda _i: rng.choice(children)
    if mode == MODES[3]:
        weights = [n['weight'] for n in children]
        return lambda _i: rng.choices(children, weights=weights, k=1)[0]
    return lambda i: children[i % len(children)]


def placements(points, distance):
    if not points:
        return
    a = points[0]
    b = points[min(len(points)-1, 1)]
    direction = degrees(atan2(b[1]-a[1], b[0]-a[0]))
    # A preview guard only: resample very long input paths instead of using all
    # pointer events as placements. It never truncates the brush's parts.
    floor = max(1., sum(hypot(b[0]-a[0], b[1]-a[1]) for a, b in zip(points, points[1:])) / 1600)
    gap = yield (a[0], a[1], direction, (a[2]+b[2])/2, .22)
    next_d, traveled, i = max(floor, gap or distance), 0., 1
    for a, b in zip(points, points[1:]):
        dx, dy = b[0]-a[0], b[1]-a[1]
        length = hypot(dx, dy)
        if length < .001:
            continue
        direction = degrees(atan2(dy, dx))
        speed = bound(length / (max(.012, b[3]-a[3])*1350))
        while next_d <= traveled + length:
            t = (next_d-traveled)/length
            gap = yield (*(x + (y-x)*t for x, y in zip(a[:2], b[:2])), direction,
                         (a[2]+b[2])/2, speed)
            i += 1
            next_d += max(floor, gap or distance)
        traveled += length


def positions(points, gap):
    stream = placements(points, gap)
    try:
        event = next(stream)
        while True:
            event = stream.send((yield event))
    except StopIteration:
        return


def stroke_events(points, gap):
    stream = positions(points, gap)
    try:
        event = next(stream)
        while True:
            event = stream.send((yield event))
    except StopIteration:
        return


def choose_at(node, children, points, gap, placed, seed):
    assigned = {n['uid']: [] for n in children}
    if placed is None:
        events = list(stroke_events(points, gap))
    else:
        events = placed
    mode = node['mode']
    if mode == MODES[1] and events:
        # In Order lays the parts along the stroke, one after another.
        total = sum(max(1, n['weight']) for n in children)
        at = 0
        for n in children:
            take = max(1, round(len(events) * max(1, n['weight']) / total))
            assigned[n['uid']] = events[at:at + take]
            at += take
        if at < len(events):
            assigned[children[-1]['uid']].extend(events[at:])
        return assigned
    choose = selector(node, children, seed)
    for i, event in enumerate(events):
        assigned[choose(i)['uid']].append(event)
    return assigned


def curve_at(points, x):
    t = bound(x)*4
    i = min(int(t), 3)
    return points[i] + (points[i+1]-points[i])*(t-i)


def allocate(w, h):
    img = QImage(w, h, ARGB)
    img.fill(Qt.GlobalColor.transparent)
    return img


def ink_color(color, tone):
    col = QColor(color)
    ratio = abs(tone)/100
    toward = float(tone > 0)
    return QColor.fromRgbF(*(c+(toward-c)*ratio for c in (col.redF(), col.greenF(), col.blueF()))).name()


def paint_mark(p, n, stack, points, placed, d, gap, seed, color, assets):
    p.setRenderHint(SMOOTH)
    tone = bound(sum(x['tone'] for x in stack), -100, 100)
    stamp = assets.stamp(n['asset'], ink_color(color, tone))
    scatter = sum(x['scatter'] for x in stack) / 200
    offset = sum(x['offset'] for x in stack) / 100
    angle = sum(x['angle'] for x in stack)
    follow = any(x['follow'] for x in stack)
    size_var = min(1., sum(x['sizeVar'] for x in stack) / 100)
    opacity_var = min(1., sum(x['opacityVar'] for x in stack) / 100)
    angle_var = sum(x['angleVar'] for x in stack)
    flow = 1.
    for scope in stack:
        flow *= scope['flow'] / 100
    for i, (x, y, direction, pressure, speed) in enumerate(positions(points, gap) if placed is None else placed):
        tilt = .23
        rng = Random(seed + n['seed']*137 + i*7919)
        dimensions, coverage, spread = 1., flow, scatter
        turn = angle + (direction if follow else 0.) + rng.uniform(-angle_var, angle_var)
        sample = dict(Pressure=pressure, Tilt=tilt, Speed=speed)
        for scope in stack:
            dimensions *= 1 - (1-pressure)*.9*scope['pressure']/100
            for key, values in scope['curves'].items():
                source, target = key.split(':')
                level = curve_at(values, sample[source])
                if target == 'Size':
                    dimensions *= .12 + .88*level
                elif target == 'Opacity':
                    coverage *= .1 + .9*level
                elif target == 'Scatter':
                    spread *= .1 + .9*level
                elif target == 'Rotation':
                    turn += (level - .5)*180
        diameter = max(.45, d * dimensions * rng.uniform(1-size_var*.64, 1+size_var*.64))
        delta = (offset + rng.uniform(-spread, spread))*d
        roll = radians(direction + 90)
        r = diameter / 2
        height = diameter * n['aspect'] / 100
        p.save()
        p.translate(x + cos(roll)*delta, y + sin(roll)*delta)
        p.rotate(turn)
        p.setOpacity(bound(coverage * (1 - opacity_var * rng.random())))
        p.drawImage(QRectF(-r, -height/2, diameter, height), stamp)
        p.restore()


def effect(p, rect, n, points, assets):
    if not n['strength']:
        return
    pattern = QBrush(assets.cutout(n['asset'], n['invert'] ^ (n['kind'] == 'mask')))
    t = QTransform()
    if n['anchor'] == 'Stroke':
        a, b = points[0], points[min(3, len(points)-1)]
        t.translate(a[0], a[1]).rotate(degrees(atan2(b[1]-a[1], b[0]-a[0])))
    scale = n['scale']/460
    pattern.setTransform(t.rotate(n['angle']).scale(scale, scale))
    # Grain and masks modulate the paint already inside this group rather than
    # painting the whole pad. This is a preview approximation of a brush engine.
    p.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut
                         if n['kind'] == 'mask' else
                         QPainter.CompositionMode.CompositionMode_DestinationIn)
    p.setOpacity(n['strength']/100)
    p.fillRect(rect, pattern)


def draw_stroke(painter, recipe, stroke, w, h, assets):
    points = [(x*w, y*h, *more) for x, y, *more in stroke['points']]
    if not points:
        return
    seed, ink = stroke['seed'], stroke['color']

    def composite(dest, n, stack, size, cadence, placed):
        if not n['visible'] or not n['opacity'] or placed == []:
            return
        chain = (*stack, n)
        d = size*n['size']/100
        cadence = cadence*n['spacing']/100
        gap = max(.85, d*cadence)
        img = allocate(w, h)
        p = QPainter(img)
        try:
            if n['kind'] == 'mark':
                paint_mark(p, n, chain, points, placed, d, gap, seed, ink, assets)
            else:
                children = [x for x in n['children'] if x['visible']]
                marks = [x for x in children if x['kind'] in (CONTAINERS | {'mark'})]
                assignments = (choose_at(n, marks, points, gap, placed, seed)
                               if marks and n['mode'] != MODES[0] else None)
                for child in marks:
                    composite(p, child, chain, d, cadence, assignments[child['uid']] if assignments is not None else placed)
                # Grain and masks affect all paint inside *this* group, never its siblings.
                for child in children:
                    if child['kind'] in ('texture', 'mask'):
                        effect(p, img.rect(), child, points, assets)
        finally:
            p.end()
        dest.save()
        dest.setOpacity(n['opacity'] / 100)
        dest.setCompositionMode(BLEND[n['blend']])
        dest.drawImage(0, 0, img)
        dest.restore()

    composite(painter, recipe, (), stroke['size'], 1., None)


def motion(fn, size, color, seed=77, n=50, firm=False):
    points = []
    for i in range(n):
        t = i/(n-1)
        pressure = .82 if firm else .16 + .80*(sin(pi*t) ** .64)
        points.append((*fn(t), pressure, .9*t, .09 + .65*t))
    return dict(points=points, seed=seed, size=size, color=color)


def swatch(p, recipe, w, h, size, assets, ink='#e8e7e3'):
    draw_stroke(p, recipe, motion(lambda t: (.086 + .828*t, .57 + .16*sin(t*2.12*pi-.42)-.12*t),
                                  min(h*.55, size), ink), w, h, assets)


def leaf_boundary(x, y, xv, yv, bow):
    # Shape along the stem axis, independent of page proportions.
    dx, dy = xv-x, yv-y
    norm = hypot(dx, dy)
    nx, ny = -dy/norm, dx/norm
    border = []
    for side in (1, -1):
        for i in (range(26) if side > 0 else range(25, -1, -1)):
            t = i/25
            shape = (sin(pi*t)**.85) * (1.15 - .62*t) * bow * side
            border.append((x+dx*t+nx*shape, y+dy*t+ny*shape))
    return border


def drawing_study(p, w, h):
    """A light printed study on the pad. It is paper content, not brush output."""
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    ink = QColor('#c2bbb0')
    def line(pts, thick):
        if len(pts) < 2:
            return
        path = QPainterPath(QPointF(pts[0][0], pts[0][1]))
        for x, y in pts[1:]:
            path.lineTo(x, y)
        p.setPen(QPen(ink, max(.7, thick), Qt.PenStyle.SolidLine,
                      Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        p.drawPath(path)

    def stem(t):
        return (.165 + .188*t - .048*t*t) * w, (.905 - .390*t) * h

    line([stem(i/42) for i in range(43)], 1.15)
    for j in range(7):
        f = .14 + j*.12
        x, y = stem(f)
        sign = -1 if j % 2 == 0 else 1
        a = x + (.136 - .062*f) * sign * w
        b = y - (.066 + .056*f) * h
        edge = leaf_boundary(x, y, a, b, min(w, h) * (.032 - .014*f))
        line(edge, .9)
        line([(x, y), (a, b)], .85)
        for i in range(2, 10):
            t = i / 11
            v = edge[min(25, i*2+5)]
            line([(x+(a-x)*t, y+(b-y)*t), v], .62)
        if j in (1, 4):
            for i in range(3, 8):
                t = i/10
                v = edge[min(50, 47-i*2)]
                line([(x+(a-x)*t, y+(b-y)*t), v], .55)
    for i in range(12):
        t = i/12
        line([((.51+.12*t)*w, (.63+.076*t)*h), ((.537+.16*t)*w, (.655+.092*t)*h)], .8)
    # A couple of loose construction arcs sit lightly in the clear area.
    p.setPen(QPen(QColor('#cec8bd'), .9))
    p.drawArc(QRectF(w*.62, h*.18, w*.30, h*.26), 205*16, 118*16)
    p.drawArc(QRectF(w*.68, h*.22, w*.26, h*.23), 190*16, 140*16)


def sky_sheet(p, w, h):
    """The fixed light and horizon; every cloud form is a retained brush stroke."""
    sky = QLinearGradient(0, 0, 0, h)
    for t, color in [(0., '#4a6078'), (.34, '#7d95a5'), (.67, '#bdb9b7'), (.86, '#e9c0a4'), (1., '#efc9ab')]:
        sky.setColorAt(t, QColor(color))
    p.fillRect(0, 0, w, h, sky)
    rng = Random(2732)
    for j, (base, color) in enumerate(((.842, '#9693a0'), (.901, '#767385'), (.952, '#586274'))):
        path = QPainterPath(QPointF(0, h))
        for i in range(180):
            t = i / 179
            depth = .015*sin(t*15+j*1.9)+.007*cos(t*35-j*1.6)+.004*sin(t*79)+rng.random()*.003
            path.lineTo(t*w, (base+depth)*h)
        path.lineTo(w, h)
        path.closeSubpath()
        p.fillPath(path, QColor(color))


def tip_lines(p, strokes, w, h):
    for s in strokes:
        points = [QPointF(x*w, y*h) for x, y, *_ in s['points']]
        if not points:
            continue
        p.setPen(QPen(QColor('#2e2d2e'), s['size'], Qt.PenStyle.SolidLine,
                      Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin))
        path = QPainterPath(points[0])
        if len(points) == 1:
            p.drawPoint(points[0])
        else:
            for pt in points[1:]:
                path.lineTo(pt)
            p.drawPath(path)


class Artwork(QQuickPaintedItem):
    appearanceChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._kind, self._source, self._cache, self._cache_key = 'pad', '', None, None
        self.appearanceChanged.connect(self.update)
        STUDIO.changed.connect(self.update)
        STUDIO.selectionChanged.connect(self.update)
        STUDIO.padChanged.connect(self.update)
        STUDIO.tipChanged.connect(self.update)
        STUDIO.libraryChanged.connect(self.update)

    @Property(str, notify=appearanceChanged)
    def kind(self):
        return self._kind

    @kind.setter
    def kind(self, value):
        self._kind = value
        self.appearanceChanged.emit()

    @Property(str, notify=appearanceChanged)
    def source(self):
        return self._source

    @source.setter
    def source(self, value):
        self._source = value
        self.appearanceChanged.emit()

    def paint(self, painter):
        w, h = int(self.width()), int(self.height())
        if min(w, h) < 2:
            return
        s, kind = STUDIO, self.kind
        work = s.work
        assets = s.assets
        if kind in ('tip', 'tipPreview', 'imprint'):
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            painter.setRenderHint(SMOOTH)
            if kind == 'tip':
                tip_lines(painter, s.tip.strokes + ((s.tip.active,) if s.tip.active else ()), w, h)
            elif kind == 'tipPreview' and (s.tip.strokes or s.tip.active):
                strokes = s.tip.strokes + ((s.tip.active,) if s.tip.active else ())
                img = Image.new('RGBA', (RES, RES), '#d9d9d6')
                img.putalpha(assets.drawn(strokes))
                painter.drawImage(QRectF(2, 2, w-4, h-4), qimage(img))
            elif kind == 'imprint':
                item = s.locate(self.source)
                if item and item[0]['kind'] == 'mark':
                    n = item[0]
                    d = min(w, h)*.84
                    height = d*n['aspect']/100
                    painter.translate(w/2, h/2)
                    painter.rotate(n['angle'])
                    painter.setOpacity(n['opacity']/100)
                    painter.drawImage(QRectF(-d/2, -height/2, d, height), assets.stamp(n['asset'], '#2d2d30'))
            return
        # Render retained inputs again only when the brush, sheet or history changes.
        if kind == 'pad':
            draft = work.original if work.compare else work.root
            key = (kind, id(work), s.version, w, h, work.compare, work.sheet,
                   tuple(stroke['seed'] for stroke in work.pad.strokes))
        else:
            if kind == 'groupPreview':
                draft = s.groups.get(self.source)
            else:
                draft = s.catalog.get(self.source) or work.root
            if draft is None:
                return
            key = (kind, self.source, id(draft), w, h, s.version if kind == 'current' else 0)
        if key != self._cache_key:
            img = allocate(w, h)
            p = QPainter(img)
            try:
                p.setRenderHint(QPainter.RenderHint.Antialiasing)
                if kind == 'pad':
                    p.fillRect(img.rect(), QColor('#faf9f6'))
                    paint = lambda stroke: draw_stroke(p, draft, stroke, w, h, assets)
                    if work.sheet == 'Sky study':
                        sky_sheet(p, w, h)
                    elif work.sheet == 'Graphite sheet':
                        drawing_study(p, w, h)
                    elif work.sheet == 'Stroke tests':
                        for i, k in enumerate((1.2, .84, .53)):
                            paint(motion(lambda t: (.12+.7*t, .20+.20*i+.076*sin(t*5.8)-.052*t),
                                         work.test_size*k, work.ink, seed=77+i))
                    for stroke in work.pad.strokes:
                        paint(stroke)
                else:
                    size = 22 if kind == 'groupPreview' else PRESENTATION.get(self.source, (24, '', ''))[0]
                    swatch(p, draft, w, h, size, assets)
            finally:
                p.end()
            self._cache, self._cache_key = img, key
        painter.drawImage(0, 0, self._cache)
        if kind == 'pad' and work.pad.active and not work.compare:
            draw_stroke(painter, draft, work.pad.active, w, h, assets)
