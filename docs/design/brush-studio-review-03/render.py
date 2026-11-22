"""A deterministic settings-aware compositor for the review.

It is not the production renderer and does not import it.  What it does do is
respond to the controls the review claims to demonstrate: spacing, taper,
hardness, grain anchoring, wet pickup, colour variation, emission and the
shared dynamics curves all alter the image.  Settings that are organisation-only
are labelled in the interface instead of silently doing nothing.
"""
import hashlib
from math import atan2, cos, degrees, hypot, pi, radians, sin
from hashlib import sha256
from random import Random

import catalog
from assets import ARGB, SURFACES, qimage
from PySide6.QtCore import QPointF, QRect, QRectF, Qt
from PySide6.QtGui import (QBrush, QColor, QFont, QImage, QPainter, QPainterPath,
                           QPen, QTransform)

BLEND = {
    "Normal": QPainter.CompositionMode.CompositionMode_SourceOver,
    "Multiply": QPainter.CompositionMode.CompositionMode_Multiply,
    "Screen": QPainter.CompositionMode.CompositionMode_Screen,
    "Overlay": QPainter.CompositionMode.CompositionMode_Overlay,
    "Soft light": QPainter.CompositionMode.CompositionMode_SoftLight,
    "Height": QPainter.CompositionMode.CompositionMode_SourceOver,
    "Mask": QPainter.CompositionMode.CompositionMode_DestinationOut,
    "Erase": QPainter.CompositionMode.CompositionMode_DestinationOut,
}


def stable_hash(text):
    return int.from_bytes(sha256(str(text).encode('utf-8')).digest()[:4], 'little')


def bound(x, low=0.0, high=1.0):
    return max(low, min(high, x))


def allocate(w, h):
    image = QImage(int(w), int(h), ARGB)
    image.fill(Qt.GlobalColor.transparent)
    return image


def curve_at(points, x):
    if not points:
        return bound(x)
    if x <= points[0]["x"]:
        return points[0]["y"]
    for a, b in zip(points, points[1:]):
        if x <= b["x"]:
            span = max(1e-6, b["x"] - a["x"])
            return a["y"] + (b["y"] - a["y"]) * ((x - a["x"]) / span)
    return points[-1]["y"]


def signal_value(signal, sample, stamp_index):
    if signal == "Pressure":
        return bound(sample.get("pressure", 0.72))
    if signal == "Tilt":
        return bound(sample.get("tilt", 0.24))
    if signal == "Direction":
        return bound(sample.get("direction", 0.0) / 360.0 + 0.5)
    if signal == "Velocity":
        return bound(sample.get("speed", 0.25))
    if signal == "Distance":
        return bound(sample.get("distance", 0.0))
    if signal == "Progress":
        return bound(sample.get("progress", 0.5))
    if signal == "Time":
        return bound(sample.get("time", 0.5), 0.0, 1.0)
    if signal == "Rotation":
        return bound(sample.get("rotation", 0.0) / 360.0 + 0.5)
    if signal == "Hover":
        return 1.0
    if signal == "Random":
        return bound((sin(stamp_index * 12.9898 + sample.get("seed", 1)) * 43758.5453) % 1.0)
    return 0.5


def resolve(doc, key, sample, stamp_index):
    value = float(effective(doc, key))
    strength = doc.get("dynamics_strength", 1.0)
    for binding in doc.get("dynamics", []):
        if not binding.get("enabled", True) or binding.get("target") != key:
            continue
        level = curve_at(binding["curve"], signal_value(binding["signal"], sample, stamp_index))
        amount = bound(binding.get("amount", 1.0) * strength)
        value *= (1.0 - amount) + amount * level
    meta = catalog.SETTINGS.get(key)
    if meta and meta["kind"] == "slider":
        value = max(meta["minimum"], min(meta["maximum"], value))
    return value


def effective(doc, key):
    if key in doc.get("overrides", {}):
        return doc["overrides"][key]
    if key.startswith("assist_"):
        return doc.get("assist", {}).get(key, catalog.SETTINGS[key]["default"])
    return doc["settings"].get(key, catalog.SETTINGS[key]["default"])


def mix_colour(a, b, t):
    t = bound(t)
    return QColor.fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                           a.greenF() + (b.greenF() - a.greenF()) * t,
                           a.blueF() + (b.blueF() - a.blueF()) * t,
                           a.alphaF())


def vary_colour(doc, colour, sample, seed, scope, stamp_index):
    rng = Random((seed + stamp_index * 7919) & 0xFFFFFFFF)
    hue = resolve(doc, "hue_variation", sample, stamp_index)
    sat = resolve(doc, "saturation_variation", sample, stamp_index) / 100.0
    val = resolve(doc, "brightness_variation", sample, stamp_index) / 100.0
    if scope == "Per stroke":
        rng = Random(seed & 0xFFFFFFFF)
    h, s, v, a = colour.getHslF()
    if h < 0:
        h = 0.0
    h = (h + (rng.uniform(-hue, hue) / 360.0 if hue else 0.0)) % 1.0
    s = bound(s + rng.uniform(-sat, sat) * 0.18)
    v = bound(v + rng.uniform(-val, val) * 0.34)
    out = QColor.fromHslF(h, s, v, a)
    secondary = QColor(doc.get("secondary_colour", "#7a756c"))
    mix = resolve(doc, "colour_secondary", sample, stamp_index) / 100.0
    colour_input = effective(doc, "colour_input")
    if colour_input != "Off":
        sig = {"Pressure": "Pressure", "Tilt": "Tilt", "Speed": "Velocity",
               "Direction": "Direction", "Random": "Random"}.get(colour_input, "Pressure")
        level = signal_value(sig, sample, stamp_index)
        mix = bound(mix * (0.35 + 0.65 * level))
    if mix:
        out = mix_colour(out, secondary, mix)
    return out


def smooth_points(points, amount, strength=0.0):
    if len(points) < 3 or amount <= 0:
        return points
    weight = 1.0 - bound(amount / 100.0) * 0.86
    result = [points[0]]
    x, y, p, s = points[0]
    for nx, ny, np, ns in points[1:]:
        if strength and strength > 0:
            # Sharp-angle preservation commits to the raw point at a corner.
            vx, vy = nx - x, ny - y
            px, py = x - result[-1][0], y - result[-1][1]
            dot = (vx * px + vy * py) / max(1e-6, hypot(vx, vy) * hypot(px, py))
            if dot < -0.15 and strength > 35:
                weight = 1.0
        x += (nx - x) * weight
        y += (ny - y) * weight
        p += (np - p) * weight
        s += (ns - s) * weight
        result.append((x, y, p, s))
        weight = 1.0 - bound(amount / 100.0) * 0.86
    return result


def length_of(points):
    return sum(hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))


def events(points, spacing, variation, jitter, seed, stamp_index_start=0):
    """Resample a pointer path into stamp events with stable randomness."""
    if not points:
        return []
    if len(points) == 1:
        x, y, pressure, speed = points[0]
        return [dict(x=x, y=y, direction=0.0, pressure=pressure, speed=speed,
                     distance=0.0, progress=0.0, seed=seed, index=stamp_index_start)]
    total = length_of(points)
    result = []
    rng = Random(seed & 0xFFFFFFFF)
    next_at = 0.0
    travelled = 0.0
    index = stamp_index_start
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        seg = hypot(dx, dy)
        if seg < 1e-5:
            continue
        direction = degrees(atan2(dy, dx))
        while next_at <= travelled + seg:
            t = (next_at - travelled) / seg
            x = a[0] + dx * t
            y = a[1] + dy * t
            pressure = a[2] + (b[2] - a[2]) * t
            speed = a[3] + (b[3] - a[3]) * t
            if jitter:
                angle = rng.uniform(0, 2 * pi)
                radius = rng.random() * jitter / 100.0
                x += cos(angle) * radius
                y += sin(angle) * radius
            progress = bound(next_at / max(1e-6, total))
            result.append(dict(x=x, y=y, direction=direction, pressure=pressure,
                               speed=speed, distance=next_at, progress=progress,
                               seed=seed, index=index))
            index += 1
            step = max(0.35, spacing)
            if variation:
                step *= 1.0 + rng.uniform(-variation / 100.0, variation / 100.0) * 0.8
            next_at += step
        travelled += seg
    return result


def selected_children(node, stamp_index, seed, mode, step=1, weighted=True):
    children = [c for c in node.get("children", []) if c.get("visible", True)]
    parts = [c for c in children if c["kind"] in ("mark", "group", "brush")]
    if not parts:
        return []
    mode = mode or node.get("mode", "Paint together")
    wanted = max(1, int(node.get("members_per_stamp", 1)))
    rng = Random((seed ^ (stamp_index * 2654435761)) & 0xFFFFFFFF)
    if mode in ("Paint together", "Combination"):
        return parts
    if mode == "In order":
        return [parts[(stamp_index // max(1, step)) % len(parts)]]
    if mode == "Random":
        pool = parts[:]
        rng.shuffle(pool)
        return pool[:min(wanted, len(pool))]
    if mode == "Weighted" and weighted:
        pool = parts[:]
        chosen = []
        while pool and len(chosen) < min(wanted, len(parts)):
            total = sum(max(0.0, c.get("weight", 1)) for c in pool) or 1.0
            target = rng.random() * total
            running = 0.0
            pick = pool[-1]
            for candidate in pool:
                running += max(0.0, candidate.get("weight", 1))
                if target <= running:
                    pick = candidate
                    break
            chosen.append(pick)
            pool.remove(pick)
        return chosen
    if mode == "Alternating":
        return [parts[(stamp_index + i) % len(parts)] for i in range(min(wanted, len(parts)))]
    return parts


def taper_envelope(doc, sample, key, start_end):
    amount = resolve(doc, key, sample, sample.get("index", 0)) / 100.0
    if amount <= 0:
        return 1.0
    progress = sample.get("progress", 0.5)
    opening = 0.18
    closing = 0.22
    if start_end == "start":
        edge = bound(progress / opening)
        start = effective(doc, "start_behaviour")
        base = {"Pointed": 0.0, "Round": 0.36, "Flat": 0.68, "Full contact": 1.0, "Skipped": 0.0}[start]
        if start == "Skipped" and progress < 0.035:
            return 0.0
        edge = base + (1 - base) * (edge ** (1.0 + resolve(doc, "tip_shaping", sample, 0) / 90.0))
        return 1 - amount * (1 - edge)
    edge = bound((1 - progress) / closing)
    end = effective(doc, "end_behaviour")
    base = {"Lift": 0.0, "Taper": 0.0, "Flat": 0.62, "Hold": 1.0, "Release": 0.12}[end]
    edge = base + (1 - base) * (edge ** (1.0 + resolve(doc, "tip_shaping", sample, 0) / 90.0))
    return 1 - amount * (1 - edge)


def surface_pattern(assets, key, size, opacity=100):
    image = assets.grain(key, 100, False, True)
    brush = QBrush(image)
    transform = QTransform()
    transform.scale(size / image.width(), size / image.height())
    brush.setTransform(transform)
    return brush


def apply_texture(painter, rect, node, assets, stroke_start, size, sample=None, doc=None):
    strength = node.get("texture_strength", 40) / 100.0
    if doc is not None and sample is not None:
        strength *= resolve(doc, "texture_strength", sample, sample.get("index", 0)) / 100.0
    if strength <= 0:
        return
    key = node.get("texture_source", "Cold press")
    mapped = SURFACES.get(key, key.lower().replace(" ", "_"))
    from assets import GRAINS
    if mapped not in GRAINS:
        mapped = "paper"
    grain = assets.grain(mapped, node.get("texture_contrast", 100), False,
                         node.get("texture_filter", "Smooth") == "Smooth")
    brush = QBrush(grain)
    transform = QTransform()
    anchor = node.get("texture_anchor", "Canvas")
    if anchor == "Stroke" and stroke_start:
        transform.translate(stroke_start[0], stroke_start[1])
        transform.rotate(stroke_start[2])
    transform.rotate(node.get("texture_rotation", 0))
    if node.get("texture_per_stamp") and sample is not None:
        transform.rotate(sample.get("direction", 0.0) * 0.35)
        transform.translate((sample.get("index", 0) * 7) % 61, (sample.get("index", 0) * 11) % 47)
    scale = max(0.02, node.get("texture_scale", 100) / 100.0 * max(0.35, size / 260.0))
    transform.scale(scale, scale)
    transform.translate(node.get("texture_offset_x", 0), node.get("texture_offset_y", 0))
    brush.setTransform(transform)
    old = painter.compositionMode()
    painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationIn)
    painter.setOpacity(bound(strength * (0.55 + node.get("texture_depth", 70) / 220.0)))
    painter.fillRect(rect, brush)
    painter.setOpacity(1.0)
    painter.setCompositionMode(old)


def draw_mark(painter, doc, node, sample, seed, stamp_index, assets, base_image, chain):
    size = resolve(doc, "tip_size", sample, stamp_index)
    size *= node.get("scale", 100) / 100.0
    size *= chain.get("scale", 1.0)
    size *= taper_envelope(doc, sample, "taper_size", "start")
    size *= taper_envelope(doc, sample, "taper_size", "end")
    size *= 1.0 - resolve(doc, "distance_effects", sample, stamp_index) / 100.0 * bound(sample.get("distance", 0.0) / 900.0)
    size *= 1.0 - resolve(doc, "speed_effects", sample, stamp_index) / 100.0 * bound(sample.get("speed", 0.0)) * 0.42
    minimum = resolve(doc, "tip_size", sample, stamp_index) * resolve(doc, "tip_min_size", sample, stamp_index) / 100.0
    size = max(max(0.5, minimum), size)
    max_size = effective(doc, "property_max_size")
    size = min(size, max_size)
    if effective(doc, "property_screen_size"):
        size *= 1.0
    if size < 0.45:
        return

    rng = Random((seed ^ (stamp_index * 2246822519) ^ stable_hash(node["uid"])) & 0xFFFFFFFF)
    variation = resolve(doc, "mark_variation", sample, stamp_index) / 100.0
    if variation:
        size *= rng.uniform(1 - variation * 0.45, 1 + variation * 0.45)

    aspect = resolve(doc, "tip_aspect", sample, stamp_index) / 100.0 * node.get("aspect", 100) / 100.0
    angle = resolve(doc, "tip_angle", sample, stamp_index) + node.get("angle", 0)
    angle += chain.get("angle", 0.0)
    rotation_source = effective(doc, "tip_rotation_source")
    if effective(doc, "follow_path"):
        angle += sample.get("direction", 0.0)
    if rotation_source == "Tilt":
        angle += (signal_value("Tilt", sample, stamp_index) - 0.5) * 90
    elif rotation_source == "Barrel roll":
        angle += sample.get("rotation", 0.0)
    elif rotation_source == "Random":
        angle += rng.uniform(-90, 90)
    aspect *= 0.85 + 0.55 * signal_value("Tilt", sample, stamp_index)
    if effective(doc, "mode_ribbon"):
        aspect *= 1.0 + 0.28 * sin(stamp_index * 0.55)

    flip_h = bool(effective(doc, "flip_h")) ^ bool(node.get("flipH"))
    flip_v = bool(effective(doc, "flip_v")) ^ bool(node.get("flipV"))
    hardness = resolve(doc, "tip_hardness", sample, stamp_index)
    threshold = resolve(doc, "shape_filter", sample, stamp_index)
    alpha = resolve(doc, "flow", sample, stamp_index) / 100.0
    alpha *= resolve(doc, "density", sample, stamp_index) / 100.0
    alpha *= node.get("opacity", 100) / 100.0
    alpha *= chain.get("opacity", 1.0)
    alpha *= taper_envelope(doc, sample, "taper_opacity", "start")
    alpha *= taper_envelope(doc, sample, "taper_opacity", "end")
    alpha *= 1.0 - resolve(doc, "taper_flow", sample, stamp_index) / 100.0 * (
        1 - min(taper_envelope(doc, sample, "taper_flow", "start"),
                taper_envelope(doc, sample, "taper_flow", "end")))
    alpha = bound(alpha * (0.35 + 0.65 * sample.get("pressure", 1.0)))
    if variation:
        alpha *= rng.uniform(1 - variation * 0.55, 1.0)
    alpha = min(alpha, resolve(doc, "ink_limit", sample, stamp_index) / 100.0)
    if alpha <= 0.002:
        return

    # Wet pickup samples the artist's existing surface, never the stroke being
    # built, so the result is stable as the preview re-renders.
    wet = bool(effective(doc, "wet_enabled")) and effective(doc, "wet_medium") != "Off"
    ink_colour = QColor(doc.get("colour", "#2b2927"))
    if wet and base_image is not None and 0 <= sample["x"] < base_image.width() and 0 <= sample["y"] < base_image.height():
        pickup = resolve(doc, "wet_pickup", sample, stamp_index) / 100.0
        mixing = resolve(doc, "wet_mixing", sample, stamp_index) / 100.0
        if pickup > 0.01:
            sx = int(sample["x"])
            sy = int(sample["y"])
            pull = resolve(doc, "wet_pull", sample, stamp_index) / 100.0
            sx = max(0, min(base_image.width() - 1, int(sx - cos(radians(sample.get("direction", 0))) * pull * 7)))
            sy = max(0, min(base_image.height() - 1, int(sy - sin(radians(sample.get("direction", 0))) * pull * 7)))
            picked = base_image.pixelColor(sx, sy)
            if picked.alpha() > 8:
                ink_colour = mix_colour(ink_colour, picked, pickup * mixing)
        dilution = resolve(doc, "wet_dilution", sample, stamp_index) / 100.0
        alpha *= 1.0 - dilution * 0.38
        if effective(doc, "wet_medium") == "Gouache":
            alpha *= 1.08
        elif effective(doc, "wet_medium") == "Ink":
            alpha *= 1.15
    if wet:
        if resolve(doc, "wet_blur", sample, stamp_index) > 0:
            alpha *= 0.92
        if resolve(doc, "wet_variation", sample, stamp_index) > 0:
            alpha *= 0.82 + 0.18 * abs(sin(stamp_index * 0.7))

    colour = vary_colour(doc, ink_colour, sample, seed,
                         effective(doc, "colour_scope"), stamp_index)
    tinted = assets.stamp(node.get("asset", "nib"), colour.name(),
                          hardness, threshold)

    diameter = max(0.6, size)
    height = diameter * aspect
    painter.save()
    painter.translate(sample["x"], sample["y"])
    painter.rotate(angle)
    painter.setOpacity(alpha)
    rect = QRectF(-diameter / 2, -height / 2, diameter, height)
    if flip_h:
        rect = QRectF(-rect.right(), rect.top(), rect.width(), rect.height())
    if flip_v:
        rect = QRectF(rect.left(), -rect.bottom(), rect.width(), rect.height())
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform,
                          bool(effective(doc, "antialias")))
    if resolve(doc, "wet_edges", sample, stamp_index) > 0 or resolve(doc, "burnt_edges", sample, stamp_index) > 0:
        edge = max(resolve(doc, "wet_edges", sample, stamp_index),
                   resolve(doc, "burnt_edges", sample, stamp_index)) / 100.0
        painter.setOpacity(alpha * edge * 0.34)
        painter.drawImage(rect.adjusted(-diameter * 0.045, -height * 0.045,
                                        diameter * 0.045, height * 0.045), tinted)
    painter.setOpacity(alpha)
    painter.drawImage(rect, tinted)
    painter.restore()


def _max_scale(node):
    scale = node.get("scale", 100) / 100.0
    for child in node.get("children", []):
        scale = max(scale, scale * _max_scale(child))
    return scale


def _stamp_rect(dest, doc, node, sample, stamp_index, chain):
    """A conservative canvas-space box around one stamp.

    Group layers used to be full-canvas images.  Allocating one per stamp made
    a slider drag spend seconds copying empty pixels.  Every mark lives inside
    a small multiple of the effective tip size, so composite a local box and
    let the parent painter map it back into canvas coordinates.
    """
    size = resolve(doc, "tip_size", sample, stamp_index)
    extent = max(24.0, size * max(0.25, chain.get("scale", 1.0)) * _max_scale(node) * 1.6 + 20.0)
    left = int(sample.get("x", 0.0) - extent)
    top = int(sample.get("y", 0.0) - extent)
    width = int(extent * 2)
    height = int(extent * 2)
    left = max(0, min(max(0, dest.device().width() - 1), left))
    top = max(0, min(max(0, dest.device().height() - 1), top))
    width = max(1, min(dest.device().width() - left, width))
    height = max(1, min(dest.device().height() - top, height))
    return QRect(left, top, width, height)


def composite_node(dest, doc, node, sample, seed, stamp_index, assets, base_image,
                   chain=None, stroke_start=None, defer_textures=False):
    if not node.get("visible", True) or node.get("opacity", 100) <= 0:
        return
    chain = chain or {"scale": 1.0, "opacity": 1.0, "angle": 0.0}
    kind = node["kind"]
    if kind == "mark":
        draw_mark(dest, doc, node, sample, seed, stamp_index, assets, base_image, chain)
        return
    if kind in ("texture", "mask"):
        apply_texture(dest, QRectF(0, 0, dest.device().width(), dest.device().height()),
                      node, assets, stroke_start, resolve(doc, "tip_size", sample, stamp_index),
                      sample, doc)
        return
    if kind not in ("group", "brush"):
        return

    child_chain = {
        "scale": chain["scale"] * node.get("scale", 100) / 100.0,
        "opacity": chain["opacity"] * node.get("opacity", 100) / 100.0,
        "angle": chain["angle"] + node.get("angle", 0),
    }
    mode = effective(doc, "stroke_mode")
    group_mode = node.get("mode", "Paint together")
    if kind == "group":
        if mode == "Repeated stamp":
            child_mode = "Paint together"
        elif mode == "Ordered sequence":
            child_mode = "In order"
        elif mode == "Random":
            child_mode = "Random"
        elif mode == "Weighted":
            child_mode = "Weighted"
        elif mode == "Alternating":
            child_mode = "Alternating"
        else:
            child_mode = group_mode
    else:
        child_mode = group_mode
    chosen = selected_children(node, stamp_index, seed ^ stable_hash(node["uid"]),
                               child_mode, int(effective(doc, "mode_step")),
                               bool(effective(doc, "mode_weighted")))
    amount = node.get("amount", 100) / 100.0 if kind == "brush" else 1.0
    opacity = bound(node.get("opacity", 100) / 100.0 * amount)
    textures = [] if defer_textures else [
        child for child in node.get("children", [])
        if child.get("visible", True) and child["kind"] in ("texture", "mask")]

    # Composite straight into the parent whenever no local texture layer is
    # required.  The child chain already carries this node's opacity and scale;
    # the paint mode only needs to wrap the children.  This avoids allocating
    # and clearing a layer for every mark of every stamp.
    if not textures:
        dest.save()
        dest.setCompositionMode(BLEND.get(node.get("blend", "Normal"), BLEND["Normal"]))
        for child in chosen:
            composite_node(dest, doc, child, sample, seed, stamp_index, assets,
                           base_image, child_chain, stroke_start, defer_textures)
        dest.restore()
        return

    rect = _stamp_rect(dest, doc, node, sample, stamp_index, chain)
    buffer = allocate(rect.width(), rect.height())
    painter = QPainter(buffer)
    try:
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setWindow(rect)
        painter.setViewport(buffer.rect())
        for child in chosen:
            composite_node(painter, doc, child, sample, seed, stamp_index, assets,
                           base_image, child_chain, stroke_start, defer_textures)
        for child in textures:
            apply_texture(painter, QRectF(rect), child, assets, stroke_start,
                          resolve(doc, "tip_size", sample, stamp_index), sample, doc)
    finally:
        painter.end()

    dest.save()
    dest.setOpacity(opacity)
    dest.setCompositionMode(BLEND.get(node.get("blend", "Normal"), BLEND["Normal"]))
    dest.drawImage(rect.topLeft(), buffer)
    dest.restore()

def render_stroke_proxy(painter, doc, stroke, w, h):
    """A cheap, tapered path preview for the frames while a slider moves.

    Stamping hundreds of textured marks per frame in Python cannot stay inside
    a 60 Hz budget.  The proxy keeps the path, pressure taper, width, colour
    and opacity, then the full stamp renderer replaces it when the drag ends.
    """
    raw = [(x * w, y * h, pressure, speed) for x, y, pressure, speed in stroke["points"]]
    if not raw:
        return
    probe = {"pressure": raw[0][2]}
    smoothing = max(resolve(doc, "path_smoothing", probe, 0),
                    resolve(doc, "stabilisation", probe, 0) * 0.8,
                    resolve(doc, "motion_filter", probe, 0) * 0.45)
    raw = smooth_points(raw, smoothing, resolve(doc, "sharp_angles", probe, 0))
    size = max(1.0, resolve(doc, "tip_size", {"pressure": 0.8, "speed": 0.2}, 0))
    colour = QColor(doc.get("colour", "#2b2927"))
    alpha = bound(resolve(doc, "opacity", {"pressure": 1.0}, 0) / 100.0)
    alpha *= bound(resolve(doc, "flow", {"pressure": 1.0}, 0) / 100.0 + 0.28)
    painter.save()
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setOpacity(alpha)
    if effective(doc, "property_eraser"):
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
    else:
        painter.setCompositionMode(BLEND.get(effective(doc, "layer_blend"), BLEND["Normal"]))
    if len(raw) == 1:
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(colour)
        painter.drawEllipse(QPointF(raw[0][0], raw[0][1]), size / 2, size / 2)
        painter.restore()
        return
    # A ribbon whose width follows pressure reads as a brush much better than
    # a constant-width pen, and costs one filled polygon for the whole stroke.
    left, right = [], []
    for index, (x, y, pressure, _speed) in enumerate(raw):
        previous = raw[max(0, index - 1)]
        following = raw[min(len(raw) - 1, index + 1)]
        dx, dy = following[0] - previous[0], following[1] - previous[1]
        length = max(1e-4, hypot(dx, dy))
        nx, ny = -dy / length, dx / length
        taper = min(1.0, index / 6.0, (len(raw) - 1 - index) / 6.0)
        width = size * (0.34 + 0.66 * pressure) * (0.55 + 0.45 * taper)
        left.append((x + nx * width / 2, y + ny * width / 2))
        right.append((x - nx * width / 2, y - ny * width / 2))
    path = QPainterPath()
    path.moveTo(*left[0])
    for point in left[1:]:
        path.lineTo(*point)
    for point in reversed(right):
        path.lineTo(*point)
    path.closeSubpath()
    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(colour)
    painter.drawPath(path)
    painter.restore()



def render_stroke(painter, doc, stroke, w, h, assets, base_image=None, quality=1.0,
                  spacing_scale=1.0, texture_pass=True):
    raw = [(x * w, y * h, pressure, speed) for x, y, pressure, speed in stroke["points"]]
    if not raw:
        return
    probe = {"pressure": raw[0][2]}
    smoothing = max(resolve(doc, "path_smoothing", probe, 0),
                    resolve(doc, "stabilisation", probe, 0) * 0.8,
                    resolve(doc, "motion_filter", probe, 0) * 0.45)
    raw = smooth_points(raw, smoothing, resolve(doc, "sharp_angles", probe, 0))
    seed = int(stroke.get("seed", 7))
    size = max(1.0, resolve(doc, "tip_size", {"pressure": 0.8, "speed": 0.2}, 0))
    spacing_px = max(0.7, size * resolve(doc, "spacing", {"pressure": 1.0}, 0) / 100.0)
    spacing_px *= max(1.0, spacing_scale)
    stream = events(raw, spacing_px, resolve(doc, "spacing_variation", {"pressure": 1.0}, 0),
                    resolve(doc, "jitter", {"pressure": 1.0}, 0), seed)
    if not stream:
        return
    quality = max(0.25, min(1.0, float(quality)))
    sw, sh = max(1, int(w * quality)), max(1, int(h * quality))
    scratch = allocate(sw, sh)
    paint = QPainter(scratch)
    try:
        paint.setRenderHint(QPainter.RenderHint.Antialiasing)
        paint.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        if quality < 0.999:
            paint.setWindow(QRect(0, 0, int(w), int(h)))
            paint.setViewport(scratch.rect())
        for sample in stream:
            sample["tilt"] = stroke.get("tilt", 0.22)
            sample["rotation"] = stroke.get("rotation", 0.0)
            sample["time"] = bound(sample.get("progress", 0.0))
            repeats = max(1, int(resolve(doc, "stamp_count", sample, sample["index"])))
            falloff = resolve(doc, "stamp_falloff", sample, sample["index"]) / 100.0
            for repeat in range(repeats):
                local = dict(sample)
                if repeat:
                    angle = (repeat / max(1, repeats)) * 2 * pi
                    shift = size * 0.10 * repeat
                    local["x"] += cos(angle) * shift
                    local["y"] += sin(angle) * shift
                    local["opacity_scale"] = max(0.0, 1 - falloff * repeat / max(1, repeats - 1))
                composite_node(paint, doc, doc["root"], local, seed, sample["index"] + repeat,
                               assets, base_image, defer_textures=True)
            if bool(effective(doc, "spray_enabled")):
                count = int(resolve(doc, "spray_count", sample, sample["index"]))
                spread = resolve(doc, "spray_scatter", sample, sample["index"]) / 100.0 * size
                density = resolve(doc, "spray_density", sample, sample["index"]) / 100.0
                rng = Random((seed ^ sample["index"] * 3266489917) & 0xFFFFFFFF)
                for particle in range(count):
                    if rng.random() > density:
                        continue
                    angle = rng.uniform(0, 2 * pi)
                    dist = spread * rng.random() ** 0.6
                    local = dict(sample)
                    local["x"] += cos(angle) * dist
                    local["y"] += sin(angle) * dist
                    local["index"] = sample["index"] * 97 + particle
                    composite_node(paint, doc, doc["root"], local, seed ^ particle,
                                   local["index"], assets, base_image, defer_textures=True)
        # Grain and masks at the root belong to the finished stroke, not to
        # every stamp.  Applying them once keeps the texture honest and avoids
        # a full-canvas pattern fill per stamp.
        if texture_pass:
            probe = {"pressure": raw[0][2], "x": raw[0][0], "y": raw[0][1], "index": 0}
            for child in doc["root"].get("children", []):
                if child.get("visible", True) and child["kind"] in ("texture", "mask"):
                    apply_texture(paint, QRectF(0, 0, w, h), child, assets,
                                  (raw[0][0], raw[0][1], 0.0),
                                  resolve(doc, "tip_size", probe, 0), probe, doc)
    finally:
        paint.end()
    painter.save()
    painter.setOpacity(bound(resolve(doc, "opacity", {"pressure": 1.0}, 0) / 100.0))
    if effective(doc, "property_eraser"):
        painter.setCompositionMode(QPainter.CompositionMode.CompositionMode_DestinationOut)
    else:
        painter.setCompositionMode(BLEND.get(effective(doc, "layer_blend"), BLEND["Normal"]))
    if quality < 0.999:
        painter.drawImage(QRectF(0, 0, w, h), scratch)
    else:
        painter.drawImage(0, 0, scratch)
    painter.restore()


def motion(fn, size=54, seed=77, count=52, pressure_scale=1.0):
    points = []
    for i in range(count):
        t = i / (count - 1)
        pressure = (0.16 + 0.80 * (sin(pi * t) ** 0.64)) * pressure_scale
        points.append((*fn(t), pressure, 0.08 + 0.62 * abs(sin(pi * t))))
    return dict(points=points, seed=seed, size=size)


def sample_strokes(mode, doc, w, h):
    size = resolve(doc, "tip_size", {"pressure": 1.0}, 0)
    scale = min(1.0, max(0.55, 70.0 / max(8, size)))
    if mode == "mark":
        result = []
        for index, pressure in enumerate((0.28, 0.52, 0.76, 1.0)):
            x = 0.16 + index * 0.20
            result.append(dict(points=[(x, 0.54, pressure, 0.12)], seed=40 + index, size=size))
        return result
    if mode == "stroke":
        return [motion(lambda t: (0.10 + 0.80 * t, 0.34 + 0.09 * sin(t * 4.4 + 0.2)),
                       seed=77, pressure_scale=0.92),
                motion(lambda t: (0.12 + 0.74 * t, 0.56 + 0.04 * sin(t * 3.1 + 1.1)),
                       seed=78, pressure_scale=0.76),
                motion(lambda t: (0.18 + 0.61 * t, 0.75 + 0.04 * sin(t * 5.0 - 0.4)),
                       seed=79, pressure_scale=0.58)]
    if mode == "repeat":
        result = []
        for row in range(3):
            for column in range(2):
                x0, y0 = 0.10 + column * 0.43, 0.25 + row * 0.22
                result.append(motion(lambda t, x0=x0, y0=y0: (x0 + 0.34 * t, y0 + 0.045 * sin(t * 6.2)),
                                     seed=90 + row * 2 + column, pressure_scale=0.86))
        return result
    if mode == "pressure":
        return [motion(lambda t: (0.10 + 0.78 * t, 0.32 + 0.075 * sin(t * 3.8)),
                       seed=61, pressure_scale=0.32),
                motion(lambda t: (0.10 + 0.78 * t, 0.52 + 0.055 * sin(t * 3.8)),
                       seed=62, pressure_scale=0.62),
                motion(lambda t: (0.10 + 0.78 * t, 0.72 + 0.035 * sin(t * 3.8)),
                       seed=63, pressure_scale=0.94)]
    if mode == "texture":
        return [motion(lambda t: (0.08 + 0.84 * t, 0.55 + 0.16 * sin(t * 2.3 * pi - 0.6)),
                       seed=71, pressure_scale=0.9)]
    if mode == "mixing":
        return [motion(lambda t: (0.10 + 0.78 * t, 0.54 + 0.11 * sin(t * 2.6 * pi + 0.4)),
                       seed=73, pressure_scale=0.86)]
    if mode == "surface":
        return [motion(lambda t: (0.12 + 0.72 * t, 0.24 + 0.06 * sin(t * 3.4)),
                       seed=81, pressure_scale=0.78),
                motion(lambda t: (0.14 + 0.66 * t, 0.53 + 0.05 * sin(t * 4.1)),
                       seed=82, pressure_scale=0.72),
                motion(lambda t: (0.16 + 0.58 * t, 0.78 + 0.035 * sin(t * 5.3)),
                       seed=83, pressure_scale=0.66)]
    return []


def paint_surface(painter, doc, w, h, assets):
    key = SURFACES.get(effective(doc, "surface"), "paper")
    scale = effective(doc, "surface_scale") / 100.0
    base = QColor("#e8e4da")
    if effective(doc, "surface") in ("Linen canvas", "Cotton canvas"):
        base = QColor("#ded9cd")
    elif effective(doc, "surface") == "Non-absorbent palette":
        base = QColor("#e4e2dc")
    painter.fillRect(0, 0, w, h, base)
    grain = assets.grain(key, 82, False, True)
    brush = QBrush(grain)
    transform = QTransform()
    transform.scale(scale, scale)
    brush.setTransform(transform)
    painter.setOpacity(bound(effective(doc, "surface_grain") / 100.0) * 0.22)
    painter.fillRect(0, 0, w, h, brush)
    painter.setOpacity(1.0)
    # A very quiet edge and registration marks make the pad read as a surface,
    # not as a floating card.
    painter.setPen(QPen(QColor(0, 0, 0, 26), 1))
    painter.drawRect(QRectF(14.5, 14.5, w - 29, h - 29))


def paint_mixing_base(painter, w, h):
    painter.save()
    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(QColor("#8c8478"))
    painter.setOpacity(0.42)
    painter.drawEllipse(QRectF(w * 0.12, h * 0.30, w * 0.32, h * 0.34))
    painter.setBrush(QColor("#c8b9a3"))
    painter.setOpacity(0.5)
    painter.drawEllipse(QRectF(w * 0.44, h * 0.38, w * 0.30, h * 0.26))
    painter.setBrush(QColor("#5a5854"))
    painter.setOpacity(0.30)
    painter.drawEllipse(QRectF(w * 0.28, h * 0.58, w * 0.36, h * 0.22))
    painter.restore()


def paint_labels(painter, mode, w, h):
    font = QFont("Inter", 9)
    font.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, 0.7)
    painter.setFont(font)
    painter.setPen(QColor(0, 0, 0, 92))
    captions = {
        "mark": ["light contact", "medium", "firm", "full pressure"],
        "pressure": ["3% pressure", "58% pressure", "90% pressure"],
        "surface": ["surface tooth", "material response", "edge accumulation"],
    }.get(mode, [])
    if mode == "mark":
        for index, text in enumerate(captions):
            painter.drawText(QRectF(w * (0.08 + index * 0.20), h * 0.63, w * 0.18, 18),
                             Qt.AlignmentFlag.AlignHCenter, text)
    elif mode in ("pressure", "surface"):
        for index, text in enumerate(captions):
            y = h * (0.23 if mode == "surface" else 0.24) + index * h * (0.27 if mode == "surface" else 0.20)
            painter.drawText(QRectF(w * 0.08, y, 160, 18), Qt.AlignmentFlag.AlignLeft, text)


def paint_guides(painter, w, h):
    painter.save()
    painter.setPen(QPen(QColor(0, 0, 0, 18), 1, Qt.PenStyle.DashLine))
    for i in (1, 2):
        painter.drawLine(QPointF(w * i / 3, 20), QPointF(w * i / 3, h - 20))
        painter.drawLine(QPointF(20, h * i / 3), QPointF(w - 20, h * i / 3))
    painter.restore()


def digest_image(image):
    rgba = image.convertToFormat(QImage.Format.Format_RGBA8888)
    return hashlib.sha256(rgba.bits().tobytes()).hexdigest()
