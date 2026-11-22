"""Brush documents for the review. Construction is general, never subject-specific."""
from copy import deepcopy
from itertools import count

import catalog

SERIAL = count(1)


def next_serial():
    return next(SERIAL)
MODES = ["Paint together", "In order", "Random", "Weighted", "Alternating", "Combination"]
BLENDS = ["Normal", "Multiply", "Screen", "Overlay", "Soft light", "Height", "Mask"]
TEX_ANCHORS = ["Canvas", "Stroke", "Per stamp"]


def node(kind, name, **values):
    ident = values.pop("uid", None) or f"n{next(SERIAL)}"
    base = dict(
        uid=ident, kind=kind, name=name, visible=True, opacity=100, blend="Normal",
        mode=MODES[0], weight=4, amount=100, scale=100, angle=0, aspect=100,
        flipH=False, flipV=False, asset="graphite", children=[], texture_scale=100,
        texture_rotation=0, texture_strength=40, texture_depth=70, texture_contrast=100,
        texture_anchor="Canvas", texture_offset_x=0, texture_offset_y=0,
        texture_source="Cold press", texture_tiling=True, texture_per_stamp=False,
        texture_filter="Smooth", members_per_stamp=1, ref="",
    )
    base.update(values)
    return base


def mark(name, asset, **values):
    return node("mark", name, asset=asset, **values)


def group(name, children=(), **values):
    return node("group", name, children=list(children), **values)


def texture(name, key, **values):
    return node("texture", name, texture_source=key, **values)


def mask(name, key, **values):
    return node("mask", name, texture_source=key, **values)


def secondary(name, children=(), amount=30, blend="Normal", **values):
    return node("brush", name, children=list(children), amount=amount, blend=blend, **values)


def walk(root):
    stack = [(root, None, 0)]
    while stack:
        item, parent, depth = stack.pop()
        yield item, parent, depth
        for child in reversed(item["children"]):
            stack.append((child, item, depth + 1))


def find(root, uid):
    return next((item for item in walk(root) if item[0]["uid"] == uid), None)


def clone(root, fresh=True):
    result = deepcopy(root)

    def visit(item):
        if fresh:
            item["uid"] = f"n{next(SERIAL)}"
        for child in item["children"]:
            visit(child)
        return item

    return visit(result)


def counts(root):
    result = {"group": 0, "mark": 0, "texture": 0, "mask": 0, "brush": 0}
    for item, _parent, _depth in walk(root):
        if item["kind"] in result:
            result[item["kind"]] += 1
    return result


def pressure_curve(points, name="Pressure"):
    return [dict(x=x, y=y) for x, y in points]


def binding(ident, name, signal, target, amount, points, scope="Per stamp"):
    return dict(id=ident, name=name, signal=signal, target=target, amount=amount,
                curve=pressure_curve(points), scope=scope, enabled=True)


def build_ferrous():
    root = node("brush", "Ferrous Ink", children=[
        group("Core mark", [
            mark("Graphite point", "graphite", weight=9, opacity=88),
            mark("Nib", "nib", weight=3, scale=82, angle=7, opacity=72),
        ], mode="Weighted", members_per_stamp=1, opacity=100),
        group("Broken edge", [
            mark("Torn wisp", "wisp", weight=3, scale=112, angle=-16),
            mark("Rake", "rake", weight=4, scale=96, angle=11),
            mark("Shale chip", "chip", weight=2, scale=64, angle=26),
        ], mode="Random", members_per_stamp=1, spacing=100, opacity=58),
        texture("Cold press", "Cold press", texture_strength=36, texture_scale=118,
                texture_depth=66, texture_anchor="Canvas"),
        secondary("Cotton haze", [
            mark("Soft round", "air", scale=160, opacity=36),
            texture("Fine mist", "Fine mist", texture_strength=28, texture_anchor="Stroke"),
        ], amount=32, blend="Soft light", scale=155, opacity=70),
        secondary("Dry drag", [
            group("Strands", [
                mark("Dry bristles", "bristle", scale=118, angle=3),
                mark("Fine strands", "hatch", scale=82, angle=-8),
            ], mode="Alternating", members_per_stamp=1),
        ], amount=26, blend="Multiply", opacity=64),
        secondary("Soft lift", [
            mark("Frayed veil", "veil", scale=144, angle=-22, opacity=42),
        ], amount=17, blend="Screen", opacity=48),
    ])
    return root


def build_graphite():
    return node("brush", "Dry Graphite", children=[
        group("Core", [
            mark("Graphite point", "graphite", scale=78, opacity=82),
            mark("Scraped edge", "scrape", scale=108, angle=8, weight=3),
        ], mode="Weighted", members_per_stamp=1),
        texture("Paper tooth", "Paper tooth", texture_strength=44, texture_scale=86, texture_depth=76),
        secondary("Soft dust", [mark("Soft round", "air", scale=130, opacity=22)],
                  amount=14, blend="Screen"),
    ])


def build_wet():
    return node("brush", "Wet Ochre", children=[
        group("Reservoir mark", [
            mark("Round nib", "nib", scale=92, opacity=68),
            mark("Broken edge", "broken", scale=86, weight=3, opacity=54),
        ], mode="Weighted", members_per_stamp=1),
        texture("Cold press", "Cold press", texture_strength=26, texture_scale=134, texture_depth=58),
        secondary("Water bloom", [mark("Soft round", "air", scale=78, opacity=7)],
                  amount=12, blend="Multiply", opacity=38),
    ])


def build_spatter():
    return node("brush", "Mist Spatter", children=[
        group("Drops", [
            mark("Fine dots", "spatter", scale=96, opacity=72),
            mark("Torn wisp", "wisp", scale=72, weight=2, opacity=58),
        ], mode="Random", members_per_stamp=2, opacity=82),
        texture("Flecks", "Flecks", texture_strength=22, texture_scale=152, texture_depth=54),
        secondary("Haze", [mark("Soft round", "air", scale=140, opacity=12)],
                  amount=12, blend="Screen"),
    ])


STARTERS = {
    "ferrous": ("Ferrous Ink", build_ferrous, {"tip_size": 54, "flow": 62, "spacing": 8,
                                               "taper_pressure": 55, "taper_size": 66, "opacity": 88}),
    "graphite": ("Dry Graphite", build_graphite, {"tip_size": 28, "flow": 46, "spacing": 6,
                                                  "opacity": 78, "taper_size": 72, "tip_hardness": 82}),
    "wet": ("Wet Ochre", build_wet, {"tip_size": 88, "flow": 54, "opacity": 82, "spacing": 8,
                                     "wet_medium": "Watercolour", "wet_pickup": 48, "wet_pull": 42,
                                     "surface": "Rough paper"}),
    "spatter": ("Mist Spatter", build_spatter, {"tip_size": 36, "flow": 70, "opacity": 74,
                                                "spacing": 58, "spray_enabled": 1, "spray_count": 9,
                                                "spray_scatter": 84, "spray_density": 58}),
}


def brush_doc(key="ferrous"):
    label, builder, patch = STARTERS[key]
    settings = catalog.defaults()
    settings.update({
        "tip_size": 54, "tip_hardness": 74, "tip_aspect": 100, "opacity": 88, "flow": 62,
        "spacing": 18, "spacing_variation": 6, "path_smoothing": 0, "stabilisation": 14,
        "sharp_angles": 30, "taper_pressure": 55, "taper_touch": 30, "taper_size": 66,
        "taper_opacity": 50, "taper_flow": 36, "start_behaviour": "Pointed",
        "end_behaviour": "Lift", "texture_source": "Cold press", "texture_strength": 36,
        "texture_scale": 118, "texture_depth": 66, "surface": "Cold press",
        "wet_pickup": 26, "wet_pull": 18, "brightness_variation": 9,
        "colour_secondary": 0, "spray_enabled": 0, "stroke_mode": "Continuous",
        "dynamics_strength": 100, "dynamics_fallback": "Distance",
        "assist_device": "Pen · simulated", "assist_pressure": 50,
    })
    settings.update(patch)
    doc = dict(
        key=key, name=label, engine="Digital", root=builder(), settings=settings,
        overrides={}, pins=[], initial=dict(settings), assist={key: settings[key] for key in (
            "assist_device", "assist_smoothing", "assist_speed", "assist_pressure",
            "assist_fallback", "assist_tilt", "assist_roll", "assist_hover", "assist_catchup")},
        dynamics=[
            binding("d1", "Pressure opens the tip", "Pressure", "tip_size", 0.58,
                    [(0, 0.22), (0.35, 0.48), (0.72, 0.82), (1, 1.0)]),
            binding("d2", "Light touch is lighter ink", "Pressure", "opacity", 0.34,
                    [(0, 0.28), (0.5, 0.62), (1, 1.0)]),
            binding("d3", "Fast hand thins the line", "Velocity", "tip_size", 0.22,
                    [(0, 1.0), (0.55, 0.86), (1, 0.62)]),
            binding("d4", "Tilt widens the edge", "Tilt", "tip_aspect", 0.18,
                    [(0, 0.9), (1, 1.35)]),
        ],
        presets=[
            dict(id="p1", name="Soft pressure", signal="Pressure",
                 curve=pressure_curve([(0, 0.2), (0.5, 0.55), (1, 1.0)]), scope="Per stamp"),
            dict(id="p2", name="Fast thinning", signal="Velocity",
                 curve=pressure_curve([(0, 1.0), (0.5, 0.82), (1, 0.55)]), scope="Per stamp"),
            dict(id="p3", name="Tilt flare", signal="Tilt",
                 curve=pressure_curve([(0, 0.8), (1, 1.5)]), scope="Per stamp"),
            dict(id="p4", name="Wobble", signal="Random",
                 curve=pressure_curve([(0, 0.55), (0.5, 1.0), (1, 0.55)]), scope="Per stamp"),
        ],
        colour="#2b2927", secondary_colour="#7a756c",
        tips=["graphite", "nib", "bristle", "air", "round", "billow", "broken", "wisp",
              "rake", "ribbon", "veil", "spatter", "leaf", "chip", "hatch", "scrape"],
        version=1,
    )
    return doc


def catalog_docs():
    return {key: brush_doc(key) for key in STARTERS}
