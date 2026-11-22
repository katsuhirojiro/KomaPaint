"""Local visual capture and the packaged Windows interaction receipt."""
import hashlib
import json
import sys
from pathlib import Path

import recipes
import render
from math import sin
from PySide6.QtCore import QObject, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest


def settle(ms=280):
    QTest.qWait(ms)


def shot(win, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    image = win.grabWindow()
    if not image.save(str(path), "PNG"):
        raise RuntimeError(f"Could not save {path}")
    print("captured", path)
    return image


def item(win, name):
    found = [x for x in win.findChildren(QObject, name) if isinstance(x, QQuickItem)]
    stack = [win.contentItem()]
    while stack:
        candidate = stack.pop()
        if not isinstance(candidate, QQuickItem):
            continue
        if candidate.objectName() == name and candidate not in found:
            found.append(candidate)
        stack.extend(candidate.childItems())
    for candidate in found:
        if candidate.isVisible() and candidate.window() is not None:
            return candidate
    return found[0] if found else None


def popup(win, name):
    found = win.findChildren(QObject, name)
    return found[0] if found else None


def digest(image):
    rgba = image.convertToFormat(QImage.Format.Format_RGBA8888)
    return hashlib.sha256(rgba.bits().tobytes()).hexdigest()


def pad_image(win):
    target = item(win, "padCanvas")
    if not target:
        raise RuntimeError("The drawing pad was not visible")
    return win.grabWindow().copy(
        int(target.mapToScene(target.position()).x()), int(target.mapToScene(target.position()).y()),
        int(target.width()), int(target.height()))


def draw_demo(s):
    for x0, x1, y, press in ((.12, .88, .40, .9), (.16, .78, .60, .65)):
        s.beginStroke(x0, y, press)
        for i in range(1, 40):
            t = i / 39
            s.moveStroke(x0 + (x1 - x0) * t, y + .035 * sin(t * 5.8), press)
        s.endStroke()
    settle(120)


def render_digest(s, key=None):
    image = render.allocate(720, 460)
    painter = QPainter(image)
    try:
        render.paint_surface(painter, s.doc, 720, 460, s.assets)
        strokes = render.sample_strokes("stroke", s.doc, 720, 460)
        for stroke in strokes:
            render.render_stroke(painter, s.doc, stroke, 720, 460, s.assets, image)
    finally:
        painter.end()
    return digest(image)


def capture(win, folder):
    out = Path(folder)
    s = render.STUDIO
    win.resize(1440, 900)
    settle(700)
    areas = ["stroke", "stabilise", "taper", "tip", "texture", "render", "wet",
             "colour", "dynamics", "spray", "modes", "secondary", "material",
             "properties", "construction", "quick", "assist"]
    names = ["stroke-path", "stabilisation", "taper-endings", "brush-tip",
             "texture-grain", "rendering-ink", "wet-mixing", "colour-dynamics",
             "input-dynamics", "spraying", "stroke-modes", "secondary-brushes",
             "materials-surface", "properties", "construction", "quick-access",
             "input-assistance"]
    for index, (area, name) in enumerate(zip(areas, names), start=1):
        s.setArea(area)
        if area == "construction":
            s.selectNode(s.doc["root"]["children"][1]["uid"])
        if area == "dynamics":
            s.selectBinding(s.doc["dynamics"][0]["id"])
        settle(260)
        shot(win, out / f"{index:02d}-{name}.png")

    s.setArea("stroke")
    s.setLayer("personal")
    s.setSetting("spacing", 42)
    s.togglePin("spacing")
    settle(250)
    shot(win, out / "18-personal-override.png")

    for index, mode in enumerate(["Mixing", "Pressure", "Texture", "Surface"], start=19):
        s.setArea("stroke")
        s.setTestMode(mode)
        settle(320)
        shot(win, out / f"{index:02d}-pad-{mode.lower()}.png")

    s.setTestMode("Stroke")
    s.setArea("tip")
    settle(200)
    tip = popup(win, "tipRoom")
    if tip:
        tip.open()
        settle(350)
        shot(win, out / "23-tip-room.png")
        tip.close()
        settle(150)

    s.setArea("construction")
    settle(200)
    reuse = popup(win, "reuseRoom")
    if reuse:
        reuse.open()
        settle(300)
        shot(win, out / "24-reusable-construction.png")
        reuse.close()
        settle(150)

    s.setArea("dynamics")
    settle(200)
    shot(win, out / "25-dynamics-large.png")

    win.resize(1024, 720)
    settle(500)
    s.setArea("wet")
    settle(300)
    shot(win, out / "26-compact-1024.png")
    win.resize(1440, 900)
    settle(300)


def walkthrough(win, folder, warnings):
    out = Path(folder)
    shots = out / "screenshots"
    checks = []

    def check(name, condition, detail=""):
        checks.append(dict(name=name, passed=bool(condition), detail=str(detail)))
        if not condition:
            print(f"CHECK FAILED: {name} {detail}", file=sys.stderr)
        return bool(condition)

    try:
        s = render.STUDIO
        capture(win, shots)
        win.resize(1280, 800)
        settle(400)
        s.setArea("stroke")
        s.setLayer("personal")
        s.setTestMode("Stroke")

        creator_spacing = s.doc["settings"]["spacing"]
        s.setSetting("spacing", 47)
        check("personal edit leaves the creator value intact",
              s.doc["settings"]["spacing"] == creator_spacing and s.doc["overrides"]["spacing"] == 47,
              s.doc["settings"]["spacing"])
        s.resetSetting("spacing")
        check("personal reset returns the creator value", "spacing" not in s.doc["overrides"])

        s.setLayer("creator")
        s.setSetting("spacing", 23)
        check("creator edit changes the definition", s.doc["settings"]["spacing"] == 23)
        s.undoEdit()
        check("brush undo restores the creator edit", s.doc["settings"]["spacing"] == creator_spacing)
        s.setLayer("personal")

        s.togglePin("opacity")
        s.setArea("quick")
        settle(200)
        check("pinned setting is visible in Quick Access",
              any(row["key"] == "opacity" for card in s.sections for row in card["settings"]))
        check("pin does not rewrite the creator", s.doc["settings"]["opacity"] == 88)
        s.togglePin("opacity")
        s.setArea("stroke")

        s.copySettings()
        s.pasteSettings()
        check("paste applies personal overrides", "opacity" in s.doc["overrides"])
        s.resetPersonal()
        check("reset personal clears overrides and pins",
              not s.doc["overrides"] and not s.doc["pins"])

        before = s.counts["mark"]
        s.setArea("construction")
        s.selectNode(s.doc["root"]["uid"])
        s.addNode("mark")
        check("add a mark", s.counts["mark"] == before + 1)
        before = s.counts["group"]
        s.addNode("group")
        check("add a group", s.counts["group"] == before + 1)
        before = s.counts["texture"]
        s.addNode("texture")
        check("add a texture", s.counts["texture"] == before + 1)

        before = s.counts["brush"]
        for _ in range(8):
            s.addSecondary()
        check("eight secondary brushes can be added without a two-brush limit",
              s.counts["brush"] == before + 8, s.counts["brush"])

        first_mark = next(node for node, _p, _d in recipes.walk(s.doc["root"]) if node["kind"] == "mark")
        s.selectNode(first_mark["uid"])
        before = s.counts["mark"]
        s.duplicateNode()
        check("duplicate a part", s.counts["mark"] == before + 1)
        s.removeNode()
        check("remove a part", s.counts["mark"] == before)

        marks = [node for node, _p, _d in recipes.walk(s.doc["root"]) if node["kind"] == "mark"]
        s.selectNode(marks[0]["uid"])
        s.setNodeTip("leaf")
        check("a mark's tip can be replaced", marks[0]["asset"] == "leaf")
        s.setNodeTip("graphite")

        s.beginTip(.2, .7)
        for i in range(1, 12):
            s.moveTip(.2 + .45 * i / 11, .7 - .35 * i / 11)
        s.endTip()
        drawn = s.makeDrawnTip()
        check("a hand-drawn tip becomes a stored mark", bool(drawn) and drawn in s.doc["tips"])

        s.setArea("dynamics")
        ident = s.ensureBinding("spacing")
        binding = s.bindingById(ident)
        s.setCurvePoint(ident, 1, .4, .8)
        changed = s.bindingById(ident)
        check("one shared curve accepts a new point", changed["points"][1]["y"] > .75)
        s.setBindingSignal(ident, "Distance")
        check("a binding can use any available signal", s.bindingById(ident)["signal"] == "Distance")
        s.applyPreset("p2")
        check("a reusable dynamic applies to the selected binding",
              s.bindingById(ident)["signal"] == "Velocity")
        s.setDevice("Mouse")
        check("mouse makes pressure unavailable rather than fake",
              not next(sig for sig in s.signals if sig["name"] == "Pressure")["available"])
        s.setDevice("Pen · simulated")

        s.setArea("stroke")
        base_digest = render_digest(s)
        s.setSetting("texture_strength", 0)
        s.setLayer("personal")
        s.setSetting("texture_strength", 0)
        plain = render_digest(s)
        s.setSetting("texture_strength", 85)
        textured = render_digest(s)
        check("texture visibly changes the rendered stroke", plain != textured, (plain[:12], textured[:12]))
        s.setSetting("texture_strength", 42)
        s.setLayer("personal")
        s.setSetting("wet_enabled", True)
        s.setChoice("wet_medium", "Watercolour")
        s.setSetting("wet_pickup", 0)
        dry = render_digest(s)
        s.setSetting("wet_pickup", 82)
        wet = render_digest(s)
        check("wet pickup visibly changes the rendered stroke", dry != wet)

        settle(200)
        before_digest = digest(pad_image(win))
        draw_demo(s)
        settle(300)
        after_digest = digest(pad_image(win))
        check("drawing changes the pad", before_digest != after_digest)
        count = len(s.strokes)
        s.undoPad()
        after_undo = len(s.strokes)
        check("test-pad undo removes only a test stroke", after_undo == count - 1,
              (count, after_undo))
        s.setSetting("opacity", 62)
        check("a brush edit survives test-pad undo",
              s.doc["overrides"].get("opacity") == 62)
        s.undoEdit()
        check("brush undo leaves test-pad state untouched", len(s.strokes) == after_undo)
        s.redoPad()
        check("test-pad redo is independent", len(s.strokes) == count)

        s.retainInput()
        check("retained input is stored with its settings",
              s.hasRetained and s.compare and len(s.retainedStrokes) == 1)
        s.clearRetained()
        check("retained input can be released", not s.hasRetained)

        areas = {area["key"] for area in s.areas}
        check("every required settings area has a home",
              {"stroke", "stabilise", "taper", "tip", "texture", "render", "wet", "colour",
               "dynamics", "spray", "modes", "secondary", "material", "properties"} <= areas)
        check("the settings catalogue covers every requested family",
              len(render.catalog.SETTINGS) >= 110, len(render.catalog.SETTINGS))
        vector = next(x for x in render.catalog.SETTINGS.values() if x["key"] == "property_vector_erase")
        check("vector erase is present but labelled organisation only",
              "property_vector_erase" not in render.catalog.RENDERED.get("properties", set())
              and vector["choices"] == ["Off", "Whole stroke", "Partial"])
        check("the preview only claims controls it composes",
              sum(len(v) for v in render.catalog.RENDERED.values()) < len(render.catalog.SETTINGS),
              sum(len(v) for v in render.catalog.RENDERED.values()))
        check("no Qt warnings during the walkthrough", not warnings, warnings[:4])

        win.resize(1280, 800)
        settle(300)
        shot(win, shots / "27-final-state.png")
        report = dict(passed=all(c["passed"] for c in checks), frozen=bool(getattr(sys, "frozen", False)),
                      qtPlatform=QGuiApplication.instance().platformName(),
                      architecture=64 if sys.maxsize > 2**32 else 32, checks=checks, qtWarnings=list(warnings))
        out.mkdir(parents=True, exist_ok=True)
        (out / "qt-walkthrough.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        return report["passed"]
    except Exception as exc:
        import traceback
        traceback.print_exc()
        out.mkdir(parents=True, exist_ok=True)
        (out / "qt-walkthrough.json").write_text(json.dumps(dict(
            passed=False, checks=checks, qtWarnings=list(warnings),
            exception=f"{exc}\n{traceback.format_exc()}"), indent=2), encoding="utf-8")
        return False
