"""The review's whole settings surface, defined once.

This is deliberately a catalogue, not a pile of bespoke panels.  Every area,
card, label, unit, range, explanation and advanced/ordinary level comes from
here, so the QML cannot grow a second, disagreeing set of names.  It also means
the isolated review can show the complete organisation without pretending each
control is already a production engine feature.
"""
from copy import deepcopy

AREAS = [
    ("stroke", "Stroke Path", "How the tip travels and how often it leaves a mark."),
    ("stabilise", "Stabilisation", "Input correction before the mark is placed."),
    ("taper", "Taper & Endings", "How a stroke opens, closes and finishes."),
    ("tip", "Brush Tip & Marks", "The shape, edge and orientation of each mark."),
    ("texture", "Texture & Grain", "A second image that breaks or carries the paint."),
    ("render", "Rendering & Ink", "Coverage, accumulation and the ink itself."),
    ("wet", "Wet Mixing", "Paint that moves, picks up and blends on the surface."),
    ("colour", "Colour Dynamics", "How ink varies, from mark to mark and across a stroke."),
    ("dynamics", "Input Dynamics", "One signal-to-setting system for the whole brush."),
    ("spray", "Spraying & Particles", "Scattered emission around the stroke path."),
    ("modes", "Stroke Modes", "How marks are ordered, repeated and combined."),
    ("secondary", "Secondary Brushes", "Independent brushes inside this brush."),
    ("material", "Materials & Surface", "Paper, canvas and physical media interaction."),
    ("properties", "Properties", "Limits, erasing and application-level behaviours."),
    ("construction", "Construction", "Marks, groups, textures and sub-brushes."),
    ("quick", "Quick Access", "The controls this artist pinned, kept apart from the brush."),
    ("assist", "Input Assistance", "This device only. Never saved into the brush."),
]


def S(key, label, default, minimum=0, maximum=100, unit="%", kind="slider",
      desc="", advanced=False, choices=None, effect="", step=1):
    return dict(key=key, label=label, default=default, minimum=minimum,
                maximum=maximum, unit=unit, kind=kind, desc=desc,
                advanced=advanced, choices=choices or [], effect=effect, step=step)


SETTINGS = {}


def _normalise_kind():
    """Keep the prose out of the control kind.

    A handful of entries were written with the explanation in the ``kind``
    slot.  Normalise once so every consumer can trust ``kind`` and read the
    sentence from ``desc``.
    """
    for item in SETTINGS.values():
        if item["kind"] in ("slider", "toggle", "choice", "text"):
            continue
        explanation = item["kind"]
        if not item["desc"]:
            item["desc"] = explanation
        if item["choices"]:
            item["kind"] = "choice"
        elif item["minimum"] == 0 and item["maximum"] == 1 and not item["unit"]:
            item["kind"] = "toggle"
        else:
            item["kind"] = "slider"


def add(area, items):
    for item in items:
        item["area"] = area
        SETTINGS[item["key"]] = item


# ---------------------------------------------------------------- stroke path
add("stroke", [
    S("spacing", "Spacing", 18, 5, 200, "%",
      "Distance between marks as a percentage of tip size.", effect="Mark spacing"),
    S("spacing_variation", "Spacing variation", 6, 0, 100, "%",
      "Change the gap from mark to mark instead of laying an even chain.", effect="Mark spacing"),
    S("jitter", "Position jitter", 0, 0, 100, "%",
      "Move each mark off the path. Independent of spacing variation.",
      advanced=True, effect="Mark position"),
    S("stamp_count", "Repeated stamps", 1, 1, 16, "×",
      "Place several marks at each spacing step.", effect="Stamps per step"),
    S("stamp_falloff", "Stamp fall-off", 0, 0, 100, "%",
      "Fade the extra repeated stamps away from the first.", advanced=True,
      effect="Extra stamps"),
    S("follow_path", "Marks follow the path", 1, 0, 1, "", "toggle",
      "Turn each mark to the direction the stroke is travelling.", effect="Mark rotation"),
    S("path_smoothing", "Path smoothing", 0, 0, 100, "%",
      "Even out small pointer wobbles before spacing is calculated.",
      advanced=True, effect="Stamp path"),
])

# --------------------------------------------------------------- stabilisation
add("stabilise", [
    S("stabilisation", "Stabilisation", 14, 0, 100, "%",
      "How strongly the drawn point trails and settles behind the pointer.", effect="Input path"),
    S("motion_filter", "Motion filtering", 8, 0, 100, "%",
      "Reject small high-frequency movements without adding distance lag.",
      advanced=True, effect="Input path"),
    S("speed_adjust", "Speed-based adjustment", 0, 0, 100, "%",
      "Change smoothing as the hand moves faster.", advanced=True, effect="Input path"),
    S("post_correction", "Post-stroke correction", 0, 0, 100, "%",
      "Fit the finished path; the preview is shown before the stroke is committed.",
      advanced=True, effect="Finished path"),
    S("sharp_angles", "Preserve sharp angles", 30, 0, 100, "%",
      "Keep deliberate corners and reversals instead of rounding them off.", effect="Corner shape"),
    S("guide_snap", "Ruler or guide snapping", "Off", kind="choice",
      choices=["Off", "Straight", "Ruler", "Ellipse", "Perspective"],
      desc="Snap only while a guide is present. Unsupported guides stay unavailable.",
      effect="Stroke path"),
])

# --------------------------------------------------------------------- taper
add("taper", [
    S("taper_pressure", "Pressure taper", 55, 0, 100, "%",
      "Shape the ends from the pressure actually reported by the device.", effect="Stroke ends"),
    S("taper_touch", "Touch taper", 30, 0, 100, "%",
      "Shorten or soften the first and last contact, independent of pressure.", effect="Stroke ends"),
    S("taper_size", "Size taper", 65, 0, 100, "%",
      "Narrow the tip at the opening and closing ends.", effect="Mark size"),
    S("taper_opacity", "Opacity taper", 50, 0, 100, "%",
      "Fade coverage at the ends instead of narrowing the mark.", effect="Mark opacity"),
    S("taper_flow", "Flow taper", 35, 0, 100, "%",
      "Reduce how much ink each end mark deposits.", advanced=True, effect="Ink per mark"),
    S("tip_shaping", "Tip shaping", 18, 0, 100, "%",
      "Round or sharpen the taper profile itself.", advanced=True, effect="Taper profile"),
    S("start_behaviour", "Start behaviour", "Pointed", kind="choice",
      choices=["Pointed", "Round", "Flat", "Full contact", "Skipped"],
      desc="What the first few marks do when the pen touches down.", effect="Stroke opening"),
    S("end_behaviour", "End behaviour", "Lift", kind="choice",
      choices=["Lift", "Taper", "Flat", "Hold", "Release"],
      desc="What happens as the pen leaves the surface.", effect="Stroke close"),
    S("distance_effects", "Distance-based effects", 0, 0, 100, "%",
      "Change size or coverage after a set distance along the stroke.",
      advanced=True, effect="Long-stroke shape"),
    S("speed_effects", "Speed-based effects", 0, 0, 100, "%",
      "Thin or lighten the mark as the hand moves faster.",
      advanced=True, effect="Stroke weight"),
])

# -------------------------------------------------------------- brush tip
add("tip", [
    S("tip_size", "Size", 54, 2, 420, "px",
      "Base diameter of the mark at full contact.", effect="Mark size"),
    S("tip_min_size", "Minimum size", 18, 1, 100, "%",
      "Smallest fraction of the tip size reached by pressure.", effect="Mark size"),
    S("tip_hardness", "Hardness", 74, 0, 100, "%",
      "Crisp edge to a soft falloff. Bitmap marks keep their own alpha unless filtered.",
      effect="Mark edge"),
    S("tip_aspect", "Thickness", 100, 25, 400, "%",
      "Width against height. Below 100 is a flat, calligraphic tip.", effect="Mark shape"),
    S("tip_angle", "Angle", 0, -180, 180, "°",
      "Fixed rotation of the tip before direction or tilt is added.", effect="Mark angle"),
    S("shape_filter", "Shape filtering", 0, 0, 100, "%",
      "Cut faint alpha from imported and drawn marks before they are stamped.",
      advanced=True, effect="Mark alpha"),
    S("mark_variation", "Independent mark variation", 0, 0, 100, "%",
      "Vary size, opacity and angle independently for every mark, not for the stroke.",
      advanced=True, effect="Mark variation"),
    S("flip_h", "Flip horizontally", 0, 0, 1, "", "toggle",
      "Mirror every mark around its vertical axis.", effect="Mark orientation"),
    S("flip_v", "Flip vertically", 0, 0, 1, "", "toggle",
      "Mirror every mark around its horizontal axis.", advanced=True, effect="Mark orientation"),
    S("tip_rotation_source", "Rotate from", "Direction", kind="choice",
      choices=["Off", "Direction", "Tilt", "Barrel roll", "Random"],
      desc="One rotation decision for the whole brush. Unavailable device inputs are disabled.",
      effect="Mark angle"),
])

# ------------------------------------------------------------------ texture
add("texture", [
    S("texture_source", "Texture source", "Cold press", kind="choice",
      choices=["None", "Cold press", "Rough paper", "Cotton", "Linen", "Flecks", "Fine mist", "Edge breakup", "Imported"],
      desc="A texture is an independent grayscale image, not the brush tip.", effect="Coverage"),
    S("texture_strength", "Strength", 42, 0, 100, "%",
      "How deeply the texture removes or tints paint.", effect="Coverage"),
    S("texture_scale", "Scale", 100, 5, 400, "%",
      "Size of the texture pattern relative to the canvas.", effect="Grain size"),
    S("texture_rotation", "Rotation", 0, -180, 180, "°",
      "Rotate the texture independently of the mark.", effect="Grain direction"),
    S("texture_depth", "Depth", 70, 0, 100, "%",
      "Cut through the mark (low) or only tint its edges (high).", effect="Grain contrast"),
    S("texture_contrast", "Contrast", 100, 0, 200, "%",
      "Expand or compress the texture's tonal range.", advanced=True, effect="Grain contrast"),
    S("texture_offset_x", "Offset X", 0, -100, 100, "%",
      "Shift the pattern inside its anchor.", advanced=True, effect="Grain position"),
    S("texture_offset_y", "Offset Y", 0, -100, 100, "%",
      "Shift the pattern inside its anchor.", advanced=True, effect="Grain position"),
    S("texture_movement", "Movement", 0, 0, 100, "%",
      "Let the texture crawl with the stroke instead of staying fixed.", advanced=True,
      effect="Grain position"),
    S("texture_anchor", "Anchor", "Canvas", kind="choice",
      choices=["Canvas", "Stroke", "Per stamp"],
      desc="Canvas keeps the grain still as the hand moves; Stroke carries it along.",
      effect="Grain position"),
    S("texture_per_stamp", "Apply per stamp", 0, 0, 1, "", "toggle",
      "Evaluate the texture for each mark rather than over the finished group.",
      advanced=True, effect="Grain structure"),
    S("texture_tiling", "Repeat pattern", 1, 0, 1, "", "toggle",
      "Tile the source when the brush is larger than the texture.",
      advanced=True, effect="Grain coverage"),
    S("texture_filter", "Filtering", "Smooth", kind="choice",
      choices=["Crisp", "Smooth", "Film grain"],
      desc="How the texture is sampled when scaled.", advanced=True, effect="Grain edge"),
])

# ------------------------------------------------------------------- render
add("render", [
    S("opacity", "Opacity", 88, 0, 100, "%",
      "Final coverage of the whole stroke.", effect="Overall ink"),
    S("flow", "Flow", 62, 0, 100, "%",
      "Ink deposited by each mark before accumulation.", effect="Ink per mark"),
    S("density", "Density", 100, 1, 200, "%",
      "Multiplier on how much ink each mark carries.", advanced=True, effect="Ink per mark"),
    S("buildup", "Buildup", 1, 0, 1, "", "toggle",
      "Let repeated marks accumulate to full opacity.", effect="Ink accumulation"),
    S("wet_edges", "Wet edges", 0, 0, 100, "%",
      "Darken and pull paint towards the rim of the stroke.", effect="Stroke rim"),
    S("burnt_edges", "Burnt edges", 0, 0, 100, "%",
      "Dry, darkened edge where the mark meets the paper.", effect="Stroke rim"),
    S("alpha_threshold", "Alpha threshold", 0, 0, 100, "%",
      "Discard faint coverage instead of keeping a soft veil.", advanced=True,
      effect="Mark edge"),
    S("layer_blend", "Stroke blend", "Normal", kind="choice",
      choices=["Normal", "Multiply", "Screen", "Overlay", "Soft light", "Erase"],
      desc="How the finished stroke meets the artwork beneath it.", effect="Composite"),
    S("antialias", "Antialiasing", 1, 0, 1, "", "toggle",
      "Feather the mark edge. Off gives deliberately hard pixels.",
      advanced=True, effect="Mark edge"),
    S("ink_limit", "Ink limit", 100, 10, 100, "%",
      "Stop a mark from becoming denser than this.", advanced=True, effect="Ink accumulation"),
])

# ---------------------------------------------------------------------- wet
add("wet", [
    S("wet_enabled", "Wet mixing", 1, 0, 1, "", "toggle",
      "Paint behaves as a wet material rather than a dry digital mark.",
      effect="Ink behaviour"),
    S("wet_medium", "Physical behaviour", "Watercolour", kind="choice",
      choices=["Watercolour", "Gouache", "Ink", "Oil", "Off"],
      desc="Supported media change spreading, pickup and drying. Unsupported media show why.",
      effect="Ink behaviour"),
    S("wet_dilution", "Dilution", 26, 0, 100, "%",
      "Water in the mark. More water means paler, more mobile paint.", effect="Ink body"),
    S("wet_charge", "Paint charge", 72, 0, 100, "%",
      "Paint carried into the stroke before it needs to pick up more.", effect="Reservoir"),
    S("wet_pickup", "Pickup / attack", 34, 0, 100, "%",
      "How eagerly the brush lifts colour already on the surface.", effect="Colour pickup"),
    S("wet_pull", "Pull / smear", 28, 0, 100, "%",
      "How far picked-up paint is dragged along the stroke.", effect="Ink movement"),
    S("wet_stretch", "Colour stretch", 0, 0, 100, "%",
      "Stretch the picked-up colour along the direction of travel.", advanced=True,
      effect="Ink movement"),
    S("wet_mixing", "Mixing rate", 45, 0, 100, "%",
      "How quickly the reservoir colour and the picked-up colour combine.", effect="Colour blend"),
    S("wet_blur", "Wet blur", 0, 0, 100, "%",
      "Diffuse the edge as water moves into the paper.", advanced=True, effect="Stroke edge"),
    S("wet_variation", "Wetness variation", 0, 0, 100, "%",
      "Change wetness along the stroke.", advanced=True, effect="Ink behaviour"),
    S("wet_drying", "Drying", 50, 0, 100, "%",
      "How quickly the delivered paint stops moving.", effect="Ink behaviour"),
])

# ------------------------------------------------------------------- colour
add("colour", [
    S("hue_variation", "Hue variation", 0, -180, 180, "°",
      "Rotate hue per stroke or per mark.", effect="Ink colour"),
    S("saturation_variation", "Saturation variation", 0, 0, 100, "%",
      "Change colour intensity independently of brightness.", effect="Ink colour"),
    S("brightness_variation", "Brightness variation", 9, 0, 100, "%",
      "Lighten or darken each mark.", effect="Ink tone"),
    S("colour_secondary", "Secondary ink", 0, 0, 100, "%",
      "Mix a second stored colour into the stroke.", effect="Ink colour"),
    S("colour_input", "Input-driven colour", "Pressure", kind="choice",
      choices=["Off", "Pressure", "Tilt", "Speed", "Direction", "Random"],
      desc="Uses the same dynamics system; no separate colour-response engine.",
      effect="Ink colour"),
    S("colour_scope", "Apply variation", "Per stamp", kind="choice",
      choices=["Per stamp", "Per stroke"],
      desc="One decision per mark, or one for the whole stroke.", effect="Ink colour"),
])

# ---------------------------------------------------------------- dynamics
add("dynamics", [
    S("dynamics_strength", "Global dynamics strength", 100, 0, 100, "%",
      "The artist's own strength on top of every binding. Does not alter the creator's curves.",
      effect="All dynamic settings"),
    S("dynamics_smoothing", "Signal smoothing", 12, 0, 100, "%",
      "Shared smoothing for input signals before any curve is evaluated.",
      advanced=True, effect="Signal response"),
    S("dynamics_fallback", "Missing input fallback", "Distance", kind="choice",
      choices=["Distance", "Speed", "Random", "Constant"],
      desc="What an unsupported signal uses on this device. Never silently reports zero.",
      effect="Signal response"),
    S("dynamics_per_stamp", "Evaluate per stamp", 1, 0, 1, "", "toggle",
      "Default scope for new bindings; each binding can still choose its own.",
      effect="Signal response"),
])

# -------------------------------------------------------------------- spray
add("spray", [
    S("spray_enabled", "Spray emission", 0, 0, 1, "", "toggle",
      "Emit particles around the path in addition to the main mark.", effect="Marks"),
    S("spray_count", "Particles", 4, 1, 64, "×",
      "Particles emitted at each spacing step.", effect="Marks"),
    S("spray_size", "Particle size", 35, 1, 100, "%",
      "Particle size as a fraction of the tip.", effect="Particle size"),
    S("spray_density", "Density", 60, 0, 100, "%",
      "How many emitted particles actually leave ink.", effect="Particle count"),
    S("spray_scatter", "Scatter", 55, 0, 200, "%",
      "Radius around the path that particles occupy.", effect="Particle position"),
    S("spray_deviation", "Spray deviation", 20, 0, 100, "%",
      "Random change in particle direction.", advanced=True, effect="Particle direction"),
    S("spray_direction", "Direction", "Radial", kind="choice",
      choices=["Radial", "Forward", "Backward", "Up", "Down"],
      desc="Where particles are thrown from the path.", effect="Particle direction"),
    S("spray_continuous", "Continuous emission", 0, 0, 1, "", "toggle",
      "Keep emitting between spacing steps while contact is held.",
      effect="Particle timing"),
    S("spray_independent", "Independent variation", 1, 0, 1, "", "toggle",
      "Each particle gets its own size, tone and rotation.", effect="Particle variation"),
])

# --------------------------------------------------------------- stroke modes
add("modes", [
    S("stroke_mode", "Primary mode", "Continuous", kind="choice",
      choices=["Continuous", "Repeated stamp", "Ordered sequence", "Random", "Weighted", "Ribbon", "Alternating", "Continuous spray"],
      desc="How the brush places marks. Modes select behaviour; they do not cap parts.",
      effect="Mark placement"),
    S("mode_step", "Sequence step", 1, 1, 32, "×",
      "How many marks before a sequence advances.", effect="Mark order"),
    S("mode_reverse", "Reverse order", 0, 0, 1, "", "toggle",
      "Play the sequence backwards.", effect="Mark order"),
    S("mode_alternate", "Alternate direction", 0, 0, 1, "", "toggle",
      "Flip direction on each pass.", effect="Mark orientation"),
    S("mode_weighted", "Use weights", 1, 0, 1, "", "toggle",
      "Weighted selection uses each mark's weight.", effect="Mark choice"),
    S("mode_ribbon", "Ribbon follows path", 1, 0, 1, "", "toggle",
      "Bend marks along the path instead of only rotating them.",
      effect="Mark shape"),
    S("mode_seed", "Variation seed", 137, 0, 9999, "",
      "Repeats a random or weighted arrangement exactly.", advanced=True,
      effect="Mark choice"),
])

# ------------------------------------------------------------------ material
add("material", [
    S("surface", "Surface", "Cold press", kind="choice",
      choices=["Cold press", "Rough paper", "Hot press", "Smooth paper", "Linen canvas", "Cotton canvas", "Non-absorbent palette"],
      desc="A canvas-wide surface, not a brush texture. Changing it affects every stroke.",
      effect="Surface"),
    S("surface_scale", "Surface scale", 100, 10, 300, "%",
      "Size of the paper or weave pattern.", effect="Surface grain"),
    S("surface_tooth", "Tooth", 38, 0, 100, "%",
      "How strongly the surface breaks up the mark.", effect="Mark coverage"),
    S("surface_absorbency", "Absorbency", 46, 0, 100, "%",
      "How quickly water leaves the mark.", effect="Ink behaviour"),
    S("surface_grain", "Surface grain", 50, 0, 100, "%",
      "Tonal grain visible beneath the paint.", effect="Surface"),
    S("material_interaction", "Brush-material interaction", 45, 0, 100, "%",
      "How much the brush responds to tooth, absorbency and wetness.", effect="Mark coverage"),
    S("surface_wet", "Wet simulation", 1, 0, 1, "", "toggle",
      "Let delivered paint continue to spread and dry.", effect="Ink movement"),
    S("surface_drying", "Drying speed", 50, 0, 100, "%",
      "How fast a wet stroke settles.", effect="Ink movement"),
    S("surface_granulation", "Granulation", 18, 0, 100, "%",
      "Pigment settling into the tooth of the paper.", effect="Ink texture"),
    S("surface_edge", "Edge accumulation", 12, 0, 100, "%",
      "Pigment gathering at the rim of a drying mark.", effect="Stroke rim"),
])

# ---------------------------------------------------------------- properties
add("properties", [
    S("property_max_size", "Maximum size", 420, 1, 4096, "px",
      "Limit the brush's own size without changing the tip definition.", effect="Mark size"),
    S("property_min_size", "Minimum size", 1, 1, 512, "px",
      "Smallest size this brush may use.", effect="Mark size"),
    S("property_screen_size", "Screen-relative size", 0, 0, 1, "", "toggle",
      "Keep the mark the same size on screen when the canvas is zoomed.",
      effect="Mark size"),
    S("property_eraser", "Eraser", 0, 0, 1, "", "toggle",
      "Subtract coverage from the active layer instead of adding ink.",
      effect="Stroke result"),
    S("property_vector_erase", "Vector erase", "Off", kind="choice",
      choices=["Off", "Whole stroke", "Partial"],
      desc="Requires a vector layer. Not available on raster layers.", effect="Stroke result"),
    S("property_overflow", "Anti-overflow", 0, 0, 1, "", "toggle",
      "Keep paint inside the active selection or protected boundary.",
      effect="Stroke bounds"),
    S("property_reference", "Reference layer", "Active layer", kind="choice",
      choices=["Active layer", "All visible", "Selected layers", "None"],
      desc="Which artwork anti-overflow and colour pickup may read.", effect="Stroke bounds"),
    S("property_watercolour", "Watercolour edges", 0, 0, 1, "", "toggle",
      "Allow pigment to collect at the rim of a wet mark.", effect="Stroke rim"),
    S("property_alpha_lock", "Preserve layer alpha", 0, 0, 1, "", "toggle",
      "Paint colour without changing existing transparency.", effect="Stroke result"),
    S("property_persistent", "Keep the reservoir", 0, 0, 1, "", "toggle",
      "Carry unused paint into the next stroke.", effect="Reservoir"),
])

# ---------------------------------------------------------------- assistance
ASSIST = [
    S("assist_device", "Active device", "Pen · simulated", kind="choice",
      choices=["Pen · simulated", "Mouse", "Touch"],
      desc="Only signals the selected device can report are offered. Others use a declared fallback.",
      effect="All input"),
    S("assist_smoothing", "Application smoothing", 18, 0, 100, "%",
      "This artist's own stabiliser. Applied after the brush, before the stroke is stored.",
      effect="Input path"),
    S("assist_speed", "Speed compensation", 0, 0, 100, "%",
      "Correct pointer lag at speed for this device.", effect="Input path"),
    S("assist_pressure", "Pressure calibration", 50, 0, 100, "%",
      "Mid-point of this device's pressure curve; the brush curve is unchanged.",
      effect="Pressure signal"),
    S("assist_fallback", "Fallback pressure", 65, 0, 100, "%",
      "Used by a mouse or a pen without pressure reporting.", effect="Pressure signal"),
    S("assist_tilt", "Tilt input", 1, 0, 1, "", "toggle",
      "Only shown when the selected device reports tilt.", effect="Tilt signal"),
    S("assist_roll", "Barrel roll", 1, 0, 1, "", "toggle",
      "Only shown when the device reports rotation.", effect="Rotation signal"),
    S("assist_hover", "Hover preview", 1, 0, 1, "", "toggle",
      "Show the footprint before contact; requires hover reporting.", effect="Cursor"),
    S("assist_catchup", "Stroke catch-up", 1, 0, 1, "", "toggle",
      "Draw the path that arrived between pointer events.", effect="Input path"),
]

for item in ASSIST:
    item["area"] = "assist"
    SETTINGS[item["key"]] = item

# Section cards: (area, title, note, keys)
SECTIONS = [
    ("stroke", "Stamp placement", "The cadence of marks along the path.",
     ["spacing", "spacing_variation", "stamp_count", "stamp_falloff", "follow_path"]),
    ("stroke", "Path", "Correction shared by every mark on the stroke.",
     ["path_smoothing", "jitter"]),
    ("stabilise", "Input correction", "Applies to this brush only. The application-level stabiliser lives under Input Assistance.",
     ["stabilisation", "motion_filter", "speed_adjust", "post_correction"]),
    ("stabilise", "Corner and guide handling", "Keep intent; snap only when a guide exists.",
     ["sharp_angles", "guide_snap"]),
    ("taper", "Pressure and touch", "How contact at the ends shapes the mark.",
     ["taper_pressure", "taper_touch", "tip_shaping"]),
    ("taper", "What tapers", "Size, opacity and flow are independent decisions.",
     ["taper_size", "taper_opacity", "taper_flow"]),
    ("taper", "Start and end", "The first and last marks are often the whole character of a stroke.",
     ["start_behaviour", "end_behaviour", "distance_effects", "speed_effects"]),
    ("tip", "Footprint", "The physical tip the marks are cut from.",
     ["tip_size", "tip_min_size", "tip_hardness", "tip_aspect", "tip_angle"]),
    ("tip", "Marks", "How independent marks are filtered, mirrored and turned.",
     ["shape_filter", "mark_variation", "flip_h", "flip_v", "tip_rotation_source"]),
    ("texture", "Grain source", "An image independent of the tip and of the surface.",
     ["texture_source", "texture_strength", "texture_scale", "texture_rotation"]),
    ("texture", "Application", "Where the grain is anchored and how it meets the paint.",
     ["texture_anchor", "texture_depth", "texture_contrast", "texture_per_stamp",
      "texture_movement", "texture_tiling", "texture_filter", "texture_offset_x", "texture_offset_y"]),
    ("render", "Coverage", "How much ink a mark carries and how marks build.",
     ["opacity", "flow", "density", "buildup", "ink_limit"]),
    ("render", "Edge and composite", "What happens at the boundary of the stroke.",
     ["wet_edges", "burnt_edges", "alpha_threshold", "layer_blend", "antialias"]),
    ("wet", "Charge", "What the brush carries to the surface.",
     ["wet_enabled", "wet_medium", "wet_dilution", "wet_charge", "wet_drying"]),
    ("wet", "Movement", "How paint already on the surface joins the stroke.",
     ["wet_pickup", "wet_pull", "wet_stretch", "wet_mixing", "wet_blur", "wet_variation"]),
    ("colour", "Variation", "Colour follows the same dynamics system as size and opacity.",
     ["hue_variation", "saturation_variation", "brightness_variation", "colour_scope"]),
    ("colour", "Second ink", "A stored second colour, mixed by the stroke.",
     ["colour_secondary", "colour_input"]),
    ("dynamics", "System", "Every binding points a signal at any setting. There is one curve editor.",
     ["dynamics_strength", "dynamics_smoothing", "dynamics_fallback", "dynamics_per_stamp"]),
    ("spray", "Emission", "Particles are marks from the same construction system.",
     ["spray_enabled", "spray_count", "spray_size", "spray_density", "spray_scatter"]),
    ("spray", "Direction and timing", "Where and when particles are thrown.",
     ["spray_direction", "spray_deviation", "spray_continuous", "spray_independent"]),
    ("modes", "Arrangement", "A mode changes how the existing parts are chosen, never how many parts exist.",
     ["stroke_mode", "mode_step", "mode_reverse", "mode_alternate", "mode_weighted",
      "mode_ribbon", "mode_seed"]),
    ("material", "Surface", "Canvas-wide. Changing it is not a brush edit.",
     ["surface", "surface_scale", "surface_tooth", "surface_absorbency", "surface_grain"]),
    ("material", "Physical response", "Supported media only; unavailable behaviour is explained, not faked.",
     ["material_interaction", "surface_wet", "surface_drying", "surface_granulation", "surface_edge"]),
    ("properties", "Limits", "Protect the brush's usable range.",
     ["property_max_size", "property_min_size", "property_screen_size", "property_persistent"]),
    ("properties", "Erase and boundaries", "Application behaviour attached to this brush.",
     ["property_eraser", "property_vector_erase", "property_overflow", "property_reference",
      "property_watercolour", "property_alpha_lock"]),
    ("assist", "Device", "The third ownership layer. Read by this application; never written into the brush.",
     ["assist_device", "assist_pressure", "assist_fallback"]),
    ("assist", "Stroke assistance", "This artist, this device. The creator's brush is untouched.",
     ["assist_smoothing", "assist_speed", "assist_catchup"]),
    ("assist", "Signals", "Unavailable device inputs are disabled with a reason.",
     ["assist_tilt", "assist_roll", "assist_hover"]),
]

# Assign a section title to each setting for the flat tooltip/lookup.
SECTION_OF = {}
for area, title, note, keys in SECTIONS:
    for key in keys:
        SECTION_OF[key] = title

# Signals the shared dynamics system can read, with their device requirements.
SIGNALS = [
    ("Pressure", "Pen with pressure", "pressure"),
    ("Tilt", "Pen with tilt", "tilt"),
    ("Direction", "Any pointer", "direction"),
    ("Velocity", "Any pointer", "velocity"),
    ("Distance", "Any pointer", "distance"),
    ("Random", "Always", "random"),
    ("Time", "Always", "time"),
    ("Progress", "Always", "progress"),
    ("Rotation", "Pen with barrel roll", "rotation"),
    ("Hover", "Device with hover", "hover"),
]

SIGNAL_REQUIREMENT = {name: need for name, label, need in SIGNALS}

# What each area actually changes in the isolated preview. The review is
# honest: the "organisation only" lane is visible in the interface.
_PREVIEW_USED = {
    "antialias", "brightness_variation", "burnt_edges", "colour_input", "colour_scope",
    "colour_secondary", "density", "distance_effects", "dynamics_strength", "end_behaviour",
    "flip_h", "flip_v", "flow", "follow_path", "hue_variation", "ink_limit", "jitter",
    "layer_blend", "mark_variation", "motion_filter", "mode_ribbon", "mode_step", "mode_weighted", "opacity",
    "path_smoothing", "property_eraser", "property_max_size", "property_min_size", "saturation_variation",
    "shape_filter", "sharp_angles", "spacing", "spacing_variation", "speed_effects",
    "spray_count", "spray_density", "spray_enabled", "spray_scatter", "stamp_count", "stabilisation",
    "stamp_falloff", "start_behaviour", "stroke_mode", "surface", "surface_grain",
    "surface_scale", "taper_flow", "taper_opacity", "taper_size", "texture_strength",
    "tip_angle", "tip_aspect", "tip_hardness", "tip_min_size", "tip_rotation_source",
    "tip_shaping", "tip_size", "wet_blur", "wet_dilution", "wet_edges", "wet_enabled",
    "wet_medium", "wet_mixing", "wet_pickup", "wet_pull", "wet_variation",
}

# Only controls the preview compositor actually reads are allowed to claim
# "affects preview". Everything else is deliberately shown as organisation
# only, so the review cannot flatter a control the production engine lacks.
RENDERED = {}
for _key in _PREVIEW_USED:
    _meta = SETTINGS.get(_key)
    if _meta:
        RENDERED.setdefault(_meta["area"], set()).add(_key)

# Editor-facing summaries of the feature families. Used by the review's
# "what this does not fake" popover, not by the renderer.
LIMITS = {
    "organisation": "This review shows where a setting belongs and how it is owned. The painter is a preview compositor, not the production renderer.",
    "honest": "Unavailable device inputs, unsupported media and editor-only settings are visibly disabled or labelled. Nothing here claims to be implemented in the production engine.",
}


def setting(key):
    return SETTINGS[key]


def defaults():
    return {key: item["default"] for key, item in SETTINGS.items()}


def clone(items):
    return deepcopy(items)


_normalise_kind()
